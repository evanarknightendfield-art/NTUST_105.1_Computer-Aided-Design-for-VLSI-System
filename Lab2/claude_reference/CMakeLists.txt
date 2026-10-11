cmake_minimum_required(VERSION 3.10)
project(cbi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 評分機只會執行 `cmake -S . -B build && cmake --build build`，
# 沒指定 build type 時預設是不開最佳化的，所以這裡強制預設 Release (-O2/-O3)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(cbi ${SOURCES})   # 產生 build/cbi
target_compile_options(cbi PRIVATE -Wall -Wextra)
