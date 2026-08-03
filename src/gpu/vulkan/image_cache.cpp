// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 image/cache model at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior reference: SharpEmu guest-image sizing and alias tests at 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/image_cache.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

#include "core/memory/guest_memory.h"
#include "gpu/format.h"

namespace kajps5::gpu::vulkan {
namespace {

struct Dispatch {
  PFN_vkCreateImage create_image = nullptr;
  PFN_vkDestroyImage destroy_image = nullptr;
  PFN_vkGetImageMemoryRequirements image_requirements = nullptr;
  PFN_vkBindImageMemory bind_image = nullptr;
  PFN_vkCreateImageView create_view = nullptr;
  PFN_vkDestroyImageView destroy_view = nullptr;
  PFN_vkCreateBuffer create_buffer = nullptr;
  PFN_vkDestroyBuffer destroy_buffer = nullptr;
  PFN_vkGetBufferMemoryRequirements buffer_requirements = nullptr;
  PFN_vkAllocateMemory allocate = nullptr;
  PFN_vkFreeMemory free = nullptr;
  PFN_vkBindBufferMemory bind_buffer = nullptr;
  PFN_vkMapMemory map = nullptr;
  PFN_vkUnmapMemory unmap = nullptr;
  PFN_vkFlushMappedMemoryRanges flush = nullptr;
  PFN_vkInvalidateMappedMemoryRanges invalidate = nullptr;
  PFN_vkCmdPipelineBarrier pipeline_barrier = nullptr;
  PFN_vkCmdCopyBufferToImage copy_buffer_to_image = nullptr;
  PFN_vkCmdCopyImageToBuffer copy_image_to_buffer = nullptr;
};

bool Load(VulkanDeviceContext& context, Dispatch& d) noexcept {
  const auto get = [&](const char* name) { return context.ResolveDeviceFunction(name); };
  d.create_image = reinterpret_cast<PFN_vkCreateImage>(get("vkCreateImage"));
  d.destroy_image = reinterpret_cast<PFN_vkDestroyImage>(get("vkDestroyImage"));
  d.image_requirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(get("vkGetImageMemoryRequirements"));
  d.bind_image = reinterpret_cast<PFN_vkBindImageMemory>(get("vkBindImageMemory"));
  d.create_view = reinterpret_cast<PFN_vkCreateImageView>(get("vkCreateImageView"));
  d.destroy_view = reinterpret_cast<PFN_vkDestroyImageView>(get("vkDestroyImageView"));
  d.create_buffer = reinterpret_cast<PFN_vkCreateBuffer>(get("vkCreateBuffer"));
  d.destroy_buffer = reinterpret_cast<PFN_vkDestroyBuffer>(get("vkDestroyBuffer"));
  d.buffer_requirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(get("vkGetBufferMemoryRequirements"));
  d.allocate = reinterpret_cast<PFN_vkAllocateMemory>(get("vkAllocateMemory"));
  d.free = reinterpret_cast<PFN_vkFreeMemory>(get("vkFreeMemory"));
  d.bind_buffer = reinterpret_cast<PFN_vkBindBufferMemory>(get("vkBindBufferMemory"));
  d.map = reinterpret_cast<PFN_vkMapMemory>(get("vkMapMemory"));
  d.unmap = reinterpret_cast<PFN_vkUnmapMemory>(get("vkUnmapMemory"));
  d.flush = reinterpret_cast<PFN_vkFlushMappedMemoryRanges>(get("vkFlushMappedMemoryRanges"));
  d.invalidate = reinterpret_cast<PFN_vkInvalidateMappedMemoryRanges>(get("vkInvalidateMappedMemoryRanges"));
  d.pipeline_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(get("vkCmdPipelineBarrier"));
  d.copy_buffer_to_image = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(get("vkCmdCopyBufferToImage"));
  d.copy_image_to_buffer = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(get("vkCmdCopyImageToBuffer"));
  return d.create_image && d.destroy_image && d.image_requirements && d.bind_image &&
         d.create_view && d.destroy_view && d.create_buffer && d.destroy_buffer &&
         d.buffer_requirements && d.allocate && d.free && d.bind_buffer && d.map &&
         d.unmap && d.flush;
}

bool LoadCommands(VulkanDeviceContext& context, Dispatch& d) noexcept {
  return Load(context, d) && d.pipeline_barrier && d.copy_buffer_to_image &&
         d.copy_image_to_buffer;
}

bool LoadInvalidate(VulkanDeviceContext& context, Dispatch& d) noexcept {
  return Load(context, d) && d.invalidate;
}

void Fail(VulkanGuestImagePreparation& p, VulkanGuestImageStatus status,
          const char* message) {
  p.status = status;
  p.diagnostics.push_back({status, message});
}

bool Align(VkDeviceSize value, VkDeviceSize alignment, VkDeviceSize& out) {
  if (alignment == 0 || value > std::numeric_limits<VkDeviceSize>::max() - (alignment - 1))
    return false;
  out = ((value + alignment - 1) / alignment) * alignment;
  return true;
}

std::optional<std::uint32_t> MemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                                        std::uint32_t bits, VkMemoryPropertyFlags required,
                                        bool prefer_device_local = false) {
  std::optional<std::uint32_t> fallback;
  for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) == 0 ||
        (properties.memoryTypes[i].propertyFlags & required) != required)
      continue;
    if (!fallback) fallback = i;
    if (prefer_device_local &&
        (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return i;
  }
  return fallback;
}

