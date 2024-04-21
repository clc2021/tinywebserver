#include "scheduler.h"
// 当前线程的调度器，同一个调度器下的所有线程共享同一个实例。
static thread_local Scheduler *t_scheduler = nullptr;
// 当前线程的调度协程，每个线程都独有一份。   
static thread_local Fiber *t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name) {
    std::cout << "Scheduler构造开头, Scheduler(threads, use_caller, name) = " << threads << "\t" << use_caller <<
                "\t" << name << std::endl; 
    assert(threads > 0);

    m_useCaller = use_caller;
    m_name      = name;
    if (use_caller) {
        --threads;
        std::cout << "Scheduler构造中, Fiber::GetThis()开始" << std::endl;
        Fiber::GetThis();
        std::cout << "Scheduler构造中, Fiber::GetThis()结束" << std::endl;
        assert(GetThis() == nullptr);
        t_scheduler = this;

        // caller线程的主协程不会被线程的调度协程run进行调度，而且，线程的调度协程停止时，应该返回caller线程的主协程
        // 在user caller情况下，把caller线程的主协程暂时保存起来，等调度协程结束时，再resume caller协程
        std :: cout << "Scheduler构造中, m_rootFiber开始" << std::endl;
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
        std :: cout << "Scheduler构造中, m_rootFiber结束" << std::endl;

        Thread::SetName(m_name);
        t_scheduler_fiber = m_rootFiber.get(); // 这是调度器
        m_rootThread = syscall(SYS_gettid); // 这是调度器所在线程ID
        m_threadIds.push_back(m_rootThread);
    } else {
        m_rootThread = -1;
    }
    m_threadCount = threads;
    std::cout << "Scheduler构造结束" << std::endl;
}

Scheduler *Scheduler::GetThis() { 
    return t_scheduler; 
}

Fiber *Scheduler::GetMainFiber() { 
    return t_scheduler_fiber;
}

void Scheduler::setThis() {
    t_scheduler = this;
}

Scheduler::~Scheduler() {
    std::cout << "Scheduler::~Scheduler()" << std::endl;
    // SYLAR_ASSERT(m_stopping);
    assert(m_stopping);
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
}

