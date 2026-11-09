cmake_minimum_required(VERSION 3.14)
project(docmerge VERSION 0.1.0 LANGUAGES CXX)

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
    src/pattern/date.cpp
    src/pattern/version.cpp
    src/pattern/pattern.cpp
    src/config/config.cpp
    src/doc/line.cpp
    src/doc/doc_parse.cpp
    src/doc/doc_write.cpp
    src/merge/diff.cpp
    src/merge/conflict.cpp
    src/merge/diff3.cpp
    src/merge/ordering.cpp
    src/merge/tree_merge.cpp
    src/merge/resolve.cpp
    src/verify/invariants.cpp
    src/cli/args.cpp
    src/cli/init_infer.cpp
    src/cli/commands.cpp
)
target_include_directories(docmerge_core PUBLIC src)

add_executable(docmerge
    src/main.cpp
)
target_link_libraries(docmerge PRIVATE docmerge_core)

include(GNUInstallDirs)
install(TARGETS docmerge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

enable_testing()

add_executable(test_foundation
    tests/unit/test_foundation.cpp
)
target_link_libraries(test_foundation PRIVATE docmerge_core)
add_test(NAME test_foundation COMMAND test_foundation)

add_executable(test_parsing_and_config
    tests/unit/test_parsing_and_config.cpp
)
target_compile_definitions(test_parsing_and_config PRIVATE
    DOCMERGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_parsing_and_config PRIVATE docmerge_core)
add_test(NAME test_parsing_and_config COMMAND test_parsing_and_config)

add_executable(test_tree_merge_and_cli
    tests/unit/test_tree_merge_and_cli.cpp
)
target_compile_definitions(test_tree_merge_and_cli PRIVATE
    DOCMERGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_tree_merge_and_cli PRIVATE docmerge_core)
add_test(NAME test_tree_merge_and_cli COMMAND test_tree_merge_and_cli)

add_executable(test_real_oss_and_git
    tests/unit/test_real_oss_and_git.cpp
)
target_compile_definitions(test_real_oss_and_git PRIVATE
    DOCMERGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    DOCMERGE_BINARY_PATH="$<TARGET_FILE:docmerge>"
)
target_link_libraries(test_real_oss_and_git PRIVATE docmerge_core)
add_test(NAME test_real_oss_and_git COMMAND test_real_oss_and_git)
