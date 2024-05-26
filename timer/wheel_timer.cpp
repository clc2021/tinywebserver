#include "wheel_timer.h"
#include "../http/http_conn.h"

////////////////////////////////////// 时间轮 ////////////////////////////////////// 
time_wheel::time_wheel():cur_slot(0) 
{
    for(int i = 0; i < N; ++i) {
        slots[i] = NULL; // 初始化每个槽的头结点
    }
}

time_wheel::~time_wheel() 
{
    // 遍历每个槽，并销毁其中的定时器
    for(int i = 0; i < N; ++i) {
        tw_timer * tmp = slots[i]; 
        while(tmp) {
            slots[i] = tmp->next;
            delete tmp;
            tmp = slots[i];
        }
    }
}

// 这里的timeout是加入时候的系统时间+15s
// 150000+15
void time_wheel::add_timer(time_t timeout, tw_timer * timer) { // 把tw_timer*改了
    if(timeout < 0) {
        return ;
    }
    int ticks = 0;
    if(timeout < SI) {
        ticks = 1;
    } else {
        ticks = timeout / SI; // 因为SI=1 这里相当于timeout, 滴答这么多次
    }
    int rotation = ticks / N; // 计算转多少圈
    int ts = (cur_slot + (ticks % N)) % N; // 计算槽位
    // 创建新的定时器，它在时间轮转动rotation圈之后被触发，且位于第ts个槽上。
    //tw_timer * timer = new tw_timer(rotation, ts);
    timer->rotation = rotation;
    timer->time_slot = ts;
    if(!slots[ts]) {
        slots[ts] = timer;
    } else { // slots[ts]这个槽不空
        timer->next = slots[ts]; // ? 这里是头插吗
        slots[ts]->prev = timer;
        slots[ts] = timer;
    }
    //return timer;
}

void time_wheel::del_timer(tw_timer * timer) {
    if(!timer) {
        return;
    }
    int ts = timer->time_slot;
    if(timer == slots[ts]) {
        slots[ts] = slots[ts]->next;
        if(slots[ts]) {
            slots[ts]->prev = NULL;
        }
        delete timer;
    } else { // timer = slots[ts]
        timer->prev->next = timer->next;
        if(timer->next) {
            timer->next->prev = timer->prev;
        }
        delete timer;
    }
}

void time_wheel::tick() {
    tw_timer * tmp = slots[cur_slot];
    while(tmp) {                                  
        if(tmp->rotation > 0) { //先挨个转圈减圈数，到下一个节点；极端情况下转一圈减一圈扩一圈
            tmp->rotation--; 
            tmp = tmp->next;
        }
        //接着直接cur_slot = ++cur_slot % N; eg. cur_slot=0 cur_slot更新为1
        // 否则，说明定时器已经到期，于是执行定时任务，然后删除该定时器

        else { // tmp->rotation=0
            tmp->cb_func(tmp->user_data);
            if(tmp == slots[cur_slot]) {
                slots[cur_slot] = tmp->next;
                delete tmp;
                if(slots[cur_slot]) {
                    slots[cur_slot]->prev = NULL;
                }
                tmp = slots[cur_slot];
            } else {
                tmp->prev->next = tmp->next;
                if(tmp->next) {
                    tmp->next->prev = tmp->prev;
                }
                tw_timer * tmp2 = tmp->next;
                delete tmp;
                tmp = tmp2;
            }
        }
    }

    cur_slot = ++cur_slot % N; // 更新时间轮的当前槽，以反应时间轮的转动
}

////////////////////////////////////// Utils ////////////////////////////////////// 
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    // fcntl(fd, 得到属性) fctl(fd, 修改属性, 新的属性值)
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    // O_NONBLOCK: 使I/O变成非阻塞模式, 在读不到数据或写缓冲满了的清空下return，不会等待
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
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

//信号处理函数：可以看到Utils是信号处理函数主要干一个事：往管道写端写入信号sig。

void Utils::sig_handler(int sig) 
{
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入，传输字符类型，而非整型
    // send(fd, 地址, 长度, 不管了) 写入fd
    send(u_pipefd[1], (char *)&msg, 1, 0);
    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    //执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1); // sa是新的，不保存旧操作。
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_tw.tick(); // 这里就是有不断触发了
    alarm(m_TIMESLOT); 
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
//定时器回调函数
void cb_func(client_data *user_data) // 删除监听的fd, 关闭，减少连接数
{
    // 删除非活动连接在socket上的注册事件。
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    // 关闭文件描述符。
    close(user_data->sockfd);
    // 减少连接数。
    http_conn::m_user_count--;
}
