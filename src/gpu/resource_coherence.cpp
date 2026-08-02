// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/graphics/host_gpu/memoryTracker.*,
// pageManager.*, and renderer/cache/gpuResourceManager.* at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.HLE/GuestImageWriteTracker.cs and
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/resource_coherence.h"

#include <limits>
#include <stdexcept>

namespace kajps5::gpu {
namespace {

[[nodiscard]] bool IsValidRange(std::uint64_t address,
                                std::uint64_t size) noexcept {
  return size != 0 && size <= std::numeric_limits<std::uint64_t>::max() -
                                     address;
}

[[nodiscard]] bool RangesOverlap(std::uint64_t left_address,
                                 std::uint64_t left_size,
                                 std::uint64_t right_address,
                                 std::uint64_t right_size) noexcept {
  return left_address < right_address + right_size &&
         right_address < left_address + left_size;
}

}  // namespace

GpuResourceCoherence::GpuResourceCoherence(memory::GuestMemory& memory) noexcept
    : memory_(memory) {}

std::shared_ptr<GpuResourceCoherence> GpuResourceCoherence::Create(
    memory::GuestMemory& memory) {
  auto result = std::shared_ptr<GpuResourceCoherence>(
      new GpuResourceCoherence(memory));
  if (!memory.SetWriteObserver(result)) {
    throw std::logic_error(
        "GuestMemory already has a live GPU write observer.");
  }
  return result;
}

GpuResourceCoherence::~GpuResourceCoherence() {
  memory_.ClearWriteObserver(this);
}

std::optional<GpuResourceId> GpuResourceCoherence::RegisterResource(
    std::uint64_t address, std::uint64_t size) noexcept {
  if (!IsValidRange(address, size)) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);
  // Holding this lock before sampling GuestMemory means a concurrent post-sample
  // write queues behind registration and is delivered to the freshly published
  // record instead of being lost between the write and record snapshots.
  // CaptureMappingToken is atomic with mapping mutations, and GuestMemory never
  // invokes this callback while holding its mapping or coherence locks.
  const auto snapshot = memory_.coherence_snapshot();
  const auto mapping_token = memory_.CaptureMappingToken(address, size);
  if (!mapping_token || mapping_token->generation_exhausted) {
    return std::nullopt;
  }

  const auto resource = next_resource_id_;
  if (resource == 0) {
    return std::nullopt;
  }
  const ResourceRecord record{
      .address = address,
      .size = size,
      .mapping_token = *mapping_token,
      .cpu_write_generation = snapshot.write_generation,
      .cpu_generation_precise = !snapshot.write_generation_exhausted,
      .needs_cpu_upload = true,
  };
  try {
    // unordered_map::emplace leaves the container unchanged if allocation or
    // construction throws. Advance the externally visible ID only after that
    // commit succeeds, so a late failure cannot publish a partial record.
    const auto [_, inserted] = records_.emplace(resource, record);
    if (!inserted) {
      return std::nullopt;
    }
  } catch (...) {
    return std::nullopt;
  }
  ++next_resource_id_;
  return resource;
}

bool GpuResourceCoherence::UnregisterResource(
    GpuResourceId resource) noexcept {
  std::lock_guard lock(mutex_);
  return records_.erase(resource) != 0;
}

std::optional<GpuResourceCoherenceState> GpuResourceCoherence::Query(
    GpuResourceId resource) const noexcept {
  std::lock_guard lock(mutex_);
  const auto found = records_.find(resource);
  if (found == records_.end()) {
    return std::nullopt;
  }

  const auto& record = found->second;
  const auto mapping_token =
      memory_.CaptureMappingToken(record.address, record.size);
  return GpuResourceCoherenceState{
      .mapped = mapping_token.has_value(),
      .mapping_changed = !mapping_token || mapping_token->generation_exhausted ||
                          *mapping_token != record.mapping_token,
      .needs_cpu_upload = record.needs_cpu_upload,
      .gpu_write_pending = record.gpu_write_pending,
      .cpu_generation_precise = record.cpu_generation_precise,
      .cpu_write_generation = record.cpu_write_generation,
  };
}

bool GpuResourceCoherence::AcknowledgeCpuUpload(
    GpuResourceId resource, memory::GuestMemoryGeneration generation) noexcept {
  std::lock_guard lock(mutex_);
  const auto found = records_.find(resource);
  if (found == records_.end() || !RecordIsCurrent(found->second)) {
    return false;
  }

  auto& record = found->second;
  if (!record.needs_cpu_upload || !record.cpu_generation_precise ||
      record.cpu_write_generation != generation) {
    return false;
  }
  record.needs_cpu_upload = false;
  return true;
}

bool GpuResourceCoherence::MarkGpuWrite(GpuResourceId resource) noexcept {
  std::lock_guard lock(mutex_);
  const auto found = records_.find(resource);
  if (found == records_.end() || !RecordIsCurrent(found->second)) {
    return false;
  }
  found->second.gpu_write_pending = true;
  return true;
}

bool GpuResourceCoherence::InvalidateGpuWrite(
    GpuResourceId resource) noexcept {
  std::lock_guard lock(mutex_);
  const auto found = records_.find(resource);
  if (found == records_.end() || !RecordIsCurrent(found->second)) {
    return false;
  }
  found->second.gpu_write_pending = false;
  return true;
}

void GpuResourceCoherence::OnGuestMemoryWrite(
    const memory::GuestMemoryWriteEvent& event) noexcept {
  if (!IsValidRange(event.address, event.size)) {
    return;
  }

  std::lock_guard lock(mutex_);
  for (auto& [_, record] : records_) {
    if (!RangesOverlap(record.address, record.size, event.address,
                       event.size)) {
      continue;
    }
    // NotifyWrite assigns generations before invoking callbacks, but concurrent
    // callbacks can reach this record out of order. A later upload subsumes an
    // earlier event, so never regress the record or let a delayed callback
    // re-dirty an already acknowledged newer generation. Exhaustion is sticky:
    // once precision is lost, acknowledgement must remain fail-closed.
    const auto is_newer = event.generation > record.cpu_write_generation;
    if (is_newer) {
      record.cpu_write_generation = event.generation;
    }
    if (event.generation_exhausted) {
      record.cpu_generation_precise = false;
    }
    if (is_newer || event.generation_exhausted ||
        !record.cpu_generation_precise) {
      record.needs_cpu_upload = true;
    }
  }
}

bool GpuResourceCoherence::RecordIsCurrent(
    const ResourceRecord& record) const noexcept {
  const auto mapping_token =
      memory_.CaptureMappingToken(record.address, record.size);
  return mapping_token && !mapping_token->generation_exhausted &&
         *mapping_token == record.mapping_token;
}

}  // namespace kajps5::gpu
