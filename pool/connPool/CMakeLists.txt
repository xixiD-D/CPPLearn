cmake_minimum_required(VERSION 3.10)
project(MySQLConnPool)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置必要的包
set(MYSQLCPPCONN_INCLUDE_DIRS /usr/include/cppconn)
set(MYSQLCPPCONN_LIBRARIES libmysqlcppconn.so)
set(MYSQLCPPCONN_LIBRARY_DIRS /usr/lib/x86_64-linux-gnu)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra")

# 包含头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${MYSQLCPPCONN_INCLUDE_DIRS})

# 源文件
set(SOURCES
    src/main.cpp
    src/MySQLConn.cpp
    src/MySQLConnPool.cpp
    src/MySQLWorker.cpp
    src/AsyncProcessor.cpp
    src/SQLOperation.cpp
)

# 创建可执行文件
add_executable(mysql_connpool_demo ${SOURCES})

# 链接库
target_link_libraries(mysql_connpool_demo 
    ${MYSQLCPPCONN_LIBRARIES}
    pthread
)

# 设置库路径
link_directories(${MYSQLCPPCONN_LIBRARY_DIRS})

# 安装目标
install(TARGETS mysql_connpool_demo DESTINATION bin) 