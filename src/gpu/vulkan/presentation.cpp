// Copyright (C) 2026 KajPS5 contributors
// Architecture adapted from KytyPS5 presentation/window swapchain flow at
// fb5ecec455cf6c67154134429485ffccbfc34203.  Behavioral constraints follow
// SharpEmu VulkanVideoPresenter tests at 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/presentation.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace kajps5::gpu::vulkan {
namespace {
template <typename T>
T Resolve(VulkanDeviceContext& context, const char* name) {
  return reinterpret_cast<T>(context.ResolveDeviceFunction(name));
}
template <typename T>
T ResolveInstance(VulkanDeviceContext& context, const char* name) {
  return reinterpret_cast<T>(context.ResolveInstanceFunction(name));
}

struct Dispatch {
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR capabilities = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR formats = nullptr;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR modes = nullptr;
  PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
  PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
  PFN_vkGetSwapchainImagesKHR images = nullptr;
  PFN_vkAcquireNextImageKHR acquire = nullptr;
  PFN_vkQueuePresentKHR present = nullptr;
  PFN_vkCreateCommandPool create_pool = nullptr;
  PFN_vkDestroyCommandPool destroy_pool = nullptr;
  PFN_vkAllocateCommandBuffers allocate = nullptr;
  PFN_vkBeginCommandBuffer begin = nullptr;
  PFN_vkEndCommandBuffer end = nullptr;
  PFN_vkResetCommandBuffer reset_command = nullptr;
  PFN_vkCmdPipelineBarrier barrier = nullptr;
  PFN_vkCmdBlitImage blit = nullptr;
  PFN_vkCreateSemaphore create_semaphore = nullptr;
  PFN_vkDestroySemaphore destroy_semaphore = nullptr;
  PFN_vkCreateFence create_fence = nullptr;
  PFN_vkDestroyFence destroy_fence = nullptr;
  PFN_vkGetFenceStatus fence_status = nullptr;
  PFN_vkResetFences reset_fences = nullptr;
  PFN_vkQueueSubmit submit = nullptr;
};

bool Load(VulkanDeviceContext& context, Dispatch& dispatch) {
  dispatch.capabilities = ResolveInstance<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
      context, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  dispatch.formats = ResolveInstance<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
      context, "vkGetPhysicalDeviceSurfaceFormatsKHR");
  dispatch.modes = ResolveInstance<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
      context, "vkGetPhysicalDeviceSurfacePresentModesKHR");
  dispatch.create_swapchain = Resolve<PFN_vkCreateSwapchainKHR>(context, "vkCreateSwapchainKHR");
  dispatch.destroy_swapchain = Resolve<PFN_vkDestroySwapchainKHR>(context, "vkDestroySwapchainKHR");
  dispatch.images = Resolve<PFN_vkGetSwapchainImagesKHR>(context, "vkGetSwapchainImagesKHR");
  dispatch.acquire = Resolve<PFN_vkAcquireNextImageKHR>(context, "vkAcquireNextImageKHR");
  dispatch.present = Resolve<PFN_vkQueuePresentKHR>(context, "vkQueuePresentKHR");
  dispatch.create_pool = Resolve<PFN_vkCreateCommandPool>(context, "vkCreateCommandPool");
  dispatch.destroy_pool = Resolve<PFN_vkDestroyCommandPool>(context, "vkDestroyCommandPool");
  dispatch.allocate = Resolve<PFN_vkAllocateCommandBuffers>(context, "vkAllocateCommandBuffers");
  dispatch.begin = Resolve<PFN_vkBeginCommandBuffer>(context, "vkBeginCommandBuffer");
  dispatch.end = Resolve<PFN_vkEndCommandBuffer>(context, "vkEndCommandBuffer");
  dispatch.reset_command = Resolve<PFN_vkResetCommandBuffer>(context, "vkResetCommandBuffer");
  dispatch.barrier = Resolve<PFN_vkCmdPipelineBarrier>(context, "vkCmdPipelineBarrier");
  dispatch.blit = Resolve<PFN_vkCmdBlitImage>(context, "vkCmdBlitImage");
  dispatch.create_semaphore = Resolve<PFN_vkCreateSemaphore>(context, "vkCreateSemaphore");
  dispatch.destroy_semaphore = Resolve<PFN_vkDestroySemaphore>(context, "vkDestroySemaphore");
  dispatch.create_fence = Resolve<PFN_vkCreateFence>(context, "vkCreateFence");
  dispatch.destroy_fence = Resolve<PFN_vkDestroyFence>(context, "vkDestroyFence");
  dispatch.fence_status = Resolve<PFN_vkGetFenceStatus>(context, "vkGetFenceStatus");
  dispatch.reset_fences = Resolve<PFN_vkResetFences>(context, "vkResetFences");
  dispatch.submit = Resolve<PFN_vkQueueSubmit>(context, "vkQueueSubmit");
  return dispatch.capabilities && dispatch.formats && dispatch.modes &&
         dispatch.create_swapchain && dispatch.destroy_swapchain && dispatch.images &&
         dispatch.acquire && dispatch.present && dispatch.create_pool &&
         dispatch.destroy_pool && dispatch.allocate && dispatch.begin && dispatch.end &&
         dispatch.reset_command && dispatch.barrier && dispatch.blit &&
         dispatch.create_semaphore && dispatch.destroy_semaphore &&
         dispatch.create_fence && dispatch.destroy_fence && dispatch.fence_status &&
         dispatch.reset_fences && dispatch.submit;
}

