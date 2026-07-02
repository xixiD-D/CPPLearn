//
// Created by zexi zhong on 2026/7/2.
//
// step2.cpp
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

// ==================== 任务抽象层 ====================
class Task {
public:
    virtual ~Task() {}
    virtual void execute() = 0; // 纯虚函数，子类必须实现
};

class PrintTask : public Task {
    int m_id;
public:
    explicit PrintTask(int id) : m_id(id) {}
    void execute() {
        printf("  [Task %d] executed by thread %lu\n", m_id, (unsigned long)pthread_self());
        usleep(500000); // 模拟 500ms 耗时操作
    }
};

// ==================== 线程参数 ====================
struct WorkerArg {
    int id;
    Task* task; // 直接绑定一个任务给线程
};

void* worker_entry(void* arg) {
    WorkerArg* wa = (WorkerArg*)arg;
    printf("[Worker %d] ready\n", wa->id);
    if (wa->task) {
        wa->task->execute();
        delete wa->task; // 执行完释放内存
    }
    return NULL;
}

int main() {
    const int NUM = 2;
    pthread_t threads[NUM];
    WorkerArg args[NUM];

    // 主线程充当生产者，直接给每个线程分配一个任务
    for (int i = 0; i < NUM; ++i) {
        args[i].id = i;
        args[i].task = new PrintTask(i); // 堆上分配任务
        pthread_create(&threads[i], NULL, worker_entry, &args[i]);
    }

    for (int i = 0; i < NUM; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("[Main] All tasks done\n");
    return 0;
}