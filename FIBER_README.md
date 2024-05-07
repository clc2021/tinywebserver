5.2.2. m:n 线程:协程模型
最初设计中 TinyRPC 框架是 1:n 线程:协程模型的，即一个线程对于 n 个协程。
每个线程有单独的协程池，线程只会 Resume 属于它自己协程池里面的协程，各个 IO 线程之前的协程互相不干扰。
然而 1:n 模型可能会增加请求的时延。例如当某个 IO 线程在处理请求时，耗费了太多的时间，导致此 IO 线程的
其他请求得不到及时处理，只能阻塞等待。
因此 TinyRPC 框架使用 m:n 线程:协程模型进行了重构。所谓 m:n 即 m 个线程共同调度 n 个协程。由于 m 个
线程共用一个协程池，因此协程池里的就绪任务总会尽快的被 Resume。
一般来说，每一个客户端连接对象 TcpConnection, 对应一个协程。对客户端连接的 读数据、业务处理、写数据
这三步，其实都在这个协程中完成的。对于 m:n 协程模型 来说，一个 TcpConnection对象所持有的协程，可能会
来回被多个不同的IO线程调度。
举个例子，协程 A 可能先由 IO线程1 Resume，然后协程 A Yield后，下一次又被 IO线程2 Resume 唤醒。
因此，在实现业务逻辑的时候，要特别谨慎使用线程局部变量(thread_local)。因为对当前协程来说，可能
执行此协程的线程都已经变了，那对于的线程局部变量当然也会改变。
当然，一个协程任一时刻只会被一个线程来调度，不会存在多个 IO 线程同时 Resume 同一个协程的情况。
这一点由 TinyRPC 框架保证。
不过，m:n 模型也引入了更强的线程竞争条件，所以对协程池加互斥锁是必须的。
————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
问题编号0001：
这是http_conn类中有关注册事件的函数：
//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) // ev是现在已有的事件
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

这是使用到上述函数的成员函数：
使用了addfd()：
//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;// doc_root=/home/clab/Downloads/tinytime/root
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}
使用了modfd():
bool http_conn::write()
{
    int temp = 0;
    int newadd = 0;
    
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp >= 0) {
            bytes_have_send += temp;
            newadd = bytes_have_send - m_write_idx;
        }
        else {
            if (errno == EAGAIN) {
                if (bytes_have_send >= m_iv[0].iov_len) {
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_address + newadd;
                    m_iv[1].iov_len = bytes_to_send;
               } else {
                m_iv[0].iov_base = m_write_buf + bytes_have_send;
                m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
              }
              modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
              return true;
           }
           unmap();
           return false;
       }
       bytes_to_send -= temp;
       if (bytes_to_send <= 0) { 
        unmap();
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        if (m_linger) {
            init();
            return true;
        } else {
            return false;
        }
       }
    }
}
void http_conn::process() 
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) //NO_REQUEST，表示请求不完整，需要继续接收请求数据
    {
        // modfd(int epollfd, int fd, int ev, int TRIGMode)
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); //注册并监听读事件
        return;
    }
    bool write_ret = process_write(read_ret); //调用process_write完成报文响应
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); //注册并监听写事件
}
使用了removefd():
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
请问如何用IOManager类的方法对上述函数进行修改，即：不调用http_conn类自定义的addfd()，modfd()和removefd()，而是使用IOManager类的成员函数？


问题编号0010：
IOManager类的构造函数中，有：m_epfd = epoll_create(5000);
还有有关事件处理的函数：
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {  
    // 找到fd对应的FdContext，如果不存在，那就分配一个
    // fd对应的上下文记为fd_ctx
    FdContext *fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    // 同一个fd不允许重复添加相同的事件
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (__builtin_expect(fd_ctx->events & event,0)) {
        std::cout << "IOManager中, addEvent assert fd=" << fd << " event=" << (EPOLL_EVENTS)event << " fd_ctx.event=" << 
        (EPOLL_EVENTS)fd_ctx->events << std::endl;
        assert(!(fd_ctx->events & event));
    }

    // 将新的事件加入epoll_wait，使用epoll_event的私有指针存储FdContext的位置
    // 不是NONE的话改变，是NONE的话添加
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD; 
    epoll_event epevent;
    epevent.events   = EPOLLET | fd_ctx->events | event;
    // 这就是用epoll_ctl的最后一个参数和epoll_wait的第二个参数数组的.data.ptr来存储文件描述符上下文
    epevent.data.ptr = fd_ctx; 

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        std::cout << "IOManager中, epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):" << 
        rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events << std:: endl;
        return -1;
    }

    // 待执行IO事件数加1
    ++m_pendingEventCount;

    // 找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进行赋值
    fd_ctx->events                     = (Event)(fd_ctx->events | event);
    // getEventContext()会返回read或write
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event); // 这里有scheduler, fiber和cb
    assert(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

    // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执行体
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) {
        event_ctx.cb.swap(cb);
    } else {
        event_ctx.fiber = Fiber::GetThis();
        assert(event_ctx.fiber->getState() == Fiber::RUNNING);
    }
    return 0;
}
如果绕开addEvent()，直接向m_epfd注册事件，会不会出现什么问题？