template <typename T, typename F>
bool Enumerate(F&& enumerate, std::vector<T>& values) {
  for (int attempt = 0; attempt != 4; ++attempt) {
    std::uint32_t count = 0;
    if (enumerate(&count, static_cast<T*>(nullptr)) != VK_SUCCESS) return false;
    values.resize(count);
    if (count == 0) return true;
    const VkResult result = enumerate(&count, values.data());
    if (result == VK_SUCCESS) {
      values.resize(count);
      return true;
    }
    if (result != VK_INCOMPLETE) return false;
  }
  return false;
}

void Add(VulkanPresentationResult& result, VulkanPresentationStatus status,
         const char* message, VkResult api = VK_SUCCESS) {
  result.status = status;
  result.diagnostics.push_back({status, static_cast<std::int32_t>(api), message});
}

VkCompositeAlphaFlagBitsKHR Alpha(VkCompositeAlphaFlagsKHR flags) {
  for (const auto value : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                           VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
    if ((flags & value) != 0) return value;
  }
  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}
}  // namespace

VulkanPresentationChoice ChooseVulkanPresentationChoice(
    const VkSurfaceCapabilitiesKHR& capabilities,
    const std::vector<VkSurfaceFormatKHR>& formats,
    const std::vector<VkPresentModeKHR>& modes, VkExtent2D requested) noexcept {
  VulkanPresentationChoice choice{};
  if (!formats.empty()) {
    choice.format = formats.front();
    for (const auto& format : formats) {
      if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
          (format.format == VK_FORMAT_B8G8R8A8_SRGB ||
           format.format == VK_FORMAT_R8G8B8A8_SRGB)) {
        choice.format = format;
        break;
      }
    }
    if (choice.format.format != VK_FORMAT_B8G8R8A8_SRGB &&
        choice.format.format != VK_FORMAT_R8G8B8A8_SRGB) {
      for (const auto& format : formats) {
        if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
            (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
             format.format == VK_FORMAT_R8G8B8A8_UNORM)) {
          choice.format = format;
          break;
        }
      }
    }
  }
  choice.present_mode = VK_PRESENT_MODE_FIFO_KHR;
  if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) !=
      modes.end()) choice.present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  choice.extent = capabilities.currentExtent.width !=
                          std::numeric_limits<std::uint32_t>::max()
                      ? capabilities.currentExtent
                      : VkExtent2D{
                            std::clamp(requested.width,
                                       capabilities.minImageExtent.width,
                                       capabilities.maxImageExtent.width),
                            std::clamp(requested.height,
                                       capabilities.minImageExtent.height,
                                       capabilities.maxImageExtent.height)};
  choice.image_count = std::max(capabilities.minImageCount + 1, 2u);
  if (capabilities.maxImageCount != 0) {
    choice.image_count = std::min(choice.image_count, capabilities.maxImageCount);
  }
  return choice;
}

