//
// Created by zexi zhong on 2026/7/2.
//

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <pthread.h>

/**
 * 任务抽象基类
 * 使用者需继承此类，实现 execute 方法
 */
class Task {
public:
    virtual ~Task() = default;

    virtual void execute() = 0;
};

/**
 * 线程池类
 */
class ThreadPool {
public:
    // explicit 防止隐式类型转换
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    // 初始化
    bool initialize();

    // 优雅关闭，等待所有线程结束，清理资源
    void shutdown();

    // 提交任务（线程池获得任务所有权，执行后 delete）
    void addTask(Task* task);

    bool isRunning() const;

private:
    // 禁止拷贝赋值
    ThreadPool(const ThreadPool&);
    ThreadPool& operator=(const ThreadPool&);

    // 工作线程入口（必须是 static，因为 pthread 要求 C 风格函数）
    static void* workEntry(void* arg);

    // 工作线程主循环
    void workerLoop();

    size_t m_numThreads;
    std::vector<pthread_t> m_threads; // 线程句柄数组
    std::queue<Task*> m_taskQueue; // 任务队列

    mutable pthread_mutex_t m_mutex{}; // mutable 允许 const 方法加锁
    pthread_cond_t m_cond{};

    bool m_initialized; // 是否初始化成功
    bool m_running; // 是否运行中
    bool m_stop; // 停止标志
};

#endif //THREAD_POOL_H
