#include "thread_pool_modern.h"
#include "blocking_queue.h" // 完整定义只出现在 .cpp，对头文件使用者不可见

ThreadPool::ThreadPool(size_t threads_num)
    : task_queue_(std::make_unique<BlockingQueue<std::function<void()>>>())
{
    for (size_t i = 0; i < threads_num; i++)
    {
        workers_.emplace_back([this]
                              { Worker(); });
    }
}

void ThreadPool::Worker()
{
    while (true)
    {
        std::function<void()> task;
        if (!task_queue_->Pop(task))
        {
            break;
        }
        task();
    }
}

void ThreadPool::Post(std::function<void()> task)
{
    task_queue_->Push(task);
}

ThreadPool::~ThreadPool()
{
    task_queue_->Cancel();
    for (auto &worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}