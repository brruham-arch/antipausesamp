cmake_minimum_required(VERSION 3.10)
project(antipause)

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fno-rtti -fno-exceptions")

add_library(antipause SHARED jni/main.cpp)
target_link_libraries(antipause dl log)