问题编号0011：
我的意思是：modfd()除了修改事件，更重要的是它还把事件注册成one_shot的：
if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
但是在addEvent()中，注册事件并没有用one_shot：
epevent.events   = EPOLLET | fd_ctx->events | event;
所以使用addEvent()直接代替modfd()会不会有问题？

问题0100：
那么IOManager类如何保证事件只被处理一次？

问题0101:
在http_conn的write()函数里：
这里为什么使用modfd()？请详细说明解释。

问题0110：
如果对某个fd，先调用addEvent(fd, IOManager::READ)进行注册，然后再调用modfd(epollfd, fd, EPOLLIN, trigmode)
将事件注册修改为one_shot，会有什么问题？ 

问题0111：
对于addEvent()，这里epoll_ctl注册的事件为： epevent.events   = EPOLLET | fd_ctx->events | event;
但是在fd_ctx中更新的事件为：fd_ctx->events                     = (Event)(fd_ctx->events | event);
这两个地方事件是不是不同？这种差异会不会导致问题？
—————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
2023_12_8
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{ //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void modfd(int epollfd, int fd, int ev, int TRIGMode) // ev是现在已有的事件
{ //将事件重置为EPOLLONESHOT
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

2023_12_5
fiber之前有epoll_ctl的：(除掉时间轮和原本的webserver.cpp)
http_conn中的removefd()[http_conn的close_conn()使用], 
    addfd()[在http_conn的init()使用],
    modfd()[在http_conn的write()， process()使用]

Utils的addfd(), cb_func()。

关于iomanager的epoll_ctl操作(包括scheduler)
除了几个Event函数，还有idle。
scheduler完！全！没！有！epoll有关的操作。
http_conn只！有！epoll_ctl！操作。
数据库也完全没有epoll相关操作。


—————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
2023/11/30
可以根据gpt提出的在dealclientdata末尾加上runWorker调度试试，从这开始。

2023/11/25
1. WebServer(); 不用改
2. ~WebServer(); 已改
3. init(int port ,...) 已改，初始化
4. sql_pool(); 不用改
5. log_write(); 不用改
6. trig_mode(); 不用改
7. eventListen(); 
8. timer(int connfd, struct sockaddr_in client_address);
9. deal_timer(tw_timer *timer, int sockfd);
10. eventLoop(); 
11. dealclinetdata();
12. dealwithsignal(bool& timeout, bool& stop_server);
13. dealwithread(int sockfd);
14. dealwithwrite(int sockfd); 
——————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
2023/11/23
chatgpt知道：http_conn，Webserver类,IOManager类

有epoll_create()的几个地方：
1. eventListen()，创建m_epollfd
2. IOManager类的构造，创建m_epfd

有ctl的几个地方：
1. http里定义的无关http的函数：
addfd(int epollfd, int fd, bool one_shot, int TRIGMode)：EPOLL_CTL_ADD     
removefd(int epollfd, int fd)：EPOLL_CTL_DEL
modfd(int epollfd, int fd, int ev, int TRIGMode)：EPOLL_CTL_MOD
http的init()会用addfd()，close_conn()会用removefd()，process()和write()会用modfd()
——>拓：关于主类中的users：
构造函数：users = new http_conn[MAX_FD];
eventListen()：http_conn::m_epollfd = m_epollfd;
timer()：users[connfd].init()，这里就有addfd()把connfd加入m_epollfd。
最后就是dealwithread和dealwithwrite了。

2.定时器类中的cb_func():
关于主类中的定时器users_timer：
Utils::u_epollfd = m_epollfd
在时间轮中有个cb_func()的回调函数：epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
这个回调函数在tick()函数中用得到。
deal_timer(定时器, fd)：就是这个cb_func()回调函数。
3. dealwithread(sockfd)：
当检测到imporv和timer_flag都是1，deal_timer()会epoll_ctl(DEL)
当检测到read_once()没，deal_timer会epoll_ctl(DEL)
4. dealwithwirte():
当检测到improv和timer_flag都是1，deal_timer()会epoll_ctl(DEL)
当检测到write()没，deal_timer()会epoll_ctl(DEL)
5. IOManager类的构造，注册m_tickleFds[0]读端,epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event)
6. addEvent() 
7. delEvent()，
8. cancelAll()
9. IOManager类的idle()，epoll_ctl(m_epfd, op, fd_ctx->fd, &event);剔除已发生事件，剩下事件重新加入。
10. Utils类中有，addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
11. 就是主类不进行epoll_ctl()，但是可以用addfd()函数。
用到了的地方：eventListen()里添加m_listenfd，m_pipefd[0]。注册管道是为了信号处理。
12. eventLoop()里，服务器端关闭连接，deal_timer(timer, sockfd);

有wait的几个地方：
1. eventLoop() int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1); 1万个
2. IOManager类的idle() epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout); 256个

