// Adapted from KytyPS5
// tests/ShaderRecompilerComputeTests.cpp at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/shader/bindings.h"
#include "gpu/shader/recompiler/ShaderRecompiler.h"
#include "gpu/shader/recompiler/emitter/SpirvEmitter.h"
#include "gpu/shader/recompiler/emitter/spirvEmitterInternal.h"
#include "gpu/shader/recompiler/ir/ShaderIR.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using kajps5::gpu::ResourceDescriptorType;
using kajps5::gpu::ShaderLaneMaskMode;
using kajps5::gpu::ShaderComputeInputInfo;
using kajps5::gpu::ShaderPixelInputInfo;
using kajps5::gpu::ShaderType;
using kajps5::gpu::ShaderVertexInputInfo;
using namespace kajps5::gpu::shader::recompiler;

constexpr uint32_t kSpirvMagic = 0x07230203u;
constexpr uint16_t kOpConstant = 43u;
constexpr uint16_t kOpFNegate = 127u;
constexpr uint16_t kOpDecorate = 71u;
constexpr uint16_t kOpUDiv = 134u;
constexpr uint32_t kDecorationLocation = 30u;

constexpr uint32_t InlineU32(uint32_t value) { return 128u + value; }

constexpr uint32_t Vgpr(uint32_t reg) { return 256u + reg; }

constexpr uint32_t EncodeSMovB32(uint32_t dst, uint32_t src) {
  return 0x80000000u | (0x7du << 23u) | ((dst & 0x7fu) << 16u) | (0x03u << 8u) |
         (src & 0xffu);
}

constexpr uint32_t EncodeSopp(uint32_t opcode, uint32_t simm = 0) {
  return 0x80000000u | (0x7fu << 23u) | ((opcode & 0x7fu) << 16u) | (simm & 0xffffu);
}

constexpr uint32_t EncodeVop1(uint32_t opcode, uint32_t dst, uint32_t src0) {
  return (0x3fu << 25u) | ((dst & 0xffu) << 17u) | ((opcode & 0xffu) << 9u) |
         (src0 & 0x1ffu);
}

constexpr uint32_t EncodeVop2(uint32_t opcode, uint32_t dst, uint32_t src0, uint32_t src1) {
  return ((opcode & 0x3fu) << 25u) | ((dst & 0xffu) << 17u) | ((src1 & 0xffu) << 9u) |
         (src0 & 0x1ffu);
}

constexpr uint32_t EncodeVop3Word0(uint32_t opcode, uint32_t dst, uint32_t abs = 0,
                                   uint32_t op_sel = 0, bool clamp = false) {
  return (0x35u << 26u) | ((opcode & 0x3ffu) << 16u) | ((abs & 0x7u) << 8u) |
         ((op_sel & 0xfu) << 11u) | (clamp ? (1u << 15u) : 0u) | (dst & 0xffu);
}

constexpr uint32_t EncodeVop3BWord0(uint32_t opcode, uint32_t vdst, uint32_t sdst) {
  return (0x35u << 26u) | ((opcode & 0x3ffu) << 16u) | ((sdst & 0x7fu) << 8u) |
         (vdst & 0xffu);
}

constexpr uint32_t EncodeVop3Word1(uint32_t src0, uint32_t src1, uint32_t src2 = 0,
                                   uint32_t omod = 0, uint32_t neg = 0) {
  return (src0 & 0x1ffu) | ((src1 & 0x1ffu) << 9u) | ((src2 & 0x1ffu) << 18u) |
         ((omod & 0x3u) << 27u) | ((neg & 0x7u) << 29u);
}

constexpr uint32_t EncodeMubuf0(uint32_t opcode, uint32_t offset = 0, bool idxen = false,
                                bool offen = true) {
  return (0x38u << 26u) | ((opcode & 0x7fu) << 18u) | (offen ? (1u << 12u) : 0u) |
         (idxen ? (1u << 13u) : 0u) | (offset & 0xfffu);
}

