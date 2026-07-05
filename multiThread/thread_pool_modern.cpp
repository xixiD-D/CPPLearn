#include "thread_pool_modern.h"

ThreadPool::ThreadPool(size_t numThreads)
    : m_stop(false), m_active(0)
{
    m_workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        // emplace_back 直接在线程数组中构造线程，避免拷贝
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    if (!m_stop.load()) {
        shutdown();
    }
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;

        {
            // unique_lock 比 lock_guard 更灵活：支持 unlock/lock 和 condition_variable
            std::unique_lock<std::mutex> lock(m_queueMutex);

            // wait 的第二个参数是 lambda 谓词：只有返回 true 时才会继续，否则阻塞
            // 这等价于 while (!pred) { wait(); } 的封装
            m_condition.wait(lock, [this] {
                return m_stop.load() || !m_tasks.empty();
            });

            // 被唤醒后判断：如果是停止信号且队列为空，线程退出
            if (m_stop.load() && m_tasks.empty()) {
                return;
            }

            // 取出任务（使用 std::move 避免拷贝）
            task = std::move(m_tasks.front());
            m_tasks.pop();
            m_active.fetch_add(1); // 原子操作：活跃数 +1
        }

        // 在锁外执行任务，最大化并发度
        task();

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_active.fetch_sub(1); // 原子操作：活跃数 -1
            // 如果队列为空且没有活跃线程，通知 waitForAll
            if (m_tasks.empty() && m_active.load() == 0) {
                m_finishedCondition.notify_all();
            }
        }
    }
}

void ThreadPool::waitForAll() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    // 等待直到：停止标志为真 或 (队列为空 且 活跃线程为 0)
    m_finishedCondition.wait(lock, [this] {
        return m_tasks.empty() && m_active.load() == 0;
    });
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stop.store(true);
    }

    // 唤醒所有线程，让它们检查 m_stop 并退出
    m_condition.notify_all();

    // join 所有线程，确保它们真正结束
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    m_workers.clear();
}