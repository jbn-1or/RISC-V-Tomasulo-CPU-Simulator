cmake_minimum_required(VERSION 3.16)
project(riscv_tomasulo_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 单元测试与 naive 对拍基准默认不构建：
#   OJ 端执行 `cmake .. && make` 时只产出 tomasulo_simulator 一个可执行文件，
#   不会因缺失测试源码或多余可执行文件而出错。
#   本地开发用 `cmake -DBUILD_TESTING=ON .. && make` 编译全部 target。
option(BUILD_TESTING "Build unit tests and naive reference interpreter" OFF)

# common sources (decoder, loader, etc.)
set(COMMON_SOURCES
    src/cpu.cpp
    src/instruction_fetch.cpp
    src/instruction_decoder.cpp
    src/memory_loader.cpp
)

# tomasulo simulator —— OJ 提交目标，始终构建
set(TOMASULO_SOURCES
    ${COMMON_SOURCES}
    src/alu.cpp
    src/reservation_station.cpp
    src/rob.cpp
    src/cdb.cpp
    src/load_store_buffer.cpp
    src/main.cpp
)

add_executable(tomasulo_simulator ${TOMASULO_SOURCES})
target_include_directories(tomasulo_simulator PRIVATE include)

# 以下为本地开发 / 调试用的辅助 target（单元测试与 naive 对拍基准）
if(BUILD_TESTING)
    # naive interpreter — 单周期顺序解释器，作为对拍基准
    add_executable(naive
        naive/naive.cpp
        src/alu.cpp
        src/instruction_fetch.cpp
        src/instruction_decoder.cpp
        src/memory_loader.cpp
    )
    target_include_directories(naive PRIVATE include)

    # decoder unit test
    add_executable(decoder_test
        test/decoder_test.cpp
        src/instruction_decoder.cpp
    )
    target_include_directories(decoder_test PRIVATE include)

    # rob unit test
    add_executable(rob_test
        test/rob_test.cpp
        src/rob.cpp
    )
    target_include_directories(rob_test PRIVATE include)

    # rs unit test
    add_executable(rs_test
        test/rs_test.cpp
        src/reservation_station.cpp
        src/rob.cpp
        src/alu.cpp
    )
    target_include_directories(rs_test PRIVATE include)

    # lsq unit test
    add_executable(lsq_test
        test/lsq_test.cpp
        src/load_store_buffer.cpp
        src/rob.cpp
    )
    target_include_directories(lsq_test PRIVATE include)

    # cdb unit test
    add_executable(cdb_test
        test/cdb_test.cpp
        src/cdb.cpp
        src/reservation_station.cpp
        src/load_store_buffer.cpp
        src/rob.cpp
        src/alu.cpp
    )
    target_include_directories(cdb_test PRIVATE include)

    # issue unit test
    add_executable(issue_test
        test/issue_test.cpp
        src/cpu.cpp
        src/instruction_fetch.cpp
        src/instruction_decoder.cpp
        src/memory_loader.cpp
        src/rob.cpp
        src/reservation_station.cpp
        src/load_store_buffer.cpp
        src/cdb.cpp
        src/alu.cpp
    )
    target_include_directories(issue_test PRIVATE include)

    # execute unit test
    add_executable(execute_test
        test/execute_test.cpp
        src/cpu.cpp
        src/instruction_fetch.cpp
        src/instruction_decoder.cpp
        src/memory_loader.cpp
        src/rob.cpp
        src/reservation_station.cpp
        src/load_store_buffer.cpp
        src/cdb.cpp
        src/alu.cpp
    )
    target_include_directories(execute_test PRIVATE include)

    # flush unit test
    add_executable(flush_test
        test/flush_test.cpp
        src/cpu.cpp
        src/instruction_fetch.cpp
        src/instruction_decoder.cpp
        src/memory_loader.cpp
        src/rob.cpp
        src/reservation_station.cpp
        src/load_store_buffer.cpp
        src/cdb.cpp
        src/alu.cpp
    )
    target_include_directories(flush_test PRIVATE include)
endif()
