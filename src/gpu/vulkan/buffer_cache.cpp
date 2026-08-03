// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/host_gpu/renderer/cache/bufferCache.* at
// fb5ecec455cf6c67154134429485ffccbfc34203. SPDX-License-Identifier:
// GPL-2.0-only

#include "gpu/vulkan/buffer_cache.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>

#include "core/memory/guest_memory.h"
#include "gpu/shader/bindings.h"

namespace kajps5::gpu::vulkan {
namespace {

constexpr std::size_t kMaximumGuestBufferViews = 32;
constexpr std::uint64_t kMaximumGuestBufferBytes = 64ULL * 1024ULL * 1024ULL;

void Fail(VulkanGuestBufferPreparation &result, VulkanGuestBufferStatus status,
          const char *message) {
  result.status = status;
  result.diagnostics.push_back({status, message});
}

bool Add(std::uint64_t left, std::uint64_t right, std::uint64_t &out) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  out = left + right;
  return true;
}

bool Footprint(const shader::recompiler::IR::BufferResource &resource,
               const ShaderBufferResource &descriptor,
               std::uint64_t &size) noexcept {
  // max_byte_extent is the recompiler's proven access bound. Descriptor facts
  // extend it only when they are representable without overflow.
  const std::uint64_t stride = descriptor.Stride();
  const std::uint64_t records = descriptor.NumRecords();
  std::uint64_t descriptor_size = 0;
  if (stride != 0 && records != 0) {
    if (records > std::numeric_limits<std::uint64_t>::max() / stride) {
      return false;
    }
    descriptor_size = records * stride;
  }
  size = std::max<std::uint64_t>(resource.max_byte_extent, descriptor_size);
  return size != 0;
}

struct BufferDispatch {
  PFN_vkCreateBuffer create_buffer = nullptr;
  PFN_vkDestroyBuffer destroy_buffer = nullptr;
  PFN_vkGetBufferMemoryRequirements get_buffer_memory_requirements = nullptr;
  PFN_vkAllocateMemory allocate_memory = nullptr;
  PFN_vkFreeMemory free_memory = nullptr;
  PFN_vkBindBufferMemory bind_buffer_memory = nullptr;
  PFN_vkMapMemory map_memory = nullptr;
  PFN_vkUnmapMemory unmap_memory = nullptr;
  PFN_vkFlushMappedMemoryRanges flush_mapped_memory_ranges = nullptr;
  PFN_vkInvalidateMappedMemoryRanges invalidate_mapped_memory_ranges = nullptr;
};

bool LoadBufferDispatch(VulkanDeviceContext &context,
                        BufferDispatch &dispatch) noexcept {
  const auto get = [&](const char *name) {
    return context.ResolveDeviceFunction(name);
  };
  dispatch.create_buffer =
      reinterpret_cast<PFN_vkCreateBuffer>(get("vkCreateBuffer"));
  dispatch.destroy_buffer =
      reinterpret_cast<PFN_vkDestroyBuffer>(get("vkDestroyBuffer"));
  dispatch.get_buffer_memory_requirements =
      reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(
          get("vkGetBufferMemoryRequirements"));
  dispatch.allocate_memory =
      reinterpret_cast<PFN_vkAllocateMemory>(get("vkAllocateMemory"));
  dispatch.free_memory =
      reinterpret_cast<PFN_vkFreeMemory>(get("vkFreeMemory"));
  dispatch.bind_buffer_memory =
      reinterpret_cast<PFN_vkBindBufferMemory>(get("vkBindBufferMemory"));
  dispatch.map_memory = reinterpret_cast<PFN_vkMapMemory>(get("vkMapMemory"));
  dispatch.unmap_memory =
      reinterpret_cast<PFN_vkUnmapMemory>(get("vkUnmapMemory"));
  dispatch.flush_mapped_memory_ranges =
      reinterpret_cast<PFN_vkFlushMappedMemoryRanges>(
          get("vkFlushMappedMemoryRanges"));
  dispatch.invalidate_mapped_memory_ranges =
      reinterpret_cast<PFN_vkInvalidateMappedMemoryRanges>(
          get("vkInvalidateMappedMemoryRanges"));
  return dispatch.create_buffer != nullptr &&
         dispatch.destroy_buffer != nullptr &&
         dispatch.get_buffer_memory_requirements != nullptr &&
         dispatch.allocate_memory != nullptr &&
         dispatch.free_memory != nullptr &&
         dispatch.bind_buffer_memory != nullptr &&
         dispatch.map_memory != nullptr && dispatch.unmap_memory != nullptr &&
         dispatch.flush_mapped_memory_ranges != nullptr &&
         dispatch.invalidate_mapped_memory_ranges != nullptr;
}

bool AlignUp(VkDeviceSize value, VkDeviceSize alignment,
             VkDeviceSize &out) noexcept {
  if (alignment == 0 ||
      value > std::numeric_limits<VkDeviceSize>::max() - (alignment - 1)) {
    return false;
  }
  out = ((value + alignment - 1) / alignment) * alignment;
  return true;
}

void DestroyBacking(const BufferDispatch &dispatch, VkDevice device,
                    VulkanGuestBufferPreparation &preparation) noexcept {
  if (preparation.mapped != nullptr && preparation.memory != VK_NULL_HANDLE) {
    dispatch.unmap_memory(device, preparation.memory);
    preparation.mapped = nullptr;
  }
  if (preparation.buffer != VK_NULL_HANDLE) {
    dispatch.destroy_buffer(device, preparation.buffer, nullptr);
    preparation.buffer = VK_NULL_HANDLE;
  }
  if (preparation.memory != VK_NULL_HANDLE) {
    dispatch.free_memory(device, preparation.memory, nullptr);
    preparation.memory = VK_NULL_HANDLE;
  }
}

} // namespace

