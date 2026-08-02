// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp and src/graphics/shader/shader.h at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/Agc/AgcExports.cs at
// cf3bd0b4f2016eede08692110b6c14f08b5a912c.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/shader_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/memory/guest_memory.h"

namespace kajps5::gpu {
namespace {

constexpr std::size_t kShaderHeaderBytes = 0x60;
constexpr std::size_t kShaderFileHeaderOffset = 0x00;
constexpr std::size_t kShaderVersionOffset = 0x04;
constexpr std::size_t kShaderUserDataOffset = 0x08;
constexpr std::size_t kShaderCodeOffset = 0x10;
constexpr std::size_t kShaderCxRegistersOffset = 0x18;
constexpr std::size_t kShaderShRegistersOffset = 0x20;
constexpr std::size_t kShaderSpecialsOffset = 0x28;
constexpr std::size_t kShaderInputSemanticsOffset = 0x30;
constexpr std::size_t kShaderOutputSemanticsOffset = 0x38;
constexpr std::size_t kShaderSizeOffset = 0x44;
constexpr std::size_t kShaderInputSemanticsCountOffset = 0x50;
constexpr std::size_t kShaderTypeOffset = 0x5a;
constexpr std::size_t kShaderShRegisterCountOffset = 0x5c;
constexpr std::uint32_t kShaderFileHeader = 0x34333231U;
constexpr std::uint32_t kShaderVersion = 0x18U;
constexpr std::size_t kShaderUserDataBytes = 0x28;
constexpr std::size_t kShaderRegisterBytes = 8;
constexpr std::size_t kShaderRegisterValueOffset = 4;
constexpr std::size_t kShaderSemanticBytes = 4;

constexpr std::uint8_t kComputeShaderType = 0;
constexpr std::uint8_t kPixelShaderType = 1;
constexpr std::uint8_t kGeometryShaderType = 2;
constexpr std::uint8_t kHullShaderType = 3;
constexpr std::uint8_t kGeometryFrontShaderType = 4;
constexpr std::uint8_t kHullFrontShaderType = 5;
constexpr std::uint8_t kGeometryBackShaderType = 6;
constexpr std::uint8_t kHullBackShaderType = 7;
constexpr std::uint8_t kFetchShaderType = 8;

constexpr std::uint32_t kComputeProgramLo = 0x20c;
constexpr std::uint32_t kPixelProgramLo = 0x008;
constexpr std::uint32_t kVertexProgramLo = 0x048;
constexpr std::uint32_t kGeometryExportProgramLo = 0x0c8;
constexpr std::uint32_t kHullExportProgramLo = 0x148;
constexpr std::uint32_t kGeometryProgramLo = 0x088;
constexpr std::uint32_t kHullProgramLo = 0x108;
constexpr std::uint32_t kGeometryResource1 = 0x08a;
constexpr std::uint32_t kHullResource1 = 0x10a;

constexpr std::array kHeaderPointerOffsets = {
    kShaderUserDataOffset,       kShaderCxRegistersOffset,
    kShaderShRegistersOffset,    kShaderSpecialsOffset,
    kShaderInputSemanticsOffset, kShaderOutputSemanticsOffset,
};
constexpr std::array kUserDataPointerOffsets = {
    std::size_t{0x00}, std::size_t{0x08}, std::size_t{0x10},
    std::size_t{0x18}, std::size_t{0x20},
};

struct PlannedMutation {
  std::uint64_t address = 0;
  std::vector<std::byte> value;
  std::vector<std::byte> previous;
};

struct ShaderRegisterEntry {
  std::uint64_t address = 0;
  std::uint32_t offset = 0;
  std::uint32_t value = 0;
};

struct ProgramRegisterPair {
  std::uint32_t lo = 0;
  std::uint32_t hi = 0;
};

constexpr std::array kProgramRegisterPairs = {
    ProgramRegisterPair{kComputeProgramLo, kComputeProgramLo + 1U},
    ProgramRegisterPair{kPixelProgramLo, kPixelProgramLo + 1U},
    ProgramRegisterPair{kVertexProgramLo, kVertexProgramLo + 1U},
    ProgramRegisterPair{kGeometryExportProgramLo,
                        kGeometryExportProgramLo + 1U},
    ProgramRegisterPair{kGeometryProgramLo, kGeometryProgramLo + 1U},
    ProgramRegisterPair{kHullProgramLo, kHullProgramLo + 1U},
    ProgramRegisterPair{kHullExportProgramLo, kHullExportProgramLo + 1U},
};

struct SrtReaderContext {
  memory::GuestMemory& memory;
};

struct ShaderRuntimeTestFaultState {
  std::mutex mutex;
  ShaderRuntimeTestFault fault;
};

ShaderRuntimeTestFaultState& GetShaderRuntimeTestFaultState() noexcept {
  static ShaderRuntimeTestFaultState state;
  return state;
}

ShaderRuntimeTestFault SnapshotCreateShaderTestFault() noexcept {
  auto& state = GetShaderRuntimeTestFaultState();
  std::lock_guard lock(state.mutex);
  return state.fault;
}

bool AddAddress(std::uint64_t left, std::uint64_t right,
                std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

bool AddAddressSize(std::uint64_t left, std::size_t right,
                    std::uint64_t& result) noexcept {
  return AddAddress(left, static_cast<std::uint64_t>(right), result);
}

bool IsEncodableProgramAddress(std::uint64_t address) noexcept {
  // Program LO/HI encode bits 8..47. Low eight bits and the top sixteen bits
  // would be discarded by that representation.
  return (address & 0xffff0000000000ffULL) == 0;
}

bool MultiplyAddress(std::uint64_t left, std::uint64_t right,
                     std::uint64_t& result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

bool RangeEnd(std::uint64_t address, std::size_t size,
              std::uint64_t& end) noexcept {
  return AddAddressSize(address, size, end);
}

bool RangesOverlap(std::uint64_t first_address, std::size_t first_size,
                   std::uint64_t second_address,
                   std::size_t second_size) noexcept {
  std::uint64_t first_end = 0;
  std::uint64_t second_end = 0;
  return !RangeEnd(first_address, first_size, first_end) ||
                 !RangeEnd(second_address, second_size, second_end)
             ? true
             : first_address < second_end && second_address < first_end;
}

std::uint32_t Read32(std::span<const std::byte> bytes,
                     std::size_t offset = 0) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes,
                     std::size_t offset = 0) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::vector<std::byte> Encode32(std::uint32_t value) {
  std::vector<std::byte> bytes(sizeof(value));
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
  return bytes;
}

std::vector<std::byte> Encode64(std::uint64_t value) {
  std::vector<std::byte> bytes(sizeof(value));
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
  return bytes;
}

bool AddMutation(std::vector<PlannedMutation>& mutations,
                 std::uint64_t address, std::vector<std::byte> value) {
  if (value.empty()) {
    return false;
  }
  for (const auto& mutation : mutations) {
    if (RangesOverlap(address, value.size(), mutation.address,
                      mutation.value.size())) {
      return false;
    }
  }
  mutations.push_back({address, std::move(value), {}});
  return true;
}

std::optional<ProgramRegisterPair> ProgramPairForType(
    std::uint8_t binary_type) noexcept {
  switch (binary_type) {
    case kComputeShaderType:
      return ProgramRegisterPair{kComputeProgramLo, kComputeProgramLo + 1U};
    case kPixelShaderType:
      return ProgramRegisterPair{kPixelProgramLo, kPixelProgramLo + 1U};
    case kGeometryShaderType:
      return ProgramRegisterPair{kGeometryExportProgramLo,
                                 kGeometryExportProgramLo + 1U};
    case kHullShaderType:
      return ProgramRegisterPair{kHullExportProgramLo,
                                 kHullExportProgramLo + 1U};
    case kGeometryFrontShaderType:
    case kGeometryBackShaderType:
      return ProgramRegisterPair{kGeometryProgramLo, kGeometryProgramLo + 1U};
    case kHullFrontShaderType:
    case kHullBackShaderType:
      return ProgramRegisterPair{kHullProgramLo, kHullProgramLo + 1U};
    case kFetchShaderType:
      return std::nullopt;
  }
  return std::nullopt;
}

bool IsKnownShaderType(std::uint8_t binary_type) noexcept {
  return binary_type <= kFetchShaderType;
}

std::optional<ShaderType> RecompilerStageForType(
    std::uint8_t binary_type) noexcept {
  switch (binary_type) {
    case kComputeShaderType:
      return ShaderType::Compute;
    case kPixelShaderType:
      return ShaderType::Pixel;
    // A fused geometry-export image is the only non-CS/PS Gen5 image whose
    // current recompiler boundary has enough verified metadata to lower it.
    case kGeometryShaderType:
      return ShaderType::Vertex;
    default:
      return std::nullopt;
  }
}

bool IsExpectedFrontTableStart(std::uint8_t binary_type,
                               std::uint32_t first_offset) noexcept {
  return (binary_type == kHullFrontShaderType &&
          (first_offset == kHullResource1 || first_offset == kHullProgramLo)) ||
         (binary_type == kGeometryFrontShaderType &&
          (first_offset == kGeometryResource1 ||
           first_offset == kGeometryProgramLo));
}

bool ReadRegisterTable(memory::GuestMemory& memory,
                       std::uint64_t address, std::uint32_t count,
                       std::vector<ShaderRegisterEntry>& entries) {
  entries.clear();
  entries.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::uint64_t byte_offset = 0;
    std::uint64_t entry_address = 0;
    if (!MultiplyAddress(static_cast<std::uint64_t>(index),
                         kShaderRegisterBytes, byte_offset) ||
        !AddAddress(address, byte_offset, entry_address)) {
      return false;
    }
    std::array<std::byte, kShaderRegisterBytes> entry_bytes{};
    if (!memory.Read(entry_address, entry_bytes)) {
      return false;
    }
    entries.push_back(
        {entry_address, Read32(entry_bytes), Read32(entry_bytes, 4)});
  }
  return true;
}

std::optional<std::pair<const ShaderRegisterEntry*, const ShaderRegisterEntry*>>
FindPair(std::span<const ShaderRegisterEntry> entries,
         ProgramRegisterPair expected) noexcept {
  const ShaderRegisterEntry* lo = nullptr;
  const ShaderRegisterEntry* hi = nullptr;
  for (const auto& entry : entries) {
    if (entry.offset == expected.lo) {
      lo = &entry;
    }
    if (entry.offset == expected.hi) {
      hi = &entry;
    }
  }
  if (lo == nullptr || hi == nullptr) {
    return std::nullopt;
  }
  return std::pair{lo, hi};
}

std::optional<std::pair<const ShaderRegisterEntry*, const ShaderRegisterEntry*>>
FindFallbackProgramPair(std::span<const ShaderRegisterEntry> entries,
                         ProgramRegisterPair expected) noexcept {
  // The SharpEmu table scan gives precedence to the first valid program LO in
  // guest-table order, not to the order of our known-program-pair list.
  for (std::size_t lo_index = 0; lo_index < entries.size(); ++lo_index) {
    const auto& lo = entries[lo_index];
    for (const auto candidate : kProgramRegisterPairs) {
      if ((candidate.lo == expected.lo && candidate.hi == expected.hi) ||
          lo.offset != candidate.lo) {
        continue;
      }
      if (lo_index + 1U < entries.size() &&
          entries[lo_index + 1U].offset == candidate.hi) {
        return std::pair{&lo, &entries[lo_index + 1U]};
      }
      for (std::size_t hi_index = 0; hi_index < entries.size(); ++hi_index) {
        if (hi_index != lo_index && entries[hi_index].offset == candidate.hi) {
          return std::pair{&lo, &entries[hi_index]};
        }
      }
      // Program LO values are unique across the known pairs.
      break;
    }
  }
  return std::nullopt;
}

bool ReadGuestSrtDword(void* userdata, std::uint64_t address,
                       std::uint32_t* value) {
  if (userdata == nullptr || value == nullptr ||
      address > std::numeric_limits<std::uint64_t>::max() - 3U) {
    return false;
  }
  auto* const context = static_cast<SrtReaderContext*>(userdata);
  std::array<std::byte, sizeof(std::uint32_t)> bytes{};
  if (!context->memory.Read(address, bytes)) {
    return false;
  }
  *value = Read32(bytes);
  return true;
}

ShaderMapResult FailedMap(ShaderRuntimeStatus status) {
  return {status, {}};
}

}  // namespace

ShaderRuntime::ShaderRuntime(memory::GuestMemory& memory) noexcept
    : memory_(memory) {}

void ShaderRuntime::SetCreateShaderTestFaultForTesting(
    ShaderRuntimeTestFault fault) noexcept {
  auto& state = GetShaderRuntimeTestFaultState();
  std::lock_guard lock(state.mutex);
  state.fault = fault;
}

void ShaderRuntime::ClearCreateShaderTestFaultForTesting() noexcept {
  SetCreateShaderTestFaultForTesting({});
}

ShaderMapResult ShaderRuntime::CreateShader(std::uint64_t destination_address,
                                             std::uint64_t header_address,
                                             std::uint64_t code_address) {
  std::lock_guard lock(mutex_);
  try {
    const auto test_fault = SnapshotCreateShaderTestFault();
    if (header_address == 0 || code_address == 0 ||
        !IsEncodableProgramAddress(code_address)) {
      return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
    }

    std::array<std::byte, kShaderHeaderBytes> header{};
    if (!memory_.Read(header_address, header)) {
      return FailedMap(ShaderRuntimeStatus::kMemoryFault);
    }
    if (Read32(header, kShaderFileHeaderOffset) != kShaderFileHeader ||
        Read32(header, kShaderVersionOffset) != kShaderVersion) {
      return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
    }

    const auto code_size_bytes = Read32(header, kShaderSizeOffset);
    if (code_size_bytes == 0 ||
        code_size_bytes > kMaximumRegisteredShaderBytes ||
        code_size_bytes % sizeof(std::uint32_t) != 0U) {
      return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
    }
    if (!memory_.CanAccess(code_address, code_size_bytes,
                           memory::GuestMemoryProtection::kRead)) {
      return FailedMap(ShaderRuntimeStatus::kMemoryFault);
    }
    std::vector<std::byte> code_probe(code_size_bytes);
    if (!memory_.Read(code_address, code_probe)) {
      return FailedMap(ShaderRuntimeStatus::kMemoryFault);
    }

    const auto binary_type = std::to_integer<std::uint8_t>(
        header[kShaderTypeOffset]);
    if (!IsKnownShaderType(binary_type)) {
      return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
    }
    const auto input_semantics_count =
        Read32(header, kShaderInputSemanticsCountOffset);
    if (input_semantics_count > kMaximumRegisteredShaderInputSemantics) {
      return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
    }

    std::vector<PlannedMutation> mutations;
    mutations.reserve(kHeaderPointerOffsets.size() +
                      kUserDataPointerOffsets.size() + 3U);
    std::array<std::uint64_t, kHeaderPointerOffsets.size()> relocated{};
    for (std::size_t index = 0; index < kHeaderPointerOffsets.size(); ++index) {
      const auto offset = kHeaderPointerOffsets[index];
      std::uint64_t field_address = 0;
      if (!AddAddressSize(header_address, offset, field_address)) {
        return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
      }
      const auto relative = Read64(header, offset);
      if (relative == 0) {
        continue;
      }
      if (!AddAddress(field_address, relative, relocated[index]) ||
          !AddMutation(mutations, field_address, Encode64(relocated[index]))) {
        return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
      }
    }

    std::uint64_t code_field_address = 0;
    if (!AddAddressSize(header_address, kShaderCodeOffset, code_field_address) ||
        !AddMutation(mutations, code_field_address, Encode64(code_address))) {
      return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
    }

    const auto user_data_address = relocated[0];
    if (user_data_address != 0) {
      std::array<std::byte, kShaderUserDataBytes> user_data{};
      if (!memory_.Read(user_data_address, user_data)) {
        return FailedMap(ShaderRuntimeStatus::kMemoryFault);
      }
      for (const auto offset : kUserDataPointerOffsets) {
        std::uint64_t field_address = 0;
        if (!AddAddressSize(user_data_address, offset, field_address)) {
          return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
        }
        const auto relative = Read64(user_data, offset);
        if (relative == 0) {
          continue;
        }
        std::uint64_t relocated_pointer = 0;
        if (!AddAddress(field_address, relative, relocated_pointer) ||
            !AddMutation(mutations, field_address, Encode64(relocated_pointer))) {
          return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
        }
      }
    }

    const auto input_semantics_address = relocated[4];
    if (input_semantics_count != 0) {
      if (input_semantics_address == 0 ||
          !memory_.CanAccess(
              input_semantics_address,
              static_cast<std::uint64_t>(input_semantics_count) *
                  kShaderSemanticBytes,
              memory::GuestMemoryProtection::kRead)) {
        return FailedMap(ShaderRuntimeStatus::kMemoryFault);
      }
    }

    const auto register_count = static_cast<std::uint32_t>(
        std::to_integer<std::uint8_t>(header[kShaderShRegisterCountOffset]));
    const auto sh_registers_address = relocated[2];
    const auto expected_pair = ProgramPairForType(binary_type);
    if (expected_pair.has_value()) {
      if (sh_registers_address == 0 || register_count < 2U) {
        return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
      }
      std::vector<ShaderRegisterEntry> entries;
      if (!ReadRegisterTable(memory_, sh_registers_address, register_count,
                             entries)) {
        return FailedMap(ShaderRuntimeStatus::kMemoryFault);
      }
      auto found_pair = FindPair(entries, *expected_pair);
      if (!found_pair.has_value()) {
        found_pair = FindFallbackProgramPair(entries, *expected_pair);
      }
      // Known front-half tables may defer PGM publication only when neither
      // their preferred pair nor any valid fallback program pair is present.
      const auto skip_front_patch =
          !found_pair.has_value() && !entries.empty() &&
          IsExpectedFrontTableStart(binary_type, entries.front().offset);
      if (!found_pair.has_value() && !skip_front_patch) {
        return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
      }
      if (found_pair.has_value()) {
        std::uint64_t lo_value_address = 0;
        std::uint64_t hi_value_address = 0;
        const auto shader_offset =
            (static_cast<std::uint64_t>(found_pair->first->value) << 8U) |
            ((static_cast<std::uint64_t>(found_pair->second->value) & 0xffU)
             << 40U);
        std::uint64_t program_address = 0;
        if (!AddAddressSize(found_pair->first->address,
                            kShaderRegisterValueOffset, lo_value_address) ||
            !AddAddressSize(found_pair->second->address,
                            kShaderRegisterValueOffset, hi_value_address) ||
            !AddAddress(code_address, shader_offset, program_address) ||
            !IsEncodableProgramAddress(program_address) ||
            !AddMutation(
                mutations, lo_value_address,
                Encode32(static_cast<std::uint32_t>(program_address >> 8U))) ||
            !AddMutation(
                mutations, hi_value_address,
                Encode32((found_pair->second->value & 0xffffff00U) |
                         static_cast<std::uint32_t>((program_address >> 40U) &
                                                    0xffU)))) {
          return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
        }
      }
    }

    if (destination_address != 0 &&
        !AddMutation(mutations, destination_address, Encode64(header_address))) {
      return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
    }

    for (auto& mutation : mutations) {
      if (!memory_.CanAccess(mutation.address, mutation.value.size(),
                             memory::GuestMemoryProtection::kWrite)) {
        return FailedMap(ShaderRuntimeStatus::kMemoryFault);
      }
      mutation.previous.resize(mutation.value.size());
      if (!memory_.Read(mutation.address, mutation.previous)) {
        return FailedMap(ShaderRuntimeStatus::kMemoryFault);
      }
    }

    RegisteredShader record;
    record.code_address = code_address;
    record.header_address = header_address;
    record.code_size_bytes = code_size_bytes;
    record.binary_type = binary_type;
    record.user_data_address = user_data_address;
    record.input_semantics_address = input_semantics_address;
    record.input_semantics_count = input_semantics_count;
    // Finish all potentially allocating registry work before any guest write.
    // Successful publication is then a non-allocating swap after the complete
    // transaction has been applied.
    auto next_records = records_;
    next_records.insert_or_assign(code_address, record);

    if (test_fault.point ==
        ShaderRuntimeTestFaultPoint::kBeforeMutationCommitBadAlloc) {
      throw std::bad_alloc();
    }

    std::size_t written = 0;
    for (; written < mutations.size(); ++written) {
      const auto& mutation = mutations[written];
      if (test_fault.point ==
              ShaderRuntimeTestFaultPoint::
                  kFailMutationWriteAfterSuccessfulWrites &&
          written == test_fault.successful_mutation_writes) {
        break;
      }
      if (test_fault.point ==
              ShaderRuntimeTestFaultPoint::kPartiallyWriteMutationThenFail &&
          written == test_fault.successful_mutation_writes) {
        // Exercise the same partial-write condition GuestMemory can expose
        // across backing chunks before reporting a deterministic failure.
        const auto prefix_size = mutation.value.size() / 2U;
        if (prefix_size != 0U) {
          const std::span<const std::byte> prefix(mutation.value.data(),
                                                  prefix_size);
          (void)memory_.Write(mutation.address, prefix);
        }
        break;
      }
      if (!memory_.Write(mutation.address, mutation.value)) {
        break;
      }
    }
    if (written != mutations.size()) {
      // GuestMemory may have modified part of the attempted range before its
      // write reports failure, so restore it before previously completed
      // mutations. Each restoration is best effort in case the backing itself
      // remains faulting.
      const auto& attempted = mutations[written];
      (void)memory_.Write(attempted.address, attempted.previous);
      while (written != 0) {
        --written;
        const auto& mutation = mutations[written];
        (void)memory_.Write(mutation.address, mutation.previous);
      }
      return FailedMap(ShaderRuntimeStatus::kMemoryFault);
    }

    records_.swap(next_records);
    return {ShaderRuntimeStatus::kOk, record};
  } catch (const std::bad_alloc&) {
    return FailedMap(ShaderRuntimeStatus::kResourceLimit);
  } catch (...) {
    return FailedMap(ShaderRuntimeStatus::kInvalidArgument);
  }
}

std::optional<RegisteredShader> ShaderRuntime::Lookup(
    std::uint64_t code_address) const {
  std::lock_guard lock(mutex_);
  const auto found = records_.find(code_address);
  return found == records_.end() ? std::nullopt
                                 : std::optional<RegisteredShader>(found->second);
}

ShaderCompileResult ShaderRuntime::Recompile(
    std::uint64_t code_address,
    const shader::recompiler::CompileOptions& options,
    shader::recompiler::CompileResult& result) {
  std::lock_guard lock(mutex_);
  try {
    const auto found = records_.find(code_address);
    if (found == records_.end()) {
      return {ShaderRuntimeStatus::kNotRegistered,
              "shader code address is not registered"};
    }
    const auto& record = found->second;
    const auto expected_stage = RecompilerStageForType(record.binary_type);
    if (!expected_stage.has_value()) {
      return {ShaderRuntimeStatus::kUnsupportedShaderType,
              "registered shader type is not supported by the recompiler"};
    }
    if (options.stage != *expected_stage) {
      return {ShaderRuntimeStatus::kStageMismatch,
              "requested stage does not match the registered shader type"};
    }
    if (record.code_size_bytes == 0 ||
        record.code_size_bytes > kMaximumRegisteredShaderBytes ||
        record.code_size_bytes % sizeof(std::uint32_t) != 0U ||
        !memory_.CanAccess(record.code_address, record.code_size_bytes,
                           memory::GuestMemoryProtection::kRead)) {
      return {ShaderRuntimeStatus::kMemoryFault,
              "registered shader range is no longer readable"};
    }

    std::vector<std::byte> code_bytes(record.code_size_bytes);
    if (!memory_.Read(record.code_address, code_bytes)) {
      return {ShaderRuntimeStatus::kMemoryFault,
              "registered shader range could not be read"};
    }
    std::vector<std::uint32_t> code(code_bytes.size() /
                                    sizeof(std::uint32_t));
    for (std::size_t index = 0; index < code.size(); ++index) {
      code[index] = Read32(code_bytes, index * sizeof(std::uint32_t));
    }

    SrtReaderContext reader{memory_};
    auto effective_options = options;
    effective_options.stage = *expected_stage;
    effective_options.shader_base = record.code_address;
    effective_options.read_memory = ReadGuestSrtDword;
    effective_options.read_memory_data = &reader;

    shader::recompiler::CompileResult compiled;
    std::string error;
    if (!shader::recompiler::TryRecompile(code, effective_options, compiled,
                                          &error)) {
      return {ShaderRuntimeStatus::kCompilationFailed, std::move(error)};
    }
    result = std::move(compiled);
    return {};
  } catch (const std::bad_alloc&) {
    return {ShaderRuntimeStatus::kResourceLimit,
            "shader runtime allocation failed"};
  } catch (...) {
    return {ShaderRuntimeStatus::kCompilationFailed,
            "shader runtime compilation failed"};
  }
}

const char* ShaderRuntimeStatusName(ShaderRuntimeStatus status) noexcept {
  switch (status) {
    case ShaderRuntimeStatus::kOk:
      return "ok";
    case ShaderRuntimeStatus::kInvalidArgument:
      return "invalid-argument";
    case ShaderRuntimeStatus::kMemoryFault:
      return "memory-fault";
    case ShaderRuntimeStatus::kResourceLimit:
      return "resource-limit";
    case ShaderRuntimeStatus::kNotRegistered:
      return "not-registered";
    case ShaderRuntimeStatus::kUnsupportedShaderType:
      return "unsupported-shader-type";
    case ShaderRuntimeStatus::kStageMismatch:
      return "stage-mismatch";
    case ShaderRuntimeStatus::kCompilationFailed:
      return "compilation-failed";
  }
  return "unknown";
}

}  // namespace kajps5::gpu
