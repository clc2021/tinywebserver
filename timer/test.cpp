#include <time.h>
#include <iostream>
using namespace std;
//��ȡʱ�䣬��λ��s
 time_t current_time() // ʡ��static
{
    time_t t;
    struct timespec ti;
    clock_gettime(CLOCK_MONOTONIC, &ti);
    t = (time_t)ti.tv_sec;
    return t;
}
int main() {
    // time_t�Ƕ�����time.h�е�һ�����ͣ���ʾһ������ʱ�䣬
    // Ҳ���Ǵ�1970��1��1��0ʱ0��0�뵽��ʱ������
    // time_t cur = time(NULL);
    // cout << cur / 3600 / 24 / 365 << endl; // 53���ȥ��
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