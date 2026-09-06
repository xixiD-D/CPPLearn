# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(main CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# === MySQL 配置 ===
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

# 包含 MySQL 头文件路径
include_directories(${MYSQL_INCLUDE_DIRS})

add_executable(main
        main.cpp
        multiThread/src/thread_pool_modern.cpp
        MYSQL/connection_pool.cpp
        network/cTcpClient.cpp
        network/cTcpServer.cpp
)

# 链接 MySQL 库
target_link_libraries(main ${MYSQL_LIBRARIES})