VkImageType ImageType(Prospero::ImageType type) {
  if (type == Prospero::ImageType::kColor1D || type == Prospero::ImageType::kColor1DArray)
    return VK_IMAGE_TYPE_1D;
  return type == Prospero::ImageType::kColor3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
}

VkImageViewType ImageViewType(Prospero::ImageType type, std::uint32_t layers) {
  switch (type) {
    case Prospero::ImageType::kColor1D: return VK_IMAGE_VIEW_TYPE_1D;
    case Prospero::ImageType::kColor1DArray: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case Prospero::ImageType::kColor3D: return VK_IMAGE_VIEW_TYPE_3D;
    case Prospero::ImageType::kCube:
      return layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    default: return layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  }
}

void Destroy(VulkanDeviceContext& context, const Dispatch& d,
             VulkanGuestImagePreparation& p) noexcept {
  if (p.staging_mapped && p.staging_memory) d.unmap(context.device(), p.staging_memory);
  p.staging_mapped = nullptr;
  if (p.sibling_view) d.destroy_view(context.device(), p.sibling_view, nullptr);
  if (p.view) d.destroy_view(context.device(), p.view, nullptr);
  if (p.staging_buffer) d.destroy_buffer(context.device(), p.staging_buffer, nullptr);
  if (p.image) d.destroy_image(context.device(), p.image, nullptr);
  if (p.staging_memory) d.free(context.device(), p.staging_memory, nullptr);
  if (p.image_memory) d.free(context.device(), p.image_memory, nullptr);
  p.sibling_view = p.view = VK_NULL_HANDLE;
  p.staging_buffer = VK_NULL_HANDLE;
  p.image = VK_NULL_HANDLE;
  p.staging_memory = p.image_memory = VK_NULL_HANDLE;
}

}  // namespace

