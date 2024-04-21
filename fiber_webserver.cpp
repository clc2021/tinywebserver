#include "fiber_webserver.h"
#include <fstream>
using namespace std;
ofstream fout("./output.txt");
WebServer::WebServer()
{
    std::cout << "=====================Webserver构造开头" << std::endl;
    users = new http_conn[MAX_FD];

    char server_path[200];
    getcwd(server_path, 200); 
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path); 
    strcat(m_root, root); 
    //users_timer = new client_data[MAX_FD];
    std::cout << "====================Webserver构造结尾" << std::endl;
}

WebServer::~WebServer()
{
    std::cout << "====================Webserver析构开头" << std::endl;
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete m_iom;
    std::cout << "=================Webserver析构结尾" << std::endl;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    std::cout << "==============init开头" << std::endl;
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

    std::cout << "在new_scheduler()里, m_iom = 前 线程个数 " << m_thread_num << std::endl;
    m_iom = new IOManager(m_thread_num, true, "IOManager", m_actormodel, m_connPool);
    std::cout << "在new_scheduler()里, m_iom = 后" << std::endl;
    // 既然这里有了epollfd，那么就在这儿初始化吧
    m_epollfd = m_iom->getEpollfd();
    http_conn::m_epollfd = m_epollfd;
    Utils::u_epollfd = m_epollfd;
    std::cout << "==============init结尾" << std::endl;
}

void WebServer::trig_mode()
{
    std::cout << "==============trigemode开头" << std::endl;
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }

    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
    std::cout << "==============trigemode结尾" << std::endl;
}

void WebServer::log_write()
{
    std::cout << "==============log_write开头" << std::endl;
    if (0 == m_close_log)
    {
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
    std::cout << "==============trigemode结尾" << std::endl;
}

void WebServer::sql_pool()
{
    std::cout << "==============sql_pool开头" << std::endl;
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    users->initmysql_result(m_connPool);
    std::cout << "===============sql_pool结尾" << std::endl;
}

bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) 
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
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
            switch (signals[i]) 
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                m_iom->stop();
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    //reactor
    std::cout << "==============dealwithread开头" << std::endl;
    if (1 == m_actormodel)
    {
        //m_iom->append(users+sockfd, 0);
        //m_iom->addEvent(sockfd, IOManager::READ, [this, sockfd](){this->m_iom->runWorker();});
        m_iom->schedule([this, sockfd]() {m_iom->append(users + sockfd, 0);});
        m_iom->schedule([this]() {m_iom->runWorker();});
        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    // deal_timer(timer, sockfd);
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
            //m_iom->append_p(users + sockfd);
            //m_iom->addEvent(sockfd, IOManager::WRITE, std::bind(&IOManager::runWorker, this));
            //m_iom->addEvent(sockfd, IOManager::WRITE, [this, sockfd](){this->m_iom->runWorker();});
            //m_iom->schedule([this]() {m_iom->runWorker();});
            m_iom->schedule([this, sockfd]() {m_iom->append_p(users + sockfd);});
        }
    }
    std::cout << "==============dealwithread结尾" << std::endl;
}

void WebServer::dealwithwrite(int sockfd)
{
    std::cout << "==============dealwithwrite开头" << std::endl;
    //tw_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        m_iom->schedule([this, sockfd]() {m_iom->append(users + sockfd, 1);});
        m_iom->schedule([this]() {m_iom->runWorker();});
        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    //deal_timer(timer, sockfd);
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
    }
    std::cout << "==============dealwithwrite结尾" << std::endl;
}

//接下来是尝试使用IOManager
bool WebServer::dealclientdata() 
{
    std::cout << "==============dealclientdata开头" << std::endl;
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
        // timer(connfd, client_address);可能不太需要定时了
        // 但是因为这个timer()里有关于http的设置，所以另写
        // 初始化connfd套接字对应的http
        // 注意：这里有用addfd注册文件，此时epoll实例来自于IOManager，之后再看看怎么注册吧
        users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName); 
        std::cout << "==============reactor, dealclientdata执行到绑dealwithread和dealwithwrite" << std::endl;
        m_iom->addEvent(connfd, IOManager::READ, [this, connfd](){this->dealwithread(connfd);});
        m_iom->addEvent(connfd, IOManager::WRITE, [this, connfd](){this->dealwithwrite(connfd);});
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
            // timer(connfd, client_address);
            // 同样是在http上进行初始化
            users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName); 
            std::cout << "==============proactor, dealclientdata执行到绑dealwithread和dealwithwrite" << std::endl;
            m_iom->addEvent(connfd, IOManager::READ, [this, connfd](){this->dealwithread(connfd);});
            m_iom->addEvent(connfd, IOManager::WRITE, [this, connfd](){this->dealwithwrite(connfd);});
        }
        return false;
    }

    m_iom->delEvent(m_listenfd, IOManager::READ);
    // m_iom->addEvent(m_listenfd, IOManager::READ, std::bind(&WebServer::dealclientdata, this));
     m_iom->addEvent(m_listenfd, IOManager::READ, [this](){this->dealclientdata();});
    return true;
}

void WebServer::eventListen()
{
    std::cout << "==============eventListen开头" << std::endl;
    bool timeout = false;
    bool stop_server = false;
    m_epollfd = m_iom->getEpollfd(); 
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0); 
    if (0 == m_OPT_LINGER) // 表示在关闭套接字时最多等待 1 秒钟来尝试发送未发送的数据。
    {
        struct linger tmp = {0, 1}; // {0}优雅退出，{非零 0}强制退出 {非零 非零}延时退出
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER) // 表示在关闭套接字时最多等待 1 秒钟来尝试发送未发送的数据。
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);
    
    // 注册监听socket
    fout << "开始监听了" << endl;
    std::cout << "服务器绑定到：" << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port) << std::endl;
    std::cout << "服务器正在监听端口 " << m_port << std::endl;
    std::cout << "==============eventListen执行到绑dealclientdata" << std::endl;
    m_iom->addEvent(m_listenfd, IOManager::READ, [this](){this->dealclientdata();});
    std::cout << "==============eventListen结尾" << std::endl;
    /*
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    //utils.addfd(m_epollfd, m_pipefd[0], false, 0);
    m_iom->addEvent(m_pipefd[0], IOManager::READ, std::bind(&WebServer::dealwithsignal, this, timeout, stop_server));
    utils.addsig(SIGPIPE, SIG_IGN); // 忽略SIGPIPE信号
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);
    alarm(TIMESLOT);
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
    */
}

void WebServer::eventLoop() {
    eventListen();
}