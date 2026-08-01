// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: KytyPS5 src/libs/agc.cpp at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"

namespace {

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_submission_queue_test: " << message << '\n';
    std::exit(1);
  }
}

std::uint32_t Pm4(std::uint32_t dwords, std::uint32_t opcode,
                  std::uint32_t packet_register = 0) {
  return 0xc0000000U | (((dwords - 2U) & 0x3fffU) << 16U) |
         ((opcode & 0xffU) << 8U) | ((packet_register & 0x3fU) << 2U);
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

bool Store32(kajps5::memory::GuestMemory& memory, std::uint64_t address,
             std::uint32_t value) noexcept {
  std::array<std::byte, 4> bytes{};
  Write32(bytes, 0, value);
  return memory.Write(address, bytes);
}

void WriteDwords(kajps5::memory::GuestMemory& memory, std::uint64_t address,
                 std::span<const std::uint32_t> words) {
  std::vector<std::byte> bytes(words.size() * 4U);
  for (std::size_t index = 0; index < words.size(); ++index) {
    Write32(bytes, index * 4U, words[index]);
  }
  Check(memory.Write(address, bytes), "guest command write failed");
}

class SignallingSink final : public kajps5::gpu::GpuSubmissionSink {
 public:
  SignallingSink(kajps5::memory::GuestMemory& memory,
                 std::uint64_t label) noexcept
      : memory_(memory), label_(label) {}

  [[nodiscard]] kajps5::gpu::GpuCommandStatus Submit(
      const kajps5::gpu::GpuAction& action) noexcept override {
    try {
      actions_.push_back(action.type);
    } catch (...) {
      return kajps5::gpu::GpuCommandStatus::kResourceLimit;
    }
    if (action.type == kajps5::gpu::GpuActionType::kDispatch &&
        !signalled_) {
      signalled_ = true;
      return Store32(memory_, label_, 0x77U)
                 ? kajps5::gpu::GpuCommandStatus::kComplete
                 : kajps5::gpu::GpuCommandStatus::kMemoryFault;
    }
    return kajps5::gpu::GpuCommandStatus::kComplete;
  }

  [[nodiscard]] std::span<const kajps5::gpu::GpuActionType> actions()
      const noexcept {
    return actions_;
  }

 private:
  kajps5::memory::GuestMemory& memory_;
  std::uint64_t label_ = 0;
  bool signalled_ = false;
  std::vector<kajps5::gpu::GpuActionType> actions_;
};

}  // namespace

