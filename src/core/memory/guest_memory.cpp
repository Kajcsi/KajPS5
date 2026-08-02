// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/memory/guest_memory.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

#include "core/memory/shared_memory_backing.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace kajps5::memory {
namespace {

constexpr std::uint8_t kAllProtectionBits =
    static_cast<std::uint8_t>(GuestMemoryProtection::kRead) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kWrite) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kExecute) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kGpuRead) |
    static_cast<std::uint8_t>(GuestMemoryProtection::kGpuWrite);

bool IsValidProtection(GuestMemoryProtection protection) noexcept {
  return (static_cast<std::uint8_t>(protection) & ~kAllProtectionBits) == 0;
}

bool HasProtection(GuestMemoryProtection value,
                   GuestMemoryProtection required) noexcept {
  return (static_cast<std::uint8_t>(value) &
          static_cast<std::uint8_t>(required)) != 0;
}

bool AlignLengthToPage(std::uint64_t length, std::size_t page_size,
                       std::uint64_t& aligned_length) noexcept {
  if (length == 0 || page_size == 0) {
    return false;
  }
  const auto mask = static_cast<std::uint64_t>(page_size - 1);
  if ((page_size & (page_size - 1)) != 0 ||
      length > std::numeric_limits<std::uint64_t>::max() - mask) {
    return false;
  }
  aligned_length = (length + mask) & ~mask;
  return true;
}

std::size_t HostPageSize() noexcept {
#if defined(_WIN32)
  SYSTEM_INFO information{};
  GetSystemInfo(&information);
  return information.dwPageSize;
#elif defined(__unix__) || defined(__APPLE__)
  const auto page_size = sysconf(_SC_PAGESIZE);
  return page_size > 0 ? static_cast<std::size_t>(page_size) : 0;
#else
  return 0;
#endif
}

void* AllocateHostMapping(std::size_t size) noexcept {
#if defined(_WIN32)
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__unix__) || defined(__APPLE__)
  auto* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return mapping == MAP_FAILED ? nullptr : mapping;
#else
  (void)size;
  return nullptr;
#endif
}

bool ProtectHostMapping(void* address, std::size_t size,
                        GuestMemoryProtection protection) noexcept {
  if (address == nullptr || size == 0) {
    return false;
  }
#if defined(_WIN32)
  const auto read = HasProtection(protection, GuestMemoryProtection::kRead);
  const auto write = HasProtection(protection, GuestMemoryProtection::kWrite);
  const auto execute =
      HasProtection(protection, GuestMemoryProtection::kExecute);
  DWORD native = PAGE_NOACCESS;
  if (execute && write) {
    native = PAGE_EXECUTE_READWRITE;
  } else if (execute && read) {
    native = PAGE_EXECUTE_READ;
  } else if (execute) {
    native = PAGE_EXECUTE;
  } else if (write) {
    native = PAGE_READWRITE;
  } else if (read) {
    native = PAGE_READONLY;
  }
  DWORD previous = 0;
  if (VirtualProtect(address, size, native, &previous) == 0) {
    return false;
  }
  return !execute ||
         FlushInstructionCache(GetCurrentProcess(), address, size) != 0;
#elif defined(__unix__) || defined(__APPLE__)
  int native = PROT_NONE;
  if (HasProtection(protection, GuestMemoryProtection::kRead)) {
    native |= PROT_READ;
  }
  if (HasProtection(protection, GuestMemoryProtection::kWrite)) {
    native |= PROT_WRITE;
  }
  if (HasProtection(protection, GuestMemoryProtection::kExecute)) {
    native |= PROT_EXEC;
  }
  if (mprotect(address, size, native) != 0) {
    return false;
  }
  if ((native & PROT_EXEC) != 0) {
    auto* begin = static_cast<char*>(address);
    __builtin___clear_cache(begin, begin + size);
  }
  return true;
#else
  (void)protection;
  return false;
#endif
}

void FreeHostMapping(void* address, std::size_t size) noexcept {
  if (address == nullptr) {
    return;
  }
#if defined(_WIN32)
  (void)size;
  (void)VirtualFree(address, 0, MEM_RELEASE);
#elif defined(__unix__) || defined(__APPLE__)
  (void)munmap(address, size);
#else
  (void)size;
#endif
}

std::size_t ValidateSize(std::uint64_t base_address, std::size_t size) {
  const auto size64 = static_cast<std::uint64_t>(size);
  if (size64 > std::numeric_limits<std::uint64_t>::max() - base_address) {
    throw std::invalid_argument("Guest memory address range overflows.");
  }
  return size;
}

std::atomic<GuestMemoryTestFaultPoint>& GuestMemoryWriteTestFault() noexcept {
  static std::atomic<GuestMemoryTestFaultPoint> fault{
      GuestMemoryTestFaultPoint::kNone};
  return fault;
}

[[nodiscard]] GuestMemoryTestFaultPoint SnapshotGuestMemoryWriteTestFault()
    noexcept {
  return GuestMemoryWriteTestFault().load(std::memory_order_relaxed);
}

enum class GuestMemoryTestSynchronizationState : std::uint8_t {
  kIdle = 0,
  kArmed,
  kPaused,
  kReleased,
};

struct GuestMemoryTestSynchronization {
  std::atomic<GuestMemoryTestSynchronizationState> write_observer_state{
      GuestMemoryTestSynchronizationState::kIdle};
  std::atomic<GuestMemoryTestSynchronizationState> host_initialize_state{
      GuestMemoryTestSynchronizationState::kIdle};
  std::atomic<std::uint64_t> host_initialize_lock_attempts = 0;
  std::atomic<std::uint64_t> host_initialize_lock_acquisitions = 0;
};

GuestMemoryTestSynchronization& GetGuestMemoryTestSynchronization() noexcept {
  static GuestMemoryTestSynchronization synchronization;
  return synchronization;
}

std::atomic<GuestMemoryTestSynchronizationState>&
SynchronizationStateFor(GuestMemoryTestSynchronizationPoint point) noexcept {
  auto& synchronization = GetGuestMemoryTestSynchronization();
  return point == GuestMemoryTestSynchronizationPoint::kWriteObserver
             ? synchronization.write_observer_state
             : synchronization.host_initialize_state;
}

