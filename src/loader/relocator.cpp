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

#include "loader/sce_symbol.h"

namespace kajps5::loader {
namespace {

constexpr std::uint32_t kRelocationNone = 0;
constexpr std::uint32_t kRelocationAbsolute64 = 1;
constexpr std::uint32_t kRelocationPc32 = 2;
constexpr std::uint32_t kRelocationPlt32 = 4;
constexpr std::uint32_t kRelocationGlobDat = 6;
constexpr std::uint32_t kRelocationJumpSlot = 7;
constexpr std::uint32_t kRelocationRelative = 8;
constexpr std::uint32_t kRelocationUnsigned32 = 10;
constexpr std::uint32_t kRelocationSigned32 = 11;
constexpr std::uint32_t kRelocationTlsModuleId = 16;
constexpr std::uint32_t kRelocationPc64 = 24;
constexpr std::uint32_t kRelocationSize32 = 32;
constexpr std::uint32_t kRelocationSize64 = 33;
constexpr std::uint32_t kRelocationRelative64 = 38;
constexpr std::uint8_t kSymbolBindingWeak = 2;
constexpr std::uint16_t kUndefinedSectionIndex = 0;

struct PlannedRelocation {
  std::uint64_t address = 0;
  std::array<std::byte, sizeof(std::uint64_t)> value{};
  std::size_t size = sizeof(std::uint64_t);
};

struct ImportReference {
  std::string_view nid;
  const ElfLibraryIdentity* library = nullptr;
  const ElfModuleIdentity* module = nullptr;
  bool valid = false;
};

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
    return {symbol, nullptr, nullptr, !symbol.empty()};
  }
  const auto parsed = ParseSceSymbolReference(symbol);
  if (!parsed.has_value()) {
    return {symbol.substr(0, first_separator), nullptr, nullptr, false};
  }

  const auto* library = FindIdentity(
      parsed->library_id, metadata.dynamic_info.import_libraries,
      metadata.dynamic_info.export_libraries);
  const auto* module = FindIdentity(
      parsed->module_id, metadata.dynamic_info.import_modules,
      metadata.dynamic_info.export_modules);
  return {parsed->nid, library, module,
          library != nullptr && module != nullptr};
}

constexpr bool IsSymbolRelocation(std::uint32_t type) noexcept {
  switch (type) {
    case kRelocationAbsolute64:
    case kRelocationPc32:
    case kRelocationPlt32:
    case kRelocationGlobDat:
    case kRelocationJumpSlot:
    case kRelocationUnsigned32:
    case kRelocationSigned32:
    case kRelocationPc64:
    case kRelocationSize32:
    case kRelocationSize64: return true;
    default: return false;
  }
}

constexpr bool IsPcRelativeRelocation(std::uint32_t type) noexcept {
  return type == kRelocationPc32 || type == kRelocationPlt32 ||
         type == kRelocationPc64;
}

constexpr bool IsSizeRelocation(std::uint32_t type) noexcept {
  return type == kRelocationSize32 || type == kRelocationSize64;
}

constexpr std::size_t RelocationWriteSize(std::uint32_t type) noexcept {
  switch (type) {
    case kRelocationPc32:
    case kRelocationPlt32:
    case kRelocationUnsigned32:
    case kRelocationSigned32:
    case kRelocationSize32: return sizeof(std::uint32_t);
    default: return sizeof(std::uint64_t);
  }
}

