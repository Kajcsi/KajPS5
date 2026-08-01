add_library(kajps5_core STATIC
  core/memory/guest_memory.cpp
  core/memory/shared_memory_backing.cpp
  core/project_info.cpp
  cpu/host_executable_buffer.cpp
  cpu/native_guest_executor.cpp
  cpu/native_guest_process_launcher.cpp
  cpu/native_guest_thread_runner.cpp
  cpu/native_hle_import_table.cpp
  cpu/native_hle_trampoline.cpp
  cpu/native_leaf_executor.cpp
  hle/call_context.cpp
  hle/data_symbols.cpp
  hle/export_registry.cpp
  hle/import_coverage.cpp
  hle/import_registry.cpp
  hle/json_exports.cpp
  hle/kernel_clock_exports.cpp
  hle/kernel_event_queue_exports.cpp
  hle/kernel_event_flag_exports.cpp
  hle/kernel_exports.cpp
  hle/kernel_file_exports.cpp
  hle/kernel_memory_exports.cpp
  hle/kernel_pthread_exports.cpp
  hle/kernel_semaphore_exports.cpp
  hle/libc_exports.cpp
  hle/libc_format.cpp
  hle/libc_thread_exports.cpp
  kernel/clock.cpp
  kernel/cxa_guard.cpp
  kernel/direct_memory.cpp
  kernel/event_queue.cpp
  kernel/event_flag.cpp
  kernel/file.cpp
  kernel/guest_scheduler.cpp
  kernel/handle_table.cpp
  kernel/json_value.cpp
  kernel/libc_heap.cpp
  kernel/process_lifecycle.cpp
  kernel/pthread.cpp
  kernel/semaphore.cpp
  loader/elf.cpp
  loader/elf_trace.cpp
  loader/launch_metadata.cpp
  loader/layered_import_resolver.cpp
  loader/lifecycle_plan.cpp
  loader/module_export_registry.cpp
  loader/module_plan.cpp
  loader/relocation_trace.cpp
  loader/relocator.cpp
  loader/sce_symbol.cpp
  loader/static_tls_layout.cpp
)

target_include_directories(kajps5_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(kajps5_core PUBLIC
  KAJPS5_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
  target_compile_options(kajps5_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(kajps5_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kajps5 app/main.cpp)
target_link_libraries(kajps5 PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5)
