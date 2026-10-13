#pragma once
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <cppconn/resultset.h>
#include <unordered_map>

#include "QueryCallback.h"
#include "MySQLConfig.h"

namespace sql
{
    class ResultSet;
}

class MySQLConn;
template <typename T>
class BlockingQueue;
class SQLOperation;

class MySQLConnPool
{
public:
    static MySQLConnPool *GetInstance(const MySQLConfig &config);
    static MySQLConnPool *GetInstance(const std::string &db);

    ~MySQLConnPool();

    QueryCallback Query(const std::string &sql, std::function<void(std::unique_ptr<sql::ResultSet>)> &&cb);

private:
    explicit MySQLConnPool(const MySQLConfig &config);

    bool InitPool();

    MySQLConfig config_;
    std::vector<std::unique_ptr<MySQLConn>> pool_;
    std::mutex pool_mutex_;
    std::atomic<bool> available_{false};

    static inline std::unordered_map<std::string, std::unique_ptr<MySQLConnPool>> instances_;
    static inline std::mutex instances_mutex_;

    std::unique_ptr<BlockingQueue<std::unique_ptr<SQLOperation>>> task_queue_;
};
