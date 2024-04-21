/**adId
 * @file thread.cc
 * @brief 线程封装实现
 * @version 0.1
 * @date 2021-06-15
 */
#include "thread.h"

static thread_local Thread *t_thread          = nullptr;
static thread_local std::string t_thread_name = "UNKNOW";

Thread *Thread::GetThis() {
    return t_thread;
}

const std::string &Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string &name) {
    if (name.empty()) {
        return;
    }
    if (t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string &name)
    : m_cb(cb)
    , m_name(name) {
    std::cout << "Thread构造开头, name = " << name << std::endl;
    if (name.empty()) {
        m_name = "UNKNOW";
    }
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) {
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
    std::cout << "Thread构造结束" << std::endl;
}

Thread::~Thread() {
    if (m_thread) {
        pthread_detach(m_thread);
    }
}

void Thread::join() {
    if (m_thread) {
        int rt = pthread_join(m_thread, nullptr);
        if (rt) {
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void *Thread::run(void *arg) {
    Thread *thread = (Thread *)arg;
    t_thread       = thread;
    t_thread_name  = thread->m_name;
    // thread->m_id   = sylar::GetThreadId();
    thread->m_id = syscall(SYS_gettid);
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    // 这种交换的操作可能在多线程环境下是线程安全的，因为 std::function 的 swap 操作通常是原子的。
    /*
    function<void()> 表示一个可以包装任何不接受参数且返回类型为 void 的
    函数、函数指针、Lambda 表达式等的 function 对象。
    这种声明方式通常用于表示回调函数或者一些无参操作的函数对象。
    */
    std::function<void()> cb; 
    /*
    Q：为什么要做这样的交换呢？
    答：多线程环境中，如果直接访问共享数据，会涉及线程安全问题。
    通过swap()操作，可以在不使用锁的情况下交换数据。
    cb得到了原先m_cb中的回调函数，后续执行cb()相当于执行了线程对象中原先存储的回调函数。

    “std::function 成为一种灵活的工具，
    特别适用于需要在运行时动态确定要调用的函数或函数对象的情况。
    在你的代码中，它被用来存储线程的回调函数，通过 swap 操作，
    可以在不涉及锁的情况下安全地获取并执行回调函数。”
    */
    cb.swap(thread->m_cb); // 交换cp和线程对象的成员m_cb

    thread->m_semaphore.notify();

    cb();
    return 0;
}

