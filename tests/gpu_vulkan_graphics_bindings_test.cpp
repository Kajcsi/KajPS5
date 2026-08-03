// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "gpu/vulkan/graphics_bindings.h"

namespace {
namespace vk = kajps5::gpu::vulkan;
using kajps5::gpu::ShaderType;

[[noreturn]] void Fail(const char* message) { std::cerr << message << '\n'; std::exit(1); }
void Check(bool value, const char* message) { if (!value) Fail(message); }

kajps5::gpu::shader::recompiler::CompileResult Stage(ShaderType stage) {
  kajps5::gpu::shader::recompiler::CompileResult result;
  result.program.stage = stage;
  result.program.resource_tracking_complete = true;
  result.program.shader_info_complete = true;
  result.program.binding_layout_complete = true;
  result.program.bindings.descriptor_set = stage == ShaderType::Pixel ? 1 : 0;
  return result;
}

vk::VulkanGuestImageSetPreparation EmptyImages() {
  vk::VulkanGuestImageSetPreparation result;
  result.status = vk::VulkanGuestImageSetStatus::kOk;
  return result;
}

void AddSampledImage(kajps5::gpu::shader::recompiler::CompileResult& compile,
                     vk::VulkanGuestImageSetPreparation& images,
                     std::uint64_t address, bool storage = false) {
  namespace ir = kajps5::gpu::shader::recompiler::IR;
  ir::ImageResource resource;
  resource.kind = storage ? ir::ResourceKind::StorageImage : ir::ResourceKind::Image;
  resource.dimension = kajps5::gpu::shader::recompiler::Decoder::ImageDimension::Dim2D;
  resource.read = !storage; resource.written = storage;
  compile.program.info.images.push_back(resource);
  ir::DescriptorValue snapshot; snapshot.dword_count = 8;
  compile.resources.images.push_back(snapshot);
  compile.program.bindings.descriptors.push_back(
      {storage ? ir::DescriptorBindingKind::Storage2D : ir::DescriptorBindingKind::Sampled2D,
       3, {0}});
  vk::VulkanGuestImagePreparation prepared;
  prepared.status = vk::VulkanGuestImageStatus::kOk;
  prepared.resource = 17; prepared.image = reinterpret_cast<VkImage>(std::uintptr_t{1});
  prepared.view = reinterpret_cast<VkImageView>(std::uintptr_t{2});
  prepared.layout.storage_key = {address, 64, 0};
  images.images.push_back(prepared);
  images.image_descriptors.push_back({
      compile.program.bindings.descriptors.back().kind, 3, 0, 0, 0, prepared.view,
      storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      !storage, storage});
}

void TestDescriptorFreeLayouts() {
  auto vertex = Stage(ShaderType::Vertex);
  auto pixel = Stage(ShaderType::Pixel);
  vk::VulkanGuestBufferPreparation vb, pb;
  auto vi = EmptyImages(); auto pi = EmptyImages();
  vk::VulkanDeviceCandidate device;
  device.max_bound_descriptor_sets = 1;
  device.max_push_constants_size = 128;
  device.max_per_stage_resources = 0;
  vk::VulkanGraphicsBindingPlan plan;
  const auto status = vk::BuildVulkanGraphicsBindingPlan(
      device, {&vertex, &vb, &vi}, {&pixel, &pb, &pi}, plan);
  Check(status == vk::VulkanGraphicsBindingStatus::kOk && plan &&
            plan.set_layouts.size() == 1 && plan.set_layouts[0].set == 0 &&
            plan.set_layouts[0].bindings.empty(),
        "descriptor-free graphics plan must publish one empty set-0 layout");
}

void TestPixelSetAndStableImageRecord() {
  auto vertex = Stage(ShaderType::Vertex);
  auto pixel = Stage(ShaderType::Pixel);
  vk::VulkanGuestBufferPreparation vb, pb;
  auto vi = EmptyImages(); auto pi = EmptyImages();
  AddSampledImage(pixel, pi, 0x4000);
  vk::VulkanDeviceCandidate device;
  device.max_bound_descriptor_sets = 2; device.max_push_constants_size = 128;
  device.max_per_stage_resources = 1; device.max_per_stage_descriptor_sampled_images = 1;
  device.max_descriptor_set_sampled_images = 1;
  vk::VulkanGraphicsBindingPlan plan;
  Check(vk::BuildVulkanGraphicsBindingPlan(device, {&vertex, &vb, &vi}, {&pixel, &pb, &pi}, plan) ==
            vk::VulkanGraphicsBindingStatus::kOk && plan.set_layouts.size() == 2 &&
            plan.set_layouts[0].bindings.empty() && plan.set_layouts[1].set == 1 &&
            plan.set_layouts[1].bindings[0].image_infos[0].imageView == pi.images[0].view,
        "pixel-only descriptors must retain an explicit empty set 0 and a deep set-1 image record");
  pi.images[0].view = VK_NULL_HANDLE;
  Check(plan.set_layouts[1].bindings[0].image_infos[0].imageView != VK_NULL_HANDLE,
        "published descriptor records must not point into mutable preparations");
}

void TestInvalidAndAllocationFailureAreFailClosed() {
  auto vertex = Stage(ShaderType::Vertex);
  auto pixel = Stage(ShaderType::Pixel);
  vk::VulkanGuestBufferPreparation vb, pb;
  auto vi = EmptyImages(); auto pi = EmptyImages();
  vk::VulkanDeviceCandidate device;
  device.max_bound_descriptor_sets = 1; device.max_push_constants_size = 128;
  vertex.program.bindings.descriptor_set = 1;
  vk::VulkanGraphicsBindingPlan plan;
  Check(vk::BuildVulkanGraphicsBindingPlan(device, {&vertex, &vb, &vi}, {&pixel, &pb, &pi}, plan) ==
            vk::VulkanGraphicsBindingStatus::kInvalidSpecialization && !plan,
        "invalid set specialization must not publish a plan");
  vertex.program.bindings.descriptor_set = 0;
  Check(vk::BuildVulkanGraphicsBindingPlan(device, {&vertex, &vb, &vi}, {&pixel, &pb, &pi}, plan, 0) ==
            vk::VulkanGraphicsBindingStatus::kAllocationFailure && !plan,
        "injected allocation exhaustion must fail closed");
}
} // namespace

int main() {
  TestDescriptorFreeLayouts();
  TestPixelSetAndStableImageRecord();
  TestInvalidAndAllocationFailureAreFailClosed();
  return 0;
}
