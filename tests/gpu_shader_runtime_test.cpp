// Copyright (C) 2026 KajPS5 contributors
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/Agc/AgcExports.cs at
// cf3bd0b4f2016eede08692110b6c14f08b5a912c.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "gpu/shader/recompiler/ShaderRecompiler.h"

namespace {

constexpr std::uint64_t kBase = 0x1000000;
constexpr std::uint64_t kMemorySize = 0x10000;
constexpr std::uint64_t kHeader = kBase + 0x1000;
constexpr std::uint64_t kCode = kBase + 0x1800;
constexpr std::uint64_t kUserData = kBase + 0x2000;
constexpr std::uint64_t kCxRegisters = kBase + 0x2800;
constexpr std::uint64_t kShRegisters = kBase + 0x3000;
constexpr std::uint64_t kSpecials = kBase + 0x3800;
constexpr std::uint64_t kInputSemantics = kBase + 0x4000;
constexpr std::uint64_t kOutputSemantics = kBase + 0x4800;
constexpr std::uint64_t kPointerTargets = kBase + 0x5000;
constexpr std::uint64_t kDestination = kBase + 0x7000;
constexpr std::uint32_t kFileHeader = 0x34333231U;
constexpr std::uint32_t kVersion = 0x18U;
constexpr std::uint32_t kEndProgram = 0xbf810000U;
constexpr std::uint32_t kSpirvMagic = 0x07230203U;

constexpr std::size_t kHeaderBytes = 0x60;
constexpr std::size_t kUserDataBytes = 0x28;
constexpr std::size_t kUserDataOffset = 0x08;
constexpr std::size_t kCodeOffset = 0x10;
constexpr std::size_t kCxRegistersOffset = 0x18;
constexpr std::size_t kShRegistersOffset = 0x20;
constexpr std::size_t kSpecialsOffset = 0x28;
constexpr std::size_t kInputSemanticsOffset = 0x30;
constexpr std::size_t kOutputSemanticsOffset = 0x38;
constexpr std::size_t kShaderSizeOffset = 0x44;
constexpr std::size_t kInputSemanticsCountOffset = 0x50;
constexpr std::size_t kShaderTypeOffset = 0x5a;
constexpr std::size_t kShRegisterCountOffset = 0x5c;

struct RegisterWord {
  std::uint32_t offset = 0;
  std::uint32_t value = 0;
};

struct Fixture {
  kajps5::memory::GuestMemory memory{
      kBase, kMemorySize,
      kajps5::memory::GuestMemoryProtection::kRead |
          kajps5::memory::GuestMemoryProtection::kWrite};
  kajps5::gpu::GpuRuntime runtime{memory};
};

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_shader_runtime_test: " << message << '\n';
    std::exit(1);
  }
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

std::uint32_t Read32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Relative(std::uint64_t field_address, std::uint64_t target) {
  Check(target >= field_address, "fixture pointer target precedes field");
  return target - field_address;
}

void WriteGuest64(kajps5::memory::GuestMemory& memory, std::uint64_t address,
                  std::uint64_t value) {
  std::array<std::byte, sizeof(value)> bytes{};
  Write64(bytes, 0, value);
  Check(memory.Write(address, bytes), "guest 64-bit write failed");
}

void WriteGuest32(kajps5::memory::GuestMemory& memory, std::uint64_t address,
                  std::uint32_t value) {
  std::array<std::byte, sizeof(value)> bytes{};
  Write32(bytes, 0, value);
  Check(memory.Write(address, bytes), "guest 32-bit write failed");
}

std::vector<std::byte> Snapshot(const Fixture& fixture) {
  std::vector<std::byte> bytes(kMemorySize);
  Check(fixture.memory.Read(kBase, bytes), "fixture snapshot failed");
  return bytes;
}

std::vector<std::byte> SnapshotMemory(
    const kajps5::memory::GuestMemory& memory, std::uint64_t base,
    std::size_t size) {
  std::vector<std::byte> bytes(size);
  Check(memory.Read(base, bytes), "guest-memory snapshot failed");
  return bytes;
}

void CheckUnchanged(const Fixture& fixture, std::span<const std::byte> before,
                    std::string_view message) {
  const auto after = Snapshot(fixture);
  Check(after.size() == before.size() &&
            std::equal(after.begin(), after.end(), before.begin()),
        message);
}

void ConfigureShader(Fixture& fixture, std::uint8_t binary_type,
                     std::span<const RegisterWord> registers,
                     std::uint32_t code_word = kEndProgram) {
  std::array<std::byte, kHeaderBytes> header{};
  Write32(header, 0, kFileHeader);
  Write32(header, 4, kVersion);
  Write64(header, kUserDataOffset,
          Relative(kHeader + kUserDataOffset, kUserData));
  Write64(header, kCxRegistersOffset,
          Relative(kHeader + kCxRegistersOffset, kCxRegisters));
  Write64(header, kShRegistersOffset,
          Relative(kHeader + kShRegistersOffset, kShRegisters));
  Write64(header, kSpecialsOffset,
          Relative(kHeader + kSpecialsOffset, kSpecials));
  Write64(header, kInputSemanticsOffset,
          Relative(kHeader + kInputSemanticsOffset, kInputSemantics));
  Write64(header, kOutputSemanticsOffset,
          Relative(kHeader + kOutputSemanticsOffset, kOutputSemantics));
  Write32(header, kShaderSizeOffset, sizeof(code_word));
  Write32(header, kInputSemanticsCountOffset, 1);
  header[kShaderTypeOffset] = static_cast<std::byte>(binary_type);
  header[kShRegisterCountOffset] =
      static_cast<std::byte>(registers.size());
  Check(fixture.memory.Write(kHeader, header), "shader header write failed");

  std::array<std::byte, kUserDataBytes> user_data{};
  for (std::size_t offset = 0; offset < user_data.size();
       offset += sizeof(std::uint64_t)) {
    const auto field_address = kUserData + offset;
    const auto target = kPointerTargets + offset * 8U;
    Write64(user_data, offset, Relative(field_address, target));
  }
  Check(fixture.memory.Write(kUserData, user_data),
        "shader user-data write failed");

  std::array<std::byte, sizeof(std::uint32_t)> input_semantics{};
  Check(fixture.memory.Write(kInputSemantics, input_semantics),
        "shader input semantics write failed");

  std::vector<std::byte> register_bytes(registers.size() * 8U);
  for (std::size_t index = 0; index < registers.size(); ++index) {
    Write32(register_bytes, index * 8U, registers[index].offset);
    Write32(register_bytes, index * 8U + 4U, registers[index].value);
  }
  Check(fixture.memory.Write(kShRegisters, register_bytes),
        "shader register table write failed");
  WriteGuest32(fixture.memory, kCode, code_word);
  WriteGuest64(fixture.memory, kDestination, 0xd1d2d3d4d5d6d7d8ULL);
}

void ConfigureMinimalComputeShader(
    kajps5::memory::GuestMemory& memory, std::uint64_t header_address,
    std::uint64_t code_address, std::uint64_t register_address,
    std::span<const RegisterWord> registers) {
  std::array<std::byte, kHeaderBytes> header{};
  Write32(header, 0, kFileHeader);
  Write32(header, 4, kVersion);
  Write64(header, kShRegistersOffset,
          Relative(header_address + kShRegistersOffset, register_address));
  Write32(header, kShaderSizeOffset, sizeof(std::uint32_t));
  header[kShaderTypeOffset] = std::byte{0};
  header[kShRegisterCountOffset] =
      static_cast<std::byte>(registers.size());
  Check(memory.Write(header_address, header),
        "minimal shader header write failed");

  std::vector<std::byte> register_bytes(registers.size() * 8U);
  for (std::size_t index = 0; index < registers.size(); ++index) {
    Write32(register_bytes, index * 8U, registers[index].offset);
    Write32(register_bytes, index * 8U + 4U, registers[index].value);
  }
  Check(memory.Write(register_address, register_bytes),
        "minimal shader registers write failed");
  WriteGuest32(memory, code_address, kEndProgram);
}

std::array<std::byte, kHeaderBytes> ReadHeader(const Fixture& fixture) {
  std::array<std::byte, kHeaderBytes> header{};
  Check(fixture.memory.Read(kHeader, header), "shader header read failed");
  return header;
}

std::array<std::byte, kUserDataBytes> ReadUserData(const Fixture& fixture) {
  std::array<std::byte, kUserDataBytes> user_data{};
  Check(fixture.memory.Read(kUserData, user_data), "shader user-data read failed");
  return user_data;
}

std::vector<std::byte> ReadRegisters(const Fixture& fixture,
                                     std::size_t count) {
  std::vector<std::byte> registers(count * 8U);
  Check(fixture.memory.Read(kShRegisters, registers),
        "shader register-table read failed");
  return registers;
}

void CheckCreateFailure(Fixture& fixture, std::uint64_t destination,
                        std::uint64_t code,
                        kajps5::gpu::ShaderRuntimeStatus expected,
                        std::string_view message) {
  const auto before = Snapshot(fixture);
  const auto mapped = fixture.runtime.CreateShader(destination, kHeader, code);
  Check(mapped.status == expected, message);
  CheckUnchanged(fixture, before, "failed mapping changed guest memory");
  Check(!fixture.runtime.LookupRegisteredShader(code).has_value(),
        "failed mapping published a registry record");
}

void CheckPatchedPair(const Fixture& fixture, std::uint32_t expected_lo,
                      std::uint32_t original_lo, std::uint32_t original_hi) {
  const auto registers = ReadRegisters(fixture, 2);
  const auto program_address =
      kCode + (static_cast<std::uint64_t>(original_lo) << 8U) +
      ((static_cast<std::uint64_t>(original_hi) & 0xffU) << 40U);
  Check(Read32(registers, 0) == expected_lo &&
            Read32(registers, 8) == expected_lo + 1U,
        "shader program pair offsets changed");
  Check(Read32(registers, 4) ==
            static_cast<std::uint32_t>(program_address >> 8U),
        "shader program low address was not patched");
  Check(Read32(registers, 12) ==
            ((original_hi & 0xffffff00U) |
             static_cast<std::uint32_t>((program_address >> 40U) & 0xffU)),
        "shader program high address did not preserve upper bits");
}

void CheckPatchedPairAt(const Fixture& fixture, std::size_t register_count,
                        std::size_t lo_index, std::size_t hi_index,
                        std::uint32_t expected_lo, std::uint32_t original_lo,
                        std::uint32_t original_hi) {
  const auto registers = ReadRegisters(fixture, register_count);
  const auto program_address =
      kCode + (static_cast<std::uint64_t>(original_lo) << 8U) +
      ((static_cast<std::uint64_t>(original_hi) & 0xffU) << 40U);
  const auto lo_offset = lo_index * 8U;
  const auto hi_offset = hi_index * 8U;
  Check(Read32(registers, lo_offset) == expected_lo &&
            Read32(registers, hi_offset) == expected_lo + 1U,
        "selected shader program pair offsets changed");
  Check(Read32(registers, lo_offset + 4U) ==
            static_cast<std::uint32_t>(program_address >> 8U),
        "selected shader program low address was not patched");
  Check(Read32(registers, hi_offset + 4U) ==
            ((original_hi & 0xffffff00U) |
             static_cast<std::uint32_t>((program_address >> 40U) & 0xffU)),
        "selected shader program high address did not preserve upper bits");
}

void CheckResultPreserved(
    const kajps5::gpu::ShaderCompileResult& outcome,
    kajps5::gpu::ShaderRuntimeStatus expected,
    const kajps5::gpu::shader::recompiler::CompileResult& result,
    std::string_view message) {
  Check(outcome.status == expected, message);
  Check(result.spirv.size() == 1U && result.spirv.front() == 0xdecafbadU,
        "failed compilation changed caller result");
}

void TestNormalComputeMappingAndCompilation() {
  Fixture fixture;
  constexpr std::array registers = {
      RegisterWord{0x20c, 0x11111111U}, RegisterWord{0x20d, 0xaabbccddU}};
  ConfigureShader(fixture, 0, registers);

  const auto mapped = fixture.runtime.CreateShader(kDestination, kHeader, kCode);
  Check(mapped.status == kajps5::gpu::ShaderRuntimeStatus::kOk,
        "normal compute shader mapping failed");
  const auto header = ReadHeader(fixture);
  Check(Read64(header, kUserDataOffset) == kUserData &&
            Read64(header, kCxRegistersOffset) == kCxRegisters &&
            Read64(header, kShRegistersOffset) == kShRegisters &&
            Read64(header, kSpecialsOffset) == kSpecials &&
            Read64(header, kInputSemanticsOffset) == kInputSemantics &&
            Read64(header, kOutputSemanticsOffset) == kOutputSemantics,
        "header relative pointers were not relocated");
  Check(Read64(header, kCodeOffset) == kCode,
        "header code pointer was not written directly");
  const auto user_data = ReadUserData(fixture);
  for (std::size_t offset = 0; offset < user_data.size();
       offset += sizeof(std::uint64_t)) {
    Check(Read64(user_data, offset) == kPointerTargets + offset * 8U,
          "user-data relative pointer was not relocated");
  }
  std::array<std::byte, sizeof(std::uint64_t)> destination{};
  Check(fixture.memory.Read(kDestination, destination) &&
            Read64(destination, 0) == kHeader,
        "shader destination was not published");
  CheckPatchedPair(fixture, 0x20c, registers[0].value, registers[1].value);

  const auto record = fixture.runtime.LookupRegisteredShader(kCode);
  Check(record.has_value() && record->code_address == kCode &&
            record->header_address == kHeader &&
            record->code_size_bytes == sizeof(std::uint32_t) &&
            record->binary_type == 0 && record->user_data_address == kUserData &&
            record->input_semantics_address == kInputSemantics &&
            record->input_semantics_count == 1,
        "shader registry record is incomplete");

  kajps5::gpu::shader::recompiler::CompileOptions options;
  options.stage = kajps5::gpu::ShaderType::Compute;
  options.dump_ir = false;
  kajps5::gpu::shader::recompiler::CompileResult compiled;
  const auto outcome = fixture.runtime.RecompileRegisteredShader(
      kCode, options, compiled);
  Check(outcome.status == kajps5::gpu::ShaderRuntimeStatus::kOk &&
            !compiled.spirv.empty() && compiled.spirv.front() == kSpirvMagic,
        "registered compute shader did not compile");
}

void TestTransactionalCreateFailures() {
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    WriteGuest32(fixture.memory, kHeader, kFileHeader + 1U);
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "invalid file header did not fail as an argument error");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    WriteGuest32(fixture.memory, kHeader + 4, kVersion + 1U);
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "invalid version did not fail as an argument error");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    WriteGuest32(fixture.memory, kHeader + kShaderSizeOffset, 0);
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "zero shader size did not fail");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    WriteGuest32(fixture.memory, kHeader + kShaderSizeOffset, 6);
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "unaligned shader size did not fail");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    WriteGuest32(fixture.memory, kHeader + kShaderSizeOffset,
                 kajps5::gpu::kMaximumRegisteredShaderBytes + 4U);
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "oversized shader image did not fail");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    CheckCreateFailure(fixture, kDestination, kCode + 1U,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "unaligned shader code address did not fail");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    const auto before = Snapshot(fixture);
    Check(fixture.memory.Protect(
              kCode, sizeof(std::uint32_t),
              kajps5::memory::GuestMemoryProtection::kWrite),
          "shader unreadable-range protection setup failed");
    const auto mapped = fixture.runtime.CreateShader(kDestination, kHeader, kCode);
    Check(mapped.status == kajps5::gpu::ShaderRuntimeStatus::kMemoryFault,
          "unreadable shader code range did not fault");
    Check(fixture.memory.Protect(
              kCode, sizeof(std::uint32_t),
              kajps5::memory::GuestMemoryProtection::kRead |
                  kajps5::memory::GuestMemoryProtection::kWrite),
          "shader unreadable-range protection restore failed");
    CheckUnchanged(fixture, before,
                   "unreadable shader code range changed guest memory");
    Check(!fixture.runtime.LookupRegisteredShader(kCode).has_value(),
          "unreadable shader code range published a registry record");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    WriteGuest64(fixture.memory, kHeader + kCxRegistersOffset,
                 std::numeric_limits<std::uint64_t>::max());
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "overflowing relative pointer did not fail");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    const auto table_address = kBase + kMemorySize - 4U;
    WriteGuest64(fixture.memory, kHeader + kShRegistersOffset,
                 Relative(kHeader + kShRegistersOffset, table_address));
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kMemoryFault,
                       "truncated register table did not fault");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    CheckCreateFailure(fixture, kBase + kMemorySize, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kMemoryFault,
                       "unwritable destination did not fault");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    const auto before = Snapshot(fixture);
    kajps5::gpu::ShaderRuntime::SetCreateShaderTestFaultForTesting(
        {kajps5::gpu::ShaderRuntimeTestFaultPoint::
             kFailMutationWriteAfterSuccessfulWrites,
         1});
    const auto mapped = fixture.runtime.CreateShader(kDestination, kHeader, kCode);
    kajps5::gpu::ShaderRuntime::ClearCreateShaderTestFaultForTesting();
    Check(mapped.status == kajps5::gpu::ShaderRuntimeStatus::kMemoryFault,
          "forced late shader mutation write did not report a memory fault");
    CheckUnchanged(fixture, before,
                   "late shader mutation failure did not roll back guest memory");
    Check(!fixture.runtime.LookupRegisteredShader(kCode).has_value(),
          "late shader mutation failure published a registry record");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    const auto before = Snapshot(fixture);
    kajps5::gpu::ShaderRuntime::SetCreateShaderTestFaultForTesting(
        {kajps5::gpu::ShaderRuntimeTestFaultPoint::
             kPartiallyWriteMutationThenFail,
         1});
    const auto mapped = fixture.runtime.CreateShader(kDestination, kHeader, kCode);
    kajps5::gpu::ShaderRuntime::ClearCreateShaderTestFaultForTesting();
    Check(mapped.status == kajps5::gpu::ShaderRuntimeStatus::kMemoryFault,
          "forced partial shader mutation did not report a memory fault");
    CheckUnchanged(fixture, before,
                   "partial shader mutation failure did not restore guest memory");
    Check(!fixture.runtime.LookupRegisteredShader(kCode).has_value(),
          "partial shader mutation failure published a registry record");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
    ConfigureShader(fixture, 0, registers);
    const auto before = Snapshot(fixture);
    kajps5::gpu::ShaderRuntime::SetCreateShaderTestFaultForTesting(
        {kajps5::gpu::ShaderRuntimeTestFaultPoint::kBeforeMutationCommitBadAlloc,
         0});
    const auto mapped = fixture.runtime.CreateShader(kDestination, kHeader, kCode);
    kajps5::gpu::ShaderRuntime::ClearCreateShaderTestFaultForTesting();
    Check(mapped.status == kajps5::gpu::ShaderRuntimeStatus::kResourceLimit,
          "forced shader allocation failure did not report a resource limit");
    CheckUnchanged(fixture, before,
                   "shader allocation failure changed guest memory");
    Check(!fixture.runtime.LookupRegisteredShader(kCode).has_value(),
          "shader allocation failure published a registry record");
  }
  {
    constexpr std::uint64_t kHighBase = 1ULL << 48U;
    kajps5::memory::GuestMemory memory(
        kHighBase, kMemorySize,
        kajps5::memory::GuestMemoryProtection::kRead |
            kajps5::memory::GuestMemoryProtection::kWrite);
    kajps5::gpu::ShaderRuntime runtime(memory);
    const auto high_code = kHighBase + 0x200U;
    const auto before = SnapshotMemory(memory, kHighBase, kMemorySize);
    const auto mapped = runtime.CreateShader(0, kHighBase + 0x100U, high_code);
    Check(mapped.status == kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
          "48-bit-overflow shader code address did not fail");
    const auto after = SnapshotMemory(memory, kHighBase, kMemorySize);
    Check(after == before,
          "48-bit-overflow shader code address changed guest memory");
    Check(!runtime.Lookup(high_code).has_value(),
          "48-bit-overflow shader code address published a registry record");
  }
  {
    constexpr std::uint64_t kProgramAddressLimit = 1ULL << 48U;
    constexpr std::uint64_t kHighBase = kProgramAddressLimit - kMemorySize;
    constexpr std::uint64_t kHighHeader = kHighBase + 0x100U;
    constexpr std::uint64_t kHighRegisters = kHighBase + 0x200U;
    constexpr std::uint64_t kHighCode = kProgramAddressLimit - 0x100U;
    kajps5::memory::GuestMemory memory(
        kHighBase, kMemorySize,
        kajps5::memory::GuestMemoryProtection::kRead |
            kajps5::memory::GuestMemoryProtection::kWrite);
    kajps5::gpu::ShaderRuntime runtime(memory);
    constexpr std::array registers = {
        RegisterWord{0x20c, 1U}, RegisterWord{0x20d, 0xaabbcc00U}};
    ConfigureMinimalComputeShader(memory, kHighHeader, kHighCode,
                                  kHighRegisters, registers);
    const auto before = SnapshotMemory(memory, kHighBase, kMemorySize);
    const auto mapped = runtime.CreateShader(0, kHighHeader, kHighCode);
    Check(mapped.status == kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
          "unencodable program address after shader offset did not fail");
    const auto after = SnapshotMemory(memory, kHighBase, kMemorySize);
    Check(after == before,
          "unencodable program address after shader offset changed guest memory");
    Check(!runtime.Lookup(kHighCode).has_value(),
          "unencodable program address after shader offset published a record");
  }
}

void TestProgramPairsAndFrontHalves() {
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x008, 0x11223344U}, RegisterWord{0x009, 0x7654aa55U}};
    ConfigureShader(fixture, 1, registers);
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "pixel shader mapping failed");
    CheckPatchedPair(fixture, 0x008, registers[0].value, registers[1].value);
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x008, 0x11223344U}, RegisterWord{0x009, 0x7654aa55U}};
    ConfigureShader(fixture, 0, registers);
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "robust program-pair fallback failed");
    CheckPatchedPair(fixture, 0x008, registers[0].value, registers[1].value);
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x10a, 0x11111111U}, RegisterWord{0x10b, 0x22222222U},
        RegisterWord{0x008, 0x33333333U}, RegisterWord{0x009, 0x44444444U}};
    ConfigureShader(fixture, 5, registers);
    const auto before = ReadRegisters(fixture, registers.size());
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "HS-front resource-table fallback failed");
    CheckPatchedPairAt(fixture, registers.size(), 2, 3, 0x008,
                       registers[2].value, registers[3].value);
    const auto after = ReadRegisters(fixture, registers.size());
    Check(std::equal(before.begin(), before.begin() + 16, after.begin()),
          "HS-front resource registers changed during fallback patch");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x08a, 0x11111111U}, RegisterWord{0x08b, 0x22222222U},
        RegisterWord{0x108, 0x33333333U}, RegisterWord{0x109, 0x44444444U}};
    ConfigureShader(fixture, 4, registers);
    const auto before = ReadRegisters(fixture, registers.size());
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "GS-front resource-table fallback failed");
    CheckPatchedPairAt(fixture, registers.size(), 2, 3, 0x108,
                       registers[2].value, registers[3].value);
    const auto after = ReadRegisters(fixture, registers.size());
    Check(std::equal(before.begin(), before.begin() + 16, after.begin()),
          "GS-front resource registers changed during fallback patch");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x10a, 0x11111111U}, RegisterWord{0x10b, 0x22222222U}};
    ConfigureShader(fixture, 5, registers);
    const auto before = ReadRegisters(fixture, registers.size());
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "HS-front resource-only table did not defer PGM patching");
    Check(ReadRegisters(fixture, registers.size()) == before,
          "HS-front resource-only table unexpectedly patched registers");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x108, 0x01020304U}, RegisterWord{0x109, 0xaa00bbccU}};
    ConfigureShader(fixture, 5, registers);
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "HS-front direct PGM pair failed");
    CheckPatchedPair(fixture, 0x108, registers[0].value, registers[1].value);
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x088, 0x01020304U}, RegisterWord{0x089, 0xbb00ccddU}};
    ConfigureShader(fixture, 4, registers);
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "GS-front direct PGM pair failed");
    CheckPatchedPair(fixture, 0x088, registers[0].value, registers[1].value);
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x200, 0x11111111U}, RegisterWord{0x201, 0x22222222U}};
    ConfigureShader(fixture, 5, registers);
    CheckCreateFailure(fixture, kDestination, kCode,
                       kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument,
                       "unrelated front-half table did not fail");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x10a, 0x11111111U}, RegisterWord{0x10b, 0x22222222U},
        RegisterWord{0x008, 0x33333333U}, RegisterWord{0x009, 0x44444444U},
        RegisterWord{0x108, 0x55555555U}, RegisterWord{0x109, 0x66666666U}};
    ConfigureShader(fixture, 5, registers);
    const auto before = ReadRegisters(fixture, registers.size());
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "later preferred HS-front program pair was not found");
    CheckPatchedPairAt(fixture, registers.size(), 4, 5, 0x108,
                       registers[4].value, registers[5].value);
    const auto after = ReadRegisters(fixture, registers.size());
    Check(std::equal(before.begin() + 16, before.begin() + 32,
                     after.begin() + 16),
          "fallback pair won over a later preferred HS-front program pair");
  }
  {
    Fixture fixture;
    constexpr std::array registers = {
        RegisterWord{0x108, 0x11111111U}, RegisterWord{0x109, 0x22222222U},
        RegisterWord{0x008, 0x33333333U}, RegisterWord{0x009, 0x44444444U}};
    ConfigureShader(fixture, 0, registers);
    const auto before = ReadRegisters(fixture, registers.size());
    Check(static_cast<bool>(
              fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
          "table-order fallback program pair was not found");
    CheckPatchedPairAt(fixture, registers.size(), 0, 1, 0x108,
                       registers[0].value, registers[1].value);
    const auto after = ReadRegisters(fixture, registers.size());
    Check(std::equal(before.begin() + 16, before.end(), after.begin() + 16),
          "fallback did not preserve the later program pair");
  }
}

