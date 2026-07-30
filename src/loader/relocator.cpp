// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/relocator.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <utility>
#include <vector>

namespace kajps5::loader {
namespace {

constexpr std::uint32_t kRelocationNone = 0;
constexpr std::uint32_t kRelocationAbsolute64 = 1;
constexpr std::uint32_t kRelocationGlobDat = 6;
constexpr std::uint32_t kRelocationJumpSlot = 7;
constexpr std::uint32_t kRelocationRelative = 8;
constexpr std::uint32_t kRelocationTlsModuleId = 16;
constexpr std::uint8_t kSymbolBindingWeak = 2;
constexpr std::uint16_t kUndefinedSectionIndex = 0;

struct PlannedRelocation {
  std::uint64_t address = 0;
  std::array<std::byte, sizeof(std::uint64_t)> value{};
};

struct ImportReference {
  std::string_view nid;
  const ElfLibraryIdentity* library = nullptr;
  bool valid = false;
};

std::optional<std::uint16_t> DecodeSceId(std::string_view value) noexcept {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
  if (value.empty() || value.size() > 3) {
    return std::nullopt;
  }

  std::uint32_t decoded = 0;
  for (const auto character : value) {
    const auto digit = alphabet.find(character);
    if (digit == std::string_view::npos) {
      return std::nullopt;
    }
    decoded = (decoded << 6U) | static_cast<std::uint32_t>(digit);
  }
  if (decoded > std::numeric_limits<std::uint16_t>::max() ||
      (value.size() == 2 && decoded < 0x40U) ||
      (value.size() == 3 && decoded < 0x1000U)) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(decoded);
}

template <typename Identity>
const Identity* FindIdentity(std::uint16_t id,
                             const std::vector<Identity>& imports,
                             const std::vector<Identity>& exports) noexcept {
  const auto find = [id](const std::vector<Identity>& identities) {
    const auto identity = std::find_if(
        identities.begin(), identities.end(),
        [id](const Identity& candidate) { return candidate.id == id; });
    return identity == identities.end() ? nullptr : &*identity;
  };
  if (const auto* identity = find(imports); identity != nullptr) {
    return identity;
  }
  return find(exports);
}

ImportReference ParseImportReference(const ElfMetadata& metadata,
                                     std::string_view symbol) noexcept {
  const auto first_separator = symbol.find('#');
  if (first_separator == std::string_view::npos) {
    return {symbol, nullptr, !symbol.empty()};
  }

  const auto nid = symbol.substr(0, first_separator);
  const auto second_separator = symbol.find('#', first_separator + 1);
  if (second_separator == std::string_view::npos ||
      symbol.find('#', second_separator + 1) != std::string_view::npos) {
    return {nid, nullptr, false};
  }
  const auto library_id = DecodeSceId(symbol.substr(
      first_separator + 1, second_separator - first_separator - 1));
  const auto module_id = DecodeSceId(symbol.substr(second_separator + 1));
  if (!library_id.has_value() || !module_id.has_value()) {
    return {nid, nullptr, false};
  }

  const auto* library = FindIdentity(
      *library_id, metadata.dynamic_info.import_libraries,
      metadata.dynamic_info.export_libraries);
  const auto* module = FindIdentity(
      *module_id, metadata.dynamic_info.import_modules,
      metadata.dynamic_info.export_modules);
  return {nid, library, library != nullptr && module != nullptr};
}

RelocationStatus PlanTable(const std::vector<ElfRelaEntry>& entries,
                           const ElfMetadata& metadata,
                           const memory::GuestMemory& memory,
                           const ImportResolver* resolver,
                           std::uint64_t load_bias,
                           std::uint64_t tls_module_id,
                           std::vector<PlannedRelocation>& planned,
                           std::size_t& resolved_import_count,
                           std::vector<UnresolvedImport>& unresolved_imports,
                           std::optional<std::uint32_t>& unsupported_type) {
  for (const auto& entry : entries) {
    const auto type = entry.type();
    switch (type) {
      case kRelocationNone: continue;
      case kRelocationAbsolute64:
      case kRelocationGlobDat:
      case kRelocationJumpSlot: break;
      case kRelocationTlsModuleId: break;
      case kRelocationRelative:
        if (entry.symbol() != 0) {
          return RelocationStatus::kInvalidRelativeSymbol;
        }
        break;
      default:
        unsupported_type = type;
        return RelocationStatus::kUnsupportedRelocation;
    }

    if (entry.offset >
        std::numeric_limits<std::uint64_t>::max() - load_bias) {
      return RelocationStatus::kTargetAddressOverflow;
    }
    const auto target = load_bias + entry.offset;
    if (!memory.IsMapped(target, sizeof(std::uint64_t))) {
      return RelocationStatus::kTargetNotMapped;
    }

    if (type == kRelocationTlsModuleId) {
      if (tls_module_id == 0) {
        return RelocationStatus::kMissingTlsModuleId;
      }
      PlannedRelocation relocation;
      relocation.address = target;
      for (std::size_t index = 0; index < relocation.value.size(); ++index) {
        relocation.value[index] = static_cast<std::byte>(
            (tls_module_id >> (index * 8U)) & 0xffU);
      }
      planned.push_back(relocation);
      continue;
    }

    if (type == kRelocationAbsolute64 || type == kRelocationGlobDat ||
        type == kRelocationJumpSlot) {
      if (resolver == nullptr) {
        UnresolvedImport unresolved;
        unresolved.symbol_index = entry.symbol();
        unresolved.relocation_type = type;
        unresolved.target_address = target;
        if (entry.symbol() < metadata.dynamic_info.symbols.size()) {
          unresolved.symbol =
              metadata.dynamic_info.symbols[entry.symbol()].name;
        }
        unresolved_imports.push_back(std::move(unresolved));
        continue;
      }
      if (entry.symbol() >= metadata.dynamic_info.symbols.size()) {
        return RelocationStatus::kInvalidSymbolIndex;
      }
      const auto& symbol = metadata.dynamic_info.symbols[entry.symbol()];
      std::optional<std::uint64_t> resolved;
      bool resolved_import = false;
      if (entry.symbol() == 0) {
        resolved = 0;
      } else if (symbol.section_index != kUndefinedSectionIndex) {
        if (symbol.value >
            std::numeric_limits<std::uint64_t>::max() - load_bias) {
          return RelocationStatus::kInvalidResolvedAddress;
        }
        resolved = load_bias + symbol.value;
      } else if (symbol.binding() == kSymbolBindingWeak) {
        resolved = 0;
      } else {
        const auto reference = ParseImportReference(metadata, symbol.name);
        if (reference.nid.empty()) {
          return RelocationStatus::kEmptyImportSymbol;
        }
        if (!reference.valid) {
          unresolved_imports.push_back(
              {entry.symbol(), type, target, symbol.name});
          continue;
        }
        if (reference.library != nullptr) {
          const std::span<const std::string> library_scope(
              &reference.library->name, 1);
          resolved = resolver->ResolveImport(reference.nid, library_scope);
        } else {
          resolved = resolver->ResolveImport(
              reference.nid, metadata.dynamic_info.needed_libraries);
        }
        if (!resolved.has_value()) {
          unresolved_imports.push_back(
              {entry.symbol(), type, target, symbol.name});
          continue;
        }
        if (*resolved == 0) {
          return RelocationStatus::kInvalidResolvedAddress;
        }
        resolved_import = true;
      }
      PlannedRelocation relocation;
      relocation.address = target;
      const auto addend = type == kRelocationAbsolute64
                              ? std::bit_cast<std::uint64_t>(entry.addend)
                              : 0;
      const auto value = *resolved + addend;
      for (std::size_t index = 0; index < relocation.value.size(); ++index) {
        relocation.value[index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
      }
      planned.push_back(relocation);
      if (resolved_import) {
        ++resolved_import_count;
      }
      continue;
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

RelocationResult ApplyRelocationsInternal(const ElfMetadata& metadata,
                                          memory::GuestMemory& memory,
                                          const ImportResolver* resolver,
                                          std::uint64_t load_bias,
                                          std::uint64_t tls_module_id) {
  std::vector<PlannedRelocation> planned;
  planned.reserve(metadata.dynamic_info.relocations.size() +
                  metadata.dynamic_info.plt_relocations.size());
  std::size_t resolved_import_count = 0;
  std::vector<UnresolvedImport> unresolved_imports;
  std::optional<std::uint32_t> unsupported_type;

  auto status = PlanTable(metadata.dynamic_info.relocations, metadata, memory,
                          resolver, load_bias, tls_module_id, planned,
                          resolved_import_count, unresolved_imports,
                          unsupported_type);
  if (status == RelocationStatus::kOk) {
    status = PlanTable(metadata.dynamic_info.plt_relocations, metadata, memory,
                       resolver, load_bias, tls_module_id, planned,
                       resolved_import_count, unresolved_imports,
                       unsupported_type);
  }
  if (status != RelocationStatus::kOk) {
    RelocationResult result{status, 0, unresolved_imports.size(),
                            resolved_import_count,
                            std::move(unresolved_imports)};
    result.unsupported_relocation_type = unsupported_type;
    return result;
  }

  std::size_t applied_count = 0;
  for (const auto& relocation : planned) {
    if (!memory.Initialize(relocation.address, relocation.value)) {
      return {RelocationStatus::kWriteFailed, applied_count,
              unresolved_imports.size(), resolved_import_count,
              std::move(unresolved_imports)};
    }
    ++applied_count;
  }
  return {RelocationStatus::kOk, applied_count, unresolved_imports.size(),
          resolved_import_count, std::move(unresolved_imports)};
}

}  // namespace

RelocationResult ApplyRelativeRelocations(const ElfMetadata& metadata,
                                          memory::GuestMemory& memory,
                                          std::uint64_t load_bias,
                                          std::uint64_t tls_module_id) {
  return ApplyRelocationsInternal(metadata, memory, nullptr, load_bias,
                                  tls_module_id);
}

RelocationResult ApplyRelocations(const ElfMetadata& metadata,
                                  memory::GuestMemory& memory,
                                  const ImportResolver& resolver,
                                  std::uint64_t load_bias,
                                  std::uint64_t tls_module_id) {
  return ApplyRelocationsInternal(metadata, memory, &resolver, load_bias,
                                  tls_module_id);
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
    case RelocationStatus::kInvalidSymbolIndex:
      return "invalid-symbol-index";
    case RelocationStatus::kEmptyImportSymbol:
      return "empty-import-symbol";
    case RelocationStatus::kInvalidResolvedAddress:
      return "invalid-resolved-address";
    case RelocationStatus::kMissingTlsModuleId:
      return "missing-tls-module-id";
  }
  return "unknown";
}

}  // namespace kajps5::loader