constexpr uint32_t EncodeMubuf1(uint32_t vdata, uint32_t srsrc, uint32_t vaddr,
                                uint32_t soffset = 128) {
  return ((soffset & 0xffu) << 24u) | ((srsrc & 0x1fu) << 16u) | ((vdata & 0xffu) << 8u) |
         (vaddr & 0xffu);
}

constexpr uint32_t EncodeSmem0(uint32_t opcode, uint32_t dst, uint32_t sbase = 0) {
  return (0x3du << 26u) | ((opcode & 0xffu) << 18u) | ((dst & 0x7fu) << 6u) |
         (sbase & 0x3fu);
}

constexpr uint32_t EncodeSmem1(uint32_t offset, uint32_t soffset = 0) {
  return (offset & 0x1fffffu) | ((soffset & 0x7fu) << 25u);
}

constexpr uint32_t EncodeVintrp(uint32_t opcode, uint32_t dst, uint32_t attr, uint32_t chan,
                                uint32_t src) {
  return (0x32u << 26u) | ((dst & 0xffu) << 18u) | ((opcode & 0x3u) << 16u) |
         ((attr & 0x3fu) << 10u) | ((chan & 0x3u) << 8u) | (src & 0xffu);
}

constexpr uint32_t EndProgram() { return EncodeSopp(0x01u); }

struct Checks {
  int failures = 0;

  bool Require(bool condition, std::string_view message) {
    if (!condition) {
      std::cerr << "gpu_shader_emitter_test: " << message << '\n';
      ++failures;
    }
    return condition;
  }
};

bool ReadWords(void* userdata, uint64_t address, uint32_t* value) {
  const auto* words = static_cast<const std::vector<uint32_t>*>(userdata);
  if (words == nullptr || value == nullptr || address % sizeof(uint32_t) != 0u) {
    return false;
  }
  const auto index = address / sizeof(uint32_t);
  if (index >= words->size()) {
    return false;
  }
  *value = (*words)[index];
  return true;
}

bool Compile(std::span<const uint32_t> code, ShaderType stage, CompileResult& result,
             std::string& error, const CompileOptions* base_options = nullptr) {
  CompileOptions options;
  if (base_options != nullptr) {
    options = *base_options;
  }
  options.stage = stage;
  options.dump_ir = false;
  error.clear();
  return TryRecompile(code, options, result, &error);
}

bool ContainsInstruction(const std::vector<uint32_t>& spirv, uint16_t opcode) {
  if (spirv.size() < 5u || spirv.front() != kSpirvMagic) {
    return false;
  }
  for (size_t offset = 5; offset < spirv.size();) {
    const auto first_word = spirv[offset];
    const auto word_count = static_cast<uint16_t>(first_word >> 16u);
    if (word_count == 0u || offset + word_count > spirv.size()) {
      return false;
    }
    if (static_cast<uint16_t>(first_word & 0xffffu) == opcode) {
      return true;
    }
    offset += word_count;
  }
  return false;
}

bool ContainsGlslExtInstruction(const std::vector<uint32_t>& spirv, uint32_t instruction) {
  if (spirv.size() < 5u || spirv.front() != kSpirvMagic) {
    return false;
  }
  for (size_t offset = 5; offset < spirv.size();) {
    const auto first_word = spirv[offset];
    const auto word_count = static_cast<uint16_t>(first_word >> 16u);
    if (word_count == 0u || offset + word_count > spirv.size()) {
      return false;
    }
    if (static_cast<uint16_t>(first_word & 0xffffu) == 12u && word_count >= 5u &&
        spirv[offset + 4u] == instruction) {
      return true;
    }
    offset += word_count;
  }
  return false;
}

