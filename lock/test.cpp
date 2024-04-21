#include "locker.h"
#include <iostream>
using namespace std;
int main() {
    sem x(4);
    // for (int i = 1; i < 8; i++) {
    //     x.wait(); //
    //     cout << i << endl; 
    //     if (i == 4)
    //         x.post();
    // } 

    pthread_mutex_t m_mutex;
    //cout << m_mutex << endl;
    locker lock = locker();
    int i = 1;
    if (i)
        lock.lock();
    i++;
    cout << i << endl;
    lock.unlock();
    
}