// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/unresolved_import_stubs.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <vector>

#include "hle/call_context.h"
#include "loader/sce_symbol.h"

namespace kajps5::hle {
namespace {

constexpr std::uint8_t kElfSymbolTypeNone = 0;
constexpr std::uint8_t kElfSymbolTypeFunction = 2;

std::string EncodeHex(std::string_view value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(value.size() * 2);
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0xfU]);
  }
  return result;
}

}  // namespace

std::optional<std::size_t> UnresolvedImportStubStore::Add(std::string library,
                                                          std::string module,
                                                          std::string nid) {
  const auto key = std::pair<std::string, std::string>(library, nid);
  if (const auto found = indices_.find(key); found != indices_.end()) {
    return found->second;
  }
  if (records_.size() >= kMaximumUnresolvedImportStubs) {
    return std::nullopt;
  }
  const auto index = records_.size();
  UnresolvedImportStubRecord record;
  record.library = std::move(library);
  record.module_omitted_bytes =
      module.size() > kMaximumUnresolvedImportModuleLength
          ? module.size() - kMaximumUnresolvedImportModuleLength
          : 0;
  module.resize(std::min(module.size(), kMaximumUnresolvedImportModuleLength));
  record.module = std::move(module);
  record.nid = std::move(nid);
  records_.push_back(std::move(record));
  indices_.emplace(std::move(key), index);
  return index;
}

void UnresolvedImportStubStore::RecordCall(
    std::size_t index, const HleCallContext& context) noexcept {
  if (index >= records_.size()) {
    return;
  }
  auto& record = records_[index];
  const auto first_call = record.call_count == 0;
  if (record.call_count != std::numeric_limits<std::uint64_t>::max()) {
    ++record.call_count;
  }
  if (total_calls_ != std::numeric_limits<std::uint64_t>::max()) {
    ++total_calls_;
  }
  if (!first_call) {
    return;
  }
  for (std::size_t argument = 0; argument < record.first_arguments.size();
       ++argument) {
    record.first_arguments[argument] = context.Argument(argument).value_or(0);
  }
}

std::size_t UnresolvedImportStubStore::size() const noexcept {
  return records_.size();
}

std::uint64_t UnresolvedImportStubStore::total_calls() const noexcept {
  return total_calls_;
}

std::span<const UnresolvedImportStubRecord>
UnresolvedImportStubStore::records() const noexcept {
  return records_;
}

UnresolvedImportStubRegistrationResult RegisterUnresolvedImportStubs(
    const loader::ElfMetadata& metadata, ExportRegistry& registry,
    const ImportRegistry& data_registry, UnresolvedImportStubStore& store) {
  UnresolvedImportStubRegistrationResult result;
  const auto coverage = AnalyzeImportCoverage(metadata, registry, &data_registry);
  result.coverage_status = coverage.status;
  if (!coverage) {
    result.status = ExportRegistryStatus::kInvalidArgument;
    return result;
  }

  for (const auto& entry : coverage.imports) {
    if (entry.lookup_status == ExportRegistryStatus::kOk) {
      continue;
    }
    if (entry.lookup_status == ExportRegistryStatus::kAmbiguous ||
        entry.symbol_index >= metadata.dynamic_info.symbols.size()) {
      ++result.invalid_reference_count;
      continue;
    }
    const auto& symbol = metadata.dynamic_info.symbols[entry.symbol_index];
    // STT_NOTYPE is retained for compatibility with imports whose producers do
    // not emit STT_FUNC. All explicitly typed data and non-callable symbols
    // must remain unresolved rather than being registered as executable stubs.
    if (symbol.type() != kElfSymbolTypeNone &&
        symbol.type() != kElfSymbolTypeFunction) {
      continue;
    }
    const auto reference =
        loader::ResolveElfImportReference(metadata, symbol.name);
    if (!reference.valid || reference.nid.empty() ||
        reference.nid.size() > kMaximumExportSymbolLength) {
      ++result.invalid_reference_count;
      continue;
    }
    const std::string nid(reference.nid);
    const std::string module =
        reference.module != nullptr ? reference.module->name : std::string{};

    std::vector<std::string> libraries;
    if (reference.library != nullptr) {
      libraries.push_back(reference.library->name);
    } else {
      libraries = metadata.dynamic_info.needed_libraries;
    }
    if (libraries.empty()) {
      ++result.invalid_reference_count;
      continue;
    }

    for (const auto& library : libraries) {
      if (library.empty() || library.size() > kMaximumExportLibraryLength) {
        continue;
      }
      const std::array<std::string, 1> library_order = {library};
      if (registry.Lookup(nid, library_order).status ==
          ExportRegistryStatus::kOk) {
        continue;
      }
      const auto size_before = store.size();
      const auto index = store.Add(library, module, nid);
      if (!index.has_value()) {
        ++result.omitted_count;
        continue;
      }
      if (store.size() == size_before) {
        continue;
      }
      const auto status = registry.RegisterFallback(
          library, nid, [&store, stub_index = *index](HleCallContext& context) {
            store.RecordCall(stub_index, context);
            context.SetReturn(0);
            return HleContextStatus::kOk;
          });
      if (status == ExportRegistryStatus::kOk) {
        ++result.registered_count;
      } else if (status != ExportRegistryStatus::kAlreadyExists) {
        result.status = status;
        return result;
      }
    }
  }
  return result;
}

std::string FormatUnresolvedImportStubTrace(
    const UnresolvedImportStubStore& store, std::string_view prefix) {
  std::ostringstream trace;
  trace << prefix << "s=" << store.size() << '\n'
        << prefix << "_calls=" << store.total_calls() << '\n';
  const auto records = store.records();
  const auto detail_count =
      std::min(records.size(), kMaximumUnresolvedStubTraceRecords);
  trace << prefix << "_details=" << detail_count << '\n'
        << prefix << "_omitted=" << records.size() - detail_count << '\n';
  std::vector<std::size_t> record_indices(records.size());
  for (std::size_t index = 0; index < record_indices.size(); ++index) {
    record_indices[index] = index;
  }
  std::stable_sort(record_indices.begin(), record_indices.end(),
                   [&records](std::size_t left, std::size_t right) {
                     return records[left].call_count > records[right].call_count;
                   });
  for (std::size_t index = 0; index < detail_count; ++index) {
    const auto& record = records[record_indices[index]];
    trace << prefix << '[' << index << "].calls=" << record.call_count << '\n'
          << prefix << '[' << index << "].library_hex="
          << EncodeHex(record.library) << '\n'
          << prefix << '[' << index << "].module_hex="
          << EncodeHex(record.module) << '\n'
          << prefix << '[' << index << "].module_omitted_bytes="
          << record.module_omitted_bytes << '\n'
          << prefix << '[' << index << "].nid_hex=" << EncodeHex(record.nid)
          << '\n';
    std::string argument_bytes;
    argument_bytes.reserve(record.first_arguments.size() * sizeof(std::uint64_t));
    for (const auto argument : record.first_arguments) {
      for (std::size_t byte = 0; byte < sizeof(argument); ++byte) {
        argument_bytes.push_back(
            static_cast<char>((argument >> (byte * 8U)) & 0xffU));
      }
    }
    trace << prefix << '[' << index << "].args_hex="
          << EncodeHex(argument_bytes) << '\n';
  }
  return trace.str();
}

}  // namespace kajps5::hle
