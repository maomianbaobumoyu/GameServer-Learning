cmake_minimum_required(VERSION 2.8)
project(memory_pool)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")
include_directories(include)
file(GLOB SRC_FILES src/*.cpp)
add_executable(memory_pool ${SRC_FILES})
target_link_libraries(memory_pool pthread)