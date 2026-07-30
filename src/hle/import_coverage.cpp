// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/import_coverage.h"

#include <algorithm>
#include <locale>
#include <map>
#include <sstream>
#include <span>
#include <string_view>
#include <utility>

#include "loader/sce_symbol.h"

namespace kajps5::hle {
namespace {

constexpr std::uint8_t kSymbolBindingWeak = 2;
constexpr std::uint16_t kUndefinedSectionIndex = 0;

constexpr bool IsImportRelocation(std::uint32_t type) noexcept {
  switch (type) {
    case 1:   // R_X86_64_64
    case 2:   // R_X86_64_PC32
    case 4:   // R_X86_64_PLT32
    case 6:   // R_X86_64_GLOB_DAT
    case 7:   // R_X86_64_JUMP_SLOT
    case 10:  // R_X86_64_32
    case 11:  // R_X86_64_32S
    case 24:  // R_X86_64_PC64
    case 32:  // R_X86_64_SIZE32
    case 33:  // R_X86_64_SIZE64
      return true;
    default: return false;
  }
}

std::string EncodeHex(std::string_view value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    encoded.push_back(digits[byte >> 4U]);
    encoded.push_back(digits[byte & 0x0fU]);
  }
  return encoded;
}

void WriteEncodedField(std::ostringstream& trace, std::string_view prefix,
                       std::string_view value) {
  const auto byte_count =
      std::min(value.size(), kMaximumImportCoverageSymbolBytes);
  trace << prefix << "_bytes=" << value.size() << '\n'
        << prefix << "_hex="
        << EncodeHex(value.substr(0, byte_count)) << '\n'
        << prefix << "_bytes_omitted=" << value.size() - byte_count << '\n';
}

bool AnalyzeTable(const std::vector<loader::ElfRelaEntry>& relocations,
                  const loader::ElfMetadata& metadata,
                  const ExportRegistry& registry,
                  const ImportRegistry* data_registry,
                  std::map<std::string, std::size_t>& import_indices,
                  ImportCoverageResult& result) {
  for (const auto& relocation : relocations) {
    if (!IsImportRelocation(relocation.type()) || relocation.symbol() == 0) {
      continue;
    }
    if (relocation.symbol() >= metadata.dynamic_info.symbols.size()) {
      result.status = ImportCoverageStatus::kInvalidSymbolIndex;
      return false;
    }

    const auto& symbol =
        metadata.dynamic_info.symbols[relocation.symbol()];
    if (symbol.section_index != kUndefinedSectionIndex ||
        symbol.binding() == kSymbolBindingWeak) {
      continue;
    }

    const auto reference =
        loader::ResolveElfImportReference(metadata, symbol.name);
    if (reference.nid.empty()) {
      result.status = ImportCoverageStatus::kEmptyImportSymbol;
      return false;
    }

    ++result.import_relocation_count;
    const auto existing = import_indices.find(symbol.name);
    if (existing != import_indices.end()) {
      auto& entry = result.imports[existing->second];
      ++entry.relocation_count;
      if (entry.lookup_status == ExportRegistryStatus::kOk) {
        ++result.resolved_relocation_count;
      } else {
        ++result.unresolved_relocation_count;
      }
      continue;
    }

    HleLookupResult lookup{ExportRegistryStatus::kInvalidArgument, {}};
    std::string requested_library;
    std::string requested_module;
    if (reference.valid) {
      if (reference.library != nullptr && reference.module != nullptr) {
        requested_library = reference.library->name;
        requested_module = reference.module->name;
        lookup = registry.Lookup(
            reference.nid,
            std::span<const std::string>(&requested_library, 1));
      } else {
        lookup = registry.Lookup(
            reference.nid, metadata.dynamic_info.needed_libraries);
        if (lookup) {
          requested_library = lookup.library;
        }
      }
      if (!lookup && data_registry != nullptr) {
        ImportLookupResult data_lookup;
        if (reference.library != nullptr && reference.module != nullptr) {
          data_lookup = data_registry->Resolve(
              reference.nid,
              std::span<const std::string>(&requested_library, 1));
        } else {
          data_lookup = data_registry->Resolve(
              reference.nid, metadata.dynamic_info.needed_libraries);
        }
        if (data_lookup) {
          lookup = {ExportRegistryStatus::kOk, data_lookup.library};
          if (requested_library.empty()) {
            requested_library = data_lookup.library;
          }
        }
      }
    }

    const auto new_index = result.imports.size();
    import_indices.emplace(symbol.name, new_index);
    result.imports.push_back(
        {relocation.symbol(), 1, symbol.name, std::move(requested_library),
         std::move(requested_module), lookup.status});
    ++result.unique_import_count;
    if (lookup) {
      ++result.resolved_relocation_count;
      ++result.resolved_unique_import_count;
    } else {
      ++result.unresolved_relocation_count;
      ++result.unresolved_unique_import_count;
    }
  }
  return true;
}

}  // namespace

