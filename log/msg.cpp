#include <pthread.h>

struct msg {
    struct msg * next;
};

msg * workq; // 缓冲队列
pthread_cond_t qready = PTHREAD_COND_INITIALIZER; // 到达某个数值时唤醒等待它的线程
pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER; // 互斥锁是为了独占式访问

void producer() { // 生产者
    msg * mp;
    while (true) {
        pthread_mutex_lock(&qlock);
        while (workq == NULL)
            pthread_cond_wait(&qready, &qlock);
        mp = workq; // 头删
        workq = mp->next;
        pthread_mutex_unlock(&qlock);
        /*开始处理消息....*/
    }
}

void consumer(msg * mp) { // 消费者
    pthread_mutex_lock(&qlock);
    mp->next = workq;
    workq = mp; // 头插
    pthread_mutex_unlock(&qlock);
    /* 此时另外一个线程在signal之前，执行了producer，刚好把mp元素拿走*/
    pthread_cond_signal(&qready);
    /*此时执行signal, 在pthread_cond_wait等待的线程被唤醒，
     * 但是mp元素已经被另外一个线程拿走，所以，workq还是NULL ,因此需要继续等待*/
}