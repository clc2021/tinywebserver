/*
关于调度任务的定义：
对于协程调度器来说，协程可以作为调度任务，但实际上函数应该也可以，
∵函数也是可执行对象，调度器应当支持直接调度一个函数，——>代码实现：将函数包装成协程。
协程调度器的实现重点以协程为主。

多线程：
多线程可以提高协程调度的效率，那么能不能把调度器所在的线程即caller线程
也加入进来作为调度线程呢？
eg. main里定义的调度器，能不能把main所在的线程也用来执行调度任务？ 可以！
在实现相同调度能力的情况下（指能够同时调度的协程数量），线程数越小，线程切换的开销也就越小，效率就更高，
∴ 调度器所在的线程，也应该支持用来执行调度任务，
甚至调度器完全可以不创建新的线程，而只使用caller线程来进行调度任务，比如只使用main所在的线程来进行协程调度。

调度器运行方式：
调度器创建后，首先会在内部创建一个调度线程池，
调度开始后，所有调度线程按顺序从任务队列里取任务执行，线程数越多，能够调用的任务数越多，
当所有任务都调度完后，调度线程就会停下来等新的任务进来。

添加调度任务：
本质是往调度器的任务队列里塞任务，但是只添加调度任务是不够的，
还应该有一种方式用于通知调度线程有新的任务加进来了，
因为调度线程并不一定知道有新任务进来了。
当然调度线程也可以不停地轮询有没有新任务，但是这样CPU占用率会很高。

调度器的停止：
调度器应该支持停止调度的功能，以便回收调度线程的资源，
只有当所有的调度线程都结束后，调度器才算真正停止。
||
——
调度器的设计：
调度器内部维护一个任务队列（协程队列）和调度线程池，
开始调度后，线程池从任务队列按顺序取任务执行，
调度线程可以包含caller线程，
当全部任务执行完了，线程池停止调度，等新的任务进来。
添加新的任务后，通知线程池有新的任务进来了，线程池重新开始运行调度，
停止调度时，各调度线程退出，调度器停止工作。
*/

/*
程序设计：
sylar的协程调度模块
支持多线程，
支持使用caller线程进行调度，（caller线程，即：调度器所在的线程）
支持将函数或协程绑定到一个具体的线程上执行，

调度器的初始化：
sylar协程调度器在初始化时支持传入线程数和use_caller——>是否使用caller线程，
use_caller=true：线程数-1；初始化一个属于caller线程的调度协程；
use_caller=true, threads=1：

问：子协程和子协程之间切换为什么容易跑飞？
答：关键在于每个线程只有两个局部变量，就是说一个线程最多知道两个协程的上下文，一个当前协程的另一个主协程的。
如果子协程和子协程切换，那这两个上下文都会变成子协程的上下文，线程主协程的上下文丢失了，程序也就跑飞了。
如果不改变这种局部，就只能线程主协程去充当调度协程，这就相当于又回到了让用户充当调度器的情况。
那么，如何改变这种情况呢？其实非常简单，只需要给每个线程增加一个线程局部变量用于保存调度协程的上下文就可以了，
这样，每个线程可以同时保存三个协程的上下文，
一个是当前正在执行的协程上下文，
另一个是线程主协程的上下文，
最后一个是调度协程的上下文。
有了这三个上下文，协程就可以根据自己的身份来选择和每次和哪个协程进行交换


创建好后：用调度器的schedule()添加调度任务。

start(): 启动调度。先创建线程池，初始化数量是根据use_caller来的。
        调度线程一旦创建，就会立即从任务队列中取任务执行，
        特殊地：如果初始化时指定线程数量为1 & use_caller=true，
                那么start什么也不做，因为不需要创建新线程用于调度。？？？
并且，由于没有创建新的调度线程，那只能由caller线程的调度协程来负责调度协程，
而caller线程的调度协程的执行时机与start方法并不在同一个地方。                

run()：调度协程。调度协程负责从任务队列中取任务执行，取出的任务即子协程。
        如果任务队列空了，会切换到一个idle协程，这个idle协程什么也不做，
            等有新任务进来时，idle协程才退出并回到调度协程，重新开始下一轮调度。

idle()：当调度器没有协程可调度时，该怎么办？
        sylar：如果任务队列空了，调度协程会不停地检测任务队列，看看有没有新任务——>忙等
  

在非caller线程里，调度协程就是调度线程的主线程，
但在caller线程里，调度协程并不是caller线程的主协程，而是相当于caller线程的子协程，
这在协程切换时会有大麻烦（这点是sylar协程调度模块最难理解的地方），
如何处理这个问题将在下面的章节专门进行讨论。

schedlue()：添加调度任务，schedule(协程or函数，线程ID)
表示是否将这个协程or函数绑定到一个具体的线程上执行，
如果任务队列为空，那么在添加任务之后，
要调用一次tickle方法以通知各调度线程的调度协程有新任务来了。

stop()：
use_caller=false： 没使用caller线程进行调度，简单地等各个调度协程退出就行。
use_caller=true：caller线程也要参与调度，
这时，调度器初始化时记录的属于caller线程的调度协程就要起作用了，
    在调度器停止前，应该让这个caller线程的调度协程也运行一次，
    让caller线程完成调度工作后再退出。如果调度器只使用了caller线程进行调度，
    那么所有的调度任务要在调度器停止时才会被调度。
代码过程：
1. 设置m_stopping标志，该标志表示正在停止
2. 检测是否使用了caller线程进行调度，如果使用了caller线程进行调度，那要保证stop方法是由caller线程发起的
3. 通知其他调度线程的调度协程退出调度
4. 通知当前线程的调度协程退出调度
5. 如果使用了caller线程进行调度，那执行一次caller线程的调度协程（只使用caller线程时的协程调度全仰仗这个操作）
6. 等caller线程的调度协程返回
7. 等所有调度线程结束
*/

