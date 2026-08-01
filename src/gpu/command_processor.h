// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/graphics/guest_gpu/command_processor and
// src/graphics/guest_gpu/graphicsRun.cpp at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kajps5::gpu {

enum class GpuCommandStatus {
  kComplete,
  kBlocked,
  kInvalidArgument,
  kMemoryFault,
  kMalformedPacket,
  kUnsupportedPacket,
  kResourceLimit,
};

enum class GpuRegisterSpace : std::uint8_t {
  kContext,
  kShader,
  kUserConfig,
};

enum class GpuActionType : std::uint8_t {
  kNop,
  kRegisterWrite,
  kSetBase,
  kSetIndexBuffer,
  kSetIndexSize,
  kSetIndexCount,
  kSetNumInstances,
  kDraw,
  kDispatch,
  kPredication,
  kWriteData,
  kWaitMemory,
  kIndirectBuffer,
  kRewind,
  kLodStats,
};

struct GpuAction {
  GpuActionType type = GpuActionType::kNop;
  std::uint64_t packet_address = 0;
  std::uint32_t packet_dwords = 0;
  std::uint32_t opcode = 0;
  std::uint32_t packet_register = 0;
  std::array<std::uint64_t, 8> values{};
  std::size_t value_count = 0;
};

class GpuSubmissionSink {
 public:
  virtual ~GpuSubmissionSink() = default;
  // Submission is synchronous. A sink must not call the same GpuRuntime.
  [[nodiscard]] virtual bool Submit(const GpuAction& action) noexcept = 0;
};

class GpuActionTrace final : public GpuSubmissionSink {
 public:
  explicit GpuActionTrace(std::size_t capacity = 4096) noexcept;

  [[nodiscard]] bool Submit(const GpuAction& action) noexcept override;
  [[nodiscard]] std::span<const GpuAction> actions() const noexcept;
  void Clear() noexcept;

 private:
  std::size_t capacity_ = 0;
  std::vector<GpuAction> actions_;
};

struct GpuCommandLimits {
  std::size_t max_actions = 4096;
  std::uint32_t max_indirect_depth = 8;
  std::uint64_t max_processed_dwords = 1'000'000;
  std::uint32_t max_buffer_dwords = 1'000'000;
};

struct GpuCommandResult {
  GpuCommandStatus status = GpuCommandStatus::kComplete;
  std::uint64_t packet_address = 0;
  std::uint64_t processed_dwords = 0;
  std::size_t submitted_actions = 0;
  std::uint32_t opcode = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == GpuCommandStatus::kComplete;
  }
};

[[nodiscard]] const char* GpuCommandStatusName(
    GpuCommandStatus status) noexcept;

}  // namespace kajps5::gpu
