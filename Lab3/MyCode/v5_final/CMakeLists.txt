cmake_minimum_required(VERSION 3.10)
project(dp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without an explicit build type CMake builds without optimization.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)   # parallel SA (std::thread)
file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_library(dp_core STATIC ${SOURCES})
target_compile_options(dp_core PRIVATE -Wall -Wextra)
target_link_libraries(dp_core PUBLIC Threads::Threads)
add_executable(dp src/main.cpp)
target_compile_options(dp PRIVATE -Wall -Wextra)
target_link_libraries(dp PRIVATE dp_core)
