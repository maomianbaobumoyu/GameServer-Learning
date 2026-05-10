cmake_minimum_required(VERSION 2.8.12)
project(http_server)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")

include_directories(include)

file(GLOB SRC_FILES src/*.cpp)

add_executable(http_server ${SRC_FILES})

target_link_libraries(http_server pthread)