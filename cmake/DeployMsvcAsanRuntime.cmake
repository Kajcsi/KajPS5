# Copyright (C) 2026 KajPS5 contributors
# SPDX-License-Identifier: GPL-2.0-or-later

set(_kajps5_all_cxx_flags
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_CXX_FLAGS_MINSIZEREL}")

set(KAJPS5_MSVC_ASAN_ENABLED OFF)
if(MSVC AND _kajps5_all_cxx_flags MATCHES "(^|[ ;])/fsanitize=address([ ;]|$)")
  set(KAJPS5_MSVC_ASAN_ENABLED ON)

  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_kajps5_asan_runtime_name clang_rt.asan_dynamic-x86_64.dll)
  else()
    set(_kajps5_asan_runtime_name clang_rt.asan_dynamic-i386.dll)
  endif()

  get_filename_component(_kajps5_compiler_directory
                         "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_file(KAJPS5_MSVC_ASAN_RUNTIME
            NAMES "${_kajps5_asan_runtime_name}"
            HINTS "${_kajps5_compiler_directory}"
            NO_DEFAULT_PATH
            NO_CACHE)

  if(NOT KAJPS5_MSVC_ASAN_RUNTIME)
    file(GLOB _kajps5_asan_runtime_candidates
         LIST_DIRECTORIES false
         "${_kajps5_compiler_directory}/../lib/clang/*/lib/windows/${_kajps5_asan_runtime_name}")
    list(SORT _kajps5_asan_runtime_candidates
         COMPARE NATURAL
         ORDER DESCENDING)
    list(LENGTH _kajps5_asan_runtime_candidates _kajps5_candidate_count)
    if(_kajps5_candidate_count GREATER 0)
      list(GET _kajps5_asan_runtime_candidates 0
           KAJPS5_MSVC_ASAN_RUNTIME)
    endif()
  endif()

  if(NOT KAJPS5_MSVC_ASAN_RUNTIME)
    message(FATAL_ERROR
            "MSVC AddressSanitizer is enabled, but ${_kajps5_asan_runtime_name} was not found.")
  endif()

  message(STATUS
          "KajPS5 will deploy the MSVC AddressSanitizer runtime: ${KAJPS5_MSVC_ASAN_RUNTIME}")
endif()

function(kajps5_deploy_msvc_asan_runtime target)
  if(NOT KAJPS5_MSVC_ASAN_ENABLED)
    return()
  endif()

  add_custom_command(
    TARGET "${target}"
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${KAJPS5_MSVC_ASAN_RUNTIME}"
            "$<TARGET_FILE_DIR:${target}>"
    COMMENT "Deploying the MSVC AddressSanitizer runtime for ${target}"
    VERBATIM)
endfunction()
