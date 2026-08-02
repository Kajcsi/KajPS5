// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/graphics/host_gpu/memoryTracker.*,
// pageManager.*, and renderer/cache/gpuResourceManager.* at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.HLE/GuestImageWriteTracker.cs and
// src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "core/memory/guest_memory.h"

namespace kajps5::gpu {

class GpuRuntime;

using GpuResourceId = std::uint64_t;

// This is metadata only. It deliberately owns neither a guest address space
// nor host resource storage; a future backend must perform any actual upload,
// readback, or invalidation through its existing owners.
struct GpuResourceCoherenceState {
  bool mapped = false;
  bool mapping_changed = false;
  bool needs_cpu_upload = false;
  bool gpu_write_pending = false;
  bool cpu_generation_precise = true;
  memory::GuestMemoryGeneration cpu_write_generation;
};

// GpuRuntime is the sole production owner of this component. It observes every
// actual range changed by checked GuestMemory mutation APIs, including a late
// partial mutation that reports failure. Native direct guest stores are
// intentionally not claimed until GuestMemory provides a fault-backed source.
class GpuResourceCoherence final : public memory::GuestMemoryWriteObserver {
 public:
  ~GpuResourceCoherence() override;

  GpuResourceCoherence(const GpuResourceCoherence&) = delete;
  GpuResourceCoherence& operator=(const GpuResourceCoherence&) = delete;

  // Creates a record that needs one initial CPU upload. Invalid, overflowing,
  // or unmapped ranges are rejected without publishing a partial record.
  [[nodiscard]] std::optional<GpuResourceId> RegisterResource(
      std::uint64_t address, std::uint64_t size) noexcept;
  [[nodiscard]] bool UnregisterResource(GpuResourceId resource) noexcept;

  // Querying is non-consuming. The returned generation must be acknowledged
  // only after the backend has uploaded exactly those checked guest bytes.
  // GuestMemory validates a range-local mapping token at each operation; that
  // snapshot is not a reservation across an independently concurrent backend.
  [[nodiscard]] std::optional<GpuResourceCoherenceState> Query(
      GpuResourceId resource) const noexcept;
  [[nodiscard]] bool AcknowledgeCpuUpload(
      GpuResourceId resource,
      memory::GuestMemoryGeneration generation) noexcept;

  // A GPU write never mutates GuestMemory. It records that host GPU data is
  // newer until the backend explicitly resolves it by readback or discard.
  [[nodiscard]] bool MarkGpuWrite(GpuResourceId resource) noexcept;
  [[nodiscard]] bool InvalidateGpuWrite(GpuResourceId resource) noexcept;

 private:
  friend class GpuRuntime;

  struct ResourceRecord {
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    memory::GuestMemoryMappingToken mapping_token;
    memory::GuestMemoryGeneration cpu_write_generation;
    // Callbacks may arrive out of generation order; this generation only moves
    // forward, and a lost-precision state never becomes precise again.
    bool cpu_generation_precise = true;
    bool needs_cpu_upload = true;
    bool gpu_write_pending = false;
  };

  explicit GpuResourceCoherence(memory::GuestMemory& memory) noexcept;
  [[nodiscard]] static std::shared_ptr<GpuResourceCoherence> Create(
      memory::GuestMemory& memory);

  void OnGuestMemoryWrite(
      const memory::GuestMemoryWriteEvent& event) noexcept override;
  [[nodiscard]] bool RecordIsCurrent(
      const ResourceRecord& record) const noexcept;

  memory::GuestMemory& memory_;
  mutable std::mutex mutex_;
  std::unordered_map<GpuResourceId, ResourceRecord> records_;
  GpuResourceId next_resource_id_ = 1;
};

}  // namespace kajps5::gpu
