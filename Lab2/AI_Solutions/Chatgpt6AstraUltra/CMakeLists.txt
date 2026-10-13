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

# Stage 3 用 std::thread 平行跑 2 條退火鏈（C++ 標準函式庫；Linux 需要連結 pthread）
find_package(Threads REQUIRED)
target_link_libraries(cbi PRIVATE Threads::Threads)

option(CBI_BUILD_TESTS "Build structural and geometric regression tests" OFF)
if(CBI_BUILD_TESTS)
  enable_testing()
  add_executable(checkpoint_test tests/checkpoint_test.cpp src/tree.cpp src/problem.cpp)
  add_executable(diamond_test tests/diamond_test.cpp src/tree.cpp src/problem.cpp)
  add_executable(exact_placement_test tests/exact_placement_test.cpp src/exact_placement.cpp
    src/tree.cpp src/problem.cpp src/construct.cpp src/placement.cpp)
  foreach(test checkpoint_test diamond_test exact_placement_test)
    target_include_directories(${test} PRIVATE src)
    add_test(NAME ${test} COMMAND ${test})
  endforeach()
endif()
