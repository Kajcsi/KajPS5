// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <iostream>
#include <string>

#include "loader/relocation_trace.h"
#include "loader/relocator.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "relocation_trace_test: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  kajps5::loader::RelocationResult result;
  result.applied_count = 1;
  result.resolved_import_count = 2;
  result.unresolved_import_count = 2;
  result.unresolved_imports.push_back({1, 7, 0x1008, "open\n=?"});
  result.unresolved_imports.push_back({2, 6, 0x1010, std::string(1, '\x01')});

  const std::string expected =
      "relocation.status=ok\n"
      "relocation.applied=1\n"
      "relocation.resolved_imports=2\n"
      "relocation.unresolved_imports=2\n"
      "relocation.unresolved_details=2\n"
      "relocation.unresolved_omitted=0\n"
      "relocation.unresolved[0].symbol_index=1\n"
      "relocation.unresolved[0].type=7\n"
      "relocation.unresolved[0].target=0x0000000000001008\n"
      "relocation.unresolved[0].symbol_bytes=7\n"
      "relocation.unresolved[0].symbol_hex=6f70656e0a3d3f\n"
      "relocation.unresolved[0].symbol_bytes_omitted=0\n"
      "relocation.unresolved[1].symbol_index=2\n"
      "relocation.unresolved[1].type=6\n"
      "relocation.unresolved[1].target=0x0000000000001010\n"
      "relocation.unresolved[1].symbol_bytes=1\n"
      "relocation.unresolved[1].symbol_hex=01\n"
      "relocation.unresolved[1].symbol_bytes_omitted=0\n";
  Check(kajps5::loader::FormatRelocationTrace(result) == expected,
        "stable relocation trace changed");

  kajps5::loader::RelocationResult unsupported;
  unsupported.status =
      kajps5::loader::RelocationStatus::kUnsupportedRelocation;
  unsupported.unsupported_relocation_type = 24;
  Check(kajps5::loader::FormatRelocationTrace(unsupported).find(
            "relocation.unsupported_type=24\n") != std::string::npos,
        "unsupported relocation type is missing from the trace");

  kajps5::loader::RelocationResult bounded;
  bounded.unresolved_import_count =
      kajps5::loader::kMaximumRelocationTraceImports + 3;
  for (std::size_t index = 0; index < bounded.unresolved_import_count;
       ++index) {
    bounded.unresolved_imports.push_back(
        {static_cast<std::uint32_t>(index), 7, index, "symbol"});
  }
  const auto bounded_trace = kajps5::loader::FormatRelocationTrace(bounded);
  Check(bounded_trace.find("relocation.unresolved_details=32\n") !=
            std::string::npos &&
            bounded_trace.find("relocation.unresolved_omitted=3\n") !=
                std::string::npos &&
            bounded_trace.find("relocation.unresolved[32]") ==
                std::string::npos,
        "relocation trace detail cap failed");

  kajps5::loader::RelocationResult long_symbol;
  long_symbol.unresolved_import_count = 1;
  long_symbol.unresolved_imports.push_back(
      {1, 7, 0x1000,
       std::string(kajps5::loader::kMaximumRelocationTraceSymbolBytes + 3,
                   'a')});
  const auto long_symbol_trace =
      kajps5::loader::FormatRelocationTrace(long_symbol);
  Check(long_symbol_trace.find(".symbol_bytes=131\n") !=
            std::string::npos &&
            long_symbol_trace.find(".symbol_bytes_omitted=3\n") !=
                std::string::npos,
        "relocation trace symbol cap failed");
  return failures == 0 ? 0 : 1;
}
