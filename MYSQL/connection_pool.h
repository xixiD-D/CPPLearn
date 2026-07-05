#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <list>
#include <string>
#include <pthread.h>
#include <mysql.h>

// 数据库连接信息
struct ConnectionInfo {
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    unsigned int port;
    std::string unixSocket; // NULL 表示使用TCP
    unsigned long clientFlag;

    ConnectionInfo() : port(3306), clientFlag(0) {
    }
};

// 连接池类
class ConnectionPool {
public:
    // 构造函数：传入连接信息和池大小参数
    ConnectionPool(const ConnectionInfo &info, int initSize, int maxSize);

    ~ConnectionPool();

    // 初始化：创建 initSize 个连接
    bool initialize();

    // 获取连接（阻塞、超时检测，单位：毫秒）
    // timeoutMs = -1 表示无限等待
    MYSQL *getConnection(int timeoutMs = 5000);

    // 归还连接
    void releaseConnection(MYSQL *conn);

    // 获取当前空闲连接数
    int getFreeSize();

    // 获取当前总连接数
    int getTotalSize();

    // 关闭所有连接，唤醒等待进程
    void shutdown();

private:
    // 禁止拷贝
    ConnectionPool(const ConnectionPool &);

    ConnectionPool &operator=(const ConnectionPool &);

    // 创建一个新连接
    MYSQL *createConnection() const;

    // 检查连接是否有效，无效则重建
    static bool validateConnection(MYSQL *conn);

    ConnectionInfo m_info;
    int m_initSize; // 初始连接数
    int m_maxSize; // 最大连接数
    int m_currentSize; // 当前总连接数

    std::list<MYSQL *> m_freeList; // 空闲连接队列
    pthread_mutex_t m_mutex{};
    pthread_cond_t m_cond{};
    bool m_shutdown; // 关闭标志
};
#endif
