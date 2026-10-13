#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <queue>
#include <mutex>
#include <thread>

template <typename T>
class BlockingQueue {
public:
    BlockingQueue(bool nonblock = false) : nonblock_(nonblock) { }

    void Push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    bool Pop(T &value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this]{ return !queue_.empty() || nonblock_; });
        if (queue_.empty()) return false;

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool PopWithTimeout(T &value, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                [this]{ return !queue_.empty() || nonblock_; })) {
            return false;
        }
        if (queue_.empty()) return false;

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void Cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        nonblock_ = true;
        not_empty_.notify_all();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    bool nonblock_;
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
};
