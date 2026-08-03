// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp and
// src/graphics/guest_gpu/pm4.h at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>

#include "gpu/command_processor.h"
#include "gpu/resource_coherence.h"
#include "gpu/shader_runtime.h"
#include "gpu/submission_queue.h"
#include "gpu/vulkan/buffer_cache.h"
#include "gpu/vulkan/device.h"
#include "gpu/vulkan/execution.h"
#include "gpu/vulkan/graphics_execution.h"
#include "gpu/vulkan/image_cache.h"
#include "gpu/vulkan/presentation.h"

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::kernel {
class EventQueueService;
}

namespace kajps5::gpu {

inline constexpr std::size_t kAgcCommandBufferSize = 0x38;
inline constexpr std::size_t kAgcCommandBufferCursorUpOffset = 0x10;
inline constexpr std::size_t kAgcCommandBufferCursorDownOffset = 0x18;
inline constexpr std::size_t kAgcCommandBufferCallbackOffset = 0x20;
inline constexpr std::size_t kAgcCommandBufferUserDataOffset = 0x28;
inline constexpr std::size_t kAgcCommandBufferReservedDwordsOffset = 0x30;
inline constexpr std::uint32_t kMaximumPm4PacketDwords = 0x4001;

enum class GpuRuntimeStatus {
  kOk,
  kInvalidArgument,
  kMemoryFault,
  kBufferTooSmall,
  kCallbackRequired,
  kResourceLimit,
};

enum class AgcPacketType {
  kSetShRegisterDirect,
  kSetCxRegisterDirect,
  kSetUcRegisterDirect,
  kSetIndexSize,
  kSetIndexBuffer,
  kSetIndexCount,
  kSetNumInstances,
  kDrawIndex,
  kDrawIndexMultiInstanced,
  kDrawIndexAuto,
  kDrawIndexOffset,
  kSetBaseIndirectArgs,
  kDispatchIndirect,
  kJump,
  kRewind,
  kSetPredication,
  kWriteData,
  kReleaseMemory,
  kEventWrite,
  kGetLodStats,
  kWaitRegMem,
};

struct GpuPacketResult {
  GpuRuntimeStatus status = GpuRuntimeStatus::kOk;
  std::uint64_t address = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == GpuRuntimeStatus::kOk;
  }
};

struct GpuPacketSizeResult {
  GpuRuntimeStatus status = GpuRuntimeStatus::kOk;
  std::uint32_t dwords = 0;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == GpuRuntimeStatus::kOk;
  }
};

class GpuRuntime final {
 public:
  // A non-null sink must outlive this runtime and must not call it recursively.
  // Construction rejects a GuestMemory that already has a live GPU owner.
  explicit GpuRuntime(
      memory::GuestMemory& memory,
      GpuSubmissionSink* submission_sink = nullptr,
      kernel::EventQueueService* event_queues = nullptr);

  [[nodiscard]] GpuPacketResult WriteNop(std::uint64_t command_buffer,
                                         std::uint32_t dword_count);
  [[nodiscard]] GpuPacketResult WriteDispatch(
      std::uint64_t command_buffer, std::uint32_t group_count_x,
      std::uint32_t group_count_y, std::uint32_t group_count_z,
      std::uint32_t modifier);
  [[nodiscard]] GpuPacketResult WriteAgcPacket(
      AgcPacketType type, std::span<const std::uint64_t> arguments);
  [[nodiscard]] GpuPacketSizeResult GetPacketSize(
      std::uint64_t packet_address) const noexcept;
  [[nodiscard]] GpuRuntimeStatus SetPacketPredication(
      std::uint64_t packet_address, std::uint32_t predication) noexcept;
  [[nodiscard]] ShaderMapResult CreateShader(
      std::uint64_t destination_address, std::uint64_t header_address,
      std::uint64_t code_address);
  [[nodiscard]] std::optional<RegisteredShader> LookupRegisteredShader(
      std::uint64_t code_address) const;
  [[nodiscard]] ShaderCompileResult RecompileRegisteredShader(
      std::uint64_t code_address,
      const shader::recompiler::CompileOptions& options,
      shader::recompiler::CompileResult& result);
  [[nodiscard]] GpuCommandResult ProcessCommandBuffer(
      std::uint64_t address, std::uint32_t dword_count,
      GpuSubmissionSink& sink,
      GpuCommandLimits limits = {});
  [[nodiscard]] GpuCommandCursor BeginCommandBuffer(
      std::uint64_t address, std::uint32_t dword_count,
      GpuCommandLimits limits = {});
  [[nodiscard]] GpuCommandResult ResumeCommandBuffer(
      GpuCommandCursor& cursor, GpuSubmissionSink& sink);
  [[nodiscard]] std::optional<std::uint32_t> ReadRegister(
      GpuRegisterSpace space, std::uint32_t offset) const noexcept;
  [[nodiscard]] GpuResourceCoherence& resource_coherence() noexcept {
    return *resource_coherence_;
  }
  [[nodiscard]] const GpuResourceCoherence& resource_coherence() const
      noexcept {
    return *resource_coherence_;
  }
  [[nodiscard]] GpuSubmissionQueue& submissions() noexcept {
    return submission_queue_;
  }
  [[nodiscard]] const GpuSubmissionQueue& submissions() const noexcept {
    return submission_queue_;
  }
  [[nodiscard]] GpuQueueDrainResult DrainSubmissions() {
    return submission_queue_.Drain(*submission_sink_);
  }
  [[nodiscard]] const GpuActionRing& submission_history() const noexcept {
    return submission_history_;
  }

