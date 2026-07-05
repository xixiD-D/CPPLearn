#include <cstdio>
#include <pthread.h>
#include <mysql.h>
#include <list>
#include <thread>

// 连接池参数
auto HOST = "192.168.31.82";
auto USER = "zhongzexi";
auto PASSWORD = "123456";
auto DB = "test_DB";
constexpr int PORT = 3306;

// 连接池
class SimplePool {
public:
    explicit SimplePool(int initSize) : m_maxSize(initSize) {
        pthread_mutex_init(&m_mutex, nullptr);
    }

    ~SimplePool() {
        pthread_mutex_lock(&m_mutex);
        while (!m_freeList.empty()) {
            // 还有空闲的连接
            MYSQL *conn = m_freeList.front();
            m_freeList.pop_front();
            mysql_close(conn);
        }
        pthread_mutex_unlock(&m_mutex);
        pthread_mutex_destroy(&m_mutex);
    }

    bool initialize() {
        for (int i = 0; i < m_maxSize; i++) {
            MYSQL *conn = createConnection();
            if (!conn) {
                return false;
            }
            m_freeList.push_back(conn);
        }
        return true;
    }

    // 获取连接
    MYSQL* getConnection() {
        pthread_mutex_lock(&m_mutex);
        if (m_freeList.empty()) {
            // 说明没有空闲连接了
            pthread_mutex_unlock(&m_mutex);
            return nullptr;
        }
        MYSQL* conn = m_freeList.front();
        m_freeList.pop_front();
        pthread_mutex_unlock(&m_mutex);
        return conn;
    }

    // 归还连接
    void releaseConnection(MYSQL* conn) {
        if (!conn) {
            return;
        }
        pthread_mutex_lock(&m_mutex);
        m_freeList.push_back(conn);
        pthread_mutex_unlock(&m_mutex);
    }

private:
    int m_maxSize;
    pthread_mutex_t m_mutex{};
    std::list<MYSQL *> m_freeList;

    static MYSQL *createConnection() {
        MYSQL *conn = mysql_init(nullptr);
        if (!conn) {
            return nullptr;
        }
        if (!mysql_real_connect(conn, HOST, USER, PASSWORD, DB, PORT, nullptr, 0)) {
            printf("Create connection failed: %s\n", mysql_error(conn));
            mysql_close(conn);
            return nullptr;
        }
        return conn;
    }
};

// ============ 测试 ============
void* worker(void* arg) {
    SimplePool* pool = static_cast<SimplePool *>(arg);
    MYSQL* conn = pool->getConnection();
    if (!conn) {
        printf("  [Thread %lu] No connection available\n", static_cast<unsigned long>(pthread_self()));
        return nullptr;
    }

    printf("  [Thread %lu] Got connection, querying...\n", static_cast<unsigned long>(pthread_self()));
    mysql_query(conn, "SELECT 1+1 AS result");
    if (MYSQL_RES* res = mysql_store_result(conn)) {
        MYSQL_ROW row = mysql_fetch_row(res);
        printf("  [Thread %lu] Result: %s\n", static_cast<unsigned long>(pthread_self()), row[0]);
        mysql_free_result(res);
    }

    pool->releaseConnection(conn);
    printf("  [Thread %lu] Released connection\n", static_cast<unsigned long>(pthread_self()));
    return nullptr;
}

int main() {
    SimplePool pool(3);
    if (!pool.initialize()) {
        printf("Pool init failed\n");
        return 1;
    }
    printf("[Main] Creating 5 threads, but only 3 connections...\n");
    pthread_t threads[5];
    for (int i = 0; i < 5; i++) {
        pthread_create(&threads[i], nullptr, worker, &pool);
    }
    for (int i = 0; i < 5; i++) {
        pthread_join(threads[i], nullptr);
    }
    printf("[Main] Done\n");
    return 0;
}