std::optional<VulkanImageFormat> MapGuestImageFormat(std::uint32_t format) noexcept {
  using F = Prospero::BufferFormat;
  const auto f = [&](VkFormat vk, VulkanImageStorageClass storage,
                     std::optional<VkFormat> sibling = {}) {
    return VulkanImageFormat{vk, storage, sibling};
  };
  switch (static_cast<F>(format)) {
    case F::k8UNorm: return f(VK_FORMAT_R8_UNORM, VulkanImageStorageClass::kR8, VK_FORMAT_R8_SRGB);
    case F::k8Srgb: return f(VK_FORMAT_R8_SRGB, VulkanImageStorageClass::kR8, VK_FORMAT_R8_UNORM);
    case F::k8UInt: return f(VK_FORMAT_R8_UINT, VulkanImageStorageClass::kR8);
    case F::k8_8UNorm: return f(VK_FORMAT_R8G8_UNORM, VulkanImageStorageClass::kR8G8, VK_FORMAT_R8G8_SRGB);
    case F::k8_8Srgb: return f(VK_FORMAT_R8G8_SRGB, VulkanImageStorageClass::kR8G8, VK_FORMAT_R8G8_UNORM);
    case F::k8_8UInt: return f(VK_FORMAT_R8G8_UINT, VulkanImageStorageClass::kR8G8);
    case F::k8_8_8_8UNorm: return f(VK_FORMAT_R8G8B8A8_UNORM, VulkanImageStorageClass::kR8G8B8A8, VK_FORMAT_R8G8B8A8_SRGB);
    case F::k8_8_8_8Srgb: return f(VK_FORMAT_R8G8B8A8_SRGB, VulkanImageStorageClass::kR8G8B8A8, VK_FORMAT_R8G8B8A8_UNORM);
    case F::k8_8_8_8UInt: return f(VK_FORMAT_R8G8B8A8_UINT, VulkanImageStorageClass::kR8G8B8A8);
    case F::k16UNorm: return f(VK_FORMAT_R16_UNORM, VulkanImageStorageClass::kR16);
    case F::k16Float: return f(VK_FORMAT_R16_SFLOAT, VulkanImageStorageClass::kR16);
    case F::k16UInt: return f(VK_FORMAT_R16_UINT, VulkanImageStorageClass::kR16);
    case F::k16_16UNorm: return f(VK_FORMAT_R16G16_UNORM, VulkanImageStorageClass::kR16G16);
    case F::k16_16Float: return f(VK_FORMAT_R16G16_SFLOAT, VulkanImageStorageClass::kR16G16);
    case F::k16_16UInt: return f(VK_FORMAT_R16G16_UINT, VulkanImageStorageClass::kR16G16);
    case F::k16_16_16_16UNorm: return f(VK_FORMAT_R16G16B16A16_UNORM, VulkanImageStorageClass::kR16G16B16A16);
    case F::k16_16_16_16Float: return f(VK_FORMAT_R16G16B16A16_SFLOAT, VulkanImageStorageClass::kR16G16B16A16);
    case F::k16_16_16_16UInt: return f(VK_FORMAT_R16G16B16A16_UINT, VulkanImageStorageClass::kR16G16B16A16);
    case F::k32Float: return f(VK_FORMAT_R32_SFLOAT, VulkanImageStorageClass::kR32);
    case F::k32UInt: return f(VK_FORMAT_R32_UINT, VulkanImageStorageClass::kR32);
    case F::k32_32Float: return f(VK_FORMAT_R32G32_SFLOAT, VulkanImageStorageClass::kR32G32);
    case F::k32_32UInt: return f(VK_FORMAT_R32G32_UINT, VulkanImageStorageClass::kR32G32);
    case F::k32_32_32_32Float: return f(VK_FORMAT_R32G32B32A32_SFLOAT, VulkanImageStorageClass::kR32G32B32A32);
    case F::k32_32_32_32UInt: return f(VK_FORMAT_R32G32B32A32_UINT, VulkanImageStorageClass::kR32G32B32A32);
    case F::kBc1UNorm: return f(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VulkanImageStorageClass::kBc1, VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
    case F::kBc1Srgb: return f(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VulkanImageStorageClass::kBc1, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    case F::kBc3UNorm: return f(VK_FORMAT_BC3_UNORM_BLOCK, VulkanImageStorageClass::kBc3, VK_FORMAT_BC3_SRGB_BLOCK);
    case F::kBc3Srgb: return f(VK_FORMAT_BC3_SRGB_BLOCK, VulkanImageStorageClass::kBc3, VK_FORMAT_BC3_UNORM_BLOCK);
    case F::kBc4UNorm: return f(VK_FORMAT_BC4_UNORM_BLOCK, VulkanImageStorageClass::kBc4);
    case F::kBc4SNorm: return f(VK_FORMAT_BC4_SNORM_BLOCK, VulkanImageStorageClass::kBc4);
    case F::kBc5UNorm: return f(VK_FORMAT_BC5_UNORM_BLOCK, VulkanImageStorageClass::kBc5);
    case F::kBc5SNorm: return f(VK_FORMAT_BC5_SNORM_BLOCK, VulkanImageStorageClass::kBc5);
    case F::kBc6UFloat: return f(VK_FORMAT_BC6H_UFLOAT_BLOCK, VulkanImageStorageClass::kBc6);
    case F::kBc6SFloat: return f(VK_FORMAT_BC6H_SFLOAT_BLOCK, VulkanImageStorageClass::kBc6);
    case F::kBc7UNorm: return f(VK_FORMAT_BC7_UNORM_BLOCK, VulkanImageStorageClass::kBc7, VK_FORMAT_BC7_SRGB_BLOCK);
    case F::kBc7Srgb: return f(VK_FORMAT_BC7_SRGB_BLOCK, VulkanImageStorageClass::kBc7, VK_FORMAT_BC7_UNORM_BLOCK);
    default: return std::nullopt;
  }
}

VulkanGuestImageCache::VulkanGuestImageCache(VulkanDeviceContext& context,
                                               memory::GuestMemory& memory,
                                               GpuResourceCoherence& coherence) noexcept
    : context_(context), memory_(memory), coherence_(coherence) {}

VulkanGuestImageCache::~VulkanGuestImageCache() {
  for (std::size_t index = 0; index < lost_dirty_count_; ++index)
    (void)coherence_.UnregisterResource(lost_dirty_resources_[index]);
}

VulkanGuestImagePreparation VulkanGuestImageCache::Prepare(const VulkanGuestImageRequest& request) {
  VulkanGuestImagePreparation p;
  p.layout = CalculateGuestImageLayout(request.input);
  if (!p.layout.ok()) { Fail(p, VulkanGuestImageStatus::kInvalidLayout, p.layout.diagnostic.data()); return p; }
  const auto format = MapGuestImageFormat(request.input.format);
  if (!format) { Fail(p, VulkanGuestImageStatus::kUnsupportedFormat, "guest image view format is unsupported by Vulkan"); return p; }
  if (request.samples != VK_SAMPLE_COUNT_1_BIT || request.usage == 0) { Fail(p, VulkanGuestImageStatus::kUnsupportedTopology, "image samples or usage are unsupported"); return p; }
  if (request.request_sibling_view && !format->sibling_format) { Fail(p, VulkanGuestImageStatus::kUnsupportedFormat, "guest image has no compatible sRGB or UNORM sibling view"); return p; }
  for (std::uint32_t mip = 0; mip < request.input.mip_count; ++mip) {
    const auto& layout = p.layout.mips[mip];
    if (layout.row_bytes == 0 || layout.slice_bytes % layout.row_bytes != 0) {
      Fail(p, VulkanGuestImageStatus::kInvalidLayout,
           "guest image slice pitch cannot form a Vulkan copy region");
      return p;
    }
  }
  p.format = *format; p.writable = request.writable;
  const auto read = memory::GuestMemoryProtection::kRead | memory::GuestMemoryProtection::kGpuRead;
  const auto write = memory::GuestMemoryProtection::kWrite | memory::GuestMemoryProtection::kGpuWrite;
  if (!memory_.CanAccess(request.input.guest_address, p.layout.total_bytes, read) ||
      (request.writable && !memory_.CanAccess(request.input.guest_address, p.layout.total_bytes, write))) {
    Fail(p, VulkanGuestImageStatus::kGuestMemoryProtection, "guest image is not mapped with required GPU access"); return p;
  }
  const auto resource = coherence_.RegisterResource(request.input.guest_address, p.layout.total_bytes);
  if (!resource) { Fail(p, VulkanGuestImageStatus::kGuestMemoryFault, "guest image could not be registered with resource coherence"); return p; }
  p.resource = *resource;
  std::vector<std::byte> upload(static_cast<size_t>(p.layout.total_bytes));
  if (!memory_.Read(request.input.guest_address, upload) || !context_.memory_properties()) {
    Discard(p); Fail(p, VulkanGuestImageStatus::kGuestMemoryFault, "checked guest image upload or memory properties failed"); return p;
  }
  Dispatch d;
  if (!Load(context_, d)) { Discard(p); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "Vulkan image resource functions are unavailable"); return p; }
  const auto cleanup = [&] { Destroy(context_, d, p); if (p.resource) { (void)coherence_.UnregisterResource(p.resource); p.resource = 0; } };
  VkFormat formats[2] = {format->format, format->sibling_format.value_or(VK_FORMAT_UNDEFINED)};
  VkImageFormatListCreateInfo format_list{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
  format_list.viewFormatCount = request.request_sibling_view ? 2 : 0; format_list.pViewFormats = formats;
  VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_info.pNext = request.request_sibling_view ? &format_list : nullptr;
  image_info.flags = request.request_sibling_view ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
  image_info.imageType = ImageType(request.input.image_type); image_info.format = format->format;
  image_info.extent = {request.input.width, request.input.height, request.input.depth};
  image_info.mipLevels = request.input.mip_count;
  image_info.arrayLayers = request.input.image_type == Prospero::ImageType::kColor3D ? 1 : p.layout.array_layers;
  image_info.samples = request.samples; image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = request.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      (request.writable ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image = VK_NULL_HANDLE;
  if (d.create_image(context_.device(), &image_info, nullptr, &image) != VK_SUCCESS) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "vkCreateImage failed"); return p; }
  p.image = image;
  VkMemoryRequirements requirements{}; d.image_requirements(context_.device(), p.image, &requirements);
  const auto image_type = MemoryType(*context_.memory_properties(), requirements.memoryTypeBits, 0, true);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  if (!image_type) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "no Vulkan memory type can back image"); return p; }
  allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = *image_type;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  if (d.allocate(context_.device(), &allocation, nullptr, &image_memory) != VK_SUCCESS) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "vkAllocateMemory failed"); return p; }
  p.image_memory = image_memory;
  if (d.bind_image(context_.device(), p.image, p.image_memory, 0) != VK_SUCCESS) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "vkBindImageMemory failed"); return p; }
  const auto make_view = [&](VkFormat view_format, VkImageView* out) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; info.image = p.image;
    info.viewType = ImageViewType(request.input.image_type, p.layout.array_layers); info.format = view_format;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; info.subresourceRange.levelCount = request.input.mip_count;
    info.subresourceRange.layerCount = image_info.arrayLayers; return d.create_view(context_.device(), &info, nullptr, out);
  };
  VkImageView primary_view = VK_NULL_HANDLE;
  if (make_view(format->format, &primary_view) != VK_SUCCESS) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "vkCreateImageView failed"); return p; }
  p.view = primary_view;
  if (request.request_sibling_view) {
    VkImageView sibling_view = VK_NULL_HANDLE;
    if (make_view(*format->sibling_format, &sibling_view) != VK_SUCCESS) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "vkCreateImageView failed"); return p; }
    p.sibling_view = sibling_view;
  }
  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; buffer_info.size = p.layout.total_bytes;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                      (p.writable ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkBuffer staging_buffer = VK_NULL_HANDLE;
  if (d.create_buffer(context_.device(), &buffer_info, nullptr, &staging_buffer) != VK_SUCCESS) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "vkCreateBuffer failed"); return p; }
  p.staging_buffer = staging_buffer;
  d.buffer_requirements(context_.device(), p.staging_buffer, &requirements);
  const auto staging_type = MemoryType(*context_.memory_properties(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  if (!staging_type || !Align(std::max<VkDeviceSize>(requirements.size, p.layout.total_bytes), context_.properties().non_coherent_atom_size, p.staging_allocation_size)) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "no aligned host-visible staging memory is available"); return p; }
  p.staging_host_coherent = ((*context_.memory_properties()).memoryTypes[*staging_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
  allocation.allocationSize = p.staging_allocation_size; allocation.memoryTypeIndex = *staging_type;
  const auto destroy_staging_buffer = [&] {
    if (p.staging_buffer) {
      d.destroy_buffer(context_.device(), p.staging_buffer, nullptr);
      p.staging_buffer = VK_NULL_HANDLE;
    }
  };
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  if (d.allocate(context_.device(), &allocation, nullptr, &staging_memory) != VK_SUCCESS) {
    destroy_staging_buffer(); cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "staging allocation failed"); return p;
  }
  p.staging_memory = staging_memory;
  if (d.bind_buffer(context_.device(), p.staging_buffer, p.staging_memory, 0) != VK_SUCCESS) {
    destroy_staging_buffer(); cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "staging binding failed"); return p;
  }
  void* staging_mapped = nullptr;
  if (d.map(context_.device(), p.staging_memory, 0, p.staging_allocation_size, 0, &staging_mapped) != VK_SUCCESS) {
    destroy_staging_buffer(); cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "staging mapping failed"); return p;
  }
  p.staging_mapped = staging_mapped;
  std::memcpy(p.staging_mapped, upload.data(), upload.size());
  p.uploaded_bytes = std::move(upload);
  if (!p.staging_host_coherent) { VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = p.staging_memory; range.size = p.staging_allocation_size; if (d.flush(context_.device(), 1, &range) != VK_SUCCESS) { cleanup(); Fail(p, VulkanGuestImageStatus::kDeviceResourceFailure, "vkFlushMappedMemoryRanges failed"); return p; } }
  const std::uint32_t block_bytes = Prospero::BlockCompressedBytesPerBlock(request.input.format);
  const std::uint32_t texel_bytes = Prospero::NumBytesPerElement(request.input.format);
  for (std::uint32_t mip = 0; mip < request.input.mip_count; ++mip) {
    const auto& layout = p.layout.mips[mip];
    const std::uint64_t unit_bytes = block_bytes != 0 ? block_bytes : texel_bytes;
    const std::uint64_t tight_row = static_cast<std::uint64_t>(layout.block_width) * unit_bytes;
    const std::uint64_t tight_slice = layout.row_bytes * layout.block_height;
    VkBufferImageCopy copy{};
    copy.bufferOffset = layout.byte_offset;
    // Zero denotes tightly packed data. Explicit pitch is represented in
    // texels (or 4x4 blocks converted to texels), as Vulkan requires.
    if (layout.row_bytes != tight_row) {
      copy.bufferRowLength = static_cast<std::uint32_t>(layout.row_bytes / unit_bytes) *
                             (block_bytes != 0 ? 4u : 1u);
    }
    if (layout.slice_bytes != tight_slice) {
      copy.bufferImageHeight = static_cast<std::uint32_t>(layout.slice_bytes / layout.row_bytes) *
                               (block_bytes != 0 ? 4u : 1u);
    }
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = mip;
    copy.imageSubresource.layerCount = image_info.arrayLayers;
    copy.imageExtent = {layout.width, layout.height, layout.depth};
    p.copy_regions.push_back(copy);
  }
  const auto state = coherence_.Query(p.resource);
  if (!state || !coherence_.AcknowledgeCpuUpload(p.resource, state->cpu_write_generation)) { cleanup(); Fail(p, VulkanGuestImageStatus::kGuestMemoryFault, "guest image upload generation could not be acknowledged"); return p; }
  p.status = VulkanGuestImageStatus::kOk; return p;
}

