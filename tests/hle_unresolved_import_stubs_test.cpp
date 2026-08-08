// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include "core/memory/guest_memory.h"
#include "hle/call_context.h"
#include "hle/export_registry.h"
#include "hle/import_registry.h"
#include "hle/unresolved_import_stubs.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_unresolved_import_stubs_test: " << message << '\n';
    ++failures;
  }
}

kajps5::loader::ElfMetadata MakeStubMetadata() {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.import_libraries.push_back(
      {0x1234, 0x0100, "libKernel"});
  metadata.dynamic_info.import_modules.push_back(
      {0x0040, 1, 2, "kernelModule"});
  metadata.dynamic_info.needed_libraries = {"libKernel"};
  metadata.dynamic_info.symbols.resize(5);
  metadata.dynamic_info.symbols[1].info = 0x10;
  metadata.dynamic_info.symbols[1].name = "known#BI0#BA";
  metadata.dynamic_info.symbols[2].info = 0x10;
  metadata.dynamic_info.symbols[2].name = "missing#BI0#BA";
  metadata.dynamic_info.symbols[3].info = 0x10;
  metadata.dynamic_info.symbols[3].name = "rawnid";
  metadata.dynamic_info.symbols[4].info = 0x10;
  metadata.dynamic_info.symbols[4].name = "bad\nnid#BI0#BA";
  metadata.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0},
      {0x1008, (std::uint64_t{2} << 32U) | 6U, 0},
      {0x1010, (std::uint64_t{3} << 32U) | 6U, 0},
      {0x1018, (std::uint64_t{4} << 32U) | 6U, 0},
  };
  return metadata;
}

}  // namespace

