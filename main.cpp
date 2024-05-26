#include "config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "17441027Da";
    string databasename = "tinydb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化
    std::cout<< "主函数, init前"<<std::endl;
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    
    
    server.log_write(); // 日志
    server.sql_pool(); // 数据库：获得单例，初始化，然后获得数据表
    server.thread_pool();  //线程池：就构造就开始运行了
    server.trig_mode(); // 触发模式
    server.eventListen(); // 监听
    server.eventLoop(); // 运行
    return 0;
}
