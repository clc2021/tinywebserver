/**
 * @file fiber.cpp
 * @brief 协程实现
 * @version 0.1
 * @date 2021-06-15
 */
#include <atomic>
#include "fiber.h"
//#include "config.h"
//#include "log.h"
//#include "macro.h"
#include "scheduler.h"

// 全局静态变量，用于生成协程id
static std::atomic<uint64_t> s_fiber_id{0};
// 全局静态变量，用于统计当前的协程数
static std::atomic<uint64_t> s_fiber_count{0};

// 线程局部变量，当前线程正在运行的协程
static thread_local Fiber *t_fiber = nullptr; // 线程的当前协程
// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针形式
static thread_local Fiber::ptr t_thread_fiber = nullptr; // 线程的主协程

class MallocStackAllocator {
public:
    static void *Alloc(size_t size) { return malloc(size); }
    static void Dealloc(void *vp, size_t size) { return free(vp); }
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() {
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

Fiber::Fiber() {
    SetThis(this); // t_fiber=f=this，当前协程是当前协程
    m_state = RUNNING;

    assert(getcontext(&m_ctx) == 0); // 保存当前的上下文
    ++s_fiber_count;
    m_id = s_fiber_id++; // 协程id从0开始，用完加1
    std :: cout << "Fiber::Fiber() main id = " << m_id << "协程个数=" << s_fiber_count << std::endl;
}

void Fiber::SetThis(Fiber *f) { 
    t_fiber = f; 
}

// 获取当前协程，同时充当初始化当前线程主协程的作用，这个函数在使用协程之前要调用一下
Fiber::ptr Fiber::GetThis() {
    if (t_fiber) {
        std :: cout << "Fiber中, GetThis()t_fiber存在" << std::endl;
        return t_fiber->shared_from_this();
    }
    std::cout << "Fiber中, GetThis()t_fiber不存在, 建一个" << std::endl;
    Fiber::ptr main_fiber(new Fiber);
    //assert(t_fiber == main_fiber.get());
    assert(t_fiber == main_fiber.get());
    t_thread_fiber = main_fiber;
    return t_fiber->shared_from_this();
}

// 带参数的构造函数用于创建其他协程，需要分配栈
// Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) 
    : m_id(s_fiber_id++)
    , m_cb(cb)
    , m_runInScheduler(run_in_scheduler) {
    ++s_fiber_count;
    // m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
    m_stacksize = stacksize ? stacksize : 128 * 1024;
    m_stack     = StackAllocator::Alloc(m_stacksize);

    assert(getcontext(&m_ctx) == 0); // 保存当前的上下文

    m_ctx.uc_link          = nullptr;
    m_ctx.uc_stack.ss_sp   = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0); // 修改，把当前的上下文改成可以执行MainFunc的，
    // 等到setcontext或者swapcontext就激活, MainFunc里有m_cb()函数。

    std::cout << "Fiber::Fiber() id = " << m_id << "协程个数 = " << s_fiber_count << "\t"  << "m_cb"<< std::endl;
}

// 线程的主协程析构时需要特殊处理，因为主协程没有分配栈和cb
Fiber::~Fiber() {
    std::cout << "Fiber::~Fiber() id = " << m_id << "析构开头，协程个数 = " << s_fiber_count << std::endl;
    --s_fiber_count;
    if (m_stack) {
        // 有栈，说明是子协程，需要确保子协程一定是结束状态
        // assert(m_state == TERM);
        assert(m_state == TERM);

        StackAllocator::Dealloc(m_stack, m_stacksize);
        std::cout << "有栈, 说明是子协程, dealloc stack, id = " << m_id << "协程个数 = " << s_fiber_count << std::endl;
    } else {
        // 没有栈，说明是线程的主协程
        assert(!m_cb);              // 主协程没有cb
        assert(m_state == RUNNING); // 主协程一定是执行状态

        Fiber *cur = t_fiber; // 当前协程就是自己
        if (cur == this) {
            SetThis(nullptr);
        }
    }
}

/*
这里为了简化状态管理，强制只有TERM状态的协程才可以重置，
但其实刚创建好但没执行过的协程也应该允许重置的
*/
void Fiber::reset(std::function<void()> cb) {
    assert(m_stack);
    assert(m_state == TERM);
    m_cb = cb;

    assert(getcontext(&m_ctx) == 0);
    m_ctx.uc_link          = nullptr;
    m_ctx.uc_stack.ss_sp   = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    // MainFunc里执行cb()，但是是：
    // 当前协程引用计数+1 ——> cb() ——> 当前协程引用计数-1 ——> yield()；
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = READY;
}

void Fiber::resume() {
    assert(m_state != TERM && m_state != RUNNING);
    SetThis(this); // 当前协程设置为this, this是这个fiber对象，比如x->resume()，this就是这个x
    m_state = RUNNING;
    // m_runInScheduler=T: 主协程-调度协程-子协程， yield()子协程->调度协程
    // m_runInScheduler=F: 主协程-子协程，yield()子协程->调度协程，即上下文和调度协程无关，可以脱离调度器

    if (m_runInScheduler) {// swap(调度协程，当前协程)
        assert(swapcontext(&(Scheduler::GetMainFiber()->m_ctx), &m_ctx) != -1); // 调度协程, 当前协程
    } else { // 
        assert(swapcontext(&(t_thread_fiber->m_ctx), &m_ctx) != -1); // 主协程, 当前协程
    }
}

void Fiber::yield() {
    // 协程运行完之后会自动yield一次，用于回到主协程，此时状态已为结束状态???
    assert(m_state == RUNNING || m_state == TERM); // 确认是执行或者结束？
    SetThis(t_thread_fiber.get()); //t_fiber=t_thread_fiber.get()
    if (m_state != TERM) { // 如果协程是运行态的话，让它变成ready
        m_state = READY;
    }

    // 如果协程参与调度器调度，那么应该和调度器的主协程进行swap，而不是线程主协程
    if (m_runInScheduler) {
        // 协程调度器参与：保存到m_ctx，然后执行调度器的协程。
        assert(swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx)) != -1);
    } else {
        // 协程调度器不参与：保存到m_ctx中，然后执行主协程。
        assert(swapcontext(&m_ctx, &(t_thread_fiber->m_ctx)) != -1);
    }
}

// 这里没有处理协程函数出现异常的情况，同样是为了简化状态管理，
// 并且个人认为协程的异常不应该由框架处理，应该由开发者自行处理 
void Fiber::MainFunc() {
    Fiber::ptr cur = GetThis(); // GetThis()的shared_from_this()方法让引用计数加1
    assert(cur);

    cur->m_cb();
    cur->m_cb    = nullptr;
    cur->m_state = TERM;

    auto raw_ptr = cur.get(); // 手动让t_fiber的引用计数减1
    cur.reset();
    raw_ptr->yield();
}
