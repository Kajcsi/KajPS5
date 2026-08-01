// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/import_coverage.h"

#include <algorithm>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <span>
#include <string_view>
#include <utility>

#include "loader/sce_symbol.h"

namespace kajps5::hle {
namespace {

constexpr std::uint8_t kSymbolBindingWeak = 2;
constexpr std::uint16_t kUndefinedSectionIndex = 0;

struct ImportCoverageGroup {
  std::string library;
  std::string module;
  std::size_t unique_import_count = 0;
  std::size_t relocation_count = 0;
};

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

template <typename Lookup>
bool AnalyzeTable(const std::vector<loader::ElfRelaEntry>& relocations,
                  const loader::ElfMetadata& metadata, Lookup& lookup,
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

    std::string requested_library;
    std::string requested_module;
    const auto lookup_result =
        lookup(reference, requested_library, requested_module);

    const auto new_index = result.imports.size();
    import_indices.emplace(symbol.name, new_index);
    result.imports.push_back(
        {relocation.symbol(), 1, symbol.name, std::move(requested_library),
         std::move(requested_module), lookup_result.status});
    ++result.unique_import_count;
    if (lookup_result) {
      ++result.resolved_relocation_count;
      ++result.resolved_unique_import_count;
    } else {
      ++result.unresolved_relocation_count;
      ++result.unresolved_unique_import_count;
    }
  }
  return true;
}

template <typename Lookup>
ImportCoverageResult AnalyzeWithLookup(const loader::ElfMetadata& metadata,
                                       std::size_t available_export_count,
                                       std::size_t available_data_symbol_count,
                                       Lookup lookup) {
  ImportCoverageResult result;
  result.available_export_count = available_export_count;
  result.available_data_symbol_count = available_data_symbol_count;
  result.imports.reserve(metadata.dynamic_info.symbols.size());
  std::map<std::string, std::size_t> import_indices;
  if (!AnalyzeTable(metadata.dynamic_info.relocations, metadata, lookup,
                    import_indices, result)) {
    return result;
  }
  (void)AnalyzeTable(metadata.dynamic_info.plt_relocations, metadata, lookup,
                     import_indices, result);
  return result;
}

}  // namespace

ImportCoverageResult AnalyzeImportCoverage(
    const loader::ElfMetadata& metadata, const ExportRegistry& registry,
    const ImportRegistry* data_registry) {
  const auto lookup = [&metadata, &registry, data_registry](
                          const loader::ElfImportReference& reference,
                          std::string& requested_library,
                          std::string& requested_module) {
    HleLookupResult result{ExportRegistryStatus::kInvalidArgument, {}};
    if (!reference.valid) {
      return result;
    }
    if (reference.library != nullptr && reference.module != nullptr) {
      requested_library = reference.library->name;
      requested_module = reference.module->name;
      result = registry.Lookup(
          reference.nid, std::span<const std::string>(&requested_library, 1));
    } else {
      result = registry.Lookup(reference.nid,
                               metadata.dynamic_info.needed_libraries);
      if (result) {
        requested_library = result.library;
      }
    }
    if (result || data_registry == nullptr) {
      return result;
    }
    ImportLookupResult data_lookup;
    if (reference.library != nullptr && reference.module != nullptr) {
      data_lookup = data_registry->Resolve(
          reference.nid, std::span<const std::string>(&requested_library, 1));
    } else {
      data_lookup = data_registry->Resolve(
          reference.nid, metadata.dynamic_info.needed_libraries);
    }
    if (data_lookup) {
      result = {ExportRegistryStatus::kOk, data_lookup.library};
      if (requested_library.empty()) {
        requested_library = data_lookup.library;
      }
    }
    return result;
  };
  return AnalyzeWithLookup(metadata, registry.size(),
                           data_registry == nullptr ? 0 : data_registry->size(),
                           lookup);
}

ImportCoverageResult AnalyzeImportCoverage(
    const loader::ElfMetadata& metadata,
    const loader::ImportResolver& resolver) {
  const auto lookup = [&metadata, &resolver](
                          const loader::ElfImportReference& reference,
                          std::string& requested_library,
                          std::string& requested_module) {
    if (!reference.valid) {
      return HleLookupResult{ExportRegistryStatus::kInvalidArgument, {}};
    }
    std::optional<std::uint64_t> resolved;
    if (reference.library != nullptr && reference.module != nullptr) {
      requested_library = reference.library->name;
      requested_module = reference.module->name;
      resolved = resolver.ResolveScopedImport(reference.nid, *reference.library,
                                              *reference.module);
    } else {
      resolved = resolver.ResolveImport(reference.nid,
                                        metadata.dynamic_info.needed_libraries);
    }
    return HleLookupResult{
        resolved ? ExportRegistryStatus::kOk : ExportRegistryStatus::kNotFound,
        {}};
  };
  return AnalyzeWithLookup(metadata, 0, 0, lookup);
}

