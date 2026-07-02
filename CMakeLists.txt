# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(main CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(main
        main.cpp
        multiThread/thread_pool.cpp
)