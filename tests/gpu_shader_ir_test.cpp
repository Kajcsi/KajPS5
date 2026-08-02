// Copyright (C) 2026 KajPS5 contributors
// Implementation and test reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/definitions.h"
#include "gpu/shader/recompiler/BufferFormat.h"
#include "gpu/shader/recompiler/cfg/ShaderCFG.h"
#include "gpu/shader/recompiler/decompiler/ShaderDecoder.h"
#include "gpu/shader/recompiler/ir/BindingLayout.h"
#include "gpu/shader/recompiler/ir/ResourceTracking.h"
#include "gpu/shader/recompiler/ir/ShaderIR.h"
#include "gpu/shader/recompiler/ir/ShaderInfoCollection.h"
#include "gpu/shader/recompiler/ir/ScalarProvenance.h"
#include "gpu/shader/recompiler/ir/SrtPatcher.h"
#include "gpu/shader/recompiler/ir/SrtWalker.h"
#include "gpu/shader/types.h"

namespace {

namespace cfg = kajps5::gpu::shader::recompiler::CFG;
namespace decoder = kajps5::gpu::shader::recompiler::Decoder;
namespace format = kajps5::gpu::shader::recompiler::Format;
namespace ir = kajps5::gpu::shader::recompiler::IR;

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "gpu_shader_ir_test: " << message << '\n';
    ++failures;
  }
}

decoder::Program MakeModifiedMoveProgram() {
  decoder::Instruction move;
  move.opcode = decoder::Opcode::VMovB32;
  move.pc = 0;
  move.word_count = 1;
  move.dst.kind = decoder::OperandKind::Vgpr;
  move.dst.reg = 0;
  move.src0.kind = decoder::OperandKind::Vgpr;
  move.src0.reg = 1;
  move.src0.absolute = true;
  move.src0.negate = true;
  move.src_count = 1;

  decoder::Instruction end;
  end.opcode = decoder::Opcode::SEndpgm;
  end.pc = 4;
  end.word_count = 1;

  decoder::Program program;
  program.instructions = {move, end};
  return program;
}

void TestCompleteLoweringPath() {
  constexpr auto EncodeScalarMove = [](std::uint32_t destination,
                                       std::uint32_t source) {
    return 0x80000000U | (0x7dU << 23U) |
           ((destination & 0x7fU) << 16U) | (0x03U << 8U) |
           (source & 0xffU);
  };
  const std::array code = {EncodeScalarMove(2, 135), 0xbf810000U};

  decoder::Program decoded;
  cfg::Graph graph;
  ir::Program lowered;
  std::string error;
  Check(decoder::DecodeProgram(std::span<const std::uint32_t>(code), decoded,
                               &error),
        error.c_str());
  Check(cfg::BuildGraph(decoded, graph, &error), error.c_str());
  Check(cfg::Structurize(graph, &error), error.c_str());
  Check(ir::LowerProgram(decoded, graph, kajps5::gpu::ShaderType::Compute,
                         64, lowered, &error),
        error.c_str());
  Check(lowered.stage == kajps5::gpu::ShaderType::Compute &&
            lowered.wave_size == 64 && lowered.blocks.size() == 2 &&
            lowered.blocks.front().instructions.size() == 1 &&
            lowered.blocks.front().instructions.front().op ==
                ir::Opcode::MoveU32,
        "decode, CFG, and IR lowering did not preserve the scalar move");
  Check(ir::ProgramToString(lowered).find("MoveU32") != std::string::npos,
        "the IR diagnostic lost the lowered instruction");
}

void TestFloatSignModifiersRemainExplicit() {
  const auto decoded = MakeModifiedMoveProgram();
  cfg::Graph graph;
  ir::Program lowered;
  std::string error;
  Check(cfg::BuildGraph(decoded, graph, &error), error.c_str());
  Check(cfg::Structurize(graph, &error), error.c_str());
  Check(ir::LowerProgram(decoded, graph, kajps5::gpu::ShaderType::Pixel, 32,
                         lowered, &error),
        error.c_str());
  Check(lowered.blocks.size() == 2 &&
            lowered.blocks.front().instructions.size() == 1,
        "the modified vector move did not lower to one IR instruction");
  if (lowered.blocks.empty() || lowered.blocks.front().instructions.empty()) {
    return;
  }
  const auto& move = lowered.blocks.front().instructions.front();
  Check(move.op == ir::Opcode::MoveF32Bits && move.src_count == 1 &&
            move.src[0].absolute && move.src[0].negate,
        "SDWA/VOP float sign modifiers were converted to integer arithmetic");
}