VulkanGuestBufferCache::VulkanGuestBufferCache(
    VulkanDeviceContext &context, memory::GuestMemory &memory,
    GpuResourceCoherence &coherence) noexcept
    : context_(&context), memory_(memory), coherence_(coherence) {}

VulkanGuestBufferCache::VulkanGuestBufferCache(
    memory::GuestMemory &memory, GpuResourceCoherence &coherence) noexcept
    : memory_(memory), coherence_(coherence) {}

VulkanGuestBufferCache::~VulkanGuestBufferCache() {
  if (context_ != nullptr) {
    BufferDispatch dispatch;
    if (LoadBufferDispatch(*context_, dispatch)) {
      for (std::optional<IdleBacking> &idle : idle_backings_) {
        if (!idle.has_value())
          continue;
        VulkanGuestBufferPreparation preparation;
        preparation.buffer = idle->buffer;
        preparation.memory = idle->memory;
        preparation.mapped = idle->mapped;
        DestroyBacking(dispatch, context_->device(), preparation);
        idle.reset();
      }
    }
  }
  for (std::size_t index = 0; index < lost_dirty_count_; ++index) {
    (void)coherence_.UnregisterResource(lost_dirty_resources_[index]);
  }
}

VulkanGuestBufferPreparation VulkanGuestBufferCache::Prepare(
    const shader::recompiler::CompileResult &compile) {
  VulkanGuestBufferPreparation result;
  struct Rollback final {
    VulkanGuestBufferCache &cache;
    VulkanGuestBufferPreparation &preparation;
    bool armed = true;
    ~Rollback() {
      if (armed)
        cache.Discard(preparation);
    }
  } rollback{*this, result};
  const auto &program = compile.program;
  const auto &snapshot = compile.resources;
  if (program.stage != ShaderType::Compute ||
      !program.binding_layout_complete || !program.resource_tracking_complete ||
      !program.shader_info_complete) {
    Fail(result, VulkanGuestBufferStatus::kUnsupportedTopology,
         "translated compute requires a complete compute binding layout and "
         "resource snapshot");
    return result;
  }
  std::string error;
  if (!shader::recompiler::IR::ValidateResourceSpecialization(program, snapshot,
                                                              &error)) {
    Fail(result, VulkanGuestBufferStatus::kInvalidSpecialization,
         error.empty() ? "resource specialization is invalid" : error.c_str());
    return result;
  }
  const auto &layout = program.bindings;
  if (layout.descriptor_set != 0 || !snapshot.addresses.empty() ||
      !snapshot.flattened_srt.empty()) {
    Fail(result, VulkanGuestBufferStatus::kUnsupportedTopology,
         "translated compute uses an unsupported descriptor address space");
    return result;
  }
  const shader::recompiler::IR::DescriptorBinding *buffers = nullptr;
  for (const auto &binding : layout.descriptors) {
    using K = shader::recompiler::IR::DescriptorBindingKind;
    if (binding.kind == K::Buffers) {
      if (buffers != nullptr) {
        Fail(result, VulkanGuestBufferStatus::kUnsupportedTopology,
             "translated compute contains more than one buffer descriptor binding");
        return result;
      }
      buffers = &binding;
      continue;
    }
    const bool image_or_sampler = binding.kind == K::Samplers ||
        (binding.kind >= K::Sampled1D && binding.kind <= K::StorageUint3D);
    if (!image_or_sampler) {
      Fail(result, VulkanGuestBufferStatus::kUnsupportedTopology,
           "translated compute contains an unsupported non-buffer descriptor");
      return result;
    }
  }
  if ((buffers != nullptr && (buffers->resources.empty() ||
      buffers->resources.size() > kMaximumGuestBufferViews ||
      layout.buffer_offset_count != buffers->resources.size())) ||
      (buffers == nullptr && layout.buffer_offset_count != 0) ||
      snapshot.user_data.size() < layout.user_data_registers.size()) {
    Fail(result, VulkanGuestBufferStatus::kUnsupportedTopology,
         "translated compute buffer layout is incomplete or exceeds fixed "
         "cache bounds");
    return result;
  }

  try {
    if (buffers != nullptr)
      result.views.reserve(buffers->resources.size());
    if (layout.ShaderDataDwords() > 256 ||
        layout.buffer_offset_dword > std::numeric_limits<std::uint32_t>::max() -
                                         layout.buffer_offset_count) {
      Fail(result, VulkanGuestBufferStatus::kResourceLimit,
           "translated compute shader-data layout exceeds fixed cache bounds");
      return result;
    }
    result.shader_data_dwords.assign(layout.ShaderDataDwords(), 0);
    for (std::size_t index = 0; index < layout.user_data_registers.size();
         ++index) {
      const std::uint32_t reg = layout.user_data_registers[index];
      if (reg < program.user_data_base ||
          index >= result.shader_data_dwords.size()) {
        Fail(result, VulkanGuestBufferStatus::kInvalidDescriptor,
             "translated compute user-data layout is invalid");
        return result;
      }
      result.shader_data_dwords[index] =
          snapshot.user_data[reg - program.user_data_base];
    }
    // Image-only compute still has a valid (often empty) push-data layout.
    // The image cache owns all non-buffer descriptor groups in that case.
    if (buffers == nullptr) {
      result.status = VulkanGuestBufferStatus::kOk;
      rollback.armed = false;
      return result;
    }
    if (context_ != nullptr &&
        (buffers->resources.size() >
             context_->properties().max_per_stage_descriptor_storage_buffers ||
         buffers->resources.size() >
             context_->properties().max_descriptor_set_storage_buffers)) {
      Fail(result, VulkanGuestBufferStatus::kResourceLimit,
           "buffer descriptor count exceeds selected Vulkan device limits");
      return result;
    }
    std::uint64_t total_bytes = 0;
    for (std::size_t index = 0; index < buffers->resources.size(); ++index) {
      const std::uint32_t source = buffers->resources[index];
      if (source >= program.info.buffers.size() ||
          source >= snapshot.buffers.size() ||
          snapshot.buffers[source].dword_count != 4) {
        Fail(
            result, VulkanGuestBufferStatus::kInvalidDescriptor,
            "buffer descriptor does not match the recompiler's dense topology");
        return result;
      }
      const auto &resource = program.info.buffers[source];
      ShaderBufferResource descriptor{};
      std::copy_n(snapshot.buffers[source].dwords.begin(), 4,
                  descriptor.fields);
      std::uint64_t size = 0;
      if (!Footprint(resource, descriptor, size)) {
        Fail(result,
             resource.max_byte_extent == 0
                 ? VulkanGuestBufferStatus::kZeroFootprint
                 : VulkanGuestBufferStatus::kRangeOverflow,
             "buffer descriptor has a zero or overflowing byte footprint");
        return result;
      }
      std::uint64_t end = 0;
      if (size > kMaximumGuestBufferBytes ||
          !Add(total_bytes, size, total_bytes) ||
          total_bytes > kMaximumGuestBufferBytes ||
          !Add(descriptor.Base48(), size, end)) {
        Fail(result, VulkanGuestBufferStatus::kResourceLimit,
             "translated compute buffer footprint exceeds cache bounds");
        return result;
      }
      const auto read_permission = memory::GuestMemoryProtection::kRead |
                                   memory::GuestMemoryProtection::kGpuRead;
      const auto write_permission = memory::GuestMemoryProtection::kWrite |
                                    memory::GuestMemoryProtection::kGpuWrite;
      if (!memory_.CanAccess(descriptor.Base48(), size, read_permission)) {
        Fail(result, VulkanGuestBufferStatus::kGuestMemoryProtection,
             "guest buffer is not mapped with checked GPU-read permission");
        return result;
      }
      if ((resource.written || resource.atomic) &&
          !memory_.CanAccess(descriptor.Base48(), size, write_permission)) {
        Fail(result, VulkanGuestBufferStatus::kGuestMemoryProtection,
             "shader-written guest buffer is not mapped with checked GPU-write "
             "permission");
        return result;
      }
      auto id = coherence_.RegisterResource(descriptor.Base48(), size);
      if (!id.has_value()) {
        Fail(result, VulkanGuestBufferStatus::kGuestMemoryFault,
             "guest buffer range could not be registered with resource "
             "coherence");
        return result;
      }
      VulkanGuestBufferView view;
      view.resource = *id;
      view.guest_address = descriptor.Base48();
      view.size = size;
      view.descriptor_index = static_cast<std::uint32_t>(index);
      // Filled after all descriptors are known, relative to the aligned
      // descriptor binding base rather than the shader-data location.
      view.packed_offset_dword = 0;
      view.shader_reads = resource.read;
      view.shader_writes = resource.written || resource.atomic;
      view.uploaded_bytes.resize(static_cast<std::size_t>(size));
      if (!memory_.Read(view.guest_address, view.uploaded_bytes)) {
        (void)coherence_.UnregisterResource(*id);
        Fail(result, VulkanGuestBufferStatus::kGuestMemoryFault,
             "checked guest buffer upload failed");
        return result;
      }
      result.views.push_back(std::move(view));
    }
    std::uint64_t binding_base = result.views.front().guest_address;
    for (const VulkanGuestBufferView &view : result.views) {
      binding_base = std::min(binding_base, view.guest_address);
    }
    // The emitter stores dword offsets in six bits; use an aligned base so
    // the offset is exactly the value consumed after the shader's << 2.
    binding_base &= ~std::uint64_t{3};
    result.descriptor_binding_base = binding_base;
    if (context_ == nullptr || !context_->memory_properties().has_value()) {
      Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
           "selected Vulkan device did not expose physical memory properties");
      Discard(result);
      return result;
    }
    VkDeviceSize backing_end = 0;
    const VkDeviceSize storage_alignment = std::max<VkDeviceSize>(
        context_->properties().min_storage_buffer_offset_alignment, 1);
    for (std::size_t index = 0; index < result.views.size(); ++index) {
      VulkanGuestBufferView &view = result.views[index];
      const VkDeviceSize relative = static_cast<VkDeviceSize>(
          view.guest_address - result.descriptor_binding_base);
      view.descriptor_offset = relative - (relative % storage_alignment);
      const VkDeviceSize prefix = relative - view.descriptor_offset;
      if ((prefix & 3U) != 0 || prefix / 4U > 63U ||
          prefix > std::numeric_limits<VkDeviceSize>::max() - view.size) {
        Fail(result, VulkanGuestBufferStatus::kInvalidDescriptor,
             "descriptor prefix cannot be represented by the six-bit dword "
             "offset contract");
        return result;
      }
      view.packed_offset_dword = static_cast<std::uint32_t>(prefix / 4U);
      view.data_offset = view.descriptor_offset + prefix;
      view.descriptor_range = prefix + static_cast<VkDeviceSize>(view.size);
      if (view.descriptor_range >
              context_->properties().max_storage_buffer_range ||
          view.descriptor_offset > std::numeric_limits<VkDeviceSize>::max() -
                                       view.descriptor_range) {
        Fail(result, VulkanGuestBufferStatus::kResourceLimit,
             "descriptor view exceeds maxStorageBufferRange");
        return result;
      }
      const std::size_t word = layout.buffer_offset_dword + index / 4U;
      const std::uint32_t shift =
          static_cast<std::uint32_t>((index % 4U) * 8U + 2U);
      result.shader_data_dwords[word] |= view.packed_offset_dword << shift;
      const VkDeviceSize end = view.descriptor_offset + view.descriptor_range;
      backing_end = std::max(backing_end, end);
    }
    if (backing_end == 0 ||
        backing_end > context_->properties().max_storage_buffer_range) {
      Fail(result, VulkanGuestBufferStatus::kResourceLimit,
           "guest buffer backing exceeds maxStorageBufferRange");
      Discard(result);
      return result;
    }
    BufferDispatch dispatch;
    if (!LoadBufferDispatch(*context_, dispatch)) {
      Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
           "Vulkan buffer cache is missing required device entry points");
      Discard(result);
      return result;
    }
    // Reuse only an idle backing; in-flight preparations never enter this
    // array because they retain their own handles until fence completion.
    std::optional<std::size_t> reusable;
    for (std::size_t index = 0; index < idle_backings_.size(); ++index) {
      if (idle_backings_[index].has_value() &&
          idle_backings_[index]->logical_size >= backing_end &&
          (!reusable.has_value() ||
           idle_backings_[index]->logical_size <
               idle_backings_[*reusable]->logical_size)) {
        reusable = index;
      }
    }
    if (reusable.has_value()) {
      IdleBacking backing = std::move(*idle_backings_[*reusable]);
      idle_backings_[*reusable].reset();
      result.buffer = backing.buffer;
      result.memory = backing.memory;
      result.mapped = backing.mapped;
      result.allocation_size = backing.allocation_size;
      result.logical_size = backing.logical_size;
      result.host_coherent = backing.host_coherent;
    }
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = backing_end;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer created_buffer = VK_NULL_HANDLE;
    if (result.buffer == VK_NULL_HANDLE &&
        dispatch.create_buffer(context_->device(), &buffer_info, nullptr,
                               &created_buffer) != VK_SUCCESS) {
      Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
           "vkCreateBuffer failed");
      return result;
    }
    if (result.buffer == VK_NULL_HANDLE)
      result.buffer = created_buffer;
    if (reusable.has_value()) {
      // The allocation is already mapped and holds no live submission data.
      for (const VulkanGuestBufferView &view : result.views) {
        std::memcpy(static_cast<std::byte *>(result.mapped) + view.data_offset,
                    view.uploaded_bytes.data(), view.uploaded_bytes.size());
      }
      if (!result.host_coherent) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = result.memory;
        range.offset = 0;
        range.size = result.allocation_size;
        if (dispatch.flush_mapped_memory_ranges(context_->device(), 1,
                                                &range) != VK_SUCCESS) {
          Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
               "vkFlushMappedMemoryRanges failed");
          return result;
        }
      }
    }
    if (!reusable.has_value()) {
      VkMemoryRequirements requirements{};
      dispatch.get_buffer_memory_requirements(context_->device(), result.buffer,
                                              &requirements);
      const auto &memory_properties = *context_->memory_properties();
      std::uint32_t memory_type = UINT32_MAX;
      bool coherent = false;
      for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount;
           ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) == 0)
          continue;
        const VkMemoryPropertyFlags flags =
            memory_properties.memoryTypes[index].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
          continue;
        const bool candidate_coherent =
            (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        if (memory_type == UINT32_MAX || (candidate_coherent && !coherent)) {
          memory_type = index;
          coherent = candidate_coherent;
        }
      }
      VkDeviceSize allocation_size = 0;
      if (memory_type == UINT32_MAX ||
          !AlignUp(std::max(requirements.size, backing_end),
                   context_->properties().non_coherent_atom_size,
                   allocation_size)) {
        Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
             "no host-visible Vulkan memory type or aligned allocation size is "
             "available");
        Discard(result);
        return result;
      }
      VkMemoryAllocateInfo allocation_info{};
      allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocation_info.allocationSize = allocation_size;
      allocation_info.memoryTypeIndex = memory_type;
      VkDeviceMemory allocated_memory = VK_NULL_HANDLE;
      if (dispatch.allocate_memory(context_->device(), &allocation_info,
                                   nullptr, &allocated_memory) != VK_SUCCESS) {
        Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
             "vkAllocateMemory failed");
        return result;
      }
      result.memory = allocated_memory;
      void *mapped = nullptr;
      if (dispatch.bind_buffer_memory(context_->device(), result.buffer,
                                      result.memory, 0) != VK_SUCCESS ||
          dispatch.map_memory(context_->device(), result.memory, 0,
                              allocation_size, 0, &mapped) != VK_SUCCESS) {
        Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
             "vkBindBufferMemory or vkMapMemory failed");
        return result;
      }
      result.mapped = mapped;
      result.allocation_size = allocation_size;
      result.logical_size = backing_end;
      result.host_coherent = coherent;
      for (const VulkanGuestBufferView &view : result.views) {
        std::memcpy(static_cast<std::byte *>(result.mapped) + view.data_offset,
                    view.uploaded_bytes.data(), view.uploaded_bytes.size());
      }
      if (!coherent) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = result.memory;
        range.offset = 0;
        range.size = allocation_size;
        if (dispatch.flush_mapped_memory_ranges(context_->device(), 1,
                                                &range) != VK_SUCCESS) {
          Fail(result, VulkanGuestBufferStatus::kDeviceResourceFailure,
               "vkFlushMappedMemoryRanges failed");
          return result;
        }
      }
    }
    for (const VulkanGuestBufferView &view : result.views) {
      const auto state = coherence_.Query(view.resource);
      if (!state.has_value() ||
          !coherence_.AcknowledgeCpuUpload(view.resource,
                                           state->cpu_write_generation)) {
        Fail(result, VulkanGuestBufferStatus::kGuestMemoryFault,
             "guest buffer upload generation could not be acknowledged");
        return result;
      }
    }
  } catch (const std::bad_alloc &) {
    Fail(result, VulkanGuestBufferStatus::kResourceLimit,
         "translated compute buffer cache allocation failed");
  }
  if (result)
    rollback.armed = false;
  return result;
}

