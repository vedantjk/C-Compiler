cmake_minimum_required(VERSION 3.20)
project(cc89 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Source files — add new .cpp files here as you create them
set(SOURCES
    src/main.cpp
)

add_executable(cc89 ${SOURCES})

target_include_directories(cc89 PRIVATE ${CMAKE_SOURCE_DIR}/src)