VulkanPresentationSwapchainPlan BuildVulkanPresentationSwapchainPlan(
    const VkSurfaceCapabilitiesKHR& capabilities,
    const std::vector<VkSurfaceFormatKHR>& formats,
    const std::vector<VkPresentModeKHR>& modes, VkExtent2D requested) noexcept {
  VulkanPresentationSwapchainPlan plan;
  plan.choice = ChooseVulkanPresentationChoice(capabilities, formats, modes, requested);
  plan.retry_when_minimized = plan.choice.extent.width == 0 || plan.choice.extent.height == 0;
  if (!plan.retry_when_minimized &&
      (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0) {
    plan.image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  plan.transform = (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                       ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                       : capabilities.currentTransform;
  plan.composite_alpha = Alpha(capabilities.supportedCompositeAlpha);
  return plan;
}

struct VulkanPresentation::Impl {
  VulkanDeviceContext* context = nullptr;
  VulkanGuestImageCache* cache = nullptr;
  std::optional<VulkanPresentationTestHooks> hooks;
  Dispatch dispatch;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> images;
  VkSurfaceFormatKHR format{};
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  VkExtent2D requested{640, 480};
  VkExtent2D extent{};
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkSemaphore acquired = VK_NULL_HANDLE;
  VkSemaphore ready = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VulkanGuestImagePreparation preparation{};
  bool pending = false;
  bool recreate = false;
  bool poisoned = false;
  bool failed = false;
  std::uint64_t timeline = 0;

  bool MarkDeviceLost(VulkanPresentationResult& result, const char* message,
                      VkResult api);
  bool RecoverAfterAcquireFailure(VulkanGuestImagePreparation& preparation,
                                  VulkanPresentationResult& result,
                                  const char* message, VkResult failure);

  ~Impl() {
    if (hooks.has_value()) {
      if (pending) {
        (void)hooks->complete(preparation);
        hooks->discard(preparation);
      }
      return;
    }
    if (pending) {
      (void)context->WaitIdle();
      if (!cache->Complete(preparation)) context->MarkDeviceLost();
      cache->Discard(preparation);
    }
    if (fence != VK_NULL_HANDLE) dispatch.destroy_fence(context->device(), fence, nullptr);
    if (ready != VK_NULL_HANDLE) dispatch.destroy_semaphore(context->device(), ready, nullptr);
    if (acquired != VK_NULL_HANDLE) dispatch.destroy_semaphore(context->device(), acquired, nullptr);
    if (pool != VK_NULL_HANDLE) dispatch.destroy_pool(context->device(), pool, nullptr);
    if (swapchain != VK_NULL_HANDLE) {
      dispatch.destroy_swapchain(context->device(), swapchain, nullptr);
    }
    context->ReleasePresentationOwner();
  }
};

bool VulkanPresentation::Impl::MarkDeviceLost(
    VulkanPresentationResult& result, const char* message, VkResult api) {
  poisoned = true;
  context->MarkDeviceLost();
  Add(result, VulkanPresentationStatus::kDeviceLost, message, api);
  return false;
}

bool VulkanPresentation::Impl::RecoverAfterAcquireFailure(
    VulkanGuestImagePreparation& acquired_preparation, VulkanPresentationResult& result,
    const char* message, VkResult failure) {
  cache->Discard(acquired_preparation);
  const VkResult idle = context->WaitIdle();
  if (idle == VK_ERROR_DEVICE_LOST) {
    return MarkDeviceLost(result,
                          "presentation recovery device idle reported device loss", idle);
  }
  if (idle != VK_SUCCESS) {
    failed = true;
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "presentation recovery device idle failed", idle);
    return false;
  }
  if (swapchain != VK_NULL_HANDLE) {
    dispatch.destroy_swapchain(context->device(), swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
  }
  images.clear();
  if (acquired != VK_NULL_HANDLE) {
    dispatch.destroy_semaphore(context->device(), acquired, nullptr);
    acquired = VK_NULL_HANDLE;
  }
  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  const VkResult semaphore = dispatch.create_semaphore(
      context->device(), &semaphore_info, nullptr, &acquired);
  if (semaphore == VK_ERROR_DEVICE_LOST) {
    return MarkDeviceLost(result,
                          "presentation recovery semaphore creation reported device loss",
                          semaphore);
  }
  if (semaphore != VK_SUCCESS) {
    failed = true;
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "presentation recovery semaphore creation failed", semaphore);
    return false;
  }
  recreate = true;
  Add(result, VulkanPresentationStatus::kDeviceFailure, message, failure);
  return false;
}

namespace {
bool CreateSwapchain(VulkanPresentation::Impl& impl,
                     VulkanPresentationResult& result) {
  if (impl.hooks.has_value()) {
    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    if (!impl.hooks->recreate ||
        !impl.hooks->recreate(impl.swapchain, replacement, images) ||
        replacement == VK_NULL_HANDLE || images.empty()) {
      Add(result, VulkanPresentationStatus::kRecreateRequired,
          "injected swapchain recreation failed");
      return false;
    }
    impl.swapchain = replacement;
    impl.images = std::move(images);
    impl.extent = impl.requested;
    impl.recreate = false;
    return true;
  }

  VkSurfaceCapabilitiesKHR capabilities{};
  VkResult api = impl.dispatch.capabilities(impl.context->physical_device(),
                                             impl.context->presentation_surface(),
                                             &capabilities);
  if (api != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "surface capabilities query failed", api);
    return false;
  }
  if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
    Add(result, VulkanPresentationStatus::kSurfaceUnavailable,
        "surface does not support transfer-destination swapchain images");
    return false;
  }
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> modes;
  if (!Enumerate<VkSurfaceFormatKHR>([&](auto* count, auto* values) {
        return impl.dispatch.formats(impl.context->physical_device(),
                                     impl.context->presentation_surface(), count, values);
      }, formats) ||
      !Enumerate<VkPresentModeKHR>([&](auto* count, auto* values) {
        return impl.dispatch.modes(impl.context->physical_device(),
                                   impl.context->presentation_surface(), count, values);
      }, modes) || formats.empty()) {
    Add(result, VulkanPresentationStatus::kSurfaceUnavailable,
        "surface formats or present modes are unavailable");
    return false;
  }
  const auto plan = BuildVulkanPresentationSwapchainPlan(
      capabilities, formats, modes, impl.requested);
  const auto choice = plan.choice;
  if (plan.retry_when_minimized) {
    Add(result, VulkanPresentationStatus::kMinimized, "surface has zero extent");
    return false;
  }
  if (!impl.context->SupportsOptimalTilingFeatures(
          choice.format.format, VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
    Add(result, VulkanPresentationStatus::kSurfaceUnavailable,
        "chosen swapchain format does not support blit destination");
    return false;
  }
  VkSwapchainCreateInfoKHR create_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  create_info.surface = impl.context->presentation_surface();
  create_info.minImageCount = choice.image_count;
  create_info.imageFormat = choice.format.format;
  create_info.imageColorSpace = choice.format.colorSpace;
  create_info.imageExtent = choice.extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = plan.image_usage;
  create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create_info.preTransform = plan.transform;
  create_info.compositeAlpha = plan.composite_alpha;
  create_info.presentMode = choice.present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = impl.swapchain;
  VkSwapchainKHR replacement = VK_NULL_HANDLE;
  api = impl.dispatch.create_swapchain(impl.context->device(), &create_info,
                                       nullptr, &replacement);
  if (api != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kRecreateRequired,
        "swapchain creation failed", api);
    return false;
  }
  std::vector<VkImage> images;
  if (!Enumerate<VkImage>([&](auto* count, auto* values) {
        return impl.dispatch.images(impl.context->device(), replacement, count, values);
      }, images) || images.empty()) {
    impl.dispatch.destroy_swapchain(impl.context->device(), replacement, nullptr);
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "swapchain returned no images");
    return false;
  }
  const VkSwapchainKHR old = std::exchange(impl.swapchain, replacement);
  impl.images = std::move(images);
  impl.format = choice.format;
  impl.mode = choice.present_mode;
  impl.extent = choice.extent;
  impl.recreate = false;
  if (old != VK_NULL_HANDLE) {
    impl.dispatch.destroy_swapchain(impl.context->device(), old, nullptr);
  }
  return true;
}
}  // namespace

