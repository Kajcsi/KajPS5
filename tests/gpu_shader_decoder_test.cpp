// Copyright (C) 2026 KajPS5 contributors
// Implementation and test reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "gpu/shader/recompiler/decompiler/ShaderDecoder.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "gpu_shader_decoder_test: " << message << '\n';
    ++failures;
  }
}

constexpr std::uint32_t EncodeScalarMove(std::uint32_t destination,
                                         std::uint32_t source) {
  return 0x80000000U | (0x7dU << 23U) |
         ((destination & 0x7fU) << 16U) | (0x03U << 8U) |
         (source & 0xffU);
}

constexpr std::uint32_t EncodeVectorMove(std::uint32_t destination,
                                         std::uint32_t source) {
  return (0x3fU << 25U) | ((destination & 0xffU) << 17U) |
         (0x01U << 9U) | (source & 0x1ffU);
}

constexpr std::uint32_t EncodeVectorMoveSdwa(
    std::uint32_t source, std::uint32_t source_selection) {
  return (source & 0xffU) | (6U << 8U) |
         ((source_selection & 0x7U) << 16U);
}

}  // namespace

int main() {
  using namespace kajps5::gpu::shader::recompiler::Decoder;

  const std::array code = {
      EncodeScalarMove(2, 135),
      EncodeVectorMove(5, 9),
      EncodeVectorMove(103, 249),
      EncodeVectorMoveSdwa(5, 4),
      0xbf810000U,
  };
  Program program;
  std::string error;
  Check(DecodeProgram(code, program, &error), error.c_str());
  Check(program.instructions.size() == 4,
        "the decoder did not consume the complete instruction stream");
  if (program.instructions.size() == 4) {
    Check(program.instructions[0].opcode == Opcode::SMovB32 &&
              program.instructions[1].opcode == Opcode::VMovB32 &&
              program.instructions[2].opcode == Opcode::VMovB32 &&
              program.instructions[2].word_count == 2 &&
              program.instructions[2].src0.sdwa_sel == 4 &&
              program.instructions[3].opcode == Opcode::SEndpgm,
          "the decoder returned the wrong scalar, vector, or SDWA metadata");
  }
  const auto text = ProgramToString(program);
  Check(text.find("s_mov_b32 s2, 7") != std::string::npos &&
            text.find("v_mov_b32 v5, s9") != std::string::npos &&
            text.find("v_mov_b32 v103, v5.sdwa(sel=4") !=
                std::string::npos &&
            text.find("s_endpgm") != std::string::npos,
        "the decoded diagnostic text lost instruction details");
  return failures == 0 ? 0 : 1;
}
