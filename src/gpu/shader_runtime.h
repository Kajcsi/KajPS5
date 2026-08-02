// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp and src/graphics/shader/shader.h at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.Libs/Agc/AgcExports.cs at
// cf3bd0b4f2016eede08692110b6c14f08b5a912c.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

#include "gpu/shader/recompiler/ShaderRecompiler.h"

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::gpu {

struct GpuShaderBinding;

// The guest header carries a 32-bit byte count with no intrinsic allocation
// bound. Keep copied shader images deliberately small until a later GPU owner
// has a broader, independently checked memory budget.
inline constexpr std::uint32_t kMaximumRegisteredShaderBytes =
    16U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumRegisteredShaderInputSemantics = 32U;

enum class ShaderRuntimeStatus {
  kOk,
  kInvalidArgument,
  kMemoryFault,
  kResourceLimit,
  kNotRegistered,
  kUnsupportedShaderType,
  kStageMismatch,
  kCompilationFailed,
};

// Diagnostic fault points used only by focused runtime and HLE transaction
// tests. Each guarded ShaderRuntime operation snapshots this process-wide
// configuration at entry, so setting and clearing it are thread-safe and
// deterministic for subsequent calls.
enum class ShaderRuntimeTestFaultPoint {
  kNone,
  kBeforeMutationCommitBadAlloc,
  kFailMutationWriteAfterSuccessfulWrites,
  kPartiallyWriteMutationThenFail,
  kBeforeBindingResolutionResourceLimit,
};

struct ShaderRuntimeTestFault {
  ShaderRuntimeTestFaultPoint point = ShaderRuntimeTestFaultPoint::kNone;
  // For mutation-write fault points, zero selects the first write, one selects
  // the second write, and so on.
  std::size_t successful_mutation_writes = 0;
};

struct RegisteredShader {
  std::uint64_t code_address = 0;
  // The reconstructed PGM address and its offset into code_address's checked
  // image. Front halves that defer PGM publication keep has_program_binding
  // false and are intentionally absent from the direct program index.
  std::uint64_t program_address = 0;
  std::uint64_t header_address = 0;
  std::uint64_t program_offset_bytes = 0;
  std::uint32_t code_size_bytes = 0;
  std::uint8_t binary_type = 0;
  bool has_program_binding = false;
  std::uint64_t user_data_address = 0;
  std::uint64_t input_semantics_address = 0;
  std::uint32_t input_semantics_count = 0;
};

struct ShaderMapResult {
  ShaderRuntimeStatus status = ShaderRuntimeStatus::kOk;
  RegisteredShader record;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ShaderRuntimeStatus::kOk;
  }
};

struct ShaderCompileResult {
  ShaderRuntimeStatus status = ShaderRuntimeStatus::kOk;
  std::string error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ShaderRuntimeStatus::kOk;
  }
};

// This registry is owned by GpuRuntime. It stores guest addresses and scalar
// metadata only; every later read returns through the checked GuestMemory API.
class ShaderRuntime final {
 public:
  explicit ShaderRuntime(memory::GuestMemory& memory) noexcept;

  [[nodiscard]] ShaderMapResult CreateShader(std::uint64_t destination_address,
                                             std::uint64_t header_address,
                                             std::uint64_t code_address);
  [[nodiscard]] std::optional<RegisteredShader> Lookup(
      std::uint64_t code_address) const;
  [[nodiscard]] std::optional<RegisteredShader> LookupProgram(
      std::uint64_t program_address) const;
  // Resolves program-address snapshots without allocating or compiling. An
  // unregistered address remains explicitly unregistered for later renderer
  // policy and diagnostics.
  [[nodiscard]] ShaderRuntimeStatus ResolveProgramBindings(
      std::span<GpuShaderBinding> bindings) const noexcept;
  // Returns structured status and error information. `result` is replaced only
  // after the checked image read and TryRecompile both succeed.
  [[nodiscard]] ShaderCompileResult Recompile(
      std::uint64_t code_address,
      const shader::recompiler::CompileOptions& options,
      shader::recompiler::CompileResult& result);

  static void SetCreateShaderTestFaultForTesting(
      ShaderRuntimeTestFault fault) noexcept;
  static void ClearCreateShaderTestFaultForTesting() noexcept;
  static void SetBindingResolutionTestFaultForTesting(
      ShaderRuntimeTestFault fault) noexcept;
  static void ClearBindingResolutionTestFaultForTesting() noexcept;

 private:
  memory::GuestMemory& memory_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, RegisteredShader> records_;
  std::unordered_map<std::uint64_t, std::uint64_t> program_records_;
};

[[nodiscard]] const char* ShaderRuntimeStatusName(
    ShaderRuntimeStatus status) noexcept;

}  // namespace kajps5::gpu
