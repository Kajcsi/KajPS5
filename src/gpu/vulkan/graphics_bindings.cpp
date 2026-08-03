// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/graphics/host_gpu/renderer/pipeline/descriptors.cpp.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/graphics_bindings.h"

#include <algorithm>
#include <limits>
#include <new>
#include <string_view>

#include "gpu/shader/recompiler/ir/ResourceMaterialization.h"

namespace kajps5::gpu::vulkan {
namespace {
using Kind = shader::recompiler::IR::DescriptorBindingKind;
using Stage = ShaderType;

bool IsSampled(Kind kind) noexcept {
  return kind >= Kind::Sampled1D && kind <= Kind::SampledUint3D;
}
bool IsStorage(Kind kind) noexcept {
  return kind >= Kind::Storage1D && kind <= Kind::StorageUint3D;
}
VkDescriptorType TypeFor(Kind kind) noexcept {
  if (kind == Kind::Buffers) return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  if (kind == Kind::Samplers) return VK_DESCRIPTOR_TYPE_SAMPLER;
  if (IsSampled(kind)) return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  if (IsStorage(kind)) return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}
VkShaderStageFlags FlagFor(Stage stage) noexcept {
  return stage == Stage::Vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
}
bool Overlap(std::uint64_t lhs_address, std::uint64_t lhs_size,
             std::uint64_t rhs_address, std::uint64_t rhs_size) noexcept {
  if (lhs_size == 0 || rhs_size == 0) return false;
  if (lhs_address > std::numeric_limits<std::uint64_t>::max() - lhs_size ||
      rhs_address > std::numeric_limits<std::uint64_t>::max() - rhs_size) return true;
  return lhs_address < rhs_address + rhs_size && rhs_address < lhs_address + lhs_size;
}
void Fail(VulkanGraphicsBindingPlan& plan, VulkanGraphicsBindingStatus status,
          std::string_view message) {
  plan = {};
  try { plan.diagnostics.push_back({status, std::string(message)}); }
  catch (...) { plan.diagnostics.clear(); }
}
bool ConsumeBudget(std::size_t& budget, std::size_t count) noexcept {
  if (count > budget) return false;
  budget -= count;
  return true;
}
bool Within(std::uint64_t count, std::uint32_t limit) noexcept { return count <= limit; }

struct Counts { std::uint64_t buffers = 0, sampled = 0, storage = 0, samplers = 0; };
bool Limits(const Counts& count, const VulkanDeviceCandidate& p) noexcept {
  const auto total = count.buffers + count.sampled + count.storage + count.samplers;
  return Within(count.buffers, p.max_per_stage_descriptor_storage_buffers) &&
         Within(count.buffers, p.max_descriptor_set_storage_buffers) &&
         Within(count.sampled, p.max_per_stage_descriptor_sampled_images) &&
         Within(count.sampled, p.max_descriptor_set_sampled_images) &&
         Within(count.storage, p.max_per_stage_descriptor_storage_images) &&
         Within(count.storage, p.max_descriptor_set_storage_images) &&
         Within(count.samplers, p.max_per_stage_descriptor_samplers) &&
         Within(count.samplers, p.max_descriptor_set_samplers) &&
         Within(total, p.max_per_stage_resources);
}

bool AddPool(std::vector<VkDescriptorPoolSize>& pools, VkDescriptorType type,
             std::uint32_t count) {
  for (auto& pool : pools) {
    if (pool.type == type) {
      if (pool.descriptorCount > std::numeric_limits<std::uint32_t>::max() - count) return false;
      pool.descriptorCount += count;
      return true;
    }
  }
  pools.push_back({type, count});
  return true;
}

struct ImageUse {
  const VulkanGuestImageSetPreparation* set = nullptr;
  std::uint32_t index = 0;
  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkPipelineStageFlags stages = 0;
  VkAccessFlags access = 0;
};

bool SameImage(const ImageUse& use, const VulkanGuestImageSetPreparation& set,
               std::uint32_t index) noexcept { return use.set == &set && use.index == index; }

bool AppendStage(const VulkanGraphicsStageBindingInput& input, Stage expected,
                 const VulkanDeviceCandidate& properties, std::size_t& budget,
                 VulkanGraphicsBindingPlan& plan, Counts& counts,
                 std::vector<ImageUse>& image_uses) {
  if (input.compile == nullptr || input.buffers == nullptr || input.images == nullptr ||
      !*input.buffers || !*input.images) return false;
  const auto& program = input.compile->program;
  const auto& layout = program.bindings;
  std::string specialization_error;
  if (program.stage != expected || !program.binding_layout_complete ||
      !program.resource_tracking_complete || !program.shader_info_complete ||
      !shader::recompiler::IR::ValidateResourceSpecialization(program, input.compile->resources,
                                                               &specialization_error) ||
      layout.descriptor_set != (expected == Stage::Vertex ? 0U : 1U)) return false;
  const auto stage_flag = FlagFor(expected);
  if (!ConsumeBudget(budget, 1)) throw std::bad_alloc();
  VulkanGraphicsDescriptorSetPlan set;
  set.set = layout.descriptor_set;
  std::vector<bool> consumed_images(input.images->image_descriptors.size());
  std::vector<bool> consumed_samplers(input.images->sampler_descriptors.size());
  std::size_t image_cursor = 0;
  std::size_t sampler_cursor = 0;
  bool buffers_seen = false;
  for (const auto& group : layout.descriptors) {
    const VkDescriptorType type = TypeFor(group.kind);
    if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM || group.resources.empty() ||
        group.resources.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    if (std::any_of(set.bindings.begin(), set.bindings.end(), [&](const auto& b) {
          return b.layout.binding == group.binding; })) return false;
    if (!ConsumeBudget(budget, 1 + group.resources.size())) throw std::bad_alloc();
    VulkanGraphicsDescriptorBindingPlan binding;
    binding.layout.binding = group.binding;
    binding.layout.descriptorType = type;
    binding.layout.descriptorCount = static_cast<std::uint32_t>(group.resources.size());
    binding.layout.stageFlags = stage_flag;
    if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
      if (buffers_seen || group.resources.size() != input.buffers->views.size()) return false;
      buffers_seen = true;
      for (std::size_t i = 0; i < group.resources.size(); ++i) {
        const auto& view = input.buffers->views[i];
        if (group.resources[i] != i || view.descriptor_index != i ||
            input.buffers->buffer == VK_NULL_HANDLE || view.descriptor_range == 0) return false;
        binding.buffer_infos.push_back({input.buffers->buffer, view.descriptor_offset,
                                        view.descriptor_range});
      }
      counts.buffers += group.resources.size();
    } else if (type == VK_DESCRIPTOR_TYPE_SAMPLER) {
      for (std::size_t array = 0; array < group.resources.size(); ++array) {
        const auto dense = group.resources[array];
        if (dense >= program.info.samplers.size() || dense >= input.compile->resources.samplers.size() ||
            sampler_cursor >= input.images->sampler_descriptors.size()) return false;
        const auto& descriptor = input.images->sampler_descriptors[sampler_cursor];
        if (consumed_samplers[sampler_cursor] || descriptor.binding != group.binding ||
            descriptor.array_index != array || descriptor.dense_sampler_index != dense ||
            descriptor.lease_index >= input.images->samplers.size() ||
            descriptor.sampler == VK_NULL_HANDLE ||
            input.images->samplers[descriptor.lease_index].sampler != descriptor.sampler) return false;
        consumed_samplers[sampler_cursor++] = true;
        binding.image_infos.push_back({descriptor.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
      }
      counts.samplers += group.resources.size();
    } else {
      const bool storage = type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      for (std::size_t array = 0; array < group.resources.size(); ++array) {
        const auto dense = group.resources[array];
        if (dense >= program.info.images.size() || dense >= input.compile->resources.images.size() ||
            image_cursor >= input.images->image_descriptors.size()) return false;
        const auto& descriptor = input.images->image_descriptors[image_cursor];
        if (consumed_images[image_cursor] || descriptor.kind != group.kind || descriptor.binding != group.binding ||
            descriptor.array_index != array || descriptor.dense_image_index != dense ||
            descriptor.preparation_index >= input.images->images.size() ||
            descriptor.view == VK_NULL_HANDLE || descriptor.descriptor_type != type) return false;
        const auto& prepared = input.images->images[descriptor.preparation_index];
        if (!prepared || prepared.image == VK_NULL_HANDLE || prepared.resource == 0 ||
            prepared.layout.storage_key.byte_count == 0) return false;
        consumed_images[image_cursor++] = true;
        auto found = std::find_if(image_uses.begin(), image_uses.end(), [&](const ImageUse& use) {
          return SameImage(use, *input.images, descriptor.preparation_index);
        });
        if (found == image_uses.end()) {
          image_uses.push_back({input.images, descriptor.preparation_index,
                                storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                stage_flag, descriptor.shader_reads ? VK_ACCESS_SHADER_READ_BIT : VkAccessFlags{0}});
          found = std::prev(image_uses.end());
        } else {
          if (storage) found->layout = VK_IMAGE_LAYOUT_GENERAL;
          found->stages |= stage_flag;
          if (descriptor.shader_reads) found->access |= VK_ACCESS_SHADER_READ_BIT;
        }
        if (storage || descriptor.shader_writes) found->access |= VK_ACCESS_SHADER_WRITE_BIT;
        binding.image_infos.push_back({VK_NULL_HANDLE, descriptor.view, found->layout});
      }
      if (storage) counts.storage += group.resources.size(); else counts.sampled += group.resources.size();
    }
    if (!AddPool(plan.pool_sizes, type, binding.layout.descriptorCount)) return false;
    set.bindings.push_back(std::move(binding));
  }
  if ((!buffers_seen && !input.buffers->views.empty()) ||
      std::any_of(consumed_images.begin(), consumed_images.end(), [](bool used) { return !used; }) ||
      std::any_of(consumed_samplers.begin(), consumed_samplers.end(), [](bool used) { return !used; }) ||
      !Limits(counts, properties)) return false;
  const auto end = static_cast<std::uint64_t>(layout.push_constant_offset) + layout.push_constant_size;
  if ((layout.push_constant_offset & 3U) != 0 || (layout.push_constant_size & 3U) != 0 ||
      end > properties.max_push_constants_size || layout.push_constant_size !=
          input.buffers->shader_data_dwords.size() * sizeof(std::uint32_t)) return false;
  if (layout.push_constant_size != 0) {
    if (!ConsumeBudget(budget, 1 + input.buffers->shader_data_dwords.size())) throw std::bad_alloc();
    VulkanGraphicsPushConstantPlan push;
    push.range = {stage_flag, layout.push_constant_offset, layout.push_constant_size};
    push.data_dwords = input.buffers->shader_data_dwords;
    plan.push_constants.push_back(std::move(push));
  }
  plan.set_layouts.push_back(std::move(set));
  return true;
}
} // namespace

VulkanGraphicsBindingStatus BuildVulkanGraphicsBindingPlan(
    const VulkanDeviceCandidate& properties, const VulkanGraphicsStageBindingInput& vertex,
    const VulkanGraphicsStageBindingInput& pixel, VulkanGraphicsBindingPlan& output,
    std::size_t allocation_record_limit) noexcept {
  output = {};
  try {
    const auto preflight = [&](const VulkanGraphicsStageBindingInput& input, Stage expected) {
      if (input.compile == nullptr || input.buffers == nullptr || input.images == nullptr ||
          !*input.buffers || !*input.images) return VulkanGraphicsBindingStatus::kMalformedPreparation;
      const auto& program = input.compile->program;
      const auto& layout = program.bindings;
      std::string error;
      if (program.stage != expected || !program.binding_layout_complete ||
          !program.resource_tracking_complete || !program.shader_info_complete ||
          layout.descriptor_set != (expected == Stage::Vertex ? 0U : 1U) ||
          !shader::recompiler::IR::ValidateResourceSpecialization(program, input.compile->resources, &error))
        return VulkanGraphicsBindingStatus::kInvalidSpecialization;
      Counts counts;
      std::vector<std::uint32_t> bindings;
      for (const auto& group : layout.descriptors) {
        const auto type = TypeFor(group.kind);
        if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM || group.resources.empty() ||
            std::find(bindings.begin(), bindings.end(), group.binding) != bindings.end())
          return VulkanGraphicsBindingStatus::kUnsupportedTopology;
        bindings.push_back(group.binding);
        if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) counts.buffers += group.resources.size();
        else if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) counts.sampled += group.resources.size();
        else if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) counts.storage += group.resources.size();
        else counts.samplers += group.resources.size();
      }
      if (!Limits(counts, properties)) return VulkanGraphicsBindingStatus::kDeviceResourceLimit;
      const auto end = static_cast<std::uint64_t>(layout.push_constant_offset) + layout.push_constant_size;
      if ((layout.push_constant_offset & 3U) != 0 || (layout.push_constant_size & 3U) != 0 ||
          layout.push_constant_size != input.buffers->shader_data_dwords.size() * sizeof(std::uint32_t))
        return VulkanGraphicsBindingStatus::kMalformedPreparation;
      if (end > properties.max_push_constants_size) return VulkanGraphicsBindingStatus::kDeviceResourceLimit;
      return VulkanGraphicsBindingStatus::kOk;
    };
    const auto vertex_preflight = preflight(vertex, Stage::Vertex);
    const auto pixel_preflight = preflight(pixel, Stage::Pixel);
    if (vertex_preflight != VulkanGraphicsBindingStatus::kOk ||
        pixel_preflight != VulkanGraphicsBindingStatus::kOk) {
      const auto status = vertex_preflight != VulkanGraphicsBindingStatus::kOk ? vertex_preflight : pixel_preflight;
      Fail(output, status, "graphics stage input failed binding-plan preflight");
      return status;
    }
    std::size_t budget = allocation_record_limit;
    Counts vertex_counts, pixel_counts;
    std::vector<ImageUse> image_uses;
    if (!AppendStage(vertex, Stage::Vertex, properties, budget, output, vertex_counts, image_uses) ||
        !AppendStage(pixel, Stage::Pixel, properties, budget, output, pixel_counts, image_uses)) {
      Fail(output, VulkanGraphicsBindingStatus::kMalformedPreparation,
           "graphics descriptor preparation does not exactly match its translated binding layout");
      return output.diagnostics.front().status;
    }
    const bool vertex_used = !output.set_layouts[0].bindings.empty();
    const bool pixel_used = !output.set_layouts[1].bindings.empty();
    if ((pixel_used && properties.max_bound_descriptor_sets < 2) ||
        (!pixel_used && vertex_used && properties.max_bound_descriptor_sets < 1) ||
        (!pixel_used && !vertex_used && properties.max_bound_descriptor_sets < 1)) {
      Fail(output, VulkanGraphicsBindingStatus::kDeviceResourceLimit,
           "selected device does not expose the required descriptor-set count");
      return output.diagnostics.front().status;
    }
    for (auto& binding : output.set_layouts[0].bindings) {
      for (auto& image : binding.image_infos) {
        if (image.sampler == VK_NULL_HANDLE && image.imageView != VK_NULL_HANDLE) {
          for (const auto& use : image_uses) {
            // Matching by view is intentional: descriptor views may be auxiliary.
            for (const auto& descriptor : use.set->image_descriptors)
              if (descriptor.view == image.imageView && descriptor.preparation_index == use.index)
                image.imageLayout = use.layout;
          }
        }
      }
    }
    for (auto& binding : output.set_layouts[1].bindings) for (auto& image : binding.image_infos)
      if (image.sampler == VK_NULL_HANDLE && image.imageView != VK_NULL_HANDLE)
        for (const auto& use : image_uses) for (const auto& descriptor : use.set->image_descriptors)
          if (descriptor.view == image.imageView && descriptor.preparation_index == use.index)
            image.imageLayout = use.layout;
    for (const auto& use : image_uses) {
      const auto& prepared = use.set->images[use.index];
      output.image_uploads.push_back({prepared.resource == 0 ? 0U :
                                      (use.stages & VK_SHADER_STAGE_VERTEX_BIT ? 0U : 1U),
                                      use.index, prepared.resource, prepared.layout.storage_key.guest_address,
                                      prepared.layout.storage_key.byte_count, prepared.image, use.layout, use.stages, use.access});
    }
    // Cross-stage aliases are invalid only if either side writes.
    const auto& vb = *vertex.buffers;
    const auto& pb = *pixel.buffers;
    for (const auto& left : vb.views) for (const auto& right : pb.views)
      if (Overlap(left.guest_address, left.size, right.guest_address, right.size) &&
          (left.shader_writes || right.shader_writes)) {
        Fail(output, VulkanGraphicsBindingStatus::kAliasConflict, "writable vertex/pixel buffer ranges overlap");
        return output.diagnostics.front().status;
      }
    for (const auto& left : image_uses) for (const auto& right : image_uses) {
      if (&left >= &right || left.set == right.set || !(left.stages & VK_SHADER_STAGE_VERTEX_BIT) ||
          !(right.stages & VK_SHADER_STAGE_FRAGMENT_BIT)) continue;
      const auto& a = left.set->images[left.index]; const auto& b = right.set->images[right.index];
      if (Overlap(a.layout.storage_key.guest_address, a.layout.storage_key.byte_count,
                  b.layout.storage_key.guest_address, b.layout.storage_key.byte_count) &&
          ((left.access | right.access) & VK_ACCESS_SHADER_WRITE_BIT)) {
        Fail(output, VulkanGraphicsBindingStatus::kAliasConflict, "writable vertex/pixel image ranges overlap");
        return output.diagnostics.front().status;
      }
    }
    for (const auto& buffer : vb.views) for (const auto& image : image_uses) {
      if (!(image.stages & VK_SHADER_STAGE_FRAGMENT_BIT)) continue;
      const auto& prepared = image.set->images[image.index];
      if (Overlap(buffer.guest_address, buffer.size, prepared.layout.storage_key.guest_address,
                  prepared.layout.storage_key.byte_count) &&
          (buffer.shader_writes || (image.access & VK_ACCESS_SHADER_WRITE_BIT))) {
        Fail(output, VulkanGraphicsBindingStatus::kAliasConflict, "writable vertex buffer and pixel image ranges overlap");
        return output.diagnostics.front().status;
      }
    }
    for (const auto& buffer : pb.views) for (const auto& image : image_uses) {
      if (!(image.stages & VK_SHADER_STAGE_VERTEX_BIT)) continue;
      const auto& prepared = image.set->images[image.index];
      if (Overlap(buffer.guest_address, buffer.size, prepared.layout.storage_key.guest_address,
                  prepared.layout.storage_key.byte_count) &&
          (buffer.shader_writes || (image.access & VK_ACCESS_SHADER_WRITE_BIT))) {
        Fail(output, VulkanGraphicsBindingStatus::kAliasConflict, "writable pixel buffer and vertex image ranges overlap");
        return output.diagnostics.front().status;
      }
    }
    if (!pixel_used) output.set_layouts.pop_back();
    return VulkanGraphicsBindingStatus::kOk;
  } catch (const std::bad_alloc&) {
    Fail(output, VulkanGraphicsBindingStatus::kAllocationFailure, "binding-plan allocation failed");
    return VulkanGraphicsBindingStatus::kAllocationFailure;
  } catch (...) {
    Fail(output, VulkanGraphicsBindingStatus::kAllocationFailure, "binding-plan construction failed");
    return VulkanGraphicsBindingStatus::kAllocationFailure;
  }
}

const char* VulkanGraphicsBindingStatusName(VulkanGraphicsBindingStatus status) noexcept {
  switch (status) {
    case VulkanGraphicsBindingStatus::kOk: return "ok";
    case VulkanGraphicsBindingStatus::kInvalidSpecialization: return "invalid_specialization";
    case VulkanGraphicsBindingStatus::kUnsupportedTopology: return "unsupported_topology";
    case VulkanGraphicsBindingStatus::kMalformedPreparation: return "malformed_preparation";
    case VulkanGraphicsBindingStatus::kDeviceResourceLimit: return "device_resource_limit";
    case VulkanGraphicsBindingStatus::kAliasConflict: return "alias_conflict";
    case VulkanGraphicsBindingStatus::kAllocationFailure: return "allocation_failure";
  }
  return "unknown";
}
} // namespace kajps5::gpu::vulkan
