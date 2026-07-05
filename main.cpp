

#include <unistd.h>

#include "MYSQL/connection_pool.h"
#include "multiThread/thread_pool.h"

// 任务：持有连接池指针，执行数据库操作
class DBQueryTask : public Task {
    ConnectionPool* m_pool;
    int m_taskId;
public:
    DBQueryTask(ConnectionPool* pool, int id) : m_pool(pool), m_taskId(id) {}
    void execute() override {
        MYSQL* conn = m_pool->getConnection(3000);
        if (!conn) {
            printf("  [Task %d] Failed to get connection\n", m_taskId);
            return;
        }

        char sql[256];
        snprintf(sql, sizeof(sql),
                 "SELECT %d AS task_id, CONNECTION_ID() AS conn_id", m_taskId);

        mysql_query(conn, sql);
        if (MYSQL_RES* res = mysql_store_result(conn)) {
            MYSQL_ROW row = mysql_fetch_row(res);
            printf("  [Task %d] Result: task_id=%s, conn_id=%s, thread=%lu\n",
                   m_taskId, row[0], row[1], static_cast<unsigned long>(pthread_self()));
            mysql_free_result(res);
        }

        m_pool->releaseConnection(conn);
    }
};

// 主函数
int main() {
    ConnectionInfo info;
    info.host = "192.168.31.82";
    info.user = "zhongzexi";
    info.password = "123456";
    info.database = "test_DB";
    info.port = 3306;

    ConnectionPool pool(info, 2, 5);
    if (!pool.initialize()) return 1;

    ThreadPool threadPool(4); // 4 个工作线程
    if (!threadPool.initialize()) return 1;

    // 提交 20 个数据库任务
    for (int i = 0; i < 20; ++i) {
        threadPool.addTask(new DBQueryTask(&pool, i));
    }

    sleep(3);
    threadPool.shutdown();
    pool.shutdown();
    return 0;
}