void PauseIfTestSynchronizationArmed(
    GuestMemoryTestSynchronizationPoint point) noexcept {
  auto& state = SynchronizationStateFor(point);
  auto expected = GuestMemoryTestSynchronizationState::kArmed;
  if (!state.compare_exchange_strong(expected,
                                     GuestMemoryTestSynchronizationState::kPaused,
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire)) {
    return;
  }
  while (state.load(std::memory_order_acquire) ==
         GuestMemoryTestSynchronizationState::kPaused) {
    std::this_thread::yield();
  }
  state.store(GuestMemoryTestSynchronizationState::kIdle,
              std::memory_order_release);
}

}  // namespace

void GuestMemory::MutationReport::Add(std::uint64_t address,
                                      std::uint64_t size) noexcept {
  if (whole_memory || size == 0) {
    return;
  }

  auto merged_start = address;
  auto merged_end = address + size;
  for (std::size_t index = 0; index < count;) {
    const auto& existing = ranges[index];
    const auto existing_end = existing.address + existing.size;
    if (existing.address > merged_end || merged_start > existing_end) {
      ++index;
      continue;
    }
    merged_start = std::min(merged_start, existing.address);
    merged_end = std::max(merged_end, existing_end);
    ranges[index] = ranges[count - 1];
    --count;
  }

  if (count == ranges.size()) {
    whole_memory = true;
    count = 0;
    return;
  }
  ranges[count++] = {merged_start, merged_end - merged_start};
}

GuestMemory::GuestMemory(std::uint64_t base_address, std::size_t size,
                         GuestMemoryProtection initial_protection)
    : base_address_(base_address),
      storage_size_(size),
      bytes_(ValidateSize(base_address, size)) {
  if (!IsValidProtection(initial_protection)) {
    throw std::invalid_argument("Guest memory protection is invalid.");
  }
  if (!bytes_.empty() && initial_protection != GuestMemoryProtection::kNone) {
    GuestMemoryGeneration mapping_generation;
    if (!AllocateMappingGenerationLocked(mapping_generation)) {
      throw std::logic_error("Guest memory mapping generation is exhausted.");
    }
    regions_.push_back({base_address_, this->size(), initial_protection});
    mapping_identity_regions_.push_back(
        {base_address_, this->size(), mapping_generation});
  }
}

GuestMemory::GuestMemory(std::byte* host_mapping, std::size_t size,
                         std::size_t mapping_granularity) noexcept
    : base_address_(static_cast<std::uint64_t>(
          reinterpret_cast<std::uintptr_t>(host_mapping))),
      storage_size_(size),
      mapping_granularity_(mapping_granularity),
      host_mapping_(host_mapping) {}

GuestMemory::~GuestMemory() {
  FreeHostMapping(host_mapping_, storage_size_);
}

std::unique_ptr<GuestMemory> GuestMemory::CreateHostMapped(
    std::size_t size,
    GuestMemoryProtection initial_protection) noexcept {
  const auto page_size = HostPageSize();
  if (size == 0 || !IsValidProtection(initial_protection) ||
      page_size == 0 || (page_size & (page_size - 1)) != 0 ||
      size % page_size != 0) {
    return nullptr;
  }
  auto* const mapping = static_cast<std::byte*>(AllocateHostMapping(size));
  if (mapping == nullptr ||
      size > std::numeric_limits<std::uint64_t>::max() -
                 static_cast<std::uint64_t>(
                     reinterpret_cast<std::uintptr_t>(mapping)) ||
      !ProtectHostMapping(mapping, size, GuestMemoryProtection::kNone)) {
    FreeHostMapping(mapping, size);
    return nullptr;
  }
  auto result = std::unique_ptr<GuestMemory>(
      new (std::nothrow) GuestMemory(mapping, size, page_size));
  if (!result) {
    FreeHostMapping(mapping, size);
    return nullptr;
  }
  if (initial_protection != GuestMemoryProtection::kNone &&
      !result->Map(result->base_address(), result->size(),
                   initial_protection)) {
    return nullptr;
  }
  return result;
}

std::size_t GuestMemory::HostMappingGranularity() noexcept {
  return HostPageSize();
}

std::uint64_t GuestMemory::base_address() const noexcept {
  return base_address_;
}

std::uint64_t GuestMemory::end_address() const noexcept {
  return base_address_ + size();
}

std::uint64_t GuestMemory::size() const noexcept {
  return static_cast<std::uint64_t>(storage_size_);
}

bool GuestMemory::host_mapped() const noexcept {
  return host_mapping_ != nullptr;
}

std::uint64_t GuestMemory::mapping_granularity() const noexcept {
  return static_cast<std::uint64_t>(mapping_granularity_);
}

bool GuestMemory::Contains(std::uint64_t address,
                           std::uint64_t length) const noexcept {
  if (address < base_address_) {
    return false;
  }

  const auto offset = address - base_address_;
  if (offset >= size()) {
    return false;
  }

  return length <= size() - offset;
}

bool GuestMemory::CanMap(std::uint64_t address,
                          std::uint64_t length) const noexcept {
  std::shared_lock lock(mapping_mutex_);
  return CanMapLocked(address, length);
}

