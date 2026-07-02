//
// Created by zexi zhong on 2026/7/2.
//
// step3.cpp
#include <queue>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

class Task {
public:
    virtual ~Task() {}
    virtual void execute() = 0;
};

class PrintTask : public Task {
    int m_id;
public:
    explicit PrintTask(int id) : m_id(id) {}
    void execute() {
        printf("  [Task %d] by thread %lu\n", m_id, (unsigned long)pthread_self());
        usleep(300000);
    }
};

// ==================== 全局共享资源（仅为教学演示，后续会封装到类中） ====================
std::queue<Task*> g_task_queue;           // 任务队列
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER; // 静态初始化互斥锁
bool g_running = true;                    // 运行标志

void* worker_entry(void* arg) {
    int id = *(int*)arg;
    while (g_running) {
        Task* task = NULL;

        // ========== 临界区开始 ==========
        pthread_mutex_lock(&g_mutex);
        if (!g_task_queue.empty()) {
            task = g_task_queue.front();
            g_task_queue.pop();
        }
        pthread_mutex_unlock(&g_mutex);
        // ========== 临界区结束 ==========

        if (task) {
            task->execute();
            delete task;
        } else {
            // 队列空，忙等待（CPU 空转！这是反模式，下一步解决）
            usleep(100000);
        }
    }
    printf("[Worker %d] exiting\n", id);
    return NULL;
}

int main() {
    const int NUM = 3;
    pthread_t threads[NUM];
    int ids[NUM];

    for (int i = 0; i < NUM; ++i) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, worker_entry, &ids[i]);
    }

    // 主线程充当生产者
    printf("[Main] Submitting 10 tasks...\n");
    for (int i = 0; i < 10; ++i) {
        pthread_mutex_lock(&g_mutex);
        g_task_queue.push(new PrintTask(i));
        pthread_mutex_unlock(&g_mutex);
        usleep(50000); // 模拟任务到达间隔
    }

    sleep(2); // 等待任务执行完

    g_running = false;
    for (int i = 0; i < NUM; ++i) {
        pthread_join(threads[i], NULL);
    }

    // 清理残留任务
    pthread_mutex_lock(&g_mutex);
    while (!g_task_queue.empty()) {
        Task* t = g_task_queue.front(); g_task_queue.pop();
        delete t;
    }
    pthread_mutex_unlock(&g_mutex);

    pthread_mutex_destroy(&g_mutex);
    printf("[Main] Done\n");
    return 0;
}