// 主要是初始化调度线程池，若只使用caller线程进行调度，那么这个方法什么也不做。
void Scheduler::start() {
    std::cout << "Scheduler中, start" << std::endl;
    MutexType::Lock lock(m_mutex);
    if (m_stopping) {
        std::cout << "Scheduler is stopped" << std::endl;
        return;
    }
    assert(m_threads.empty());
    m_threads.resize(m_threadCount); // 只使用caller：use_caller=T, threads=1——> resize变0
    for (size_t i = 0; i < m_threadCount; i++) {
        // 这个是Scheduler()里的start,这个绑定的意思是：创建一个线程就可以立马执行调度
          
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                      m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

// 判断调度器是否已经停止的方法，只有当所有的任务都被执行完了，调度器才可以停止。
bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle() { 
    std::cout << "Scheduler中, tickle开头" << std::endl;
    std::cout << "Scheduler中, tickle结尾" << std::endl;
}

void Scheduler::idle() {
    std :: cout << "Scheduler中, idle开头" << std :: endl;
    while (!stopping()) { // 调度器还没停止
        Fiber::GetThis()->yield();
        // 当前协程t_fiber->yield()代表从当前协程回到调度协程或者主协程
    }
    std::cout << "Scheduler中, idle结尾" << std::endl;
}

// 最后是调度器的stop方法，在使用了caller线程的情况下，
// 调度器依赖stop方法来执行caller线程的调度协程，
// 如果调度器只使用了caller线程来调度，那调度器真正开始执行调度的位置就是这个stop方法。
void Scheduler::stop() {
    std::cout << "Scheduler中, stop开头" << std::endl;
    if (stopping()) {
        return;
    }
    // 1. 设置m_stopping标志，该标志表示正在停止。
    m_stopping = true;
    // 2. 检测是否使用了caller线程进行调度， 如果使用了caller线程进行调度，那要保证stop方法是由caller线程发起的。
    if (m_useCaller) {
        assert(GetThis() == this);
    } else {
        assert(GetThis() != this);
    }
    std::cout << "Scheduler中, stop()中, m_threadCount = " << m_threadCount << std::endl;
    // 3. 通知其他调度线程的调度协程退出调度
    for (size_t i = 0; i < m_threadCount; i++) {
        std::cout << "Scheduler中, stop()中, for i循环的tikcle()开始" << std::endl;
        tickle();
        std::cout << "Scheduler中, stop()中, for i循环的tikcle()结束" << std::endl;
    }

    // 4. 通知当前线程的调度协程退出调度
    if (m_rootFiber) {
        std::cout << "Scheduler中, stop()中, m_rootF的tikcle()开始" << std::endl;
        tickle();
        std::cout << "Scheduler中, stop()中, m_rootF的tikcle()结束" << std::endl;
    }

    // 在use_caller情况下，调度器协程结束时，应该返回caller协程
    if (m_rootFiber) {
        std::cout << "Scheduler中, stop()中, m_rootF->resume()开始" << std::endl;
        m_rootFiber->resume();
        std::cout << "Scheduler中, stop()中, m_rootF->resume()结束" << std::endl;
    }

    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    for (auto &i : thrs) {
        i->join();
    }
    std::cout << "Scheduler中, stop结尾" << std::endl;
}

/*
内部有一个while(true)的循环，不停地从任务队列取任务并执行，
由于Fiber类改造过，每个被调度器执行的协程在结束时都会回到调度协程，所以不用担心跑飞的问题，
当任务队列为空时，代码会进入idle协程，但是idle协程什么也不做就直接yield了，
状态还是READY，所以这里还是个忙等待，CPU占用率高，——>处在RUNNING，直接READY
只有当调度器检测到停止标志时，idle协程才会真正结束，调度协程也会检测到idle的协程状态为TERM，
并且随之退出整个调度协程。（注意，这里退出的是调度协程！）
对于一个任务协程，只要其从resume返回了，那么不管它的状态是TERM还是READY，调度器都不会自动
将其再次加入调度。
*/
void Scheduler::run() {
    std::cout << "Scheduler中, run开头" << std::endl;
    // set_hook_enable(true);
    setThis();
    // m_rootThread=调用调度器的线程ID或者-1
    if (syscall(SYS_gettid) != m_rootThread) { // eg. m_rootThread=5, 当前=1554
        std::cout <<"Scheduler中, run()开头判断, m_rootThread = " << m_rootThread <<"\tsyscall(SYS_gettid) = " << syscall(SYS_gettid) <<" , 所以要自己构造。" << std::endl;
        t_scheduler_fiber = Fiber::GetThis().get(); // t_fiber
    }

    // 这里声明了idle_fiber和cb_fiber，都是智能指针协程。
    // 这里创建idle_fiber对象，立刻执行idle()
    std :: cout << "Scheduler的run()函数, 定义idle_fiber前, 绑定的是Scheduler的idle?" << std::endl;
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    std :: cout << "Scheduler的run()函数, 定义idle_fiber后" << std::endl;
    Fiber::ptr cb_fiber; 

    ScheduleTask task; //    也是m_tasks类型，有[协程指针，函数指针，指定线程]
    while (true) {
        task.reset(); // 这个任务类型也有reset()，这个reset很简单，数据全部空或者-1
        bool tickle_me = false; // 是否tickle其他线程进行任务调度
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();
            // 遍历所有调度任务
            while (it != m_tasks.end()) {
                if (it->thread != -1 && it->thread != syscall(SYS_gettid)) {
                    // 指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，
                    // 然后跳过这个任务，继续下一个。
                    ++it;
                    tickle_me = true;
                    continue;
                }

                // 找到一个未指定线程，或是指定了当前线程的任务
                assert(it->fiber || it->cb);

                if(it->fiber && it->fiber->getState() == Fiber::RUNNING) {
                    ++it;
                    continue;
                }
                    
                // 当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it++);
                ++m_activeThreadCount;
                break;
            }
            // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
            tickle_me |= (it != m_tasks.end());
        }

        if (tickle_me) {
            tickle();
        }

        /*
        下面这段是执行重点：
        如果任务队列里的任务是协程，直接resume()
        如果任务队列里的任务是函数，把函数放在协程里去resume()
        如果任务队列空，执行idle()
        */           
        if (task.fiber) {
            // resume协程，resume返回时，协程要么执行完了，要么半路yield了，总之这个任务就算完成了，活跃线程数减一
            std::cout << "Scheduler的run()里, task.fiber开头" << std::endl;
            task.fiber->resume(); // 
            --m_activeThreadCount; 
            task.reset();
            std::cout << "Scheduler的run()里, task.fiber结尾" << std::endl;
        } else if (task.cb) { // 也就是说，这个cb也会被放到协程中去执行
            std::cout << "Scheduler的run()里, task.cb开头" << std::endl;
            if (cb_fiber) {
                cb_fiber->reset(task.cb);
            } else {
                cb_fiber.reset(new Fiber(task.cb));
            }
            task.reset(); // task的reset只是置空
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset(); // fiber的reset是可以从TERM——>READY，而它转去的MainFunc可以——>TERM
        } else { // 进到这个分支情况一定是任务队列空了，调度idle协程即可
            // 如果调度器没有调度任务，那么idle协程会不停地resume和yield，不会结束，
            //就是：idle_fiber->resume()，然后再yield()回来
            // 如果idle协程结束了，那一定是调度器停止了
            if (idle_fiber->getState() == Fiber::TERM) {
                // SYLAR_LOG_DEBUG(g_logger) << "idle fiber term";
                std::cout << "Scheduler中, idle fiber term" << std::endl;
                break;
            }
            ++m_idleThreadCount;
            idle_fiber->resume();
            --m_idleThreadCount;
        }
    }
    std::cout << "Scheduler中, run结尾" << std::endl;
}
