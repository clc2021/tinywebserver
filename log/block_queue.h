/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁

生产者通过调用push()方法向队列中添加元素，
消费者通过调用pop()方法从队列中取出元素。

这个队列支持多个生产者和多个消费者同时操作。当队列满时，生产者会被阻塞，
直到队列有空位为止；当队列空时，消费者会被阻塞，直到队列有元素为止。

这种实现利用了互斥锁和条件变量，确保了多个线程之间的同步和互斥。
生产者在向队列添加元素时需要先获取互斥锁，防止多个生产者同时操作
导致数据不一致；消费者在取出元素时也需要获取互斥锁，同样防止多个
消费者同时操作导致数据不一致。当队列满或者空时，生产者或者消费者
会通过条件变量等待，直到条件满足（队列不满或者队列不空）时被唤醒。

**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000) // 阻塞队列的容量为1千
    {
        if (max_size <= 0)
        {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear() // 清空，要加锁
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue() 
    {
        m_mutex.lock();
        if (m_array != NULL)
            delete [] m_array; // 清空就是整个数组清除

        m_mutex.unlock();
    }

    //判断队列是否满了
    bool full() 
    {
        m_mutex.lock();
        if (m_size >= m_max_size) // 判断也要锁上判断，不然其他的线程可能对它加加减减
        {

            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    //判断队列是否为空
    bool empty() 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //返回队首元素
    bool front(T &value) 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    //返回队尾元素
    bool back(T &value) 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size() 
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }

    int max_size()
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }

    //往队列添加元素，需要将所有使用队列的线程先唤醒，
    //当有元素push进队列，相当于生产者生产了一个元素，
    //若当前没有线程等待条件变量，则唤醒无意义。
    bool push(const T &item)
    {
        m_mutex.lock();
        if (m_size >= m_max_size) // 不能再push了
        {

            m_cond.broadcast(); // 广播唤醒线程
            m_mutex.unlock();
            return false;
        }

        //将新增数据放在循环数组的对应位置-->就是说不保存数据而是直接插入新数据,那么一定有数据失效
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {

        m_mutex.lock();
        //多个消费者的时候，这里要是用while而不是if
        while (m_size <= 0) // 不够pop的
        {
            //当重新抢到互斥锁，pthread_cond_wait返回为0
            // ret = pthread_cond_wait(&m_cond, m_mutex)返回ret==0
            //get返回pthread_mutex_t *
            if (!m_cond.wait(m_mutex.get())) // 这里代表没抢到互斥锁
            {
                m_mutex.unlock();
                return false;
            }
        }

        //取出队列首的元素，这里需要理解一下，使用循环数组模拟的队列 
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //增加了超时处理
    //在pthread_cond_wait基础上增加了等待的时间，只指定时间内能抢到互斥锁即可
    //其他逻辑不变
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex; // 互斥锁。这两个地方声明了这个初始变量是相当于已经初始化了
    cond m_cond; // 条件

    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif
