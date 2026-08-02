// Copyright (C) 2026 KajPS5 contributors
// Architecture and register reference: KytyPS5 src/libs/agc.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/Agc/AgcExports.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/command_processor.h"
#include "gpu/runtime.h"
#include "gpu/shader/recompiler/ShaderRecompiler.h"

namespace {

constexpr std::uint64_t kBase = 0x2000000;
constexpr std::size_t kMemorySize = 0x80000;
constexpr std::uint32_t kShaderFileHeader = 0x34333231U;
constexpr std::uint32_t kShaderVersion = 0x18U;
constexpr std::uint32_t kEndProgram = 0xbf810000U;
constexpr std::uint32_t kInvalidProgramWord = 0xe0301000U;
constexpr std::uint32_t kSpirvMagic = 0x07230203U;
constexpr std::size_t kShaderHeaderBytes = 0x60;
constexpr std::size_t kShaderShRegistersOffset = 0x20;
constexpr std::size_t kShaderSizeOffset = 0x44;
constexpr std::size_t kShaderTypeOffset = 0x5a;
constexpr std::size_t kShaderShRegisterCountOffset = 0x5c;
constexpr std::uint64_t kCommands = kBase + 0x40000;
constexpr std::uint64_t kCommandsSecond = kBase + 0x41000;
constexpr std::uint64_t kRegisterTable = kBase + 0x45000;

struct RegisterWord {
  std::uint32_t offset = 0;
  std::uint32_t value = 0;
};

struct ShaderLocation {
  std::uint64_t header = 0;
  std::uint64_t code = 0;
  std::uint64_t registers = 0;
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
    std::cerr << "gpu_shader_binding_test: " << message << '\n';
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

std::uint64_t Relative(std::uint64_t field_address, std::uint64_t target) {
  Check(target >= field_address, "fixture pointer target precedes field");
  return target - field_address;
}

void WriteDwords(kajps5::memory::GuestMemory& memory, std::uint64_t address,
                 std::span<const std::uint32_t> words) {
  std::vector<std::byte> bytes(words.size() * sizeof(std::uint32_t));
  for (std::size_t index = 0; index < words.size(); ++index) {
    Write32(bytes, index * sizeof(std::uint32_t), words[index]);
  }
  Check(memory.Write(address, bytes), "guest dword write failed");
}

std::vector<std::byte> SnapshotMemory(const Fixture& fixture) {
  std::vector<std::byte> snapshot(kMemorySize);
  Check(fixture.memory.Read(kBase, snapshot), "guest snapshot failed");
  return snapshot;
}

void ConfigureShader(Fixture& fixture, ShaderLocation location,
                     std::uint8_t binary_type,
                     std::span<const RegisterWord> registers,
                     std::uint32_t code_size_bytes =
                         sizeof(std::uint32_t),
                     std::uint32_t entry_offset_bytes = 0,
                     std::uint32_t prefix_word = 0) {
  Check(code_size_bytes >= sizeof(std::uint32_t) &&
            code_size_bytes % sizeof(std::uint32_t) == 0U &&
            entry_offset_bytes <= code_size_bytes - sizeof(std::uint32_t),
        "invalid shader fixture code range");

  std::array<std::byte, kShaderHeaderBytes> header{};
  Write32(header, 0, kShaderFileHeader);
  Write32(header, 4, kShaderVersion);
  Write64(header, kShaderShRegistersOffset,
          Relative(location.header + kShaderShRegistersOffset,
                   location.registers));
  Write32(header, kShaderSizeOffset, code_size_bytes);
  header[kShaderTypeOffset] = static_cast<std::byte>(binary_type);
  header[kShaderShRegisterCountOffset] =
      static_cast<std::byte>(registers.size());
  Check(fixture.memory.Write(location.header, header),
        "shader header write failed");

  std::vector<std::byte> register_bytes(registers.size() * 8U);
  for (std::size_t index = 0; index < registers.size(); ++index) {
    Write32(register_bytes, index * 8U, registers[index].offset);
    Write32(register_bytes, index * 8U + 4U, registers[index].value);
  }
  Check(fixture.memory.Write(location.registers, register_bytes),
        "shader register table write failed");

  std::vector<std::byte> code(code_size_bytes);
  if (prefix_word != 0) {
    Write32(code, 0, prefix_word);
  }
  Write32(code, entry_offset_bytes, kEndProgram);
  Check(fixture.memory.Write(location.code, code), "shader code write failed");
}

std::uint32_t Pm4(std::uint32_t dwords, std::uint32_t opcode,
                  std::uint32_t packet_register = 0) {
  return 0xc0000000U | (((dwords - 2U) & 0x3fffU) << 16U) |
         ((opcode & 0xffU) << 8U) | ((packet_register & 0x3fU) << 2U);
}

std::uint32_t ProgramLo(std::uint64_t address) {
  return static_cast<std::uint32_t>(address >> 8U);
}

std::uint32_t ProgramHi(std::uint64_t address) {
  return static_cast<std::uint32_t>((address >> 40U) & 0xffU);
}

void AppendSetShValue(std::vector<std::uint32_t>& commands,
                      std::uint32_t offset, std::uint32_t value) {
  commands.insert(commands.end(), {Pm4(3, 0x76), offset, value});
}

void AppendSetShProgram(std::vector<std::uint32_t>& commands,
                        std::uint32_t lo_offset,
                        std::uint64_t program_address) {
  commands.insert(commands.end(),
                  {Pm4(4, 0x76), lo_offset, ProgramLo(program_address),
                   ProgramHi(program_address)});
}

void AppendDraw(std::vector<std::uint32_t>& commands) {
  commands.insert(commands.end(), {Pm4(3, 0x2d), 3U, 2U});
}

void AppendDispatch(std::vector<std::uint32_t>& commands) {
  commands.insert(commands.end(), {Pm4(5, 0x15), 1U, 2U, 3U, 0x41U});
}

const kajps5::gpu::GpuAction* FindAction(
    const kajps5::gpu::GpuActionTrace& trace,
    kajps5::gpu::GpuActionType type, std::size_t occurrence = 0) {
  for (const auto& action : trace.actions()) {
    if (action.type == type) {
      if (occurrence == 0) {
        return &action;
      }
      --occurrence;
    }
  }
  return nullptr;
}

const kajps5::gpu::GpuShaderBinding* FindBinding(
    const kajps5::gpu::GpuAction& action,
    kajps5::gpu::GpuShaderStage stage) {
  for (std::size_t index = 0; index < action.shader_binding_count; ++index) {
    if (action.shader_bindings[index].stage == stage) {
      return &action.shader_bindings[index];
    }
  }
  return nullptr;
}

void CheckRegisteredBinding(const kajps5::gpu::GpuShaderBinding& binding,
                            const kajps5::gpu::RegisteredShader& record,
                            std::string_view message) {
  Check(binding.status == kajps5::gpu::GpuShaderBindingStatus::kRegistered &&
            binding.program_address == record.program_address &&
            binding.code_address == record.code_address &&
            binding.header_address == record.header_address &&
            binding.code_offset_bytes == record.program_offset_bytes &&
            binding.code_size_bytes == record.code_size_bytes &&
            binding.binary_type == record.binary_type,
        message);
}

void CheckUnregisteredBinding(const kajps5::gpu::GpuShaderBinding& binding,
                              std::uint64_t program_address,
                              std::string_view message) {
  Check(binding.status == kajps5::gpu::GpuShaderBindingStatus::kUnregistered &&
            binding.program_address == program_address &&
            binding.code_address == 0 && binding.header_address == 0 &&
            binding.code_offset_bytes == 0 && binding.code_size_bytes == 0 &&
            binding.binary_type == 0,
        message);
}

void TestDrawAndDispatchSnapshots() {
  Fixture fixture;
  constexpr ShaderLocation pixel{.header = kBase + 0x1000,
                                 .code = kBase + 0x4000,
                                 .registers = kBase + 0x7000};
  constexpr ShaderLocation export_shader{.header = kBase + 0x8000,
                                         .code = kBase + 0xb000,
                                         .registers = kBase + 0xe000};
  constexpr ShaderLocation compute{.header = kBase + 0x10000,
                                   .code = kBase + 0x13000,
                                   .registers = kBase + 0x16000};
  constexpr ShaderLocation second_pixel{.header = kBase + 0x19000,
                                        .code = kBase + 0x1c000,
                                        .registers = kBase + 0x1f000};
  constexpr std::array pixel_registers = {
      RegisterWord{0x008U, 0}, RegisterWord{0x009U, 0}};
  constexpr std::array export_registers = {
      RegisterWord{0x0c8U, 0}, RegisterWord{0x0c9U, 0}};
  constexpr std::array compute_registers = {
      RegisterWord{0x20cU, 0}, RegisterWord{0x20dU, 0}};
  ConfigureShader(fixture, pixel, 1, pixel_registers);
  ConfigureShader(fixture, export_shader, 2, export_registers);
  ConfigureShader(fixture, compute, 0, compute_registers);
  ConfigureShader(fixture, second_pixel, 1, pixel_registers);

  const auto pixel_record = fixture.runtime.CreateShader(0, pixel.header,
                                                          pixel.code);
  const auto export_record = fixture.runtime.CreateShader(
      0, export_shader.header, export_shader.code);
  const auto compute_record = fixture.runtime.CreateShader(0, compute.header,
                                                            compute.code);
  const auto second_pixel_record = fixture.runtime.CreateShader(
      0, second_pixel.header, second_pixel.code);
  Check(pixel_record && export_record && compute_record &&
            second_pixel_record && pixel_record.record.has_program_binding &&
            export_record.record.has_program_binding &&
            compute_record.record.has_program_binding,
        "registered shader setup failed");

  constexpr std::uint64_t kGeometryUnregistered = kBase + 0x30000;
  constexpr std::uint64_t kHullUnregistered = kBase + 0x31000;
  constexpr std::uint64_t kLocalUnregistered = kBase + 0x32000;
  std::vector<std::uint32_t> commands;
  // Write in reverse-ish order to prove action order is independent of PM4
  // write order.
  AppendSetShProgram(commands, 0x148U, kLocalUnregistered);
  AppendSetShProgram(commands, 0x108U, kHullUnregistered);
  AppendSetShProgram(commands, 0x088U, kGeometryUnregistered);
  AppendSetShProgram(commands, 0x0c8U, export_record.record.program_address);
  AppendSetShProgram(commands, 0x008U, pixel_record.record.program_address);
  AppendDraw(commands);
  AppendSetShProgram(commands, 0x20cU, compute_record.record.program_address);
  AppendDispatch(commands);
  WriteDwords(fixture.memory, kCommands, commands);
  kajps5::gpu::GpuActionTrace trace(64);
  const auto result = fixture.runtime.ProcessCommandBuffer(
      kCommands, static_cast<std::uint32_t>(commands.size()), trace);
  Check(result.status == kajps5::gpu::GpuCommandStatus::kComplete,
        "registered draw and dispatch stream did not complete");

  const auto* draw = FindAction(trace, kajps5::gpu::GpuActionType::kDraw);
  const auto* dispatch =
      FindAction(trace, kajps5::gpu::GpuActionType::kDispatch);
  Check(draw != nullptr && dispatch != nullptr && draw->shader_binding_count == 5 &&
            dispatch->shader_binding_count == 1,
        "draw or dispatch binding snapshot is incomplete");
  constexpr std::array expected_stages = {
      kajps5::gpu::GpuShaderStage::kPixel,
      kajps5::gpu::GpuShaderStage::kGeometry,
      kajps5::gpu::GpuShaderStage::kExport,
      kajps5::gpu::GpuShaderStage::kHull,
      kajps5::gpu::GpuShaderStage::kLocal,
  };
  for (std::size_t index = 0; index < expected_stages.size(); ++index) {
    Check(draw->shader_bindings[index].stage == expected_stages[index],
          "draw shader stages are not in stable order");
  }
  CheckRegisteredBinding(draw->shader_bindings[0], pixel_record.record,
                         "pixel binding was not enriched");
  CheckUnregisteredBinding(draw->shader_bindings[1], kGeometryUnregistered,
                           "geometry binding was not retained unregistered");
  CheckRegisteredBinding(draw->shader_bindings[2], export_record.record,
                         "ES/vertex binding was not enriched");
  CheckUnregisteredBinding(draw->shader_bindings[3], kHullUnregistered,
                           "hull binding was not retained unregistered");
  CheckUnregisteredBinding(draw->shader_bindings[4], kLocalUnregistered,
                           "local binding was not retained unregistered");
  Check(dispatch->shader_bindings[0].stage ==
            kajps5::gpu::GpuShaderStage::kCompute,
        "dispatch did not retain only the compute stage");
  CheckRegisteredBinding(dispatch->shader_bindings[0], compute_record.record,
                         "compute binding was not enriched");

  trace.Clear();
  commands.clear();
  AppendSetShProgram(commands, 0x008U, pixel_record.record.program_address);
  AppendDraw(commands);
  WriteDwords(fixture.memory, kCommandsSecond, commands);
  Check(static_cast<bool>(fixture.runtime.ProcessCommandBuffer(
            kCommandsSecond, static_cast<std::uint32_t>(commands.size()),
            trace)),
        "first immutable draw stream did not complete");
  const auto* first_draw = FindAction(trace, kajps5::gpu::GpuActionType::kDraw);
  Check(first_draw != nullptr, "first immutable draw was not traced");
  const auto* first_pixel =
      FindBinding(*first_draw, kajps5::gpu::GpuShaderStage::kPixel);
  Check(first_pixel != nullptr, "first immutable pixel binding was not traced");
  const auto first_pixel_snapshot = *first_pixel;

  commands.clear();
  AppendSetShProgram(commands, 0x008U,
                     second_pixel_record.record.program_address);
  AppendDraw(commands);
  WriteDwords(fixture.memory, kCommandsSecond, commands);
  Check(static_cast<bool>(fixture.runtime.ProcessCommandBuffer(
            kCommandsSecond, static_cast<std::uint32_t>(commands.size()),
            trace)),
        "second immutable draw stream did not complete");
  const auto* second_draw =
      FindAction(trace, kajps5::gpu::GpuActionType::kDraw, 1);
  Check(second_draw != nullptr, "second immutable draw was not traced");
  const auto* second_pixel_binding =
      FindBinding(*second_draw, kajps5::gpu::GpuShaderStage::kPixel);
  Check(first_pixel_snapshot.program_address ==
                pixel_record.record.program_address &&
            first_pixel_snapshot.code_address == pixel_record.record.code_address &&
            second_pixel_binding != nullptr &&
            second_pixel_binding->program_address ==
                second_pixel_record.record.program_address &&
            second_pixel_binding->code_address ==
                second_pixel_record.record.code_address,
        "later SH writes mutated an already delivered shader snapshot");
}

void TestMissingAndIndirectBindings() {
  constexpr std::uint64_t kProgramAddress = kBase + 0x33000;
  Fixture direct_fixture;
  kajps5::gpu::GpuActionTrace direct_trace(16);
  std::vector<std::uint32_t> commands;
  AppendSetShValue(commands, 0x008U, ProgramLo(kProgramAddress));
  AppendDraw(commands);
  WriteDwords(direct_fixture.memory, kCommands, commands);
  Check(static_cast<bool>(direct_fixture.runtime.ProcessCommandBuffer(
            kCommands, static_cast<std::uint32_t>(commands.size()),
            direct_trace)),
        "incomplete direct SH stream did not complete");
  const auto* incomplete_draw =
      FindAction(direct_trace, kajps5::gpu::GpuActionType::kDraw);
  Check(incomplete_draw != nullptr && incomplete_draw->shader_binding_count == 0,
        "incomplete SH pair was not omitted");

  direct_trace.Clear();
  commands.clear();
  AppendSetShValue(commands, 0x009U, ProgramHi(kProgramAddress));
  AppendDraw(commands);
  WriteDwords(direct_fixture.memory, kCommandsSecond, commands);
  Check(static_cast<bool>(direct_fixture.runtime.ProcessCommandBuffer(
            kCommandsSecond, static_cast<std::uint32_t>(commands.size()),
            direct_trace)),
        "complete unregistered direct SH stream did not complete");
  const auto* direct_draw =
      FindAction(direct_trace, kajps5::gpu::GpuActionType::kDraw);
  Check(direct_draw != nullptr && direct_draw->shader_binding_count == 1,
        "complete direct SH pair was not retained");
  CheckUnregisteredBinding(direct_draw->shader_bindings[0], kProgramAddress,
                           "direct unregistered program was not reported");

  Fixture indirect_fixture;
  const std::array register_table = {
      0x008U, ProgramLo(kProgramAddress), 0x009U, ProgramHi(kProgramAddress)};
  WriteDwords(indirect_fixture.memory, kRegisterTable, register_table);
  commands = {
      Pm4(5, 0x63), static_cast<std::uint32_t>(kRegisterTable),
      static_cast<std::uint32_t>(kRegisterTable >> 32U), 0U, 2U,
  };
  AppendDraw(commands);
  WriteDwords(indirect_fixture.memory, kCommands, commands);
  kajps5::gpu::GpuActionTrace indirect_trace(16);
  Check(static_cast<bool>(indirect_fixture.runtime.ProcessCommandBuffer(
            kCommands, static_cast<std::uint32_t>(commands.size()),
            indirect_trace)),
        "indirect SH stream did not complete");
  const auto* indirect_draw =
      FindAction(indirect_trace, kajps5::gpu::GpuActionType::kDraw);
  Check(indirect_draw != nullptr && indirect_draw->shader_binding_count == 1,
        "indirect SH pair was not retained");
  CheckUnregisteredBinding(indirect_draw->shader_bindings[0], kProgramAddress,
                           "indirect unregistered program was not reported");
  Check(indirect_fixture.runtime.ReadRegister(
            kajps5::gpu::GpuRegisterSpace::kShader, 0x008U) ==
                ProgramLo(kProgramAddress) &&
            indirect_fixture.runtime.ReadRegister(
                kajps5::gpu::GpuRegisterSpace::kShader, 0x009U) ==
                ProgramHi(kProgramAddress),
        "indirect SH writes did not reach the same register map");
}

void TestProgramIndexRecompileAndTransactions() {
  Fixture fixture;
  constexpr ShaderLocation offset_compute{.header = kBase + 0x22000,
                                          .code = kBase + 0x23000,
                                          .registers = kBase + 0x25000};
  constexpr std::array offset_registers = {
      RegisterWord{0x20cU, 1U}, RegisterWord{0x20dU, 0U}};
  ConfigureShader(fixture, offset_compute, 0, offset_registers, 0x200U,
                  0x100U, kInvalidProgramWord);
  const auto mapped = fixture.runtime.CreateShader(
      0, offset_compute.header, offset_compute.code);
  Check(mapped && mapped.record.has_program_binding &&
            mapped.record.program_address == offset_compute.code + 0x100U &&
            mapped.record.program_offset_bytes == 0x100U,
        "nonzero program offset was not registered directly");

  kajps5::gpu::shader::recompiler::CompileOptions options;
  options.stage = kajps5::gpu::ShaderType::Compute;
  kajps5::gpu::shader::recompiler::CompileResult compiled;
  const auto program_compile = fixture.runtime.RecompileRegisteredShader(
      mapped.record.program_address, options, compiled);
  Check(program_compile && !compiled.spirv.empty() &&
            compiled.spirv.front() == kSpirvMagic,
        "program-address lookup did not recompile from its entry dword");
  compiled = {};
  const auto code_compile = fixture.runtime.RecompileRegisteredShader(
      offset_compute.code, options, compiled);
  Check(code_compile && !compiled.spirv.empty() &&
            compiled.spirv.front() == kSpirvMagic,
        "code-base lookup did not recompile from its registered entry dword");

  const auto repeated = fixture.runtime.CreateShader(
      0, offset_compute.header, offset_compute.code);
  Check(repeated && repeated.record.program_address ==
                        mapped.record.program_address &&
            repeated.record.code_address == mapped.record.code_address,
        "repeated shader registration did not preserve the direct index");

  constexpr ShaderLocation conflicting{.header = kBase + 0x26000,
                                       .code = kBase + 0x23100,
                                       .registers = kBase + 0x27000};
  static_assert(conflicting.code == offset_compute.code + 0x100U);
  constexpr std::array conflict_registers = {
      RegisterWord{0x20cU, 0U}, RegisterWord{0x20dU, 0U}};
  ConfigureShader(fixture, conflicting, 0, conflict_registers);
  const auto before_conflict = SnapshotMemory(fixture);
  const auto conflict = fixture.runtime.CreateShader(
      0, conflicting.header, conflicting.code);
  Check(conflict.status == kajps5::gpu::ShaderRuntimeStatus::kInvalidArgument &&
            SnapshotMemory(fixture) == before_conflict &&
            !fixture.runtime.LookupRegisteredShader(conflicting.code).has_value() &&
            fixture.runtime.LookupRegisteredShader(offset_compute.code)
                .has_value(),
        "conflicting program ownership was not rejected transactionally");

  // A failed registration must not leave a direct-program index behind that
  // would reject a later legitimate owner of the same program address.
  Fixture failed_fixture;
  constexpr ShaderLocation failed_owner{.header = kBase + 0x2b000,
                                        .code = kBase + 0x2a000,
                                        .registers = kBase + 0x2c000};
  constexpr ShaderLocation replacement_owner{.header = kBase + 0x2d000,
                                             .code = kBase + 0x29f00,
                                             .registers = kBase + 0x2e000};
  static_assert(replacement_owner.code + 0x100U == failed_owner.code);
  constexpr std::array zero_offset_registers = {
      RegisterWord{0x20cU, 0U}, RegisterWord{0x20dU, 0U}};
  constexpr std::array nonzero_offset_registers = {
      RegisterWord{0x20cU, 1U}, RegisterWord{0x20dU, 0U}};
  ConfigureShader(failed_fixture, failed_owner, 0, zero_offset_registers);
  kajps5::gpu::ShaderRuntime::SetCreateShaderTestFaultForTesting(
      {kajps5::gpu::ShaderRuntimeTestFaultPoint::
           kBeforeMutationCommitBadAlloc,
       0});
  const auto failed = failed_fixture.runtime.CreateShader(
      0, failed_owner.header, failed_owner.code);
  kajps5::gpu::ShaderRuntime::ClearCreateShaderTestFaultForTesting();
  ConfigureShader(failed_fixture, replacement_owner, 0,
                  nonzero_offset_registers, 0x200U, 0x100U,
                  kInvalidProgramWord);
  const auto replacement = failed_fixture.runtime.CreateShader(
      0, replacement_owner.header, replacement_owner.code);
  Check(failed.status == kajps5::gpu::ShaderRuntimeStatus::kResourceLimit &&
            !failed_fixture.runtime.LookupRegisteredShader(failed_owner.code)
                 .has_value() &&
            replacement && replacement.record.program_address ==
                               failed_owner.code,
        "failed registration partially published a program-address index");
}

void TestBindingFailureDoesNotPublishAction() {
  Fixture fixture;
  constexpr std::uint64_t kUnregisteredProgram = kBase + 0x34000;
  std::vector<std::uint32_t> commands;
  AppendSetShProgram(commands, 0x008U, kUnregisteredProgram);
  WriteDwords(fixture.memory, kCommands, commands);
  kajps5::gpu::GpuActionTrace trace(8);
  Check(static_cast<bool>(fixture.runtime.ProcessCommandBuffer(
            kCommands, static_cast<std::uint32_t>(commands.size()), trace)),
        "binding-failure setup stream did not complete");

  trace.Clear();
  commands.clear();
  AppendDraw(commands);
  WriteDwords(fixture.memory, kCommandsSecond, commands);
  kajps5::gpu::ShaderRuntime::SetBindingResolutionTestFaultForTesting(
      {kajps5::gpu::ShaderRuntimeTestFaultPoint::
           kBeforeBindingResolutionResourceLimit,
       0});
  const auto result = fixture.runtime.ProcessCommandBuffer(
      kCommandsSecond, static_cast<std::uint32_t>(commands.size()), trace);
  kajps5::gpu::ShaderRuntime::ClearBindingResolutionTestFaultForTesting();
  Check(result.status == kajps5::gpu::GpuCommandStatus::kResourceLimit &&
            trace.actions().empty(),
        "binding resource failure partially published a draw action");
}

}  // namespace

int main() {
  TestDrawAndDispatchSnapshots();
  TestMissingAndIndirectBindings();
  TestProgramIndexRecompileAndTransactions();
  TestBindingFailureDoesNotPublishAction();
  return 0;
}
