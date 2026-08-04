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
template <typename T> T Resolve(VulkanDeviceContext& c, const char* n) {
  return reinterpret_cast<T>(c.ResolveDeviceFunction(n));
}
template <typename T> T ResolveInstance(VulkanDeviceContext& c, const char* n) {
  return reinterpret_cast<T>(c.ResolveInstanceFunction(n));
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
  PFN_vkWaitForFences wait_fences = nullptr;
  PFN_vkResetFences reset_fences = nullptr;
  PFN_vkQueueSubmit submit = nullptr;
};
bool Load(VulkanDeviceContext& c, Dispatch& d) {
  d.capabilities = ResolveInstance<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(c, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  d.formats = ResolveInstance<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(c, "vkGetPhysicalDeviceSurfaceFormatsKHR");
  d.modes = ResolveInstance<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(c, "vkGetPhysicalDeviceSurfacePresentModesKHR");
  d.create_swapchain = Resolve<PFN_vkCreateSwapchainKHR>(c, "vkCreateSwapchainKHR"); d.destroy_swapchain = Resolve<PFN_vkDestroySwapchainKHR>(c, "vkDestroySwapchainKHR");
  d.images = Resolve<PFN_vkGetSwapchainImagesKHR>(c, "vkGetSwapchainImagesKHR"); d.acquire = Resolve<PFN_vkAcquireNextImageKHR>(c, "vkAcquireNextImageKHR"); d.present = Resolve<PFN_vkQueuePresentKHR>(c, "vkQueuePresentKHR");
  d.create_pool=Resolve<PFN_vkCreateCommandPool>(c,"vkCreateCommandPool"); d.destroy_pool=Resolve<PFN_vkDestroyCommandPool>(c,"vkDestroyCommandPool"); d.allocate=Resolve<PFN_vkAllocateCommandBuffers>(c,"vkAllocateCommandBuffers"); d.begin=Resolve<PFN_vkBeginCommandBuffer>(c,"vkBeginCommandBuffer"); d.end=Resolve<PFN_vkEndCommandBuffer>(c,"vkEndCommandBuffer"); d.reset_command=Resolve<PFN_vkResetCommandBuffer>(c,"vkResetCommandBuffer"); d.barrier=Resolve<PFN_vkCmdPipelineBarrier>(c,"vkCmdPipelineBarrier"); d.blit=Resolve<PFN_vkCmdBlitImage>(c,"vkCmdBlitImage");
  d.create_semaphore=Resolve<PFN_vkCreateSemaphore>(c,"vkCreateSemaphore"); d.destroy_semaphore=Resolve<PFN_vkDestroySemaphore>(c,"vkDestroySemaphore"); d.create_fence=Resolve<PFN_vkCreateFence>(c,"vkCreateFence"); d.destroy_fence=Resolve<PFN_vkDestroyFence>(c,"vkDestroyFence"); d.fence_status=Resolve<PFN_vkGetFenceStatus>(c,"vkGetFenceStatus"); d.wait_fences=Resolve<PFN_vkWaitForFences>(c,"vkWaitForFences"); d.reset_fences=Resolve<PFN_vkResetFences>(c,"vkResetFences"); d.submit=Resolve<PFN_vkQueueSubmit>(c,"vkQueueSubmit");
  return d.capabilities && d.formats && d.modes && d.create_swapchain && d.destroy_swapchain && d.images && d.acquire && d.present && d.create_pool && d.destroy_pool && d.allocate && d.begin && d.end && d.reset_command && d.barrier && d.blit && d.create_semaphore && d.destroy_semaphore && d.create_fence && d.destroy_fence && d.fence_status && d.wait_fences && d.reset_fences && d.submit;
}
template <typename T, typename F> bool Enumerate(F&& f, std::vector<T>& values) {
  for (int i=0;i!=4;++i) { std::uint32_t count=0; if (f(&count,static_cast<T*>(nullptr))!=VK_SUCCESS) return false; values.resize(count); if (!count) return true; const VkResult r=f(&count,values.data()); if(r==VK_SUCCESS){values.resize(count);return true;} if(r!=VK_INCOMPLETE)return false; } return false;
}
void Add(VulkanPresentationResult& r, VulkanPresentationStatus s, const char* m, VkResult api=VK_SUCCESS) { r.status=s; r.diagnostics.push_back({s,static_cast<std::int32_t>(api),m}); }
VkCompositeAlphaFlagBitsKHR Alpha(VkCompositeAlphaFlagsKHR flags) {
  for (auto value : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) if ((flags&value)!=0) return value;
  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}
}