void TestCompilationRejections() {
  Fixture fixture;
  constexpr std::array registers = {
      RegisterWord{0x20c, 0}, RegisterWord{0x20d, 0}};
  ConfigureShader(fixture, 0, registers);
  Check(static_cast<bool>(
            fixture.runtime.CreateShader(kDestination, kHeader, kCode)),
        "compute setup mapping failed");

  kajps5::gpu::shader::recompiler::CompileOptions options;
  options.stage = kajps5::gpu::ShaderType::Compute;
  options.dump_ir = false;
  kajps5::gpu::shader::recompiler::CompileResult result;
  result.spirv = {0xdecafbadU};
  CheckResultPreserved(
      fixture.runtime.RecompileRegisteredShader(kCode + 4U, options, result),
      kajps5::gpu::ShaderRuntimeStatus::kNotRegistered, result,
      "unregistered shader compilation did not fail");

  options.stage = kajps5::gpu::ShaderType::Pixel;
  CheckResultPreserved(
      fixture.runtime.RecompileRegisteredShader(kCode, options, result),
      kajps5::gpu::ShaderRuntimeStatus::kStageMismatch, result,
      "stage mismatch did not fail");

  Check(fixture.memory.Protect(kCode, sizeof(std::uint32_t),
                               kajps5::memory::GuestMemoryProtection::kWrite),
        "shader readability protection setup failed");
  options.stage = kajps5::gpu::ShaderType::Compute;
  CheckResultPreserved(
      fixture.runtime.RecompileRegisteredShader(kCode, options, result),
      kajps5::gpu::ShaderRuntimeStatus::kMemoryFault, result,
      "changed-unreadable shader range did not fail");

  {
    Fixture half_fixture;
    constexpr std::array half_registers = {
        RegisterWord{0x108, 0}, RegisterWord{0x109, 0}};
    ConfigureShader(half_fixture, 5, half_registers);
    Check(static_cast<bool>(half_fixture.runtime.CreateShader(
              kDestination, kHeader, kCode)),
          "unsupported-half mapping setup failed");
    result.spirv = {0xdecafbadU};
    options.stage = kajps5::gpu::ShaderType::Vertex;
    CheckResultPreserved(
        half_fixture.runtime.RecompileRegisteredShader(kCode, options, result),
        kajps5::gpu::ShaderRuntimeStatus::kUnsupportedShaderType, result,
        "unsupported shader half reached the recompiler");
  }
  {
    Fixture failed_fixture;
    ConfigureShader(failed_fixture, 0, registers, 0xe0301000U);
    Check(static_cast<bool>(failed_fixture.runtime.CreateShader(
              kDestination, kHeader, kCode)),
          "compile-failure mapping setup failed");
    result.spirv = {0xdecafbadU};
    options.stage = kajps5::gpu::ShaderType::Compute;
    CheckResultPreserved(
        failed_fixture.runtime.RecompileRegisteredShader(kCode, options, result),
        kajps5::gpu::ShaderRuntimeStatus::kCompilationFailed, result,
        "failed compilation did not preserve caller result");
  }
  {
    Fixture geometry_fixture;
    constexpr std::array geometry_registers = {
        RegisterWord{0x0c8, 0}, RegisterWord{0x0c9, 0}};
    ConfigureShader(geometry_fixture, 2, geometry_registers);
    Check(static_cast<bool>(geometry_fixture.runtime.CreateShader(
              kDestination, kHeader, kCode)),
          "geometry-export mapping setup failed");
    kajps5::gpu::shader::recompiler::CompileResult geometry_result;
    options.stage = kajps5::gpu::ShaderType::Vertex;
    Check(geometry_fixture.runtime.RecompileRegisteredShader(
              kCode, options, geometry_result) &&
              !geometry_result.spirv.empty() &&
              geometry_result.spirv.front() == kSpirvMagic,
          "geometry-export shader did not map to vertex compilation");
  }
}

}  // namespace

int main() {
  TestNormalComputeMappingAndCompilation();
  TestTransactionalCreateFailures();
  TestProgramPairsAndFrontHalves();
  TestCompilationRejections();
  return 0;
}
