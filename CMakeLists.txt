cmake_minimum_required(VERSION 3.10)
project(rawtools CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(third_party/qmgc)
add_subdirectory(third_party/qmgd)
add_subdirectory(tools/common)
add_subdirectory(tools/zip2qmg)
add_subdirectory(tools/qmg2zip)