VulkanPresentationChoice ChooseVulkanPresentationChoice(const VkSurfaceCapabilitiesKHR& c, const std::vector<VkSurfaceFormatKHR>& formats, const std::vector<VkPresentModeKHR>& modes, VkExtent2D requested) noexcept {
  VulkanPresentationChoice choice{};
  if (!formats.empty()) {
    choice.format=formats.front();
    for (const auto& f:formats) if (f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && (f.format==VK_FORMAT_B8G8R8A8_SRGB || f.format==VK_FORMAT_R8G8B8A8_SRGB)) {choice.format=f;break;}
    if (choice.format.format != VK_FORMAT_B8G8R8A8_SRGB && choice.format.format != VK_FORMAT_R8G8B8A8_SRGB) for (const auto& f:formats) if (f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && (f.format==VK_FORMAT_B8G8R8A8_UNORM || f.format==VK_FORMAT_R8G8B8A8_UNORM)) {choice.format=f;break;}
  }
  choice.present_mode=VK_PRESENT_MODE_FIFO_KHR;
  if (std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_MAILBOX_KHR)!=modes.end()) choice.present_mode=VK_PRESENT_MODE_MAILBOX_KHR;
  choice.extent=c.currentExtent.width!=std::numeric_limits<std::uint32_t>::max()?c.currentExtent:VkExtent2D{std::clamp(requested.width,c.minImageExtent.width,c.maxImageExtent.width),std::clamp(requested.height,c.minImageExtent.height,c.maxImageExtent.height)};
  choice.image_count=std::max(c.minImageCount+1,2u); if(c.maxImageCount)choice.image_count=std::min(choice.image_count,c.maxImageCount); return choice;
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
      ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
  plan.composite_alpha = Alpha(capabilities.supportedCompositeAlpha);
  return plan;
}

struct VulkanPresentation::Impl {
  VulkanDeviceContext* context = nullptr; VulkanGuestImageCache* cache = nullptr; std::optional<VulkanPresentationTestHooks> hooks; Dispatch dispatch; VkSwapchainKHR swapchain=VK_NULL_HANDLE; std::vector<VkImage> images; VkSurfaceFormatKHR format{}; VkPresentModeKHR mode=VK_PRESENT_MODE_FIFO_KHR; VkExtent2D requested{640,480}, extent{}; VkCommandPool pool=VK_NULL_HANDLE; VkCommandBuffer command=VK_NULL_HANDLE; VkSemaphore acquired=VK_NULL_HANDLE, ready=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE; VulkanGuestImagePreparation preparation{}; bool pending=false, recreate=false, poisoned=false; std::uint64_t timeline=0;
  ~Impl() { if (hooks.has_value()) { if (pending) { (void)hooks->complete(preparation); hooks->discard(preparation); } return; } if (pending) { (void)context->WaitIdle(); const bool completed=cache->Complete(preparation); if(!completed) context->MarkDeviceLost(); cache->Discard(preparation); } if(fence)dispatch.destroy_fence(context->device(),fence,nullptr); if(ready)dispatch.destroy_semaphore(context->device(),ready,nullptr); if(acquired)dispatch.destroy_semaphore(context->device(),acquired,nullptr); if(pool)dispatch.destroy_pool(context->device(),pool,nullptr); if(swapchain)dispatch.destroy_swapchain(context->device(),swapchain,nullptr); context->ReleasePresentationOwner(); }
};