std::string FormatImportCoverageTrace(const ImportCoverageResult& result,
                                      std::string_view trace_prefix) {
  std::vector<const ImportCoverageEntry*> unresolved;
  std::map<std::pair<std::string, std::string>, ImportCoverageGroup> groups;
  unresolved.reserve(result.unresolved_unique_import_count);
  for (const auto& entry : result.imports) {
    if (entry.lookup_status != ExportRegistryStatus::kOk) {
      unresolved.push_back(&entry);
      const auto key =
          std::pair(entry.requested_library, entry.requested_module);
      auto [group, inserted] = groups.try_emplace(
          key,
          ImportCoverageGroup{entry.requested_library, entry.requested_module});
      (void)inserted;
      ++group->second.unique_import_count;
      group->second.relocation_count += entry.relocation_count;
    }
  }
  std::stable_sort(
      unresolved.begin(), unresolved.end(),
      [](const ImportCoverageEntry* left, const ImportCoverageEntry* right) {
        return left->relocation_count > right->relocation_count;
      });
  const auto detail_count =
      std::min(unresolved.size(), kMaximumImportCoverageDetails);
  std::vector<const ImportCoverageGroup*> ordered_groups;
  ordered_groups.reserve(groups.size());
  for (const auto& [key, group] : groups) {
    (void)key;
    ordered_groups.push_back(&group);
  }
  std::stable_sort(
      ordered_groups.begin(), ordered_groups.end(),
      [](const ImportCoverageGroup* left, const ImportCoverageGroup* right) {
        if (left->unique_import_count != right->unique_import_count) {
          return left->unique_import_count > right->unique_import_count;
        }
        if (left->relocation_count != right->relocation_count) {
          return left->relocation_count > right->relocation_count;
        }
        if (left->library != right->library) {
          return left->library < right->library;
        }
        return left->module < right->module;
      });
  const auto group_count =
      std::min(ordered_groups.size(), kMaximumImportCoverageGroups);

  std::ostringstream trace;
  trace.imbue(std::locale::classic());
  trace << trace_prefix << ".status=" << ImportCoverageStatusName(result.status)
        << '\n'
        << trace_prefix
        << ".available_exports=" << result.available_export_count << '\n'
        << trace_prefix
        << ".available_data_symbols=" << result.available_data_symbol_count
        << '\n'
        << trace_prefix
        << ".import_relocations=" << result.import_relocation_count << '\n'
        << trace_prefix
        << ".resolved_relocations=" << result.resolved_relocation_count << '\n'
        << trace_prefix
        << ".unresolved_relocations=" << result.unresolved_relocation_count
        << '\n'
        << trace_prefix << ".unique_imports=" << result.unique_import_count
        << '\n'
        << trace_prefix
        << ".resolved_unique_imports=" << result.resolved_unique_import_count
        << '\n'
        << trace_prefix << ".unresolved_unique_imports="
        << result.unresolved_unique_import_count << '\n'
        << trace_prefix << ".unresolved_details=" << detail_count << '\n'
        << trace_prefix
        << ".unresolved_omitted=" << unresolved.size() - detail_count << '\n'
        << trace_prefix << ".unresolved_groups=" << group_count << '\n'
        << trace_prefix
        << ".unresolved_groups_omitted=" << ordered_groups.size() - group_count
        << '\n';

  for (std::size_t index = 0; index < group_count; ++index) {
    const auto& group = *ordered_groups[index];
    const auto prefix = std::string(trace_prefix) + ".unresolved_group[" +
                        std::to_string(index) + "]";
    trace << prefix << ".unique_imports=" << group.unique_import_count << '\n'
          << prefix << ".relocations=" << group.relocation_count << '\n';
    WriteEncodedField(trace, prefix + ".library", group.library);
    WriteEncodedField(trace, prefix + ".module", group.module);
  }

  for (std::size_t index = 0; index < detail_count; ++index) {
    const auto& entry = *unresolved[index];
    trace << trace_prefix << ".unresolved[" << index
          << "].symbol_index=" << entry.symbol_index << '\n'
          << trace_prefix << ".unresolved[" << index
          << "].references=" << entry.relocation_count << '\n'
          << trace_prefix << ".unresolved[" << index
          << "].lookup=" << ExportRegistryStatusName(entry.lookup_status)
          << '\n';
    const auto prefix = std::string(trace_prefix) + ".unresolved[" +
                        std::to_string(index) + "]";
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
