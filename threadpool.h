#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <cstdio>
#include <exception>
#include <list>
#include "locker/locker.h"

typedef struct task{
    void *(*function)(void *);          /* 函数指针，回调函数 */
    void *arg;                          /* 上面函数的参数 */
} threadpool_task_k;                    /* 各子线程任务结构体 */

class threadpool {
public:
    threadpool(int min_thr_num, int max_thr_num, int max_requests);
    ~threadpool();
    int threadpool_add(threadpool *pool, void *(*function)(void *arg), void *arg);
    int threadpool_all_threadnum(threadpool *pool);
    int threadpool_busy_threadnum(threadpool *pool);
    int is_thread_alive(pthread_t tid);


private:
    static void *worker(void *arg);      //工作线程
    static void *manager(void *arg);     //管理者线程

private:
    int min_thr_num;       /* 线程池最小线程数 */
    int max_thr_num;       /* 线程池最大线程数 */
    int live_thr_num;      /* 当前存活线程个数 */
    int busy_thr_num;      /* 忙状态线程个数 */
    int wait_exit_thr_num; /* 要销毁的线程个数 */
    int max_requests;      /* 请求队列中允许的最大请求数*/

    pthread_t *m_threads;  /* 描述线程池的数组，其大小为m_thread_number*/
    pthread_t manager_tid; /* 管理者线程id*/
    std::list<task> task_list;

    locker threadpool_lock;           /* 用于锁住线程池 */
    locker thread_counter; /* 记录busy线程个数的锁 */
    cond list_not_full;    /* 任务队列满时，添加任务的线程阻塞，等待此条件变量 */
    cond list_not_empty;   /* 任务队列不为空时，通知等待任务的线程*/

    int shutdown; /* 标志位，线程池使用状态，true或false */

};





#endif
