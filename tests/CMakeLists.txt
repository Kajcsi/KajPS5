find_package(Threads REQUIRED)

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

add_executable(kajps5_static_tls_layout_test static_tls_layout_test.cpp)
target_link_libraries(kajps5_static_tls_layout_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_static_tls_layout_test)

add_test(NAME kajps5_static_tls_layout COMMAND kajps5_static_tls_layout_test)

add_executable(kajps5_title_session_test title_session_test.cpp)
target_link_libraries(kajps5_title_session_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_title_session_test)

add_test(NAME kajps5_title_session COMMAND kajps5_title_session_test)

add_executable(kajps5_title_loader_test title_loader_test.cpp)
target_link_libraries(kajps5_title_loader_test PRIVATE kajps5_core)
add_dependencies(kajps5_title_loader_test kajps5)
kajps5_deploy_msvc_asan_runtime(kajps5_title_loader_test)

add_test(NAME kajps5_title_loader
  COMMAND kajps5_title_loader_test $<TARGET_FILE:kajps5>)

add_executable(kajps5_module_plan_test module_plan_test.cpp)
target_link_libraries(kajps5_module_plan_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_module_plan_test)

add_test(NAME kajps5_module_plan COMMAND kajps5_module_plan_test)

add_executable(kajps5_module_loader_test module_loader_test.cpp)
target_link_libraries(kajps5_module_loader_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_module_loader_test)

add_test(NAME kajps5_module_loader COMMAND kajps5_module_loader_test)

add_executable(kajps5_module_runtime_test module_runtime_test.cpp)
target_link_libraries(kajps5_module_runtime_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_module_runtime_test)

add_test(NAME kajps5_module_runtime COMMAND kajps5_module_runtime_test)

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

add_executable(kajps5_native_guest_executor_test
               native_guest_executor_test.cpp)
target_link_libraries(kajps5_native_guest_executor_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_native_guest_executor_test)

add_test(NAME kajps5_native_guest_executor
         COMMAND kajps5_native_guest_executor_test)

add_executable(kajps5_native_guest_process_launcher_test
               native_guest_process_launcher_test.cpp)
target_link_libraries(kajps5_native_guest_process_launcher_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_native_guest_process_launcher_test)

add_test(NAME kajps5_native_guest_process_launcher
         COMMAND kajps5_native_guest_process_launcher_test)

add_executable(kajps5_native_guest_thread_runner_test
               native_guest_thread_runner_test.cpp)
target_link_libraries(kajps5_native_guest_thread_runner_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_native_guest_thread_runner_test)

add_test(NAME kajps5_native_guest_thread_runner
         COMMAND kajps5_native_guest_thread_runner_test)

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

add_executable(kajps5_hle_libc_format_test hle_libc_format_test.cpp)
target_link_libraries(kajps5_hle_libc_format_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_libc_format_test)

add_test(NAME kajps5_hle_libc_format COMMAND kajps5_hle_libc_format_test)

add_executable(kajps5_hle_libc_thread_exports_test
               hle_libc_thread_exports_test.cpp)
target_link_libraries(kajps5_hle_libc_thread_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_libc_thread_exports_test)

add_test(NAME kajps5_hle_libc_thread_exports
         COMMAND kajps5_hle_libc_thread_exports_test)

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

add_executable(kajps5_hle_ampr_exports_test hle_ampr_exports_test.cpp)
target_link_libraries(kajps5_hle_ampr_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_ampr_exports_test)

add_test(NAME kajps5_hle_ampr_exports COMMAND kajps5_hle_ampr_exports_test)

add_executable(kajps5_hle_agc_exports_test hle_agc_exports_test.cpp)
target_link_libraries(kajps5_hle_agc_exports_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_hle_agc_exports_test)

add_test(NAME kajps5_hle_agc_exports COMMAND kajps5_hle_agc_exports_test)

add_executable(kajps5_gpu_command_processor_test
               gpu_command_processor_test.cpp)
target_link_libraries(kajps5_gpu_command_processor_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_command_processor_test)

add_test(NAME kajps5_gpu_command_processor
         COMMAND kajps5_gpu_command_processor_test)

add_executable(kajps5_gpu_resource_coherence_test
               gpu_resource_coherence_test.cpp)
target_link_libraries(kajps5_gpu_resource_coherence_test
                      PRIVATE kajps5_core Threads::Threads)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_resource_coherence_test)

add_test(NAME kajps5_gpu_resource_coherence
         COMMAND kajps5_gpu_resource_coherence_test)

add_executable(kajps5_gpu_vulkan_device_test gpu_vulkan_device_test.cpp)
target_link_libraries(kajps5_gpu_vulkan_device_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_vulkan_device_test)

add_test(NAME kajps5_gpu_vulkan_device
         COMMAND kajps5_gpu_vulkan_device_test)

