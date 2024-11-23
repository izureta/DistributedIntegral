cmake_minimum_required(VERSION 3.10)
project(DistributedIntegral)
set(CMAKE_CXX_STANDARD 23)

add_subdirectory(master)
add_subdirectory(worker)
