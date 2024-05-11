#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <sys/mman.h> // 用于内存映射的
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h" // 数据库
//#include "../timer/lst_timer.h"
#include "../timer/wheel_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200; // 设置读取文件的名称m_real_file的大小
    static const int READ_BUFFER_SIZE = 2048; // 设置读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 设置写缓冲区大小
    enum METHOD // 请求方法
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE // 主状态机状态
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE // 报文解析结果
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS // 从状态机的状态
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 这里是初始化套接字？
    int getFd() {return m_sockfd;}
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true); // 关闭http连接
    void process();  
    bool read_once(); // 读取浏览器端发来的全部数据
    bool write(); // 响应报文写入函数
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool); // 同步线程初始化数据库读取表
    int timer_flag;
    int improv;


private:
    void init();
    HTTP_CODE process_read(); // 读
    bool process_write(HTTP_CODE ret); // 写
    HTTP_CODE parse_request_line(char *text); // 主状态机解析报文中请求行的数据-->就是报文中的第一行
    HTTP_CODE parse_headers(char *text); // 主状态机解析报文中请求头的数据-->就是请求首部
    HTTP_CODE parse_content(char *text); //主状态机解析报文中的请求内容 -->实体体
    HTTP_CODE do_request(); //生成响应报文
    // 这个get_line()返回的是
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line(); //　从状态机读取一行，分析是请求报文的哪个部分
    void unmap();
    
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd; //
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE]; // 存储读取请求的报文数据 m_read_buf[2048]==》读缓冲区
    long m_read_idx; // m_read_buf中数据的最后一个字节的下一个位置
    long m_checked_idx; // m_read_buf读取的位置
    int m_start_line; // m_read_buf已被解析的字符个数。
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx; // 指示写缓冲区中的长度
    CHECK_STATE m_check_state; // 主状态机的状态
    METHOD m_method; // 请求方法
    char m_real_file[FILENAME_LEN]; // 读取文件名
    char *m_url; // 解析请求报文中的六个变量。
    char *m_version;
    char *m_host;
    long m_content_length;
    const char * m_content_type; // 增加上传文件的内容。
    bool m_linger;
    char *m_file_address; //  读取服务器上的文件地址  
    struct stat m_file_stat; 
    struct iovec m_iv[2]; //io向量机制iovec
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    string username; // 在用的时候
    int bytes_to_send; //剩余发送字节数
    int bytes_have_send; //已发送字节数
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