  // Vulkan stays opt-in so headless loading and existing GPU-core tests do
  // not probe host hardware. A successful context is retained for this
  // runtime's lifetime; a duplicate request preserves that context unchanged.
  [[nodiscard]] vulkan::VulkanInitializationResult InitializeVulkan(
      const vulkan::VulkanContextOptions& options = {});
  [[nodiscard]] vulkan::VulkanInitializationResult InitializeVulkan(
      vulkan::VulkanLoader loader,
      const vulkan::VulkanContextOptions& options = {});
  [[nodiscard]] bool has_vulkan_context() const noexcept;
  // Presentation creates the Vulkan context with its surface extensions in a
  // single transaction; it cannot be attached to an existing headless device.
  [[nodiscard]] vulkan::VulkanPresentationResult InitializeVulkanPresentation(
      const vulkan::VulkanSurfaceFactory& surface_factory,
      const vulkan::VulkanContextOptions& options = {});
  [[nodiscard]] vulkan::VulkanPresentationResult PresentVulkanGuestFrame(
      const GuestImageLayoutInput& input,
      std::uint64_t timeout_ns = 50'000'000ULL);
  [[nodiscard]] vulkan::VulkanPresentationResult PollVulkanPresentation();
  [[nodiscard]] vulkan::VulkanPresentationResult ResizeVulkanPresentation(
      VkExtent2D extent);
  // The returned context remains owned by this runtime. Initialize Vulkan
  // before sharing the runtime with submission threads, and synchronize with
  // runtime destruction before retaining this pointer.
  [[nodiscard]] vulkan::VulkanDeviceContext* vulkan_context() noexcept;
  [[nodiscard]] const vulkan::VulkanDeviceContext* vulkan_context() const
      noexcept;

  // Executes already-validated SPIR-V through this runtime's sole Vulkan
  // compute owner. Vulkan initialization remains explicit: this entry never
  // probes hardware or creates a device on its own.
  [[nodiscard]] vulkan::VulkanComputeResult SubmitVulkanCompute(
      std::span<const std::uint32_t> spirv_words,
      std::uint32_t group_count_x,
      std::uint32_t group_count_y,
      std::uint32_t group_count_z,
      std::uint64_t timeout_ns =
          vulkan::kDefaultVulkanComputeFenceWaitNanoseconds);
  [[nodiscard]] vulkan::VulkanComputeResult PollVulkanCompute();
  [[nodiscard]] bool has_vulkan_compute_execution() const noexcept;

  // The immutable CompileResult remains the sole binding contract. This
  // preflights guest descriptors through the runtime-owned cache before the
  // descriptor-capable execution path is entered.
  [[nodiscard]] vulkan::VulkanComputeResult SubmitVulkanTranslatedCompute(
      const shader::recompiler::CompileResult &compile,
      std::uint32_t group_count_x, std::uint32_t group_count_y,
      std::uint32_t group_count_z,
      std::uint64_t timeout_ns =
          vulkan::kDefaultVulkanComputeFenceWaitNanoseconds);
  [[nodiscard]] vulkan::VulkanGraphicsResult SubmitVulkanTranslatedDraw(
      const vulkan::VulkanTranslatedDrawRequest& request);
  [[nodiscard]] vulkan::VulkanGraphicsResult PollVulkanGraphics();

private:
  [[nodiscard]] GpuPacketResult AppendPacket(
      std::uint64_t command_buffer, std::span<const std::uint32_t> packet);

  memory::GuestMemory& memory_;
  ShaderRuntime shader_runtime_;
  std::shared_ptr<GpuResourceCoherence> resource_coherence_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, std::uint32_t> context_registers_;
  std::unordered_map<std::uint32_t, std::uint32_t> shader_registers_;
  std::unordered_map<std::uint32_t, std::uint32_t> user_config_registers_;
  GpuSubmissionQueue submission_queue_;
  GpuActionRing submission_history_;
  GpuEventSubmissionSink event_effects_;
  GpuMemorySubmissionSink submission_effects_;
  GpuSubmissionSink* submission_sink_ = nullptr;
  mutable std::mutex vulkan_mutex_;
  std::unique_ptr<vulkan::VulkanDeviceContext> vulkan_context_;
  // Declared before execution so destruction releases retained execution
  // leases before the guest buffer cache and then the device context.
  std::unique_ptr<vulkan::VulkanGuestBufferCache> vulkan_buffer_cache_;
  std::unique_ptr<vulkan::VulkanGuestImageCache> vulkan_image_cache_;
  // Declared after the context so destruction reverses this order: retained
  // execution resources are dealt with before the Vulkan device disappears.
  std::unique_ptr<vulkan::VulkanComputeExecution> vulkan_execution_;
  std::unique_ptr<vulkan::VulkanGraphicsExecution> vulkan_graphics_execution_;
  std::unique_ptr<vulkan::VulkanPresentation> vulkan_presentation_;
};

[[nodiscard]] const char* GpuRuntimeStatusName(
    GpuRuntimeStatus status) noexcept;

}  // namespace kajps5::gpu
