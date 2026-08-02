// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// Resource-coherence reference: KytyPS5
// src/graphics/host_gpu/{memoryTracker.*,pageManager.*,
// renderer/cache/gpuResourceManager.*} and SharpEmu
// src/SharpEmu.HLE/GuestImageWriteTracker.cs at the pinned upstream commits.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <vector>

namespace kajps5::memory {

class SharedMemoryBacking;

enum class GuestMemoryProtection : std::uint8_t {
  kNone = 0,
  kRead = 1U << 0U,
  kWrite = 1U << 1U,
  kExecute = 1U << 2U,
  kGpuRead = 0x10,
  kGpuWrite = 0x20,
};

[[nodiscard]] constexpr GuestMemoryProtection operator|(
    GuestMemoryProtection left, GuestMemoryProtection right) noexcept {
  return static_cast<GuestMemoryProtection>(
      static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

// A portable 128-bit logical clock represented as two scalar words. Sequence
// rollover increments epoch, so generation ordering never wraps backwards. If
// both fields are exhausted, callers must fail closed rather than reuse state.
struct GuestMemoryGeneration {
  std::uint64_t epoch = 0;
  std::uint64_t sequence = 0;

  [[nodiscard]] constexpr auto operator<=>(
      const GuestMemoryGeneration&) const noexcept = default;
};

[[nodiscard]] constexpr bool AdvanceGuestMemoryGeneration(
    GuestMemoryGeneration& generation) noexcept {
  if (generation.sequence != std::numeric_limits<std::uint64_t>::max()) {
    ++generation.sequence;
    return true;
  }
  if (generation.epoch == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  ++generation.epoch;
  generation.sequence = 1;
  return true;
}

struct GuestMemoryRegion {
  std::uint64_t address = 0;
  std::uint64_t size = 0;
  GuestMemoryProtection protection = GuestMemoryProtection::kNone;
};

// A range-local snapshot of the mapped regions that cover an entire range.
// generation is the newest identity in that range, so any unmap/remap
// overlapping it changes the token while unrelated mapping operations do not.
// GuestMemory stores these identities alongside its canonical region view;
// GpuRuntime keeps no second mapping ledger.
struct GuestMemoryMappingToken {
  GuestMemoryGeneration generation;
  bool generation_exhausted = false;

  [[nodiscard]] constexpr auto operator<=>(
      const GuestMemoryMappingToken&) const noexcept = default;
};

struct GuestMemoryWriteEvent {
  std::uint64_t address = 0;
  std::uint64_t size = 0;
  GuestMemoryGeneration generation;
  bool generation_exhausted = false;
};

class GuestMemoryWriteObserver {
 public:
  virtual ~GuestMemoryWriteObserver() = default;

  virtual void OnGuestMemoryWrite(
      const GuestMemoryWriteEvent& event) noexcept = 0;
};

struct GuestMemoryCoherenceSnapshot {
  GuestMemoryGeneration write_generation;
  bool write_generation_exhausted = false;
};

// This fault point is intentionally test-only. It forces a checked mutation to
// fail after its first completed internal segment, so coherence coverage of
// real partial writes is regression-tested without relying on allocator luck.
enum class GuestMemoryTestFaultPoint : std::uint8_t {
  kNone = 0,
  kFailWriteOrFillAfterFirstChunk,
};

// Test-only synchronization points make concurrency regressions reproducible
// without relying on scheduler timing.
enum class GuestMemoryTestSynchronizationPoint : std::uint8_t {
  kWriteObserver = 0,
  kHostInitializeAfterWritable,
};

class GuestMemory final {
 public:
  GuestMemory(
      std::uint64_t base_address, std::size_t size,
      GuestMemoryProtection initial_protection =
          GuestMemoryProtection::kRead | GuestMemoryProtection::kWrite);
  ~GuestMemory();

  GuestMemory(const GuestMemory&) = delete;
  GuestMemory& operator=(const GuestMemory&) = delete;

  [[nodiscard]] static std::unique_ptr<GuestMemory> CreateHostMapped(
      std::size_t size,
      GuestMemoryProtection initial_protection =
          GuestMemoryProtection::kNone) noexcept;
  [[nodiscard]] static std::size_t HostMappingGranularity() noexcept;

  [[nodiscard]] std::uint64_t base_address() const noexcept;
  [[nodiscard]] std::uint64_t end_address() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;
  [[nodiscard]] bool host_mapped() const noexcept;
  [[nodiscard]] std::uint64_t mapping_granularity() const noexcept;

  [[nodiscard]] bool Contains(std::uint64_t address,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool CanMap(std::uint64_t address,
                            std::uint64_t length) const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> FindUnmappedRange(
      std::uint64_t search_start, std::uint64_t length,
      std::uint64_t alignment) const noexcept;
  [[nodiscard]] bool Map(std::uint64_t address, std::uint64_t length,
                         GuestMemoryProtection protection);
  [[nodiscard]] bool MapShared(
      std::uint64_t address, std::uint64_t length,
      GuestMemoryProtection protection,
      std::shared_ptr<SharedMemoryBacking> backing,
      std::uint64_t backing_offset);
  [[nodiscard]] bool Protect(std::uint64_t address, std::uint64_t length,
                             GuestMemoryProtection protection);
  [[nodiscard]] bool Unmap(std::uint64_t address, std::uint64_t length);
  [[nodiscard]] bool IsMapped(std::uint64_t address,
                              std::uint64_t length) const noexcept;
  [[nodiscard]] bool CanAccess(
      std::uint64_t address, std::uint64_t length,
      GuestMemoryProtection required_protection) const noexcept;
  [[nodiscard]] bool CanExecute(std::uint64_t address,
                                std::uint64_t length) const noexcept;
  [[nodiscard]] std::optional<GuestMemoryRegion> QueryRegion(
      std::uint64_t address) const noexcept;
  // The returned span is borrowed. Callers that retain it across concurrent
  // Map/Protect/Unmap calls must provide their own external synchronization.
  [[nodiscard]] std::span<const GuestMemoryRegion> regions() const noexcept;

  // Captures mapping truth while Map/MapShared/Protect/Unmap are excluded. It
  // is a snapshot, not a reservation: a backend that spans calls must
  // synchronize its mapping lifetime and revalidate before committing work.
  [[nodiscard]] std::optional<GuestMemoryMappingToken> CaptureMappingToken(
      std::uint64_t address, std::uint64_t length) const noexcept;

  // The single observer is intended for GpuRuntime's resource owner. The
  // callback observes every range actually changed by checked mutation APIs,
  // including a changed prefix/range when an API fails late. It is invoked
  // after GuestMemory releases its mapping and coherence locks.
  [[nodiscard]] bool SetWriteObserver(
      std::weak_ptr<GuestMemoryWriteObserver> observer) noexcept;
  void ClearWriteObserver(const GuestMemoryWriteObserver* observer) noexcept;
  [[nodiscard]] GuestMemoryCoherenceSnapshot coherence_snapshot() const
      noexcept;

  static void SetWriteTestFaultForTesting(
      GuestMemoryTestFaultPoint point) noexcept;
  static void ClearWriteTestFaultForTesting() noexcept;
  static void ArmTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint point) noexcept;
  [[nodiscard]] static bool WaitForTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint point,
      std::size_t maximum_yields) noexcept;
  static void ResumeTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint point) noexcept;
  [[nodiscard]] static std::uint64_t
  HostInitializeLockAttemptsForTesting() noexcept;
  [[nodiscard]] static std::uint64_t
  HostInitializeLockAcquisitionsForTesting() noexcept;

  [[nodiscard]] bool Read(std::uint64_t address,
                          std::span<std::byte> destination) const noexcept;
  [[nodiscard]] bool Write(std::uint64_t address,
                           std::span<const std::byte> source) noexcept;
  [[nodiscard]] bool Copy(std::uint64_t destination,
                          std::uint64_t source,
                          std::uint64_t length) noexcept;
  [[nodiscard]] bool Fill(std::uint64_t address, std::uint64_t length,
                          std::byte value) noexcept;
  [[nodiscard]] bool Initialize(
      std::uint64_t address, std::span<const std::byte> source) noexcept;
  [[nodiscard]] bool InitializeFill(std::uint64_t address,
                                    std::uint64_t length,
                                    std::byte value) noexcept;

 private:
  GuestMemory(std::byte* host_mapping, std::size_t size,
              std::size_t mapping_granularity) noexcept;

  struct SharedMapping {
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    std::uint64_t backing_offset = 0;
    std::shared_ptr<SharedMemoryBacking> backing;
  };

  // Mapping-lifetime intervals are internal GuestMemory truth. Splits caused
  // by Protect() or a partial Unmap() retain their identity; a later Map()
  // receives a newer one. They remain distinct from the canonical public
  // region view, which deliberately coalesces adjacent equal protections.
  struct MappingIdentityRegion {
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    GuestMemoryGeneration generation;
  };

  struct MutationRange {
    std::uint64_t address = 0;
    std::uint64_t size = 0;
  };

  // Alias reporting never allocates in a mutation or callback path. If a
  // single mutation touches more disjoint aliases than this bounded report can
  // retain, it deliberately falls back to one conservative whole-memory event.
  struct MutationReport {
    static constexpr std::size_t kMaximumExactRanges = 64;

    std::array<MutationRange, kMaximumExactRanges> ranges{};
    std::size_t count = 0;
    bool whole_memory = false;

    void Add(std::uint64_t address, std::uint64_t size) noexcept;
  };

  [[nodiscard]] std::size_t FindContainingRegion(
      std::uint64_t address) const noexcept;
  [[nodiscard]] std::size_t FindSharedMapping(
      std::uint64_t address) const noexcept;
  [[nodiscard]] std::size_t OffsetOf(std::uint64_t address) const noexcept;
  [[nodiscard]] bool CanMapLocked(std::uint64_t address,
                                  std::uint64_t length) const noexcept;
  [[nodiscard]] bool CanAccessLocked(
      std::uint64_t address, std::uint64_t length,
      GuestMemoryProtection required_protection) const noexcept;
  [[nodiscard]] bool IsMappedLocked(std::uint64_t address,
                                    std::uint64_t length) const noexcept;
  [[nodiscard]] std::optional<GuestMemoryMappingToken>
  CaptureMappingTokenLocked(std::uint64_t address,
                            std::uint64_t length) const noexcept;
  [[nodiscard]] bool MapLocked(std::uint64_t address, std::uint64_t length,
                               GuestMemoryProtection protection);
  [[nodiscard]] bool ReadBytes(
      std::uint64_t address, std::span<std::byte> destination) const noexcept;
  [[nodiscard]] bool WriteBytes(
      std::uint64_t address, std::span<const std::byte> source,
      std::uint64_t& bytes_written, MutationReport& mutations) noexcept;
  [[nodiscard]] bool FillBytes(std::uint64_t address,
                               std::uint64_t length,
                               std::byte value,
                               std::uint64_t& bytes_written,
                               MutationReport& mutations) noexcept;
  void RecordActualWriteRangeLocked(MutationReport& mutations,
                                    std::uint64_t address,
                                    std::uint64_t length) const noexcept;
  void NotifyMutationReport(const MutationReport& mutations) noexcept;
  // The only write-observation funnel. A future fault-backed implementation
  // must first defer a verified native fault to ordinary GuestMemory execution;
  // this path is deliberately not callable directly from signal/fault context.
  void NotifyWrite(std::uint64_t address, std::uint64_t length) noexcept;
  [[nodiscard]] bool AllocateMappingGenerationLocked(
      GuestMemoryGeneration& generation) noexcept;
  void CoalesceRegions();
  void CoalesceMappingIdentityRegions();

  std::uint64_t base_address_ = 0;
  std::size_t storage_size_ = 0;
  std::size_t mapping_granularity_ = 1;
  std::vector<std::byte> bytes_;
  std::byte* host_mapping_ = nullptr;
  mutable std::shared_mutex mapping_mutex_;
  std::vector<GuestMemoryRegion> regions_;
  std::vector<MappingIdentityRegion> mapping_identity_regions_;
  std::vector<SharedMapping> shared_mappings_;
  mutable std::mutex coherence_mutex_;
  GuestMemoryGeneration write_generation_;
  GuestMemoryGeneration next_mapping_generation_;
  bool write_generation_exhausted_ = false;
  bool mapping_generation_exhausted_ = false;
  std::weak_ptr<GuestMemoryWriteObserver> write_observer_;
};

}  // namespace kajps5::memory
