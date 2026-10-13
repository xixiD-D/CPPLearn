
#pragma once

#include <future>
#include <functional>
#include <memory>
#include <cppconn/resultset.h>

namespace sql
{
    class ResultSet;
}

class QueryCallback {
public:
    QueryCallback(std::future<std::unique_ptr<sql::ResultSet>> &&future, std::function<void(std::unique_ptr<sql::ResultSet>)> &&cb)
        : future_(std::move(future)), cb_(std::move(cb))
    {
    }

    bool InvokeIfReady() {
        if (future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            cb_(future_.get());
            return true;
        }
        return false;
    }
private:
    std::future<std::unique_ptr<sql::ResultSet>> future_;
    std::function<void(std::unique_ptr<sql::ResultSet>)> cb_;
};
