#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <iostream>

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

using namespace std;
int READ_BUFFER_SIZE = 50;
#define WRITE_BUFFER_SIZE 1024

int aveInt(int v, int q, ...) {
    int ret = 0;
    int i = v; 
    va_list ap; // 定义va_list类型的变量，叫ap
    va_start(ap, q); // ap指向第一个变参的位置。第二个参数用最后一个固定参数初始化。
    while (i > 0) {
        ret += va_arg(ap, int); // 通过移动ap来获取变参内容,9->6->0
        cout << "i = " << i << ", ret = " << ret << endl;
        i--;
    }
    va_end(ap); // 清空ap
    return ret /= v; 
}
void vatest() {
    //cout << aveInt(2, 2, 3) << endl; //ap=2
    //cout << aveInt(4, 2, 4, 6, 8) << endl;
    cout << aveInt(4, 5, 9, 6, 0) << endl;
}

void add_responseTestPrint(char * m_write_buf, const char *format, ...)
{
    va_list arg_list; 
    va_start(arg_list, format); 
    vsnprintf(m_write_buf, WRITE_BUFFER_SIZE, format, arg_list);
    va_end(arg_list);
    
}
#define filename ./writev.txt

void writevTest() {
    struct iovec iov[4];
    char *p1 = (char *)"i";
    char *p2 = (char *)" am";
    char *p3 = (char *)" happy.\n";
    int a = 5;
    char  *p4 = (char *)"nihao";
    iov[0].iov_base = p1;
    iov[0].iov_len = strlen(p1);

    iov[1].iov_base = p2;
    iov[1].iov_len = strlen(p2);

    iov[2].iov_base = p3;
    iov[2].iov_len = strlen(p3);

    iov[3].iov_base = p4;
    iov[3].iov_len = strlen(p4);
    ssize_t ret = writev(STDOUT_FILENO,iov, 3); //i am happy.
    printf("%ld bytes\n", ret);
}
int main() {
    writevTest();
    /*
    add
    char m_write_buf[WRITE_BUFFER_SIZE];
    add_responseTestPrint(m_write_buf, "my name is %s,my age is %d, I use %.2f money.\n","bob",18, 52.0);
    printf("%s", m_write_buf);
    //vatest();
    
    // char*是变量，值可以改变， char[]是常量，值不能改变。 
    // const char * a=”string1” 
    // char b[]=”string2”; 例如对于这两个类型，a是const char, b是char const指针常量值可以变指向不变
    char m_real_file[READ_BUFFER_SIZE];
    const char* doc_root = "/home/qgy/github/ini_tinywebserver/root";
    int len = strlen(doc_root);
    const char * m_url = doc_root; // 常量指针，值不变指向可变；
    const char *p = strrchr(m_url, '/'); 
    strcpy(m_real_file, doc_root);
    cout << p << endl;
    cout << doc_root << endl;
    cout << m_real_file << endl;
    char *m_url_real = (char *)malloc(sizeof(char) * 200);
    strcpy(m_url_real, "/register.html");
    // strcpy(m_real_file, m_url_real);// register.html
    // strncpy(m_real_file, m_url_real, strlen(m_url_real)); //  register.html
    strncpy(m_real_file + strlen(m_real_file), m_url_real, strlen(m_url_real)); // /home/qgy/github/ini_tinywebserver/root/register.html
    cout << m_real_file << endl;
    
    // strpbrk(字符串，子串)返回的是子串里某个第一个在左边出现的字符开始的字符串
    char * text = (char*)"ddddacbrcdefgMsabrfadgdfsa";
    char * m_url;
    m_url = strpbrk(text, "g"); // 比如这里，返回的是d开头的整个text，因为d先在text里出现
    cout << "text = " << text << endl;
    cout << "m_url = " << m_url << endl;
    int x = strspn(text, "b");
    cout << "x = " << x << endl;
    m_url = strchr(m_url, 'M');
    cout << "m_url = " << m_url << endl;
    m_url = strpbrk(m_url, "M");
    cout << "m_url = " << m_url << endl;
    m_url = strchr(m_url, 'M');
    cout << "m_url = " << m_url << endl;
    cout << "keep-alive的长度  " << strlen("keep-alive") << endl; 
    cout << "close的长度  " << strlen("close") << endl;
    cout << "Connection:的长度  " << strlen("Connection:") << endl;
    text = (char *)"Content-length:bcxnvQ8";
    if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        cout << "text = " << text << endl;
        x = strspn(text, "zxcvbnm"); 
        text += x;
        cout << "x = " << x << endl;
        cout << "text = " << text << endl;
        
    }

    
    int arr[4] = {8, 2, 3, 4};
    cout << *arr << endl;
    int * p = arr;
    *p++ = 7;  // 先把*p这个位置的数字变成7，然后p++
    cout << *p << endl;
    cout << *arr << endl;
    // cout << *m_url << endl << "======" << endl;

    *m_url++='\0';

    
    cout << "请输入字符串：" << '\r';
    string str;
    cin >> str;
    cout << '\r';
    //cout << endl;
    cout << str << endl;
    cout << char('\0') << endl;
    
    char m_read_buf[READ_BUFFER_SIZE] = "abcdefg,highjthidfadsfarerf";
    int m_start_line = 5;
    char * buff = m_read_buf + m_start_line;
    cout << buff << endl;
    */
    return 0;
}