VulkanPresentation::VulkanPresentation(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
VulkanPresentation::~VulkanPresentation() = default;

std::unique_ptr<VulkanPresentation> VulkanPresentation::Create(
    VulkanDeviceContext& context, VulkanGuestImageCache& cache,
    VulkanPresentationResult& result) {
  bool lost = false;
  if (!context.TryAcquirePresentationOwner(lost)) {
    Add(result, lost ? VulkanPresentationStatus::kDeviceLost
                     : VulkanPresentationStatus::kSurfaceUnavailable,
        "presentation surface is unavailable or already owned");
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->context = &context;
  impl->cache = &cache;
  if (!Load(context, impl->dispatch)) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "presentation Vulkan functions are unavailable");
    return nullptr;
  }
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = context.queue_family_index();
  if (impl->dispatch.create_pool(context.device(), &pool_info, nullptr,
                                 &impl->pool) != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kDeviceFailure, "command pool creation failed");
    return nullptr;
  }
  VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_info.commandPool = impl->pool;
  command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_info.commandBufferCount = 1;
  if (impl->dispatch.allocate(context.device(), &command_info,
                              &impl->command) != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "command buffer allocation failed");
    return nullptr;
  }
  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  if (impl->dispatch.create_semaphore(context.device(), &semaphore_info, nullptr,
                                      &impl->acquired) != VK_SUCCESS ||
      impl->dispatch.create_semaphore(context.device(), &semaphore_info, nullptr,
                                      &impl->ready) != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "presentation semaphore creation failed");
    return nullptr;
  }
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (impl->dispatch.create_fence(context.device(), &fence_info, nullptr,
                                  &impl->fence) != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kDeviceFailure, "presentation fence creation failed");
    return nullptr;
  }
  if (!CreateSwapchain(*impl, result)) return nullptr;
  result.status = VulkanPresentationStatus::kOk;
  return std::unique_ptr<VulkanPresentation>(new VulkanPresentation(std::move(impl)));
}

