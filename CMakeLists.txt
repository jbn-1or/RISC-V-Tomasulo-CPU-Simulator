cmake_minimum_required(VERSION 3.16)
project(riscv_tomasulo_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# common sources (decoder, loader, etc.)
set(COMMON_SOURCES
    src/cpu.cpp
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
    src/main.cpp
)

add_executable(tomasulo_simulator ${TOMASULO_SOURCES})
target_include_directories(tomasulo_simulator PRIVATE include)

# decoder unit test
add_executable(decoder_test
    src/decoder_test.cpp
    src/instruction_decoder.cpp
)
target_include_directories(decoder_test PRIVATE include)

# rob unit test
add_executable(rob_test
    src/rob_test.cpp
    src/rob.cpp
)
target_include_directories(rob_test PRIVATE include)

# rs unit test
add_executable(rs_test
    src/rs_test.cpp
    src/reservation_station.cpp
)
target_include_directories(rs_test PRIVATE include)
