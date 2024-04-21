/**
 * @file thread.h
 * @brief 线程相关的封装
 * @author sylar.yin
 * @email 564628276@qq.com
 * @date 2019-05-31
 * @copyright Copyright (c) 2019年 sylar.yin All rights reserved (www.sylar.top)
 */
#ifndef THREAD_H__
#define THREAD_H__

#include "../lock/mutex.h"
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <iostream>
#include <string.h>

class Thread : Noncopyable { // 这里对于线程也定义了智能指针。
public:
    // 线程智能指针类型
    typedef std::shared_ptr<Thread> ptr;
    Thread(std::function<void()> cb, const std::string &name);
    ~Thread();
    pid_t getId() const { return m_id; }
    const std::string &getName() const { return m_name; }
    void join();
    static Thread *GetThis();
    static const std::string &GetName();
    static void SetName(const std::string &name);
    //pid_t GetThreadId() {return syscall(SYS_gettid);}

private:
    static void *run(void *arg);

private:
    // 线程id
    pid_t m_id = -1;
    // 线程结构
    pthread_t m_thread = 0;
    // 线程执行函数
    std::function<void()> m_cb; //——>这个函数相当于是threadpool.h里的run()
    // 线程名称
    std::string m_name;
    // 信号量
    Semaphore m_semaphore;
};


#endif
