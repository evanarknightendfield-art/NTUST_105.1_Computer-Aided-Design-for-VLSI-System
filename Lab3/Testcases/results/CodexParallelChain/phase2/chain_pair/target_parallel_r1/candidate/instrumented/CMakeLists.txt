cmake_minimum_required(VERSION 3.10)
project(CodexCapDP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without an explicit build type CMake builds without optimization.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)   # v4: parallel SA (std::thread)
file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_library(dp_core STATIC ${SOURCES})
target_compile_options(dp_core PRIVATE -Wall -Wextra)
target_link_libraries(dp_core PUBLIC Threads::Threads)
add_executable(dp src/main.cpp)
target_compile_options(dp PRIVATE -Wall -Wextra)
target_link_libraries(dp PRIVATE dp_core)

include(CTest)
if(BUILD_TESTING)
  foreach(name window multirow optimizer chain target_window parallel_sa pool global capsearch cap_windows guard)
    add_executable(${name}_test tests/${name}_test.cpp)
    target_link_libraries(${name}_test PRIVATE dp_core)
    # Several inherited exhaustive tests use assert, including Release builds.
    target_compile_options(${name}_test PRIVATE -UNDEBUG)
    add_test(NAME ${name} COMMAND ${name}_test)
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
  endforeach()
endif()
