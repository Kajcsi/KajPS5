cmake_minimum_required(VERSION 3.24)

project(KajPS5 VERSION 0.1.0 LANGUAGES CXX)

option(KAJPS5_BUILD_TESTS "Build the KajPS5 tests." ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)

if(KAJPS5_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