std::unique_ptr<VulkanPresentation> VulkanPresentation::CreateForTesting(
    VulkanPresentationTestHooks hooks, VulkanPresentationResult& result) {
  auto impl = std::make_unique<Impl>();
  impl->hooks = std::move(hooks);
  impl->requested = {64, 64};
  if (!CreateSwapchain(*impl, result)) return nullptr;
  result.status = VulkanPresentationStatus::kOk;
  return std::unique_ptr<VulkanPresentation>(new VulkanPresentation(std::move(impl)));
}

VulkanPresentationResult VulkanPresentation::Poll() {
  VulkanPresentationResult result;
  if (impl_->hooks.has_value()) {
    if (impl_->poisoned) {
      Add(result, VulkanPresentationStatus::kDeviceLost,
          "injected presentation is terminally lost");
      return result;
    }
    if (impl_->failed) {
      Add(result, VulkanPresentationStatus::kDeviceFailure,
          "injected presentation is terminally failed");
      return result;
    }
    if (!impl_->pending) {
      if (impl_->recreate && !CreateSwapchain(*impl_, result)) return result;
      return result;
    }
    const VkResult status = impl_->hooks->fence_status();
    if (status == VK_NOT_READY) {
      Add(result, VulkanPresentationStatus::kRetainedWorkPending,
          "injected present work remains retained");
      return result;
    }
    if (status == VK_ERROR_DEVICE_LOST) {
      impl_->poisoned = true;
      Add(result, VulkanPresentationStatus::kDeviceLost,
          "injected fence reported device loss", status);
      return result;
    }
    if (status != VK_SUCCESS) {
      Add(result, VulkanPresentationStatus::kDeviceFailure,
          "injected fence status failed", status);
      return result;
    }
    const bool complete = impl_->hooks->complete(impl_->preparation);
    impl_->hooks->discard(impl_->preparation);
    impl_->preparation = {};
    impl_->pending = false;
    if (!complete) {
      Add(result, VulkanPresentationStatus::kDeviceFailure,
          "injected image completion failed");
      return result;
    }
    if (impl_->recreate && !CreateSwapchain(*impl_, result)) return result;
    result.timeline = impl_->timeline;
    return result;
  }
  if (impl_->poisoned || impl_->context->is_device_lost()) {
    impl_->poisoned = true;
    Add(result, VulkanPresentationStatus::kDeviceLost,
        "Vulkan device is terminally lost");
    return result;
  }
  if (impl_->failed) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "presentation is terminally failed");
    return result;
  }
  if (!impl_->pending) {
    if (impl_->recreate && !CreateSwapchain(*impl_, result)) return result;
    return result;
  }
  const VkResult status = impl_->dispatch.fence_status(impl_->context->device(), impl_->fence);
  if (status == VK_NOT_READY) {
    Add(result, VulkanPresentationStatus::kRetainedWorkPending,
        "presentation work remains retained");
    return result;
  }
  if (status == VK_ERROR_DEVICE_LOST) {
    impl_->MarkDeviceLost(result, "presentation fence reported device loss", status);
    return result;
  }
  if (status != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "presentation fence status failed", status);
    return result;
  }
  const bool completed = impl_->cache->Complete(impl_->preparation);
  impl_->cache->Discard(impl_->preparation);
  impl_->preparation = {};
  impl_->pending = false;
  if (!completed) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "completed present work could not be finalized in guest-image coherence");
    return result;
  }
  if (impl_->recreate && !CreateSwapchain(*impl_, result)) return result;
  result.timeline = impl_->timeline;
  return result;
}