int main() {
  using kajps5::gpu::GpuActionType;
  using kajps5::gpu::GpuCommandBufferDescriptor;
  using kajps5::gpu::GpuEnqueueStatus;
  using kajps5::gpu::GpuQueueLimits;
  using kajps5::gpu::GpuRuntime;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  constexpr std::uint64_t kBase = 0x900000;
  constexpr std::uint64_t kGraphics = kBase + 0x1000;
  constexpr std::uint64_t kGraphicsNext = kBase + 0x2000;
  constexpr std::uint64_t kCompute = kBase + 0x3000;
  constexpr std::uint64_t kLabel = kBase + 0x4000;
  GuestMemory memory(
      kBase, 0x6000,
      GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);
  GpuRuntime runtime(memory);

  const std::array graphics = {
      Pm4(7, 0x10, 0x0a), static_cast<std::uint32_t>(kLabel),
      static_cast<std::uint32_t>(kLabel >> 32U), 0xffU, 0x77U, 0x13U, 1U,
      Pm4(3, 0x2d), 3U, 2U,
  };
  const std::array graphics_next = {Pm4(3, 0x2d), 9U, 2U};
  const std::array compute = {Pm4(5, 0x15), 1U, 2U, 3U, 0x41U};
  WriteDwords(memory, kGraphics, graphics);
  WriteDwords(memory, kGraphicsNext, graphics_next);
  WriteDwords(memory, kCompute, compute);
  Check(Store32(memory, kLabel, 0), "label initialization failed");

  auto& queue = runtime.submissions();
  const auto first = queue.EnqueueGraphics(
      kGraphics, static_cast<std::uint32_t>(graphics.size()));
  const auto second = queue.EnqueueGraphics(
      kGraphicsNext, static_cast<std::uint32_t>(graphics_next.size()));
  const auto third = queue.EnqueueCompute(
      4, kCompute, static_cast<std::uint32_t>(compute.size()));
  Check(first && second && third && first.submission_id == 1 &&
            second.submission_id == 2 && third.submission_id == 3 &&
            queue.PendingSubmissionCount() == 3,
        "valid submissions were not queued in sequence");

  const std::array replacement = {Pm4(2, 0xfe), 0U};
  WriteDwords(memory, kGraphicsNext, replacement);
  SignallingSink sink(memory, kLabel);
  const auto drained = queue.Drain(sink);
  Check(drained.completed_submissions == 3 &&
            drained.failed_submissions == 0 &&
            drained.blocked_queues == 0 &&
            drained.pending_submissions == 0 &&
            !drained.pass_limit_reached &&
            queue.PendingSubmissionCount() == 0,
        "cross-queue drain did not reach a fixed point");
  Check(sink.actions().size() == 4 &&
            sink.actions()[0] == GpuActionType::kWaitMemory &&
            sink.actions()[1] == GpuActionType::kDispatch &&
            sink.actions()[2] == GpuActionType::kDraw &&
            sink.actions()[3] == GpuActionType::kDraw,
        "queue ordering or snapshot ownership is incorrect");

  const auto invalid = queue.EnqueueCompute(99, kBase + 0x7000, 2);
  Check(invalid.status == GpuEnqueueStatus::kMemoryFault &&
            queue.PendingSubmissionCount() == 0,
        "invalid compute submission changed queue state");

  constexpr std::uint64_t kBatchFirst = kBase + 0x4800;
  constexpr std::uint64_t kBatchSecond = kBase + 0x4900;
  const std::array batch_first = {Pm4(3, 0x2d), 20U, 2U};
  const std::array batch_second = {Pm4(3, 0x2d), 21U, 2U};
  WriteDwords(memory, kBatchFirst, batch_first);
  WriteDwords(memory, kBatchSecond, batch_second);
  const std::array batch = {
      GpuCommandBufferDescriptor{
          kBatchFirst, static_cast<std::uint32_t>(batch_first.size())},
      GpuCommandBufferDescriptor{
          kBatchSecond, static_cast<std::uint32_t>(batch_second.size())},
  };
  const auto batch_result = queue.EnqueueGraphicsBatch(batch);
  Check(batch_result && batch_result.submission_id == 4 &&
            queue.PendingSubmissionCount() == 2,
        "valid graphics batch was not queued atomically");
  const std::array replacement_batch = {Pm4(2, 0xfe), 0U};
  WriteDwords(memory, kBatchSecond, replacement_batch);
  SignallingSink batch_sink(memory, kLabel);
  const auto batch_drain = queue.Drain(batch_sink);
  Check(batch_drain.completed_submissions == 2 &&
            batch_drain.failed_submissions == 0 &&
            batch_sink.actions().size() == 2 &&
            batch_sink.actions()[0] == GpuActionType::kDraw &&
            batch_sink.actions()[1] == GpuActionType::kDraw,
        "graphics batch did not retain its validated snapshots");
  WriteDwords(memory, kBatchSecond, batch_second);

  const std::array invalid_batch = {
      batch[0],
      GpuCommandBufferDescriptor{kBase + 0x7000, 2},
  };
  const auto invalid_batch_result =
      queue.EnqueueGraphicsBatch(invalid_batch);
  Check(invalid_batch_result.status == GpuEnqueueStatus::kMemoryFault &&
            queue.PendingSubmissionCount() == 0,
        "failed graphics batch changed queue state");

  const auto compute_batch_result = queue.EnqueueComputeBatch(12, batch);
  Check(compute_batch_result && compute_batch_result.submission_id == 6 &&
            queue.PendingSubmissionCount() == 2,
        "valid compute batch was not queued atomically");
  SignallingSink compute_batch_sink(memory, kLabel);
  const auto compute_batch_drain = queue.Drain(compute_batch_sink);
  Check(compute_batch_drain.completed_submissions == 2 &&
            compute_batch_drain.failed_submissions == 0,
        "compute batch did not drain through its owner queue");

  GpuQueueLimits one_pending;
  one_pending.max_pending_submissions = 1;
  const auto limited_batch = queue.EnqueueGraphicsBatch(batch, one_pending);
  Check(limited_batch.status == GpuEnqueueStatus::kResourceLimit &&
            queue.PendingSubmissionCount() == 0,
        "batch limit failure changed queue state");
  Check(Store32(memory, kLabel, 0), "label reset failed");
  const auto limited_first = queue.EnqueueGraphics(
      kGraphics, static_cast<std::uint32_t>(graphics.size()), one_pending);
  const auto limited_second = queue.EnqueueGraphics(
      kGraphics, static_cast<std::uint32_t>(graphics.size()), one_pending);
  Check(limited_first &&
            limited_second.status == GpuEnqueueStatus::kResourceLimit &&
            queue.PendingSubmissionCount() == 1,
        "pending-submission limit was not enforced");

  Check(std::string_view(kajps5::gpu::GpuEnqueueStatusName(
                            GpuEnqueueStatus::kAccepted)) == "accepted",
        "enqueue status name is incorrect");
  return 0;
}