void eventListen(); 
void timer(int connfd, struct sockaddr_in client_address); 创建定时器，并添加到定时器链表
void deal_timer(tw_timer *timer, int sockfd); 处理定时器超时事件
bool dealclinetdata()：
处理客户端数据，可以设置LT或ET触发，通过accept()接收连接，并创建定时器。
bool dealwithsignal(bool& timeout, bool& stop_server);
void dealwithread(int sockfd); // 读事件处理
void dealwithwrite(int sockfd); // 写事件处理
void eventLoop()：


——————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
2023_11_20 开始做项目
新增了两个文件：fiber_webserver.cpp和fiber_webserver.h
原本的线程池：(有事件触发模式，有数据库连接池，8，一万)
先定义了一个IOManager m_iom = new IOManager();
// size_t threads, bool use_caller, const std::string &name
m_iom = new IOManager(m_thread_num, true, "IOManager", m_actormodel, m_connPool);
线程类:
在scheduler里m_threads是线程池，只有start()和stop()涉及，
iomanager里没有，在iomanager的构造里有start()，析构里有stop()。
scheduler里的start()创造的线程执行的是scheduler的run()，这个run()里有idle()和tickle(),
因为idle()、tickle()和stopping()定义的都是虚函数, 所以用IOManager里的。
也就是说：start()里的线程绑定Scheduler::run(), 然后这个run()里执行重写的idle()和tickle()。  
协程类：
scheduler的构造里有m_rooFiber=new Fiber执行Scheduler::run, 
scheduler的run()里有idle_fiber=new Fiber执行Scheduler::idle
                里有new Fiber(task.cb)
对于协程里的resume和yield，协程在定义的时候有入口函数：比如run和idle，当执行这两个操作的时候这个入口函数就被激活。

iomanager里, triggerEvent会调用schedule()向任务队列添加任务, idle()会调用。

改写webserver.cpp文件成为fiber_webserver.cpp文件：
在 dealwithread() 和 dealwithwrite() 中使用 Scheduler 类的内容进行改写后，
不再需要在函数体内使用 while(true) 循环的原因是 Scheduler 类的调度机制会自动
管理协程的执行。Scheduler 类内部有一个任务队列，通过协程的切换机制，可以在一个
协程执行过程中让出 CPU，切换到另一个协程执行，以此来实现多任务的协同执行。
具体来说，当一个协程执行 dealwithread() 或 dealwithwrite() 时，如果在函数体内执行了 
while(true) 循环，那么该协程将会一直占用 CPU 资源，不会主动让出执行权，导致其他协程
无法执行。这样就破坏了协程的调度机制。而使用 Scheduler 类时，你可以将 dealwithread()
和 dealwithwrite() 中的处理逻辑放在一个任务函数中，然后通过 Scheduler 提供的接口将任务
函数添加到任务队列中。Scheduler 会负责协程的调度，执行任务队列中的任务。协程在遇到 I/O 
操作或者其他可能引起阻塞的操作时，会主动让出执行权，让其他协程有机会执行，从而提高程序的
并发性能。总的来说，使用 Scheduler 类的调度机制，你不再需要手动编写死循环，而是通过协程的
切换机制，更加灵活地管理任务的执行。这样的设计使得代码更清晰、易读，并提高了程序的并发性能。

