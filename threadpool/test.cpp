#include <iostream>
#include "iomanager.h"
using namespace std;
void testCoroutine() {
    // 在协程中执行的简单函数，打印消息
    cout << "In testCoroutine: " << endl;
    std::cout << "Coroutine in thread: " << syscall(SYS_gettid) << " executing." << std::endl;
}

int main() {
    cout << "先定义一个IOManager对象:" << endl;
    IOManager *m_iom = new IOManager(2, true);
    cout << "定义完毕" << endl;
    // 注册一个读事件，当有数据可读时，执行 testCoroutine 函数
    cout << "addEvent开始: " << endl;
    m_iom->addEvent(0, IOManager::READ, &testCoroutine);   
    cout << "addEvent结束" << endl;

    // 等待 IOManager 执行
    return 0;
}