bool HasLocationDecoration(const std::vector<uint32_t>& spirv, uint32_t location) {
  if (spirv.size() < 5u || spirv.front() != kSpirvMagic) {
    return false;
  }
  for (size_t offset = 5; offset < spirv.size();) {
    const auto first_word = spirv[offset];
    const auto word_count = static_cast<uint16_t>(first_word >> 16u);
    if (word_count == 0u || offset + word_count > spirv.size()) {
      return false;
    }
    if (static_cast<uint16_t>(first_word & 0xffffu) == kOpDecorate && word_count >= 4u &&
        spirv[offset + 2u] == kDecorationLocation && spirv[offset + 3u] == location) {
      return true;
    }
    offset += word_count;
  }
  return false;
}

bool HasUdivByU32Constant(const std::vector<uint32_t>& spirv, uint32_t divisor) {
  if (spirv.size() < 5u || spirv.front() != kSpirvMagic) {
    return false;
  }
  for (size_t offset = 5; offset < spirv.size();) {
    const auto first_word = spirv[offset];
    const auto word_count = static_cast<uint16_t>(first_word >> 16u);
    if (word_count == 0u || offset + word_count > spirv.size()) {
      return false;
    }
    if (static_cast<uint16_t>(first_word & 0xffffu) == kOpUDiv && word_count >= 5u) {
      const auto divisor_id = spirv[offset + 4u];
      for (size_t constant_offset = 5; constant_offset < spirv.size();) {
        const auto constant_first_word = spirv[constant_offset];
        const auto constant_word_count = static_cast<uint16_t>(constant_first_word >> 16u);
        if (constant_word_count == 0u || constant_offset + constant_word_count > spirv.size()) {
          return false;
        }
        if (static_cast<uint16_t>(constant_first_word & 0xffffu) == kOpConstant &&
            constant_word_count >= 4u && spirv[constant_offset + 2u] == divisor_id &&
            spirv[constant_offset + 3u] == divisor) {
          return true;
        }
        constant_offset += constant_word_count;
      }
    }
    offset += word_count;
  }
  return false;
}

bool WriteLittleEndianSpirv(const std::filesystem::path& path,
                            const std::vector<uint32_t>& spirv) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  for (const auto word : spirv) {
    const char bytes[4] = {
        static_cast<char>(word & 0xffu), static_cast<char>((word >> 8u) & 0xffu),
        static_cast<char>((word >> 16u) & 0xffu), static_cast<char>((word >> 24u) & 0xffu)};
    output.write(bytes, sizeof(bytes));
  }
  return output.good();
}

bool WriteRepresentativeModules(const std::filesystem::path& directory,
                                const CompileResult& compute, const CompileResult& vertex,
                                const CompileResult& pixel, const CompileResult& resource,
                                const CompileResult& wave32, const CompileResult& float_modifiers,
                                const CompileResult& pixel_collision) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return false;
  }

  struct Artifact {
    const char* file_name;
    const std::vector<uint32_t>* spirv;
  };
  const std::array artifacts = {
      Artifact{"kajps5_gpu_shader_emitter_compute.spv", &compute.spirv},
      Artifact{"kajps5_gpu_shader_emitter_vertex.spv", &vertex.spirv},
      Artifact{"kajps5_gpu_shader_emitter_pixel.spv", &pixel.spirv},
      Artifact{"kajps5_gpu_shader_emitter_resource.spv", &resource.spirv},
      Artifact{"kajps5_gpu_shader_emitter_wave32.spv", &wave32.spirv},
      Artifact{"kajps5_gpu_shader_emitter_float.spv", &float_modifiers.spirv},
      Artifact{"kajps5_gpu_shader_emitter_pixel_collision.spv", &pixel_collision.spirv},
  };
  for (const auto& artifact : artifacts) {
    if (!WriteLittleEndianSpirv(directory / artifact.file_name, *artifact.spirv)) {
      return false;
    }
  }
  return true;
}

