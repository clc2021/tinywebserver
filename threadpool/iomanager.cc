/*
https://www.midlane.top/wiki/pages/viewpage.action?pageId=10061031 IOManager协程调度
https://www.midlane.top/wiki/pages/viewpage.action?pageId=10060957 Fiber协程

*/
#include <unistd.h>    // for pipe()
#include <sys/epoll.h> // for epoll_xxx()
#include <fcntl.h>     // for fcntl()
#include "iomanager.h"

//#include "log.h" g_logger
// #include "macro.h"

//static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

enum EpollCtlOp {
};

static std::ostream &operator<<(std::ostream &os, const EpollCtlOp &op) {
    switch ((int)op) {
#define XX(ctl) \
    case ctl:   \
        return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
#undef XX
    default:
        return os << (int)op;
    }
}

static std::ostream &operator<<(std::ostream &os, EPOLL_EVENTS events) {
    if (!events) {
        return os << "0";
    }
    bool first = true;
#define XX(E)          \
    if (events & E) {  \
        if (!first) {  \
            os << "|"; \
        }              \
        os << #E;      \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

// connection_pool *m_connPool;  // 数据库
// list<http_conn *> m_workqueue; //请求队列
// locker m_queuelocker;       //保护请求队列的互斥锁
// sem m_queuestat;              // 是否有任务需要处理
int IOManager::setnonblocking(int fd)
{
    // fcntl系统调用可以用来对已打开的文件描述符进行各种控制操作以改变已打开文件的的各种属性
    int old_option = fcntl(fd, F_GETFL); // 对fd获取文件状态标志
    int new_option = old_option | O_NONBLOCK; // 
    fcntl(fd, F_SETFL, new_option); // 设置文件状态标志
    return old_option;
}

bool IOManager::append(http_conn * request, int state) {
    std::cout << "=============IOManager, append开头" << std::endl;
    m_queuelocker.lock(); // 加锁：互斥锁是一个线程内的。
    if (m_workqueue.size() >= 10000)
    {
        m_queuelocker.unlock(); // 所以一定是要有解锁操作的。
        return false;
    }
    // eg. http_conn类的m_state读为0 写为1，也就是要看请求是读请求还是写请求
    request->m_state = state; 
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // V
    std::cout << "=============IOManager, append结尾" << std::endl;
    return true;
}

bool IOManager::append_p(http_conn * request) {
    std::cout << "=============IOManager, append_p开头" << std::endl;
    m_queuelocker.lock();
    if (m_workqueue.size() >= 10000)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    std::cout << "=============IOManager, append_p结尾" << std::endl;
    return true;
}

void IOManager::runWorker() {
    std::cout << "=============IOManager, runWorker开头" << std::endl;
    while (true)
    {
        m_queuestat.wait(); // 信号量等待 P
        m_queuelocker.lock(); // 被唤醒后先加上锁。对请求队列的所有处理都要锁。
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        http_conn *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model) // reactor模式
        {
            if (0 == request->m_state) // 读状态
            {
                if (request->read_once()) // 这里的设计确实有点奇怪，认为是T类型一定有read_once()函数。
                {
                    request->improv = 1;
                    // connPool->GetConnection(); 
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process(); // 然后就交给http_conn类去处理了。这是在一个线程中。
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else // 写状态
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else // proactor模式
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
    std::cout << "=============IOManager, runWorker结尾" << std::endl;
}

IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(IOManager::Event event) {
    std::cout << "IOManager中, getEventContext(), 返回read或write" << std::endl;
    switch (event) {
    case IOManager::READ:
        std::cout << "IOManager中, 是read事件" << std::endl;
        return read;
    case IOManager::WRITE:
        std::cout << "IOManager中, 是write事件" << std::endl;
        return write;
    default:
        assert(event == false);
    }
    throw std::invalid_argument("getContext invalid event");
}

/*
Fiber里的reset(cb)执行过程：
得到当前协程上下文后 ——> m_cb = cb ——> makecontext(&m_ctx, MainFunc, 0)
MainFunc实际上就是执行协程函数cb，执行过程：
当前协程引用计数+1 ——> cb() ——>　当前协程引用计数-1 ——> yield()；
reset()里刚开始的时候TERM，最后为READY；MainFunc执行完后为TERM。*/

void IOManager::FdContext::resetEventContext(EventContext &ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset(); // reset()空，代表这个协程不执行任何函数。m_cb=NULL
    ctx.cb = nullptr;
}

void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    std::cout << "IOManger中, triggerEvent()开头, 会执行schedule()" << std::endl;
    // 待触发的事件必须已被注册过
    assert(events & event);
    
    // 清除该事件，表示不再关注该事件了
    // 也就是说，注册的IO事件是一次性的，如果想持续关注某个socket fd的读写事件，那么每次触发事件之后都要重新添加
    events = (Event)(events & ~event);
    // 调度对应的协程
    EventContext &ctx = getEventContext(event); //read或write
    if (ctx.cb) {
        ctx.scheduler->schedule(ctx.cb); // schedule()本质是添加任务，任务类型是协程-线程ID或者函数-线程ID
    } else {
        ctx.scheduler->schedule(ctx.fiber);
    }
    resetEventContext(ctx);
    std::cout << "IOManager中, triggerEvent()结尾" << std::endl;
    return;
}

// 构造函数首先初始化一个scheduler，随后添加了支持epoll功能，将其设置为非阻塞的边缘触发。
IOManager::IOManager(size_t threads, bool use_caller, const std::string &name, int actor_model, connection_pool *connPool)
    : Scheduler(threads, use_caller, name) {
    std::cout << "IOManager构造开头" << std::endl;
    m_epfd = epoll_create(5000);
    assert(m_epfd > 0);

    int rt = pipe(m_tickleFds); // 0是管道读端，1是管道写端
    assert(!rt);

    epoll_event event; // 关注pipe读句柄的可读事件，用于tickle协程
    memset(&event, 0, sizeof(epoll_event));
    event.events  = EPOLLIN | EPOLLET; // 关注可读事件，ET模式
    event.data.fd = m_tickleFds[0];

    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK); // 非阻塞方式，配合边缘触发
    assert(!rt);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    assert(!rt);
    
    contextResize(32);
    
    m_actor_model = actor_model; //+事件触发模式
    m_connPool = connPool; // +数据库连接池

    // 重点看这个IOManager类的构造函数，这里有个start()，这个会绑线程-run()
    start();
    std::cout << "IOManager构造结束" << std::endl;
}

IOManager::~IOManager() {
    std::cout << "IOManager析构开头" << std::endl;
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
            m_fdContexts[i] = nullptr; // error修正
        }
    }


    std::cout << "IOManager析构结尾" << std::endl;
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (!m_fdContexts[i]) {
            m_fdContexts[i]     = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}


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
    // eg.最开始的时候：events=ET|NONE|EPOLLIN
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
/*
int IOManager::addEvent(int fd, Event event, std::function<void()> cb, bool one_shot, int TRIGMode, bool isNonblocking) {  
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
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD; //比如有READ
    epoll_event epevent;
    // epevent.events   = EPOLLET | fd_ctx->events | event;
    if (1 == TRIGMode)
        epevent.events = EPOLLET | fd_ctx->events | event; //ET+READ+WRITE
    else    
        epevent.events = fd_ctx->events | event | EPOLLRDHUP;//READ+WRITE+EPOLLRDHUP
    if (one_shot)
        epevent.events |= EPOLLONESHOT;
        
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

    if (isNonblocking)
        setnonblocking(fd);
    return 0;
}
*/

bool IOManager::delEvent(int fd, Event event) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    //if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
    if (__builtin_expect(!(fd_ctx->events & event), 0)) {
        return false;
    }

    // 清除指定的事件，表示不关心这个事件了，如果清除之后结果为0，则从epoll_wait中删除该文件描述符
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        std::cout << "IOManager中, epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
        return false;
    }

    // 待执行事件数减1
    --m_pendingEventCount;
    // 重置该fd对应的event事件上下文
    fd_ctx->events                     = new_events;
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (__builtin_expect(!(fd_ctx->events & event), 0)) {
        return false;
    }

    // 删除事件
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        std::cout << "IOManager中, epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
        return false;
    }

    // 删除之前触发一次事件
    fd_ctx->triggerEvent(event);
    // 活跃事件数减1
    --m_pendingEventCount;
    return true;
}