void TestInvalidWaveSizeIsChecked() {
  const auto decoded = MakeModifiedMoveProgram();
  cfg::Graph graph;
  std::string error;
  Check(cfg::BuildGraph(decoded, graph, &error), error.c_str());
  Check(cfg::Structurize(graph, &error), error.c_str());
  ir::Program lowered;
  Check(!ir::LowerProgram(decoded, graph, kajps5::gpu::ShaderType::Compute,
                          16, lowered, &error) &&
            error == "shader wave size must be 32 or 64",
        "an invalid wave size was not returned as a checked error");
}

void TestPackedTenBitFormat() {
  const auto info = format::GetFormatInfo(
      kajps5::gpu::Prospero::BufferFormat::k10_10_10_2UInt);
  Check(info.type == format::ComponentType::Uint &&
            info.component_count == 4 && info.byte_size == 4 &&
            info.packed_bitfield && info.component_bits[3] == 2 &&
            info.component_bit_offset[3] == 30,
        "packed 10-10-10-2 uint metadata is incomplete");
}

constexpr std::uint32_t EncodeMimg0(std::uint32_t opcode,
                                    std::uint32_t dmask,
                                    std::uint32_t dimension) {
  return (0x3cU << 26U) | ((opcode & 0x7fU) << 18U) |
         ((dmask & 0xfU) << 8U) | ((dimension & 0x7U) << 3U) |
         ((opcode >> 7U) & 1U);
}

constexpr std::uint32_t EncodeMimg1(std::uint32_t vdata,
                                    std::uint32_t srsrc,
                                    std::uint32_t ssamp,
                                    std::uint32_t vaddr) {
  return ((ssamp & 0x1fU) << 21U) | ((srsrc & 0x1fU) << 16U) |
         ((vdata & 0xffU) << 8U) | (vaddr & 0xffU);
}

bool LowerAndBindMsaaImage(
    std::uint32_t dimension, decoder::ImageDimension expected_dimension,
    const char* expected_name, std::uint32_t expected_coordinates,
    ir::DescriptorBindingKind expected_binding) {
  const std::array code = {
      EncodeMimg0(0x00U, 0x1U, dimension),
      EncodeMimg1(3U, 0U, 0U, 5U),
      0xbf810000U,
  };

  decoder::Program decoded;
  cfg::Graph graph;
  ir::Program lowered;
  std::string error;
  if (!decoder::DecodeProgram(std::span<const std::uint32_t>(code), decoded,
                              &error)) {
    Check(false, error.c_str());
    return false;
  }
  if (decoded.instructions.size() != 2) {
    Check(false, "MIMG regression program did not decode two instructions");
    return false;
  }
  const auto& image = decoded.instructions.front();
  Check(image.opcode == decoder::Opcode::ImageLoad &&
            image.image_dimension == expected_dimension &&
            image.image_address_components == expected_coordinates,
        "MIMG MSAA dimension or coordinate count decoded incorrectly");
  Check(std::string(decoder::ImageDimensionToString(image.image_dimension)) ==
            expected_name,
        "MIMG MSAA image-dimension string decoded incorrectly");

  if (!cfg::BuildGraph(decoded, graph, &error) ||
      !cfg::Structurize(graph, &error) ||
      !ir::LowerProgram(decoded, graph, kajps5::gpu::ShaderType::Compute,
                        64, lowered, &error)) {
    Check(false, error.c_str());
    return false;
  }
  if (lowered.blocks.empty() || lowered.blocks.front().instructions.empty()) {
    Check(false, "MIMG regression program did not lower an image instruction");
    return false;
  }
  const auto& lowered_image = lowered.blocks.front().instructions.front();
  Check(lowered_image.op == ir::Opcode::ImageLoad &&
            lowered_image.memory.image_dimension == expected_dimension &&
            lowered_image.memory.image_address_components ==
                expected_coordinates,
        "MIMG MSAA metadata did not survive IR lowering");

  kajps5::gpu::ShaderComputeInputInfo compute_info;
  ir::ShaderInfoOptions info_options;
  info_options.compute = &compute_info;
  if (!ir::BuildScalarProvenance(lowered, &error) ||
      !ir::BuildSrtPlan(lowered, &error) ||
      !ir::PatchSrtReads(lowered, &error) ||
      !ir::TrackResources(lowered, &error) ||
      !ir::CollectShaderInfo(lowered, info_options, &error) ||
      !ir::AllocateBindings(lowered, {}, &error)) {
    Check(false, error.c_str());
    return false;
  }
  const auto* binding = ir::FindBinding(lowered.bindings, expected_binding);
  Check(lowered.info.images.size() == 1 &&
            lowered.info.images.front().dimension == expected_dimension &&
            binding != nullptr && binding->resources.size() == 1 &&
            binding->resources.front() == 0,
        "decoded MIMG MSAA image did not receive the expected binding kind");
  return true;
}

