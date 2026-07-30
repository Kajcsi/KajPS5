add_executable(kajps5_project_info_test project_info_test.cpp)
target_link_libraries(kajps5_project_info_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_project_info_test)

add_test(NAME kajps5_project_info COMMAND kajps5_project_info_test)

add_executable(kajps5_guest_memory_test guest_memory_test.cpp)
target_link_libraries(kajps5_guest_memory_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_guest_memory_test)

add_test(NAME kajps5_guest_memory COMMAND kajps5_guest_memory_test)

add_executable(kajps5_elf_loader_test elf_loader_test.cpp)
target_link_libraries(kajps5_elf_loader_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_elf_loader_test)

add_test(NAME kajps5_elf_loader COMMAND kajps5_elf_loader_test)

add_executable(kajps5_self_loader_test self_loader_test.cpp)
target_link_libraries(kajps5_self_loader_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_self_loader_test)

add_test(NAME kajps5_self_loader COMMAND kajps5_self_loader_test)

add_executable(kajps5_launch_metadata_test launch_metadata_test.cpp)
target_link_libraries(kajps5_launch_metadata_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_launch_metadata_test)

add_test(NAME kajps5_launch_metadata COMMAND kajps5_launch_metadata_test)

add_executable(kajps5_lifecycle_plan_test lifecycle_plan_test.cpp)
target_link_libraries(kajps5_lifecycle_plan_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_lifecycle_plan_test)

add_test(NAME kajps5_lifecycle_plan COMMAND kajps5_lifecycle_plan_test)

add_executable(kajps5_module_plan_test module_plan_test.cpp)
target_link_libraries(kajps5_module_plan_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_module_plan_test)

add_test(NAME kajps5_module_plan COMMAND kajps5_module_plan_test)

add_executable(kajps5_module_export_registry_test
               module_export_registry_test.cpp)
target_link_libraries(kajps5_module_export_registry_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_module_export_registry_test)

add_test(NAME kajps5_module_export_registry
         COMMAND kajps5_module_export_registry_test)

add_executable(kajps5_layered_import_resolver_test
               layered_import_resolver_test.cpp)
target_link_libraries(kajps5_layered_import_resolver_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_layered_import_resolver_test)

add_test(NAME kajps5_layered_import_resolver
         COMMAND kajps5_layered_import_resolver_test)

add_executable(kajps5_elf_dynamic_test elf_dynamic_test.cpp)
target_link_libraries(kajps5_elf_dynamic_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_elf_dynamic_test)

add_test(NAME kajps5_elf_dynamic COMMAND kajps5_elf_dynamic_test)

add_executable(kajps5_elf_sce_dynamic_test elf_sce_dynamic_test.cpp)
target_link_libraries(kajps5_elf_sce_dynamic_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_elf_sce_dynamic_test)

add_test(NAME kajps5_elf_sce_dynamic COMMAND kajps5_elf_sce_dynamic_test)

add_executable(kajps5_elf_relocation_test elf_relocation_test.cpp)
target_link_libraries(kajps5_elf_relocation_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_elf_relocation_test)

add_test(NAME kajps5_elf_relocation COMMAND kajps5_elf_relocation_test)

add_executable(kajps5_relocation_kinds_test relocation_kinds_test.cpp)
target_link_libraries(kajps5_relocation_kinds_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_relocation_kinds_test)

add_test(NAME kajps5_relocation_kinds COMMAND kajps5_relocation_kinds_test)

add_executable(kajps5_elf_symbol_test elf_symbol_test.cpp)
target_link_libraries(kajps5_elf_symbol_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_elf_symbol_test)

add_test(NAME kajps5_elf_symbol COMMAND kajps5_elf_symbol_test)

add_executable(kajps5_native_leaf_executor_test native_leaf_executor_test.cpp)
target_link_libraries(kajps5_native_leaf_executor_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_native_leaf_executor_test)

add_test(NAME kajps5_native_leaf_executor
         COMMAND kajps5_native_leaf_executor_test)

add_executable(kajps5_hle_import_registry_test hle_import_registry_test.cpp)
target_link_libraries(kajps5_hle_import_registry_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_import_registry_test)

