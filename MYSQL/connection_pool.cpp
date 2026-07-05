#include "connection_pool.h"

#include <sys/time.h>

ConnectionPool::ConnectionPool(const ConnectionInfo &info, int initSize, int maxSize) : m_info(info),
    m_initSize(initSize), m_maxSize(maxSize), m_currentSize(0), m_shutdown(false) {
    pthread_mutex_init(&m_mutex, nullptr);
    pthread_cond_init(&m_cond, nullptr);
}

ConnectionPool::~ConnectionPool() {
    if (!m_shutdown) {
        shutdown();
    }
}

bool ConnectionPool::initialize() {
    if (m_initSize > m_maxSize) {
        fprintf(stderr, "Error: initSize(%d) > maxSize(%d)\n", m_initSize, m_maxSize);
        return false;
    }
    for (int i = 0; i < m_initSize; i++) {
        MYSQL *conn = createConnection();
        if (!conn) {
            // 回滚：关闭创建的连接
            shutdown();
            return false;
        }
        m_freeList.push_back(conn);
        ++m_currentSize;
    }
    return true;
}

MYSQL *ConnectionPool::getConnection(int timeoutMs) {
    pthread_mutex_lock(&m_mutex);
    // 情况1:有空闲连接直接取
    if (!m_freeList.empty()) {
        MYSQL *conn = m_freeList.front();
        m_freeList.pop_front();

        // 健康检查:如果连接已失效，销毁并重建
        if (!validateConnection(conn)) {
            mysql_close(conn);
            --m_currentSize;
            conn = createConnection();
            if (conn) {
                m_currentSize++;
            }
        }

        pthread_mutex_unlock(&m_mutex);
        return conn;
    }

    // 情况2:无空闲连接，但当前总数未达到上限，创建新连接
    if (m_currentSize < m_maxSize) {
        MYSQL *conn = createConnection();
        if (conn) {
            m_currentSize++;
        }
        pthread_mutex_unlock(&m_mutex);
        return conn;
    }

    // 情况3:连接已达上限，必须等待其他线程归还
    if (timeoutMs < 0) {
        // 无限等待
        while (m_freeList.empty() && !m_shutdown) {
            pthread_cond_wait(&m_cond, &m_mutex);
        }
    } else {
        // 计算绝对超时时间
        struct timeval now;
        gettimeofday(&now, nullptr);
        struct timespec absTime;
        absTime.tv_sec = now.tv_sec + timeoutMs / 1000;
        absTime.tv_nsec = now.tv_usec * 1000 + (timeoutMs % 1000) * 1000000;
        if (absTime.tv_nsec > 1000000000) {
            absTime.tv_sec += 1;
            absTime.tv_nsec -= 1000000000;
        }

        while (m_freeList.empty() && !m_shutdown) {
            int ret = pthread_cond_timedwait(&m_cond, &m_mutex, &absTime);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&m_mutex);
                return nullptr;
            }
        }
    }

    // 被唤醒后检查是否因为 shutdown
    if (m_shutdown) {
        pthread_mutex_unlock(&m_mutex);
        return nullptr;
    }

    // 等到一个连接了
    MYSQL *conn = m_freeList.front();
    m_freeList.pop_front();

    // 健康检查
    if (!validateConnection(conn)) {
        mysql_close(conn);
        m_currentSize--;
        conn = createConnection();
        if (conn) {
            m_currentSize++;
        }
    }

    pthread_mutex_unlock(&m_mutex);
    return conn;
}

void ConnectionPool::releaseConnection(MYSQL *conn) {
    if (!conn) {
        return;
    }
    pthread_mutex_lock(&m_mutex);
    if (!m_shutdown) {
        m_freeList.push_back(conn);
        pthread_cond_signal(&m_cond); // 唤醒一个等待的线程
    } else {
        // 已经关闭，直接销毁
        mysql_close(conn);
        --m_currentSize;
    }
    pthread_mutex_unlock(&m_mutex);
}

int ConnectionPool::getFreeSize() {
    pthread_mutex_lock(&m_mutex);
    int size = m_freeList.size();
    pthread_mutex_unlock(&m_mutex);
    return size;
}

int ConnectionPool::getTotalSize() {
    pthread_mutex_lock(&m_mutex);
    int size = m_currentSize;
    pthread_mutex_unlock(&m_mutex);
    return size;
}

void ConnectionPool::shutdown() {
    pthread_mutex_lock(&m_mutex);
    m_shutdown = true;
    pthread_cond_broadcast(&m_cond);
    pthread_mutex_unlock(&m_mutex);

    // 关闭所有连接
    pthread_mutex_lock(&m_mutex);
    while (!m_freeList.empty()) {
        MYSQL *conn = m_freeList.front();
        m_freeList.pop_front();
        mysql_close(conn);
        --m_currentSize;
    }
    pthread_mutex_unlock(&m_mutex);

    // 注意：正在使用的连接不会被强制关闭
    // 生产环境需要更复杂的引用计数机制
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

MYSQL *ConnectionPool::createConnection() const {
    MYSQL *conn = mysql_init(nullptr);
    if (!conn) {
        fprintf(stderr, "Error: mysql_init() failed\n");
        return nullptr;
    }
    const char *unixSocket = m_info.unixSocket.empty() ? nullptr : m_info.unixSocket.c_str();

    if (!mysql_real_connect(conn, m_info.host.c_str(), m_info.user.c_str(), m_info.password.c_str(),
                            m_info.database.c_str(), m_info.port, unixSocket, m_info.clientFlag)) {
        fprintf(stderr, "Error: mysql_real_connect() failed\n");
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

bool ConnectionPool::validateConnection(MYSQL *conn) {
    if (!conn) {
        return false;
    }
    // mysql_ping 发送一个ping包，检测连接是否有效
    // 如果连接服务端关闭（如wait_timeout 超时），mysql_ping 会尝试重连
    if (mysql_ping(conn) != 0) {
        fprintf(stderr, "Connection lost, trying to reconnect: %s\n", mysql_error(conn));
        return false;
    }
    return true;
}