bool VulkanGuestImageCache::RecordUpload(
    VkCommandBuffer command_buffer, VulkanGuestImagePreparation& p,
    VkImageLayout shader_layout, VkPipelineStageFlags shader_stage,
    VkAccessFlags shader_access) noexcept {
  Dispatch d;
  if (command_buffer == VK_NULL_HANDLE || !p || p.upload_recorded ||
      p.image == VK_NULL_HANDLE || p.staging_buffer == VK_NULL_HANDLE ||
      p.current_layout != VK_IMAGE_LAYOUT_UNDEFINED || p.copy_regions.empty() ||
      shader_layout == VK_IMAGE_LAYOUT_UNDEFINED || shader_stage == 0 ||
      !LoadCommands(context_, d)) return false;
  VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.image = p.image;
  to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  to_transfer.subresourceRange.levelCount =
      static_cast<std::uint32_t>(p.copy_regions.size());
  to_transfer.subresourceRange.layerCount = p.layout.array_layers == 0 ? 1 : p.layout.array_layers;
  VkImageMemoryBarrier to_shader = to_transfer;
  to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_shader.newLayout = shader_layout;
  to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_shader.dstAccessMask = shader_access;
  to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  d.pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                     1, &to_transfer);
  d.copy_buffer_to_image(command_buffer, p.staging_buffer, p.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         static_cast<std::uint32_t>(p.copy_regions.size()),
                         p.copy_regions.data());
  d.pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, shader_stage,
                     0, 0, nullptr, 0, nullptr, 1, &to_shader);
  p.current_layout = shader_layout;
  p.upload_recorded = true;
  return true;
}

