#pragma once

#include <thread>
#include <functional>
#include <memory>     // std::unique_ptr
#include <vector>

// 前置声明：指针成员不需要完整类型
// 注意拼写：BlockingQueue（中间只有一个 n）
template <typename T>
class BlockingQueue;

class ThreadPool
{
public:
    // 初始化线程
    explicit ThreadPool(size_t numThreads);

    // 停止线程池
    // 注意：析构必须在 .cpp 中定义（那里才有 BlockingQueue 的完整定义），
    // 否则 unique_ptr 无法销毁不完整类型
    ~ThreadPool();

    // 发布任务到线程池
    void Post(std::function<void()> task);

private:
    std::unique_ptr<BlockingQueue<std::function<void()>>> task_queue_;
    std::vector<std::thread> workers_;

    void Worker();
};