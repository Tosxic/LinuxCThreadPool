#include <stdio.h>
#include <pthread.h>
#include <malloc.h>
#include "threadpool.h"

#define NUMBER 2

// 任务结构体
typedef struct Task {
	void (*function)(void* arg);
	void* arg;
}Task;

// 线程池结构体
struct ThreadPool {
	// 任务队列
	Task* taskQ;
	int queueCapacity;  // 容量
	int queueSize;  // 当前任务个数
	int queueFront;  // 队头
	int queueRear;  // 队尾

	pthread_t managerID;  // 管理者线程ID
	pthread_t* threadIDs;  // 工作线程ID
	int minNum;  // 最小线程数量
	int maxNum;  // 最大线程数量
	int busyNum;  // 忙的线程数量
	int liveNum;  // 存活的线程数量
	int exitNum;  // 要销毁的线程个数
	pthread_mutex_t mutexPool;  // 线程池锁
	pthread_mutex_t mutexBusy;  // busy变量锁
	pthread_cond_t notFull;  // 任务队列是否满
	pthread_cond_t notEmpty;  // 任务队列是否空

	int shutdown;  // 线程是否销毁，销毁为1，否则为0
};

// 创建线程池并初始化
ThreadPool* ThreadPoolCreate(int minNum, int maxNum, int queueCapacity) {
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do {
		if (pool == NULL) {
			printf("Error: malloc ThreadPool failed!\n");
			break;
		}

		// 初始化线程信息
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * maxNum);
		if (pool->threadIDs == NULL) {
			printf("Error: malloc threadIDs failed!\n");
			break;
		}
		memset(pool->threadIDs, 0, sizeof(pthread_t) * maxNum);
		pool->minNum = minNum;
		pool->maxNum = maxNum;
		pool->busyNum = 0;
		pool->liveNum = minNum;
		pool->exitNum = 0;
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0) {
			printf("Error: mutex or condition init failed!\n");
			break;
		}

		// 初始化任务队列
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueCapacity);
		if (pool->taskQ == NULL) {
			printf("Error: malloc taskQ failed!\n");
			break;
		}
		pool->queueCapacity = queueCapacity;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		// 创建线程
		int managerStatus = pthread_create(&pool->managerID, NULL, Manager, pool);
		for (int i = 0; i < minNum; i++) {
			int singleWorkStatus = pthread_create(&pool->threadIDs[i], NULL, Worker, pool);
		}

		return pool;
	} while (0);

	// 分配内存失败时，回收已分配内存
	if (pool && pool->threadIDs) {
		free(pool->threadIDs);
	}
	if (pool && pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool) {
		free(pool);
		pool = NULL;
	}

	return NULL;
}

int ThreadPoolDestroy(ThreadPool* pool) {
	if (pool == NULL) {
		return -1;
	}
	// 关闭线程池
	pthread_mutex_lock(&pool->mutexPool);
	pool->shutdown = 1;
	pthread_mutex_unlock(&pool->mutexPool);
	// 回收管理者线程
	pthread_join(pool->managerID, NULL);
	// 唤醒阻塞的消费者线程
	for (int i = 0; i < pool->liveNum; i++) {
		pthread_cond_signal(&pool->notEmpty);
		
	}
	for (int i = 0; i < pool->maxNum; i++) {
		if (pool->threadIDs[i]) {
			pthread_join(pool->threadIDs[i], NULL);
		}
	}
	// 释放内存
	if (pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool->threadIDs) {
		free(pool->threadIDs);
	}

	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);

	free(pool);
	pool = NULL;
	return 0;
}

void ThreadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg) {
	pthread_mutex_lock(&pool->mutexPool);
	// 如果任务队列已满，阻塞生产线程
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown) {
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	pthread_cond_signal(&pool->notEmpty);
	pthread_mutex_unlock(&pool->mutexPool);
}

void* Worker(void* arg) {
	ThreadPool* pool = (ThreadPool*)arg;

	while (1) {
		pthread_mutex_lock(&pool->mutexPool);  // 读取信息加锁
		// 如果队列为空，且线程池未销毁
		while (pool->queueSize == 0 && !pool->shutdown) {
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);  // 阻塞工作线程
			if (pool->exitNum > 0) {
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					ThreadExit(pool);
				}
			}
		}
		// 线程池销毁
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			ThreadExit(pool);
		}
		// 取任务
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		// 解锁
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);
		// 执行任务
		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		task.function(task.arg);
		free(task.arg);
		task.arg = NULL;

		printf("thread %ld has finished the work!\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);
	}
	return NULL;
}

void* Manager(void* arg) {
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown) {
		// 每3s检测一次
		sleep(3);
		// 取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);
		// 取出忙线程数量
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);
		// 添加线程
		// 任务个数>存活的线程数量 && 存活线程数<最大线程数
		if (queueSize > liveNum && liveNum < pool->maxNum) {
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER
				&& pool->liveNum < pool->maxNum; i++) {
				if (pool->threadIDs[i] == 0) {
					pthread_create(&pool->threadIDs[i], NULL, Worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}
		// 销毁线程
		// 忙线程*2 < 存活线程数 && 存活线程数 < 最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			// 让阻塞线程自杀
			for (int i = 0; i < NUMBER; ++i) {
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return NULL;
}


void ThreadExit(ThreadPool* pool) {
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; i++) {
		if (pool->threadIDs[i] == tid) {
			pool->threadIDs[i] = 0;
			break;
		}
	}
	printf("thread %ld exited!\n", tid);
	pthread_exit(NULL);
}