add_test(NAME kajps5_hle_import_registry
         COMMAND kajps5_hle_import_registry_test)

add_executable(kajps5_hle_call_context_test hle_call_context_test.cpp)
target_link_libraries(kajps5_hle_call_context_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_call_context_test)

add_test(NAME kajps5_hle_call_context COMMAND kajps5_hle_call_context_test)

add_executable(kajps5_hle_data_symbols_test hle_data_symbols_test.cpp)
target_link_libraries(kajps5_hle_data_symbols_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_data_symbols_test)

add_test(NAME kajps5_hle_data_symbols COMMAND kajps5_hle_data_symbols_test)

add_executable(kajps5_hle_export_registry_test hle_export_registry_test.cpp)
target_link_libraries(kajps5_hle_export_registry_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_export_registry_test)

add_test(NAME kajps5_hle_export_registry
         COMMAND kajps5_hle_export_registry_test)

add_executable(kajps5_hle_kernel_clock_exports_test
               hle_kernel_clock_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_clock_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_clock_exports_test)

add_test(NAME kajps5_hle_kernel_clock_exports
         COMMAND kajps5_hle_kernel_clock_exports_test)

add_executable(kajps5_hle_kernel_event_queue_exports_test
               hle_kernel_event_queue_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_event_queue_exports_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_event_queue_exports_test)

add_test(NAME kajps5_hle_kernel_event_queue_exports
         COMMAND kajps5_hle_kernel_event_queue_exports_test)

add_executable(kajps5_hle_kernel_event_flag_exports_test
               hle_kernel_event_flag_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_event_flag_exports_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_event_flag_exports_test)

add_test(NAME kajps5_hle_kernel_event_flag_exports
         COMMAND kajps5_hle_kernel_event_flag_exports_test)

add_executable(kajps5_hle_kernel_file_exports_test
               hle_kernel_file_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_file_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_file_exports_test)

add_test(NAME kajps5_hle_kernel_file_exports
         COMMAND kajps5_hle_kernel_file_exports_test)

add_executable(kajps5_hle_kernel_exports_test hle_kernel_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_exports_test)

add_test(NAME kajps5_hle_kernel_exports
         COMMAND kajps5_hle_kernel_exports_test)

add_executable(kajps5_hle_kernel_memory_exports_test
               hle_kernel_memory_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_memory_exports_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_memory_exports_test)

add_test(NAME kajps5_hle_kernel_memory_exports
         COMMAND kajps5_hle_kernel_memory_exports_test)

add_executable(kajps5_hle_kernel_pthread_exports_test
               hle_kernel_pthread_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_pthread_exports_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_pthread_exports_test)

add_test(NAME kajps5_hle_kernel_pthread_exports
         COMMAND kajps5_hle_kernel_pthread_exports_test)

add_executable(kajps5_hle_kernel_semaphore_exports_test
               hle_kernel_semaphore_exports_test.cpp)
target_link_libraries(kajps5_hle_kernel_semaphore_exports_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_kernel_semaphore_exports_test)

add_test(NAME kajps5_hle_kernel_semaphore_exports
         COMMAND kajps5_hle_kernel_semaphore_exports_test)

add_executable(kajps5_hle_libc_exports_test hle_libc_exports_test.cpp)
target_link_libraries(kajps5_hle_libc_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_libc_exports_test)

add_test(NAME kajps5_hle_libc_exports COMMAND kajps5_hle_libc_exports_test)

add_executable(kajps5_hle_libc_lifecycle_test hle_libc_lifecycle_test.cpp)
target_link_libraries(kajps5_hle_libc_lifecycle_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_libc_lifecycle_test)

add_test(NAME kajps5_hle_libc_lifecycle
         COMMAND kajps5_hle_libc_lifecycle_test)

add_executable(kajps5_hle_libc_heap_test hle_libc_heap_test.cpp)
target_link_libraries(kajps5_hle_libc_heap_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_libc_heap_test)

add_test(NAME kajps5_hle_libc_heap COMMAND kajps5_hle_libc_heap_test)

