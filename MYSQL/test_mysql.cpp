#include <iostream>
#include <mysql.h>
#include <ostream>

#define HOST "192.168.31.82"
#define USER "zhongzexi"
#define PASSWORD "123456"
#define PORT 3306
#define DATABASE "test_DB"

int main() {
    MYSQL *mysql = mysql_init(nullptr);
    if (!mysql) {
        std::cout << "mysql_init failed" << std::endl;
        return 1;
    }

    // 连接数据库
    // 参数：mysql句柄、主机、用户、密码、数据库、端口、unit_socket、client_flag
    if (!mysql_real_connect(mysql, HOST, USER, PASSWORD, DATABASE, PORT, nullptr, 0)) {
        printf("Connect failed: %s\n", mysql_error(mysql));
        mysql_close(mysql);
        return 2;
    }

    printf("Connected! Server version: %s\n", mysql_get_server_info(mysql));

    // 执行查询
    if (mysql_query(mysql, "SELECT 1+1 AS result")) {
        printf("Query failed: %s\n", mysql_error(mysql));
    } else {
        if (MYSQL_RES* res = mysql_store_result(mysql)) {
            MYSQL_ROW row = mysql_fetch_row(res);
            printf("1+1 = %s\n", row[0]);
            mysql_free_result(res);
        }
    }

    mysql_close(mysql);
    return 0;
}
