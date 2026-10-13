#pragma once

#include <thread>
#include <atomic>
#include <memory>

class MySQLConn;

template <typename T>
class BlockingQueue;

class SQLOperation;

class MySQLWorker
{
public:
    MySQLWorker(MySQLConn *conn, BlockingQueue<std::unique_ptr<SQLOperation>> &task_queue, int heartbeat_interval_ms);
    ~MySQLWorker();

    void Start();
    void Stop();
    void RequestStop();

private:
    void Worker();

    MySQLConn *conn_;
    std::thread worker_;
    BlockingQueue<std::unique_ptr<SQLOperation>> &task_queue_;
    int heartbeat_interval_ms_;
    std::atomic<bool> should_stop_{false};
};
