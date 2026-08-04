// Copyright (C) 2026 KajPS5 contributors
// State-to-execution adaptation reference: KytyPS5
// src/graphics/guest_gpu/{hardwareContext,graphicsRun}.* at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Diagnostics and retained-fence behavior reference: SharpEmu at
// 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "gpu/command_processor.h"

namespace kajps5::gpu {

class GpuRuntime;

enum class VulkanActionBridgeStatus : std::uint8_t {
  kDisabled,
  kCompleted,
  kBlocked,
  kRendererUnavailable,
  kUnsupportedState,
  kShaderUnavailable,
  kCompilationFailed,
  kDeviceLost,
  kExecutionFailed,
};

struct VulkanActionBridgeResult {
  VulkanActionBridgeStatus status = VulkanActionBridgeStatus::kDisabled;
  std::uint64_t packet_address = 0;
  std::uint32_t opcode = 0;
  GpuShaderStage stage = GpuShaderStage::kCompute;
  std::uint32_t first_register = 0;
  std::uint32_t first_value = 0;
  std::uint64_t timeline = 0;
  std::string message;
};

// The bridge is deliberately a sink rather than a second runtime.  It sits
// after memory/event effects and before the caller's trace/history sink.
class VulkanActionBridge final : public GpuSubmissionSink {
public:
  VulkanActionBridge(GpuRuntime &runtime,
                     GpuSubmissionSink &downstream) noexcept;

  void Enable(bool enabled) noexcept;
  [[nodiscard]] bool enabled() const noexcept;
  [[nodiscard]] const VulkanActionBridgeResult &last_result() const noexcept;
  [[nodiscard]] GpuCommandStatus
  Submit(const GpuAction &action) noexcept override;

private:
  [[nodiscard]] bool SameAction(const GpuAction &action) const noexcept;

  GpuRuntime &runtime_;
  GpuSubmissionSink &downstream_;
  bool enabled_ = false;
  std::optional<GpuAction> blocked_action_;
  VulkanActionBridgeResult last_result_{};
};

} // namespace kajps5::gpu
