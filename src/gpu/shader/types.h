// Adapted from KytyPS5
// src/graphics/shader/shader.h at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "gpu/definitions.h"
#include "gpu/shader/bindings.h"

namespace kajps5::gpu {

enum class ShaderType { Unknown, Vertex, Pixel, Fetch, Compute };

enum class ShaderLaneMaskMode { NativeWave, PerInvocation };

namespace shader::recompiler::IR {
struct Program;
struct ResourceSnapshot;
}  // namespace shader::recompiler::IR

struct ShaderStageRuntime {
  std::shared_ptr<const shader::recompiler::IR::Program> program;
  std::shared_ptr<const shader::recompiler::IR::ResourceSnapshot> resources;

  [[nodiscard]] explicit operator bool() const {
    return program != nullptr && resources != nullptr;
  }
};

struct ShaderVertexInputInfo {
  static constexpr int RES_MAX = 32;

  ShaderBufferResource resources[RES_MAX];
  ShaderVertexDestination resources_dst[RES_MAX];
  int resource_fetch_components[RES_MAX] = {};
  ShaderVertexInputBuffer buffers[RES_MAX];
  ShaderStageRuntime stage;
  int resources_num = 0;
  int fetch_shader_reg = 0;
  int fetch_attrib_reg = 0;
  int fetch_buffer_reg = 0;
  int buffers_num = 0;
  int export_count = 0;
  std::uint32_t param_export_mask = 0;
  bool fetch_external = false;
  bool fetch_embedded = false;
};

struct ShaderComputeInputInfo {
  std::uint32_t threads_num[3] = {0, 0, 0};
  std::uint32_t dispatch_threads_num[3] = {0, 0, 0};
  bool group_id[3] = {false, false, false};
  bool dispatch_thread_dimensions = false;
  std::uint32_t wave_size = 64;
  int thread_ids_num = 0;
  int workgroup_register = 0;
  bool tg_size_en = false;
  ShaderStageRuntime stage;
};

struct ShaderPixelInputInfo {
  std::uint32_t interpolator_settings[32] = {0};
  std::uint32_t input_num = 0;
  std::uint32_t ps_system_input_base = 0;
  std::uint8_t target_output_mode[8] = {};
  std::array<Prospero::ColorComponentMapping, 8> target_export_mapping = {};
  std::uint32_t mrt_output_mask = 0;
  std::uint32_t descriptor_set = 0;
  std::uint32_t push_constant_offset = 0;
  bool ps_pos_x = false;
  bool ps_pos_y = false;
  bool ps_pos_xy = false;
  bool ps_pos_z = false;
  bool ps_pos_w = false;
  bool ps_front_face = false;
  bool ps_no_perspective = false;
  bool ps_pixel_kill_enable = false;
  bool ps_depth_export_enable = false;
  bool ps_sample_mask_export_enable = false;
  bool ps_sample_shading = false;
  bool ps_early_z = false;
  bool ps_execute_on_noop = false;
  ShaderStageRuntime stage;

  [[nodiscard]] bool HasPositionInput() const {
    return ps_pos_x || ps_pos_y || ps_pos_z || ps_pos_w;
  }
};

}  // namespace kajps5::gpu