static bool CreateSwapchain(VulkanPresentation::Impl& i, VulkanPresentationResult& result) {
  if (i.hooks.has_value()) {
    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    if (!i.hooks->recreate || !i.hooks->recreate(i.swapchain, replacement, images) ||
        replacement == VK_NULL_HANDLE || images.empty()) {
      Add(result, VulkanPresentationStatus::kRecreateRequired,
          "injected swapchain recreation failed");
      return false;
    }
    i.swapchain = replacement; i.images = std::move(images); i.extent = i.requested;
    i.recreate = false; return true;
  }
  VkSurfaceCapabilitiesKHR caps{}; VkResult r=i.dispatch.capabilities(i.context->physical_device(),i.context->presentation_surface(),&caps); if(r!=VK_SUCCESS){Add(result,VulkanPresentationStatus::kDeviceFailure,"surface capabilities query failed",r);return false;}
  if((caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)==0){Add(result,VulkanPresentationStatus::kSurfaceUnavailable,"surface does not support transfer-destination swapchain images");return false;}
  std::vector<VkSurfaceFormatKHR> formats; std::vector<VkPresentModeKHR> modes; if(!Enumerate<VkSurfaceFormatKHR>([&](auto*c,auto*v){return i.dispatch.formats(i.context->physical_device(),i.context->presentation_surface(),c,v);},formats)||!Enumerate<VkPresentModeKHR>([&](auto*c,auto*v){return i.dispatch.modes(i.context->physical_device(),i.context->presentation_surface(),c,v);},modes)||formats.empty()){Add(result,VulkanPresentationStatus::kSurfaceUnavailable,"surface formats or present modes are unavailable");return false;}
  auto plan=BuildVulkanPresentationSwapchainPlan(caps,formats,modes,i.requested); auto choice=plan.choice; if(plan.retry_when_minimized){Add(result,VulkanPresentationStatus::kMinimized,"surface has zero extent");return false;}
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=i.context->presentation_surface();ci.minImageCount=choice.image_count;ci.imageFormat=choice.format.format;ci.imageColorSpace=choice.format.colorSpace;ci.imageExtent=choice.extent;ci.imageArrayLayers=1;ci.imageUsage=plan.image_usage;ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;ci.preTransform=plan.transform;ci.compositeAlpha=plan.composite_alpha;ci.presentMode=choice.present_mode;ci.clipped=VK_TRUE;ci.oldSwapchain=i.swapchain;
  VkSwapchainKHR replacement=VK_NULL_HANDLE;r=i.dispatch.create_swapchain(i.context->device(),&ci,nullptr,&replacement);if(r!=VK_SUCCESS){Add(result,VulkanPresentationStatus::kRecreateRequired,"swapchain creation failed",r);return false;}
  std::vector<VkImage> images;if(!Enumerate<VkImage>([&](auto*c,auto*v){return i.dispatch.images(i.context->device(),replacement,c,v);},images)||images.empty()){i.dispatch.destroy_swapchain(i.context->device(),replacement,nullptr);Add(result,VulkanPresentationStatus::kDeviceFailure,"swapchain returned no images");return false;}
  VkSwapchainKHR old=std::exchange(i.swapchain,replacement);i.images=std::move(images);i.format=choice.format;i.mode=choice.present_mode;i.extent=choice.extent;i.recreate=false;if(old)i.dispatch.destroy_swapchain(i.context->device(),old,nullptr);return true;
}

VulkanPresentation::VulkanPresentation(std::unique_ptr<Impl> impl) noexcept:impl_(std::move(impl)){}
VulkanPresentation::~VulkanPresentation()=default;
std::unique_ptr<VulkanPresentation> VulkanPresentation::Create(VulkanDeviceContext& c,VulkanGuestImageCache& cache,VulkanPresentationResult& result) { bool lost=false;if(!c.TryAcquirePresentationOwner(lost)){Add(result,lost?VulkanPresentationStatus::kDeviceLost:VulkanPresentationStatus::kSurfaceUnavailable,"presentation surface is unavailable or already owned");return nullptr;}auto impl=std::make_unique<Impl>(Impl{&c,&cache});if(!Load(c,impl->dispatch)){Add(result,VulkanPresentationStatus::kDeviceFailure,"presentation Vulkan functions are unavailable");return nullptr;}VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=c.queue_family_index();if(impl->dispatch.create_pool(c.device(),&pi,nullptr,&impl->pool)!=VK_SUCCESS){Add(result,VulkanPresentationStatus::kDeviceFailure,"command pool creation failed");return nullptr;}VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=impl->pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;if(impl->dispatch.allocate(c.device(),&ai,&impl->command)!=VK_SUCCESS){Add(result,VulkanPresentationStatus::kDeviceFailure,"command buffer allocation failed");return nullptr;}VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};if(impl->dispatch.create_semaphore(c.device(),&si,nullptr,&impl->acquired)!=VK_SUCCESS||impl->dispatch.create_semaphore(c.device(),&si,nullptr,&impl->ready)!=VK_SUCCESS){Add(result,VulkanPresentationStatus::kDeviceFailure,"presentation semaphore creation failed");return nullptr;}VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(impl->dispatch.create_fence(c.device(),&fi,nullptr,&impl->fence)!=VK_SUCCESS){Add(result,VulkanPresentationStatus::kDeviceFailure,"presentation fence creation failed");return nullptr;}if(!CreateSwapchain(*impl,result))return nullptr;result.status=VulkanPresentationStatus::kOk;return std::unique_ptr<VulkanPresentation>(new VulkanPresentation(std::move(impl))); }

