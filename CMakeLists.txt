cmake_minimum_required(VERSION 3.20)
project(cc89 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# All compiler sources: every .cpp under src/, excluding test files (which
# carry their own main()). CONFIGURE_DEPENDS re-scans for added/removed files
# on each build, so this never needs hand-editing again.
file(GLOB_RECURSE CC89_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER CC89_SOURCES EXCLUDE REGEX "/tests/")

add_executable(cc89 ${CC89_SOURCES})

target_include_directories(cc89 PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Tests
add_executable(test_lexer
    src/lexer/tests/test_lexer.cpp
    src/lexer/lexer.cpp
    src/utils/diagnostic.cpp
)
target_include_directories(test_lexer PRIVATE ${CMAKE_SOURCE_DIR}/src/lexer)

# clang-format target. On Windows, hint the standalone LLVM install; on PATH-based
# systems (Linux/CI) the hint is harmlessly ignored.
find_program(CLANG_FORMAT clang-format HINTS "C:/Program Files/LLVM/bin")
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.h
        ${CMAKE_SOURCE_DIR}/src/*.hpp)
    # Manual target: `cmake --build <dir> --target format` reformats in place.
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format"
    )
    # Only auto-format as part of the build on Windows (the original dev flow).
    # On Linux/CI a different clang-format version would rewrite tracked sources
    # on every build, so there `format` stays opt-in via the manual target above.
    if(WIN32)
        add_dependencies(cc89 format)
    endif()
endif()
