/*
// 单例模式 懒汉式的线程安全
// 写出懒汉式安全的并证明它线程安全
#include <iostream>
#include <thread>
#include <mutex>

class Single {
private:
    Single() {}
    ~Single() {}
    Single(const Single& s) = delete;
    const Single& operator=(const Single& s) = delete;
    static std::mutex mtx;
    static Single* instance;

public:
    static Single* getInstance() {
        if (instance == nullptr) {
            std::unique_lock<std::mutex> lock(mtx);
            if (instance == nullptr) {
                instance = new Single();
            }
        }

        return instance;
    }
    void print() {
        std::cout << "线程ID\t" << std::this_thread::get_id() << "\t" << this << std::endl;
    }
};

std::mutex Single::mtx;
Single* Single:: instance = nullptr;

int main() {
    std::thread thd1([]{ Single::getInstance()->print(); });
    std::thread thd2([]{ Single::getInstance()->print(); });
    std::thread thd3([]{ Single::getInstance()->print(); });
    thd1.join();
    thd2.join();
    thd3.join();

    return 0;
}
*/

#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
class Single {
private:
    Single() {}
    ~Single() {}
    Single(const Single& s) = delete;
    const Single& operator=(const Single& s) = delete;
    static std::mutex mtx;
    static Single* instance;

public:
    static Single* getInstance() {
        return instance;
    }
    void print() {
        std::cout << "线程ID\t" << std::this_thread::get_id() << "\t" << this << std::endl;
    }
};

std::mutex Single::mtx;
Single* Single:: instance = new Single();

int main() {
    // std::thread thd1([]{ Single::getInstance()->print(); });
    // std::thread thd2([]{ Single::getInstance()->print(); });
    // std::thread thd3([]{ Single::getInstance()->print(); });
    // thd1.join();
    // thd2.join();
    // thd3.join();
    for (int i = 0; i < 5; i++) {
        std::thread thd([]{ Single::getInstance()->print(); });
        thd.join();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}