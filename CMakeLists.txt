cmake_minimum_required(VERSION 3.16)
project(riscv_naive_interpreter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/main.cpp
    src/cpu.cpp
    src/executor.cpp
    src/instruction_fetch.cpp
    src/instruction_decoder.cpp
    src/memory_loader.cpp
)

# 可执行文件
add_executable(naive_interpreter ${SOURCES})

# 头文件搜索路径
target_include_directories(naive_interpreter PRIVATE include)