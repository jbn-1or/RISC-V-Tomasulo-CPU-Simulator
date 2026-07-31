cmake_minimum_required(VERSION 3.16)
project(riscv_tomasulo_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# common sources (decoder, loader, etc.)
set(COMMON_SOURCES
    src/cpu.cpp
    src/executor.cpp
    src/instruction_fetch.cpp
    src/instruction_decoder.cpp
    src/memory_loader.cpp
)

# naive interpreter — standalone executable
add_executable(naive
    naive/naive.cpp
    src/alu.cpp
    src/instruction_fetch.cpp
    src/instruction_decoder.cpp
    src/memory_loader.cpp
)
target_include_directories(naive PRIVATE include)

# tomasulo simulator
set(TOMASULO_SOURCES
    ${COMMON_SOURCES}
    src/alu.cpp
    src/reservation_station.cpp
    src/rob.cpp
    src/cdb.cpp
    src/load_store_buffer.cpp
    src/branch_predictor.cpp
    src/main.cpp
)

add_executable(tomasulo_simulator ${TOMASULO_SOURCES})
target_include_directories(tomasulo_simulator PRIVATE include)
