#include <stdio.h>
#include <pthread.h>
#include <math.h>
#include "threadpool.h"

void taskFunc(void* arg) {
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d, result = %.2lf\n", pthread_self(), num, sqrt(num));
    sleep(1);
}

int main()
{
    ThreadPool* pool = ThreadPoolCreate(3, 10, 100);
    for (int i = 0; i < 100; i++) {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        ThreadPoolAdd(pool, taskFunc, num);
    }
    sleep(10);
    for (int i = 0; i < 5; i++) {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        ThreadPoolAdd(pool, taskFunc, num);
    }
    for (int i = 0; i < 100; i++) {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        ThreadPoolAdd(pool, taskFunc, num);
    }
    sleep(5);
    ThreadPoolDestroy(pool);
    return 0;
}