std::unique_ptr<VulkanPresentation> VulkanPresentation::CreateForTesting(
    VulkanPresentationTestHooks hooks, VulkanPresentationResult& result) {
  auto impl = std::make_unique<Impl>(); impl->hooks = std::move(hooks);
  impl->requested = {64, 64};
  if (!CreateSwapchain(*impl, result)) return nullptr;
  result.status = VulkanPresentationStatus::kOk;
  return std::unique_ptr<VulkanPresentation>(new VulkanPresentation(std::move(impl)));
}

VulkanPresentationResult VulkanPresentation::Poll(){
  VulkanPresentationResult r;
  if (impl_->hooks.has_value()) {
    if (impl_->poisoned) { Add(r,VulkanPresentationStatus::kDeviceLost,"injected presentation is terminally lost"); return r; }
    if (!impl_->pending) { if (impl_->recreate && !CreateSwapchain(*impl_,r)) return r; return r; }
    const VkResult status = impl_->hooks->fence_status();
    if (status == VK_NOT_READY) { Add(r,VulkanPresentationStatus::kRetainedWorkPending,"injected present work remains retained"); return r; }
    if (status == VK_ERROR_DEVICE_LOST) { impl_->poisoned=true; Add(r,VulkanPresentationStatus::kDeviceLost,"injected fence reported device loss",status); return r; }
    if (status != VK_SUCCESS) { Add(r,VulkanPresentationStatus::kDeviceFailure,"injected fence status failed",status); return r; }
    const bool complete = impl_->hooks->complete(impl_->preparation);
    impl_->hooks->discard(impl_->preparation); impl_->preparation={}; impl_->pending=false;
    if (!complete) { Add(r,VulkanPresentationStatus::kDeviceFailure,"injected image completion failed"); return r; }
    if (impl_->recreate && !CreateSwapchain(*impl_,r)) return r;
    r.timeline=impl_->timeline; return r;
  }
  if(impl_->poisoned||impl_->context->is_device_lost()){impl_->poisoned=true;Add(r,VulkanPresentationStatus::kDeviceLost,"Vulkan device is terminally lost");return r;}
  if(!impl_->pending){if(impl_->recreate&&!CreateSwapchain(*impl_,r))return r;return r;}
  const VkResult s=impl_->dispatch.fence_status(impl_->context->device(),impl_->fence);
  if(s==VK_NOT_READY){Add(r,VulkanPresentationStatus::kRetainedWorkPending,"presentation work remains retained");return r;}
  if(s==VK_ERROR_DEVICE_LOST){impl_->poisoned=true;impl_->context->MarkDeviceLost();Add(r,VulkanPresentationStatus::kDeviceLost,"presentation fence reported device loss",s);return r;}
  if(s!=VK_SUCCESS){Add(r,VulkanPresentationStatus::kDeviceFailure,"presentation fence status failed",s);return r;}
  const bool completed = impl_->cache->Complete(impl_->preparation);
  impl_->cache->Discard(impl_->preparation); impl_->preparation={}; impl_->pending=false;
  if (!completed) { Add(r,VulkanPresentationStatus::kDeviceFailure,"completed present work could not be finalized in guest-image coherence"); return r; }
  if(impl_->recreate&&!CreateSwapchain(*impl_,r))return r;r.timeline=impl_->timeline;return r;
}

VulkanPresentationResult VulkanPresentation::RequestResize(VkExtent2D extent){VulkanPresentationResult r;impl_->requested=extent;impl_->recreate=true;if(extent.width==0||extent.height==0)Add(r,VulkanPresentationStatus::kMinimized,"presentation resize is minimized");return r;}

