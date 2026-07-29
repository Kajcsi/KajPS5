// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/elf_trace.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace kajps5::loader {
namespace {

constexpr std::uint32_t kProgramTypeLoad = 1;

std::string_view AbiName(std::uint8_t os_abi) noexcept {
  switch (os_abi) {
    case 0: return "system-v";
    case 9: return "freebsd";
    default: return "unknown";
  }
}

std::string SegmentFlags(std::uint32_t flags) {
  std::string result;
  result += (flags & 4U) != 0 ? 'r' : '-';
  result += (flags & 2U) != 0 ? 'w' : '-';
  result += (flags & 1U) != 0 ? 'x' : '-';
  return result;
}

void WriteHex64(std::ostringstream& trace, std::uint64_t value) {
  trace << "0x" << std::hex << std::setfill('0') << std::setw(16) << value
        << std::dec;
}

}  // namespace

std::string FormatElfTrace(const ElfMetadata& metadata) {
  std::ostringstream trace;
  trace << "elf.class=ELF64\n"
        << "elf.endian=little\n"
        << "elf.os_abi=" << AbiName(metadata.os_abi) << '\n'
        << "elf.abi_version=" << static_cast<unsigned>(metadata.abi_version)
        << '\n'
        << "elf.type=0x" << std::hex << std::setfill('0') << std::setw(4)
        << metadata.type << std::dec << '\n'
        << "elf.machine=" << metadata.machine << '\n';
  trace << "elf.entry=";
  WriteHex64(trace, metadata.entry_point);
  trace << '\n'
        << "elf.program_headers=" << metadata.program_headers.size() << '\n';

  std::size_t load_segment_count = 0;
  for (const auto& header : metadata.program_headers) {
    if (header.type == kProgramTypeLoad) {
      ++load_segment_count;
    }
  }
  trace << "elf.load_segments=" << load_segment_count << '\n';
  trace << "elf.dynamic_entries=" << metadata.dynamic_entries.size() << '\n';
  trace << "elf.dynamic_string_table_size="
        << metadata.dynamic_info.string_table_size.value_or(0) << '\n';
  trace << "elf.needed_libraries="
        << metadata.dynamic_info.needed_libraries.size() << '\n';
  trace << "elf.has_soname="
        << (metadata.dynamic_info.shared_object_name.has_value() ? 1 : 0)
        << '\n';
  trace << "elf.relocations=" << metadata.dynamic_info.relocations.size()
        << '\n';
  trace << "elf.plt_relocations="
        << metadata.dynamic_info.plt_relocations.size() << '\n';
  trace << "elf.symbols=" << metadata.dynamic_info.symbols.size() << '\n';
  const auto undefined_symbols = std::count_if(
      metadata.dynamic_info.symbols.begin(), metadata.dynamic_info.symbols.end(),
      [](const ElfSymbol& symbol) {
        return symbol.section_index == 0 && !symbol.name.empty();
      });
  trace << "elf.undefined_symbols=" << undefined_symbols << '\n';

  std::size_t load_index = 0;
  for (const auto& header : metadata.program_headers) {
    if (header.type != kProgramTypeLoad) {
      continue;
    }
    trace << "elf.load[" << load_index++ << "].flags="
          << SegmentFlags(header.flags) << " offset=";
    WriteHex64(trace, header.offset);
    trace << " virtual_address=";
    WriteHex64(trace, header.virtual_address);
    trace << " file_size=";
    WriteHex64(trace, header.file_size);
    trace << " memory_size=";
    WriteHex64(trace, header.memory_size);
    trace << " alignment=";
    WriteHex64(trace, header.alignment);
    trace << '\n';
  }

  return trace.str();
}

}  // namespace kajps5::loader
