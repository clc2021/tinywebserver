/*
这段代码涉及到了多个C++11的特性，以下是一些主要的特性：

线程（std::thread）：
std::thread是C++11引入的线程库的一部分，用于创建和管理线程。在这个代码中，
线程池使用std::thread创建了多个工作线程来执行任务。

互斥量（std::mutex）和锁（std::unique_lock）：
std::mutex是C++11引入的用于实现互斥访问的互斥量的类。std::unique_lock是一个
用于管理互斥量的RAII（Resource Acquisition Is Initialization）类。在这个代码中，
queueMutex是一个互斥量，用于保护对任务队列的访问，而std::unique_lock用于在需要
时锁定和解锁互斥量。
条件变量（std::condition_variable）：
std::condition_variable是C++11引入的条件变量类，用于在多个线程之间进行同步。
在这个代码中，condition是一个条件变量，它被用于在任务队列为空时让工作线程等待，
直到有新的任务被添加到队列中。

移动语义（std::move）：
std::move是C++11引入的用于转移语义的函数，它将一个左值转换为右值引用。
在这个代码中，使用std::move来将任务对象从任务队列中移动到工作线程中执行。

智能指针：
虽然在代码中没有直接使用智能指针，但是线程池的设计使用了RAII（Resource 
Acquisition Is Initialization）原则，类似于智能指针的思想，确保资源在
对象生命周期结束时正确释放。

Lambda 表达式：
代码中使用了Lambda表达式，例如在线程池的构造函数中和任务提交函数中。Lambda
表达式是C++11引入的一种方便定义匿名函数的方式。
这些特性共同使得这段代码更加现代化，更好地利用了C++11引入的一些重要功能，
如线程支持、互斥量、条件变量等，以实现并发编程的目的。

*/
/*
#include <iostream>
#include <unistd.h>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

using namespace std;

class ThreadPool {
public:
    ThreadPool(size_t numThreads) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            threads.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);  // lock
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) {
                            return;
                        }
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template <class F, class... Args>
    void enqueue(F&& f, Args&&... args) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace([f, args...] { f(args...); });
        }
        condition.notify_one();  // wake up thread
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& thread : threads) {
            thread.join();
        }
    }

private:
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

void printNumber(int num) {
    sleep(5);  // ssimulate operational implementation
    std::cout << num << std::endl;
}

int main() {
    ThreadPool pool(4);

    // submit tasks to thread pool
    for (int i = 0; i < 10; ++i) {
        pool.enqueue(printNumber, i);
    }

    // wait for all tasks to be completed
    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
*/


// 用线程池来打印队列。
#include <thread>
#include <chrono>
#include <vector>
#include <queue> 
#include <functional>
#include <mutex>
#include <condition_variable>
#include <iostream>

int number = 0;
void printNumber() {
    std::cout << number++ << std::endl; 
}

class ThreadPool { // 线程池
private:
    std::vector<std::thread> threads; // 线程池
    int threadNum; // 个数
    std::queue<std::function<void()>> taskQue; // 任务队列
    std::mutex mtx;
    std::condition_variable cond;
    bool stop;

public:
    ThreadPool(int num): threadNum(num), stop(false) {
        for (int i = 0; i < threadNum; i++) {
            threads.emplace_back( [this] {  this->dealTask(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            stop = true;
        }
        cond.notify_all();
        for (auto& thd : threads) {
            thd.join();
        }
    }

    void addTask(std::function<void()> task) {
        std::unique_lock<std::mutex> lock(mtx); // 上锁
        taskQue.push(task);
        cond.notify_one(); // 唤醒
    }

    void dealTask() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cond.wait(lock, [this] { return stop || !taskQue.empty(); });
                if (stop && taskQue.empty())
                    return ;
                task = taskQue.front();
                taskQue.pop();
            }
            task();
        }
    }
    
};

int main(void) {
    ThreadPool* threadpool = new ThreadPool(8);
    for (int i = 0; i < 10; i++) {
        threadpool->addTask(printNumber);
    }
    delete threadpool;
    return 0;
}