VulkanPresentationResult VulkanPresentation::Present(const GuestImageLayoutInput& input,std::uint64_t timeout,std::optional<VulkanImageFormat> format_override){VulkanGuestImageRequest req{.input=input,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT,.format_override=format_override};VulkanPresentationResult r;if(timeout==0||timeout==std::numeric_limits<std::uint64_t>::max()){Add(r,VulkanPresentationStatus::kInvalidFrame,"presentation requires a finite nonzero timeout");return r;}if(impl_->hooks.has_value()){auto p=impl_->hooks->prepare(req);if(!p){Add(r,VulkanPresentationStatus::kInvalidFrame,"injected guest frame rejected");return r;}std::uint32_t image=0;VkResult api=impl_->hooks->acquire(timeout,&image);if(api==VK_TIMEOUT){impl_->hooks->discard(p);Add(r,VulkanPresentationStatus::kAcquireTimeout,"injected acquire timeout",api);return r;}if(api==VK_ERROR_OUT_OF_DATE_KHR){impl_->hooks->discard(p);impl_->recreate=true;Add(r,VulkanPresentationStatus::kRecreateRequired,"injected acquire out of date",api);return r;}if(api==VK_ERROR_DEVICE_LOST){impl_->hooks->discard(p);impl_->poisoned=true;Add(r,VulkanPresentationStatus::kDeviceLost,"injected acquire device loss",api);return r;}if(api!=VK_SUCCESS&&api!=VK_SUBOPTIMAL_KHR){impl_->hooks->discard(p);Add(r,VulkanPresentationStatus::kDeviceFailure,"injected acquire failed",api);return r;}if(api==VK_SUBOPTIMAL_KHR)impl_->recreate=true;api=impl_->hooks->submit();if(api!=VK_SUCCESS){impl_->hooks->discard(p);if(api==VK_ERROR_DEVICE_LOST)impl_->poisoned=true;Add(r,api==VK_ERROR_DEVICE_LOST?VulkanPresentationStatus::kDeviceLost:VulkanPresentationStatus::kDeviceFailure,"injected submit failed",api);return r;}const bool marked=impl_->hooks->mark_submitted(p);api=impl_->hooks->present();impl_->preparation=std::move(p);impl_->pending=true;++impl_->timeline;r.timeline=impl_->timeline;if(!marked){Add(r,VulkanPresentationStatus::kDeviceFailure,"injected image submission registration failed");return r;}if(api==VK_ERROR_OUT_OF_DATE_KHR||api==VK_SUBOPTIMAL_KHR){impl_->recreate=true;Add(r,VulkanPresentationStatus::kRecreateRequired,"injected present needs recreation",api);return r;}if(api==VK_ERROR_DEVICE_LOST){impl_->poisoned=true;Add(r,VulkanPresentationStatus::kDeviceLost,"injected present device loss",api);return r;}if(api!=VK_SUCCESS){Add(r,VulkanPresentationStatus::kDeviceFailure,"injected present failed",api);return r;}return r;}auto poll=Poll();if(poll.status!=VulkanPresentationStatus::kOk)return poll;if(impl_->recreate&&!CreateSwapchain(*impl_,r))return r;auto p=impl_->cache->Prepare(req);if(!p){Add(r,VulkanPresentationStatus::kInvalidFrame,"guest frame layout or checked guest memory was rejected");return r;}std::uint32_t image=0;VkResult api;{std::lock_guard lock(impl_->context->queue_mutex());api=impl_->dispatch.acquire(impl_->context->device(),impl_->swapchain,timeout,impl_->acquired,VK_NULL_HANDLE,&image);}if(api==VK_TIMEOUT){impl_->cache->Discard(p);Add(r,VulkanPresentationStatus::kAcquireTimeout,"swapchain acquire timed out",api);return r;}if(api==VK_ERROR_OUT_OF_DATE_KHR){impl_->cache->Discard(p);impl_->recreate=true;Add(r,VulkanPresentationStatus::kRecreateRequired,"swapchain acquire is out of date",api);return r;}if(api==VK_ERROR_DEVICE_LOST){impl_->cache->Discard(p);impl_->poisoned=true;impl_->context->MarkDeviceLost();Add(r,VulkanPresentationStatus::kDeviceLost,"swapchain acquire reported device loss",api);return r;}if(api!=VK_SUCCESS&&api!=VK_SUBOPTIMAL_KHR){impl_->cache->Discard(p);Add(r,VulkanPresentationStatus::kDeviceFailure,"swapchain acquire failed",api);return r;}if(api==VK_SUBOPTIMAL_KHR)impl_->recreate=true;if(impl_->dispatch.reset_command(impl_->command,0)!=VK_SUCCESS){impl_->cache->Discard(p);Add(r,VulkanPresentationStatus::kDeviceFailure,"command buffer reset failed");return r;}VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;if(impl_->dispatch.begin(impl_->command,&bi)!=VK_SUCCESS||!impl_->cache->RecordUpload(impl_->command,p,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT)){impl_->cache->Discard(p);Add(r,VulkanPresentationStatus::kDeviceFailure,"guest frame upload recording failed");return r;}VkImageMemoryBarrier dst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};dst.srcAccessMask=0;dst.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;dst.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;dst.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;dst.image=impl_->images[image];dst.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;dst.subresourceRange.levelCount=1;dst.subresourceRange.layerCount=1;impl_->dispatch.barrier(impl_->command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&dst);VkImageBlit blit{};blit.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;blit.srcSubresource.layerCount=1;blit.srcOffsets[1]={static_cast<int>(input.width),static_cast<int>(input.height),1};blit.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;blit.dstSubresource.layerCount=1;blit.dstOffsets[1]={static_cast<int>(impl_->extent.width),static_cast<int>(impl_->extent.height),1};impl_->dispatch.blit(impl_->command,p.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,impl_->images[image],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);dst.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;dst.dstAccessMask=0;dst.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;dst.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;impl_->dispatch.barrier(impl_->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&dst);if(impl_->dispatch.end(impl_->command)!=VK_SUCCESS||impl_->dispatch.reset_fences(impl_->context->device(),1,&impl_->fence)!=VK_SUCCESS){impl_->cache->Discard(p);Add(r,VulkanPresentationStatus::kDeviceFailure,"presentation command finalization failed");return r;}VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&impl_->acquired;submit.pWaitDstStageMask=&stage;submit.commandBufferCount=1;submit.pCommandBuffers=&impl_->command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&impl_->ready;{std::lock_guard lock(impl_->context->queue_mutex());api=impl_->dispatch.submit(impl_->context->queue(),1,&submit,impl_->fence);}if(api!=VK_SUCCESS){impl_->cache->Discard(p);if(api==VK_ERROR_DEVICE_LOST){impl_->poisoned=true;impl_->context->MarkDeviceLost();Add(r,VulkanPresentationStatus::kDeviceLost,"presentation submit reported device loss",api);}else Add(r,VulkanPresentationStatus::kDeviceFailure,"presentation submit failed",api);return r;}const bool submitted_to_cache=impl_->cache->MarkSubmitted(p);VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&impl_->ready;present.swapchainCount=1;present.pSwapchains=&impl_->swapchain;present.pImageIndices=&image;{std::lock_guard lock(impl_->context->queue_mutex());api=impl_->dispatch.present(impl_->context->queue(),&present);}impl_->preparation=std::move(p);impl_->pending=true;++impl_->timeline;r.timeline=impl_->timeline;if(!submitted_to_cache){Add(r,VulkanPresentationStatus::kDeviceFailure,"submitted present work could not be registered with guest-image coherence");return r;}if(api==VK_ERROR_OUT_OF_DATE_KHR||api==VK_SUBOPTIMAL_KHR){impl_->recreate=true;Add(r,VulkanPresentationStatus::kRecreateRequired,"presentation requires swapchain recreation",api);return r;}if(api==VK_ERROR_DEVICE_LOST){impl_->poisoned=true;impl_->context->MarkDeviceLost();Add(r,VulkanPresentationStatus::kDeviceLost,"queue present reported device loss",api);return r;}if(api!=VK_SUCCESS){Add(r,VulkanPresentationStatus::kDeviceFailure,"queue present failed",api);return r;}return r;}
VkSurfaceFormatKHR VulkanPresentation::surface_format() const noexcept{return impl_->format;} VkPresentModeKHR VulkanPresentation::present_mode() const noexcept{return impl_->mode;}
const char* VulkanPresentationStatusName(VulkanPresentationStatus s) noexcept { switch(s){case VulkanPresentationStatus::kOk:return"ok";case VulkanPresentationStatus::kContextUnavailable:return"context_unavailable";case VulkanPresentationStatus::kInvalidFrame:return"invalid_frame";case VulkanPresentationStatus::kSurfaceUnavailable:return"surface_unavailable";case VulkanPresentationStatus::kMinimized:return"minimized";case VulkanPresentationStatus::kAcquireTimeout:return"acquire_timeout";case VulkanPresentationStatus::kRetainedWorkPending:return"retained_work_pending";case VulkanPresentationStatus::kRecreateRequired:return"recreate_required";case VulkanPresentationStatus::kDeviceLost:return"device_lost";case VulkanPresentationStatus::kDeviceFailure:return"device_failure";}return"unknown";}
}  // namespace kajps5::gpu::vulkan
