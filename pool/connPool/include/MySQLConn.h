#pragma once

#include <string>
#include <memory>
#include <cppconn/resultset.h>

#include "MySQLConfig.h"

namespace sql
{
    class Driver;
    class Connection;
    class SQLException;
    class ResultSet;
}

class MySQLWorker;

template <typename T>
class BlockingQueue;

class SQLOperation;

class MySQLConn
{
public:
    MySQLConn(const MySQLConfig &config, BlockingQueue<std::unique_ptr<SQLOperation>> &task_queue);
    ~MySQLConn();

    int Open();
    void Close();
    void StartWorker();
    bool Ping();
    bool Reconnect();

    sql::ResultSet *Query(const std::string &sql);

    void RequestStop();

private:
    void HandlerException(sql::SQLException &e);

    MySQLConfig config_;
    sql::Driver *driver_ = nullptr;
    std::unique_ptr<sql::Connection> conn_;
    std::unique_ptr<MySQLWorker> worker_;
};
