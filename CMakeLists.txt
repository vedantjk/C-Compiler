cmake_minimum_required(VERSION 3.20)
project(cc89 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

set(SOURCES
    src/main.cpp
    src/lexer/lexer.cpp
    src/utils/diagnostic.cpp
)

add_executable(cc89 ${SOURCES})

target_include_directories(cc89 PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Tests
add_executable(test_lexer
    src/lexer/tests/test_lexer.cpp
    src/lexer/lexer.cpp
    src/utils/diagnostic.cpp
)
target_include_directories(test_lexer PRIVATE ${CMAKE_SOURCE_DIR}/src/lexer)

# Auto-format sources before building
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format"
    )
    add_dependencies(cc89 format)
endif()
