cmake_minimum_required(VERSION 3.5)

project(nano-trigger CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast")

add_executable(nano-trigger src/nano-trigger.cpp)

target_link_arduino_libraries(nano-trigger PRIVATE core)
target_enable_arduino_upload(nano-trigger)

