# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.24)

project(KajPS5 VERSION 0.1.0 LANGUAGES CXX)

option(KAJPS5_BUILD_TESTS "Build the KajPS5 tests." ON)
option(KAJPS5_REQUIRE_SPIRV_VAL "Require spirv-val when configuring tests." OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/DeployMsvcAsanRuntime.cmake)

find_program(KAJPS5_SPIRV_VAL_EXECUTABLE NAMES spirv-val)
if(KAJPS5_REQUIRE_SPIRV_VAL AND NOT KAJPS5_SPIRV_VAL_EXECUTABLE)
  message(FATAL_ERROR "KAJPS5_REQUIRE_SPIRV_VAL=ON requires spirv-val from SPIRV-Tools.")
endif()

add_subdirectory(src)

if(KAJPS5_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
