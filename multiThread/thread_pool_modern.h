#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <memory>
#include <stdexcept>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    // 禁止拷贝和移动（资源管理类通常不可复制）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // 提交任务到线程池，返回 std::future 用于获取执行结果
    // 使用可变参数模板 + 完美转发，支持任意函数签名
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        // 1. 用 bind 将函数和参数打包成一个可调用对象
        using ReturnType = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 2. 从 packaged_task 获取 future（用于异步获取结果）
        std::future<ReturnType> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            if (m_stop) {
                throw std::runtime_error("Cannot submit task to stopped ThreadPool");
            }
            // 3. 将任务包装为 void() 类型存入队列
            m_tasks.emplace([task]() { (*task)(); });
        }

        // 4. 唤醒一个等待的工作线程
        m_condition.notify_one();
        return result;
    }

    // 等待所有任务完成（不停止线程池，只是阻塞直到队列为空且所有线程空闲）
    void waitForAll();

    // 优雅停止：等待所有任务完成后关闭
    void shutdown();

    // 活跃线程数
    size_t activeCount() const { return m_active.load(); }

    // 总线程数
    size_t threadCount() const { return m_workers.size(); }

private:
    void workerLoop();

    std::vector<std::thread> m_workers;           // 工作线程数组
    std::queue<std::function<void()>> m_tasks;    // 任务队列
    std::mutex m_queueMutex;                       // 保护任务队列
    std::condition_variable m_condition;         // 任务到达通知
    std::condition_variable m_finishedCondition; // 全部完成通知
    std::atomic<bool> m_stop;                     // 停止标志（原子类型）
    std::atomic<size_t> m_active;               // 当前正在执行任务的线程数
};