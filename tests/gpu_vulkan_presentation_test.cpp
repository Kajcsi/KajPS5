// Copyright (C) 2026 KajPS5 contributors
// Deterministic Vulkan presentation policy tests. SPDX-License-Identifier: GPL-2.0-only

#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "gpu/vulkan/device.h"
#include "gpu/vulkan/presentation.h"

namespace {
namespace vk = kajps5::gpu::vulkan;
void Check(bool value, std::string_view message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }

vk::VulkanDeviceCandidate Candidate() {
  vk::VulkanDeviceCandidate c; c.candidate_index=4; c.name="present"; c.stable_id="a"; c.api_version=VK_API_VERSION_1_3; c.type=vk::VulkanPhysicalDeviceType::kDiscreteGpu; c.extensions={"VK_KHR_swapchain"}; c.queue_families={{0,1,true,true,true},{1,1,true,true,false}};
  c.features.dynamic_rendering=c.features.synchronization2=c.features.robust_image_access=true;
  c.features.timeline_semaphore=c.features.sampler_mirror_clamp_to_edge=true;
  c.features.sample_rate_shading=c.features.fragment_stores_and_atomics=true;
  c.features.sampler_anisotropy=c.features.robust_buffer_access=c.features.depth_bounds=true;
  c.features.shader_storage_image_write_without_format=c.features.shader_storage_image_read_without_format=true;
  c.features.shader_image_gather_extended=c.features.independent_blend=c.features.tessellation_shader=true;
  return c;
}

void TestChoiceAndPlan() {
  VkSurfaceCapabilitiesKHR caps{}; caps.minImageCount=2; caps.maxImageCount=3;
  caps.currentExtent={UINT32_MAX,UINT32_MAX}; caps.minImageExtent={64,64}; caps.maxImageExtent={800,600};
  caps.supportedUsageFlags=VK_IMAGE_USAGE_TRANSFER_DST_BIT; caps.supportedTransforms=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; caps.currentTransform=VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR; caps.supportedCompositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  const auto plan=vk::BuildVulkanPresentationSwapchainPlan(caps, {{VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},{VK_FORMAT_B8G8R8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}}, {VK_PRESENT_MODE_FIFO_KHR,VK_PRESENT_MODE_MAILBOX_KHR}, {2000,1});
  Check(plan.choice.format.format==VK_FORMAT_B8G8R8A8_SRGB, "did not prefer sRGB format");
  Check(plan.choice.present_mode==VK_PRESENT_MODE_MAILBOX_KHR, "did not prefer mailbox");
  Check(plan.choice.extent.width==800&&plan.choice.extent.height==64, "variable extent was not clamped");
  Check(plan.choice.image_count==3&&plan.image_usage==VK_IMAGE_USAGE_TRANSFER_DST_BIT, "image count or usage invalid");
  Check(plan.transform==VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR&&plan.composite_alpha==VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, "legal transform/alpha selection failed");
  caps.currentExtent={320,240}; caps.supportedTransforms=VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR; caps.supportedCompositeAlpha=VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
  const auto fixed=vk::BuildVulkanPresentationSwapchainPlan(caps, {{VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}}, {}, {1,1});
  Check(fixed.choice.extent.width==320&&fixed.choice.extent.height==240, "fixed extent changed");
  Check(fixed.choice.present_mode==VK_PRESENT_MODE_FIFO_KHR&&fixed.transform==VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR&&fixed.composite_alpha==VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, "FIFO or legal fallback failed");
  caps.currentExtent={0,0}; const auto minimized=vk::BuildVulkanPresentationSwapchainPlan(caps, {{VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}}, {}, {1,1});
  Check(minimized.retry_when_minimized, "zero extent did not request retry");
}

void TestPresentQueueSelection() {
  auto candidate=Candidate(); vk::VulkanDeviceRequirements requirements; requirements.require_present_queue=true; requirements.required_extensions={"VK_KHR_swapchain"};
  auto selected=vk::SelectVulkanDevice({candidate},requirements);
  Check(selected&&selected.selection->queue_family_index==0, "same graphics/compute/present queue was not selected");
  candidate.queue_families[0].supports_present=false;
  auto rejected=vk::SelectVulkanDevice({candidate},requirements);
  Check(!rejected, "missing present support was accepted");
  candidate=Candidate(); candidate.extensions.clear();
  auto no_extension=vk::SelectVulkanDevice({candidate},requirements);
  Check(!no_extension, "missing swapchain extension was accepted");
}

struct Fixture {
  std::deque<VkResult> acquire{VK_SUCCESS}; std::deque<VkResult> present{VK_SUCCESS};
  std::deque<VkResult> fence{VK_SUCCESS}; std::vector<VkSwapchainKHR> old_swapchains;
  kajps5::gpu::GuestImageLayoutInput prepared_input{};
  VkImageUsageFlags prepared_usage = 0;
  std::optional<vk::VulkanImageFormat> prepared_format_override;
  int prepare=0, discard=0, complete=0, mark=0, submit=0, recreate=0;
  static VkResult Next(std::deque<VkResult>& scripted) {
    if (scripted.empty()) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult result = scripted.front(); scripted.pop_front(); return result;
  }
  vk::VulkanPresentationTestHooks Hooks() {
    return {
      .prepare=[this](const vk::VulkanGuestImageRequest& request) { ++prepare; prepared_input=request.input; prepared_usage=request.usage; prepared_format_override=request.format_override; const auto& input=request.input; vk::VulkanGuestImagePreparation p; if (input.width == 0 || input.height == 0 || input.depth == 0 || input.guest_address == 0) { p.status=vk::VulkanGuestImageStatus::kInvalidLayout; return p; } p.status=vk::VulkanGuestImageStatus::kOk; p.image=reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(1)); return p; },
      .discard=[this](auto&) { ++discard; }, .complete=[this](auto&) { ++complete; return true; },
      .mark_submitted=[this](auto&) { ++mark; return true; },
      .acquire=[this](std::uint64_t, std::uint32_t* index) { *index=0; return Next(acquire); },
      .submit=[this] { ++submit; return VK_SUCCESS; }, .present=[this] { return Next(present); },
      .fence_status=[this] { return Next(fence); },
      .recreate=[this](VkSwapchainKHR old, VkSwapchainKHR& swapchain, std::vector<VkImage>& images) { old_swapchains.push_back(old); ++recreate; swapchain=reinterpret_cast<VkSwapchainKHR>(static_cast<std::uintptr_t>(recreate)); images={reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(1))}; return true; },
    };
  }
};
kajps5::gpu::GuestImageLayoutInput ValidInput() { kajps5::gpu::GuestImageLayoutInput input{}; input.guest_address=1; input.width=input.height=input.depth=1; return input; }
void TestInjectedStateMachine() {
  Fixture f; f.acquire={VK_TIMEOUT,VK_SUCCESS,VK_SUCCESS}; f.present={VK_SUCCESS,VK_SUCCESS}; f.fence={VK_NOT_READY,VK_SUCCESS,VK_SUCCESS};
  vk::VulkanPresentationResult created; auto presenter=vk::VulkanPresentation::CreateForTesting(f.Hooks(),created); Check(presenter&&created,"test presentation creation failed");
  Check(presenter->Present(ValidInput(),1).status==vk::VulkanPresentationStatus::kAcquireTimeout&&f.submit==0&&f.discard==1,"acquire timeout retained or submitted work");
  Check(presenter->Present(ValidInput(),1)&&f.submit==1,"later acquire did not submit");
  Check(presenter->Poll().status==vk::VulkanPresentationStatus::kRetainedWorkPending&&f.complete==0,"fence timeout released retained work");
  Check(presenter->Poll()&&f.complete==1&&f.discard==2,"completion did not reclaim exactly once");
  Check(presenter->Present(ValidInput(),1)&&f.submit==2,"frame after completion did not proceed");
  (void)presenter->Poll();
  Fixture override_fixture;
  vk::VulkanPresentationResult override_result;
  auto override_presenter=vk::VulkanPresentation::CreateForTesting(
      override_fixture.Hooks(), override_result);
  auto override_input=ValidInput();
  override_input.format=0x1234; override_input.width=2; override_input.height=3;
  override_input.depth=4; override_input.layers=5; override_input.mip_count=6;
  override_input.row_pitch_bytes=64; override_input.slice_pitch_bytes=256;
  override_input.image_type=kajps5::gpu::Prospero::ImageType::kColor2DArray;
  override_input.tile_mode=kajps5::gpu::Prospero::TileMode::kLinear;
  override_input.tightly_packed=false;
  const vk::VulkanImageFormat format_override{
      VK_FORMAT_A2R10G10B10_UNORM_PACK32,
      vk::VulkanImageStorageClass::kA2B10G10R10, std::nullopt};
  Check(override_presenter && override_result &&
            override_presenter->Present(override_input, 1, format_override) &&
            override_fixture.prepared_usage == VK_IMAGE_USAGE_TRANSFER_SRC_BIT &&
            override_fixture.prepared_format_override &&
            override_fixture.prepared_format_override->format == format_override.format &&
            override_fixture.prepared_format_override->storage_class == format_override.storage_class &&
            !override_fixture.prepared_format_override->sibling_format &&
            override_fixture.prepared_input.guest_address == override_input.guest_address &&
            override_fixture.prepared_input.format == override_input.format &&
            override_fixture.prepared_input.width == override_input.width &&
            override_fixture.prepared_input.height == override_input.height &&
            override_fixture.prepared_input.depth == override_input.depth &&
            override_fixture.prepared_input.layers == override_input.layers &&
            override_fixture.prepared_input.mip_count == override_input.mip_count &&
            override_fixture.prepared_input.row_pitch_bytes == override_input.row_pitch_bytes &&
            override_fixture.prepared_input.slice_pitch_bytes == override_input.slice_pitch_bytes &&
            override_fixture.prepared_input.image_type == override_input.image_type &&
            override_fixture.prepared_input.tile_mode == override_input.tile_mode &&
            override_fixture.prepared_input.tightly_packed == override_input.tightly_packed,
        "presentation did not preserve the explicit guest image request");
  (void)override_presenter->Poll();
  Fixture out; out.acquire={VK_ERROR_OUT_OF_DATE_KHR,VK_SUCCESS}; out.present={VK_SUBOPTIMAL_KHR}; out.fence={VK_SUCCESS};
  vk::VulkanPresentationResult result; auto p=vk::VulkanPresentation::CreateForTesting(out.Hooks(),result); Check(static_cast<bool>(p),"outdate fixture create failed");
  Check(p->Present(ValidInput(),1).status==vk::VulkanPresentationStatus::kRecreateRequired&&out.submit==0,"acquire out-of-date state wrong");
  Check(p->Poll()&&out.recreate==2&&out.old_swapchains[1]!=VK_NULL_HANDLE,"out-of-date recreation did not hand off old generation");
  Check(p->Present(ValidInput(),1).status==vk::VulkanPresentationStatus::kRecreateRequired,"suboptimal did not request recreate");
  Check(p->Poll()&&out.recreate==3,"suboptimal recreation was not delayed until completion");
  Fixture present_out; present_out.present={VK_ERROR_OUT_OF_DATE_KHR}; present_out.fence={VK_SUCCESS}; vk::VulkanPresentationResult present_out_result;
  auto present_out_p=vk::VulkanPresentation::CreateForTesting(present_out.Hooks(),present_out_result);
  Check(present_out_p->Present(ValidInput(),1).status==vk::VulkanPresentationStatus::kRecreateRequired,"present out-of-date did not request recreate");
  Check(present_out_p->Poll()&&present_out.recreate==2,"present out-of-date did not retire and recreate after completion");
  Fixture invalid; vk::VulkanPresentationResult bad_result; auto bad=vk::VulkanPresentation::CreateForTesting(invalid.Hooks(),bad_result); auto malformed=ValidInput(); malformed.width=0;
  Check(bad->Present(malformed,1).status==vk::VulkanPresentationStatus::kInvalidFrame&&invalid.acquire.size()==1&&invalid.submit==0,"malformed frame reached acquire or submit");
  Fixture lost; lost.acquire={VK_ERROR_DEVICE_LOST}; vk::VulkanPresentationResult lost_result; auto terminal=vk::VulkanPresentation::CreateForTesting(lost.Hooks(),lost_result);
  Check(terminal->Present(ValidInput(),1).status==vk::VulkanPresentationStatus::kDeviceLost&&terminal->Poll().status==vk::VulkanPresentationStatus::kDeviceLost&&lost.discard==1,"device loss was not terminal or cleanup was not exactly once");
}
}  // namespace
int main() { TestChoiceAndPlan(); TestPresentQueueSelection(); TestInjectedStateMachine(); std::cout << "presentation policy tests passed\n"; }