ImportCoverageResult AnalyzeImportCoverage(
    const loader::ElfMetadata& metadata, const ExportRegistry& registry,
    const ImportRegistry* data_registry) {
  ImportCoverageResult result;
  result.available_export_count = registry.size();
  result.available_data_symbol_count =
      data_registry == nullptr ? 0 : data_registry->size();
  result.imports.reserve(metadata.dynamic_info.symbols.size());
  std::map<std::string, std::size_t> import_indices;
  if (!AnalyzeTable(metadata.dynamic_info.relocations, metadata, registry,
                    data_registry, import_indices, result)) {
    return result;
  }
  (void)AnalyzeTable(metadata.dynamic_info.plt_relocations, metadata, registry,
                     data_registry, import_indices, result);
  return result;
}

std::string FormatImportCoverageTrace(const ImportCoverageResult& result) {
  std::vector<const ImportCoverageEntry*> unresolved;
  unresolved.reserve(result.unresolved_unique_import_count);
  for (const auto& entry : result.imports) {
    if (entry.lookup_status != ExportRegistryStatus::kOk) {
      unresolved.push_back(&entry);
    }
  }
  std::stable_sort(
      unresolved.begin(), unresolved.end(),
      [](const ImportCoverageEntry* left, const ImportCoverageEntry* right) {
        return left->relocation_count > right->relocation_count;
      });
  const auto detail_count =
      std::min(unresolved.size(), kMaximumImportCoverageDetails);

  std::ostringstream trace;
  trace.imbue(std::locale::classic());
  trace << "hle.coverage.status=" << ImportCoverageStatusName(result.status)
        << '\n'
        << "hle.coverage.available_exports="
        << result.available_export_count << '\n'
        << "hle.coverage.available_data_symbols="
        << result.available_data_symbol_count << '\n'
        << "hle.coverage.import_relocations="
        << result.import_relocation_count << '\n'
        << "hle.coverage.resolved_relocations="
        << result.resolved_relocation_count << '\n'
        << "hle.coverage.unresolved_relocations="
        << result.unresolved_relocation_count << '\n'
        << "hle.coverage.unique_imports=" << result.unique_import_count
        << '\n'
        << "hle.coverage.resolved_unique_imports="
        << result.resolved_unique_import_count << '\n'
        << "hle.coverage.unresolved_unique_imports="
        << result.unresolved_unique_import_count << '\n'
        << "hle.coverage.unresolved_details=" << detail_count << '\n'
        << "hle.coverage.unresolved_omitted="
        << unresolved.size() - detail_count << '\n';

  for (std::size_t index = 0; index < detail_count; ++index) {
    const auto& entry = *unresolved[index];
    trace << "hle.coverage.unresolved[" << index
          << "].symbol_index=" << entry.symbol_index << '\n'
          << "hle.coverage.unresolved[" << index
          << "].references=" << entry.relocation_count << '\n'
          << "hle.coverage.unresolved[" << index << "].lookup="
          << ExportRegistryStatusName(entry.lookup_status) << '\n';
    const auto prefix =
        "hle.coverage.unresolved[" + std::to_string(index) + "]";
    WriteEncodedField(trace, prefix + ".symbol", entry.symbol);
    WriteEncodedField(trace, prefix + ".library", entry.requested_library);
    WriteEncodedField(trace, prefix + ".module", entry.requested_module);
  }
  return trace.str();
}

std::string_view ImportCoverageStatusName(
    ImportCoverageStatus status) noexcept {
  switch (status) {
    case ImportCoverageStatus::kOk: return "ok";
    case ImportCoverageStatus::kInvalidSymbolIndex:
      return "invalid-symbol-index";
    case ImportCoverageStatus::kEmptyImportSymbol:
      return "empty-import-symbol";
  }
  return "unknown";
}

}  // namespace kajps5::hle
