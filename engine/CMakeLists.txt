cmake_minimum_required(VERSION 3.14)
project(boost-searcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 词典路径（绝对路径，通过编译定义传给 util.hpp 的 DICT_DIR 宏）
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

# --- parser ---
add_executable(parser src/parser.cc)
target_include_directories(parser PRIVATE ${ENGINE_INCLUDE_DIRS})
target_compile_definitions(parser PRIVATE DICT_DIR="${DICT_DIR}")
target_link_libraries(parser PRIVATE Threads::Threads)

# --- debug ---
add_executable(debug src/debug.cc)
target_include_directories(debug PRIVATE ${ENGINE_INCLUDE_DIRS})
target_compile_definitions(debug PRIVATE DICT_DIR="${DICT_DIR}")

# --- http_server ---
add_executable(http_server src/http_server.cc)
target_include_directories(http_server PRIVATE ${ENGINE_INCLUDE_DIRS})
target_compile_definitions(http_server PRIVATE DICT_DIR="${DICT_DIR}")
target_link_libraries(http_server PRIVATE Threads::Threads)
