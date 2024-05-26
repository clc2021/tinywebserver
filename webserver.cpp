#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200); // 把当前的文件路径存到server_path
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path); // strcpy复制
    strcat(m_root, root); // strcat为+

    //定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 初始化日志
        // init(日志文件, 日志缓冲区大小, 最大行数, 最长日志条队列)
        if (1 == m_log_write) // 1是异步写入 0是同步写入
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

// 无比重要的3句代码：获得数据库连接池单例 ——> 初始化连接池创造多个连接 ——> 从数据表中加载数据
void WebServer::sql_pool()
{
    m_connPool = connection_pool::GetInstance();
    // init(url, User="root", PassWord="17441027Da", DBName="tinydb", Port=3306)
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤
    //+to do 2024_4_21梳理
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0); // 首先建立一个监听套接字，v4然后TCP类型
    assert(m_listenfd >= 0);

    //优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1}; // {0}优雅退出，{非零 0}强制退出 {非零 非零}延时退出
        // setsockopt设置socket属性，SO_LINGER延迟关闭连接
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp)); // 然后设置这个套接字
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address; // 设置完套接字开始设置地址
    bzero(&address, sizeof(address)); // bzero(void *s, int n)可以将s的前n个字节全部设为0
    address.sin_family = AF_INET; // IP4
    address.sin_addr.s_addr = htonl(INADDR_ANY); // 监听本地所有IP。要转换成大端字节序
    address.sin_port = htons(m_port); //固定端口9010。要转换成大端字节序。

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address)); // bind()把地址绑套接字上
    assert(ret >= 0);
    ret = listen(m_listenfd, 5); // 开始监听
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER]; // 然后epoll_event结构类型的数组events
    m_epollfd = epoll_create(5); // epoll句柄
    assert(m_epollfd != -1);

    // 把监听socket加入这个epoll句柄
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode); // eventListen()里，先注册的是m_listenfd
    http_conn::m_epollfd = m_epollfd; // 而且还让http_conn里的epollfd等于主函数里的

    // 创建管道套接字
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    
    // 设置管道的写端非阻塞, 读端为ET非阻塞。——> 管道都是非阻塞的。
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0); // 再注册的是管道的读端

    // 传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false); 

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd; // 然后让utils的管道和epollfd也等于主函数的epollfd。
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // timer()里有对http_conn类的数组users的初始化，这个初始化会往epollfd（现在主函数、utils和http_conn的epollfd是一样的）
    // 注册connfd。
    // 对于wheel_timer定时器类：有utils的addfd()，cb_func()，timer_handler吃tick吃cb_func。
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    //创建定时器
    // +to_do 把连接作为一个数据传入定时器中
    tw_timer *timer = new tw_timer(0, 0); // 多少圈以后生效，以及当前在哪个时间槽
    timer->user_data = &users_timer[connfd]; // 定时器
    timer->cb_func = cb_func; // 回调函数

    time_t cur, expire;
    struct timespec ti;
    clock_gettime(CLOCK_MONOTONIC, &ti);
    cur = ti.tv_sec;
    expire = cur + 3 * TIMESLOT;

    users_timer[connfd].timer = timer;
    // 定时器链表是装在utils这个工具类里的
    // add_timer(到期时间, 定时器)
    utils.m_tw.add_timer(expire, timer); // 到期时间是当前时间+TIMESLOT的三倍，3个滴答周期后到期
}

void WebServer::deal_timer(tw_timer *timer, int sockfd)
{
    // 如果timer()里是利用http_conn的addfd来ADD，那么这里就是利用cb_func来DEL
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_tw.del_timer(timer); // 移除定时器
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata()
{ // dealclientdata()会利用timer吃http_conn.init对connfd来ADD
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address); //http_conn用addfd()进行初始化
    }

    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0); // 从管道的读端读取信号
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i]) // 只监测两个信号
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{ //dealwithread会利用deal_timer的cb_func来DEL
    tw_timer *timer = users_timer[sockfd].timer; // sockfd这个客户事件的fd

    //reactor
    if (1 == m_actormodel)
    {
        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd); 
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    // 和dealwithread()一样，利用deal_timer的cb_func来DEL
    // 有http_conn的write()，涉及到MOD
    tw_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write()) 
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        //监测发生事件的文件描述符
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 轮询文件描述符
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata(); // http_conn来ADD
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                tw_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd); // cb_func来DEL
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd); // cb_func来DEL
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd); // cb_func来DEL
            }
        }
        if (timeout)
        {
            utils.timer_handler(); // 时针走动；发送一个alarm(5)；

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}
