cmake_minimum_required(VERSION 3.10)
project(dp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without an explicit build type CMake builds without optimization.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(dp ${SOURCES})
target_compile_options(dp PRIVATE -Wall -Wextra)