bool GuestMemory::CanMapLocked(std::uint64_t address,
                                std::uint64_t length) const noexcept {
  if (length == 0) {
    return false;
  }
  if (host_mapped()) {
    const auto granularity = mapping_granularity();
    if (granularity == 0 || address % granularity != 0 ||
        !AlignLengthToPage(length, static_cast<std::size_t>(granularity),
                           length)) {
      return false;
    }
  }
  if (!Contains(address, length)) {
    return false;
  }

  const auto end_address = address + length;
  const auto insertion = std::lower_bound(
      regions_.begin(), regions_.end(), address,
      [](const GuestMemoryRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  if (insertion != regions_.begin()) {
    const auto& previous = *(insertion - 1);
    if (previous.address + previous.size > address) {
      return false;
    }
  }
  return insertion == regions_.end() || end_address <= insertion->address;
}

std::optional<std::uint64_t> GuestMemory::FindUnmappedRange(
    std::uint64_t search_start, std::uint64_t length,
    std::uint64_t alignment) const noexcept {
  std::shared_lock lock(mapping_mutex_);
  if (length == 0 || alignment == 0 ||
      (alignment & (alignment - 1)) != 0) {
    return std::nullopt;
  }
  if (host_mapped()) {
    const auto granularity = mapping_granularity();
    if (!AlignLengthToPage(length,
                           static_cast<std::size_t>(granularity), length)) {
      return std::nullopt;
    }
    alignment = std::max(alignment, granularity);
  }

  const auto align_up = [alignment](std::uint64_t address)
      -> std::optional<std::uint64_t> {
    const auto mask = alignment - 1;
    if (address > std::numeric_limits<std::uint64_t>::max() - mask) {
      return std::nullopt;
    }
    return (address + mask) & ~mask;
  };

  auto candidate = align_up(std::max(search_start, base_address_));
  if (!candidate.has_value()) {
    return std::nullopt;
  }
  for (const auto& region : regions_) {
    const auto region_end = region.address + region.size;
    if (region_end <= *candidate) {
      continue;
    }
    if (*candidate < region.address &&
        length <= region.address - *candidate) {
      return *candidate;
    }
    candidate = align_up(region_end);
    if (!candidate.has_value()) {
      return std::nullopt;
    }
  }
  return Contains(*candidate, length) ? candidate : std::nullopt;
}

bool GuestMemory::Map(std::uint64_t address, std::uint64_t length,
                      GuestMemoryProtection protection) {
  std::unique_lock lock(mapping_mutex_);
  return MapLocked(address, length, protection);
}

bool GuestMemory::MapLocked(std::uint64_t address, std::uint64_t length,
                            GuestMemoryProtection protection) {
  if (!IsValidProtection(protection) || length == 0) {
    return false;
  }

  auto mapped_length = length;
  if (host_mapped()) {
    const auto page_size = mapping_granularity_;
    if (page_size == 0 || address % page_size != 0 ||
        !AlignLengthToPage(length, page_size, mapped_length)) {
      return false;
    }
  }
  if (!CanMapLocked(address, mapped_length)) {
    return false;
  }
  GuestMemoryGeneration mapping_generation;
  if (!AllocateMappingGenerationLocked(mapping_generation)) {
    return false;
  }
  if (host_mapped() &&
      !ProtectHostMapping(host_mapping_ + OffsetOf(address),
                          static_cast<std::size_t>(mapped_length),
                          protection)) {
    return false;
  }

  const auto insertion = std::lower_bound(
      regions_.begin(), regions_.end(), address,
      [](const GuestMemoryRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  const auto mapping_insertion = std::lower_bound(
      mapping_identity_regions_.begin(), mapping_identity_regions_.end(),
      address,
      [](const MappingIdentityRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  try {
    const auto inserted_region =
        regions_.insert(insertion, {address, mapped_length, protection});
    try {
      mapping_identity_regions_.insert(
          mapping_insertion, {address, mapped_length, mapping_generation});
    } catch (...) {
      regions_.erase(inserted_region);
      throw;
    }
  } catch (...) {
    if (host_mapped()) {
      (void)ProtectHostMapping(host_mapping_ + OffsetOf(address),
                               static_cast<std::size_t>(mapped_length),
                               GuestMemoryProtection::kNone);
    }
    return false;
  }
  CoalesceRegions();
  CoalesceMappingIdentityRegions();
  return true;
}

bool GuestMemory::MapShared(
    std::uint64_t address, std::uint64_t length,
    GuestMemoryProtection protection,
    std::shared_ptr<SharedMemoryBacking> backing,
    std::uint64_t backing_offset) {
  std::unique_lock lock(mapping_mutex_);
  if (host_mapped() || !backing ||
      !backing->Contains(backing_offset, length) ||
      !IsValidProtection(protection) || !CanMapLocked(address, length)) {
    return false;
  }

  const auto insertion = std::lower_bound(
      shared_mappings_.begin(), shared_mappings_.end(), address,
      [](const SharedMapping& mapping, std::uint64_t candidate) {
        return mapping.address < candidate;
      });
  const auto index = static_cast<std::size_t>(
      insertion - shared_mappings_.begin());
  try {
    shared_mappings_.insert(
        insertion, {address, length, backing_offset, std::move(backing)});
  } catch (...) {
    return false;
  }
  try {
    if (MapLocked(address, length, protection)) {
      return true;
    }
  } catch (...) {
  }
  shared_mappings_.erase(
      shared_mappings_.begin() + static_cast<std::ptrdiff_t>(index));
  return false;
}

bool GuestMemory::Protect(std::uint64_t address, std::uint64_t length,
                           GuestMemoryProtection protection) {
  std::unique_lock lock(mapping_mutex_);
  if (length == 0 || !IsValidProtection(protection)) {
    return false;
  }

  auto protected_length = length;
  if (host_mapped()) {
    const auto page_size = mapping_granularity_;
    if (page_size == 0 || address % page_size != 0 ||
        !AlignLengthToPage(length, page_size, protected_length)) {
      return false;
    }
  }
  if (!IsMappedLocked(address, protected_length)) {
    return false;
  }

  const auto range_end = address + protected_length;
  std::vector<GuestMemoryRegion> updated;
  updated.reserve(regions_.size() + 2);
  for (const auto& region : regions_) {
    const auto region_end = region.address + region.size;
    if (range_end <= region.address || address >= region_end) {
      updated.push_back(region);
      continue;
    }

    if (region.address < address) {
      updated.push_back(
          {region.address, address - region.address, region.protection});
    }
    const auto protected_start = std::max(region.address, address);
    const auto protected_end = std::min(region_end, range_end);
    updated.push_back(
        {protected_start, protected_end - protected_start, protection});
    if (range_end < region_end) {
      updated.push_back(
          {range_end, region_end - range_end, region.protection});
    }
  }

  if (host_mapped() &&
      !ProtectHostMapping(host_mapping_ + OffsetOf(address),
                           static_cast<std::size_t>(protected_length),
                           protection)) {
    return false;
  }
  regions_ = std::move(updated);
  CoalesceRegions();
  return true;
}

bool GuestMemory::Unmap(std::uint64_t address, std::uint64_t length) {
  std::unique_lock lock(mapping_mutex_);
  if (length == 0) {
    return false;
  }

  auto unmapped_length = length;
  if (host_mapped()) {
    const auto page_size = mapping_granularity_;
    if (page_size == 0 || address % page_size != 0 ||
        !AlignLengthToPage(length, page_size, unmapped_length)) {
      return false;
    }
  }
  if (!IsMappedLocked(address, unmapped_length)) {
    return false;
  }

  const auto range_end = address + unmapped_length;
  auto updated_shared_mappings = shared_mappings_;
  for (std::size_t index = 0; index < updated_shared_mappings.size();) {
    auto& mapping = updated_shared_mappings[index];
    const auto mapping_end = mapping.address + mapping.size;
    const auto overlap_start = std::max(address, mapping.address);
    const auto overlap_end = std::min(range_end, mapping_end);
    if (overlap_start >= overlap_end) {
      ++index;
      continue;
    }
    if (overlap_start == mapping.address && overlap_end == mapping_end) {
      updated_shared_mappings.erase(
          updated_shared_mappings.begin() +
          static_cast<std::ptrdiff_t>(index));
      continue;
    }
    if (overlap_start == mapping.address) {
      const auto removed = overlap_end - mapping.address;
      mapping.address = overlap_end;
      mapping.backing_offset += removed;
      mapping.size -= removed;
      ++index;
      continue;
    }
    if (overlap_end == mapping_end) {
      mapping.size = overlap_start - mapping.address;
      ++index;
      continue;
    }

    auto suffix = mapping;
    suffix.address = overlap_end;
    suffix.backing_offset += overlap_end - mapping.address;
    suffix.size = mapping_end - overlap_end;
    mapping.size = overlap_start - mapping.address;
    updated_shared_mappings.insert(
        updated_shared_mappings.begin() +
            static_cast<std::ptrdiff_t>(index + 1),
        std::move(suffix));
    index += 2;
  }
  auto updated_mapping_identity_regions = mapping_identity_regions_;
  for (std::size_t index = 0;
       index < updated_mapping_identity_regions.size();) {
    auto& identity = updated_mapping_identity_regions[index];
    const auto identity_end = identity.address + identity.size;
    const auto overlap_start = std::max(address, identity.address);
    const auto overlap_end = std::min(range_end, identity_end);
    if (overlap_start >= overlap_end) {
      ++index;
      continue;
    }
    if (overlap_start == identity.address && overlap_end == identity_end) {
      updated_mapping_identity_regions.erase(
          updated_mapping_identity_regions.begin() +
          static_cast<std::ptrdiff_t>(index));
      continue;
    }
    if (overlap_start == identity.address) {
      const auto removed = overlap_end - identity.address;
      identity.address = overlap_end;
      identity.size -= removed;
      ++index;
      continue;
    }
    if (overlap_end == identity_end) {
      identity.size = overlap_start - identity.address;
      ++index;
      continue;
    }

    auto suffix = identity;
    suffix.address = overlap_end;
    suffix.size = identity_end - overlap_end;
    identity.size = overlap_start - identity.address;
    updated_mapping_identity_regions.insert(
        updated_mapping_identity_regions.begin() +
            static_cast<std::ptrdiff_t>(index + 1),
        std::move(suffix));
    index += 2;
  }
  std::vector<GuestMemoryRegion> updated;
  updated.reserve(regions_.size() + 1);
  for (const auto& region : regions_) {
    const auto region_end = region.address + region.size;
    if (range_end <= region.address || address >= region_end) {
      updated.push_back(region);
      continue;
    }

    if (region.address < address) {
      updated.push_back(
          {region.address, address - region.address, region.protection});
    }
    if (range_end < region_end) {
      updated.push_back(
          {range_end, region_end - range_end, region.protection});
    }
  }

  const auto offset = OffsetOf(address);
  if (host_mapped()) {
    auto* const target = host_mapping_ + offset;
    const auto native_length = static_cast<std::size_t>(unmapped_length);
    const auto writable = GuestMemoryProtection::kRead |
                          GuestMemoryProtection::kWrite;
    if (!ProtectHostMapping(target, native_length, writable)) {
      return false;
    }
    std::memset(target, 0, native_length);
    if (!ProtectHostMapping(target, native_length,
                            GuestMemoryProtection::kNone)) {
      for (const auto& region : regions_) {
        const auto overlap_start = std::max(address, region.address);
        const auto overlap_end =
            std::min(address + unmapped_length,
                     region.address + region.size);
        if (overlap_start < overlap_end) {
          (void)ProtectHostMapping(
              host_mapping_ + OffsetOf(overlap_start),
              static_cast<std::size_t>(overlap_end - overlap_start),
              region.protection);
        }
      }
      // The map table still describes the original mapping, but the bytes were
      // already cleared. Release the mapping lock before reporting that real
      // mutation so a resource record cannot remain clean after this failure.
      lock.unlock();
      NotifyWrite(address, unmapped_length);
      return false;
    }
  } else {
    std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                 static_cast<std::size_t>(length), std::byte{0});
  }
  regions_ = std::move(updated);
  mapping_identity_regions_ = std::move(updated_mapping_identity_regions);
  shared_mappings_ = std::move(updated_shared_mappings);
  CoalesceRegions();
  CoalesceMappingIdentityRegions();
  return true;
}

bool GuestMemory::IsMapped(std::uint64_t address,
                           std::uint64_t length) const noexcept {
  std::shared_lock lock(mapping_mutex_);
  return IsMappedLocked(address, length);
}

bool GuestMemory::CanAccess(
    std::uint64_t address, std::uint64_t length,
    GuestMemoryProtection required_protection) const noexcept {
  std::shared_lock lock(mapping_mutex_);
  return CanAccessLocked(address, length, required_protection);
}

bool GuestMemory::IsMappedLocked(std::uint64_t address,
                                 std::uint64_t length) const noexcept {
  return CanAccessLocked(address, length, GuestMemoryProtection::kNone);
}

bool GuestMemory::CanAccessLocked(
    std::uint64_t address, std::uint64_t length,
    GuestMemoryProtection required_protection) const noexcept {
  if (!Contains(address, length)) {
    return false;
  }

  auto region_index = FindContainingRegion(address);
  if (region_index == regions_.size()) {
    return false;
  }

  auto current_address = address;
  auto remaining = length;
  while (region_index < regions_.size()) {
    const auto& region = regions_[region_index];
    const auto region_end = region.address + region.size;
    if (current_address < region.address || current_address >= region_end) {
      return false;
    }

    const auto actual = static_cast<std::uint8_t>(region.protection);
    const auto required = static_cast<std::uint8_t>(required_protection);
    if ((actual & required) != required) {
      return false;
    }
    if (remaining == 0) {
      return true;
    }

    const auto available = region_end - current_address;
    const auto chunk = std::min(remaining, available);
    remaining -= chunk;
    if (remaining == 0) {
      return true;
    }
    current_address += chunk;
    ++region_index;
  }
  return false;
}

bool GuestMemory::CanExecute(std::uint64_t address,
                             std::uint64_t length) const noexcept {
  return CanAccess(address, length, GuestMemoryProtection::kExecute);
}

std::optional<GuestMemoryRegion> GuestMemory::QueryRegion(
    std::uint64_t address) const noexcept {
  std::shared_lock lock(mapping_mutex_);
  const auto index = FindContainingRegion(address);
  return index == regions_.size()
             ? std::nullopt
             : std::optional<GuestMemoryRegion>(regions_[index]);
}

std::span<const GuestMemoryRegion> GuestMemory::regions() const noexcept {
  return regions_;
}

std::optional<GuestMemoryMappingToken> GuestMemory::CaptureMappingToken(
    std::uint64_t address, std::uint64_t length) const noexcept {
  std::shared_lock lock(mapping_mutex_);
  return CaptureMappingTokenLocked(address, length);
}

std::optional<GuestMemoryMappingToken>
GuestMemory::CaptureMappingTokenLocked(std::uint64_t address,
                                       std::uint64_t length) const noexcept {
  if (length == 0 || !IsMappedLocked(address, length)) {
    return std::nullopt;
  }

  const auto insertion = std::lower_bound(
      mapping_identity_regions_.begin(), mapping_identity_regions_.end(),
      address,
      [](const MappingIdentityRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  auto identity_index = static_cast<std::size_t>(
      insertion - mapping_identity_regions_.begin());
  if (insertion == mapping_identity_regions_.end() ||
      insertion->address != address) {
    if (insertion == mapping_identity_regions_.begin()) {
      return std::nullopt;
    }
    --identity_index;
  }
  if (identity_index == mapping_identity_regions_.size()) {
    return std::nullopt;
  }

  auto current_address = address;
  auto remaining = length;
  GuestMemoryGeneration newest_generation;
  while (identity_index < mapping_identity_regions_.size()) {
    const auto& identity = mapping_identity_regions_[identity_index];
    const auto identity_end = identity.address + identity.size;
    if (current_address < identity.address ||
        current_address >= identity_end) {
      return std::nullopt;
    }

    newest_generation = std::max(newest_generation, identity.generation);
    const auto chunk = std::min(remaining, identity_end - current_address);
    remaining -= chunk;
    if (remaining == 0) {
      return GuestMemoryMappingToken{
          .generation = newest_generation,
          .generation_exhausted = mapping_generation_exhausted_,
      };
    }
    current_address += chunk;
    ++identity_index;
  }
  return std::nullopt;
}

bool GuestMemory::SetWriteObserver(
    std::weak_ptr<GuestMemoryWriteObserver> observer) noexcept {
  const auto candidate = observer.lock();
  if (!candidate) {
    return false;
  }

  std::lock_guard lock(coherence_mutex_);
  const auto existing = write_observer_.lock();
  if (existing && existing.get() != candidate.get()) {
    return false;
  }
  write_observer_ = candidate;
  return true;
}

void GuestMemory::ClearWriteObserver(
    const GuestMemoryWriteObserver* observer) noexcept {
  if (observer == nullptr) {
    return;
  }

  std::lock_guard lock(coherence_mutex_);
  const auto existing = write_observer_.lock();
  if (!existing || existing.get() == observer) {
    write_observer_.reset();
  }
}

GuestMemoryCoherenceSnapshot GuestMemory::coherence_snapshot() const
    noexcept {
  std::lock_guard lock(coherence_mutex_);
  return {
      .write_generation = write_generation_,
      .write_generation_exhausted = write_generation_exhausted_,
  };
}

void GuestMemory::SetWriteTestFaultForTesting(
    GuestMemoryTestFaultPoint point) noexcept {
  GuestMemoryWriteTestFault().store(point, std::memory_order_relaxed);
}

void GuestMemory::ClearWriteTestFaultForTesting() noexcept {
  SetWriteTestFaultForTesting(GuestMemoryTestFaultPoint::kNone);
}

void GuestMemory::ArmTestSynchronizationForTesting(
    GuestMemoryTestSynchronizationPoint point) noexcept {
  auto& synchronization = GetGuestMemoryTestSynchronization();
  if (point ==
      GuestMemoryTestSynchronizationPoint::kHostInitializeAfterWritable) {
    synchronization.host_initialize_lock_attempts.store(
        0, std::memory_order_relaxed);
    synchronization.host_initialize_lock_acquisitions.store(
        0, std::memory_order_relaxed);
  }
  SynchronizationStateFor(point).store(
      GuestMemoryTestSynchronizationState::kArmed, std::memory_order_release);
}

bool GuestMemory::WaitForTestSynchronizationForTesting(
    GuestMemoryTestSynchronizationPoint point,
    std::size_t maximum_yields) noexcept {
  const auto& state = SynchronizationStateFor(point);
  for (std::size_t yield_count = 0; yield_count < maximum_yields;
       ++yield_count) {
    if (state.load(std::memory_order_acquire) ==
        GuestMemoryTestSynchronizationState::kPaused) {
      return true;
    }
    std::this_thread::yield();
  }
  return state.load(std::memory_order_acquire) ==
         GuestMemoryTestSynchronizationState::kPaused;
}

void GuestMemory::ResumeTestSynchronizationForTesting(
    GuestMemoryTestSynchronizationPoint point) noexcept {
  SynchronizationStateFor(point).store(
      GuestMemoryTestSynchronizationState::kReleased,
      std::memory_order_release);
}

std::uint64_t GuestMemory::HostInitializeLockAttemptsForTesting() noexcept {
  return GetGuestMemoryTestSynchronization().host_initialize_lock_attempts.load(
      std::memory_order_acquire);
}

std::uint64_t GuestMemory::HostInitializeLockAcquisitionsForTesting() noexcept {
  return GetGuestMemoryTestSynchronization()
      .host_initialize_lock_acquisitions.load(std::memory_order_acquire);
}

bool GuestMemory::Read(std::uint64_t address,
                       std::span<std::byte> destination) const noexcept {
  std::shared_lock lock(mapping_mutex_);
  if (!CanAccessLocked(address, destination.size(),
                       GuestMemoryProtection::kRead)) {
    return false;
  }
  return ReadBytes(address, destination);
}

bool GuestMemory::Write(std::uint64_t address,
                         std::span<const std::byte> source) noexcept {
  MutationReport mutations;
  bool completed = false;
  {
    std::shared_lock lock(mapping_mutex_);
    if (!CanAccessLocked(address, source.size(),
                         GuestMemoryProtection::kWrite)) {
      return false;
    }
    std::uint64_t bytes_written = 0;
    completed = WriteBytes(address, source, bytes_written, mutations);
  }
  NotifyMutationReport(mutations);
  return completed;
}

bool GuestMemory::Copy(std::uint64_t destination, std::uint64_t source,
                        std::uint64_t length) noexcept {
  MutationReport mutations;
  bool completed = false;
  {
    std::shared_lock lock(mapping_mutex_);
    if (!CanAccessLocked(source, length, GuestMemoryProtection::kRead) ||
        !CanAccessLocked(destination, length, GuestMemoryProtection::kWrite)) {
      return false;
    }
    if (length == 0 || destination == source) {
      return true;
    }

    const auto byte_count = static_cast<std::size_t>(length);
    if (host_mapped()) {
      std::memmove(host_mapping_ + OffsetOf(destination),
                   host_mapping_ + OffsetOf(source), byte_count);
      RecordActualWriteRangeLocked(mutations, destination, length);
      completed = true;
    } else if (shared_mappings_.empty()) {
      std::memmove(bytes_.data() + OffsetOf(destination),
                   bytes_.data() + OffsetOf(source), byte_count);
      RecordActualWriteRangeLocked(mutations, destination, length);
      completed = true;
    } else {
      constexpr std::size_t kCopyChunkBytes = 4096;
      std::array<std::byte, kCopyChunkBytes> bytes{};
      if (destination > source && destination - source < length) {
        auto remaining = length;
        bool failed = false;
        while (remaining != 0) {
          const auto chunk = static_cast<std::size_t>(
              std::min<std::uint64_t>(bytes.size(), remaining));
          const auto offset = remaining - chunk;
          const auto view = std::span(bytes).first(chunk);
          if (!ReadBytes(source + offset, view)) {
            failed = true;
            break;
          }
          std::uint64_t bytes_written = 0;
          const auto write_completed =
              WriteBytes(destination + offset, view, bytes_written, mutations);
          if (!write_completed || bytes_written != chunk) {
            failed = true;
            break;
          }
          remaining = offset;
        }
        completed = !failed && remaining == 0;
      } else {
        std::uint64_t copied = 0;
        bool failed = false;
        while (copied < length) {
          const auto chunk = static_cast<std::size_t>(
              std::min<std::uint64_t>(bytes.size(), length - copied));
          const auto view = std::span(bytes).first(chunk);
          if (!ReadBytes(source + copied, view)) {
            failed = true;
            break;
          }
          std::uint64_t bytes_written = 0;
          const auto write_completed =
              WriteBytes(destination + copied, view, bytes_written, mutations);
          copied += bytes_written;
          if (!write_completed || bytes_written != chunk) {
            failed = true;
            break;
          }
        }
        completed = !failed && copied == length;
      }
    }
  }
  NotifyMutationReport(mutations);
  return completed;
}

bool GuestMemory::Fill(std::uint64_t address, std::uint64_t length,
                       std::byte value) noexcept {
  MutationReport mutations;
  bool completed = false;
  {
    std::shared_lock lock(mapping_mutex_);
    if (!CanAccessLocked(address, length, GuestMemoryProtection::kWrite)) {
      return false;
    }
    std::uint64_t bytes_written = 0;
    completed = FillBytes(address, length, value, bytes_written, mutations);
  }
  NotifyMutationReport(mutations);
  return completed;
}

bool GuestMemory::Initialize(
    std::uint64_t address, std::span<const std::byte> source) noexcept {
  MutationReport mutations;
  bool completed = false;
  const auto uses_host_mapping = host_mapped();
  if (uses_host_mapping) {
    GetGuestMemoryTestSynchronization().host_initialize_lock_attempts.fetch_add(
        1, std::memory_order_release);
  }
  {
    // Host initialization temporarily changes native page protections. A unique
    // mapping lock keeps another initializer or checked writer from restoring a
    // page to read-only while this copy is still in progress.
    std::unique_lock lock(mapping_mutex_);
    if (uses_host_mapping) {
      GetGuestMemoryTestSynchronization()
          .host_initialize_lock_acquisitions.fetch_add(
              1, std::memory_order_release);
    }
    if (!IsMappedLocked(address, source.size())) {
      return false;
    }
    if (source.empty() || !host_mapped()) {
      std::uint64_t bytes_written = 0;
      completed = WriteBytes(address, source, bytes_written, mutations);
    } else {
      const auto writable = GuestMemoryProtection::kRead |
                            GuestMemoryProtection::kWrite;
      std::size_t copied = 0;
      completed = true;
      while (copied < source.size()) {
        const auto current = address + copied;
        const auto region_index = FindContainingRegion(current);
        if (region_index == regions_.size()) {
          completed = false;
          break;
        }
        const auto& region = regions_[region_index];
        const auto chunk = std::min<std::size_t>(
            source.size() - copied,
            static_cast<std::size_t>(region.address + region.size - current));
        auto* const region_mapping = host_mapping_ + OffsetOf(region.address);
        if (!ProtectHostMapping(region_mapping,
                                static_cast<std::size_t>(region.size),
                                writable)) {
          completed = false;
          break;
        }
        PauseIfTestSynchronizationArmed(
            GuestMemoryTestSynchronizationPoint::kHostInitializeAfterWritable);
        std::memcpy(host_mapping_ + OffsetOf(current), source.data() + copied,
                    chunk);
        copied += chunk;
        if (!ProtectHostMapping(region_mapping,
                                static_cast<std::size_t>(region.size),
                                region.protection)) {
          completed = false;
          break;
        }
      }
      RecordActualWriteRangeLocked(mutations, address,
                                   static_cast<std::uint64_t>(copied));
    }
  }
  NotifyMutationReport(mutations);
  return completed;
}

bool GuestMemory::InitializeFill(std::uint64_t address,
                                  std::uint64_t length,
                                  std::byte value) noexcept {
  MutationReport mutations;
  bool completed = false;
  const auto uses_host_mapping = host_mapped();
  if (uses_host_mapping) {
    GetGuestMemoryTestSynchronization().host_initialize_lock_attempts.fetch_add(
        1, std::memory_order_release);
  }
  {
    std::unique_lock lock(mapping_mutex_);
    if (uses_host_mapping) {
      GetGuestMemoryTestSynchronization()
          .host_initialize_lock_acquisitions.fetch_add(
              1, std::memory_order_release);
    }
    if (!IsMappedLocked(address, length)) {
      return false;
    }
    if (length == 0 || !host_mapped()) {
      std::uint64_t bytes_written = 0;
      completed = FillBytes(address, length, value, bytes_written, mutations);
    } else {
      const auto writable = GuestMemoryProtection::kRead |
                            GuestMemoryProtection::kWrite;
      std::uint64_t filled = 0;
      completed = true;
      while (filled < length) {
        const auto current = address + filled;
        const auto region_index = FindContainingRegion(current);
        if (region_index == regions_.size()) {
          completed = false;
          break;
        }
        const auto& region = regions_[region_index];
        const auto chunk = std::min(length - filled,
                                    region.address + region.size - current);
        auto* const region_mapping = host_mapping_ + OffsetOf(region.address);
        if (!ProtectHostMapping(region_mapping,
                                static_cast<std::size_t>(region.size),
                                writable)) {
          completed = false;
          break;
        }
        PauseIfTestSynchronizationArmed(
            GuestMemoryTestSynchronizationPoint::kHostInitializeAfterWritable);
        std::memset(host_mapping_ + OffsetOf(current),
                    std::to_integer<unsigned char>(value),
                    static_cast<std::size_t>(chunk));
        filled += chunk;
        if (!ProtectHostMapping(region_mapping,
                                static_cast<std::size_t>(region.size),
                                region.protection)) {
          completed = false;
          break;
        }
      }
      RecordActualWriteRangeLocked(mutations, address, filled);
    }
  }
  NotifyMutationReport(mutations);
  return completed;
}

bool GuestMemory::ReadBytes(
    std::uint64_t address,
    std::span<std::byte> destination) const noexcept {
  std::size_t copied = 0;
  while (copied < destination.size()) {
    const auto current = address + copied;
    const auto shared_index = FindSharedMapping(current);
    if (shared_index != shared_mappings_.size()) {
      const auto& mapping = shared_mappings_[shared_index];
      const auto mapping_offset = current - mapping.address;
      const auto chunk = std::min<std::size_t>(
          destination.size() - copied,
          static_cast<std::size_t>(mapping.size - mapping_offset));
      if (!mapping.backing->Read(
              mapping.backing_offset + mapping_offset,
              destination.subspan(copied, chunk))) {
        return false;
      }
      copied += chunk;
      continue;
    }

    const auto next = std::lower_bound(
        shared_mappings_.begin(), shared_mappings_.end(), current,
        [](const SharedMapping& mapping, std::uint64_t candidate) {
          return mapping.address < candidate;
        });
    auto chunk = destination.size() - copied;
    if (next != shared_mappings_.end()) {
      chunk = std::min<std::size_t>(
          chunk, static_cast<std::size_t>(next->address - current));
    }
    const auto offset = OffsetOf(current);
    if (host_mapped()) {
      std::memcpy(destination.data() + copied, host_mapping_ + offset, chunk);
    } else {
      std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), chunk,
                  destination.begin() + static_cast<std::ptrdiff_t>(copied));
    }
    copied += chunk;
  }
  return true;
}

bool GuestMemory::WriteBytes(
    std::uint64_t address,
    std::span<const std::byte> source,
    std::uint64_t& bytes_written, MutationReport& mutations) noexcept {
  bytes_written = 0;
  const auto test_fault = SnapshotGuestMemoryWriteTestFault();
  std::size_t copied = 0;
  while (copied < source.size()) {
    const auto current = address + copied;
    const auto shared_index = FindSharedMapping(current);
    if (shared_index != shared_mappings_.size()) {
      const auto& mapping = shared_mappings_[shared_index];
      const auto mapping_offset = current - mapping.address;
      const auto chunk = std::min<std::size_t>(
          source.size() - copied,
          static_cast<std::size_t>(mapping.size - mapping_offset));
      if (!mapping.backing->Write(
              mapping.backing_offset + mapping_offset,
              source.subspan(copied, chunk))) {
        return false;
      }
      RecordActualWriteRangeLocked(mutations, current, chunk);
      copied += chunk;
      bytes_written += chunk;
      if (test_fault ==
          GuestMemoryTestFaultPoint::kFailWriteOrFillAfterFirstChunk) {
        return false;
      }
      continue;
    }

    const auto next = std::lower_bound(
        shared_mappings_.begin(), shared_mappings_.end(), current,
        [](const SharedMapping& mapping, std::uint64_t candidate) {
          return mapping.address < candidate;
        });
    auto chunk = source.size() - copied;
    if (next != shared_mappings_.end()) {
      chunk = std::min<std::size_t>(
          chunk, static_cast<std::size_t>(next->address - current));
    }
    const auto offset = OffsetOf(current);
    if (host_mapped()) {
      std::memcpy(host_mapping_ + offset, source.data() + copied, chunk);
    } else {
      std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(copied), chunk,
                  bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    RecordActualWriteRangeLocked(mutations, current, chunk);
    copied += chunk;
    bytes_written += chunk;
    if (test_fault ==
        GuestMemoryTestFaultPoint::kFailWriteOrFillAfterFirstChunk) {
      return false;
    }
  }
  return true;
}

bool GuestMemory::FillBytes(std::uint64_t address, std::uint64_t length,
                              std::byte value,
                              std::uint64_t& bytes_written,
                              MutationReport& mutations) noexcept {
  bytes_written = 0;
  const auto test_fault = SnapshotGuestMemoryWriteTestFault();
  std::uint64_t filled = 0;
  while (filled < length) {
    const auto current = address + filled;
    const auto shared_index = FindSharedMapping(current);
    if (shared_index != shared_mappings_.size()) {
      const auto& mapping = shared_mappings_[shared_index];
      const auto mapping_offset = current - mapping.address;
      const auto chunk =
          std::min(length - filled, mapping.size - mapping_offset);
      if (!mapping.backing->Fill(mapping.backing_offset + mapping_offset,
                                 chunk, value)) {
        return false;
      }
      RecordActualWriteRangeLocked(mutations, current, chunk);
      filled += chunk;
      bytes_written += chunk;
      if (test_fault ==
          GuestMemoryTestFaultPoint::kFailWriteOrFillAfterFirstChunk) {
        return false;
      }
      continue;
    }

    const auto next = std::lower_bound(
        shared_mappings_.begin(), shared_mappings_.end(), current,
        [](const SharedMapping& mapping, std::uint64_t candidate) {
          return mapping.address < candidate;
        });
    auto chunk = length - filled;
    if (next != shared_mappings_.end()) {
      chunk = std::min(chunk, next->address - current);
    }
    const auto offset = OffsetOf(current);
    if (host_mapped()) {
      std::memset(host_mapping_ + offset,
                  std::to_integer<unsigned char>(value),
                  static_cast<std::size_t>(chunk));
    } else {
      std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                  static_cast<std::size_t>(chunk), value);
    }
    RecordActualWriteRangeLocked(mutations, current, chunk);
    filled += chunk;
    bytes_written += chunk;
    if (test_fault ==
        GuestMemoryTestFaultPoint::kFailWriteOrFillAfterFirstChunk) {
      return false;
    }
  }
  return true;
}

std::size_t GuestMemory::FindContainingRegion(
    std::uint64_t address) const noexcept {
  const auto insertion = std::lower_bound(
      regions_.begin(), regions_.end(), address,
      [](const GuestMemoryRegion& region, std::uint64_t candidate) {
        return region.address < candidate;
      });
  if (insertion != regions_.end() && insertion->address == address) {
    return static_cast<std::size_t>(insertion - regions_.begin());
  }
  if (insertion == regions_.begin()) {
    return regions_.size();
  }

  const auto previous = insertion - 1;
  if (address < previous->address + previous->size) {
    return static_cast<std::size_t>(previous - regions_.begin());
  }
  return regions_.size();
}

std::size_t GuestMemory::FindSharedMapping(
    std::uint64_t address) const noexcept {
  const auto insertion = std::lower_bound(
      shared_mappings_.begin(), shared_mappings_.end(), address,
      [](const SharedMapping& mapping, std::uint64_t candidate) {
        return mapping.address < candidate;
      });
  if (insertion != shared_mappings_.end() && insertion->address == address) {
    return static_cast<std::size_t>(insertion - shared_mappings_.begin());
  }
  if (insertion == shared_mappings_.begin()) {
    return shared_mappings_.size();
  }
  const auto previous = insertion - 1;
  return address < previous->address + previous->size
             ? static_cast<std::size_t>(previous - shared_mappings_.begin())
             : shared_mappings_.size();
}

std::size_t GuestMemory::OffsetOf(std::uint64_t address) const noexcept {
  return static_cast<std::size_t>(address - base_address_);
}

void GuestMemory::RecordActualWriteRangeLocked(
    MutationReport& mutations, std::uint64_t address,
    std::uint64_t length) const noexcept {
  std::uint64_t remaining = length;
  auto current = address;
  while (remaining != 0 && !mutations.whole_memory) {
    const auto shared_index = FindSharedMapping(current);
    if (shared_index == shared_mappings_.size()) {
      const auto next = std::lower_bound(
          shared_mappings_.begin(), shared_mappings_.end(), current,
          [](const SharedMapping& mapping, std::uint64_t candidate) {
            return mapping.address < candidate;
          });
      auto chunk = remaining;
      if (next != shared_mappings_.end()) {
        chunk = std::min(chunk, next->address - current);
      }
      mutations.Add(current, chunk);
      current += chunk;
      remaining -= chunk;
      continue;
    }

    const auto& source_mapping = shared_mappings_[shared_index];
    const auto source_offset = current - source_mapping.address;
    const auto chunk = std::min(remaining, source_mapping.size - source_offset);
    const auto backing_start = source_mapping.backing_offset + source_offset;
    const auto backing_end = backing_start + chunk;
    bool reported_alias = false;
    for (const auto& alias : shared_mappings_) {
      if (alias.backing.get() != source_mapping.backing.get()) {
        continue;
      }
      const auto alias_backing_end = alias.backing_offset + alias.size;
      const auto overlap_start = std::max(backing_start, alias.backing_offset);
      const auto overlap_end = std::min(backing_end, alias_backing_end);
      if (overlap_start >= overlap_end) {
        continue;
      }
      mutations.Add(alias.address + overlap_start - alias.backing_offset,
                    overlap_end - overlap_start);
      reported_alias = true;
      if (mutations.whole_memory) {
        break;
      }
    }
    // MapShared() always leaves this source mapping in shared_mappings_. This
    // fallback remains fail-safe if a future mapping implementation violates
    // that invariant while a mutation is in flight.
    if (!reported_alias) {
      mutations.Add(current, chunk);
    }
    current += chunk;
    remaining -= chunk;
  }
}

void GuestMemory::NotifyMutationReport(
    const MutationReport& mutations) noexcept {
  if (mutations.whole_memory) {
    NotifyWrite(base_address_, size());
    return;
  }
  for (std::size_t index = 0; index < mutations.count; ++index) {
    NotifyWrite(mutations.ranges[index].address, mutations.ranges[index].size);
  }
}

void GuestMemory::NotifyWrite(std::uint64_t address,
                               std::uint64_t length) noexcept {
  if (length == 0) {
    return;
  }

  std::shared_ptr<GuestMemoryWriteObserver> observer;
  GuestMemoryWriteEvent event;
  {
    std::lock_guard lock(coherence_mutex_);
    if (!write_generation_exhausted_ &&
        !AdvanceGuestMemoryGeneration(write_generation_)) {
      write_generation_exhausted_ = true;
    }
    event = {
        .address = address,
        .size = length,
        .generation = write_generation_,
        .generation_exhausted = write_generation_exhausted_,
    };
    observer = write_observer_.lock();
  }

  // Callers release GuestMemory's mapping lock before reaching this funnel; do
  // not invoke a GPU callback while coherence_mutex_ is held either. A shared
  // snapshot also keeps the observer alive until this call completes.
  PauseIfTestSynchronizationArmed(
      GuestMemoryTestSynchronizationPoint::kWriteObserver);
  if (observer) {
    observer->OnGuestMemoryWrite(event);
  }
}

bool GuestMemory::AllocateMappingGenerationLocked(
    GuestMemoryGeneration& generation) noexcept {
  if (mapping_generation_exhausted_ ||
      !AdvanceGuestMemoryGeneration(next_mapping_generation_)) {
    mapping_generation_exhausted_ = true;
    return false;
  }
  generation = next_mapping_generation_;
  return true;
}

void GuestMemory::CoalesceRegions() {
  if (regions_.size() < 2) {
    return;
  }

  std::size_t output_index = 0;
  for (std::size_t input_index = 1; input_index < regions_.size();
       ++input_index) {
    const auto current = regions_[input_index];
    auto& previous = regions_[output_index];
    if (previous.address + previous.size == current.address &&
        previous.protection == current.protection) {
      previous.size += current.size;
    } else {
      ++output_index;
      regions_[output_index] = current;
    }
  }
  regions_.resize(output_index + 1);
}

void GuestMemory::CoalesceMappingIdentityRegions() {
  if (mapping_identity_regions_.size() < 2) {
    return;
  }

  std::size_t output_index = 0;
  for (std::size_t input_index = 1;
       input_index < mapping_identity_regions_.size(); ++input_index) {
    const auto current = mapping_identity_regions_[input_index];
    auto& previous = mapping_identity_regions_[output_index];
    if (previous.address + previous.size == current.address &&
        previous.generation == current.generation) {
      previous.size += current.size;
    } else {
      ++output_index;
      mapping_identity_regions_[output_index] = current;
    }
  }
  mapping_identity_regions_.resize(output_index + 1);
}

}  // namespace kajps5::memory