bool PreservedAfterFailure(Checks& checks, std::span<const uint32_t> code,
                           const CompileOptions& options, std::string_view expected_error,
                           std::string_view label) {
  CompileResult result;
  result.spirv = {0x11111111u, 0x22222222u};
  result.decoded_dump = "preserve decoded";
  result.ir_dump = "preserve ir";
  result.program.stage = ShaderType::Pixel;
  result.resources.user_data = {0x33333333u};
  const auto expected_spirv = result.spirv;
  const auto expected_resources = result.resources;

  std::string error;
  if (!checks.Require(!TryRecompile(code, options, result, &error),
                      std::string(label) + " must fail")) {
    return false;
  }
  return checks.Require(error == expected_error,
                        std::string(label) + " error must be stable: " + error) &&
         checks.Require(result.spirv == expected_spirv,
                        std::string(label) + " must preserve SPIR-V") &&
         checks.Require(result.decoded_dump == "preserve decoded" && result.ir_dump == "preserve ir",
                        std::string(label) + " must preserve dumps") &&
         checks.Require(result.program.stage == ShaderType::Pixel,
                        std::string(label) + " must preserve program") &&
         checks.Require(result.resources.user_data == expected_resources.user_data,
                        std::string(label) + " must preserve resources");
}

bool CheckExactSubgroupSemantics(Checks& checks) {
  IR::Program program;
  program.blocks.emplace_back();
  auto& block = program.blocks.back();
  if (!checks.Require(!Spirv::ProgramRequiresExactSubgroupSize(program),
                      "empty program must not require an exact subgroup size")) {
    return false;
  }

  block.terminator.kind = CFG::TerminatorKind::ConditionalBranch;
  block.terminator.condition = CFG::BranchCondition::SccZero;
  if (!checks.Require(!Spirv::ProgramRequiresExactSubgroupSize(program),
                      "SCC branch must not require an exact subgroup size")) {
    return false;
  }

  block.terminator.condition = CFG::BranchCondition::VccZero;
  if (!checks.Require(Spirv::ProgramRequiresExactSubgroupSize(program),
                      "VCC branch must require an exact subgroup size")) {
    return false;
  }

  block.terminator = {};
  IR::Instruction compare;
  compare.op = IR::Opcode::CompareEqU32;
  compare.dst.kind = IR::OperandKind::Register;
  compare.dst.reg = {IR::RegisterFile::Vector, 0};
  block.instructions.push_back(compare);
  return checks.Require(Spirv::ProgramRequiresExactSubgroupSize(program),
                        "vector compare must require an exact subgroup size");
}

bool CheckEmitterTransactionalFailure(Checks& checks) {
  IR::Program incomplete_program;
  incomplete_program.stage = ShaderType::Compute;
  IR::ResourceSnapshot resources;
  std::vector<uint32_t> spirv = {0xabcdef01u, 0x10203040u};
  const auto expected_spirv = spirv;
  std::string error;
  const auto emitted = Spirv::EmitProgram(incomplete_program, resources, nullptr, nullptr,
                                          nullptr, spirv, &error);
  return checks.Require(!emitted, "incomplete emitter program must fail") &&
         checks.Require(error == "SPIR-V emitter requires a fully planned native shader program",
                        "incomplete emitter program must report a stable error") &&
         checks.Require(spirv == expected_spirv,
                        "failed emitter call must preserve prior SPIR-V output");
}

bool CheckPixelParameterCollisionFallback(Checks& checks) {
  IR::Program program;
  IR::ResourceSnapshot resources;
  ShaderPixelInputInfo pixel_info {};
  pixel_info.input_num = 2;
  pixel_info.interpolator_settings[0] = 5u;
  pixel_info.interpolator_settings[1] = 5u;

  Spirv::Emitter::EmitterState state(program, resources);
  state.stage = ShaderType::Pixel;
  state.pixel_input_info = &pixel_info;

  Spirv::Emitter::InputBinding first;
  first.kind = IR::StageInputKind::Parameter;
  first.location = 0;
  Spirv::Emitter::InputBinding second;
  second.kind = IR::StageInputKind::Parameter;
  second.location = 1;
  state.inputs = {first, second};

  return checks.Require(Spirv::Emitter::PixelParameterLocation(state, 1u) == 1u,
                        "colliding pixel parameter must use its first free fallback location");
}