void TestMsaaImageDimensionPipeline() {
  LowerAndBindMsaaImage(6U, decoder::ImageDimension::Dim2DMsaa,
                        "2d_msaa", 3U,
                        ir::DescriptorBindingKind::Sampled2DMsaa);
  LowerAndBindMsaaImage(7U, decoder::ImageDimension::Dim2DMsaaArray,
                        "2d_msaa_array", 4U,
                        ir::DescriptorBindingKind::Sampled2DMsaaArray);
}

ir::Program MakeReadConstProgram() {
  ir::Program program;
  program.stage = kajps5::gpu::ShaderType::Compute;
  program.shader_hash = 0x42U;
  program.blocks.resize(1);
  program.provenance.values.resize(6);
  program.provenance.values[0].op = ir::ScalarValueOp::Undefined;
  program.provenance.values[1].op = ir::ScalarValueOp::Unknown;
  program.provenance.values[2].op = ir::ScalarValueOp::Constant;
  program.provenance.values[2].imm = 0x1000U;
  program.provenance.values[3].op = ir::ScalarValueOp::Constant;
  program.provenance.values[4].op = ir::ScalarValueOp::Constant;
  auto& read = program.provenance.values[5];
  read.op = ir::ScalarValueOp::ReadConst;
  read.pc = 0x40U;
  read.args[0] = 2;
  read.args[1] = 3;
  read.args[2] = 4;

  ir::Instruction load;
  load.op = ir::Opcode::SLoadDword;
  load.pc = 0x40U;
  load.scalar_value = 5;
  program.blocks.front().instructions.push_back(load);
  return program;
}

bool RejectSrtRead(void*, std::uint64_t, std::uint32_t*) { return false; }

bool ReadGuestDword(void* userdata, std::uint64_t address,
                    std::uint32_t* value) {
  if (userdata == nullptr || value == nullptr) {
    return false;
  }
  auto* memory = static_cast<kajps5::memory::GuestMemory*>(userdata);
  std::array<std::byte, sizeof(std::uint32_t)> bytes{};
  if (!memory->Read(address, bytes)) {
    return false;
  }
  *value = static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(bytes[0])) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[1]))
            << 8U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[2]))
            << 16U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[3]))
            << 24U);
  return true;
}

void TestSrtReadRequiresCheckedMemoryReader() {
  auto program = MakeReadConstProgram();
  std::string error;
  Check(ir::BuildSrtPlan(program, &error), error.c_str());
  if (!program.srt_plan_complete) {
    return;
  }

  constexpr std::uint32_t kSentinel = 0xfeedfaceU;
  const std::vector<std::uint32_t> expected_unchanged = {kSentinel};
  std::vector<std::uint32_t> flat = expected_unchanged;
  ir::SrtRuntime runtime;
  Check(!ir::WalkSrt(program, runtime, flat, &error) &&
            error ==
                "shader SRT: hash=0x0000000000000042 stage=compute "
                "pc=0x00000040 ReadConst pc=0x00000040 requires an "
                "explicit checked memory reader" &&
            flat == expected_unchanged,
        "ReadConst without a checked reader did not fail transactionally");

  error.clear();
  flat = expected_unchanged;
  runtime.read_memory = RejectSrtRead;
  Check(!ir::WalkSrt(program, runtime, flat, &error) &&
            error ==
                "shader SRT: hash=0x0000000000000042 stage=compute "
                "pc=0x00000040 ReadConst pc=0x00000040 failed at "
                "0x0000000000001000" &&
            flat == expected_unchanged,
        "a failed checked SRT reader did not fail transactionally");

  kajps5::memory::GuestMemory memory(0x1000U, sizeof(std::uint32_t));
  const std::array<std::byte, sizeof(std::uint32_t)> guest_word = {
      std::byte {0x12U}, std::byte {0x34U}, std::byte {0x56U},
      std::byte {0x78U}};
  Check(memory.Write(0x1000U, guest_word),
        "GuestMemory setup for the SRT reader failed");
  error.clear();
  flat = expected_unchanged;
  runtime.read_memory = ReadGuestDword;
  runtime.userdata = &memory;
  Check(ir::WalkSrt(program, runtime, flat, &error) &&
            flat == std::vector<std::uint32_t> {0x78563412U},
        "GuestMemory-backed checked SRT reader did not resolve ReadConst");
}

