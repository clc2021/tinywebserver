#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_
// 这个sql_connection_pool类中定义的类为：connection_pool
#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取空闲连接数
	void DestroyPool();					 //销毁所有连接

	// 这里创造的是一个整的数据库连接池。
	static connection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数 maxconn = curconn + freeconn
	locker lock; // lock是一个互斥锁
	
	//对connList的操作，始终要在lock()和unlock()内，因为是多线程访问的资源。！！！！！
	list<MYSQL *> connList; //连接池：是一个MYSQL类型组成的链表类型，也就是connList

	sem reserve; // reserve是一个信号量

public:
	string m_url;			 //主机地址
	string m_Port;		 //数据库端口号
	string m_User;		 //登陆数据库用户名
	string m_PassWord;	 //登陆数据库密码
	string m_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关
};

class connectionRAII{

public:
	// 双指针对MYSQL * con修改
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII; // 一条数据库连接
	connection_pool *poolRAII; // 指向数据库连接池
};

#endif
