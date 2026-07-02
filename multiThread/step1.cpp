// step1.cpp
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

// 工作线程入口函数
// pthread 要求入口必须是 void* func(void*) 的 C 风格函数
void *worker_entry(void *arg) {
    int worker_id = *(int *) arg;
    printf("[Worker %d] started, pthread_id=%lu\n", worker_id, (unsigned long) pthread_self());
    sleep(1); // 模拟工作
    printf("[Worker %d] finished\n", worker_id);
    return NULL;
}

int main() {
    const int NUM_THREADS = 3;
    pthread_t threads[NUM_THREADS]; // 线程句柄数组
    int ids[NUM_THREADS];

    printf("[Main] Creating %d threads...\n", NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        ids[i] = i;
        // 参数1: 句柄出参
        // 参数2: 线程属性（NULL=默认）
        // 参数3: 入口函数
        // 参数4: 传给入口函数的参数
        pthread_create(&threads[i], NULL, worker_entry, &ids[i]);
    }

    printf("[Main] All threads created, waiting...\n");
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL); // 阻塞等待线程结束
    }

    printf("[Main] All threads joined, exit\n");
    return 0;
}