void EncodeLittleEndian(std::uint64_t value, PlannedRelocation& relocation) {
  for (std::size_t index = 0; index < relocation.size; ++index) {
    relocation.value[index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
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
      case kRelocationPc32:
      case kRelocationPlt32:
      case kRelocationGlobDat:
      case kRelocationJumpSlot:
      case kRelocationUnsigned32:
      case kRelocationSigned32:
      case kRelocationPc64:
      case kRelocationSize32:
      case kRelocationSize64: break;
      case kRelocationTlsModuleId: break;
      case kRelocationRelative:
      case kRelocationRelative64:
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
    const auto write_size = RelocationWriteSize(type);
    if (!memory.IsMapped(target, write_size)) {
      return RelocationStatus::kTargetNotMapped;
    }

    if (type == kRelocationTlsModuleId) {
      if (tls_module_id == 0) {
        return RelocationStatus::kMissingTlsModuleId;
      }
      PlannedRelocation relocation;
      relocation.address = target;
      EncodeLittleEndian(tls_module_id, relocation);
      planned.push_back(relocation);
      continue;
    }

    if (IsSymbolRelocation(type)) {
      const ElfSymbol* symbol = nullptr;
      if (entry.symbol() != 0) {
        if (entry.symbol() >= metadata.dynamic_info.symbols.size()) {
          return RelocationStatus::kInvalidSymbolIndex;
        }
        symbol = &metadata.dynamic_info.symbols[entry.symbol()];
      }

      std::optional<std::uint64_t> resolved;
      bool resolved_import = false;
      if (entry.symbol() == 0) {
        resolved = 0;
      } else if (symbol->section_index != kUndefinedSectionIndex) {
        if (IsSizeRelocation(type)) {
          resolved = symbol->size;
        } else {
          if (symbol->value >
              std::numeric_limits<std::uint64_t>::max() - load_bias) {
            return RelocationStatus::kInvalidResolvedAddress;
          }
          resolved = load_bias + symbol->value;
        }
      } else if (symbol->binding() == kSymbolBindingWeak) {
        resolved = IsSizeRelocation(type) ? symbol->size : 0;
      } else if (resolver == nullptr) {
        unresolved_imports.push_back(
            {entry.symbol(), type, target, symbol->name});
        continue;
      } else {
        const auto reference = ParseImportReference(metadata, symbol->name);
        if (reference.nid.empty()) {
          return RelocationStatus::kEmptyImportSymbol;
        }
        if (!reference.valid) {
          unresolved_imports.push_back(
              {entry.symbol(), type, target, symbol->name});
          continue;
        }
        if (reference.library != nullptr && reference.module != nullptr) {
          resolved = resolver->ResolveScopedImport(
              reference.nid, *reference.library, *reference.module);
        } else {
          resolved = resolver->ResolveImport(
              reference.nid, metadata.dynamic_info.needed_libraries);
        }
        if (!resolved.has_value()) {
          unresolved_imports.push_back(
              {entry.symbol(), type, target, symbol->name});
          continue;
        }
        if (*resolved == 0) {
          return RelocationStatus::kInvalidResolvedAddress;
        }
        resolved_import = true;
        if (IsSizeRelocation(type)) {
          resolved = symbol->size;
        }
      }
      if (!resolved.has_value()) {
        return RelocationStatus::kInvalidSymbolIndex;
      }

      PlannedRelocation relocation;
      relocation.address = target;
      relocation.size = write_size;
      const auto addend =
          type == kRelocationGlobDat || type == kRelocationJumpSlot
              ? 0
              : std::bit_cast<std::uint64_t>(entry.addend);
      auto value = *resolved + addend;
      if (IsPcRelativeRelocation(type)) {
        value -= target;
      }
      if (type == kRelocationPc32 || type == kRelocationPlt32 ||
          type == kRelocationSigned32) {
        const auto signed_value = std::bit_cast<std::int64_t>(value);
        if (signed_value < std::numeric_limits<std::int32_t>::min() ||
            signed_value > std::numeric_limits<std::int32_t>::max()) {
          return RelocationStatus::kRelocationValueOverflow;
        }
      } else if ((type == kRelocationUnsigned32 ||
                  type == kRelocationSize32) &&
                 value > std::numeric_limits<std::uint32_t>::max()) {
        return RelocationStatus::kRelocationValueOverflow;
      }
      EncodeLittleEndian(value, relocation);
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
    EncodeLittleEndian(value, relocation);
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
    if (!memory.Initialize(
            relocation.address,
            std::span(relocation.value).first(relocation.size))) {
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

std::optional<std::uint64_t> ImportResolver::ResolveScopedImport(
    std::string_view symbol, const ElfLibraryIdentity& library,
    const ElfModuleIdentity& module) const {
  (void)module;
  return ResolveImport(symbol,
                       std::span<const std::string>(&library.name, 1));
}

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
    case RelocationStatus::kRelocationValueOverflow:
      return "relocation-value-overflow";
  }
  return "unknown";
}

}  // namespace kajps5::loader
