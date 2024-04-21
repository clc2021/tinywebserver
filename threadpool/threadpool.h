#ifndef THREADPOOL_H
#define THREADPOOL_H
// threadpool里的类：threadpool
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
//m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
template <typename T> // 线程池类。定义为模板类是为了代码复用。T任务类。
class threadpool
{
public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state); // 向请求队列中插入任务请求
    bool append_p(T *request);

private:
    static void *worker(void *arg); // 工作线程运行的函数，它不断从工作队列中取出任务并执行之
    void run(); // 内部访问私有成员函数run，完成线程处理要求。

// 这里涉及到三个部分：线程池、连接池和请求队列
// 线程池和请求队列都有最大数量限制，线程池用数组实现，请求队列用链表实现，连接池用数组实现
// 请求队列要有互斥锁
private:

    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    // 也就是说线程池是一个pthread_t类型的数组，这个数组是有大小的为thread number.
    pthread_t *m_threads;       //描述线程池的数组
    // 请求队列是一个T*类型的链表，这个链表是有最大限制的，为max requests.
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    
    sem m_queuestat;              // 是否有任务需要处理，这是信号量，有PV操作
    connection_pool *m_connPool;  // 数据库
    int m_actor_model;            // 事件处理模式。注意区别事件处理模式和事件触发模式。
};
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number]; //创造线程池
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i) // thread_number为线程数量，这里为8
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) // 在调用它的进程中创建1个线程
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) // 把每个线程都分离出去
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock(); // 加锁：互斥锁是一个线程内的。
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock(); // 所以一定是要有解锁操作的。
        return false;
    }
    request->m_state = state; // eg. http_conn类的m_state读为0 写为1，也就是要看请求是读请求还是写请求
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // V
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); 
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg; // 静态成员函数可以使用非静态成员的：作为参数传入
    pool->run(); 
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait(); // 信号量等待 P
        m_queuelocker.lock(); // 被唤醒后先加上锁。对请求队列的所有处理都要锁。
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model) // reactor模式
        {
            if (0 == request->m_state) // 读状态
            {
                if (request->read_once()) 
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process(); // 然后就交给http_conn类去处理了。这是在一个线程中。
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else // 写状态
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else // proactor模式
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
