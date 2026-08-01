// Copyright (C) 2026 KajPS5 contributors
// Implementation reference: KytyPS5
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "gpu/shader/spirv_builder.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "gpu_spirv_builder_test: " << message << '\n';
    ++failures;
  }
}

bool HasValidInstructionBounds(const std::vector<std::uint32_t>& module) {
  if (module.size() < 5 || module[0] != 0x07230203U ||
      module[1] != 0x00010300U || module[4] != 0) {
    return false;
  }
  for (std::size_t offset = 5; offset < module.size();) {
    const auto word_count = module[offset] >> 16U;
    if (word_count == 0 || word_count > module.size() - offset) {
      return false;
    }
    offset += word_count;
  }
  return true;
}

std::vector<std::uint32_t> MakeComputeModule() {
  using kajps5::gpu::shader::SpirvBuilder;

  SpirvBuilder builder;
  const auto void_type = builder.AllocateId();
  const auto function_type = builder.AllocateId();
  const auto entry_point = builder.AllocateId();
  const auto label = builder.AllocateId();

  builder.AddCapability({1});
  builder.AddMemoryModel({0, 1});
  builder.AddEntryPoint(5, entry_point, "main", {});
  builder.AddExecutionMode({entry_point, 17, 1, 1, 1});
  builder.AddName(entry_point, "main");
  builder.AddType({19, void_type});
  builder.AddType({33, function_type, void_type});
  builder.AddFunction({54, void_type, entry_point, 0, function_type});
  builder.AddFunction({248, label});
  builder.AddFunction({253});
  builder.AddFunction({56});
  return builder.Build();
}

}  // namespace

int main() {
  const auto first = MakeComputeModule();
  const auto second = MakeComputeModule();
  Check(first == second, "the same shader produced different SPIR-V words");
  Check(HasValidInstructionBounds(first),
        "the SPIR-V module has an invalid header or instruction boundary");
  Check(first.size() == 39 && first[3] == 5,
        "the SPIR-V module has the wrong size or ID bound");
  return failures == 0 ? 0 : 1;
}