void VulkanGuestBufferCache::Discard(
    VulkanGuestBufferPreparation &preparation) noexcept {
  const bool device_lost = context_ != nullptr && context_->is_device_lost();
  if (!device_lost && preparation.backing_reusable &&
      preparation.buffer != VK_NULL_HANDLE) {
    for (std::optional<IdleBacking> &idle : idle_backings_) {
      if (idle.has_value())
        continue;
      idle.emplace(IdleBacking{preparation.buffer, preparation.memory,
                               preparation.mapped, preparation.allocation_size,
                               preparation.logical_size,
                               preparation.host_coherent});
      preparation.buffer = VK_NULL_HANDLE;
      preparation.memory = VK_NULL_HANDLE;
      preparation.mapped = nullptr;
      break;
    }
  }
  if (context_ != nullptr && (preparation.buffer != VK_NULL_HANDLE ||
                              preparation.memory != VK_NULL_HANDLE)) {
    BufferDispatch dispatch;
    if (LoadBufferDispatch(*context_, dispatch)) {
      DestroyBacking(dispatch, context_->device(), preparation);
    }
  }
  for (VulkanGuestBufferView &view : preparation.views) {
    // A lost device never establishes that mapped bytes completed. Keep the
    // resource-coherence GPU-pending record unresolved until runtime teardown
    // instead of fabricating a host readback or clearing it here.
    if (device_lost && view.gpu_dirty && view.resource != 0) {
      if (lost_dirty_count_ < lost_dirty_resources_.size()) {
        lost_dirty_resources_[lost_dirty_count_++] = view.resource;
      }
      view.resource = 0;
      view.gpu_dirty = false;
    } else if (view.resource != 0) {
      (void)coherence_.UnregisterResource(view.resource);
      view.resource = 0;
    }
  }
  preparation.views.clear();
}

