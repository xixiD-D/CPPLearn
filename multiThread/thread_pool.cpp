//
// Created by zexi zhong on 2026/7/2.
//

#include "thread_pool.h"
#include <cstdio>
#include <cstdlib>

ThreadPool::ThreadPool(size_t numThreads)
    : m_numThreads(numThreads), m_initialized(false),
      m_running(false), m_stop(false) {
}

ThreadPool::~ThreadPool() {
    if (m_initialized) {
        shutdown();
    }
}

bool ThreadPool::initialize() {
    if (m_numThreads <= 0) {
        fprintf(stderr, "Error: thread count cannot be 0\n");
        return false;
    }
    // 初始化互斥锁
    if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
        perror("pthread_mutex_init");
        return false;
    }

    // 初始化条件变量
    if (pthread_cond_init(&m_cond, nullptr) != 0) {
        perror("pthread_cond_init");
        pthread_mutex_destroy(&m_mutex);
        return false;
    }


    m_threads.resize(m_numThreads);

    // 创建工作线程
    for (size_t i = 0; i < m_numThreads; i++) {
        if (pthread_create(&m_threads[i], nullptr, workEntry, this) != 0) {
            perror("pthread_create");
            m_stop = true;
            pthread_cond_broadcast(&m_cond);
            for (size_t j = 0; j < i; j++) {
                pthread_join(m_threads[j], nullptr);
            }
            pthread_mutex_destroy(&m_mutex);
            pthread_cond_destroy(&m_cond);
            return false;
        }
    }

    m_initialized = true;
    m_running = true;
    return true;
}

void ThreadPool::shutdown() {
    if (!m_initialized) {
        return;
    }
    // 第 1 步，设置停止标志，唤醒所有线程
    pthread_mutex_lock(&m_mutex);
    m_stop = true;
    pthread_cond_broadcast(&m_cond);
    pthread_mutex_unlock(&m_mutex);

    // 第 2 步： 等待所有线程结束（join）
    for (const unsigned long m_thread : m_threads) {
        pthread_join(m_thread, nullptr);
    }

    // 第3 步：清理未执行的任务（防止内存泄漏）
    pthread_mutex_lock(&m_mutex);

    while (!m_taskQueue.empty()) {
        Task* task = m_taskQueue.front();
        m_taskQueue.pop();
        delete task;
    }
    pthread_mutex_unlock(&m_mutex);

    // 第 4 步：销毁同步原语
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);

    m_initialized = false;
    m_running = false;
}

void ThreadPool::addTask(Task* task) {
    if (task == nullptr || !m_running) {
        return;
    }
    pthread_mutex_lock(&m_mutex);
    m_taskQueue.push(task);
    pthread_cond_signal(&m_cond); // 通知下一个线程来取任务
    pthread_mutex_unlock(&m_mutex);
}

bool ThreadPool::isRunning() const {
    return m_running;
}


void *ThreadPool::workEntry(void *arg) {
    auto* pool = static_cast<ThreadPool*>(arg);
    pool->workerLoop();
    return nullptr;
}

void ThreadPool::workerLoop() {
    while (true) {
        Task* task = nullptr;
        pthread_mutex_lock(&m_mutex);
        while (m_taskQueue.empty() && !m_stop) {
            pthread_cond_wait(&m_cond, &m_mutex);
        }
        // 停止且队列为空，推出线程
        if (m_stop && m_taskQueue.empty()) {
            pthread_mutex_unlock(&m_mutex);
            break;
        }

        // 取出任务
        task = m_taskQueue.front();
        m_taskQueue.pop();

        pthread_mutex_unlock(&m_mutex);

        // 在锁外执行任务，避免长时间持有锁
        if (task != nullptr) {
            task->execute();
            delete task;
        }
    }
}