int main() {
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleCallContext;
  using kajps5::hle::HleContextStatus;
  using kajps5::hle::HleRegister;
  using kajps5::hle::ImportRegistry;
  using kajps5::hle::ImportCoverageStatus;
  using kajps5::hle::RegisterUnresolvedImportStubs;
  using kajps5::hle::UnresolvedImportStubStore;

std::size_t real_export_calls = 0;
  ExportRegistry registry;
  Check(registry.Register(
            "libKernel", "known",
            [&real_export_calls](HleCallContext&) {
              ++real_export_calls;
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk,
        "real export setup failed");

  UnresolvedImportStubStore store;
  ImportRegistry data_registry;
  const auto registered =
      RegisterUnresolvedImportStubs(MakeStubMetadata(), registry, data_registry,
                                    store);
  Check(registered && registered.registered_count == 3 &&
            registered.invalid_reference_count == 0 &&
            registered.omitted_count == 0 && store.size() == 3 &&
            store.total_calls() == 0 && real_export_calls == 0,
        "unresolved import stubs were not registered correctly");

  const std::string kernel_order[] = {"libKernel"};
  Check(registry.Lookup("missing", kernel_order).status ==
                ExportRegistryStatus::kOk &&
            registry.Lookup("rawnid", kernel_order).status ==
                ExportRegistryStatus::kOk &&
            registry.Lookup("bad\nnid", kernel_order).status ==
                ExportRegistryStatus::kOk,
        "stubbed imports are not callable");

  kajps5::memory::GuestMemory memory(0x10000, 0x1000,
                                     kajps5::memory::GuestMemoryProtection::
                                         kNone);
  HleCallContext context(memory);
  Check(context.SetRegister(HleRegister::kRdi, 0x111) &&
            context.SetRegister(HleRegister::kRsi, 0x222),
        "stub test argument setup failed");
  const auto dispatched = registry.Dispatch("missing", kernel_order, context);
  Check(dispatched && dispatched.handler_status == HleContextStatus::kOk &&
            context.GetRegister(HleRegister::kRax).value_or(1) == 0 &&
            real_export_calls == 0,
        "stub dispatch did not return a clean zero");
  Check(context.SetRegister(HleRegister::kRdi, 0x999),
        "stub test second argument setup failed");
  Check(static_cast<bool>(registry.Dispatch("missing", kernel_order, context)),
        "second stub dispatch failed");

  const auto records = store.records();
  Check(store.total_calls() == 2 && records.size() == 3 &&
            records[0].library == "libKernel" &&
            records[0].module == "kernelModule" &&
            records[0].nid == "missing" && records[0].call_count == 2 &&
            records[0].first_arguments[0] == 0x111 &&
            records[0].first_arguments[1] == 0x222 &&
            records[0].first_arguments[2] == 0,
        "stub call records are incorrect");

  const auto real_dispatch = registry.Dispatch("known", kernel_order, context);
  Check(real_dispatch && real_export_calls == 1 &&
            store.total_calls() == 2,
        "stub registration shadowed a real export");

  const auto deduplicated =
      RegisterUnresolvedImportStubs(MakeStubMetadata(), registry, data_registry,
                                    store);
  Check(deduplicated && deduplicated.registered_count == 0 &&
            store.size() == 3,
        "stub registration was not idempotent");

  auto data_metadata = MakeStubMetadata();
  data_metadata.dynamic_info.symbols.resize(2);
  data_metadata.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0}};
  ExportRegistry data_exports;
  ImportRegistry known_data;
  Check(known_data.Register("libKernel", "known", 0x8877665544332211) ==
            kajps5::hle::ImportRegistryStatus::kOk,
        "known HLE data setup failed");
  UnresolvedImportStubStore data_store;
  const auto data_result = RegisterUnresolvedImportStubs(
      data_metadata, data_exports, known_data, data_store);
  kajps5::memory::GuestMemory data_memory(
      0x1000, 0x1000,
      kajps5::memory::GuestMemoryProtection::kRead |
          kajps5::memory::GuestMemoryProtection::kWrite);
  const auto data_link = kajps5::loader::ApplyRelocations(
      data_metadata, data_memory, known_data);
  std::array<std::byte, sizeof(std::uint64_t)> data_value{};
  Check(data_result && data_result.registered_count == 0 &&
            data_store.size() == 0 && data_link &&
            data_memory.Read(0x1000, data_value) &&
            std::to_integer<unsigned char>(data_value[0]) == 0x11 &&
            std::to_integer<unsigned char>(data_value[7]) == 0x88,
        "known HLE data was stubbed instead of relocated to its data address");

  auto unknown_data_metadata = MakeStubMetadata();
  unknown_data_metadata.dynamic_info.symbols.resize(2);
  unknown_data_metadata.dynamic_info.symbols[1].info = 0x11;
  unknown_data_metadata.dynamic_info.symbols[1].name =
      "unknown_data#BI0#BA";
  unknown_data_metadata.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0}};
  ExportRegistry unknown_data_exports;
  ImportRegistry unknown_data_registry;
  UnresolvedImportStubStore unknown_data_store;
  const auto unknown_data_result = RegisterUnresolvedImportStubs(
      unknown_data_metadata, unknown_data_exports, unknown_data_registry,
      unknown_data_store);
  Check(unknown_data_result && unknown_data_result.registered_count == 0 &&
            unknown_data_result.invalid_reference_count == 0 &&
            unknown_data_store.size() == 0 &&
            unknown_data_exports.Lookup("unknown_data", kernel_order).status ==
                ExportRegistryStatus::kNotFound,
        "unknown object import was registered as an executable fallback stub");

  auto bounded_metadata = MakeStubMetadata();
  bounded_metadata.dynamic_info.symbols.resize(2);
  bounded_metadata.dynamic_info.symbols[1].name = "missing#BI0#BA";
  bounded_metadata.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0}};
  bounded_metadata.dynamic_info.import_modules[0].name =
      std::string(kajps5::hle::kMaximumUnresolvedImportModuleLength + 80, 'm');
  ExportRegistry bounded_registry;
  ImportRegistry bounded_data;
  UnresolvedImportStubStore bounded_store;
  const auto bounded = RegisterUnresolvedImportStubs(
      bounded_metadata, bounded_registry, bounded_data, bounded_store);
  const auto bounded_trace =
      kajps5::hle::FormatUnresolvedImportStubTrace(bounded_store);
  Check(bounded && bounded_store.size() == 1 &&
            bounded_store.records()[0].module.size() ==
                kajps5::hle::kMaximumUnresolvedImportModuleLength &&
            bounded_store.records()[0].module_omitted_bytes == 80 &&
            bounded_trace.size() < 2048 &&
            bounded_trace.find("module_omitted_bytes=80") != std::string::npos,
        "hostile module metadata produced an unbounded stub trace");

  const auto trace = kajps5::hle::FormatUnresolvedImportStubTrace(store);
  Check(trace.find("title.unresolved_import_stubs=3\n") !=
                std::string::npos &&
            trace.find("title.unresolved_import_stub_calls=2\n") !=
                std::string::npos &&
            trace.find("title.unresolved_import_stub_details=3\n") !=
                std::string::npos &&
            trace.find("title.unresolved_import_stub[0].calls=2\n") !=
                std::string::npos &&
            trace.find("library_hex=6c69624b65726e656c\n") !=
                std::string::npos &&
            trace.find("module_hex=6b65726e656c4d6f64756c65\n") !=
                std::string::npos &&
            trace.find("nid_hex=6d697373696e67\n") != std::string::npos &&
            trace.find("nid_hex=7261776e6964\n") != std::string::npos &&
            trace.find("nid_hex=6261640a6e6964\n") != std::string::npos &&
            trace.find("args_hex=1101000000000000"
                       "2202000000000000"
                       "0000000000000000"
                       "0000000000000000"
                       "0000000000000000"
                       "0000000000000000\n") != std::string::npos &&
            trace.find("missing#") == std::string::npos &&
            trace.find("rawnid\n") == std::string::npos &&
            trace.find("bad\nnid") == std::string::npos,
        "stub trace is incomplete or contains raw guest text");

  auto invalid_scope = MakeStubMetadata();
  invalid_scope.dynamic_info.symbols.resize(2);
  invalid_scope.dynamic_info.symbols[1].name = "missing#A#BA";
  invalid_scope.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0}};
  ExportRegistry invalid_registry;
  ImportRegistry invalid_data_registry;
  UnresolvedImportStubStore invalid_store;
  const auto invalid = RegisterUnresolvedImportStubs(
      invalid_scope, invalid_registry, invalid_data_registry, invalid_store);
  Check(invalid && invalid.registered_count == 0 &&
            invalid.invalid_reference_count == 1 &&
            invalid_store.size() == 0,
        "malformed import scope was stubbed");

  auto empty_symbol = MakeStubMetadata();
  empty_symbol.dynamic_info.symbols.resize(2);
  empty_symbol.dynamic_info.symbols[1].name = "#BI0#BA";
  empty_symbol.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0}};
  ExportRegistry empty_registry;
  ImportRegistry empty_data_registry;
  UnresolvedImportStubStore empty_store;
  const auto empty = RegisterUnresolvedImportStubs(empty_symbol,
                                                   empty_registry,
                                                   empty_data_registry,
                                                   empty_store);
  Check(!empty && empty.status == ExportRegistryStatus::kInvalidArgument &&
            empty.coverage_status == ImportCoverageStatus::kEmptyImportSymbol,
        "empty import symbol was stubbed");

  std::cout << "hle unresolved import stub tests passed\n";
  return failures == 0 ? 0 : 1;
}