bool IOManager::cancelAll(int fd) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (!fd_ctx->events) {
        return false;
    }

    // 删除全部事件
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        std::cout << "IOManager中, epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):" << rt << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
        return false;
    }

    // 触发全部已注册的事件
    if (fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ); // trigger里有schedule
        --m_pendingEventCount;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    // SYLAR_ASSERT(fd_ctx->events == 0);
    assert(fd_ctx->events == 0);
    return true;
}

IOManager *IOManager::GetThis() {
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

// 通知调度协程，也就是Scheduler::run()从idle中退出
// Scheduler::run()每次从idle协程中退出之后，都会重新把任务队列里的所有任务执行完了再重新进入idle
// 如果没有调度线程处理于idle状态，那也就没必要发通知了
//通过写入管道的方式通知调度器有任务需要调度，主要用于唤醒阻塞在 epoll_wait 的 idle 协程。
void IOManager::tickle() {
    std::cout << "IOManager中, tickle开头" << std::endl;
    // bool hasIdleThreads() { return m_idleThreadCount > 0; }
    if(!hasIdleThreads()) { // scheduler里的函数，hasIdleThreads()当m_idleThreadCoun大于0的时候为真
        std::cout << "IOManager中, 无idle线程, tickle提前返回, tickle结尾" << std::endl;
        return; 
    }
    int rt = write(m_tickleFds[1], "T", 1); 
    // SYLAR_ASSERT(rt == 1);
    assert(rt == 1);
    std::cout << "IOManager中, tickle结尾" << std::endl;
}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

bool IOManager::stopping(uint64_t &timeout) {
    // 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
    // 增加定时器功能后，还应该保证没有剩余的定时器待触发
    timeout = getNextTimer();
    //m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

/*
调度器无调度任务时会阻塞idle协程上，对IO调度器而言，idle状态应该关注两件事，
一是有没有新的调度任务，对应Schduler::schedule()，如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；
二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行IO事件对应的回调函数
*/
void IOManager::idle() {
    std::cout << "IOManager中, idle开头" << std::endl;
    // 一次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wait继续处理
    const uint64_t MAX_EVNETS = 256;
    epoll_event *events       = new epoll_event[MAX_EVNETS]();
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) {
        delete[] ptr;
    });

    while (true) { //相当于idle()函数一直在while循环
        // 获取下一个定时器的超时时间，顺便判断调度器是否停止
        uint64_t next_timeout = 0;
        if (__builtin_expect(stopping(next_timeout), 0)) {
            std::cout << "IOManager中, name=" << getName() << "idle stopping exit" << std::endl;
            break;
        }

        // 阻塞在epoll_wait上，等待事件发生或定时器超时
        int rt = 0;
        do{
            // 默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时，
            static const int MAX_TIMEOUT = 5000;
            if(next_timeout != ~0ull) {
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            } else {
                next_timeout = MAX_TIMEOUT;
            }
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
            if(rt < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        } while(true);

        // 将所有已超时的定时器重新加入任务队列。
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) {
            for(const auto &cb : cbs) {
                schedule(cb); 
            }
            cbs.clear();
        }
        
        // 遍历所有发生的事件，根据epoll_event的私有指针找到对应的FdContext，进行事件处理
        for (int i = 0; i < rt; ++i) {
            epoll_event &event = events[i]; // 这里的event就是从epoll_wait()的events数组得来的，events[i]
            if (event.data.fd == m_tickleFds[0]) {
                // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可
                uint8_t dummy[256];
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
                    ;
                continue;
            }

            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            /*
            EPOLLERR：出错，比如读端和写端关闭pipe。EPOLLHUP：套接字对端关闭。
            出现这两种事件，应该同时出发fd的读事件和写事件，否则有可能出现注册的事件永远执行不到的情况。
            */
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            // 剔除已经发生的事件，将剩下的事件重新注册。
            int left_events = (fd_ctx->events & ~real_events);
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events    = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                std::cout << "IOManager中, epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):" << rt2 << " (" << errno << ") (" << strerror(errno) << ")" << std::endl;
                continue;
            }

            // 重点!!! 这里是处理已经发生的事件，当读事件发生时，触发读事件。
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            // 当写事件发生时，触发写事件。
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // end for

        // 一旦处理完所有的事件，idle协程yield，这样可以让调度协程(Scheduler::run)重新检查是否有新任务要调度
        // 上面triggerEvent实际也只是把对应的fiber重新加入调度，要执行的话还是要等idle协程退出。
        std::cout << "Fiber::ptr cur = Fiber::GetThis();前" << std::endl;
        Fiber::ptr cur = Fiber::GetThis();
        std::cout << "Fiber::ptr cur = Fiber::GetThis();后" << std::endl;
        auto raw_ptr   = cur.get();
        cur.reset();

        raw_ptr->yield();
    } 
    std::cout << "IOManager中, idle结尾" << std::endl;
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

