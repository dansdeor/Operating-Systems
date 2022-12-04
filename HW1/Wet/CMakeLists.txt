cmake_minimum_required(VERSION 3.16)
project(skeleton_smash)

set(CMAKE_CXX_STANDARD 14)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

add_executable(skeleton_smash smash.cpp Commands.cpp signals.cpp)