//2023_11_28
#include <iostream>
#include <csignal>
#include "iomanager.h"
#include "http_conn.h"

IOManager* iom = nullptr;

void handleClient(http_conn* client) {
    // 处理客户端连接的逻辑
    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
    client->send(response, strlen(response));
}

void signalHandler(int signo) {
    if (signo == SIGINT) {
        std::cout << "Received SIGINT, stopping server..." << std::endl;
        // 停止 IOManager
        if (iom) {
            iom->stop();
        }
    }
}

int main() {
    // 注册信号处理函数
    signal(SIGINT, signalHandler);

    // 创建 IOManager 对象，线程数量为 2，使用调用线程
    iom = new IOManager(2, true, "server");

    // 创建 HTTP 服务器
    int listen_fd = ...;  // 请替换为实际的监听套接字创建逻辑

    // 添加监听套接字的读事件，并指定回调函数
    iom->addEvent(listen_fd, IOManager::READ, [listen_fd]() {
        // 接受连接
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd > 0) {
            // 创建 http_conn 对象，用于处理客户端连接
            http_conn* client = new http_conn(client_fd);
            
            // 将处理客户端连接的协程添加到 IOManager 中
            //iom->schedule(std::bind(&handleClient, client));
            iom->addEvent(client, IOManager::READ, &handleClient);
            iom->addEvent(client, IOManager::WRITE, &handleClient);
        }
    });

    // 运行 IOManager
    iom->start();

    return 0;
}

//————————————————————————————————————————————————————————————————
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include "iomanager.h"
#include "thread.h"

int server_socket;

void handle_client(int client_socket);

// 读事件回调，每次读取之后如果套接字未关闭，需要重新添加
void do_io_read() {
    sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);
    int client_socket = accept(server_socket, reinterpret_cast<struct sockaddr*>(&client_address), &client_address_len);
    if (client_socket == -1) {
        perror("Error accepting connection");
        return;
    }

    // 处理客户端通信
    std::cout << "Accepted connection from " << inet_ntoa(client_address.sin_addr) << ":" << ntohs(client_address.sin_port) << std::endl;
    handle_client(client_socket);

    // 读完之后重新添加读事件回调
    IOManager::GetThis()->addEvent(server_socket, IOManager::READ, do_io_read);
}

// 主协程，用于初始化服务器套接字和添加初始的读事件回调
void server_main() {
    // 创建套接字
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Error creating socket");
        return;
    }

    // 绑定地址和端口
    sockaddr_in server_address;
    std::memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(5005);  // 使用与客户端相同的端口

    if (bind(server_socket, reinterpret_cast<struct sockaddr*>(&server_address), sizeof(server_address)) == -1) {
        perror("Error binding socket");
        close(server_socket);
        return;
    }

    // 开始监听
    if (listen(server_socket, 10) == -1) {
        perror("Error listening on socket");
        close(server_socket);
        return;
    }

    std::cout << "Server listening on port 5005..." << std::endl;

    // 添加初始的读事件回调
    IOManager::GetThis()->addEvent(server_socket, IOManager::READ, do_io_read);

    // 运行事件循环
    IOManager::GetThis()->run();
}

void handle_client(int client_socket) {
    // 处理客户端的通信逻辑，这里简单地将接收到的数据发送回客户端
    char buffer[1024];
    ssize_t received_bytes;

    while ((received_bytes = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        send(client_socket, buffer, received_bytes, 0);
    }

    close(client_socket);
}

int main() {
    // 启动服务器主协程
    IOManager iom(10);
    iom.schedule(server_main);

    // 运行事件循环
    iom.run();

    return 0;
}

//——————————————————————————————————————————————————————————
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

    // Create IOManager instance
    m_iom = new IOManager();

    // Initialize the IOManager
    m_iom->init(m_thread_num, true, "IOManager", m_actormodel, m_connPool);
}

//————————————————————————————————————————————————————————————————
// 在 WebServer 类的定义中添加以下成员函数声明
void handleAccept(int connfd, struct sockaddr_in client_address);
void handleSignal();
// 在 WebServer.cpp 中实现这两个函数
void WebServer::handleAccept(int connfd, struct sockaddr_in client_address)
{
    if (connfd < 0)
    {
        LOG_ERROR("%s:errno is:%d", "accept error", errno);
        return;
    }

    if (http_conn::m_user_count >= MAX_FD)
    {
        utils.show_error(connfd, "Internal server busy");
        LOG_ERROR("%s", "Internal server busy");
        return;
    }

    timer(connfd, client_address);
}

void WebServer::handleSignal()
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1 || ret == 0)
    {
        LOG_ERROR("%s:errno is:%d", "recv signal error", errno);
        return;
    }

    for (int i = 0; i < ret; ++i)
    {
        switch (signals[i])
        {
        case SIGALRM:
        {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            break;
        }
        case SIGTERM:
        {
            // 处理 SIGTERM 信号
            // ...
            break;
        }
        }
    }
}

void WebServer::eventListen()
{
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 下面是设置 SO_LINGER 选项的代码
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 下面是设置地址和端口的代码
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

    utils.init(TIMESLOT);

    // 不再创建 epoll 实例，也不再添加 m_listenfd 到 epoll 中

    // 创建管道套接字
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    // 设置管道的写端非阻塞, 读端为ET非阻塞。——> 管道都是非阻塞的。
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}
//————————————————————————————————————————————————————————————————
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // ... (unchanged)

    // Add timer expiration event to the IOManager
    m_iom->addTimerEvent(expire, std::bind(&WebServer::handleTimer, this, connfd));
}

void WebServer::dealwithread(int sockfd)
{
    // ... (unchanged)

    // Add read event to the IOManager
    m_iom->addEvent(sockfd, EPOLLIN, std::bind(&WebServer::handleRead, this, sockfd));
}

void WebServer::dealwithwrite(int sockfd)
{
    // ... (unchanged)

    // Add write event to the IOManager
    m_iom->addEvent(sockfd, EPOLLOUT, std::bind(&WebServer::handleWrite, this, sockfd));
}

void WebServer::eventLoop()
{
    // ... (unchanged)

    // Start the IOManager event loop
    m_iom->eventLoop();
}

// Define additional private member functions to handle specific events

void WebServer::handleListen(int sockfd)
{
    // Handle new client connections
    bool flag = dealclinetdata();
    if (!flag) {
        LOG_ERROR("%s", "dealclientdata failure");
    }
}

void WebServer::handleTimer(int sockfd)
{
    tw_timer *timer = users_timer[sockfd].timer;
    deal_timer(timer, sockfd);
}

void WebServer::handleRead(int sockfd)
{
    tw_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        //若监测到读事件，将该事件放入请求队列
        m_iom->append(users + sockfd, 0);
        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    handleTimer(sockfd);
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
        if (users[sockfd].readOnce())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_iom->append_p(users + sockfd);
        }
        else
        {
            handleTimer(sockfd);
        }
    }
}

void WebServer::handleWrite(int sockfd)
{
    tw_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        m_iom->append(users + sockfd, 1);
        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    handleTimer(sockfd);
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
            handleTimer(sockfd);
        }
    }
}
