// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/relocation_trace.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>

namespace kajps5::loader {
namespace {

void WriteHex64(std::ostringstream& trace, std::uint64_t value) {
  trace << "0x" << std::hex << std::setfill('0') << std::setw(16) << value
        << std::dec;
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

}  // namespace

std::string FormatRelocationTrace(const RelocationResult& result) {
  const auto detail_count =
      std::min(result.unresolved_imports.size(),
               kMaximumRelocationTraceImports);
  const auto omitted_count =
      result.unresolved_imports.size() - detail_count;

  std::ostringstream trace;
  trace.imbue(std::locale::classic());
  trace << "relocation.status=" << RelocationStatusName(result.status) << '\n'
        << "relocation.applied=" << result.applied_count << '\n'
        << "relocation.resolved_imports=" << result.resolved_import_count
        << '\n'
        << "relocation.unresolved_imports="
        << result.unresolved_import_count << '\n'
        << "relocation.unresolved_details=" << detail_count << '\n'
        << "relocation.unresolved_omitted=" << omitted_count << '\n';
  if (result.unsupported_relocation_type.has_value()) {
    trace << "relocation.unsupported_type="
          << *result.unsupported_relocation_type << '\n';
  }

  for (std::size_t index = 0; index < detail_count; ++index) {
    const auto& unresolved = result.unresolved_imports[index];
    const auto symbol_byte_count = std::min(
        unresolved.symbol.size(), kMaximumRelocationTraceSymbolBytes);
    const auto omitted_symbol_bytes =
        unresolved.symbol.size() - symbol_byte_count;
    trace << "relocation.unresolved[" << index
          << "].symbol_index=" << unresolved.symbol_index << '\n'
          << "relocation.unresolved[" << index
          << "].type=" << unresolved.relocation_type << '\n'
          << "relocation.unresolved[" << index << "].target=";
    WriteHex64(trace, unresolved.target_address);
    trace << '\n'
          << "relocation.unresolved[" << index
          << "].symbol_bytes=" << unresolved.symbol.size() << '\n'
          << "relocation.unresolved[" << index << "].symbol_hex="
          << EncodeHex(std::string_view(unresolved.symbol).substr(
                 0, symbol_byte_count))
          << '\n'
          << "relocation.unresolved[" << index
          << "].symbol_bytes_omitted=" << omitted_symbol_bytes << '\n';
  }
  return trace.str();
}

}  // namespace kajps5::loader
