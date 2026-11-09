cmake_minimum_required(VERSION 3.14)
project(docmerge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(docmerge_core STATIC
    src/util/hash.cpp
    src/util/strings.cpp
    src/util/glob.cpp
    src/util/fileio.cpp
    src/doc/line.cpp
    src/merge/diff.cpp
    src/merge/conflict.cpp
    src/merge/diff3.cpp
    src/cli/args.cpp
    src/cli/commands.cpp
)
target_include_directories(docmerge_core PUBLIC src)

add_executable(docmerge
    src/main.cpp
)
target_link_libraries(docmerge PRIVATE docmerge_core)

enable_testing()

add_executable(test_foundation
    tests/unit/test_foundation.cpp
)
target_link_libraries(test_foundation PRIVATE docmerge_core)
add_test(NAME test_foundation COMMAND test_foundation)