ir::Instruction MakeScalarMove(std::uint32_t destination,
                               std::uint32_t value,
                               bool immediate) {
  ir::Instruction move;
  move.op = ir::Opcode::MoveU32;
  move.dst.kind = ir::OperandKind::Register;
  move.dst.reg = {ir::RegisterFile::Scalar, destination};
  move.src_count = 1;
  if (immediate) {
    move.src[0].kind = ir::OperandKind::ImmediateU32;
    move.src[0].imm = value;
  } else {
    move.src[0].kind = ir::OperandKind::Register;
    move.src[0].reg = {ir::RegisterFile::Scalar, value};
  }
  return move;
}

ir::Program MakeScalarMergeProgram(std::uint32_t left,
                                   std::uint32_t right) {
  ir::Program program;
  program.stage = kajps5::gpu::ShaderType::Compute;
  program.blocks.resize(4);
  for (std::uint32_t id = 0; id < program.blocks.size(); ++id) {
    program.blocks[id].id = id;
  }
  program.blocks[0].successors = {1, 2};
  program.blocks[0].terminator.kind = cfg::TerminatorKind::ConditionalBranch;
  program.blocks[0].terminator.true_block = 1;
  program.blocks[0].terminator.false_block = 2;
  program.blocks[1].predecessors = {0};
  program.blocks[1].successors = {3};
  program.blocks[1].instructions.push_back(MakeScalarMove(8, left, true));
  program.blocks[2].predecessors = {0};
  program.blocks[2].successors = {3};
  program.blocks[2].instructions.push_back(MakeScalarMove(8, right, true));
  program.blocks[3].predecessors = {1, 2};
  program.blocks[3].instructions.push_back(MakeScalarMove(9, 8, false));
  return program;
}

void TestScalarMergeProvenance() {
  auto divergent = MakeScalarMergeProgram(0xaaaa, 0xbbbb);
  std::string error;
  Check(ir::BuildScalarProvenance(divergent, &error), error.c_str());
  const auto divergent_source =
      divergent.blocks[3].instructions.front().scalar_sources[0];
  Check(divergent_source < divergent.provenance.values.size() &&
            divergent.provenance.values[divergent_source].op ==
                ir::ScalarValueOp::Phi &&
            divergent.provenance.values[divergent_source].phi_args.size() == 2,
        "different scalar values did not form a merge value");

  auto identical = MakeScalarMergeProgram(0xcafe, 0xcafe);
  error.clear();
  Check(ir::BuildScalarProvenance(identical, &error), error.c_str());
  const auto identical_source =
      identical.blocks[3].instructions.front().scalar_sources[0];
  Check(identical_source < identical.provenance.values.size() &&
            identical.provenance.values[identical_source].op ==
                ir::ScalarValueOp::Constant &&
            identical.provenance.values[identical_source].imm == 0xcafe,
        "the same scalar value on both paths became an unnecessary merge");
}

}  // namespace

int main() {
  TestCompleteLoweringPath();
  TestFloatSignModifiersRemainExplicit();
  TestInvalidWaveSizeIsChecked();
  TestPackedTenBitFormat();
  TestMsaaImageDimensionPipeline();
  TestSrtReadRequiresCheckedMemoryReader();
  TestScalarMergeProvenance();
  return failures == 0 ? 0 : 1;
}
