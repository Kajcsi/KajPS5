// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime/title_session.h"

#include <limits>
#include <utility>

#include "hle/agc_exports.h"
#include "hle/ampr_exports.h"
#include "hle/json_exports.h"
#include "hle/kernel_exports.h"
#include "hle/libc_exports.h"
#include "hle/libc_thread_exports.h"
#include "hle/video_out_exports.h"

namespace kajps5::runtime {
namespace {

bool IsGuestRunFailure(cpu::NativeGuestThreadRunStatus status) noexcept {
  return status != cpu::NativeGuestThreadRunStatus::kIdle &&
         status != cpu::NativeGuestThreadRunStatus::kThreadExited &&
         status != cpu::NativeGuestThreadRunStatus::kThreadBlocked &&
         status != cpu::NativeGuestThreadRunStatus::kThreadYielded &&
         status != cpu::NativeGuestThreadRunStatus::kExecutionLaneBusy;
}

}  // namespace

std::unique_ptr<TitleSession> TitleSession::Create(
    std::unique_ptr<memory::GuestMemory> memory) {
  if (!memory || !memory->host_mapped()) {
    return nullptr;
  }
  return std::unique_ptr<TitleSession>(new TitleSession(std::move(memory)));
}

std::unique_ptr<TitleSession> TitleSession::Create(
    std::unique_ptr<memory::GuestMemory> memory,
    loader::ExecutableLaunchMetadata launch_metadata,
    loader::ExecutableLifecyclePlan lifecycle_plan) {
  auto session = Create(std::move(memory));
  if (!session || !session->Configure(std::move(launch_metadata),
                                      std::move(lifecycle_plan))) {
    return nullptr;
  }
  return session;
}

TitleSession::TitleSession(std::unique_ptr<memory::GuestMemory> memory)
    : memory_(std::move(memory)),
      gpu_runtime_(*memory_, nullptr, &kernel_runtime_.event_queues()),
      video_out_(*memory_, gpu_runtime_, kernel_runtime_.event_queues()),
      thread_runner_(*memory_, kernel_runtime_.scheduler(),
                     kernel_runtime_.pthreads(), execution_context_),
      process_launcher_(kernel_runtime_.pthreads(), thread_runner_) {}

bool TitleSession::Configure(loader::ExecutableLaunchMetadata launch_metadata,
                             loader::ExecutableLifecyclePlan lifecycle_plan) {
  if (phase_ != TitleSessionPhase::kCreated || configured_) {
    return false;
  }
  launch_metadata_ = std::move(launch_metadata);
  lifecycle_plan_ = std::move(lifecycle_plan);
  configured_ = true;
  return true;
}

bool TitleSession::AttachModuleRuntime(
    std::unique_ptr<ModuleRuntime> module_runtime) {
  if (phase_ != TitleSessionPhase::kCreated || configured_ || module_runtime_ ||
      !module_runtime) {
    return false;
  }
  module_runtime_ = std::move(module_runtime);
  return true;
}

TitleHleSetupResult TitleSession::PrepareHle(
    const loader::ElfMetadata& metadata, std::uint64_t data_page_address,
    std::string_view process_image_name, std::size_t stack_argument_count) {
  const loader::ElfMetadata* metadata_pointer = &metadata;
  return PrepareHleBatch(
      std::span<const loader::ElfMetadata* const>(&metadata_pointer, 1),
      data_page_address, process_image_name, stack_argument_count);
}

TitleHleSetupResult TitleSession::PrepareHleBatch(
    std::span<const loader::ElfMetadata* const> metadata,
    std::uint64_t data_page_address, std::string_view process_image_name,
    std::size_t stack_argument_count) {
  if (phase_ != TitleSessionPhase::kCreated) {
    return {TitleHleSetupStatus::kInvalidState};
  }
  if (hle_preparation_attempted_) {
    return {TitleHleSetupStatus::kAlreadyAttempted};
  }
  hle_preparation_attempted_ = true;

  TitleHleSetupResult result;
  result.data = hle::InstallHleDataSymbols(
      hle_data_, *memory_, data_page_address, process_image_name);
  if (!result.data) {
    result.status = TitleHleSetupStatus::kDataSetupFailed;
    return result;
  }
  bool pthread_arena_mapped = false;
  bool pthread_arena_configured = false;
  const auto rollback_mappings = [&] {
    hle_functions_.reset();
    bool arena_released = true;
    if (pthread_arena_configured) {
      arena_released = kernel_runtime_.pthreads().ReleaseGuestMutexArena();
      if (arena_released) {
        pthread_arena_configured = false;
      }
    }
    if (pthread_arena_mapped && arena_released) {
      (void)memory_->Unmap(pthread_mutex_arena_address_,
                           kernel::kPthreadMutexArenaSize);
      pthread_arena_mapped = false;
    }
    if (!pthread_arena_mapped) {
      pthread_mutex_arena_address_ = 0;
    }
    (void)memory_->Unmap(data_page_address, hle::kHleDataPageSize);
  };
  if (data_page_address > std::numeric_limits<std::uint64_t>::max() -
                              hle::kHleDataPageSize) {
    result.status = TitleHleSetupStatus::kPthreadArenaSetupFailed;
    rollback_mappings();
    return result;
  }
  pthread_mutex_arena_address_ = data_page_address + hle::kHleDataPageSize;
  constexpr auto read_write = memory::GuestMemoryProtection::kRead |
                              memory::GuestMemoryProtection::kWrite;
  if (!memory_->Map(pthread_mutex_arena_address_,
                    kernel::kPthreadMutexArenaSize, read_write)) {
    result.status = TitleHleSetupStatus::kPthreadArenaSetupFailed;
    rollback_mappings();
    return result;
  }
  pthread_arena_mapped = true;
  if (!kernel_runtime_.pthreads().ConfigureGuestMutexArena(
          *memory_, pthread_mutex_arena_address_)) {
    result.status = TitleHleSetupStatus::kPthreadArenaSetupFailed;
    rollback_mappings();
    return result;
  }
  pthread_arena_configured = true;
  kernel_runtime_.SetSanitizerMallocReplaceAddress(
      result.data.sanitizer_malloc_replace_address);
  kernel_runtime_.SetSanitizerNewReplaceAddress(
      result.data.sanitizer_new_replace_address);

  result.export_status =
      hle::RegisterKernelExports(hle_exports_, kernel_runtime_);
  if (result.export_status != hle::ExportRegistryStatus::kOk) {
    result.status = TitleHleSetupStatus::kKernelExportsFailed;
    rollback_mappings();
    return result;
  }
  result.export_status =
      hle::RegisterLibcExports(hle_exports_, kernel_runtime_.cxa_guards(),
                               kernel_runtime_.process_lifecycle(),
                               kernel_runtime_.libc_heap(), *memory_);
  if (result.export_status != hle::ExportRegistryStatus::kOk) {
    result.status = TitleHleSetupStatus::kLibcExportsFailed;
    rollback_mappings();
    return result;
  }
  result.export_status =
      hle::RegisterLibcThreadExports(hle_exports_, kernel_runtime_.pthreads());
  if (result.export_status != hle::ExportRegistryStatus::kOk) {
    result.status = TitleHleSetupStatus::kLibcThreadExportsFailed;
    rollback_mappings();
    return result;
  }
  result.export_status =
      hle::RegisterJsonExports(hle_exports_, kernel_runtime_.json_values());
  if (result.export_status != hle::ExportRegistryStatus::kOk) {
    result.status = TitleHleSetupStatus::kJsonExportsFailed;
    rollback_mappings();
    return result;
  }
  result.export_status = hle::RegisterAmprExports(
      hle_exports_, kernel_runtime_.ampr_command_buffers(), *memory_);
  if (result.export_status != hle::ExportRegistryStatus::kOk) {
    result.status = TitleHleSetupStatus::kAmprExportsFailed;
    rollback_mappings();
    return result;
  }
  result.export_status = hle::RegisterAgcExports(
      hle_exports_, gpu_runtime_, kernel_runtime_.event_queues());
  if (result.export_status != hle::ExportRegistryStatus::kOk) {
    result.status = TitleHleSetupStatus::kAgcExportsFailed;
    rollback_mappings();
    return result;
  }
  result.export_status = hle::RegisterVideoOutExports(hle_exports_, video_out_);
  if (result.export_status != hle::ExportRegistryStatus::kOk) {
    result.status = TitleHleSetupStatus::kVideoOutExportsFailed;
    rollback_mappings();
    return result;
  }

  for (const auto* image_metadata : metadata) {
    if (image_metadata == nullptr) {
      result.status = TitleHleSetupStatus::kImportTableBuildFailed;
      rollback_mappings();
      return result;
    }
    result.stub_status = hle::RegisterUnresolvedImportStubs(
        *image_metadata, hle_exports_, hle_data_, unresolved_import_stubs_);
    if (!result.stub_status) {
      result.status = TitleHleSetupStatus::kImportTableBuildFailed;
      rollback_mappings();
      return result;
    }
  }

  hle_functions_ = std::make_unique<cpu::NativeHleImportTable>(
      *memory_, hle_exports_, &execution_context_);
  result.imports = hle_functions_->BuildBatch(metadata, stack_argument_count);
  if (!result.imports) {
    result.status = TitleHleSetupStatus::kImportTableBuildFailed;
    rollback_mappings();
  }
  return result;
}

TitleSessionResult TitleSession::Start(
    std::string_view process_image_name, std::uint64_t stack_search_start,
    std::span<const std::string_view> extra_arguments,
    std::uint64_t exit_handler_address, std::uint64_t stack_size) {
  if (phase_ != TitleSessionPhase::kCreated || !configured_) {
    return {TitleSessionStatus::kInvalidState, phase_};
  }
  if (module_runtime_ != nullptr &&
      module_runtime_->tls_layout().module_count() != 0) {
    std::vector<loader::StaticTlsTemplateModule> tls_modules;
    for (const auto& program : module_runtime_->programs()) {
      if (!program.launch.tls.has_value()) {
        continue;
      }
      tls_modules.push_back(
          {program.module_id, program.launch.tls->image_address,
           program.launch.tls->initial_size, program.launch.tls->memory_size,
           program.tls.module.static_offset});
    }
    if (!thread_runner_.InstallStaticTlsTemplates(
            module_runtime_->tls_layout(), std::move(tls_modules))) {
      phase_ = TitleSessionPhase::kFailed;
      return {TitleSessionStatus::kStartupFailed, phase_};
    }
  }
  const auto startup = process_launcher_.BeginStartup(
      launch_metadata_, lifecycle_plan_, process_image_name, stack_search_start,
      extra_arguments, exit_handler_address, stack_size);
  if (startup.status == cpu::NativeGuestProcessStartupStatus::kReady) {
    main_thread_ = startup.launch.thread;
    phase_ = TitleSessionPhase::kRunning;
    return {TitleSessionStatus::kPending, phase_, main_thread_, 0, 0, startup};
  }
  if (startup.status == cpu::NativeGuestProcessStartupStatus::kPending ||
      startup.status == cpu::NativeGuestProcessStartupStatus::kBlocked) {
    phase_ = TitleSessionPhase::kInitializing;
    return {startup.status == cpu::NativeGuestProcessStartupStatus::kBlocked
                ? TitleSessionStatus::kBlocked
                : TitleSessionStatus::kPending,
            phase_,
            startup.thread,
            0,
            0,
            startup};
  }
  phase_ = TitleSessionPhase::kFailed;
  return {TitleSessionStatus::kStartupFailed,
          phase_,
          startup.thread,
          0,
          0,
          startup};
}

TitleSessionResult TitleSession::Run(std::size_t maximum_slices) {
  if (phase_ == TitleSessionPhase::kCreated ||
      phase_ == TitleSessionPhase::kExited ||
      phase_ == TitleSessionPhase::kFailed) {
    return {TitleSessionStatus::kInvalidState, phase_, main_thread_,
            exit_value_};
  }

  std::size_t slices = 0;
  while (slices < maximum_slices) {
    if (phase_ == TitleSessionPhase::kInitializing) {
      const auto startup = process_launcher_.ContinueStartup();
      slices += startup.slices;
      if (startup.run.thread != kernel::kInvalidKernelHandle &&
          startup.run.thread != startup.thread &&
          IsGuestRunFailure(startup.run.status)) {
        return FinishFailure(TitleSessionStatus::kGuestExecutionFailed,
                             startup.run.thread, startup.run, slices);
      }
      if (startup.status == cpu::NativeGuestProcessStartupStatus::kReady) {
        main_thread_ = startup.launch.thread;
        phase_ = TitleSessionPhase::kRunning;
        return {TitleSessionStatus::kPending,
                phase_,
                main_thread_,
                exit_value_,
                slices,
                startup};
      }
      if (startup.status == cpu::NativeGuestProcessStartupStatus::kBlocked) {
        return {TitleSessionStatus::kBlocked,
                phase_,
                startup.thread,
                exit_value_,
                slices,
                startup};
      }
      if (startup.status == cpu::NativeGuestProcessStartupStatus::kPending) {
        return {TitleSessionStatus::kPending,
                phase_,
                startup.thread,
                exit_value_,
                slices,
                startup};
      }
      phase_ = TitleSessionPhase::kFailed;
      return {TitleSessionStatus::kStartupFailed,
              phase_,
              startup.thread,
              exit_value_,
              slices,
              startup};
    }

    if (phase_ == TitleSessionPhase::kRunning) {
      const auto run = thread_runner_.RunNext();
      slices += run.slices;
      if (run.status == cpu::NativeGuestThreadRunStatus::kIdle ||
          run.status == cpu::NativeGuestThreadRunStatus::kThreadBlocked) {
        return {TitleSessionStatus::kBlocked,
                phase_,
                run.thread,
                exit_value_,
                slices,
                {},
                run};
      }
      if (run.status == cpu::NativeGuestThreadRunStatus::kThreadYielded) {
        continue;
      }
      if (run.status != cpu::NativeGuestThreadRunStatus::kThreadExited) {
        return FinishFailure(TitleSessionStatus::kGuestExecutionFailed,
                             run.thread, run, slices);
      }
      if (run.thread != main_thread_) {
        continue;
      }
      const auto main = kernel_runtime_.scheduler().Snapshot(main_thread_);
      if (!main) {
        return FinishFailure(TitleSessionStatus::kGuestExecutionFailed,
                             main_thread_, run, slices);
      }
      exit_value_ = main->exit_value;
      auto finalization = PrepareFinalization();
      if (finalization.status != TitleSessionStatus::kPending) {
        finalization.slices += slices;
        return finalization;
      }
      if (phase_ == TitleSessionPhase::kExited) {
        return {TitleSessionStatus::kExited,
                phase_,
                main_thread_,
                exit_value_,
                slices,
                {},
                run};
      }
      continue;
    }

    if (phase_ == TitleSessionPhase::kFinalizing) {
      const auto run = thread_runner_.RunNext();
      slices += run.slices;
      if (run.status == cpu::NativeGuestThreadRunStatus::kIdle ||
          run.status == cpu::NativeGuestThreadRunStatus::kThreadBlocked) {
        return {TitleSessionStatus::kBlocked,
                phase_,
                active_finalizer_,
                exit_value_,
                slices,
                {},
                run};
      }
      if (run.status == cpu::NativeGuestThreadRunStatus::kThreadYielded) {
        continue;
      }
      if (run.status != cpu::NativeGuestThreadRunStatus::kThreadExited ||
          run.execution.status != cpu::NativeGuestExecutionStatus::kOk) {
        return FinishFailure(TitleSessionStatus::kGuestExecutionFailed,
                             run.thread, run, slices);
      }
      if (run.thread != active_finalizer_) {
        continue;
      }
      ++next_finalization_call_;
      active_finalizer_ = kernel::kInvalidKernelHandle;
      if (next_finalization_call_ >= finalization_calls_.size()) {
        phase_ = TitleSessionPhase::kExited;
        return {TitleSessionStatus::kExited,
                phase_,
                main_thread_,
                exit_value_,
                slices,
                {},
                run};
      }
      auto next = StartNextFinalizer();
      if (next.status != TitleSessionStatus::kPending) {
        next.slices += slices;
        return next;
      }
      continue;
    }
  }

  return {TitleSessionStatus::kSliceLimitReached, phase_, main_thread_,
          exit_value_, slices};
}

TitleSessionResult TitleSession::PrepareFinalization() {
  finalization_calls_.clear();
  for (const auto callback :
       kernel_runtime_.process_lifecycle().PendingAtexitCallbacks()) {
    finalization_calls_.push_back({callback, {}, 0, FinalizationKind::kAtexit});
  }
  for (const auto& destructor :
       kernel_runtime_.process_lifecycle().PendingCxaDestructors()) {
    finalization_calls_.push_back({destructor.function,
                                   {destructor.argument},
                                   1,
                                   FinalizationKind::kCxaDestructor});
  }
  for (const auto finalizer : lifecycle_plan_.finalizers) {
    finalization_calls_.push_back(
        {finalizer, {}, 0, FinalizationKind::kFinalizer});
  }
  next_finalization_call_ = 0;
  if (finalization_calls_.empty()) {
    phase_ = TitleSessionPhase::kExited;
    return {TitleSessionStatus::kPending, phase_, main_thread_, exit_value_};
  }
  phase_ = TitleSessionPhase::kFinalizing;
  return StartNextFinalizer();
}

TitleSessionResult TitleSession::StartNextFinalizer() {
  if (phase_ != TitleSessionPhase::kFinalizing ||
      next_finalization_call_ >= finalization_calls_.size()) {
    return {TitleSessionStatus::kInvalidState, phase_, active_finalizer_,
            exit_value_};
  }
  const auto& call = finalization_calls_[next_finalization_call_];
  std::string_view name = "finalizer";
  if (call.kind == FinalizationKind::kAtexit) {
    name = "atexit";
  } else if (call.kind == FinalizationKind::kCxaDestructor) {
    name = "cxa-destructor";
  }
  const auto created = kernel_runtime_.pthreads().CreateThread(
      std::string(name), 0, call.address, 0);
  if (!created) {
    return FinishFailure(TitleSessionStatus::kFinalizerThreadCreateFailed,
                         kernel::kInvalidKernelHandle);
  }
  const auto allocation = thread_runner_.AllocateAndRegisterFunctionThread(
      created.handle, memory_->base_address(),
      std::span<const std::uint64_t>(call.arguments.data(),
                                     call.argument_count));
  if (!allocation) {
    const auto rolled_back =
        kernel_runtime_.pthreads().DiscardReadyThread(created.handle);
    return FinishFailure(
        rolled_back ? TitleSessionStatus::kFinalizerThreadRegistrationFailed
                    : TitleSessionStatus::kFinalizerThreadRollbackFailed,
        rolled_back ? kernel::kInvalidKernelHandle : created.handle);
  }
  active_finalizer_ = created.handle;
  return {TitleSessionStatus::kPending, phase_, active_finalizer_, exit_value_};
}

TitleSessionResult TitleSession::FinishFailure(
    TitleSessionStatus status, kernel::KernelHandle thread,
    cpu::NativeGuestThreadRunResult run, std::size_t slices) {
  phase_ = TitleSessionPhase::kFailed;
  return {status, phase_, thread, exit_value_, slices, {}, run};
}

TitleSessionPhase TitleSession::phase() const noexcept { return phase_; }

kernel::KernelHandle TitleSession::main_thread() const noexcept {
  return main_thread_;
}

std::uint64_t TitleSession::exit_value() const noexcept { return exit_value_; }

memory::GuestMemory& TitleSession::memory() noexcept { return *memory_; }

gpu::GpuRuntime& TitleSession::gpu_runtime() noexcept { return gpu_runtime_; }

gpu::VideoOutService& TitleSession::video_out() noexcept { return video_out_; }

kernel::KernelRuntime& TitleSession::kernel_runtime() noexcept {
  return kernel_runtime_;
}

cpu::NativeGuestExecutionContext& TitleSession::execution_context() noexcept {
  return execution_context_;
}

cpu::NativeGuestThreadRunner& TitleSession::thread_runner() noexcept {
  return thread_runner_;
}

const hle::ExportRegistry& TitleSession::hle_exports() const noexcept {
  return hle_exports_;
}

const hle::ImportRegistry& TitleSession::hle_data() const noexcept {
  return hle_data_;
}

const cpu::NativeHleImportTable* TitleSession::hle_functions() const noexcept {
  return hle_functions_.get();
}

const hle::UnresolvedImportStubStore& TitleSession::unresolved_import_stubs()
    const noexcept {
  return unresolved_import_stubs_;
}

const ModuleRuntime* TitleSession::module_runtime() const noexcept {
  return module_runtime_.get();
}

std::string_view TitleSessionPhaseName(TitleSessionPhase phase) noexcept {
  switch (phase) {
    case TitleSessionPhase::kCreated:
      return "created";
    case TitleSessionPhase::kInitializing:
      return "initializing";
    case TitleSessionPhase::kRunning:
      return "running";
    case TitleSessionPhase::kFinalizing:
      return "finalizing";
    case TitleSessionPhase::kExited:
      return "exited";
    case TitleSessionPhase::kFailed:
      return "failed";
  }
  return "unknown";
}

std::string_view TitleSessionStatusName(TitleSessionStatus status) noexcept {
  switch (status) {
    case TitleSessionStatus::kPending:
      return "pending";
    case TitleSessionStatus::kBlocked:
      return "blocked";
    case TitleSessionStatus::kExited:
      return "exited";
    case TitleSessionStatus::kInvalidState:
      return "invalid-state";
    case TitleSessionStatus::kStartupFailed:
      return "startup-failed";
    case TitleSessionStatus::kGuestExecutionFailed:
      return "guest-execution-failed";
    case TitleSessionStatus::kFinalizerThreadCreateFailed:
      return "finalizer-thread-create-failed";
    case TitleSessionStatus::kFinalizerThreadRegistrationFailed:
      return "finalizer-thread-registration-failed";
    case TitleSessionStatus::kFinalizerThreadRollbackFailed:
      return "finalizer-thread-rollback-failed";
    case TitleSessionStatus::kSliceLimitReached:
      return "slice-limit-reached";
  }
  return "unknown";
}

std::string_view TitleHleSetupStatusName(TitleHleSetupStatus status) noexcept {
  switch (status) {
    case TitleHleSetupStatus::kOk:
      return "ok";
    case TitleHleSetupStatus::kInvalidState:
      return "invalid-state";
    case TitleHleSetupStatus::kAlreadyAttempted:
      return "already-attempted";
    case TitleHleSetupStatus::kDataSetupFailed:
      return "data-setup-failed";
    case TitleHleSetupStatus::kPthreadArenaSetupFailed:
      return "pthread-arena-setup-failed";
    case TitleHleSetupStatus::kKernelExportsFailed:
      return "kernel-exports-failed";
    case TitleHleSetupStatus::kLibcExportsFailed:
      return "libc-exports-failed";
    case TitleHleSetupStatus::kLibcThreadExportsFailed:
      return "libc-thread-exports-failed";
    case TitleHleSetupStatus::kJsonExportsFailed:
      return "json-exports-failed";
    case TitleHleSetupStatus::kAmprExportsFailed:
      return "ampr-exports-failed";
    case TitleHleSetupStatus::kAgcExportsFailed:
      return "agc-exports-failed";
    case TitleHleSetupStatus::kVideoOutExportsFailed:
      return "video-out-exports-failed";
    case TitleHleSetupStatus::kImportTableBuildFailed:
      return "import-table-build-failed";
  }
  return "unknown";
}

}  // namespace kajps5::runtime
