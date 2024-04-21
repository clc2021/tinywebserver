#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "iomanager.h"
#include "thread.h"
using namespace std;
int sockfd;
void watch_io_read();

// 写事件回调，只执行一次，用于判断非阻塞套接字connect成功
void do_io_write() {
    std::cout << "\t写事件回调开头" << std::endl;
    int so_err;
    socklen_t len = size_t(so_err);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_err, &len);
    if(so_err) {
        std::cout << "\t写事件回调, 连接失败" << std::endl;
        return;
    } 
    std::cout << "\t写事件回调, 连接成功" << std::endl;
    std::cout << "\t写事件回调结尾" << std::endl;
}

// 读事件回调，每次读取之后如果套接字未关闭，需要重新添加
void do_io_read() {
    std::cout << "\t读事件回调开头" << std::endl;
    char buf[1024] = {0};
    int readlen = 0;
    readlen = read(sockfd, buf, sizeof(buf));
    if(readlen > 0) {
        buf[readlen] = '\0';
        std::cout << "\t在读事件回调中, 读了 " << readlen << " 字节, 读的内容是: " << buf << std::endl;
    } else if(readlen == 0) {
        std::cout << "\t在读事件回调中, 读完空了, 关闭套接字" << std::endl;
        close(sockfd);
        return;
    } else {
        std::cout << "\t在读事件回调中, 读出错, 关闭套接字 errno=" << errno << ", errstr=" << strerror(errno) << std::endl;
        close(sockfd);
        return;
    }
    // read之后重新添加读事件回调，这里不能直接调用addEvent，
    // 因为在当前位置fd的读事件上下文还是有效的，直接调用addEvent相当于重复添加相同事件
    std::cout << "\t在读事件回调中, 重新添加开头" << std::endl;
    IOManager::GetThis()->schedule(watch_io_read); // 重新添加事件回调
    std::cout << "\t在读事件回调中, 重新添加开头" << std::endl;
    std::cout << "\t读事件回调结尾" << std::endl;
}

void watch_io_read() {
    std::cout << "\twatch_io_read()开头, 用来添加读事件" << std::endl;
    IOManager::GetThis()->addEvent(sockfd, IOManager::READ, do_io_read);
    std::cout << "\twatch_io_read()结尾" << std::endl;
}

void test_io() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // 创建套接字
    assert(sockfd > 0);
    fcntl(sockfd, F_SETFL, O_NONBLOCK); // 设置为非阻塞模式

    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(5005);
    // inet_pton(AF_INET, "10.10.19.159", &servaddr.sin_addr.s_addr);
    inet_pton(AF_INET, "172.31.17.11", &servaddr.sin_addr.s_addr); //服务器地址 172.31.17.11:5005

    int rt = connect(sockfd, (const sockaddr*)&servaddr, sizeof(servaddr));
    if(rt != 0) {
        if(errno == EINPROGRESS) {
            std::cout << "\t在test_io里, EINPROGRESS, 出现errno错误" << std::endl;
            // 注册写事件回调，只用于判断connect是否成功,非阻塞的TCP套接字connect一般无法立即建立连接，要通过套接字可写来判断connect是否已经成功。
            cout << "\t在test_io里, 注册写事件前" << endl;
            IOManager::GetThis()->addEvent(sockfd, IOManager::WRITE, do_io_write);
            cout << "\t在test_io里, 注册写事件后" << endl;
            cout << "\t在test_io里, 注册读事件前" << endl;
            // 注册读事件回调，注意事件是一次性的
            IOManager::GetThis()->addEvent(sockfd, IOManager::READ, do_io_read);
            cout << "\t在test_io里, 注册读事件后" << endl;
        } else {
            std::cout << "\t在test_io里, 连接错误, errno:" << errno << ", errstr:" << strerror(errno) << std::endl;
        }
    } else {
        std::cout << "\t在test_io里, else错误, errno:" << errno << ", errstr:" << strerror(errno) << std::endl;
    }
}


int main(int argc, char *argv[]) {
    // test_iomanager();
    std:: cout<< "\t在main函数, 定义iom前" << std::endl;
    IOManager iom(10); // 演示多线程下IO协程在不同线程之间切换
    std:: cout<< "\t在main函数, 定义iom后" << std::endl;
    cout << "\t在main函数, iom.schedule(test_io)前" << endl;
    iom.schedule(test_io);
    std :: cout << "\t在main函数, iom.schedule(test_io)后" << std::endl;
    return 0;
}