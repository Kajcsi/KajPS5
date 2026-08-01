// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>

#include "gpu/command_processor.h"

namespace kajps5::gpu {

class GpuRuntime;

enum class GpuEnqueueStatus {
  kAccepted,
  kInvalidArgument,
  kMemoryFault,
  kResourceLimit,
};

struct GpuQueueLimits {
  std::size_t max_pending_submissions = 64;
  std::uint64_t max_pending_root_dwords = 4'000'000;
  std::uint32_t max_drain_passes = 256;
  GpuCommandLimits command{};
};

struct GpuEnqueueResult {
  GpuEnqueueStatus status = GpuEnqueueStatus::kAccepted;
  std::uint64_t submission_id = 0;
  GpuCommandResult command{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == GpuEnqueueStatus::kAccepted;
  }
};

struct GpuCommandBufferDescriptor {
  std::uint64_t address = 0;
  std::uint32_t dword_count = 0;
};

struct GpuQueueDrainResult {
  std::size_t completed_submissions = 0;
  std::size_t failed_submissions = 0;
  std::size_t blocked_queues = 0;
  std::size_t pending_submissions = 0;
  std::optional<GpuCommandResult> first_failure;
  bool pass_limit_reached = false;
};

class GpuSubmissionQueue final {
 public:
  explicit GpuSubmissionQueue(GpuRuntime& runtime) noexcept;

  [[nodiscard]] GpuEnqueueResult EnqueueGraphics(
      std::uint64_t address, std::uint32_t dword_count,
      GpuQueueLimits limits = {});
  [[nodiscard]] GpuEnqueueResult EnqueueCompute(
      std::uint32_t owner, std::uint64_t address,
      std::uint32_t dword_count, GpuQueueLimits limits = {});
  [[nodiscard]] GpuEnqueueResult EnqueueGraphicsBatch(
      std::span<const GpuCommandBufferDescriptor> buffers,
      GpuQueueLimits limits = {});
  [[nodiscard]] GpuEnqueueResult EnqueueComputeBatch(
      std::uint32_t owner,
      std::span<const GpuCommandBufferDescriptor> buffers,
      GpuQueueLimits limits = {});
  [[nodiscard]] GpuQueueDrainResult Drain(GpuSubmissionSink& sink);
  [[nodiscard]] std::size_t PendingSubmissionCount() const noexcept;

 private:
  struct Submission {
    std::uint64_t id = 0;
    std::uint32_t root_dwords = 0;
    GpuCommandCursor cursor;
  };

  struct QueueState {
    std::deque<Submission> submissions;
  };

  [[nodiscard]] GpuEnqueueResult Enqueue(
      QueueState& queue, std::uint64_t address,
      std::uint32_t dword_count, const GpuQueueLimits& limits);
  [[nodiscard]] GpuEnqueueResult EnqueueBatch(
      QueueState& queue,
      std::span<const GpuCommandBufferDescriptor> buffers,
      const GpuQueueLimits& limits);
  [[nodiscard]] GpuEnqueueResult Prepare(
      std::uint64_t address, std::uint32_t dword_count,
      const GpuQueueLimits& limits, GpuCommandCursor& cursor);
  [[nodiscard]] GpuEnqueueResult Commit(
      QueueState& queue, std::uint32_t dword_count,
      const GpuQueueLimits& limits, GpuCommandCursor cursor);

  GpuRuntime& runtime_;
  mutable std::mutex mutex_;
  QueueState graphics_;
  std::map<std::uint32_t, QueueState> compute_queues_;
  GpuQueueLimits limits_{};
  std::uint64_t next_submission_id_ = 1;
  std::size_t pending_submissions_ = 0;
  std::uint64_t pending_root_dwords_ = 0;
};

[[nodiscard]] const char* GpuEnqueueStatusName(
    GpuEnqueueStatus status) noexcept;

}  // namespace kajps5::gpu
