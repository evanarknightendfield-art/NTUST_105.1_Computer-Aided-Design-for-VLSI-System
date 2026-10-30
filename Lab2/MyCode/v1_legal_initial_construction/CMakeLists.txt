cmake_minimum_required(VERSION 3.10)
project(cbi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 評分機只會跑 `cmake -S . -B build && cmake --build build`，不會指定 build type，
# 不設定的話就是沒開最佳化的 debug 速度。
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 之後把程式拆成多個 .cpp 也會自動抓到（新增檔案後要重新跑一次 cmake -S . -B build）
file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(cbi ${SOURCES})   # 執行檔名稱必須是 cbi → build/cbi
target_compile_options(cbi PRIVATE -Wall -Wextra)
