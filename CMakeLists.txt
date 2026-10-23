cmake_minimum_required(VERSION 3.14)
project(ast_tool LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)           
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------- 1. Tree-sitter core ----------
add_library(tree_sitter STATIC
    third_party/tree-sitter/lib/src/lib.c
)
target_include_directories(tree_sitter
    PUBLIC  third_party/tree-sitter/lib/include
    PRIVATE third_party/tree-sitter/lib/src
)

# ---------- 2. C++ grammar ----------
add_library(tree_sitter_cpp STATIC
    third_party/tree-sitter-cpp/src/parser.c
    third_party/tree-sitter-cpp/src/scanner.c
)
target_include_directories(tree_sitter_cpp
    PRIVATE third_party/tree-sitter-cpp/src
)

# ---------- Actual tool ----------
add_executable(ast_tool src/main.cpp)
target_link_libraries(ast_tool PRIVATE tree_sitter tree_sitter_cpp)