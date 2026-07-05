#pragma once

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

// 数据库连接配置（现代C++：使用 std::string 替代 char*）
struct ConnectionConfig {
    std::string host = "localhost";
    std::string user;
    std::string password;
    std::string database;
    unsigned int port = 3306;
    std::string unixSocket;  // 空字符串表示使用 TCP
    unsigned long clientFlag = 0;
    std::string charset = "utf8mb4";  // 默认字符集
};

// RAII 封装的 MySQL 连接对象
// 职责：封装原始 MYSQL* 的生命周期，提供移动语义
class MySQLConnection {
public:
    // 从原始 MYSQL* 构造（接管所有权）
    explicit MySQLConnection(MYSQL* raw) : m_conn(raw) {}

    // 析构：自动关闭连接（RAII）
    ~MySQLConnection() {
        if (m_conn) {
            mysql_close(m_conn);
            m_conn = nullptr;
        }
    }

    // 禁用拷贝（资源不可复制）
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;

    // 启用移动语义（C++11 核心特性）
    MySQLConnection(MySQLConnection&& other) noexcept : m_conn(other.m_conn) {
        other.m_conn = nullptr;  // 将源对象置空，避免双重释放
    }
    MySQLConnection& operator=(MySQLConnection&& other) noexcept {
        if (this != &other) {
            if (m_conn) mysql_close(m_conn);
            m_conn = other.m_conn;
            other.m_conn = nullptr;
        }
        return *this;
    }

    // 访问原始 MYSQL*（用于执行 SQL）
    MYSQL* raw() const { return m_conn; }

    // 健康检查
    bool ping() const {
        return m_conn && mysql_ping(m_conn) == 0;
    }

    bool isValid() const { return m_conn != nullptr; }

private:
    MYSQL* m_conn;
};

// 连接池
class ConnectionPool {
public:
    ConnectionPool(ConnectionConfig  config, size_t initSize, size_t maxSize);
    ~ConnectionPool();

    // 禁止拷贝和移动
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    ConnectionPool(ConnectionPool&&) = delete;
    ConnectionPool& operator=(ConnectionPool&&) = delete;

    // 初始化：创建 initSize 个连接
    bool initialize();

    // 获取连接（RAII 包装，超时机制）
    // 返回 std::shared_ptr<MySQLConnection>，自定义 deleter 在销毁时自动归还连接
    std::shared_ptr<MySQLConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    );

    // 获取当前统计信息
    size_t freeSize() const;
    size_t totalSize() const;

    // 优雅关闭
    void shutdown();

private:
    // 创建一个新连接
    std::unique_ptr<MySQLConnection> createConnection() const;

    // 归还连接（被 shared_ptr 的自定义 deleter 调用）
    void releaseConnection(MySQLConnection* conn);

    ConnectionConfig m_config;
    size_t m_initSize;
    size_t m_maxSize;
    std::atomic<size_t> m_currentSize;  // 当前总连接数（原子类型，无锁读取）

    std::queue<MySQLConnection*> m_available;  // 空闲连接队列（存储原始指针）
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopped;
};