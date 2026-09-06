cmake_minimum_required(VERSION 3.14)
project(multiThread CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# 线程池库（blocking_queue.h 是模板头文件，不需要列进来编译）
add_library(thread_pool src/thread_pool_modern.cpp)
target_include_directories(thread_pool PUBLIC src)
target_link_libraries(thread_pool PUBLIC Threads::Threads)

# 测试程序
add_executable(test_thread_pool test/test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE thread_pool)

enable_testing()
add_test(NAME thread_pool_test COMMAND test_thread_pool)