————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
2023_11_16 
向timer/添加了timer.h和timer.cc，为了iomanager.h和iomanager.cc，并且进行了修改。
改写iomanager.h和iomanager.cc
改写scheduler.h和scheduler.cc。关闭hook模块也就不使用hook.h——>主要体现在set_hook_enable()
给thread.h，scheduler.h, iomanager.h和fiber.h添加#include<iostream>, <string.h>
编译命令：
g++ -o test test.cpp iomanager.cc fiber.cc thread.cc scheduler.cc ../lock/mutex.cc ../timer/timer.cc -lpthread -std=c++11
g++ -o 0test_iomanager 0test_iomanager.cc iomanager.cc fiber.cc thread.cc scheduler.cc ../lock/mutex.cc ../timer/timer.cc -lpthread -std=c++11

2023_11_17 4.5h
1. 先整明白整个test_iomanager的过程
四大文件：fiber thread scheduler iomanager
让四大文件的cout信息更具体，即它们具体从哪儿来的。
Q1. IOManager的idle()什么时候调用triggerEvent()？
答：triggerEvent()用于处理已经发生的事件，触发相应的读或写事件。
Q2. 哪些地方会用到schedule()？
答：schedule()的本质是添加任务，这个任务可能是协程也可能是函数，而且schedule()里包含tickle()函数，
意味着添加完任务后还得负责唤醒。

2023_11_18 4h 
Q3. use_caller和m_rootFiber是什么关系？
答：
当 use_caller 为 true 时，表示调度器会使用调用线程（即构造调度器的线程）来执行任务。
这时，调度器会在构造函数中创建一个调度协程 m_rootFiber，并将当前线程的主协程保存在 
m_rootFiber 中。当调度器执行 start 方法时，会启动线程池中的其他线程来执行任务。
在构造函数中，当 use_caller 为 true 时，调度器会创建一个调度协程 m_rootFiber，
并将其设为当前线程的主协程。这样，调度器就能够在构造函数执行完毕后，
通过 start 方法启动线程池中的其他线程，执行任务。
总结一下：
当 use_caller 为 true 时，调度器创建了一个调度协程 m_rootFiber，并将当前线程的主协程保存在其中。
当 use_caller 为 false 时，调度器不会创建 m_rootFiber，而是在启动线程池时，每个线程都会创建自己的调度协程。

use_caller=true：
当 use_caller 为 true 时，主要是在调度器构造函数中创建了一个调度协程 m_rootFiber，并将当前线程的主协程保存在其中。
协程的切换主要发生在当前线程的主协程和调度协程之间。调度协程是由调度器管理的，而主协程是构造调度器的线程上的主协程。
use_caller=false：
当 use_caller 为 false 时，调度器并不会创建 m_rootFiber。
相反，启动线程池时，每个线程都会创建自己的调度协程。这些调度协程在主协程和子协程之间切换。
切换顺序可以理解为：主协程（当前线程） -> 调度协程 -> 子协程。
所以，关键点在于 use_caller=false 的情况下，每个线程都有自己的调度协程，而这些调度协程统一由调度器管理。
而在 use_caller=true 的情况下，调度协程是在构造函数中创建的，属于当前线程。


Q4: caller线程指的是什么？
答：
在协程的上下文中，"caller线程" 通常指的是当前协程的创建者或调用者所在的线程。在一些协程框架中，为了方便描述，将启动
协程的线程称为 "caller线程"。在你提到的代码中，use_caller 参数的意义是决定是否使用 "caller线程" 进行协程的调度。
如果 use_caller 为 true，则表示协程调度器将会使用创建协程的线程（即 "caller线程"）进行协程的调度。
在这种情况下，协程调度器会维护一个调度协程（m_rootFiber），并在 "caller线程" 中执行该调度协程。
如果 use_caller 为 false，则表示协程调度器会创建独立的线程来执行协程的调度。每个线程都会有自己的调度协程。
在这种情况下，调度器不再依赖 "caller线程" 来进行协程的调度。
总结起来，"caller线程" 是指启动协程的线程，而 use_caller 参数决定了是否使用 "caller线程" 进行协程的调度。