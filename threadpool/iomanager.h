/*
解决了调度器在idle()情况下忙等导致CPU占用率高的问题，
它使用一对管道fd来tickle调度协程，
当调度器空闲时，idle协程通过epoll_wait()阻塞在管道的读描述符上，等管道的可读事件。
添加新任务时，tickle方法写管道，idle协程检测到管道可读后退出，调度器执行调度。

IO协程调度==增强版协程调度

之前的协程调度：把一个协程添加到调度器的任务队列，就相当于是调用了协程的resume方法，
IO调度器支持协程调度的全部功能，∵IO调度器就是直接继承协程调度器实现的，且 增加了IO事件调度
IO协程调度支持为描述符注册 可读 和 可写 事件的回调函数，当描述符可读或者可写时执行对应的回调函数，
sylar做法：让epoll支持的事件全部当做可读或可写事件，也就是EPOLLIN或EPOLLOUT。

IO协程调度器三元组FdContext：(描述符，       事件类型[可读或可写]，      回调函数)
                        这俩用于epoll_wait()                这个用于协程调度
每个fd都拥有一个对应的FdContext，有fd的值，fd上的事件，以及fd上的读写事件上下文，
------关于FdContext结构体，
内置EventContext结构体{调度器；回调协程；回调函数}：
get事件上下文，reset事件上下文，
triggerEvent(Event event)根据事件类型调用对应上下文结构中的调度器去调度协程或回调函数，
有read,  write,  fd,  events=NONE,  

IO协程调度器会在idle时epoll_wait()所有注册的fd，如果fd满足条件，epoll_wait()返回，
从私有数据中拿到fd的上下文信息，并执行其中的回调函数，
实际上idle协程只负责收集所有已触发的fd的回调函数并将其加入调度器的任务队列，
真正执行时机是idle协程退出后，调度器在下一轮调度时执行，
IO支持取消事件，表示：不关心fd的某个事件了，如果fd的可读或可写事件都被取消了，
那么这个fd会从调度器的epoll_wait()中删除。

tickle()：通知调度器有任务要调度。写端也就是m_tickleFds[1]让idle协程从epoll_wait()退出。
          等到idle协程yield之后Scheduler::run就可以调度其他任务。
          如果当前没有空闲调度线程，那就没必要发出通知。
*/

#ifndef IOMANAGER_H__
#define IOMANAGER_H__

#include "scheduler.h"
#include "../timer/timer.h"
#include <assert.h>
#include <iostream>
#include <string.h>
#include "../lock/locker.h"
#include "../http/http_conn.h"
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../CGImysql/sql_connection_pool.h"

// 继承Scheduler的IOManager，使其支持epoll，并重载tickle和idle，实现通知调度协程和IO协程调度功能。
class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType; // 这里还有个读写锁类型。在mutex.h中。

    // IO事件，继承自epoll对事件的定义，这里只关心socket fd的读和写事件，其他epoll事件会归类到这两类事件中
    enum Event {
        NONE = 0x0,    
        READ = 0x1,     // 读事件(EPOLLIN)
        WRITE = 0x4,    // 写事件(EPOLLOUT)
    };

private:
    // socket fd上下文类
    // 每个socket fd都对应一个FdContext，包括fd的值，fd上的事件，以及fd的读写事件上下文
    struct FdContext {
        typedef Mutex MutexType;
        // 事件上下文类
        // fd的每个事件都有一个事件上下文，保存这个事件的回调函数以及执行回调函数的调度器
        // sylar对fd事件做了简化，只预留了读事件和写事件，所有的事件都被归类到这两类事件中
        struct EventContext {
            // 执行事件回调的调度器
            Scheduler *scheduler = nullptr;
            // 事件回调协程
            Fiber::ptr fiber;
            // 事件回调函数
            std::function<void()> cb;
        };

        EventContext &getEventContext(Event event);
        void resetEventContext(EventContext &ctx);

        // 触发事件
        void triggerEvent(Event event);

        EventContext read;
        EventContext write;
        int fd = 0; // 可以看到FdContext对象初始化的时候会认为fd=0
        Event events = NONE; // 可以看到events=NONE
        MutexType mutex;
    };

public:
    IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager", int actor_model = 0, connection_pool *connPool = NULL);
    ~IOManager();
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    // int addEvent(int fd, Event event, std::function<void()> cb = nullptr, bool one_shot, int TRIGMode, bool isNonblocking);
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event); // 取消事件，如果该事件被注册过回调，那就触发一次回调事件
    bool cancelAll(int fd); // 取消所有事件，所有被注册的回调事件在cancel之前都会被执行一次
    static IOManager *GetThis(); // 返回当前的IOManager

    // threadpool的函数
    bool append(http_conn * request, int state);
    bool append_p(http_conn * request);
    void runWorker(); // 这是threadpoolrun和worker结合
   // static void Worker(void * args);
   int getEpollfd() {return m_epfd;}
   int setnonblocking(int fd);

protected:
    void tickle() override;
    bool stopping() override;
    void idle() override;
    bool stopping(uint64_t& timeout);
    void onTimerInsertedAtFront() override;
    void contextResize(size_t size);

private:
    int m_epfd = 0; // epoll文件句柄，epoll_create()创造
    int m_tickleFds[2]; // pipe文件句柄，fd[0]读端, fd[1]写端
    std::atomic<size_t> m_pendingEventCount = {0}; // 当前等待执行的IO事件数量
    RWMutexType m_mutex; // Mutex
    std::vector<FdContext *> m_fdContexts; // socket事件的上下文容器

    connection_pool *m_connPool;  // 数据库
    int m_actor_model;            // 事件处理模式
    std::list<http_conn *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;              // 是否有任务需要处理，这是信号量，有PV操作

    int TRIGMode;
};

#endif