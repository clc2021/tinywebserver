#ifndef WEBSERVER_H
#define WEBSERVER_H

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

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool(); // 线程池
    void sql_pool(); // 数据库池
    void log_write(); // 写入日志
    void trig_mode(); // 触发模式
    void eventListen(); // 事件监听
    void eventLoop(); // 监听循环

    void timer(int connfd, struct sockaddr_in client_address);
    void deal_timer(tw_timer *timer, int sockfd);

    bool dealclinetdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd); // 读事件处理
    void dealwithwrite(int sockfd); // 写事件处理

public:
    //基础
    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;

    int m_pipefd[2]; // 这里有个管道
    int m_epollfd;
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode; // 监听socket的触发模式
    int m_CONNTrigmode; // 连接socket的触发模式

    //定时器相关
    client_data *users_timer;
    Utils utils;
};
#endif
