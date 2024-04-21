#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;
/*值得注意的是，销毁连接池没有直接被外部调用，而是通过RAII机制来完成自动释放；
使用信号量实现多线程争夺连接的同步机制，这里将信号量初始化为数据库的连接总数。*/
connection_pool::connection_pool()
{
	m_CurConn = 0; // 当前可用连接数 ==》这两个都是零是因为池子里现在一个连接都没有。
	m_FreeConn = 0; // 当前可用连接数
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化：这里开始造出整个池子。最主要的就是for(从0到maxconn的循环构造)
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con); // MYSQL类型变量为con，利用mysql_init()语句初始化这个变量。这里用的地址&

		if (con == NULL)
		{
			LOG_ERROR("");
			exit(1);
		}
		// mysql_real_connect(变量地址，连接的主机名，用户，密码，数据库，端口，NULL，0)
		// ("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 更新连接池和空闲连接数量
		connList.push_back(con);
		++m_FreeConn;
	}

	reserve = sem(m_FreeConn); // reserve是信号量类型，sem(信号量的初始值)，线程共享，
	//这里用可用连接池数量初始化信号量。

	m_MaxConn = m_FreeConn;
}

/*
当线程数量大于数据库连接数量时，使用信号量进行同步，
每次取出连接，信号量原子减1，
释放连接原子加1，
若连接池内没有连接了，则阻塞等待。
另外，由于多线程操作连接池，会造成竞争，这里使用互斥锁完成同步，
具体的同步机制均使用lock.h中封装好的类。
*/

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;
	//取出连接，信号量原子减1，为0则等待
	reserve.wait(); // 从连接池中取出一个时，先要看看连接池里有没有。相当于 m_FreeConn--
	
	lock.lock(); // 上锁

	con = connList.front();
	connList.pop_front(); // 还记得连接池是什么吗？List<MYSQL *> connList;

	--m_FreeConn;
	++m_CurConn;

	lock.unlock(); //下锁
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();
    //释放连接原子加1
	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		//通过迭代器遍历，关闭数据库连接
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

//////////////////////////////////////////////////////////////////////

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection(); //也就是说GetConnection返回Mysql*类型
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII); 
}