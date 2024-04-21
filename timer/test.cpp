#include <time.h>
#include <iostream>
using namespace std;
//获取时间，单位是s
 time_t current_time() // 省了static
{
    time_t t;
    struct timespec ti;
    clock_gettime(CLOCK_MONOTONIC, &ti);
    t = (time_t)ti.tv_sec;
    return t;
}
int main() {
    // time_t是定义在time.h中的一个类型，表示一个日历时间，
    // 也就是从1970年1月1日0时0分0秒到此时的秒数
    // time_t cur = time(NULL);
    // cout << cur / 3600 / 24 / 365 << endl; // 53年过去了
    // int count = 3;
    // while (count) {
    //     if (count == 8)
    //         break;
    //     count++;
    //     cout << "count = " << count << endl;
    // }
    // time_t cur = time(NULL);
    // cout << cur << endl;
    time_t t;
    struct timespec ti;
    clock_gettime(CLOCK_MONOTONIC, &ti);
    t = ti.tv_sec;
    cout << t << endl;
    // struct timespec *tp
    /*
    struct timespec {
    time_t tv_sec; 
    long tv_nsec; 
    }
    */
   
}