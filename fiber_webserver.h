#ifndef FIBER_WEBSERVER_H
#define FIBER_WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h" // threadpool
#include "./http/http_conn.h" // http_conn
#include "./threadpool/iomanager.h"
#include "./threadpool/thread.h"
#include <memory>
#include <functional>

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;
class WebServer
{
    public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    void sql_pool();
    void log_write();
    void trig_mode();
    //以上都是设置，下面是IOManager类
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);
    //接着尝试eventListen()?
    void eventListen();
    void eventLoop();

public:
    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;
    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;
    connection_pool *m_connPool;
    string m_user;        
    string m_passWord;     
    string m_databaseName; 
    int m_sql_num;

    // 调度器相关
    IOManager * m_iom;
    int m_thread_num; // 调度器中线程个数。

    //epoll_event相关 这个不一定需要了
    // epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode; 
    // //定时器相关 这个不一定需要了
    // client_data *users_timer;
    Utils utils;
};
#endif
