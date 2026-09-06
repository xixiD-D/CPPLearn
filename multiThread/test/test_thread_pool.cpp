#include "thread_pool_modern.h"
#include "blocking_queue.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <numeric>
#include <vector>

// 等待条件满足，最多等 timeout，避免测试卡死
template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// 测试 1：所有投递的任务都被执行
void TestAllTasksExecuted()
{
    constexpr int kTaskNum = 1000;
    std::atomic<int> counter{0};

    {
        ThreadPool pool(4);
        for (int i = 0; i < kTaskNum; ++i)
        {
            pool.Post([&counter] { ++counter; });
        }
        bool ok = WaitFor([&] { return counter.load() == kTaskNum; });
        assert(ok && "not all tasks executed in time");
    } // 池析构：Cancel + join，不应死锁

    assert(counter == kTaskNum);
    std::cout << "[PASS] TestAllTasksExecuted: " << counter << " tasks done\n";
}

// 测试 2：任务结果正确（并行累加 1..N）
void TestTaskResults()
{
    constexpr int kN = 10000;
    std::vector<int> results(kN, 0);

    {
        ThreadPool pool(8);
        for (int i = 0; i < kN; ++i)
        {
            pool.Post([&results, i] { results[i] = i; });
        }
        std::atomic<int> done{0};
        // 用一个计数任务确认前面任务完成较繁琐，直接等结果数组写完：
        bool ok = WaitFor([&] {
            for (int i = 0; i < kN; ++i)
                if (results[i] != i) return false;
            return true;
        });
        assert(ok && "results not filled in time");
    }

    long long sum = std::accumulate(results.begin(), results.end(), 0LL);
    assert(sum == static_cast<long long>(kN - 1) * kN / 2);
    std::cout << "[PASS] TestTaskResults: sum(0.." << kN - 1 << ") = " << sum << "\n";
}

// 测试 3：BlockingQueue 的 Cancel 能唤醒阻塞中的 Pop
void TestBlockingQueueCancel()
{
    BlockingQueue<int> queue;
    std::atomic<bool> pop_returned{false};
    std::atomic<bool> pop_value{true}; // 期望 Pop 返回 false

    std::thread t([&] {
        int v = 0;
        pop_value = queue.Pop(v); // 队列空，会阻塞在这里
        pop_returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!pop_returned && "Pop should be blocking");

    queue.Cancel();
    t.join();

    assert(pop_returned && !pop_value);
    std::cout << "[PASS] TestBlockingQueueCancel\n";
}

// 测试 4：多生产者并发 Push 不丢数据
void TestConcurrentPushPop()
{
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 500;
    BlockingQueue<int> queue;

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&queue, p] {
            for (int i = 0; i < kPerProducer; ++i)
                queue.Push(p * kPerProducer + i);
        });
    }
    for (auto &t : producers) t.join();

    std::atomic<int> count{0};
    std::thread consumer([&] {
        int v;
        while (queue.Pop(v)) ++count;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    queue.Cancel();
    consumer.join();

    assert(count == kProducers * kPerProducer);
    std::cout << "[PASS] TestConcurrentPushPop: " << count << " items\n";
}

int main()
{
    TestAllTasksExecuted();
    TestTaskResults();
    TestBlockingQueueCancel();
    TestConcurrentPushPop();
    std::cout << "\nAll tests passed!\n";
    return 0;
}