add_executable(kajps5_gpu_vulkan_execution_test gpu_vulkan_execution_test.cpp)
target_link_libraries(kajps5_gpu_vulkan_execution_test PRIVATE kajps5_core
                                                               Threads::Threads)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_vulkan_execution_test)

add_test(NAME kajps5_gpu_vulkan_execution
         COMMAND kajps5_gpu_vulkan_execution_test)

add_executable(kajps5_gpu_vulkan_compute_smoke_test
               gpu_vulkan_compute_smoke_test.cpp)
target_link_libraries(kajps5_gpu_vulkan_compute_smoke_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_vulkan_compute_smoke_test)

add_test(NAME kajps5_gpu_vulkan_compute_smoke
         COMMAND kajps5_gpu_vulkan_compute_smoke_test)
set_tests_properties(kajps5_gpu_vulkan_compute_smoke
                     PROPERTIES SKIP_RETURN_CODE 77)

add_executable(kajps5_gpu_vulkan_smoke_test gpu_vulkan_smoke_test.cpp)
target_link_libraries(kajps5_gpu_vulkan_smoke_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_vulkan_smoke_test)

add_test(NAME kajps5_gpu_vulkan_smoke
         COMMAND kajps5_gpu_vulkan_smoke_test)
set_tests_properties(kajps5_gpu_vulkan_smoke PROPERTIES SKIP_RETURN_CODE 77)

add_executable(kajps5_gpu_submission_queue_test
               gpu_submission_queue_test.cpp)
target_link_libraries(kajps5_gpu_submission_queue_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_submission_queue_test)

add_test(NAME kajps5_gpu_submission_queue
         COMMAND kajps5_gpu_submission_queue_test)

add_executable(kajps5_gpu_shader_runtime_test gpu_shader_runtime_test.cpp)
target_link_libraries(kajps5_gpu_shader_runtime_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_shader_runtime_test)

add_test(NAME kajps5_gpu_shader_runtime
         COMMAND kajps5_gpu_shader_runtime_test)

add_executable(kajps5_gpu_shader_binding_test gpu_shader_binding_test.cpp)
target_link_libraries(kajps5_gpu_shader_binding_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_shader_binding_test)

add_test(NAME kajps5_gpu_shader_binding
         COMMAND kajps5_gpu_shader_binding_test)

add_executable(kajps5_gpu_spirv_builder_test gpu_spirv_builder_test.cpp)
target_link_libraries(kajps5_gpu_spirv_builder_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_spirv_builder_test)

add_test(NAME kajps5_gpu_spirv_builder COMMAND kajps5_gpu_spirv_builder_test)

add_executable(kajps5_gpu_shader_decoder_test gpu_shader_decoder_test.cpp)
target_link_libraries(kajps5_gpu_shader_decoder_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_shader_decoder_test)

add_test(NAME kajps5_gpu_shader_decoder COMMAND kajps5_gpu_shader_decoder_test)

add_executable(kajps5_gpu_shader_cfg_test gpu_shader_cfg_test.cpp)
target_link_libraries(kajps5_gpu_shader_cfg_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_shader_cfg_test)

add_test(NAME kajps5_gpu_shader_cfg COMMAND kajps5_gpu_shader_cfg_test)

add_executable(kajps5_gpu_shader_ir_test gpu_shader_ir_test.cpp)
target_link_libraries(kajps5_gpu_shader_ir_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_shader_ir_test)

add_test(NAME kajps5_gpu_shader_ir COMMAND kajps5_gpu_shader_ir_test)

add_executable(kajps5_gpu_shader_emitter_test gpu_shader_emitter_test.cpp)
target_link_libraries(kajps5_gpu_shader_emitter_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_gpu_shader_emitter_test)

set(KAJPS5_GPU_SHADER_EMITTER_COMPUTE_SPIRV
    "${CMAKE_CURRENT_BINARY_DIR}/kajps5_gpu_shader_emitter_compute.spv")
set(KAJPS5_GPU_SHADER_EMITTER_VERTEX_SPIRV
    "${CMAKE_CURRENT_BINARY_DIR}/kajps5_gpu_shader_emitter_vertex.spv")
set(KAJPS5_GPU_SHADER_EMITTER_PIXEL_SPIRV
    "${CMAKE_CURRENT_BINARY_DIR}/kajps5_gpu_shader_emitter_pixel.spv")
set(KAJPS5_GPU_SHADER_EMITTER_RESOURCE_SPIRV
    "${CMAKE_CURRENT_BINARY_DIR}/kajps5_gpu_shader_emitter_resource.spv")
set(KAJPS5_GPU_SHADER_EMITTER_WAVE32_SPIRV
    "${CMAKE_CURRENT_BINARY_DIR}/kajps5_gpu_shader_emitter_wave32.spv")
