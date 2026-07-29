// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/relocator.h"

#include <array>
#include <bit>
#include <limits>
#include <vector>

namespace kajps5::loader {
namespace {

constexpr std::uint32_t kRelocationNone = 0;
constexpr std::uint32_t kRelocationGlobDat = 6;
constexpr std::uint32_t kRelocationJumpSlot = 7;
constexpr std::uint32_t kRelocationRelative = 8;

struct PlannedRelocation {
  std::uint64_t address = 0;
  std::array<std::byte, sizeof(std::uint64_t)> value{};
};

RelocationStatus PlanTable(const std::vector<ElfRelaEntry>& entries,
                           const memory::GuestMemory& memory,
                           std::uint64_t load_bias,
                           std::vector<PlannedRelocation>& planned,
                           std::size_t& unresolved_import_count) {
  for (const auto& entry : entries) {
    switch (entry.type()) {
      case kRelocationNone: continue;
      case kRelocationGlobDat:
      case kRelocationJumpSlot:
        ++unresolved_import_count;
        continue;
      case kRelocationRelative: break;
      default: return RelocationStatus::kUnsupportedRelocation;
    }

    if (entry.symbol() != 0) {
      return RelocationStatus::kInvalidRelativeSymbol;
    }
    if (entry.offset >
        std::numeric_limits<std::uint64_t>::max() - load_bias) {
      return RelocationStatus::kTargetAddressOverflow;
    }
    const auto target = load_bias + entry.offset;
    if (!memory.IsMapped(target, sizeof(std::uint64_t))) {
      return RelocationStatus::kTargetNotMapped;
    }

    PlannedRelocation relocation;
    relocation.address = target;
    const auto value =
        load_bias + std::bit_cast<std::uint64_t>(entry.addend);
    for (std::size_t index = 0; index < relocation.value.size(); ++index) {
      relocation.value[index] =
          static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
    planned.push_back(relocation);
  }
  return RelocationStatus::kOk;
}

}  // namespace

RelocationResult ApplyRelativeRelocations(const ElfMetadata& metadata,
                                          memory::GuestMemory& memory,
                                          std::uint64_t load_bias) {
  std::vector<PlannedRelocation> planned;
  planned.reserve(metadata.dynamic_info.relocations.size() +
                  metadata.dynamic_info.plt_relocations.size());
  std::size_t unresolved_import_count = 0;

  auto status = PlanTable(metadata.dynamic_info.relocations, memory, load_bias,
                          planned, unresolved_import_count);
  if (status == RelocationStatus::kOk) {
    status = PlanTable(metadata.dynamic_info.plt_relocations, memory, load_bias,
                       planned, unresolved_import_count);
  }
  if (status != RelocationStatus::kOk) {
    return {status, 0, unresolved_import_count};
  }

  std::size_t applied_count = 0;
  for (const auto& relocation : planned) {
    if (!memory.Initialize(relocation.address, relocation.value)) {
      return {RelocationStatus::kWriteFailed, applied_count,
              unresolved_import_count};
    }
    ++applied_count;
  }
  return {RelocationStatus::kOk, applied_count, unresolved_import_count};
}

std::string_view RelocationStatusName(RelocationStatus status) noexcept {
  switch (status) {
    case RelocationStatus::kOk: return "ok";
    case RelocationStatus::kUnsupportedRelocation:
      return "unsupported-relocation";
    case RelocationStatus::kInvalidRelativeSymbol:
      return "invalid-relative-symbol";
    case RelocationStatus::kTargetAddressOverflow:
      return "target-address-overflow";
    case RelocationStatus::kTargetNotMapped: return "target-not-mapped";
    case RelocationStatus::kWriteFailed: return "write-failed";
  }
  return "unknown";
}

}  // namespace kajps5::loader
