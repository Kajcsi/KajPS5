// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "core/memory/guest_memory.h"
#include "cpu/native_guest_process_launcher.h"
#include "cpu/native_guest_thread_runner.h"
#include "cpu/native_hle_import_table.h"
#include "hle/data_symbols.h"
#include "hle/export_registry.h"
#include "hle/import_registry.h"
#include "kernel/runtime.h"
#include "loader/elf.h"
#include "loader/launch_metadata.h"
#include "loader/lifecycle_plan.h"
#include "runtime/module_runtime.h"

namespace kajps5::runtime {

enum class TitleSessionPhase {
  kCreated,
  kInitializing,
  kRunning,
  kFinalizing,
  kExited,
  kFailed,
};

enum class TitleSessionStatus {
  kPending,
  kBlocked,
  kExited,
  kInvalidState,
  kStartupFailed,
  kGuestExecutionFailed,
  kFinalizerThreadCreateFailed,
  kFinalizerThreadRegistrationFailed,
  kFinalizerThreadRollbackFailed,
  kSliceLimitReached,
};

struct TitleSessionResult {
  TitleSessionStatus status = TitleSessionStatus::kPending;
  TitleSessionPhase phase = TitleSessionPhase::kCreated;
  kernel::KernelHandle thread = kernel::kInvalidKernelHandle;
  std::uint64_t exit_value = 0;
  std::size_t slices = 0;
  cpu::NativeGuestProcessStartupResult startup;
  cpu::NativeGuestThreadRunResult run;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == TitleSessionStatus::kExited;
  }
};

enum class TitleHleSetupStatus {
  kOk,
  kInvalidState,
  kAlreadyAttempted,
  kDataSetupFailed,
  kKernelExportsFailed,
  kLibcExportsFailed,
  kLibcThreadExportsFailed,
  kJsonExportsFailed,
  kAmprExportsFailed,
  kImportTableBuildFailed,
};

struct TitleHleSetupResult {
  TitleHleSetupStatus status = TitleHleSetupStatus::kOk;
  hle::HleDataResult data;
  hle::ExportRegistryStatus export_status = hle::ExportRegistryStatus::kOk;
  cpu::NativeHleImportTableResult imports;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == TitleHleSetupStatus::kOk;
  }
};

class TitleSession final {
 public:
  [[nodiscard]] static std::unique_ptr<TitleSession> Create(
      std::unique_ptr<memory::GuestMemory> memory);
  [[nodiscard]] static std::unique_ptr<TitleSession> Create(
      std::unique_ptr<memory::GuestMemory> memory,
      loader::ExecutableLaunchMetadata launch_metadata,
      loader::ExecutableLifecyclePlan lifecycle_plan);

  TitleSession(const TitleSession&) = delete;
  TitleSession& operator=(const TitleSession&) = delete;

  [[nodiscard]] bool Configure(loader::ExecutableLaunchMetadata launch_metadata,
                               loader::ExecutableLifecyclePlan lifecycle_plan);
  [[nodiscard]] bool AttachModuleRuntime(
      std::unique_ptr<ModuleRuntime> module_runtime);
  [[nodiscard]] TitleHleSetupResult PrepareHle(
      const loader::ElfMetadata& metadata, std::uint64_t data_page_address,
      std::string_view process_image_name,
      std::size_t stack_argument_count =
          hle::kMaximumCapturedHleStackArguments);
  [[nodiscard]] TitleHleSetupResult PrepareHleBatch(
      std::span<const loader::ElfMetadata* const> metadata,
      std::uint64_t data_page_address, std::string_view process_image_name,
      std::size_t stack_argument_count =
          hle::kMaximumCapturedHleStackArguments);
  [[nodiscard]] TitleSessionResult Start(
      std::string_view process_image_name, std::uint64_t stack_search_start,
      std::span<const std::string_view> extra_arguments = {},
      std::uint64_t exit_handler_address = 0,
      std::uint64_t stack_size = cpu::kDefaultNativeGuestProcessStackSize);
  [[nodiscard]] TitleSessionResult Run(std::size_t maximum_slices);

  [[nodiscard]] TitleSessionPhase phase() const noexcept;
  [[nodiscard]] kernel::KernelHandle main_thread() const noexcept;
  [[nodiscard]] std::uint64_t exit_value() const noexcept;
  [[nodiscard]] memory::GuestMemory& memory() noexcept;
  [[nodiscard]] kernel::KernelRuntime& kernel_runtime() noexcept;
  [[nodiscard]] cpu::NativeGuestExecutionContext& execution_context() noexcept;
  [[nodiscard]] cpu::NativeGuestThreadRunner& thread_runner() noexcept;
  [[nodiscard]] const hle::ExportRegistry& hle_exports() const noexcept;
  [[nodiscard]] const hle::ImportRegistry& hle_data() const noexcept;
  [[nodiscard]] const cpu::NativeHleImportTable* hle_functions() const noexcept;
  [[nodiscard]] const ModuleRuntime* module_runtime() const noexcept;

 private:
  enum class FinalizationKind {
    kAtexit,
    kCxaDestructor,
    kFinalizer,
  };

  struct FinalizationCall {
    std::uint64_t address = 0;
    std::array<std::uint64_t, 1> arguments{};
    std::size_t argument_count = 0;
    FinalizationKind kind = FinalizationKind::kFinalizer;
  };

  explicit TitleSession(std::unique_ptr<memory::GuestMemory> memory);

  [[nodiscard]] TitleSessionResult PrepareFinalization();
  [[nodiscard]] TitleSessionResult StartNextFinalizer();
  [[nodiscard]] TitleSessionResult FinishFailure(
      TitleSessionStatus status, kernel::KernelHandle thread,
      cpu::NativeGuestThreadRunResult run = {}, std::size_t slices = 0);

  std::unique_ptr<memory::GuestMemory> memory_;
  loader::ExecutableLaunchMetadata launch_metadata_;
  loader::ExecutableLifecyclePlan lifecycle_plan_;
  kernel::KernelRuntime kernel_runtime_;
  cpu::NativeGuestExecutionContext execution_context_;
  hle::ExportRegistry hle_exports_;
  hle::ImportRegistry hle_data_;
  std::unique_ptr<cpu::NativeHleImportTable> hle_functions_;
  std::unique_ptr<ModuleRuntime> module_runtime_;
  cpu::NativeGuestThreadRunner thread_runner_;
  cpu::NativeGuestProcessLauncher process_launcher_;
  bool configured_ = false;
  bool hle_preparation_attempted_ = false;
  TitleSessionPhase phase_ = TitleSessionPhase::kCreated;
  kernel::KernelHandle main_thread_ = kernel::kInvalidKernelHandle;
  std::uint64_t exit_value_ = 0;
  std::vector<FinalizationCall> finalization_calls_;
  std::size_t next_finalization_call_ = 0;
  kernel::KernelHandle active_finalizer_ = kernel::kInvalidKernelHandle;
};

[[nodiscard]] std::string_view TitleSessionPhaseName(
    TitleSessionPhase phase) noexcept;
[[nodiscard]] std::string_view TitleSessionStatusName(
    TitleSessionStatus status) noexcept;
[[nodiscard]] std::string_view TitleHleSetupStatusName(
    TitleHleSetupStatus status) noexcept;

}  // namespace kajps5::runtime
