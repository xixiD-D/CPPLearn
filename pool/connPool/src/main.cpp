#include "MySQLConnPool.h"
#include "MySQLConfig.h"
#include "AsyncProcessor.h"
#include <cppconn/resultset.h>
#include <iostream>
#include <thread>
#include <chrono>

/*
g++ AsyncProcessor.cpp main.cpp MySQLConn.cpp MySQLConnPool.cpp MySQLWorker.cpp SQLOperation.cpp -o main -lpthread -lmysqlcppconn -std=c++17 -g
*/

void HandleQueryResult(std::unique_ptr<sql::ResultSet> res)
{
    if (!res) {
        std::cerr << "query failed or connection pool is unavailable" << std::endl;
        return;
    }

    while (res->next())
    {
        std::cout << "User: " << res->getString("User") << std::endl;
    }
}

int main()
{
    MySQLConfig config;
    config.host = "127.0.0.1";
    config.port = 3306;
    config.user = "zhongzexi";
    config.password = "123456";
    config.pool_size = 4;
    config.heartbeat_interval_s = 30;
    config.database = "mysql";

    MySQLConnPool *pool1 = MySQLConnPool::GetInstance(config);
    if (!pool1) {
        std::cerr << "failed to initialize mysql connection pool" << std::endl;
        return 1;
    }

    AsyncProcessor response_handler;

    auto query_callback1 = pool1->Query("SELECT * FROM user", HandleQueryResult);
    response_handler.AddQueryCallback(std::move(query_callback1));

    while (true)
    {
        response_handler.InvokeIfReady();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
