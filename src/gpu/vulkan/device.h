// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/vulkanInstance.h and
// src/graphics/presentation/window/vulkanWindow.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and diagnostic reference: Copyright (C) 2026 SharpEmu Emulator
// Project, src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs and
// tests/SharpEmu.Libs.Tests/VideoOut/VulkanPhysicalDeviceScoringTests.cs at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <functional>
#include <vector>

#include "gpu/vulkan/loader.h"

namespace kajps5::gpu::vulkan {

inline constexpr std::uint32_t kRequiredVulkanApiVersion =
    VK_API_VERSION_1_3;

enum class VulkanContextStatus {
  kOk,
  kLoaderUnavailable,
  kLoaderApiVersionUnsupported,
  kInstanceCreationFailed,
  kInstanceFunctionUnavailable,
  kSurfaceExtensionUnavailable,
  kSurfaceCreationFailed,
  kPhysicalDeviceEnumerationFailed,
  kNoSuitableDevice,
  kDeviceCreationFailed,
  kDeviceFunctionUnavailable,
  kQueueUnavailable,
  kAlreadyInitialized,
};

enum class VulkanDiagnosticSeverity {
  kInfo,
  kWarning,
  kError,
};

enum class VulkanDiagnosticCode {
  kLoaderOpened,
  kLoaderUnavailable,
  kLoaderVersionUnsupported,
  kGlobalFunctionUnavailable,
  kInstanceCreationFailed,
  kInstanceFunctionUnavailable,
  kSurfaceExtensionUnavailable,
  kSurfaceCreationFailed,
  kPhysicalDeviceEnumerationFailed,
  kCandidateRejectedApiVersion,
  kCandidateRejectedQueue,
  kCandidateRejectedFeature,
  kCandidateRejectedExtension,
  kCandidateAccepted,
  kNoSuitableDevice,
  kSelectedDevice,
  kOptionalCapabilityUnavailable,
  kDeviceCreationFailed,
  kDeviceFunctionUnavailable,
  kQueueUnavailable,
  kDuplicateInitialization,
};

struct VulkanDiagnostic {
  VulkanDiagnosticSeverity severity = VulkanDiagnosticSeverity::kInfo;
  VulkanDiagnosticCode code = VulkanDiagnosticCode::kLoaderOpened;
  std::optional<std::size_t> candidate_index;
  std::int32_t api_result = VK_SUCCESS;
  std::string message;
};

struct VulkanInitializationResult {
  VulkanContextStatus status = VulkanContextStatus::kOk;
  std::vector<VulkanDiagnostic> diagnostics;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == VulkanContextStatus::kOk;
  }
};

enum class VulkanPhysicalDeviceType {
  kOther,
  kIntegratedGpu,
  kDiscreteGpu,
  kVirtualGpu,
  kCpu,
};

struct VulkanQueueFamily {
  std::uint32_t index = 0;
  std::uint32_t queue_count = 0;
  bool supports_graphics = false;
  bool supports_compute = false;
  // Populated only for a presentation-enabled context.  A false value is not
  // evidence about headless operation.
  bool supports_present = false;
};

// Physical-device support queried before selection. The default requirements
// below intentionally match KytyPS5's non-surface renderer-ready baseline at
// the pinned revision. Surface, swapchain, format, and extension-specific
// color-write/depth-clip gates remain outside this unit.
struct VulkanFeatureSupport {
  // VkPhysicalDeviceVulkan13Features.
  bool dynamic_rendering = false;
  bool synchronization2 = false;
  bool robust_image_access = false;

  // VkPhysicalDeviceVulkan12Features.
  bool timeline_semaphore = false;
  bool sampler_mirror_clamp_to_edge = false;

  // VkPhysicalDeviceFeatures.
  bool sample_rate_shading = false;
  bool fragment_stores_and_atomics = false;
  bool sampler_anisotropy = false;
  bool robust_buffer_access = false;
  bool depth_bounds = false;
  bool shader_image_gather_extended = false;
  bool shader_storage_image_read_without_format = false;
  bool shader_storage_image_write_without_format = false;
  bool independent_blend = false;
  bool tessellation_shader = false;

  // Optional compatibility capabilities. They are enabled when supported but
  // never reject a default renderer-ready device.
  bool vertex_pipeline_stores_and_atomics = false;
  bool shader_int64 = false;
  bool texture_compression_bc = false;
};