set(KAJPS5_GPU_SHADER_EMITTER_FLOAT_SPIRV
    "${CMAKE_CURRENT_BINARY_DIR}/kajps5_gpu_shader_emitter_float.spv")
set(KAJPS5_GPU_SHADER_EMITTER_PIXEL_COLLISION_SPIRV
    "${CMAKE_CURRENT_BINARY_DIR}/kajps5_gpu_shader_emitter_pixel_collision.spv")
add_test(NAME kajps5_gpu_shader_emitter
         COMMAND $<TARGET_FILE:kajps5_gpu_shader_emitter_test>
                 --output-dir "${CMAKE_CURRENT_BINARY_DIR}")

if(KAJPS5_SPIRV_VAL_EXECUTABLE)
  add_test(NAME kajps5_gpu_shader_emitter_spirv_val_compute
           COMMAND "${KAJPS5_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.2
                   "${KAJPS5_GPU_SHADER_EMITTER_COMPUTE_SPIRV}")
  set_tests_properties(kajps5_gpu_shader_emitter_spirv_val_compute
                       PROPERTIES DEPENDS kajps5_gpu_shader_emitter)
  add_test(NAME kajps5_gpu_shader_emitter_spirv_val_vertex
           COMMAND "${KAJPS5_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.2
                   "${KAJPS5_GPU_SHADER_EMITTER_VERTEX_SPIRV}")
  set_tests_properties(kajps5_gpu_shader_emitter_spirv_val_vertex
                       PROPERTIES DEPENDS kajps5_gpu_shader_emitter)
  add_test(NAME kajps5_gpu_shader_emitter_spirv_val_pixel
           COMMAND "${KAJPS5_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.2
                   "${KAJPS5_GPU_SHADER_EMITTER_PIXEL_SPIRV}")
  set_tests_properties(kajps5_gpu_shader_emitter_spirv_val_pixel
                       PROPERTIES DEPENDS kajps5_gpu_shader_emitter)
  add_test(NAME kajps5_gpu_shader_emitter_spirv_val_resource
           COMMAND "${KAJPS5_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.2
                   "${KAJPS5_GPU_SHADER_EMITTER_RESOURCE_SPIRV}")
  set_tests_properties(kajps5_gpu_shader_emitter_spirv_val_resource
                       PROPERTIES DEPENDS kajps5_gpu_shader_emitter)
  add_test(NAME kajps5_gpu_shader_emitter_spirv_val_wave32
           COMMAND "${KAJPS5_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.2
                   "${KAJPS5_GPU_SHADER_EMITTER_WAVE32_SPIRV}")
  set_tests_properties(kajps5_gpu_shader_emitter_spirv_val_wave32
                       PROPERTIES DEPENDS kajps5_gpu_shader_emitter)
  add_test(NAME kajps5_gpu_shader_emitter_spirv_val_float
           COMMAND "${KAJPS5_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.2
                   "${KAJPS5_GPU_SHADER_EMITTER_FLOAT_SPIRV}")
  set_tests_properties(kajps5_gpu_shader_emitter_spirv_val_float
                       PROPERTIES DEPENDS kajps5_gpu_shader_emitter)
  add_test(NAME kajps5_gpu_shader_emitter_spirv_val_pixel_collision
           COMMAND "${KAJPS5_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.2
                   "${KAJPS5_GPU_SHADER_EMITTER_PIXEL_COLLISION_SPIRV}")
  set_tests_properties(kajps5_gpu_shader_emitter_spirv_val_pixel_collision
                       PROPERTIES DEPENDS kajps5_gpu_shader_emitter)
endif()

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

add_executable(kajps5_kernel_ampr_command_buffer_test
               kernel_ampr_command_buffer_test.cpp)
target_link_libraries(kajps5_kernel_ampr_command_buffer_test
                      PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_ampr_command_buffer_test)

add_test(NAME kajps5_kernel_ampr_command_buffer
         COMMAND kajps5_kernel_ampr_command_buffer_test)

add_executable(kajps5_kernel_file_test kernel_file_test.cpp)
target_link_libraries(kajps5_kernel_file_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_file_test)

add_test(NAME kajps5_kernel_file COMMAND kajps5_kernel_file_test)

add_executable(kajps5_host_vfs_test host_vfs_test.cpp)
target_link_libraries(kajps5_host_vfs_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_host_vfs_test)

add_test(NAME kajps5_host_vfs COMMAND kajps5_host_vfs_test)

add_executable(kajps5_kernel_event_queue_test kernel_event_queue_test.cpp)
target_link_libraries(kajps5_kernel_event_queue_test PRIVATE kajps5_core)
kajps5_deploy_msvc_asan_runtime(kajps5_kernel_event_queue_test)

add_test(NAME kajps5_kernel_event_queue COMMAND kajps5_kernel_event_queue_test)
