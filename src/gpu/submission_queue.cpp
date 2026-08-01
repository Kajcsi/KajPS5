// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/submission_queue.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "gpu/runtime.h"

namespace kajps5::gpu {
namespace {

GpuEnqueueStatus EnqueueStatus(GpuCommandStatus status) noexcept {
  switch (status) {
    case GpuCommandStatus::kInvalidArgument:
      return GpuEnqueueStatus::kInvalidArgument;
    case GpuCommandStatus::kMemoryFault:
      return GpuEnqueueStatus::kMemoryFault;
    case GpuCommandStatus::kResourceLimit:
      return GpuEnqueueStatus::kResourceLimit;
    case GpuCommandStatus::kComplete:
    case GpuCommandStatus::kBlocked:
    case GpuCommandStatus::kMalformedPacket:
    case GpuCommandStatus::kUnsupportedPacket:
      return GpuEnqueueStatus::kInvalidArgument;
  }
  return GpuEnqueueStatus::kInvalidArgument;
}

}  // namespace

GpuSubmissionQueue::GpuSubmissionQueue(GpuRuntime& runtime) noexcept
    : runtime_(runtime) {}

GpuEnqueueResult GpuSubmissionQueue::EnqueueGraphics(
    std::uint64_t address, std::uint32_t dword_count,
    GpuQueueLimits limits) {
  std::lock_guard lock(mutex_);
  return Enqueue(graphics_, address, dword_count, limits);
}

GpuEnqueueResult GpuSubmissionQueue::EnqueueCompute(
    std::uint32_t owner, std::uint64_t address,
    std::uint32_t dword_count, GpuQueueLimits limits) {
  std::lock_guard lock(mutex_);
  GpuCommandCursor cursor;
  const auto prepared = Prepare(address, dword_count, limits, cursor);
  if (!prepared) {
    return prepared;
  }
  try {
    auto [entry, inserted] = compute_queues_.try_emplace(owner);
    static_cast<void>(inserted);
    return Commit(entry->second, dword_count, limits, std::move(cursor));
  } catch (...) {
    return {GpuEnqueueStatus::kResourceLimit};
  }
}

GpuEnqueueResult GpuSubmissionQueue::Enqueue(
    QueueState& queue, std::uint64_t address,
    std::uint32_t dword_count, const GpuQueueLimits& limits) {
  GpuCommandCursor cursor;
  const auto prepared = Prepare(address, dword_count, limits, cursor);
  if (!prepared) {
    return prepared;
  }
  return Commit(queue, dword_count, limits, std::move(cursor));
}

GpuEnqueueResult GpuSubmissionQueue::Prepare(
    std::uint64_t address, std::uint32_t dword_count,
    const GpuQueueLimits& limits, GpuCommandCursor& cursor) {
  if (limits.max_pending_submissions == 0 ||
      limits.max_pending_root_dwords == 0 ||
      limits.max_drain_passes == 0 ||
      pending_submissions_ >= limits.max_pending_submissions ||
      dword_count > limits.max_pending_root_dwords -
                        std::min(pending_root_dwords_,
                                 limits.max_pending_root_dwords)) {
    return {GpuEnqueueStatus::kResourceLimit};
  }
  cursor = runtime_.BeginCommandBuffer(address, dword_count, limits.command);
  if (cursor.terminal) {
    return {EnqueueStatus(cursor.result.status), 0, cursor.result};
  }
  return {GpuEnqueueStatus::kAccepted};
}

GpuEnqueueResult GpuSubmissionQueue::Commit(
    QueueState& queue, std::uint32_t dword_count,
    const GpuQueueLimits& limits, GpuCommandCursor cursor) {
  if (next_submission_id_ == 0 ||
      pending_root_dwords_ >
          std::numeric_limits<std::uint64_t>::max() - dword_count) {
    return {GpuEnqueueStatus::kResourceLimit};
  }
  const auto id = next_submission_id_++;
  try {
    queue.submissions.push_back(
        Submission{id, dword_count, std::move(cursor)});
  } catch (...) {
    return {GpuEnqueueStatus::kResourceLimit};
  }
  ++pending_submissions_;
  pending_root_dwords_ += dword_count;
  limits_ = limits;
  return {GpuEnqueueStatus::kAccepted, id};
}

GpuQueueDrainResult GpuSubmissionQueue::Drain(GpuSubmissionSink& sink) {
  std::lock_guard lock(mutex_);
  GpuQueueDrainResult drain;

  const auto process_queue = [this, &sink, &drain](QueueState& queue) {
    bool progressed = false;
    while (!queue.submissions.empty()) {
      auto& submission = queue.submissions.front();
      const auto actions_before = submission.cursor.result.submitted_actions;
      const auto result =
          runtime_.ResumeCommandBuffer(submission.cursor, sink);
      progressed = progressed ||
                   result.submitted_actions != actions_before;
      if (result.status == GpuCommandStatus::kBlocked) {
        break;
      }
      if (result.status == GpuCommandStatus::kComplete) {
        ++drain.completed_submissions;
      } else {
        ++drain.failed_submissions;
        if (!drain.first_failure.has_value()) {
          drain.first_failure = result;
        }
      }
      pending_root_dwords_ -= submission.root_dwords;
      --pending_submissions_;
      queue.submissions.pop_front();
      progressed = true;
    }
    return progressed;
  };

  bool settled = false;
  for (std::uint32_t pass = 0; pass < limits_.max_drain_passes; ++pass) {
    bool progressed = process_queue(graphics_);
    for (auto& [owner, queue] : compute_queues_) {
      static_cast<void>(owner);
      progressed = process_queue(queue) || progressed;
    }
    if (!progressed) {
      settled = true;
      break;
    }
  }
  drain.pass_limit_reached = !settled && pending_submissions_ != 0;
  drain.pending_submissions = pending_submissions_;
  drain.blocked_queues = graphics_.submissions.empty() ? 0 : 1;
  for (auto entry = compute_queues_.begin();
       entry != compute_queues_.end();) {
    if (entry->second.submissions.empty()) {
      entry = compute_queues_.erase(entry);
    } else {
      ++drain.blocked_queues;
      ++entry;
    }
  }
  return drain;
}

std::size_t GpuSubmissionQueue::PendingSubmissionCount() const noexcept {
  std::lock_guard lock(mutex_);
  return pending_submissions_;
}

const char* GpuEnqueueStatusName(GpuEnqueueStatus status) noexcept {
  switch (status) {
    case GpuEnqueueStatus::kAccepted:
      return "accepted";
    case GpuEnqueueStatus::kInvalidArgument:
      return "invalid-argument";
    case GpuEnqueueStatus::kMemoryFault:
      return "memory-fault";
    case GpuEnqueueStatus::kResourceLimit:
      return "resource-limit";
  }
  return "unknown";
}

}  // namespace kajps5::gpu
