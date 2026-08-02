// Adapted from KytyPS5
// src/graphics/shader/recompiler/emitter/SpirvBuilder.cpp at
// 59b8fad34189816137c5cbe1982e9fd499532b6f.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/shader/spirv_builder.h"

#include <cstddef>
#include <cstring>

namespace kajps5::gpu::shader {
namespace {

constexpr std::size_t kInitialSectionReserve = 4096;
constexpr std::size_t kInitialFunctionSectionReserve = 450000;

void AppendInstructionWords(std::vector<std::uint32_t>& section,
                            const std::uint32_t* words,
                            std::size_t word_count) {
  if (word_count == 0) {
    return;
  }
  const auto opcode = words[0];
  section.push_back((static_cast<std::uint32_t>(word_count) << 16U) |
                    opcode);
  section.insert(section.end(), words + 1, words + word_count);
}

void AppendSection(std::vector<std::uint32_t>& module,
                   const std::vector<std::uint32_t>& section) {
  module.insert(module.end(), section.begin(), section.end());
}

}  // namespace

SpirvBuilder::SpirvBuilder() {
  debug_.reserve(kInitialSectionReserve);
  annotations_.reserve(kInitialSectionReserve);
  types_.reserve(kInitialSectionReserve);
  functions_.reserve(kInitialFunctionSectionReserve);
}

std::uint32_t SpirvBuilder::AllocateId() noexcept { return next_id_++; }

void SpirvBuilder::AppendString(std::vector<std::uint32_t>& words,
                                const char* text) {
  const auto length = text != nullptr ? std::strlen(text) : 0;
  const auto word_count = (length + 4U) / 4U;
  for (std::size_t word_index = 0; word_index < word_count; ++word_index) {
    std::uint32_t word = 0;
    for (std::size_t byte_index = 0; byte_index < 4; ++byte_index) {
      const auto index = word_index * 4U + byte_index;
      if (index < length) {
        word |= static_cast<std::uint32_t>(
                    static_cast<unsigned char>(text[index]))
                << (byte_index * 8U);
      }
    }
    words.push_back(word);
  }
}

void SpirvBuilder::AppendInstruction(
    std::vector<std::uint32_t>& section, std::uint32_t opcode,
    const std::vector<std::uint32_t>& operands) {
  const auto word_count = static_cast<std::uint32_t>(operands.size() + 1U);
  section.push_back((word_count << 16U) | opcode);
  section.insert(section.end(), operands.begin(), operands.end());
}

void SpirvBuilder::AppendInstruction(
    std::vector<std::uint32_t>& section, std::uint32_t opcode,
    std::initializer_list<std::uint32_t> operands) {
  const auto word_count = static_cast<std::uint32_t>(operands.size() + 1U);
  section.push_back((word_count << 16U) | opcode);
  section.insert(section.end(), operands.begin(), operands.end());
}

void SpirvBuilder::AddCapability(
    std::initializer_list<std::uint32_t> operands) {
  AppendInstruction(capabilities_, 17U, operands);
}

void SpirvBuilder::AddExtension(const char* name) {
  std::vector<std::uint32_t> operands;
  AppendString(operands, name);
  AppendInstruction(extensions_, 10U, operands);
}

void SpirvBuilder::AddExtInstImport(std::uint32_t id, const char* name) {
  std::vector<std::uint32_t> operands = {id};
  AppendString(operands, name);
  AppendInstruction(ext_inst_imports_, 11U, operands);
}

void SpirvBuilder::AddMemoryModel(
    std::initializer_list<std::uint32_t> operands) {
  AppendInstruction(memory_model_, 14U, operands);
}

void SpirvBuilder::AddEntryPoint(
    std::uint32_t execution_model, std::uint32_t entry_point,
    const char* name, const std::vector<std::uint32_t>& interfaces) {
  std::vector<std::uint32_t> operands = {execution_model, entry_point};
  AppendString(operands, name);
  operands.insert(operands.end(), interfaces.begin(), interfaces.end());
  AppendInstruction(entry_points_, 15U, operands);
}

void SpirvBuilder::AddExecutionMode(
    std::initializer_list<std::uint32_t> operands) {
  AppendInstruction(execution_modes_, 16U, operands);
}

void SpirvBuilder::AddName(std::uint32_t target, const char* name) {
  std::vector<std::uint32_t> operands = {target};
  AppendString(operands, name);
  AppendInstruction(debug_, 5U, operands);
}

void SpirvBuilder::AddAnnotation(
    std::initializer_list<std::uint32_t> words) {
  AppendInstructionWords(annotations_, words.begin(), words.size());
}

void SpirvBuilder::AddType(std::initializer_list<std::uint32_t> words) {
  AppendInstructionWords(types_, words.begin(), words.size());
}

void SpirvBuilder::AddFunction(
    std::initializer_list<std::uint32_t> words) {
  AppendInstructionWords(functions_, words.begin(), words.size());
}

void SpirvBuilder::AddFunction(const std::vector<std::uint32_t>& words) {
  AppendInstructionWords(functions_, words.data(), words.size());
}

std::vector<std::uint32_t> SpirvBuilder::Build() const {
  std::vector<std::uint32_t> module;
  module.reserve(5U + capabilities_.size() + extensions_.size() +
                 ext_inst_imports_.size() + memory_model_.size() +
                 entry_points_.size() + execution_modes_.size() +
                 debug_.size() + annotations_.size() + types_.size() +
                 functions_.size());

  module.push_back(0x07230203U);
  module.push_back(0x00010300U);
  module.push_back(0U);
  module.push_back(next_id_);
  module.push_back(0U);

  AppendSection(module, capabilities_);
  AppendSection(module, extensions_);
  AppendSection(module, ext_inst_imports_);
  AppendSection(module, memory_model_);
  AppendSection(module, entry_points_);
  AppendSection(module, execution_modes_);
  AppendSection(module, debug_);
  AppendSection(module, annotations_);
  AppendSection(module, types_);
  AppendSection(module, functions_);
  return module;
}

}  // namespace kajps5::gpu::shader