// The feature bits actually enabled on the created logical device. This is
// deliberately separate from VulkanFeatureSupport, which reports hardware
// support and can contain true bits that callers chose not to require.
struct VulkanFeatureEnablement {
  bool dynamic_rendering = false;
  bool synchronization2 = false;
  bool robust_image_access = false;
  bool timeline_semaphore = false;
  bool sampler_mirror_clamp_to_edge = false;
  bool sample_rate_shading = false;
  bool fragment_stores_and_atomics = false;
  bool sampler_anisotropy = false;
  bool robust_buffer_access = false;
  bool depth_bounds = false;
  bool shader_image_gather_extended = false;
  bool shader_storage_image_read_without_format = false;
  bool shader_storage_image_write_without_format = false;
  bool independent_blend = false;
  bool tessellation_shader = false;
  bool vertex_pipeline_stores_and_atomics = false;
  bool shader_int64 = false;
  bool texture_compression_bc = false;
};

struct VulkanDeviceCandidate {
  std::size_t candidate_index = 0;
  std::string name;
  // UUID-derived when a driver supplies one; used before mutable discovery
  // order as the deterministic same-class tie-breaker.
  std::string stable_id;
  std::uint32_t api_version = VK_API_VERSION_1_0;
  std::uint32_t driver_version = 0;
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
  VulkanPhysicalDeviceType type = VulkanPhysicalDeviceType::kOther;
  std::uint64_t min_storage_buffer_offset_alignment = 1;
  std::uint64_t max_storage_buffer_range = 0;
  std::uint32_t max_per_stage_descriptor_storage_buffers = 0;
  std::uint32_t max_descriptor_set_storage_buffers = 0;
  std::uint32_t max_per_stage_descriptor_samplers = 0;
  std::uint32_t max_descriptor_set_samplers = 0;
  std::uint32_t max_per_stage_descriptor_sampled_images = 0;
  std::uint32_t max_descriptor_set_sampled_images = 0;
  std::uint32_t max_per_stage_descriptor_storage_images = 0;
  std::uint32_t max_descriptor_set_storage_images = 0;
  std::uint32_t max_per_stage_resources = 0;
  std::uint32_t max_bound_descriptor_sets = 0;
  std::uint32_t max_push_constants_size = 0;
  std::uint64_t non_coherent_atom_size = 1;
  // Copied from VkPhysicalDeviceLimits so compute execution can reject an
  // invalid vkCmdDispatch before recording the command buffer.
  std::array<std::uint32_t, 3> max_compute_work_group_count = {1, 1, 1};
  std::vector<VulkanQueueFamily> queue_families;
  VulkanFeatureSupport features;
  std::vector<std::string> extensions;
};

struct VulkanDeviceRequirements {
  bool require_graphics_queue = true;
  bool require_compute_queue = true;
  bool require_dynamic_rendering = true;
  bool require_synchronization2 = true;
  bool require_robust_image_access = true;
  bool require_timeline_semaphore = true;
  bool require_sampler_mirror_clamp_to_edge = true;
  bool require_sample_rate_shading = true;
  bool require_fragment_stores_and_atomics = true;
  bool require_sampler_anisotropy = true;
  bool require_robust_buffer_access = true;
  bool require_depth_bounds = true;
  bool require_shader_storage_image_write_without_format = true;
  bool require_shader_storage_image_read_without_format = true;
  bool require_shader_image_gather_extended = true;
  bool require_independent_blend = true;
  bool require_tessellation_shader = true;
  // M8 itself needs no device extension. Later units must declare concrete
  // surface/format/extension needs here instead of silently widening the
  // renderer-ready core baseline.
  std::vector<std::string> required_extensions;
  bool require_present_queue = false;
};

// The host boundary provides the instance extensions and creates one surface
// after VulkanDeviceContext owns an instance.  It has no global handles and
// the context destroys the returned surface before destroying that instance.
struct VulkanSurfaceFactory {
  std::vector<std::string> required_instance_extensions;
  std::function<VkResult(VkInstance, PFN_vkGetInstanceProcAddr,
                         VkSurfaceKHR*)> create_surface;
};

struct VulkanContextOptions {
  std::string application_name = "KajPS5";
  VulkanDeviceRequirements requirements;
  // Non-owning during Create only.  The resulting surface is exclusively
  // owned by the context and consumed by its single presentation child.
  const VulkanSurfaceFactory* surface_factory = nullptr;
};

struct VulkanDeviceSelection {
  std::size_t candidate_index = 0;
  std::uint32_t queue_family_index = 0;
  std::vector<std::string> enabled_extensions;
};

struct VulkanDeviceSelectionResult {
  std::optional<VulkanDeviceSelection> selection;
  std::vector<VulkanDiagnostic> diagnostics;

  [[nodiscard]] explicit operator bool() const noexcept {
    return selection.has_value();
  }
};

// Pure selection model shared by the real discovery path and deterministic
// tests. It does not load a driver or retain any Vulkan handles.
[[nodiscard]] VulkanDeviceSelectionResult SelectVulkanDevice(
    const std::vector<VulkanDeviceCandidate>& candidates,
    const VulkanDeviceRequirements& requirements);

