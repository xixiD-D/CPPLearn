#pragma once

#include <condition_variable>
#include <functional>
#include <queue>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------
// BlockingQueue：双队列 + 双锁
// 生产者只锁 prod_mutex_，消费者平时只锁 cons_mutex_，
// 消费队列空了才通过 SwapQueue_ 一次性交换两个队列，
// 大幅减少生产者和消费者之间的锁竞争
// ---------------------------------------------------------------
template <typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue(bool nonblock = false) : nonblock_(nonblock) {}

    void Push(const T &value)
    {
        std::lock_guard<std::mutex> lock(prod_mutex_);
        prod_queue_.push(value);
        not_empty_.notify_one();
    }

    bool Pop(T &value)
    {
        std::lock_guard<std::mutex> lock(cons_mutex_);
        if (cons_queue_.empty() && SwapQueue_() == 0)
        {
            return false;
        }
        value = cons_queue_.front();
        cons_queue_.pop();
        return true;
    }

    void Cancel()
    {
        std::lock_guard<std::mutex> lock(prod_mutex_);
        nonblock_ = true;
        not_empty_.notify_all();
    }

private:
    int SwapQueue_()
    {
        std::unique_lock<std::mutex> lock(prod_mutex_);
        not_empty_.wait(lock, [this] { return !prod_queue_.empty() || nonblock_; });
        std::swap(prod_queue_, cons_queue_);
        return static_cast<int>(cons_queue_.size());
    }

    bool nonblock_ = false;

    std::mutex prod_mutex_;
    std::queue<T> prod_queue_;

    std::mutex cons_mutex_;
    std::queue<T> cons_queue_;

    std::condition_variable not_empty_;
};