std::vector<uint32_t> MakeEmbeddedFetchCode() {
  return {
      EncodeSMovB32(0u, InlineU32(0u)),
      EncodeSmem0(0x02u, 20u, 4u),
      EncodeSmem1(0u),
      EncodeVop2(0x01u, 0u, Vgpr(8u), 5u),
      EncodeVop3BWord0(0x30fu, 0u, 0u),
      EncodeVop3Word1(18u, Vgpr(0u)),
      EncodeMubuf0(0x03u, 0u, true),
      EncodeMubuf1(9u, 5u, 0u),
      EndProgram(),
  };
}

ShaderVertexInputInfo MakeEmbeddedFetchInputInfo() {
  ShaderVertexInputInfo vertex {};
  vertex.fetch_embedded = true;
  vertex.fetch_buffer_reg = 0;
  vertex.fetch_attrib_reg = 2;
  vertex.resources_num = 1;
  vertex.resources_dst[0].attr_id = 0;
  vertex.resources_dst[0].registers_num = 4;
  vertex.resource_fetch_components[0] = 1;
  return vertex;
}

bool CheckEmbeddedFetchRegisterRangeValidation(Checks& checks) {
  const std::vector<uint32_t> truncated_mubuf = {EncodeMubuf0(0x0cu)};
  const auto CheckFailure = [&](bool attribute_register, int register_value,
                                std::string_view expected_error, std::string_view label) {
    auto vertex = MakeEmbeddedFetchInputInfo();
    if (attribute_register) {
      vertex.fetch_attrib_reg = register_value;
    } else {
      vertex.fetch_buffer_reg = register_value;
    }
    CompileOptions options;
    options.stage = ShaderType::Vertex;
    options.vertex_input_info = &vertex;
    return PreservedAfterFailure(checks, truncated_mubuf, options, expected_error, label);
  };

  bool ok = true;
  ok = CheckFailure(true, std::numeric_limits<int>::max(),
                    "embedded vertex fetch attribute register is out of range",
                    "maximum embedded fetch attribute register") &&
       ok;
  ok = CheckFailure(true, std::numeric_limits<int>::min(),
                    "embedded vertex fetch attribute register is out of range",
                    "minimum embedded fetch attribute register") &&
       ok;
  ok = CheckFailure(false, std::numeric_limits<int>::max(),
                    "embedded vertex fetch buffer register is out of range",
                    "maximum embedded fetch buffer register") &&
       ok;
  ok = CheckFailure(false, std::numeric_limits<int>::min(),
                    "embedded vertex fetch buffer register is out of range",
                    "minimum embedded fetch buffer register") &&
       ok;
  return ok;
}

