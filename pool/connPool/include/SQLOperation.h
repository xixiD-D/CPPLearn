#pragma once

#include <string>
#include <future>
#include <memory>
#include <cppconn/resultset.h>

namespace sql
{
    class ResultSet;
}

class MySQLConn;

class SQLOperation
{
public:
    explicit SQLOperation(const std::string &sql) : sql_(sql) {}
    void Execute(MySQLConn *conn);

    std::future<std::unique_ptr<sql::ResultSet>> GetFuture()
    {
        return promise_.get_future();
    }

private:
    std::string sql_;
    std::promise<std::unique_ptr<sql::ResultSet>> promise_;
};
