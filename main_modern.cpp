#include "multiThread/thread_pool_modern.h"
#include "MYSQL/connection_pool_modern.h"
#include <iostream>
#include <sstream>

// 模拟业务逻辑：执行数据库查询
void dbQueryTask(ConnectionPool& pool, int taskId) {
    // acquire 返回 shared_ptr，离开作用域时自动归还连接
    auto conn = pool.acquire(std::chrono::milliseconds(3000));
    if (!conn) {
        std::cout << "  [Task " << taskId << "] Failed to acquire connection (timeout or pool stopped)"
                  << std::endl;
        return;
    }

    std::ostringstream sql;
    sql << "SELECT " << taskId << " AS task_id, CONNECTION_ID() AS conn_id, NOW() AS now";

    if (mysql_query(conn->raw(), sql.str().c_str()) != 0) {
        std::cout << "  [Task " << taskId << "] Query failed: " << mysql_error(conn->raw()) << std::endl;
        return;
    }

    if (MYSQL_RES* res = mysql_store_result(conn->raw())) {
        if (MYSQL_ROW row = mysql_fetch_row(res)) {
            std::cout << "  [Task " << taskId << "] Result: task_id=" << row[0]
                      << ", conn_id=" << row[1]
                      << ", now=" << row[2]
                      << ", worker_thread=" << std::this_thread::get_id() << std::endl;
        }
        mysql_free_result(res);
    }

    // conn 是 shared_ptr，离开作用域时：
    // 1. 引用计数 -1
    // 2. 如果引用计数归零，调用自定义 deleter -> releaseConnection -> 归还到连接池
}

int main() {
    // ========== 配置连接池 ==========
    ConnectionConfig dbConfig;
    dbConfig.host = "192.168.31.82";
    dbConfig.user = "zhongzexi";
    dbConfig.password = "123456";  // 修改为你的密码
    dbConfig.database = "test_DB";
    dbConfig.port = 3306;
    dbConfig.charset = "utf8mb4";

    // 初始 2 个连接，最大 5 个
    ConnectionPool connPool(dbConfig, 2, 5);
    if (!connPool.initialize()) {
        std::cerr << "[Main] Connection pool init failed" << std::endl;
        return 1;
    }

    std::cout << "[Main] Connection pool ready. Free: " << connPool.freeSize()
              << ", Total: " << connPool.totalSize() << std::endl;

    // ========== 配置线程池 ==========
    ThreadPool threadPool(4);  // 4 个工作线程

    std::cout << "[Main] Thread pool ready. Threads: " << threadPool.threadCount() << std::endl;

    // ========== 提交 20 个数据库任务 ==========
    std::cout << "[Main] Submitting 20 tasks..." << std::endl;

    std::vector<std::future<void>> futures;
    futures.reserve(20);

    for (int i = 0; i < 20; ++i) {
        // submit 返回 future，捕获任务结果或异常
        // 使用 lambda 捕获 pool 和 i，std::ref 避免拷贝 ConnectionPool
        futures.push_back(
            threadPool.submit(dbQueryTask, std::ref(connPool), i)
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 模拟请求间隔
    }

    // ========== 等待所有任务完成 ==========
    std::cout << "[Main] Waiting for all tasks to complete..." << std::endl;

    // 方式1：逐个 get() 等待（会阻塞直到对应任务完成）
    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            futures[i].get();  // 如果任务抛出异常，会在这里重新抛出
        } catch (const std::exception& e) {
            std::cerr << "  [Task " << i << "] Exception: " << e.what() << std::endl;
        }
    }

    // 方式2：使用 threadPool.waitForAll()（更优雅，不依赖 future）
    // threadPool.waitForAll();

    std::cout << "[Main] All tasks completed. Free: " << connPool.freeSize()
              << ", Total: " << connPool.totalSize() << std::endl;

    // ========== 优雅关闭 ==========
    std::cout << "[Main] Shutting down..." << std::endl;

    threadPool.shutdown();   // 先关闭线程池（等待任务完成）
    connPool.shutdown();     // 再关闭连接池（释放所有连接）

    std::cout << "[Main] Done." << std::endl;
    return 0;
}