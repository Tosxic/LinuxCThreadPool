#ifndef _THREADPOOL_H
#define _THREADPOOL_H

typedef struct ThreadPool ThreadPool;

// 创建线程池并初始化
ThreadPool* ThreadPoolCreate(int min, int max, int queueSize);

// 销毁线程池
int ThreadPoolDestroy(ThreadPool* pool);

// 添加任务到线程池
void ThreadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

// 工作线程任务函数
void* Worker(void* arg);

// 管理者线程任务函数
void* Manager(void* arg);

// 线程退出
void ThreadExit(ThreadPool* pool);

#endif