std::size_t VulkanGuestBufferCache::idle_backing_count() const noexcept {
  std::size_t count = 0;
  for (const auto &idle : idle_backings_)
    count += idle.has_value() ? 1U : 0U;
  return count;
}

bool VulkanGuestBufferCache::MarkSubmitted(
    VulkanGuestBufferPreparation &preparation) noexcept {
  std::size_t marked = 0;
  for (VulkanGuestBufferView &view : preparation.views) {
    if (view.shader_writes && !coherence_.MarkGpuWrite(view.resource)) {
      for (std::size_t index = 0; index < marked; ++index) {
        const VulkanGuestBufferView &prior = preparation.views[index];
        if (prior.shader_writes)
          (void)coherence_.InvalidateGpuWrite(prior.resource);
        preparation.views[index].gpu_dirty = false;
      }
      return false;
    }
    if (view.shader_writes)
      view.gpu_dirty = true;
    ++marked;
  }
  return true;
}

bool VulkanGuestBufferCache::Complete(
    VulkanGuestBufferPreparation &preparation) noexcept {
  // Image-only translated work deliberately has no buffer allocation or
  // mapped backing. Its image cache owns completion for that submission.
  if (preparation.views.empty()) {
    return true;
  }
  if (context_ == nullptr || preparation.mapped == nullptr) {
    return false;
  }
  BufferDispatch dispatch;
  if (!LoadBufferDispatch(*context_, dispatch))
    return false;
  if (!preparation.host_coherent) {
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = preparation.memory;
    range.offset = 0;
    range.size = preparation.allocation_size;
    if (dispatch.invalidate_mapped_memory_ranges(context_->device(), 1,
                                                 &range) != VK_SUCCESS) {
      return false;
    }
  }
  try {
    for (const VulkanGuestBufferView &view : preparation.views) {
      if (!view.shader_writes)
        continue;
      const std::byte *gpu =
          static_cast<const std::byte *>(preparation.mapped) + view.data_offset;
      std::vector<std::byte> current(static_cast<std::size_t>(view.size));
      if (!memory_.Read(view.guest_address, current))
        return false;
      std::size_t offset = 0;
      while (offset < current.size()) {
        if (gpu[offset] == view.uploaded_bytes[offset]) {
          ++offset;
          continue;
        }
        const std::size_t start = offset;
        do {
          ++offset;
        } while (offset < current.size() &&
                 gpu[offset] != view.uploaded_bytes[offset]);
        for (std::size_t index = start; index < offset; ++index)
          current[index] = gpu[index];
        if (!memory_.Write(view.guest_address + start,
                           std::span<const std::byte>(current).subspan(
                               start, offset - start))) {
          return false;
        }
      }
    }
  } catch (const std::bad_alloc &) {
    return false;
  }
  for (VulkanGuestBufferView &view : preparation.views) {
    if (view.shader_writes && !coherence_.InvalidateGpuWrite(view.resource))
      return false;
    if (view.shader_writes)
      view.gpu_dirty = false;
  }
  preparation.backing_reusable = true;
  return true;
}

const char *
VulkanGuestBufferStatusName(VulkanGuestBufferStatus status) noexcept {
  switch (status) {
  case VulkanGuestBufferStatus::kOk:
    return "ok";
  case VulkanGuestBufferStatus::kUnsupportedTopology:
    return "unsupported_topology";
  case VulkanGuestBufferStatus::kInvalidSpecialization:
    return "invalid_specialization";
  case VulkanGuestBufferStatus::kInvalidDescriptor:
    return "invalid_descriptor";
  case VulkanGuestBufferStatus::kRangeOverflow:
    return "range_overflow";
  case VulkanGuestBufferStatus::kZeroFootprint:
    return "zero_footprint";
  case VulkanGuestBufferStatus::kGuestMemoryFault:
    return "guest_memory_fault";
  case VulkanGuestBufferStatus::kGuestMemoryProtection:
    return "guest_memory_protection";
  case VulkanGuestBufferStatus::kDeviceResourceFailure:
    return "device_resource_failure";
  case VulkanGuestBufferStatus::kResourceLimit:
    return "resource_limit";
  }
  return "unknown";
}

} // namespace kajps5::gpu::vulkan
