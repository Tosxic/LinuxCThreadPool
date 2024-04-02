#ifndef _THREADPOOL_H
#define _THREADPOOL_H

typedef struct ThreadPool ThreadPool;

// �����̳߳ز���ʼ��
ThreadPool* ThreadPoolCreate(int min, int max, int queueSize);

// �����̳߳�
int ThreadPoolDestroy(ThreadPool* pool);

// ��������̳߳�
void ThreadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

// �����߳�������
void* Worker(void* arg);

// �������߳�������
void* Manager(void* arg);

// �߳��˳�
void ThreadExit(ThreadPool* pool);

#endif