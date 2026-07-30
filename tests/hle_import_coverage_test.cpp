// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include "hle/export_registry.h"
#include "hle/import_coverage.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "hle_import_coverage_test: " << message << '\n';
    ++failures;
  }
}

kajps5::loader::ElfMetadata MakeCoverageMetadata() {
  kajps5::loader::ElfMetadata metadata;
  metadata.dynamic_info.import_libraries.push_back(
      {0x1234, 0x0100, "libKernel"});
  metadata.dynamic_info.import_modules.push_back(
      {0x0040, 1, 2, "kernelModule"});
  metadata.dynamic_info.symbols.resize(4);
  metadata.dynamic_info.symbols[1].info = 0x10;
  metadata.dynamic_info.symbols[1].name = "known#BI0#BA";
  metadata.dynamic_info.symbols[2].info = 0x10;
  metadata.dynamic_info.symbols[2].name = "missing#BI0#BA";
  metadata.dynamic_info.symbols[3].info = 0x20;
  metadata.dynamic_info.symbols[3].name = "optional#BI0#BA";
  metadata.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{1} << 32U) | 6U, 0},
      {0x1008, (std::uint64_t{1} << 32U) | 1U, 0},
      {0x1010, (std::uint64_t{2} << 32U) | 6U, 0},
      {0x1018, (std::uint64_t{3} << 32U) | 6U, 0},
  };
  return metadata;
}

}  // namespace

int main() {
  using kajps5::hle::AnalyzeImportCoverage;
  using kajps5::hle::ExportRegistry;
  using kajps5::hle::ExportRegistryStatus;
  using kajps5::hle::HleContextStatus;
  using kajps5::hle::ImportCoverageStatus;

  std::size_t dispatch_count = 0;
  ExportRegistry registry;
  Check(registry.Register(
            "libKernel", "known",
            [&dispatch_count](kajps5::hle::HleCallContext&) {
              ++dispatch_count;
              return HleContextStatus::kOk;
            }) == ExportRegistryStatus::kOk,
        "coverage export setup failed");

  const auto coverage = AnalyzeImportCoverage(MakeCoverageMetadata(), registry);
  Check(coverage && coverage.available_export_count == 1 &&
            coverage.import_relocation_count == 3 &&
            coverage.resolved_relocation_count == 2 &&
            coverage.unresolved_relocation_count == 1 &&
            coverage.unique_import_count == 2 &&
            coverage.resolved_unique_import_count == 1 &&
            coverage.unresolved_unique_import_count == 1 &&
            coverage.imports.size() == 2 && dispatch_count == 0,
        "coverage counts are incorrect or a handler was called");
  Check(coverage.imports[0].symbol_index == 1 &&
            coverage.imports[0].relocation_count == 2 &&
            coverage.imports[0].requested_library == "libKernel" &&
            coverage.imports[0].requested_module == "kernelModule" &&
            coverage.imports[0].lookup_status == ExportRegistryStatus::kOk,
        "resolved import coverage detail is incorrect");
  Check(coverage.imports[1].lookup_status ==
            ExportRegistryStatus::kNotFound,
        "missing import coverage detail is incorrect");

  const auto trace = kajps5::hle::FormatImportCoverageTrace(coverage);
  Check(trace.find("hle.coverage.status=ok\n") != std::string::npos &&
            trace.find("hle.coverage.resolved_relocations=2\n") !=
                std::string::npos &&
            trace.find("hle.coverage.unresolved_unique_imports=1\n") !=
                std::string::npos &&
            trace.find("symbol_hex=6d697373696e6723424930234241\n") !=
                std::string::npos &&
            trace.find("library_hex=6c69624b65726e656c\n") !=
                std::string::npos &&
            trace.find("module_hex=6b65726e656c4d6f64756c65\n") !=
                std::string::npos &&
            trace.find("known") == std::string::npos &&
            trace.find("missing#") == std::string::npos,
        "coverage trace is incomplete or contains raw guest text");

  auto malformed_scope = MakeCoverageMetadata();
  malformed_scope.dynamic_info.symbols[1].name = "known#A#BA";
  malformed_scope.dynamic_info.relocations.resize(1);
  const auto malformed = AnalyzeImportCoverage(malformed_scope, registry);
  Check(malformed && malformed.unresolved_unique_import_count == 1 &&
            malformed.imports[0].lookup_status ==
                ExportRegistryStatus::kInvalidArgument,
        "malformed import scope was treated as covered");

  auto invalid_index = MakeCoverageMetadata();
  invalid_index.dynamic_info.relocations = {
      {0x1000, (std::uint64_t{9} << 32U) | 6U, 0}};
  Check(AnalyzeImportCoverage(invalid_index, registry).status ==
            ImportCoverageStatus::kInvalidSymbolIndex,
        "invalid coverage symbol index was accepted");

  auto empty_symbol = MakeCoverageMetadata();
  empty_symbol.dynamic_info.symbols[1].name = "#BI0#BA";
  empty_symbol.dynamic_info.relocations.resize(1);
  Check(AnalyzeImportCoverage(empty_symbol, registry).status ==
            ImportCoverageStatus::kEmptyImportSymbol,
        "empty coverage import symbol was accepted");

  kajps5::hle::ImportCoverageResult bounded;
  bounded.unresolved_unique_import_count =
      kajps5::hle::kMaximumImportCoverageDetails + 2;
  for (std::size_t index = 0;
       index < bounded.unresolved_unique_import_count; ++index) {
    bounded.imports.push_back(
        {static_cast<std::uint32_t>(index), index + 1,
         std::string(140, 'x'), {}, {},
         ExportRegistryStatus::kNotFound});
  }
  const auto bounded_trace =
      kajps5::hle::FormatImportCoverageTrace(bounded);
  Check(bounded_trace.find("hle.coverage.unresolved_details=32\n") !=
            std::string::npos &&
            bounded_trace.find("hle.coverage.unresolved_omitted=2\n") !=
                std::string::npos &&
            bounded_trace.find("hle.coverage.unresolved[0].references=34\n") !=
                std::string::npos &&
            bounded_trace.find("symbol_bytes_omitted=12\n") !=
                std::string::npos,
        "coverage detail bounds are incorrect");

  Check(kajps5::hle::ImportCoverageStatusName(
            ImportCoverageStatus::kEmptyImportSymbol) ==
            "empty-import-symbol",
        "coverage status name is unstable");
  return failures == 0 ? 0 : 1;
}
