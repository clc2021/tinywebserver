/*
解决子协程调度的跑飞问题：
给每个线程增加一个线程局部变量：用于保存调度协程的上下文。
这样每个线程可以同时保存三个协程的上下文：
① 当前协程的。  ② 主协程的。    ③ 调度协程的。
有了这三个上下文，协程就可以根据自己的身份来选择每次和哪个协程进行交换，具体操作：
1. Fiber类中的：bool m_runInScheduler; //  本协程是否参与调度器调度。
2. 根据协程的身份指定对应的协程类型：
    只有想让调度器调度的协程的m_runInScheduler值为true，
    线程主协程和线程调度协程的m_runInScheduler都为false。
3. resume()一个协程时：
*/

#ifndef FIBER_H__
#define FIBER_H__

#include <functional>
#include <memory>
#include <ucontext.h>
#include "thread.h"
#include <iostream>
#include <string.h>

class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    typedef std::shared_ptr<Fiber> ptr;

    enum State {
        READY, // 就绪态，刚创建或者yield之后的状态
        RUNNING, // 运行态，resume之后的状态
        TERM // 结束态，协程的回调函数执行完之后为TERM状态
    };

private:
    /*
    构造函数
    无参构造函数只用于创建线程的第一个协程，也就是线程主函数对应的协程，
    这个协程只能由GetThis()方法调用，所以定义成私有方法
    */
    Fiber();

public:
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);

    ~Fiber();

    //重置协程状态和入口函数，复用栈空间，不重新创建栈
    void reset(std::function<void()> cb);

    // 将当前协程切到到执行状态 
    // 前协程和正在运行的协程进行交换，前者状态变为RUNNING，后者状态变为READY
    void resume();

    // 当前协程让出执行权
    // 当前协程与上次resume时退到后台的协程进行交换，前者状态变为READY，后者状态变为RUNNING
    void yield();

    // 获取协程ID
    uint64_t getId() const { return m_id; }

    // 获取协程状态
    State getState() const { return m_state; }

public:
    static void SetThis(Fiber *f); // 设置当前正在运行的协程，即设置线程局部变量t_fiber的值
    static Fiber::ptr GetThis();
    static uint64_t TotalFibers(); // 获取总协程数
    static void MainFunc(); // 协程入口函数
    static uint64_t GetFiberId(); // 获取当前协程id

private:
    uint64_t m_id        = 0;   // 协程id
    uint32_t m_stacksize = 0;   // 协程栈大小
    State m_state        = READY;   // 协程状态
    ucontext_t m_ctx;   // 协程上下文
    void *m_stack = nullptr;    // 协程栈地址
    std::function<void()> m_cb;     // 协程入口函数
    bool m_runInScheduler;  // 本协程是否参与调度器调度
};

#endif