VulkanPresentationResult VulkanPresentation::RequestResize(VkExtent2D extent) {
  VulkanPresentationResult result;
  impl_->requested = extent;
  impl_->recreate = true;
  if (extent.width == 0 || extent.height == 0) {
    Add(result, VulkanPresentationStatus::kMinimized, "presentation resize is minimized");
  }
  return result;
}

VulkanPresentationResult VulkanPresentation::Present(
    const GuestImageLayoutInput& input, std::uint64_t timeout,
    std::optional<VulkanImageFormat> format_override) {
  VulkanGuestImageRequest request{
      .input = input,
      .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .format_override = format_override,
  };
  VulkanPresentationResult result;
  if (timeout == 0 || timeout == std::numeric_limits<std::uint64_t>::max()) {
    Add(result, VulkanPresentationStatus::kInvalidFrame,
        "presentation requires a finite nonzero timeout");
    return result;
  }
  if (impl_->hooks.has_value()) {
    if (impl_->poisoned) {
      Add(result, VulkanPresentationStatus::kDeviceLost,
          "injected presentation is terminally lost");
      return result;
    }
    if (impl_->failed) {
      Add(result, VulkanPresentationStatus::kDeviceFailure,
          "injected presentation is terminally failed");
      return result;
    }
    if (impl_->recreate && !CreateSwapchain(*impl_, result)) return result;
    auto preparation = impl_->hooks->prepare(request);
    if (!preparation) {
      Add(result, VulkanPresentationStatus::kInvalidFrame, "injected guest frame rejected");
      return result;
    }
    if (impl_->hooks->supports_optimal_tiling_features &&
        !impl_->hooks->supports_optimal_tiling_features(
            preparation.format.format, VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
      impl_->hooks->discard(preparation);
      Add(result, VulkanPresentationStatus::kDeviceFailure,
          "injected guest image format does not support blit source");
      return result;
    }
    std::uint32_t image = 0;
    VkResult api = impl_->hooks->acquire(timeout, &image);
    if (api == VK_TIMEOUT) {
      impl_->hooks->discard(preparation);
      Add(result, VulkanPresentationStatus::kAcquireTimeout, "injected acquire timeout", api);
      return result;
    }
    if (api == VK_ERROR_OUT_OF_DATE_KHR) {
      impl_->hooks->discard(preparation);
      impl_->recreate = true;
      Add(result, VulkanPresentationStatus::kRecreateRequired,
          "injected acquire out of date", api);
      return result;
    }
    if (api == VK_ERROR_DEVICE_LOST) {
      impl_->hooks->discard(preparation);
      impl_->poisoned = true;
      Add(result, VulkanPresentationStatus::kDeviceLost,
          "injected acquire device loss", api);
      return result;
    }
    if (api != VK_SUCCESS && api != VK_SUBOPTIMAL_KHR) {
      impl_->hooks->discard(preparation);
      Add(result, VulkanPresentationStatus::kDeviceFailure, "injected acquire failed", api);
      return result;
    }
    if (api == VK_SUBOPTIMAL_KHR) impl_->recreate = true;
    api = impl_->hooks->submit();
    if (api != VK_SUCCESS) {
      impl_->hooks->discard(preparation);
      if (api == VK_ERROR_DEVICE_LOST) {
        impl_->poisoned = true;
        Add(result, VulkanPresentationStatus::kDeviceLost,
            "injected submit failed", api);
      } else {
        // The hook has no live semaphore, but it follows the production
        // generation transition: discard once, recreate before another acquire.
        impl_->recreate = true;
        Add(result, VulkanPresentationStatus::kDeviceFailure,
            "injected submit failed", api);
      }
      return result;
    }
    const bool marked = impl_->hooks->mark_submitted(preparation);
    api = impl_->hooks->present();
    impl_->preparation = std::move(preparation);
    impl_->pending = true;
    ++impl_->timeline;
    result.timeline = impl_->timeline;
    if (!marked) {
      Add(result, VulkanPresentationStatus::kDeviceFailure,
          "injected image submission registration failed");
      return result;
    }
    if (api == VK_ERROR_OUT_OF_DATE_KHR || api == VK_SUBOPTIMAL_KHR) {
      impl_->recreate = true;
      Add(result, VulkanPresentationStatus::kRecreateRequired,
          "injected present needs recreation", api);
      return result;
    }
    if (api == VK_ERROR_DEVICE_LOST) {
      impl_->poisoned = true;
      Add(result, VulkanPresentationStatus::kDeviceLost,
          "injected present device loss", api);
      return result;
    }
    if (api != VK_SUCCESS) {
      Add(result, VulkanPresentationStatus::kDeviceFailure, "injected present failed", api);
    }
    return result;
  }

  const auto poll = Poll();
  if (poll.status != VulkanPresentationStatus::kOk) return poll;
  if (impl_->recreate && !CreateSwapchain(*impl_, result)) return result;
  auto preparation = impl_->cache->Prepare(request);
  if (!preparation) {
    Add(result, VulkanPresentationStatus::kInvalidFrame,
        "guest frame layout or checked guest memory was rejected");
    return result;
  }
  if (!impl_->context->SupportsOptimalTilingFeatures(
          preparation.format.format, VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
    impl_->cache->Discard(preparation);
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "guest image format does not support blit source");
    return result;
  }
  std::uint32_t image = 0;
  VkResult api;
  {
    std::lock_guard lock(impl_->context->queue_mutex());
    api = impl_->dispatch.acquire(impl_->context->device(), impl_->swapchain,
                                  timeout, impl_->acquired, VK_NULL_HANDLE, &image);
  }
  if (api == VK_TIMEOUT) {
    impl_->cache->Discard(preparation);
    Add(result, VulkanPresentationStatus::kAcquireTimeout,
        "swapchain acquire timed out", api);
    return result;
  }
  if (api == VK_ERROR_OUT_OF_DATE_KHR) {
    impl_->cache->Discard(preparation);
    impl_->recreate = true;
    Add(result, VulkanPresentationStatus::kRecreateRequired,
        "swapchain acquire is out of date", api);
    return result;
  }
  if (api == VK_ERROR_DEVICE_LOST) {
    impl_->cache->Discard(preparation);
    impl_->MarkDeviceLost(result, "swapchain acquire reported device loss", api);
    return result;
  }
  if (api != VK_SUCCESS && api != VK_SUBOPTIMAL_KHR) {
    impl_->cache->Discard(preparation);
    Add(result, VulkanPresentationStatus::kDeviceFailure, "swapchain acquire failed", api);
    return result;
  }
  if (api == VK_SUBOPTIMAL_KHR) impl_->recreate = true;

  api = impl_->dispatch.reset_command(impl_->command, 0);
  if (api != VK_SUCCESS) {
    if (api == VK_ERROR_DEVICE_LOST) {
      impl_->cache->Discard(preparation);
      impl_->MarkDeviceLost(result, "command buffer reset reported device loss", api);
    } else {
      impl_->RecoverAfterAcquireFailure(preparation, result,
                                        "command buffer reset failed", api);
    }
    return result;
  }
  VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  api = impl_->dispatch.begin(impl_->command, &begin_info);
  if (api != VK_SUCCESS) {
    if (api == VK_ERROR_DEVICE_LOST) {
      impl_->cache->Discard(preparation);
      impl_->MarkDeviceLost(result, "command buffer begin reported device loss", api);
    } else {
      impl_->RecoverAfterAcquireFailure(preparation, result,
                                        "command buffer begin failed", api);
    }
    return result;
  }
  if (!impl_->cache->RecordUpload(impl_->command, preparation,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_ACCESS_TRANSFER_READ_BIT)) {
    impl_->RecoverAfterAcquireFailure(preparation, result,
                                      "guest frame upload recording failed",
                                      VK_ERROR_INITIALIZATION_FAILED);
    return result;
  }

  VkImageMemoryBarrier destination{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  destination.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  destination.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  destination.image = impl_->images[image];
  destination.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  destination.subresourceRange.levelCount = 1;
  destination.subresourceRange.layerCount = 1;
  impl_->dispatch.barrier(impl_->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                          1, &destination);
  VkImageBlit blit{};
  blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.layerCount = 1;
  blit.srcOffsets[1] = {static_cast<int>(input.width), static_cast<int>(input.height), 1};
  blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.layerCount = 1;
  blit.dstOffsets[1] = {static_cast<int>(impl_->extent.width),
                        static_cast<int>(impl_->extent.height), 1};
  impl_->dispatch.blit(impl_->command, preparation.image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, impl_->images[image],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_NEAREST);
  destination.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  destination.dstAccessMask = 0;
  destination.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  destination.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  impl_->dispatch.barrier(impl_->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &destination);

  api = impl_->dispatch.end(impl_->command);
  if (api != VK_SUCCESS) {
    if (api == VK_ERROR_DEVICE_LOST) {
      impl_->cache->Discard(preparation);
      impl_->MarkDeviceLost(result, "presentation command end reported device loss", api);
    } else {
      impl_->RecoverAfterAcquireFailure(preparation, result,
                                        "presentation command end failed", api);
    }
    return result;
  }
  api = impl_->dispatch.reset_fences(impl_->context->device(), 1, &impl_->fence);
  if (api != VK_SUCCESS) {
    if (api == VK_ERROR_DEVICE_LOST) {
      impl_->cache->Discard(preparation);
      impl_->MarkDeviceLost(result, "presentation fence reset reported device loss", api);
    } else {
      impl_->RecoverAfterAcquireFailure(preparation, result,
                                        "presentation fence reset failed", api);
    }
    return result;
  }
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &impl_->acquired;
  submit.pWaitDstStageMask = &stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &impl_->command;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &impl_->ready;
  {
    std::lock_guard lock(impl_->context->queue_mutex());
    api = impl_->dispatch.submit(impl_->context->queue(), 1, &submit, impl_->fence);
  }
  if (api != VK_SUCCESS) {
    if (api == VK_ERROR_DEVICE_LOST) {
      impl_->cache->Discard(preparation);
      impl_->MarkDeviceLost(result, "presentation submit reported device loss", api);
    } else {
      impl_->RecoverAfterAcquireFailure(preparation, result,
                                        "presentation submit failed", api);
    }
    return result;
  }

  const bool submitted_to_cache = impl_->cache->MarkSubmitted(preparation);
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &impl_->ready;
  present.swapchainCount = 1;
  present.pSwapchains = &impl_->swapchain;
  present.pImageIndices = &image;
  {
    std::lock_guard lock(impl_->context->queue_mutex());
    api = impl_->dispatch.present(impl_->context->queue(), &present);
  }
  impl_->preparation = std::move(preparation);
  impl_->pending = true;
  ++impl_->timeline;
  result.timeline = impl_->timeline;
  if (!submitted_to_cache) {
    Add(result, VulkanPresentationStatus::kDeviceFailure,
        "submitted present work could not be registered with guest-image coherence");
    return result;
  }
  if (api == VK_ERROR_OUT_OF_DATE_KHR || api == VK_SUBOPTIMAL_KHR) {
    impl_->recreate = true;
    Add(result, VulkanPresentationStatus::kRecreateRequired,
        "presentation requires swapchain recreation", api);
    return result;
  }
  if (api == VK_ERROR_DEVICE_LOST) {
    impl_->MarkDeviceLost(result, "queue present reported device loss", api);
    return result;
  }
  if (api != VK_SUCCESS) {
    Add(result, VulkanPresentationStatus::kDeviceFailure, "queue present failed", api);
  }
  return result;
}

VkSurfaceFormatKHR VulkanPresentation::surface_format() const noexcept {
  return impl_->format;
}
VkPresentModeKHR VulkanPresentation::present_mode() const noexcept {
  return impl_->mode;
}

const char* VulkanPresentationStatusName(VulkanPresentationStatus status) noexcept {
  switch (status) {
    case VulkanPresentationStatus::kOk: return "ok";
    case VulkanPresentationStatus::kContextUnavailable: return "context_unavailable";
    case VulkanPresentationStatus::kInvalidFrame: return "invalid_frame";
    case VulkanPresentationStatus::kSurfaceUnavailable: return "surface_unavailable";
    case VulkanPresentationStatus::kMinimized: return "minimized";
    case VulkanPresentationStatus::kAcquireTimeout: return "acquire_timeout";
    case VulkanPresentationStatus::kRetainedWorkPending: return "retained_work_pending";
    case VulkanPresentationStatus::kRecreateRequired: return "recreate_required";
    case VulkanPresentationStatus::kDeviceLost: return "device_lost";
    case VulkanPresentationStatus::kDeviceFailure: return "device_failure";
  }
  return "unknown";
}
}  // namespace kajps5::gpu::vulkan