/*
其他情况讨论：
1. 任务协程执行过程中主动yield怎么处理？ yield前先将自己重新添加到调度器的任务队列。
2. 只使用调度器所在的线程进行调度，use_caller=T, threads=1。
    典型的就是main函数中定义调度器并且只使用main函数线程执行调度任务。
    这种场景下，可以认为是main函数先攒下一波协程，然后切到调度协程，
    把这些协程消耗完后再从调度协程切回main函数协程。每个协程在运行时
    也可以继续创建新的协程并加入调度。如果所有协程都调度完了，并且没有
    创建新的调度任务，那么下一步就是讨论idle该如何处理。
3. 只有main函数线程参与调度的调度执行时机。
sylar：把调度器的开始点放在了stop方法中。 
也就是，调度开始即结束，干完活就下班。
4. 额外创建了调度线程时的调度执行时机。
如果不额外创建线程，也就是线程数为1并且use_caller，那所有的调度任务都在stop()时才会进行调度。
若额外创建：在添加完调度任务之后任务马上就可以在另一个线程中调度执行。
归纳起来，如果只使用caller线程进行调度，
那所有的任务协程都在stop之后排队调度，
如果有额外线程，那任务协程在刚添加到任务队列时就可以得到调度。
5. 协程中的异常不处理。

*/
#ifndef SCHEDULER_H__
#define SCHEDULER_H__

#include <functional>
#include <list>
#include <memory>
#include <string>
#include "fiber.h"
//#include "log.h"
#include "thread.h"
#include <vector>
#include <assert.h>
#include <iostream>
#include <string.h>

class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;
    
    Scheduler(size_t threads, bool use_caller, const std::string &name);
    virtual ~Scheduler();
    const std::string &getName() const { return m_name; }
    static Scheduler *GetThis();
    static Fiber *GetMainFiber();
    template <class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            need_tickle = scheduleNoLock(fc, thread);
        }

        if (need_tickle) {
            tickle(); // 唤醒idle协程
        }
    }
    void start();
    void stop();

protected:
    virtual void tickle();
    void run();
    virtual void idle();
    virtual bool stopping();
    void setThis();
    bool hasIdleThreads() { return m_idleThreadCount > 0; }

private:
    template <class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        // need_tickle代表是否需要唤醒idle协程，当任务队列为空的时候就唤醒idle协程，所以此时need_tickle=T
        bool need_tickle = m_tasks.empty(); 
        ScheduleTask task(fc, thread);
        if (task.fiber || task.cb) { 
            m_tasks.push_back(task);
        }
        // 之前似乎分析过，当任务为0——>1的时候，唤醒idle都是真的
        return need_tickle;
    }

private:
    // 调度任务：协程和函数二选一，可以指定在哪个线程上调度。
    struct ScheduleTask {
        Fiber::ptr fiber;
        std::function<void()> cb;
        int thread;

        ScheduleTask(Fiber::ptr f, int thr) {
            fiber  = f;
            thread = thr;
        }
        ScheduleTask(Fiber::ptr *f, int thr) {
            fiber.swap(*f);
            thread = thr;
        }
        ScheduleTask(std::function<void()> f, int thr) {
            cb     = f;
            thread = thr;
        }
        ScheduleTask() { thread = -1; }

        void reset() {
            fiber  = nullptr;
            cb     = nullptr;
            thread = -1;
        }
    };

private:
    std::string m_name;
    MutexType m_mutex;
    std::vector<Thread::ptr> m_threads;
    std::list<ScheduleTask> m_tasks;
    std::vector<int> m_threadIds;
    size_t m_threadCount = 0; // 工作线程数量，不包含use_caller的主线程
    std::atomic<size_t> m_activeThreadCount = {0};
    std::atomic<size_t> m_idleThreadCount = {0};

    bool m_useCaller; 
    Fiber::ptr m_rootFiber; // use_caller为true时，调度器所在线程的调度协程
    int m_rootThread = 0; // use_caller为true时，调度器所在线程的id
    bool m_stopping = false; // 是否正在停止
};

#endif
