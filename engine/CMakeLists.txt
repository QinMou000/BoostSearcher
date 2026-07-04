cmake_minimum_required(VERSION 3.14)
project(boost-searcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 词典路径通过编译定义传给 util.cpp，避免运行目录影响分词词典加载。
set(DICT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/dict" CACHE PATH "cppjieba 词典目录")

# 公共 include 目录：include/ 放项目头文件，third/ 放第三方库
set(ENGINE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third
)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# pthread（httplib 内部线程池需要，macOS 上自动链接，Linux 需显式查找）
find_package(Threads REQUIRED)

# 第一方核心实现集中到静态库，入口程序和测试只链接同一份实现，头文件只保留声明。
add_library(search_engine STATIC
    src/daemon.cpp
    src/index.cpp
    src/log.cpp
    src/searcher.cpp
    src/util.cpp
)
target_include_directories(search_engine PUBLIC ${ENGINE_INCLUDE_DIRS})
target_compile_definitions(search_engine PUBLIC DICT_DIR="${DICT_DIR}")
target_link_libraries(search_engine PUBLIC Threads::Threads)

# --- parser ---
add_executable(parser src/parser.cc)
target_link_libraries(parser PRIVATE search_engine)

# --- debug ---
add_executable(debug src/debug.cc)
target_link_libraries(debug PRIVATE search_engine)

# --- http_server ---
add_executable(http_server src/http_server.cc)
target_link_libraries(http_server PRIVATE search_engine)

# --- tests ---
enable_testing()

add_executable(searcher_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/searcher_tests.cc
)
target_link_libraries(searcher_tests PRIVATE search_engine)

add_test(NAME searcher_tests COMMAND searcher_tests)