bool CheckEmbeddedFetchCallerMetadataImmutability(Checks& checks) {
  const auto code = MakeEmbeddedFetchCode();
  std::array<uint32_t, 11> user_data {};
  user_data[10] = 7u;
  const auto FetchComponents = [](const ShaderVertexInputInfo& vertex) {
    std::array<int, ShaderVertexInputInfo::RES_MAX> components {};
    std::copy_n(vertex.resource_fetch_components, components.size(), components.begin());
    return components;
  };

  auto vertex = MakeEmbeddedFetchInputInfo();
  const auto initial_components = FetchComponents(vertex);
  CompileOptions options;
  options.stage = ShaderType::Vertex;
  options.user_data_base = 8u;
  options.user_data_count = static_cast<uint32_t>(user_data.size());
  options.user_data = user_data.data();
  options.vertex_input_info = &vertex;

  CompileResult result;
  std::string error;
  bool ok = true;
  const auto compiled = Compile(code, ShaderType::Vertex, result, error, &options);
  ok = checks.Require(compiled, "embedded fetch shader must compile: " + error) && ok;
  const auto rewritten = std::any_of(result.program.blocks.begin(), result.program.blocks.end(),
                                     [](const auto& block) {
                                       return std::any_of(
                                           block.instructions.begin(), block.instructions.end(),
                                           [](const auto& inst) {
                                             return inst.op == IR::Opcode::LoadInputF32;
                                           });
                                     });
  ok = checks.Require(rewritten, "embedded fetch shader must rewrite the buffer load") && ok;
  const auto collected_input = std::any_of(
      result.program.info.inputs.begin(), result.program.info.inputs.end(), [](const auto& input) {
        return input.kind == IR::StageInputKind::Parameter && input.location == 0u &&
               input.component_count == 4u;
      });
  ok = checks.Require(collected_input,
                      "embedded fetch rewrite must reach vertex input collection") &&
       ok;
  ok = checks.Require(FetchComponents(vertex) == initial_components,
                      "successful embedded fetch recompilation must not mutate caller metadata") &&
       ok;

  auto failing_vertex = MakeEmbeddedFetchInputInfo();
  const auto failing_initial_components = FetchComponents(failing_vertex);
  CompileOptions failing_options = options;
  failing_options.vertex_input_info = &failing_vertex;
  failing_options.user_data_count = 65u;
  ok = PreservedAfterFailure(checks, code, failing_options,
                             "scalar provenance user-data count exceeds 64 SGPRs",
                             "embedded fetch late failure") &&
       ok;
  ok = checks.Require(FetchComponents(failing_vertex) == failing_initial_components,
                      "failing embedded fetch recompilation must not mutate caller metadata") &&
       ok;
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  const char* output_path = nullptr;
  const char* output_directory = nullptr;
  for (int arg_index = 1; arg_index < argc; arg_index++) {
    const std::string_view argument = argv[arg_index];
    if (argument == "--output" && output_path == nullptr && arg_index + 1 < argc) {
      output_path = argv[++arg_index];
      continue;
    }
    if (argument == "--output-dir" && output_directory == nullptr && arg_index + 1 < argc) {
      output_directory = argv[++arg_index];
      continue;
    }
    std::cerr << "usage: kajps5_gpu_shader_emitter_test [--output path] [--output-dir directory]\n";
    return 2;
  }

  Checks checks;
  const std::vector<uint32_t> end_only = {EndProgram()};
  const std::vector<uint32_t> truncated_mubuf = {EncodeMubuf0(0x0cu)};

  CompileResult first_compute;
  std::string error;
  if (!checks.Require(Compile(end_only, ShaderType::Compute, first_compute, error),
                      "minimal compute shader must compile: " + error)) {
    return 1;
  }
  checks.Require(!first_compute.spirv.empty() && first_compute.spirv.front() == kSpirvMagic,
                 "compute compilation must produce a SPIR-V module");

  CompileResult second_compute;
  checks.Require(Compile(end_only, ShaderType::Compute, second_compute, error),
                 "second minimal compute shader must compile: " + error);
  checks.Require(first_compute.spirv == second_compute.spirv,
                 "compute SPIR-V output must be deterministic");

  const std::vector<uint32_t> wave32_code = {EncodeSMovB32(0u, InlineU32(0u)), EndProgram()};
  ShaderComputeInputInfo wave32_input {};
  wave32_input.wave_size = 0u;
  wave32_input.threads_num[0] = 32u;
  wave32_input.threads_num[1] = 1u;
  wave32_input.threads_num[2] = 1u;
  wave32_input.tg_size_en = true;
  wave32_input.workgroup_register = 0;
  CompileOptions wave32_options;
  wave32_options.wave_size = 32u;
  wave32_options.compute_input_info = &wave32_input;
  CompileResult wave32_result;
  checks.Require(Compile(wave32_code, ShaderType::Compute, wave32_result, error, &wave32_options),
                 "wave-32 compute shader must compile: " + error);
  checks.Require(HasUdivByU32Constant(wave32_result.spirv, 32u),
                 "compute TG-size wave division must use the compilation wave size");

  CompileResult vertex;
  checks.Require(Compile(end_only, ShaderType::Vertex, vertex, error),
                 "minimal vertex shader must compile: " + error);
  CompileResult pixel;
  checks.Require(Compile(end_only, ShaderType::Pixel, pixel, error),
                 "minimal pixel shader must compile: " + error);

  const std::vector<uint32_t> pixel_parameter_collision = {
      EncodeVintrp(0x02u, 0u, 0u, 0u, 2u),
      EncodeVintrp(0x02u, 1u, 1u, 0u, 2u),
      EndProgram(),
  };
  ShaderPixelInputInfo collision_pixel_info {};
  collision_pixel_info.input_num = 2u;
  collision_pixel_info.interpolator_settings[0] = 5u;
  collision_pixel_info.interpolator_settings[1] = 5u;
  CompileOptions collision_options;
  collision_options.pixel_input_info = &collision_pixel_info;
  CompileResult pixel_collision_result;
  checks.Require(Compile(pixel_parameter_collision, ShaderType::Pixel, pixel_collision_result,
                         error, &collision_options),
                 "pixel parameter collision shader must compile: " + error);
  checks.Require(HasLocationDecoration(pixel_collision_result.spirv, 5u) &&
                     HasLocationDecoration(pixel_collision_result.spirv, 1u),
                 "pixel parameter collision shader must use a free fallback location");

  const std::vector<uint32_t> literal = {
      EncodeVop1(0x01u, 0u, 255u), 0x3f800000u, EndProgram()};
  CompileResult literal_result;
  checks.Require(Compile(literal, ShaderType::Compute, literal_result, error),
                 "literal move shader must compile: " + error);
  checks.Require(std::find(literal_result.spirv.begin(), literal_result.spirv.end(), 0x3f800000u) !=
                     literal_result.spirv.end(),
                 "literal constant must be preserved in SPIR-V");

  const std::vector<uint32_t> float_modifiers = {
      EncodeVop3Word0(0x181u, 0u, 1u), EncodeVop3Word1(255u, 0u, 0u, 0u, 1u),
      0x3f800000u, EndProgram()};
  CompileResult float_result;
  checks.Require(Compile(float_modifiers, ShaderType::Compute, float_result, error),
                 "float source-modifier shader must compile: " + error);
  checks.Require(ContainsGlslExtInstruction(float_result.spirv, 4u),
                 "float ABS source modifier must emit GLSL.std.450 FAbs");
  checks.Require(ContainsInstruction(float_result.spirv, kOpFNegate),
                 "float NEG source modifier must emit OpFNegate");

  std::array<uint32_t, 64> user_data {};
  user_data[2] = sizeof(uint32_t);
  user_data[50] = 1u << 20u;
  std::vector<uint32_t> memory = {0x11223344u, 0u};
  CompileOptions resource_options;
  resource_options.user_data = user_data.data();
  resource_options.read_memory = ReadWords;
  resource_options.read_memory_data = &memory;
  const std::vector<uint32_t> buffer_load_store = {
      EncodeMubuf0(0x0cu), EncodeMubuf1(0u, 0u, 30u),
      EncodeVop1(0x01u, 31u, 132u), EncodeMubuf0(0x1cu), EncodeMubuf1(0u, 12u, 31u),
      EndProgram()};
  CompileResult resource_result;
  checks.Require(Compile(buffer_load_store, ShaderType::Compute, resource_result, error,
                         &resource_options),
                 "buffer resource shader must compile: " + error);
  checks.Require(!resource_result.program.info.buffers.empty() &&
                     !resource_result.program.bindings.descriptors.empty() &&
                     !resource_result.resources.buffers.empty(),
                 "buffer shader must materialize resource bindings");

  CompileOptions empty_options;
  empty_options.stage = ShaderType::Compute;
  PreservedAfterFailure(checks, {}, empty_options, "invalid shader recompiler input", "empty input");

  CompileOptions invalid_stage;
  invalid_stage.stage = ShaderType::Fetch;
  PreservedAfterFailure(checks, end_only, invalid_stage,
                        "shader recompiler supports compute, vertex, and pixel stages",
                        "unsupported stage");

  CompileOptions invalid_wave;
  invalid_wave.stage = ShaderType::Compute;
  invalid_wave.wave_size = 16u;
  PreservedAfterFailure(checks, end_only, invalid_wave, "shader wave size must be 32 or 64",
                        "unsupported wave size");

  ShaderVertexInputInfo invalid_vertex_input {};
  invalid_vertex_input.resources_num = ShaderVertexInputInfo::RES_MAX + 1;
  CompileOptions invalid_vertex_metadata;
  invalid_vertex_metadata.stage = ShaderType::Vertex;
  invalid_vertex_metadata.vertex_input_info = &invalid_vertex_input;
  PreservedAfterFailure(checks, truncated_mubuf, invalid_vertex_metadata,
                        "vertex shader metadata resources_num must be between 0 and 32",
                        "invalid vertex resource count");

  invalid_vertex_input.resources_num = -1;
  PreservedAfterFailure(checks, truncated_mubuf, invalid_vertex_metadata,
                        "vertex shader metadata resources_num must be between 0 and 32",
                        "negative vertex resource count");

  ShaderPixelInputInfo invalid_pixel_input {};
  invalid_pixel_input.input_num = 33u;
  CompileOptions invalid_pixel_metadata;
  invalid_pixel_metadata.stage = ShaderType::Pixel;
  invalid_pixel_metadata.pixel_input_info = &invalid_pixel_input;
  PreservedAfterFailure(checks, truncated_mubuf, invalid_pixel_metadata,
                        "pixel shader metadata input_num exceeds 32", "invalid pixel input count");

  ShaderComputeInputInfo invalid_compute_input {};
  invalid_compute_input.thread_ids_num = 4;
  CompileOptions invalid_compute_metadata;
  invalid_compute_metadata.stage = ShaderType::Compute;
  invalid_compute_metadata.compute_input_info = &invalid_compute_input;
  PreservedAfterFailure(checks, truncated_mubuf, invalid_compute_metadata,
                        "compute shader metadata thread_ids_num must be between 0 and 3",
                        "invalid compute thread ID count");

  ShaderComputeInputInfo conflicting_wave_input {};
  conflicting_wave_input.wave_size = 32u;
  CompileOptions conflicting_wave_metadata;
  conflicting_wave_metadata.stage = ShaderType::Compute;
  conflicting_wave_metadata.compute_input_info = &conflicting_wave_input;
  PreservedAfterFailure(checks, truncated_mubuf, conflicting_wave_metadata,
                        "compute shader metadata wave_size must match shader wave size",
                        "conflicting compute wave size");

  CompileOptions oversized_synthetic_data;
  oversized_synthetic_data.stage = ShaderType::Compute;
  oversized_synthetic_data.user_data_count = 65u;
  PreservedAfterFailure(checks, end_only, oversized_synthetic_data,
                        "scalar provenance user-data count exceeds 64 SGPRs", "synthetic user data range");

  checks.Require(kajps5::gpu::ShaderClassifyResourceDescriptor(nullptr) ==
                     ResourceDescriptorType::Unused,
                 "null resource descriptor must be classified as unused");
  CheckExactSubgroupSemantics(checks);
  CheckEmitterTransactionalFailure(checks);
  CheckPixelParameterCollisionFallback(checks);
  CheckEmbeddedFetchRegisterRangeValidation(checks);
  CheckEmbeddedFetchCallerMetadataImmutability(checks);

  if (checks.failures != 0) {
    return 1;
  }
  if (output_path != nullptr && !WriteLittleEndianSpirv(output_path, first_compute.spirv)) {
    std::cerr << "gpu_shader_emitter_test: failed to write " << output_path << '\n';
    return 1;
  }
  if (output_directory != nullptr &&
      !WriteRepresentativeModules(output_directory, first_compute, vertex, pixel, resource_result,
                                  wave32_result, float_result, pixel_collision_result)) {
    std::cerr << "gpu_shader_emitter_test: failed to write representative modules to "
              << output_directory << '\n';
    return 1;
  }
  return 0;
}