bool VulkanGuestImageCache::RecordReadback(
    VkCommandBuffer command_buffer, VulkanGuestImagePreparation& p,
    VkPipelineStageFlags source_stage, VkAccessFlags source_access) noexcept {
  Dispatch d;
  if (command_buffer == VK_NULL_HANDLE || !p || !p.writable || !p.upload_recorded ||
      p.readback_recorded || p.current_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
      p.image == VK_NULL_HANDLE || p.staging_buffer == VK_NULL_HANDLE ||
      p.copy_regions.empty() || source_stage == 0 || !LoadCommands(context_, d)) return false;
  VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  to_transfer.srcAccessMask = source_access;
  to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.oldLayout = p.current_layout;
  to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  to_transfer.image = p.image;
  to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  to_transfer.subresourceRange.levelCount = static_cast<std::uint32_t>(p.copy_regions.size());
  to_transfer.subresourceRange.layerCount = p.layout.array_layers == 0 ? 1 : p.layout.array_layers;
  VkBufferMemoryBarrier host_visible{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  host_visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  host_visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  host_visible.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host_visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host_visible.buffer = p.staging_buffer;
  host_visible.size = p.layout.total_bytes;
  d.pipeline_barrier(command_buffer, source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     0, 0, nullptr, 0, nullptr, 1, &to_transfer);
  d.copy_image_to_buffer(command_buffer, p.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         p.staging_buffer, static_cast<std::uint32_t>(p.copy_regions.size()),
                         p.copy_regions.data());
  d.pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host_visible,
                     0, nullptr);
  p.current_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  p.readback_recorded = true;
  return true;
}

