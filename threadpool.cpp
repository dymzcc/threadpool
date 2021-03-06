#include "threadpool.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_TIME 30 /*10s检测一次*/
#define MIN_WAIT_TASK_NUM \
  10 /*如果queue_size > MIN_WAIT_TASK_NUM 添加新的线程到线程池*/
#define DEFAULT_THREAD_VARY 10 /*每次创建和销毁线程的个数*/

threadpool::threadpool(int min_thr_num, int max_thr_num, int max_requests) {
    this->min_thr_num = min_thr_num;
    this->max_thr_num = max_thr_num;
    this->max_requests = max_requests;
    this->busy_thr_num = 0;
    this->live_thr_num = min_thr_num;

    if ((min_thr_num <= 0 || max_thr_num <= 0)) {
        throw std::exception();
    }
    m_threads = new pthread_t[min_thr_num];
    if (!m_threads) {
        throw std::exception();
    }

    /* 创建min_thr_num个线程，并将它们都设置为脱离线程*/
    for (int i = 0; i < min_thr_num; i++) {
        if (pthread_create(m_threads + 1, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
//        if (pthread_detach(m_threads[i])) {
//            printf("121");
//            delete[] m_threads;
//            //throw std::exception();
//        }
    }
    if (pthread_create(&manager_tid, NULL, manager, this) != 0) {
        delete &manager_tid;
        throw std::exception();
    }
}

threadpool::~threadpool() {
    shutdown = true;
    delete[] m_threads;
}

int threadpool::threadpool_add(threadpool *pool, void *(*function)(void *arg),
                               void *arg) {
    threadpool_lock.lock();
    /* 为真时，任务队列已满，调用wait阻塞*/
    while ((task_list.size() == max_requests) && !shutdown) {
        list_not_full.wait(threadpool_lock.get());
    }
    if (shutdown) {
        threadpool_lock.unlock();
    }

    task new_task;
    new_task.arg = arg;
    new_task.function = function;
    task_list.push_back(new_task);

    list_not_empty.signal();
    threadpool_lock.unlock();
}

void *threadpool::worker(void *arg) {
    threadpool *pool = (threadpool *)arg;
    task new_tast;

    while (true) {
        /* 刚创建出线程，等待任务队列里有任务，否则阻塞等待任务队列里有任务后再唤醒接收任务*/
        pool->threadpool_lock.lock();
        /*queue_size == 0 说明没有任务，调 wait 阻塞在条件变量上,
         * 若有任务，跳过该while*/
        while ((pool->task_list.size() == 0) && !pool->shutdown) {
            pool->list_not_empty.wait(pool->threadpool_lock.get());

            if (pool->wait_exit_thr_num > 0) {
                pool->wait_exit_thr_num--;
                if (pool->live_thr_num > pool->min_thr_num) {
                    pool->live_thr_num--;
                    pthread_exit(NULL);
                    pool->threadpool_lock.unlock();
                }
            }
        }
        if (pool->shutdown) {
            pthread_exit(NULL);
            pool->threadpool_lock.unlock();
        }
        /* 从任务队列中获取一个任务*/
        new_tast = pool->task_list.front();
        pool->task_list.pop_front();
        pool->threadpool_lock.unlock();

        pool->thread_counter.lock();
        pool->busy_thr_num++;
        pool->thread_counter.unlock();

        (*(new_tast.function))(new_tast.arg);

        pool->thread_counter.lock();
        pool->busy_thr_num--;
        pool->thread_counter.unlock();
    }
    pthread_exit(NULL);
}

void *threadpool::manager(void *arg) {
    threadpool *pool = (threadpool *)arg;
    while (!pool->shutdown) {
        sleep(DEFAULT_TIME);

        pool->threadpool_lock.lock();
        int task_num = pool->task_list.size();
        int live_thr_num = pool->live_thr_num;
        pool->threadpool_lock.unlock();

        pool->thread_counter.lock();
        int busy_thr_num = pool->busy_thr_num;
        pool->thread_counter.unlock();

        /* 创建新线程 算法： 任务数大于最小线程池个数,
         * 且存活的线程数少于最大线程个数时 如：30>=10 && 40<100*/
        if (task_num >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num) {
            pool->threadpool_lock.lock();
            int add = 0;

            /*一次增加 DEFAULT_THREAD 个线程*/
            for (int i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_VARY &&
                            pool->live_thr_num < pool->max_thr_num;
                 i++) {
                if (pool->m_threads[i] == 0 || !pool->is_thread_alive(pool->m_threads[i])) {
                    pthread_create(&(pool->m_threads[i]), NULL, worker, (void *)pool);
                    add++;
                    pool->live_thr_num++;
                }
            }

            pool->threadpool_lock.unlock();
        }

        /* 销毁多余的空闲线程 算法：忙线程X2 小于 存活的线程数 且 存活的线程数 大于
         * 最小线程数时*/
        if ((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num) {
            /* 一次销毁DEFAULT_THREAD个线程, 随机10个即可 */
            pool->threadpool_lock.lock();
            pool->wait_exit_thr_num =
                    DEFAULT_THREAD_VARY; /* 要销毁的线程数 设置为10 */
            pool->threadpool_lock.unlock();

            for (int i = 0; i < DEFAULT_THREAD_VARY; i++) {
                pool->list_not_empty.signal();
            }
        }
    }
    return NULL;
}

int threadpool::threadpool_all_threadnum(threadpool *pool) {
    int all_threadnum = -1;
    threadpool_lock.lock();
    all_threadnum = pool->live_thr_num;
    threadpool_lock.unlock();
    return all_threadnum;
}

int threadpool::threadpool_busy_threadnum(threadpool *pool) {
    int busy_threadnum = -1;
    thread_counter.lock();
    busy_threadnum = pool->busy_thr_num;
    thread_counter.unlock();
    return busy_threadnum;
}

int threadpool::is_thread_alive(pthread_t tid) {
    int kill_rc = pthread_kill(tid, 0);  //发0号信号，测试线程是否存活
    if (kill_rc == ESRCH) {
        return false;
    }

    return true;
}

/* 测试，模拟线程池中线程处理业务*/
void *process(void *arg) {
    printf("thread 0x%x working on task %d\n ", pthread_self(),
           *(int *)arg);
    sleep(5);
    printf("task %d is end\n", *(int *)arg);

    return NULL;
}

int main(void){
    threadpool *pool = new threadpool(3, 100, 100);

    int num[20], i;
    for (i = 0; i < 100; i++) {
        num[i]=i;
        printf("add task %d\n",i);
        pool->threadpool_add(pool, process, (void*)&num[i]);     /* 向线程池中添加任务 */
    }
    sleep(10);                                          /* 等子线程完成任务 */

    return 0;
}