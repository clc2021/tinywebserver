#ifndef THREADPOOL_H
#define THREADPOOL_H
// threadpool里的类：threadpool
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
template <typename T>
class threadpool
{
public:
// threadpool(reactor还是proactor?, 数据库连接池的连接, 8个线程, 1万个请求)
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state); // addTask()
    bool append_p(T *request); // addTask_p()

private:
    static void *worker(void *arg); 
    void run(); 

private:

    int m_thread_number;        
    int m_max_requests;         
    pthread_t *m_threads;      
    std::list<T *> m_workqueue; 
    locker m_queuelocker;      
    
    sem m_queuestat;              
    connection_pool *m_connPool;  
    int m_actor_model;          
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
