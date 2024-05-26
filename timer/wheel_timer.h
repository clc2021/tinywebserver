#ifndef WHEEL_TIMER
#define WHEEL_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

/////////////////////////////////////// 时间轮 /////////////////////////////////////// 
class tw_timer;

struct client_data // 连接资源
{
    sockaddr_in address; //客户端socket地址
    int sockfd; //socket文件描述符
    tw_timer * timer; //定时器
};  

class tw_timer // 定时器类
{
public:
    tw_timer(int rot, int ts): next(NULL), prev(NULL), rotation(rot), time_slot(ts) {}

public:
    int rotation; // 记录定时器在时间轮转多少圈后生效。
    int time_slot; // 记录定时器属于时间轮上哪个槽（对应的链表，下同）。
    void(*cb_func)(client_data *); // 定时器回调函数。
    client_data * user_data; // 客户数据。
    tw_timer * next; // 指向下一个定时器。
    tw_timer * prev; // 指向前一个定时器。
};

class time_wheel // 时间轮
{
public:
    time_wheel();
    ~time_wheel();
    // tw_timer* add_timer(int timeout);
    void add_timer(time_t timeout, tw_timer * timer);
    void del_timer(tw_timer * timer);
    void tick();
    
private:
    static const int N = 60; // 时间轮上槽的数目
    static const int SI = 1; // 每1s时间轮转动一次，即槽间隔为1s
    tw_timer * slots[N]; // 时间轮的槽，其中每个元素指向一个定时器链表，链表无序
    int cur_slot; // 时间轮的当前槽
};

/////////////////////////////////////// 工具类 /////////////////////////////////////// 
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);
    int setnonblocking(int fd); //对文件描述符设置非阻塞
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);  //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    static void sig_handler(int sig); //信号处理函数
    void addsig(int sig, void(handler)(int), bool restart = true); //设置信号函数
    void timer_handler(); //定时处理任务，重新定时以不断触发SIGALRM信号
    void show_error(int connfd, const char *info);

public: // 在这个Utils类里，似乎只有timer_handler里涉及时间轮变量
    static int *u_pipefd;
    time_wheel m_tw; // 时间轮
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
