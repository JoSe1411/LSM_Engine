cmake_minimum_required(VERSION 3.15)
project(LSM_Engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compiler warnings for systems programming safety
add_compile_options(-Wall -Wextra -Werror -O2)

# Include the headers folder
include_directories(include)

# Core engine library
add_library(lsm_engine
    src/MemTable.cpp
)

# Sandbox runner
add_executable(lsm_sandbox
    src/main.cpp
)
target_link_libraries(lsm_sandbox lsm_engine)

# Unit test runner
add_executable(memtable_test
    tests/MemTable_test.cpp
)
target_link_libraries(memtable_test lsm_engine)
