#include "MySQLConnPool.h"
#include "MySQLConn.h"
#include "SQLOperation.h"
#include "QueryCallback.h"
#include "BlockingQueue.h"

#include <cppconn/resultset.h>
#include <future>
#include <iostream>
#include <utility>
#include <vector>

MySQLConnPool *MySQLConnPool::GetInstance(const MySQLConfig &config)
{
    std::lock_guard<std::mutex> lock(instances_mutex_);
    const std::string &db = config.database;

    auto it = instances_.find(db);
    if (it != instances_.end())
    {
        return it->second.get();
    }

    auto pool = std::unique_ptr<MySQLConnPool>(new MySQLConnPool(config));
    if (!pool->InitPool())
    {
        return nullptr;
    }

    MySQLConnPool *raw_ptr = pool.get();
    instances_.emplace(db, std::move(pool));
    return raw_ptr;
}

MySQLConnPool *MySQLConnPool::GetInstance(const std::string &db)
{
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(db);
    if (it == instances_.end())
    {
        return nullptr;
    }
    return it->second.get();
}

MySQLConnPool::MySQLConnPool(const MySQLConfig &config)
    : config_(config)
{
}

bool MySQLConnPool::InitPool()
{
    auto task_queue = std::make_unique<BlockingQueue<std::unique_ptr<SQLOperation>>>();
    std::vector<std::unique_ptr<MySQLConn>> new_pool;
    new_pool.reserve(config_.pool_size);

    for (int i = 0; i < config_.pool_size; ++i)
    {
        auto conn = std::make_unique<MySQLConn>(config_, *task_queue);
        if (conn->Open() != 0)
        {
            std::cerr << "Failed to open connection " << i << ", rollback pool initialization" << std::endl;
            return false;
        }
        new_pool.push_back(std::move(conn));
    }

    for (auto &conn : new_pool)
    {
        conn->StartWorker();
    }

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        task_queue_ = std::move(task_queue);
        pool_ = std::move(new_pool);
        available_ = true;
    }
    return true;
}

MySQLConnPool::~MySQLConnPool()
{
    available_ = false;

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        for (auto &conn : pool_)
        {
            conn->RequestStop();
        }
    }

    if (task_queue_)
    {
        task_queue_->Cancel();
    }

    std::vector<std::unique_ptr<MySQLConn>> snapshot;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        snapshot.swap(pool_);
    }
}

QueryCallback MySQLConnPool::Query(const std::string &sql,
                                   std::function<void(std::unique_ptr<sql::ResultSet>)> &&cb)
{
    if (!available_ || !task_queue_)
    {
        std::promise<std::unique_ptr<sql::ResultSet>> promise;
        promise.set_value(nullptr);
        return QueryCallback(promise.get_future(), std::move(cb));
    }

    auto op = std::make_unique<SQLOperation>(sql);
    auto future = op->GetFuture();
    task_queue_->Push(std::move(op));
    return QueryCallback(std::move(future), std::move(cb));
}
