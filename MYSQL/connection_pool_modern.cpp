#include "connection_pool_modern.h"
#include <iostream>
#include <memory>
#include <utility>

ConnectionPool::ConnectionPool(ConnectionConfig  config, size_t initSize, size_t maxSize)
    : m_config(std::move(config)),
      m_initSize(initSize),
      m_maxSize(maxSize),
      m_currentSize(0),
      m_stopped(false)
{
}

ConnectionPool::~ConnectionPool() {
    if (!m_stopped) {
        shutdown();
    }
}

bool ConnectionPool::initialize() {
    if (m_initSize > m_maxSize) {
        std::cerr << "Error: initSize(" << m_initSize << ") > maxSize(" << m_maxSize << ")" << std::endl;
        return false;
    }

    for (size_t i = 0; i < m_initSize; ++i) {
        auto conn = createConnection();
        if (!conn) {
            shutdown();
            return false;
        }
        m_available.push(conn.release());  // release 交出所有权，存入原始指针队列
        m_currentSize.fetch_add(1);
    }
    return true;
}

std::shared_ptr<MySQLConnection> ConnectionPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);

    // 情况1：有空闲连接，直接取
    if (!m_available.empty()) {
        MySQLConnection* raw = m_available.front();
        m_available.pop();
        lock.unlock();  // 提前解锁，健康检查不需要持有锁

        // 健康检查：如果连接失效，销毁并尝试重建
        if (!raw->ping()) {
            delete raw;
            m_currentSize.fetch_sub(1);
            if (auto newConn = createConnection()) {
                m_currentSize.fetch_add(1);
                // 自定义 deleter：当 shared_ptr 引用计数归零时，调用 releaseConnection 归还
                return {
                    newConn.release(),
                    [this](MySQLConnection* p) { this->releaseConnection(p); }
                };
            }
            return nullptr;
        }

        // 连接有效，包装为 shared_ptr，自定义 deleter 自动归还
        return {
            raw,
            [this](MySQLConnection* p) { this->releaseConnection(p); }
        };
    }

    // 情况2：无空闲，但总数未达上限，创建新连接
    if (m_currentSize.load() < m_maxSize) {
        lock.unlock();
        if (auto newConn = createConnection()) {
            m_currentSize.fetch_add(1);
            return {
                newConn.release(),
                [this](MySQLConnection* p) { this->releaseConnection(p); }
            };
        }
        return nullptr;
    }

    // 情况3：已达上限，等待其他线程归还
    bool acquired = m_cv.wait_for(lock, timeout, [this] {
        return m_stopped || !m_available.empty();
    });

    if (!acquired || m_stopped || m_available.empty()) {
        return nullptr;  // 超时或已关闭
    }

    MySQLConnection* raw = m_available.front();
    m_available.pop();
    lock.unlock();

    if (!raw->ping()) {
        delete raw;
        m_currentSize.fetch_sub(1);
        if (auto newConn = createConnection()) {
            m_currentSize.fetch_add(1);
            return {
                newConn.release(),
                [this](MySQLConnection* p) { this->releaseConnection(p); }
            };
        }
        return nullptr;
    }

    return {
        raw,
        [this](MySQLConnection* p) { this->releaseConnection(p); }
    };
}

void ConnectionPool::releaseConnection(MySQLConnection* conn) {
    if (!conn) return;

    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_stopped) {
        m_available.push(conn);
        lock.unlock();  // 先解锁再 notify，减少唤醒线程的阻塞时间
        m_cv.notify_one();
    } else {
        lock.unlock();
        delete conn;
        m_currentSize.fetch_sub(1);
    }
}

size_t ConnectionPool::freeSize() const {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_available.size();
}

size_t ConnectionPool::totalSize() const {
    return m_currentSize.load();
}

void ConnectionPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    m_cv.notify_all();  // 唤醒所有等待 acquire 的线程

    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_available.empty()) {
        MySQLConnection* conn = m_available.front();
        m_available.pop();
        delete conn;
        m_currentSize.fetch_sub(1);
    }
}

std::unique_ptr<MySQLConnection> ConnectionPool::createConnection() const {
    MYSQL* raw = mysql_init(nullptr);
    if (!raw) {
        std::cerr << "mysql_init failed" << std::endl;
        return nullptr;
    }

    // 设置字符集
    if (!m_config.charset.empty()) {
        mysql_options(raw, MYSQL_SET_CHARSET_NAME, m_config.charset.c_str());
    }

    const char* unixSocket = m_config.unixSocket.empty() ? nullptr : m_config.unixSocket.c_str();

    if (!mysql_real_connect(raw,
                            m_config.host.c_str(),
                            m_config.user.c_str(),
                            m_config.password.c_str(),
                            m_config.database.c_str(),
                            m_config.port,
                            unixSocket,
                            m_config.clientFlag)) {
        std::cerr << "mysql_real_connect failed: " << mysql_error(raw) << std::endl;
        mysql_close(raw);
        return nullptr;
    }

    // 使用 unique_ptr 管理，如果后续失败会自动释放
    return std::make_unique<MySQLConnection>(raw);
}