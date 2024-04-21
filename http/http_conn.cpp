#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>
#include <iostream>
using namespace std;

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users; // users也就是登陆用户是通过：密码--登陆名

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    // C++的mysql库。连接数据库之前，先创建MYSQL变量，它在很多库函数中会用到，包含一些连接信息等数据。
    // MYSQL *mysql_init(MYSQL *mysql); 下面这句其实按照这个来的
    MYSQL *mysql = NULL; 
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    // mysql_query()：查询成功返回0 不成功返回非零值
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集。此时已经执行完mysql_query了。
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞。非阻塞IO模型，轮询是否读写完。
int setnonblocking(int fd)
{
    // fcntl系统调用可以用来对已打开的文件描述符进行各种控制操作以改变已打开文件的的各种属性
    int old_option = fcntl(fd, F_GETFL); // 对fd获取文件状态标志
    int new_option = old_option | O_NONBLOCK; // 
    fcntl(fd, F_SETFL, new_option); // 设置文件状态标志
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) // ev是现在已有的事件
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;// doc_root=/home/clab/Downloads/tinytime/root
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_content_type = "application/x-www-form-urlencoded";
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
//m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节--》也可以理解成是读下一个字符，因为sizeof char = 1字节
//m_checked_idx指向从状态机当前正在分析的字节
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // 当前将要分析的字节：可以说是一个字节一个字节地分析
        temp = m_read_buf[m_checked_idx]; 
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)  //下一个字符达到了buffer结尾，则接收不完整，需要继续接收
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n') //下一个字符是\n，将\r\n改为\0\0
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD; //如果都不符合，则返回语法错误
        }
        //如果当前字符是\n，也有可能读取到完整行
        //一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if (temp == '\n')
        {
            //前一个字符是\r，则接收完整
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') 
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN; //并没有找到\r\n，需要继续接收
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据：高电平触发 读缓冲不空写缓冲不满
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    //ET读数据
    else
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) // -1表示错误，获取错误码errno，一般为EAGAIN
            {
                // EAGAIN常用于非阻塞操作中：如果你连续做read操作而没有数据可读，
                // 此时程序不会阻塞起来等待数据准备就绪返回，read函数会返回一个错误EAGAIN，
                // 提示你的应用程序现在没有数据可读请稍后再试。
                // 在某些套接字的函数操作不能立即完成时，会出现错误码EWOULDBLOCK和EAGAIN 
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0) //0表示另一端关闭了套接字
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 在HTTP报文中，请求行用来说明请求类型,要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔。
    
    //strpbrk会找到text里第一个空格和Tab，返回包括空格和tab的字符串，没找到返回NULL。
    // 返回的是后面的字符串
    // text = "POST \t/8 \tHTTP/1.1\r\n";
    m_url = strpbrk(text, " \t"); // murl=  /8  HTTP/1.1
    if (!m_url)
    {
        return BAD_REQUEST;
    }
   
    *m_url++ = '\0'; //将该位置改为\0，用于将前面数据取出，*m_url='\0', m_url++; 
    // murl: " \t/8 \tHTTP/1.1\r\n" ——> "\0\t/8 \tHTTP/1.1"——>"\t/8 \tHTTP/1.1"
    char *method = text;
    // int strcasecmp (const char *s1, const char *s2);
    // 判断字符串是否相等的函数，忽略大小写。返回负数 0 正数
    //取出数据，并通过与GET和POST比较，以确定请求方式
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else if (strcasecmp(method, "PUT") == 0) 
        m_method = PUT;
    else
        return BAD_REQUEST;

    //m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    //将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    m_url += strspn(m_url, " \t"); // strspn()会返回第一个不在m_url里出现的下标。
    // 如果格式正确，这里其实就是m_url的地址。不正确也会推到URL的正确地址。
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0') //判断是空行还是请求头，如果是0代表是空行
    {
        if (m_content_length != 0) //判断是GET还是POST请求，如果是post请求就转到CHECK_STATE_CONTENT状态，
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST; // 返回无请求
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) // Connection:这个字符串的长度是11
    {
        text += 11; // eg. Connection: Keep-Alive。此时就在:后面的空格
        text += strspn(text, " \t"); //跳过空格和\t字符.eg. Connection:     keey-alive。跳过中间
        if (strcasecmp(text, "keep-alive") == 0) // 此时就剩Keep-Alive或Close
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t"); 
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else if (strncasecmp(text, "Content-Type:", 13) == 0)
    {
        text += 13;
        text += strspn(text, " \t");
        m_content_type = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码。
        //PUT请求为用户民 密码 新密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK; // 从process_read()看到刚开始交流，认为读完报文。
    HTTP_CODE ret = NO_REQUEST; // 这时候没有request。
    char *text = 0;
    
    // while(状态机的最右边 || 每次parse_line())
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch (m_check_state) // 主状态机的三种逻辑，
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text); // 解析请求行   
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST) // 在这会解析到GET请求
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            /*接下来是新加的功能multipart/form-data*/
            if (m_method == PUT && strstr(m_content_type, "multipart/form-data"))
            { //strstr：判断后者是不是前者的子串
                LOG_INFO("body :: %s", text); // text先读到的是----分隔符\r\\n
                // parseFormData();
                string body, filename;
                body = text;
			    size_t st = 0;
                size_t ed = 0;
               // st = ed = 0;
                ed = body.find("\r\n"); // ed是第一次出现\r\n的地方也就是第一行后面
                string boundary = body.substr(0, ed);// boundary把除了\r\n的部分都给截下来了。
                cout << "body.substr(0, ed)" << ed << endl;
                // 解析文件信息
                st = body.find("filename=\"", ed) + strlen("filename=\""); // 从ed开始找filename?
                ed = body.find("\"", st);
                filename = body.substr(st, ed - st);
                cout << "filename = body.substr(st, ed - st)" << st << "\t" << ed << endl;
                // 解析文件内容，文件内容以\r\n\r\n开始
                st = body.find("\r\n\r\n", ed) + strlen("\r\n\r\n");
                ed = body.find(boundary, st) - 2; // 文件结尾也有\r\n
                string content = body.substr(st, ed - st);
                
                cout << "string content = body.substr(st, ed - st)" << st << "\t" << ed << endl;
                cout << "body[st]" << body[st] << "\t" << "body[ed]" << body[ed] << endl;
                LOG_INFO("upload file!");
                ofstream ofs;
                //m_string的值为： user=123&passwd=123 cout << "username=" << username << endl;
               
                string userpath = "./allusers/" + username;
                if (access(userpath.c_str(), 0) != 0) {
                    mkdir(userpath.c_str(), 0775);
                }
                string filepath = userpath + "/" + filename;
                ofs.open(filepath, ios::ate);
                ofs << content;
                ofs.close();
            }
            ret = parse_content(text);
            if (ret == GET_REQUEST) // 在这会解析到POST请求或PUT请求。
                return do_request();
            // 解析完消息体即完成报文解析，避免再次进入循环，更新line_status
            line_status = LINE_OPEN;  
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root); // 将初始化的m_real_file赋值为doc_root=/home/clab/Downloads/tinytime/root
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/'); // m_url的格式为：ip:port/xxx，那么这样p就是/xxx==m_url==url
    cout << "m_url=" << m_url << endl;
    //处理cgi。实现登录和注册校验
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3') || *(p + 1) == '4')
    {
        //根据标志判断是登录检测还是注册检测
        //同步线程登录校验
        //CGI多进程登录校验 这三句话等放到登录注册那一节进行理解。
        char flag = m_url[1]; // 有不同的表示

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/"); //  
        strcat(m_url_real, m_url + 2); // /数字后面的内容
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //m_string的值为： user=44&password=4444
        if (*(p + 1) != '4') 
        {
            char name[100], password[100];
            int i;
            for (i = 5; m_string[i] != '&'; ++i) // m_string用来存储请求头的数据
                name[i - 5] = m_string[i];
            name[i - 5] = '\0';
            username = name;

            int j = 0;
            for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
                password[j] = m_string[i];
            password[j] = '\0';

            if (*(p + 1) == '3')
            {
                //如果是注册，先检测数据库中是否有重名的
                //没有重名的，进行增加数据
                char *sql_insert = (char *)malloc(sizeof(char) * 200);
                strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
                strcat(sql_insert, "'");
                strcat(sql_insert, name);
                strcat(sql_insert, "', '");
                strcat(sql_insert, password);
                strcat(sql_insert, "')");

                // 判断map中能否找到重复的用户名
                if (users.find(name) == users.end())
                {
                    m_lock.lock(); //向数据库中插入数据时，需要通过锁来同步数据
                    int res = mysql_query(mysql, sql_insert);
                    users.insert(pair<string, string>(name, password));
                    m_lock.unlock();

                    if (!res) //校验成功，跳转登录页面
                        strcpy(m_url, "/log.html");
                    else //校验失败，跳转注册失败页面
                        strcpy(m_url, "/registerError.html");
                }
                else
                    strcpy(m_url, "/registerError.html");
            }

            //如果是登录，直接判断
            //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
            else if (*(p + 1) == '2')
            {
                if (users.find(name) != users.end() && users[name] == password)
                    strcpy(m_url, "/welcome.html");
                else
                    strcpy(m_url, "/logError.html");
            }
        }
        else { // 4CGI的时候是改密码
            char name[100], password[100], newpassword[100];
                int i;
                for (i = 5; m_string[i] != '&'; ++i) // m_string用来存储请求头的数据
                    name[i - 5] = m_string[i];
                name[i - 5] = '\0';
                username = name;

                int j = 0;
                for (i = i + 10; m_string[i] != '&'; ++i, ++j)
                    password[j] = m_string[i];
                password[j] = '\0';
                int k = 0;
                for (i = i + 13; m_string[i] != '\0'; ++i, ++k)
                    newpassword[k] = m_string[i];
                newpassword[k] = '\0';
                if (users.find(name) != users.end() && users[name] == password)
                { // 登录成功验证，改密码
                    char *sql_update = (char *)malloc(sizeof(char) * 200);
                    strcpy(sql_update, "UPDATE user set passwd = ");
                    strcat(sql_update, "'");
                    strcat(sql_update, newpassword);
                    strcat(sql_update, "'");
                    strcat(sql_update, " where username = ");
                    strcat(sql_update, "'");
                    strcat(sql_update, name);
                    strcat(sql_update, "'");

                    m_lock.lock(); //向数据库中插入数据时，需要通过锁来同步数据
                    int res = mysql_query(mysql, sql_update);
                    users[name] = newpassword;
                    m_lock.unlock();

                    if (!res) //校验成功，跳转登录页面
                        strcpy(m_url, "/log.html");
                    else //校验失败，跳转注册失败页面
                        strcpy(m_url, "/changepwdError.html");
                } 
                else
                    strcpy(m_url, "/changepwdError.html");
        }
    }

    if (*(p + 1) == '0') // 请求资源为/0，跳转register.html注册界面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1') // 请求资源为/1，跳转登录界面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '9') // 请求资源为/9，跳转更改密码界面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/changepwd.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5') 
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
         cout << " m_url_real上" << m_url_real << endl;
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
         cout << " m_url_real下" << m_url_real << endl;

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '8')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/putfile.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    
    //通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    //失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    //判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
