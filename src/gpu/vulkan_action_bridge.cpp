// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan_action_bridge.h"

#include <algorithm>
#include <utility>

#include "gpu/runtime.h"

namespace kajps5::gpu {

VulkanActionBridge::VulkanActionBridge(GpuRuntime &runtime,
                                       GpuSubmissionSink &downstream) noexcept
    : runtime_(runtime), downstream_(downstream) {}

void VulkanActionBridge::Enable(bool enabled) noexcept {
  enabled_ = enabled;
  if (!enabled_) {
    blocked_action_.reset();
    last_result_ = {};
  }
}

bool VulkanActionBridge::enabled() const noexcept { return enabled_; }

const VulkanActionBridgeResult &
VulkanActionBridge::last_result() const noexcept {
  return last_result_;
}

bool VulkanActionBridge::SameAction(const GpuAction &action) const noexcept {
  if (!blocked_action_.has_value())
    return false;
  const auto &retained = *blocked_action_;
  if (retained.packet_address != action.packet_address ||
      retained.packet_dwords != action.packet_dwords ||
      retained.opcode != action.opcode || retained.type != action.type ||
      retained.value_count != action.value_count ||
      retained.shader_binding_count != action.shader_binding_count ||
      !std::equal(retained.values.begin(), retained.values.end(),
                  action.values.begin())) {
    return false;
  }
  for (std::size_t index = 0; index < retained.shader_binding_count; ++index) {
    const auto &left = retained.shader_bindings[index];
    const auto &right = action.shader_bindings[index];
    if (left.stage != right.stage || left.status != right.status ||
        left.program_address != right.program_address ||
        left.code_address != right.code_address ||
        left.header_address != right.header_address ||
        left.code_offset_bytes != right.code_offset_bytes ||
        left.code_size_bytes != right.code_size_bytes ||
        left.binary_type != right.binary_type) {
      return false;
    }
  }
  // A packet is resumed only before its decoder advances, but retain the
  // execution-defining scalars too so an accidental caller retry cannot poll
  // a different action that happens to reuse the same packet address.
  const auto &left = retained.render;
  const auto &right = action.render;
  return left.status == right.status &&
         left.group_count_x == right.group_count_x &&
         left.group_count_y == right.group_count_y &&
         left.group_count_z == right.group_count_z &&
         left.vertex_count == right.vertex_count &&
         left.instance_count == right.instance_count &&
         left.color_base == right.color_base &&
         left.color_row_pitch_bytes == right.color_row_pitch_bytes &&
         left.color_width == right.color_width &&
         left.color_height == right.color_height &&
         left.color_format == right.color_format &&
         left.color_write_mask == right.color_write_mask &&
         left.primitive_type == right.primitive_type &&
         left.cull_mode == right.cull_mode &&
         left.front_face_clockwise == right.front_face_clockwise &&
         left.blend_enable == right.blend_enable &&
         left.blend_control == right.blend_control &&
         left.viewport_x_scale_bits == right.viewport_x_scale_bits &&
         left.viewport_x_offset_bits == right.viewport_x_offset_bits &&
         left.viewport_y_scale_bits == right.viewport_y_scale_bits &&
         left.viewport_y_offset_bits == right.viewport_y_offset_bits &&
         left.viewport_z_min_bits == right.viewport_z_min_bits &&
         left.viewport_z_max_bits == right.viewport_z_max_bits &&
         left.scissor_left == right.scissor_left &&
         left.scissor_top == right.scissor_top &&
         left.scissor_right == right.scissor_right &&
         left.scissor_bottom == right.scissor_bottom &&
         left.depth_enabled == right.depth_enabled &&
         left.depth_write_enabled == right.depth_write_enabled &&
         left.depth_compare == right.depth_compare &&
         left.depth_base == right.depth_base &&
         left.depth_row_pitch_bytes == right.depth_row_pitch_bytes &&
         left.depth_width == right.depth_width &&
         left.depth_height == right.depth_height;
}

GpuCommandStatus VulkanActionBridge::Submit(const GpuAction &action) noexcept {
  if (!enabled_ || (action.type != GpuActionType::kDraw &&
                    action.type != GpuActionType::kDispatch)) {
    return downstream_.Submit(action);
  }

  try {
    last_result_ = SameAction(action)
                       ? runtime_.PollVulkanActionExecution(action)
                       : runtime_.ExecuteVulkanAction(action);
  } catch (...) {
    last_result_ = {VulkanActionBridgeStatus::kExecutionFailed,
                    action.packet_address,
                    action.opcode,
                    GpuShaderStage::kCompute,
                    0,
                    0,
                    0,
                    "Vulkan action bridge allocation failed"};
  }

  if (last_result_.status == VulkanActionBridgeStatus::kBlocked) {
    if (!blocked_action_.has_value()) {
      blocked_action_ = action;
    }
    return GpuCommandStatus::kBlocked;
  }
  blocked_action_.reset();
  if (last_result_.status != VulkanActionBridgeStatus::kCompleted) {
    return GpuCommandStatus::kUnsupportedPacket;
  }
  return downstream_.Submit(action);
}

} // namespace kajps5::gpu