bool VulkanGuestImageCache::MarkSubmitted(VulkanGuestImagePreparation& p) noexcept {
  if (!p || !p.writable || !p.readback_recorded || p.gpu_dirty || p.resource == 0 ||
      !coherence_.MarkGpuWrite(p.resource)) return false;
  p.gpu_dirty = true;
  return true;
}

bool VulkanGuestImageCache::Complete(VulkanGuestImagePreparation& p) noexcept {
  try {
    const auto state = p.resource ? coherence_.Query(p.resource) : std::nullopt;
    if (!p || !p.gpu_dirty || !p.readback_recorded || !state || !state->mapped ||
        state->mapping_changed || !state->gpu_write_pending ||
        p.staging_mapped == nullptr ||
        p.uploaded_bytes.size() != p.layout.total_bytes) return false;
    if (!p.staging_host_coherent) {
      Dispatch d;
      if (!LoadInvalidate(context_, d)) return false;
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = p.staging_memory;
      range.size = p.staging_allocation_size;
      if (d.invalidate(context_.device(), 1, &range) != VK_SUCCESS) return false;
    }
    const std::byte* gpu = static_cast<const std::byte*>(p.staging_mapped);
    std::vector<std::byte> current(p.uploaded_bytes.size());
    if (!memory_.Read(p.layout.storage_key.guest_address, current)) return false;
    std::size_t offset = 0;
    while (offset < current.size()) {
      if (gpu[offset] == p.uploaded_bytes[offset]) { ++offset; continue; }
      const std::size_t start = offset;
      do { ++offset; } while (offset < current.size() &&
                              gpu[offset] != p.uploaded_bytes[offset]);
      std::memcpy(current.data() + start, gpu + start, offset - start);
      if (!memory_.Write(p.layout.storage_key.guest_address + start,
                         std::span<const std::byte>(current).subspan(
                             start, offset - start))) return false;
    }
    if (!coherence_.InvalidateGpuWrite(p.resource)) return false;
    p.gpu_dirty = false;
    return true;
  } catch (...) {
    // Completion remains retryable: no dirty/coherence state is cleared until
    // every checked GuestMemory write and the coherence invalidation succeed.
    return false;
  }
}

