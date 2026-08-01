// Adapted from KytyPS5
// src/graphics/shader/recompiler/emitter/SpirvBuilder.h at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace kajps5::gpu::shader {

class SpirvBuilder final {
 public:
  SpirvBuilder();

  [[nodiscard]] std::uint32_t AllocateId() noexcept;

  void AddCapability(std::initializer_list<std::uint32_t> operands);
  void AddExtension(const char* name);
  void AddExtInstImport(std::uint32_t id, const char* name);
  void AddMemoryModel(std::initializer_list<std::uint32_t> operands);
  void AddEntryPoint(std::uint32_t execution_model,
                     std::uint32_t entry_point, const char* name,
                     const std::vector<std::uint32_t>& interfaces);
  void AddExecutionMode(std::initializer_list<std::uint32_t> operands);
  void AddName(std::uint32_t target, const char* name);
  void AddAnnotation(std::initializer_list<std::uint32_t> words);
  void AddType(std::initializer_list<std::uint32_t> words);
  void AddFunction(std::initializer_list<std::uint32_t> words);
  void AddFunction(const std::vector<std::uint32_t>& words);

  [[nodiscard]] std::vector<std::uint32_t> Build() const;

 private:
  static void AppendInstruction(
      std::vector<std::uint32_t>& section, std::uint32_t opcode,
      const std::vector<std::uint32_t>& operands);
  static void AppendInstruction(
      std::vector<std::uint32_t>& section, std::uint32_t opcode,
      std::initializer_list<std::uint32_t> operands);
  static void AppendString(std::vector<std::uint32_t>& words,
                           const char* text);

  std::uint32_t next_id_ = 1;
  std::vector<std::uint32_t> capabilities_;
  std::vector<std::uint32_t> extensions_;
  std::vector<std::uint32_t> ext_inst_imports_;
  std::vector<std::uint32_t> memory_model_;
  std::vector<std::uint32_t> entry_points_;
  std::vector<std::uint32_t> execution_modes_;
  std::vector<std::uint32_t> debug_;
  std::vector<std::uint32_t> annotations_;
  std::vector<std::uint32_t> types_;
  std::vector<std::uint32_t> functions_;
};

}  // namespace kajps5::gpu::shader