add_executable(kajps5_hle_libc_memory_math_test
               hle_libc_memory_math_test.cpp)
target_link_libraries(kajps5_hle_libc_memory_math_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_libc_memory_math_test)

add_test(NAME kajps5_hle_libc_memory_math
         COMMAND kajps5_hle_libc_memory_math_test)

add_executable(kajps5_hle_import_link_test hle_import_link_test.cpp)
target_link_libraries(kajps5_hle_import_link_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_import_link_test)

add_test(NAME kajps5_hle_import_link COMMAND kajps5_hle_import_link_test)

add_executable(kajps5_hle_import_coverage_test hle_import_coverage_test.cpp)
target_link_libraries(kajps5_hle_import_coverage_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_import_coverage_test)

add_test(NAME kajps5_hle_import_coverage
         COMMAND kajps5_hle_import_coverage_test)

add_executable(kajps5_hle_json_exports_test hle_json_exports_test.cpp)
target_link_libraries(kajps5_hle_json_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_json_exports_test)

add_test(NAME kajps5_hle_json_exports COMMAND kajps5_hle_json_exports_test)

add_executable(kajps5_relocation_trace_test relocation_trace_test.cpp)
target_link_libraries(kajps5_relocation_trace_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_relocation_trace_test)

add_test(NAME kajps5_relocation_trace COMMAND kajps5_relocation_trace_test)

add_executable(kajps5_hle_public_guest_test hle_public_guest_test.cpp)
target_link_libraries(kajps5_hle_public_guest_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_public_guest_test)

add_test(NAME kajps5_hle_public_guest COMMAND kajps5_hle_public_guest_test)

add_executable(kajps5_kernel_event_flag_test kernel_event_flag_test.cpp)
target_link_libraries(kajps5_kernel_event_flag_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_event_flag_test)

add_test(NAME kajps5_kernel_event_flag COMMAND kajps5_kernel_event_flag_test)

add_executable(kajps5_kernel_scheduler_test kernel_scheduler_test.cpp)
target_link_libraries(kajps5_kernel_scheduler_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_scheduler_test)

add_test(NAME kajps5_kernel_scheduler COMMAND kajps5_kernel_scheduler_test)

add_executable(kajps5_kernel_pthread_test kernel_pthread_test.cpp)
target_link_libraries(kajps5_kernel_pthread_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_pthread_test)

add_test(NAME kajps5_kernel_pthread COMMAND kajps5_kernel_pthread_test)

add_executable(kajps5_kernel_event_wait_test kernel_event_wait_test.cpp)
target_link_libraries(kajps5_kernel_event_wait_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_event_wait_test)

add_test(NAME kajps5_kernel_event_wait COMMAND kajps5_kernel_event_wait_test)

add_executable(kajps5_kernel_semaphore_test kernel_semaphore_test.cpp)
target_link_libraries(kajps5_kernel_semaphore_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_semaphore_test)

add_test(NAME kajps5_kernel_semaphore COMMAND kajps5_kernel_semaphore_test)

add_executable(kajps5_kernel_clock_test kernel_clock_test.cpp)
target_link_libraries(kajps5_kernel_clock_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_clock_test)

add_test(NAME kajps5_kernel_clock COMMAND kajps5_kernel_clock_test)

add_executable(kajps5_kernel_direct_memory_test kernel_direct_memory_test.cpp)
target_link_libraries(kajps5_kernel_direct_memory_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_direct_memory_test)

add_test(NAME kajps5_kernel_direct_memory
         COMMAND kajps5_kernel_direct_memory_test)

add_executable(kajps5_kernel_file_test kernel_file_test.cpp)
target_link_libraries(kajps5_kernel_file_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_file_test)

add_test(NAME kajps5_kernel_file COMMAND kajps5_kernel_file_test)

add_executable(kajps5_kernel_event_queue_test kernel_event_queue_test.cpp)
target_link_libraries(kajps5_kernel_event_queue_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_event_queue_test)

add_test(NAME kajps5_kernel_event_queue COMMAND kajps5_kernel_event_queue_test)