class VulkanComputeExecution;
class VulkanDeviceContext;
class VulkanPresentation;

struct VulkanContextCreateResult {
  VulkanInitializationResult initialization;
  std::unique_ptr<VulkanDeviceContext> context;

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(initialization) && context != nullptr;
  }
};

// A single RAII owner for one instance, physical device, logical device, and
// universal graphics/compute queue. It intentionally has no surface,
// presentation, allocator, command buffer, or resource ownership.
class VulkanDeviceContext final {
 public:
  [[nodiscard]] static VulkanContextCreateResult Create(
      const VulkanContextOptions& options = {});
  [[nodiscard]] static VulkanContextCreateResult Create(
      VulkanLoader loader, const VulkanContextOptions& options = {});

  ~VulkanDeviceContext();

  VulkanDeviceContext(const VulkanDeviceContext&) = delete;
  VulkanDeviceContext& operator=(const VulkanDeviceContext&) = delete;
  VulkanDeviceContext(VulkanDeviceContext&&) = delete;
  VulkanDeviceContext& operator=(VulkanDeviceContext&&) = delete;

  [[nodiscard]] const VulkanDeviceCandidate& properties() const noexcept;
  // Hardware support reported by the selected physical device.
  [[nodiscard]] const VulkanFeatureSupport& capabilities() const noexcept;
  [[nodiscard]] const VulkanFeatureSupport& supported_capabilities() const
      noexcept;
  // Bits requested in VkDeviceCreateInfo and enabled on the logical device.
  [[nodiscard]] const VulkanFeatureEnablement& enabled_capabilities() const
      noexcept;
  [[nodiscard]] std::uint32_t queue_family_index() const noexcept;

  [[nodiscard]] VkInstance instance() const noexcept;
  [[nodiscard]] VkPhysicalDevice physical_device() const noexcept;
  [[nodiscard]] VkDevice device() const noexcept;
  [[nodiscard]] VkQueue queue() const noexcept;
  [[nodiscard]] const std::optional<VkPhysicalDeviceMemoryProperties> &
  memory_properties() const noexcept;
  [[nodiscard]] bool is_device_lost() noexcept;
  // Child RAII owners resolve their own small, device-local dispatch tables
  // through this context. No process-global Vulkan dispatch is exposed.
  [[nodiscard]] PFN_vkVoidFunction ResolveDeviceFunction(
      const char* name) const noexcept;
  // Narrow surface dispatch access for the runtime-owned presentation child.
  [[nodiscard]] VkSurfaceKHR presentation_surface() const noexcept;
  [[nodiscard]] PFN_vkVoidFunction ResolveInstanceFunction(
      const char* name) const noexcept;
  [[nodiscard]] std::mutex& queue_mutex() noexcept;
  [[nodiscard]] bool SupportsOptimalTilingFeatures(
      VkFormat format, VkFormatFeatureFlags required_features) const noexcept;
  [[nodiscard]] bool SupportsColorAttachmentFormat(VkFormat format) const noexcept;
  [[nodiscard]] bool SupportsDepthStencilAttachmentFormat(VkFormat format) const noexcept;

 private:
  friend class VulkanComputeExecution;
  friend class VulkanGraphicsExecution;
  friend class VulkanPresentation;

  // Each execution class has a single owner while this device lives. Ownership
  // is intentionally not part of the public context surface.
  // Ownership acquisition and terminal-loss validation are one state
  // transition, so a creator cannot claim the slot after device loss wins.
  [[nodiscard]] bool TryAcquireComputeExecutionOwner(
      bool& context_is_device_lost) noexcept;
  void ReleaseComputeExecutionOwner() noexcept;
  [[nodiscard]] bool TryAcquireGraphicsExecutionOwner(
      bool& context_is_device_lost) noexcept;
  void ReleaseGraphicsExecutionOwner() noexcept;
  [[nodiscard]] bool TryAcquirePresentationOwner(
      bool& context_is_device_lost) noexcept;
  void ReleasePresentationOwner() noexcept;
  // Compute execution uses the context's existing device dispatch to drain
  // retained submitted work before destroying its child objects.
  [[nodiscard]] VkResult WaitIdle() noexcept;
  void MarkDeviceLost() noexcept;
  [[nodiscard]] bool IsDeviceLost() noexcept;

  struct Impl;

  explicit VulkanDeviceContext(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char* VulkanContextStatusName(
    VulkanContextStatus status) noexcept;
[[nodiscard]] const char* VulkanDiagnosticCodeName(
    VulkanDiagnosticCode code) noexcept;
[[nodiscard]] const char* VulkanPhysicalDeviceTypeName(
    VulkanPhysicalDeviceType type) noexcept;

}  // namespace kajps5::gpu::vulkan