/*
bool http_conn::write()
{
    int temp = 0;

    // 若要发送的数据长度为0
    // 表示响应报文为空，一般不会发生这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); // 但是不明白这里为什么注册成读事件。
        init(); // 能置0的成员变量全置0
        return true;
    }

    while (1)
    {
        //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp = writev(m_sockfd, m_iv, m_iv_count); // temp返回已写入的字节数或者-1

        if (temp < 0) // 出错
        {
            if (errno == EAGAIN) //判断缓冲区是否满了
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); //重新注册写事件
                return true;
            }
            unmap(); //如果发送失败，但不是缓冲区问题，取消映射
            return false;
        }

        //正常发送，temp为发送的字节数
        bytes_have_send += temp; // 更新已发送字节
        bytes_to_send -= temp; //偏移文件iovec的指针
        if (bytes_have_send >= m_iv[0].iov_len) //第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        {
            //不再继续发送头部信息
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else //继续发送第一个iovec头部信息的数据
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0) //判断条件，数据已全部发送完
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); 

            if (m_linger) //浏览器的请求为长连接
            {
                init(); //重新初始化HTTP对象
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
*/

bool http_conn::write()
{
    int temp = 0;
    int newadd = 0;
    
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp >= 0) {
            bytes_have_send += temp;
            newadd = bytes_have_send - m_write_idx;
        }
        else {
            if (errno == EAGAIN) {
                if (bytes_have_send >= m_iv[0].iov_len) {
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_address + newadd;
                    m_iv[1].iov_len = bytes_to_send;
               } else {
                    m_iv[0].iov_base = m_write_buf + bytes_have_send;
                    m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
              }
              modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
              return true;
           }
           unmap();
           return false;
       }

       bytes_to_send -= temp;
       if (bytes_to_send <= 0) { 
        unmap();
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        if (m_linger) {
            init();
            return true;
        } else {
            return false;
        }
       }
    }
}

bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE) //如果写入内容超出m_write_buf大小则报错,
        return false;
    // 这里涉及到va_list
    va_list arg_list; //定义可变参数列表
    va_start(arg_list, format); //将变量arg_list初始化为传入参数
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    // |////|////|////|////|////|   |   |   |
    // m_write_buf            m_write_idx=5
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }

    //更新m_write_idx位置
    m_write_idx += len;
    //清空可变参列表
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

// 添加状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
//添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
//添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
//添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
//添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
//添加文本content
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR: //内部错误，500
    {
        add_status_line(500, error_500_title); //状态行
        add_headers(strlen(error_500_form)); // 消息报头
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST: //报文语法有误，404
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST: //资源没有访问权限，403
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST: //文件存在，200
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            //发送的全部数据为响应报文头部信息和文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            //如果请求的资源大小为0，则返回空白html文件
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
void http_conn::process() 
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) //NO_REQUEST，表示请求不完整，需要继续接收请求数据
    {
        // modfd(epollfd, fd, ev, TRIGMode)
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); //注册并监听读事件
        return;
    }
    bool write_ret = process_write(read_ret); //调用process_write完成报文响应
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); //注册并监听写事件
}
