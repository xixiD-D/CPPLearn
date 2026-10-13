#pragma once

#include <string>

struct MySQLConfig {
    std::string host = "127.0.0.1";
    int port = 3306;
    std::string user = "zhongzexi";
    std::string password;
    std::string database;

    int pool_size = 4;

    int heartbeat_interval_s = 30;

    std::string GetUrl() const {
        return "tcp://" + host + ":" + std::to_string(port);
    }
};
