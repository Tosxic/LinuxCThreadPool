#include <stdio.h>
#include <pthread.h>
#include <malloc.h>
#include "threadpool.h"

#define NUMBER 2

// ����ṹ��
typedef struct Task {
	void (*function)(void* arg);
	void* arg;
}Task;

// �̳߳ؽṹ��
struct ThreadPool {
	// �������
	Task* taskQ;
	int queueCapacity;  // ����
	int queueSize;  // ��ǰ�������
	int queueFront;  // ��ͷ
	int queueRear;  // ��β

	pthread_t managerID;  // �������߳�ID
	pthread_t* threadIDs;  // �����߳�ID
	int minNum;  // ��С�߳�����
	int maxNum;  // ����߳�����
	int busyNum;  // æ���߳�����
	int liveNum;  // �����߳�����
	int exitNum;  // Ҫ���ٵ��̸߳���
	pthread_mutex_t mutexPool;  // �̳߳���
	pthread_mutex_t mutexBusy;  // busy������
	pthread_cond_t notFull;  // ��������Ƿ���
	pthread_cond_t notEmpty;  // ��������Ƿ��

	int shutdown;  // �߳��Ƿ����٣�����Ϊ1������Ϊ0
};

// �����̳߳ز���ʼ��
ThreadPool* ThreadPoolCreate(int minNum, int maxNum, int queueCapacity) {
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do {
		if (pool == NULL) {
			printf("Error: malloc ThreadPool failed!\n");
			break;
		}

		// ��ʼ���߳���Ϣ
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

		// ��ʼ���������
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueCapacity);
		if (pool->taskQ == NULL) {
			printf("Error: malloc taskQ failed!\n");
			break;
		}
		pool->queueCapacity = queueCapacity;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		// �����߳�
		int managerStatus = pthread_create(&pool->managerID, NULL, Manager, pool);
		for (int i = 0; i < minNum; i++) {
			int singleWorkStatus = pthread_create(&pool->threadIDs[i], NULL, Worker, pool);
		}

		return pool;
	} while (0);

	// �����ڴ�ʧ��ʱ�������ѷ����ڴ�
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
	// �ر��̳߳�
	pthread_mutex_lock(&pool->mutexPool);
	pool->shutdown = 1;
	pthread_mutex_unlock(&pool->mutexPool);
	// ���չ������߳�
	pthread_join(pool->managerID, NULL);
	// �����������������߳�
	for (int i = 0; i < pool->liveNum; i++) {
		pthread_cond_signal(&pool->notEmpty);
		
	}
	for (int i = 0; i < pool->maxNum; i++) {
		if (pool->threadIDs[i]) {
			pthread_join(pool->threadIDs[i], NULL);
		}
	}
	// �ͷ��ڴ�
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
	// �������������������������߳�
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
		pthread_mutex_lock(&pool->mutexPool);  // ��ȡ��Ϣ����
		// �������Ϊ�գ����̳߳�δ����
		while (pool->queueSize == 0 && !pool->shutdown) {
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);  // ���������߳�
			if (pool->exitNum > 0) {
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					ThreadExit(pool);
				}
			}
		}
		// �̳߳�����
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			ThreadExit(pool);
		}
		// ȡ����
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		// ����
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);
		// ִ������
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
		// ÿ3s���һ��
		sleep(3);
		// ȡ���̳߳�������������͵�ǰ�̵߳�����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);
		// ȡ��æ�߳�����
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);
		// ����߳�
		// �������>�����߳����� && ����߳���<����߳���
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
		// �����߳�
		// æ�߳�*2 < ����߳��� && ����߳��� < ��С�߳���
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			// �������߳���ɱ
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