void VulkanGuestImageCache::Discard(VulkanGuestImagePreparation& p) noexcept {
  const bool lost = context_.is_device_lost();
  Dispatch d; if (Load(context_, d)) Destroy(context_, d, p);
  if (p.resource && lost && p.gpu_dirty) {
    if (lost_dirty_count_ == lost_dirty_resources_.size()) {
      // This is impossible under the executor's 8 * 32 in-flight image
      // bound. Preserve the pending record in the preparation rather than
      // evicting it or fabricating a GuestMemory completion.
      return;
    }
    lost_dirty_resources_[lost_dirty_count_++] = p.resource;
    p.resource = 0;
    p.gpu_dirty = false;
  } else if (p.resource) { (void)coherence_.UnregisterResource(p.resource); p.resource = 0; }
  p.copy_regions.clear();
}

const char* VulkanGuestImageStatusName(VulkanGuestImageStatus status) noexcept {
  switch (status) { case VulkanGuestImageStatus::kOk: return "ok"; case VulkanGuestImageStatus::kInvalidLayout: return "invalid_layout"; case VulkanGuestImageStatus::kUnsupportedFormat: return "unsupported_format"; case VulkanGuestImageStatus::kUnsupportedTopology: return "unsupported_topology"; case VulkanGuestImageStatus::kGuestMemoryProtection: return "guest_memory_protection"; case VulkanGuestImageStatus::kGuestMemoryFault: return "guest_memory_fault"; case VulkanGuestImageStatus::kDeviceResourceFailure: return "device_resource_failure"; case VulkanGuestImageStatus::kResourceLimit: return "resource_limit"; } return "unknown";
}
}  // namespace kajps5::gpu::vulkan
