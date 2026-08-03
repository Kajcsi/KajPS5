// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 image/cache model at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior reference: SharpEmu guest-image sizing and alias tests at 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/image_cache.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>
#include <unordered_map>

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
  PFN_vkCreateSampler create_sampler = nullptr;
  PFN_vkDestroySampler destroy_sampler = nullptr;
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
  d.create_sampler = reinterpret_cast<PFN_vkCreateSampler>(get("vkCreateSampler"));
  d.destroy_sampler = reinterpret_cast<PFN_vkDestroySampler>(get("vkDestroySampler"));
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

bool LoadSamplers(VulkanDeviceContext& context, Dispatch& d) noexcept {
  return Load(context, d) && d.create_sampler && d.destroy_sampler;
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

void FailSet(VulkanGuestImageSetPreparation& p, VulkanGuestImageSetStatus status,
             const char* message) {
  p.status = status;
  p.diagnostics.push_back({status, message});
}

bool IsImageBinding(shader::recompiler::IR::DescriptorBindingKind kind) {
  using K = shader::recompiler::IR::DescriptorBindingKind;
  return kind >= K::Sampled1D && kind <= K::StorageUint3D;
}

bool IsStorageBinding(shader::recompiler::IR::DescriptorBindingKind kind) {
  using K = shader::recompiler::IR::DescriptorBindingKind;
  return kind >= K::Storage1D && kind <= K::StorageUint3D;
}

bool BindingMatchesImage(shader::recompiler::IR::DescriptorBindingKind binding,
                         const shader::recompiler::IR::ImageResource& image) {
  using K = shader::recompiler::IR::DescriptorBindingKind;
  using D = shader::recompiler::Decoder::ImageDimension;
  const bool uint_binding = binding == K::SampledUint1D ||
      binding == K::SampledUint1DArray || binding == K::SampledUint2D ||
      binding == K::SampledUint2DArray || binding == K::SampledUint3D ||
      binding == K::StorageUint1D || binding == K::StorageUint1DArray ||
      binding == K::StorageUint2D || binding == K::StorageUint2DArray ||
      binding == K::StorageUint3D;
  const bool image_uint = image.kind == shader::recompiler::IR::ResourceKind::ImageUint ||
      image.kind == shader::recompiler::IR::ResourceKind::StorageImageUint;
  const bool image_numeric = image.kind == shader::recompiler::IR::ResourceKind::Image ||
      image.kind == shader::recompiler::IR::ResourceKind::ImageUint ||
      image.kind == shader::recompiler::IR::ResourceKind::StorageImage ||
      image.kind == shader::recompiler::IR::ResourceKind::StorageImageUint;
  // One dense descriptor can be consumed by sampled and storage bindings.
  // The usage class is therefore validated after unioning; numeric class and
  // dimension must still agree with every individual binding.
  if (!image_numeric || image_uint != uint_binding) return false;
  D dimension = D::Unknown;
  switch (binding) {
    case K::Sampled1D: case K::SampledUint1D: case K::Storage1D: case K::StorageUint1D: dimension = D::Dim1D; break;
    case K::Sampled1DArray: case K::SampledUint1DArray: case K::Storage1DArray: case K::StorageUint1DArray: dimension = D::Dim1DArray; break;
    case K::Sampled2D: case K::SampledUint2D: case K::Storage2D: case K::StorageUint2D: dimension = D::Dim2D; break;
    case K::Sampled2DArray: case K::SampledUint2DArray: case K::Storage2DArray: case K::StorageUint2DArray: dimension = D::Dim2DArray; break;
    case K::Sampled3D: case K::SampledUint3D: case K::Storage3D: case K::StorageUint3D: dimension = D::Dim3D; break;
    default: return false;
  }
  return image.dimension == dimension;
}

bool DecodeImageRequest(const shader::recompiler::IR::DescriptorValue& value,
                        const shader::recompiler::IR::ImageResource& image,
                        VulkanGuestImageRequest& request) {
  if (value.dword_count != 8) return false;
  const ShaderTextureResource descriptor{.fields = {
      value.dwords[0], value.dwords[1], value.dwords[2], value.dwords[3],
      value.dwords[4], value.dwords[5], value.dwords[6], value.dwords[7]}};
  if (descriptor.IsNull() || descriptor.TileMode() !=
          static_cast<std::uint8_t>(Prospero::TileMode::kLinear) ||
      descriptor.LastLevel() < descriptor.BaseLevel() ||
      descriptor.LastLevel() > descriptor.MaxMip()) return false;
  const auto type = static_cast<Prospero::ImageType>(descriptor.Type());
  if (type == Prospero::ImageType::kColor2DMsaa ||
      type == Prospero::ImageType::kColor2DMsaaArray) return false;
  request = {};
  request.input.guest_address = descriptor.Base40();
  request.input.format = descriptor.Format();
  request.input.width = static_cast<std::uint32_t>(descriptor.Width5()) + 1;
  request.input.height = static_cast<std::uint32_t>(descriptor.Height5()) + 1;
  request.input.depth = 1;
  request.input.layers = 1;
  request.input.mip_count = static_cast<std::uint32_t>(descriptor.MaxMip()) + 1;
  request.input.tile_mode = Prospero::TileMode::kLinear;
  request.view_base_mip_level = descriptor.BaseLevel();
  request.view_level_count = descriptor.LastLevel() - descriptor.BaseLevel() + 1;
  request.view_base_array_layer = descriptor.BaseArray5();
  const std::uint32_t descriptor_layers =
      static_cast<std::uint32_t>(descriptor.Depth()) + 1;
  switch (type) {
    case Prospero::ImageType::kColor1D:
      request.input.image_type = type;
      request.input.height = 1;
      request.view_type = VK_IMAGE_VIEW_TYPE_1D;
      break;
    case Prospero::ImageType::kColor1DArray:
      request.input.image_type = type;
      request.input.height = 1;
      request.input.layers = request.view_base_array_layer + descriptor_layers;
      request.view_layer_count = descriptor_layers;
      request.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
      break;
    case Prospero::ImageType::kColor2D:
      request.input.image_type = type;
      request.view_type = VK_IMAGE_VIEW_TYPE_2D;
      break;
    case Prospero::ImageType::kColor2DArray:
      request.input.image_type = type;
      request.input.layers = request.view_base_array_layer + descriptor_layers;
      request.view_layer_count = descriptor_layers;
      request.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      break;
    case Prospero::ImageType::kColor3D:
      request.input.image_type = type;
      request.input.depth = descriptor_layers;
      request.view_base_array_layer = 0;
      request.view_layer_count = 1;
      request.view_type = VK_IMAGE_VIEW_TYPE_3D;
      break;
    case Prospero::ImageType::kCube:
      if (request.input.width != request.input.height || descriptor_layers == 0 ||
          descriptor_layers % 6 != 0) return false;
      // Native cube descriptors specify faces. The storage is a 2D array;
      // GuestImageLayoutInput's public cube-count convention is not used.
      request.input.image_type = Prospero::ImageType::kColor2DArray;
      request.input.layers = request.view_base_array_layer + descriptor_layers;
      request.view_layer_count = descriptor_layers;
      request.view_type = descriptor_layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE :
                                                   VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
      break;
    default: return false;
  }
  if (image.cube != (type == Prospero::ImageType::kCube)) return false;
  return true;
}

bool MapAddressMode(std::uint8_t value, VkSamplerAddressMode& out) {
  switch (static_cast<Prospero::SamplerClampMode>(value)) {
    case Prospero::SamplerClampMode::kWrap: out = VK_SAMPLER_ADDRESS_MODE_REPEAT; return true;
    case Prospero::SamplerClampMode::kMirror: out = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; return true;
    case Prospero::SamplerClampMode::kClampLastTexel: out = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; return true;
    case Prospero::SamplerClampMode::kMirrorOnceLastTexel: out = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; return true;
    case Prospero::SamplerClampMode::kClampHalfBorder:
    case Prospero::SamplerClampMode::kClampBorder: out = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; return true;
    case Prospero::SamplerClampMode::kMirrorOnceHalfBorder:
    case Prospero::SamplerClampMode::kMirrorOnceBorder: out = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; return true;
    default: return false;
  }
}

bool MapFilter(std::uint8_t value, VkFilter& out, bool& anisotropic) {
  switch (static_cast<Prospero::SamplerFilter>(value)) {
    case Prospero::SamplerFilter::kPoint: out = VK_FILTER_NEAREST; return true;
    case Prospero::SamplerFilter::kBilinear: out = VK_FILTER_LINEAR; return true;
    case Prospero::SamplerFilter::kAnisoPoint: out = VK_FILTER_NEAREST; anisotropic = true; return true;
    case Prospero::SamplerFilter::kAnisoLinear: out = VK_FILTER_LINEAR; anisotropic = true; return true;
    default: return false;
  }
}

bool MapMipFilter(std::uint8_t value, VkSamplerMipmapMode& out) {
  switch (static_cast<Prospero::SamplerMipFilter>(value)) {
    case Prospero::SamplerMipFilter::kNone:
    case Prospero::SamplerMipFilter::kPoint: out = VK_SAMPLER_MIPMAP_MODE_NEAREST; return true;
    case Prospero::SamplerMipFilter::kLinear: out = VK_SAMPLER_MIPMAP_MODE_LINEAR; return true;
    default: return false;
  }
}

bool MapCompare(std::uint8_t value, VkCompareOp& out) {
  if (value > 7) return false;
  out = static_cast<VkCompareOp>(VK_COMPARE_OP_NEVER + value);
  return true;
}

bool UsesBorderColor(VkSamplerAddressMode mode) {
  return mode == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
}

bool SameSamplerCreateInfo(const VkSamplerCreateInfo& a,
                           const VkSamplerCreateInfo& b) {
  return a.flags == b.flags && a.magFilter == b.magFilter &&
      a.minFilter == b.minFilter && a.mipmapMode == b.mipmapMode &&
      a.addressModeU == b.addressModeU && a.addressModeV == b.addressModeV &&
      a.addressModeW == b.addressModeW && a.mipLodBias == b.mipLodBias &&
      a.anisotropyEnable == b.anisotropyEnable &&
      a.maxAnisotropy == b.maxAnisotropy &&
      a.compareEnable == b.compareEnable && a.compareOp == b.compareOp &&
      a.minLod == b.minLod && a.maxLod == b.maxLod &&
      a.borderColor == b.borderColor &&
      a.unnormalizedCoordinates == b.unnormalizedCoordinates;
}

bool CompatibleUnderlyingStorage(const VulkanGuestImageRequest& a,
                                 const GuestImageLayout& a_layout,
                                 const VulkanGuestImageRequest& b,
                                 const GuestImageLayout& b_layout) {
  const auto a_format = MapGuestImageFormat(a.input.format);
  const auto b_format = MapGuestImageFormat(b.input.format);
  return a_format && b_format &&
      ImageType(a.input.image_type) == ImageType(b.input.image_type) &&
      a.input.width == b.input.width && a.input.height == b.input.height &&
      a.input.depth == b.input.depth && a.input.mip_count == b.input.mip_count &&
      a_layout.array_layers == b_layout.array_layers && a.samples == b.samples &&
      a.input.tile_mode == b.input.tile_mode &&
      a_format->storage_class == b_format->storage_class;
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

VulkanGuestImagePreparation VulkanGuestImageCache::PrepareDepthStencil(
    const VulkanGuestDepthStencilRequest& request) {
  VulkanGuestImagePreparation preparation;
  if (request.depth_format != Prospero::DepthFormat::kZ32F ||
      request.stencil_format != Prospero::StencilFormat::kInvalid) {
    Fail(preparation, VulkanGuestImageStatus::kUnsupportedFormat,
         "only one-sample Z32 float depth without stencil is supported");
    return preparation;
  }
  if (request.samples != VK_SAMPLE_COUNT_1_BIT || request.width == 0 ||
      request.height == 0 || request.guest_address == 0 ||
      (request.row_pitch_bytes != 0 &&
       request.row_pitch_bytes < static_cast<std::uint64_t>(request.width) * 4)) {
    Fail(preparation, VulkanGuestImageStatus::kInvalidLayout,
         "depth target dimensions, pitch, samples, or address are invalid");
    return preparation;
  }
  GuestImageLayoutInput input{};
  input.guest_address = request.guest_address;
  // The layout calculator's R32 float storage has the same guest byte shape
  // as Z32 float. Vulkan image creation below overrides it to D32.
  input.format = static_cast<std::uint32_t>(Prospero::BufferFormat::k32Float);
  input.width = request.width;
  input.height = request.height;
  input.depth = 1;
  input.image_type = Prospero::ImageType::kColor2D;
  input.tile_mode = Prospero::TileMode::kLinear;
  input.tightly_packed = request.row_pitch_bytes == 0;
  input.row_pitch_bytes = request.row_pitch_bytes;
  if (request.row_pitch_bytes != 0) {
    if (request.row_pitch_bytes > std::numeric_limits<std::uint64_t>::max() /
            request.height) {
      Fail(preparation, VulkanGuestImageStatus::kInvalidLayout,
           "depth target slice pitch overflows");
      return preparation;
    }
    input.slice_pitch_bytes = request.row_pitch_bytes * request.height;
  }
  VulkanGuestImageRequest image_request{};
  image_request.input = input;
  image_request.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_request.samples = request.samples;
  image_request.writable = request.writable;
  image_request.format_override = VulkanImageFormat{
      VK_FORMAT_D32_SFLOAT, VulkanImageStorageClass::kD32, std::nullopt};
  image_request.aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
  return Prepare(image_request);
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
  const auto format = request.format_override.has_value()
      ? request.format_override : MapGuestImageFormat(request.input.format);
  if (!format) { Fail(p, VulkanGuestImageStatus::kUnsupportedFormat, "guest image view format is unsupported by Vulkan"); return p; }
  if (request.samples != VK_SAMPLE_COUNT_1_BIT || request.usage == 0 ||
      request.view_base_mip_level >= request.input.mip_count ||
      (request.view_level_count != 0 &&
       request.view_level_count > request.input.mip_count - request.view_base_mip_level)) {
    Fail(p, VulkanGuestImageStatus::kUnsupportedTopology,
         "image samples, usage, or mip view range are unsupported");
    return p;
  }
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
  p.aspect_mask = request.aspect_mask;
  if (p.aspect_mask == 0 || (p.aspect_mask & ~(
          VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT |
          VK_IMAGE_ASPECT_STENCIL_BIT)) != 0) {
    Fail(p, VulkanGuestImageStatus::kUnsupportedTopology,
         "guest image has an unsupported Vulkan aspect mask");
    return p;
  }
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
  const auto requested_view_type = request.view_type.value_or(
      ImageViewType(request.input.image_type, p.layout.array_layers));
  image_info.imageType = ImageType(request.input.image_type); image_info.format = format->format;
  image_info.extent = {request.input.width, request.input.height, request.input.depth};
  image_info.mipLevels = request.input.mip_count;
  image_info.arrayLayers = request.input.image_type == Prospero::ImageType::kColor3D ? 1 : p.layout.array_layers;
  const std::uint32_t full_layers = image_info.arrayLayers;
  const std::uint32_t view_layers = request.view_layer_count == 0 ?
      full_layers - request.view_base_array_layer : request.view_layer_count;
  if (request.view_base_array_layer >= full_layers || view_layers == 0 ||
      view_layers > full_layers - request.view_base_array_layer) {
    cleanup();
    Fail(p, VulkanGuestImageStatus::kUnsupportedTopology,
         "image array view range is outside storage");
    return p;
  }
  if (requested_view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
      requested_view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
    if (request.view_base_array_layer % 6 != 0 || view_layers % 6 != 0 ||
        (requested_view_type == VK_IMAGE_VIEW_TYPE_CUBE && view_layers != 6)) {
      cleanup();
      Fail(p, VulkanGuestImageStatus::kUnsupportedTopology,
           "cube view layers must be aligned to complete faces");
      return p;
    }
    image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  }
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
    info.viewType = requested_view_type;
    info.format = view_format;
    info.subresourceRange.aspectMask = p.aspect_mask;
    info.subresourceRange.baseMipLevel = request.view_base_mip_level;
    info.subresourceRange.levelCount = request.view_level_count == 0 ?
        request.input.mip_count - request.view_base_mip_level : request.view_level_count;
    info.subresourceRange.baseArrayLayer = request.view_base_array_layer;
    info.subresourceRange.layerCount = view_layers;
    return d.create_view(context_.device(), &info, nullptr, out);
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
    copy.imageSubresource.aspectMask = p.aspect_mask;
    copy.imageSubresource.mipLevel = mip;
    copy.imageSubresource.layerCount = image_info.arrayLayers;
    copy.imageExtent = {layout.width, layout.height, layout.depth};
    p.copy_regions.push_back(copy);
  }
  const auto state = coherence_.Query(p.resource);
  if (!state || !coherence_.AcknowledgeCpuUpload(p.resource, state->cpu_write_generation)) { cleanup(); Fail(p, VulkanGuestImageStatus::kGuestMemoryFault, "guest image upload generation could not be acknowledged"); return p; }
  p.status = VulkanGuestImageStatus::kOk; return p;
}

VulkanGuestImageSetPreparation VulkanGuestImageCache::PrepareTranslated(
    const shader::recompiler::CompileResult& result) {
  VulkanGuestImageSetPreparation set;
  try {
    std::string error;
    const auto& program = result.program;
    const auto& snapshot = result.resources;
    const bool supported_stage = program.stage == ShaderType::Compute ||
        program.stage == ShaderType::Vertex || program.stage == ShaderType::Pixel;
    const std::uint32_t expected_set = program.stage == ShaderType::Pixel ? 1U : 0U;
    if (!supported_stage || program.bindings.descriptor_set != expected_set ||
        !program.binding_layout_complete || !program.shader_info_complete ||
        !shader::recompiler::IR::ValidateResourceSpecialization(program, snapshot, &error)) {
      FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
              error.empty() ? "translated resource specialization is invalid" : error.c_str());
      return set;
    }
    if (program.info.images.size() > shader::recompiler::IR::ShaderInfo::MaxImages ||
        program.info.samplers.size() > shader::recompiler::IR::ShaderInfo::MaxSamplers) {
      FailSet(set, VulkanGuestImageSetStatus::kResourceLimit,
              "translated image or sampler count exceeds the fixed bound");
      return set;
    }
    std::size_t sampled_count = 0;
    std::size_t storage_count = 0;
    std::size_t sampler_count = 0;
    for (const auto& group : program.bindings.descriptors) {
      using K = shader::recompiler::IR::DescriptorBindingKind;
      if (group.kind == K::Samplers) sampler_count += group.resources.size();
      else if (IsImageBinding(group.kind)) {
        if (IsStorageBinding(group.kind)) storage_count += group.resources.size();
        else sampled_count += group.resources.size();
      }
    }
    const auto& limits = context_.properties();
    if (sampled_count > limits.max_per_stage_descriptor_sampled_images ||
        sampled_count > limits.max_descriptor_set_sampled_images ||
        storage_count > limits.max_per_stage_descriptor_storage_images ||
        storage_count > limits.max_descriptor_set_storage_images ||
        sampler_count > limits.max_per_stage_descriptor_samplers ||
        sampler_count > limits.max_descriptor_set_samplers) {
      FailSet(set, VulkanGuestImageSetStatus::kResourceLimit,
              "image or sampler descriptor count exceeds selected Vulkan device limits");
      return set;
    }
    std::vector<std::uint32_t> bindings;
    std::vector<std::uint32_t> image_preparation(program.info.images.size(), UINT32_MAX);
    std::vector<std::uint32_t> sampler_lease(program.info.samplers.size(), UINT32_MAX);
    std::vector<VkSamplerCreateInfo> sampler_infos;
    struct ImageRequirement {
      bool used = false;
      VulkanGuestImageRequest request;
      GuestImageLayout layout;
    };
    std::vector<ImageRequirement> requirements(program.info.images.size());
    std::vector<std::uint32_t> validated_bindings;
    // Gather every image access before creating a lease. A later storage use
    // must upgrade the single allocation rather than observing a sampled-only
    // image created by an earlier binding.
    for (const auto& group : program.bindings.descriptors) {
      if (std::find(validated_bindings.begin(), validated_bindings.end(),
                    group.binding) != validated_bindings.end()) {
        FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
                "translated descriptor binding numbers are not unique");
        return set;
      }
      validated_bindings.push_back(group.binding);
      using K = shader::recompiler::IR::DescriptorBindingKind;
      if (group.kind == K::Buffers || group.kind == K::Samplers) continue;
      if (!IsImageBinding(group.kind)) {
        FailSet(set, VulkanGuestImageSetStatus::kUnsupportedDescriptor,
                "translated descriptor group is outside image/sampler scope");
        return set;
      }
      const bool storage = IsStorageBinding(group.kind);
      for (const std::uint32_t dense : group.resources) {
        if (dense >= program.info.images.size() || dense >= snapshot.images.size()) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
                  "image descriptor has an invalid dense resource index");
          return set;
        }
        if (!BindingMatchesImage(group.kind, program.info.images[dense])) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
                  "image binding kind does not match specialized image resource");
          return set;
        }
        VulkanGuestImageRequest request;
        if (!DecodeImageRequest(snapshot.images[dense], program.info.images[dense], request)) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                  "image descriptor is null, tiled, multisampled, or malformed");
          return set;
        }
        const auto native_format = MapGuestImageFormat(request.input.format);
        if (!native_format) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                  "image descriptor format is unsupported by Vulkan");
          return set;
        }
        if (storage && Prospero::BlockCompressedBytesPerBlock(request.input.format) != 0) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                  "compressed Vulkan storage images are unsupported");
          return set;
        }
        request.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
            (storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0);
        request.writable = storage && (program.info.images[dense].written ||
                                       program.info.images[dense].atomic);
        request.request_sibling_view = native_format->sibling_format.has_value();
        const auto layout = CalculateGuestImageLayout(request.input);
        if (!layout.ok()) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                  layout.diagnostic.data());
          return set;
        }
        auto& requirement = requirements[dense];
        if (!requirement.used) {
          requirement.used = true;
          requirement.request = request;
          requirement.layout = layout;
        } else {
          requirement.request.usage |= request.usage;
          requirement.request.writable = requirement.request.writable || request.writable;
          requirement.request.request_sibling_view =
              requirement.request.request_sibling_view || request.request_sibling_view;
        }
      }
    }
    std::vector<VulkanGuestImageRequest> lease_requests;
    std::vector<GuestImageStorageKey> lease_keys;
    std::vector<GuestImageLayout> lease_layouts;
    for (std::uint32_t dense = 0; dense < requirements.size(); ++dense) {
      const auto& requirement = requirements[dense];
      if (!requirement.used) continue;
      std::uint32_t lease = UINT32_MAX;
      for (std::uint32_t index = 0; index < lease_keys.size(); ++index) {
        const auto& prior = lease_keys[index];
        const auto& key = requirement.layout.storage_key;
        const bool overlaps = key.guest_address < prior.guest_address + prior.byte_count &&
            prior.guest_address < key.guest_address + key.byte_count;
        if (!overlaps) continue;
        if (key != prior) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                  "overlapping guest image descriptors have incompatible storage");
          return set;
        }
        if (!CompatibleUnderlyingStorage(lease_requests[index], lease_layouts[index],
                                         requirement.request, requirement.layout)) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                  "equal guest image storage has incompatible Vulkan topology");
          return set;
        }
        lease = index;
        lease_requests[index].usage |= requirement.request.usage;
        lease_requests[index].writable = lease_requests[index].writable ||
            requirement.request.writable;
        lease_requests[index].request_sibling_view =
            lease_requests[index].request_sibling_view || requirement.request.request_sibling_view;
        break;
      }
      if (lease == UINT32_MAX) {
        lease = static_cast<std::uint32_t>(lease_requests.size());
        lease_requests.push_back(requirement.request);
        lease_keys.push_back(requirement.layout.storage_key);
        lease_layouts.push_back(requirement.layout);
      }
      image_preparation[dense] = lease;
    }
    for (const auto& request : lease_requests) {
      auto prepared = Prepare(request);
      if (!prepared) {
        FailSet(set, VulkanGuestImageSetStatus::kGuestImageFailure,
                "translated image lease preparation failed");
        Discard(set);
        return set;
      }
      set.images.push_back(std::move(prepared));
    }
    for (const auto& group : program.bindings.descriptors) {
      if (std::find(bindings.begin(), bindings.end(), group.binding) != bindings.end()) {
        FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
                "translated descriptor binding numbers are not unique");
        Discard(set);
        return set;
      }
      bindings.push_back(group.binding);
      using K = shader::recompiler::IR::DescriptorBindingKind;
      if (group.kind == K::Buffers) continue;
      if (group.kind == K::Samplers) {
        for (std::uint32_t array_index = 0; array_index < group.resources.size(); ++array_index) {
          const std::uint32_t dense = group.resources[array_index];
          if (dense >= program.info.samplers.size() || dense >= snapshot.samplers.size()) {
            FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
                    "sampler descriptor has an invalid dense resource index");
            Discard(set);
            return set;
          }
          if (sampler_lease[dense] == UINT32_MAX) {
            Dispatch d;
            if (!LoadSamplers(context_, d)) {
              FailSet(set, VulkanGuestImageSetStatus::kSamplerFailure,
                      "Vulkan sampler functions are unavailable");
              Discard(set);
              return set;
            }
            const auto& words = snapshot.samplers[dense].dwords;
            ShaderSamplerResource sampler{};
            std::memcpy(sampler.fields, words.data(), sizeof(sampler.fields));
            if (sampler.BorderColorType() == static_cast<std::uint8_t>(Prospero::SamplerBorderColor::kFromTable)) {
              FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                      "sampler border-color tables have no proven source");
              Discard(set);
              return set;
            }
            VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            bool anisotropic = false;
            if (!MapAddressMode(sampler.ClampX(), info.addressModeU) ||
                !MapAddressMode(sampler.ClampY(), info.addressModeV) ||
                !MapAddressMode(sampler.ClampZ(), info.addressModeW) ||
                !MapFilter(sampler.XyMagFilter(), info.magFilter, anisotropic) ||
                !MapFilter(sampler.XyMinFilter(), info.minFilter, anisotropic) ||
                !MapMipFilter(sampler.MipFilter(), info.mipmapMode) ||
                (sampler.MipFilter() != static_cast<std::uint8_t>(Prospero::SamplerMipFilter::kNone) &&
                 sampler.MinLod() > sampler.MaxLod())) {
              FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                      "sampler fields are invalid");
              Discard(set);
              return set;
            }
            if (sampler.MipFilter() == static_cast<std::uint8_t>(Prospero::SamplerMipFilter::kNone)) {
              info.minLod = 0.0f;
              info.maxLod = 0.0f;
            } else {
              info.minLod = static_cast<float>(sampler.MinLod()) / 256.0f;
              info.maxLod = static_cast<float>(sampler.MaxLod()) / 256.0f;
            }
            const std::int32_t signed_bias = static_cast<std::int32_t>(sampler.LodBias() << 18) >> 18;
            info.mipLodBias = static_cast<float>(signed_bias) / 256.0f;
            info.anisotropyEnable = anisotropic ? VK_TRUE : VK_FALSE;
            switch (static_cast<Prospero::SamplerAnisoRatio>(sampler.MaxAnisoRatio())) {
              case Prospero::SamplerAnisoRatio::kOne: info.maxAnisotropy = 1.0f; break;
              case Prospero::SamplerAnisoRatio::kTwo: info.maxAnisotropy = 2.0f; break;
              case Prospero::SamplerAnisoRatio::kFour: info.maxAnisotropy = 4.0f; break;
              case Prospero::SamplerAnisoRatio::kEight: info.maxAnisotropy = 8.0f; break;
              case Prospero::SamplerAnisoRatio::kSixteen: info.maxAnisotropy = 16.0f; break;
              default: FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                               "sampler anisotropy enum is invalid"); Discard(set); return set;
            }
            bool compare = false;
            bool integer_border = false;
            bool noninteger_border = false;
            for (const auto& pair : program.info.sampled_pairs) {
              if (pair.sampler != dense || pair.image >= program.info.images.size()) continue;
              const auto& paired_image = program.info.images[pair.image];
              compare = compare || paired_image.depth_compare;
              const bool integer = paired_image.kind == shader::recompiler::IR::ResourceKind::ImageUint ||
                  paired_image.kind == shader::recompiler::IR::ResourceKind::StorageImageUint;
              integer_border = integer_border || integer;
              noninteger_border = noninteger_border || !integer;
            }
            if (compare && !MapCompare(sampler.DepthCompareFunc(), info.compareOp)) {
              FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                      "sampler depth compare enum is invalid"); Discard(set); return set;
            }
            info.compareEnable = compare ? VK_TRUE : VK_FALSE;
            const bool uses_border = UsesBorderColor(info.addressModeU) ||
                UsesBorderColor(info.addressModeV) || UsesBorderColor(info.addressModeW);
            if (uses_border && integer_border && noninteger_border) {
              FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                      "sampler border color cannot mix integer and noninteger images");
              Discard(set);
              return set;
            }
            switch (static_cast<Prospero::SamplerBorderColor>(sampler.BorderColorType())) {
              case Prospero::SamplerBorderColor::kTransBlack:
                info.borderColor = integer_border ? VK_BORDER_COLOR_INT_TRANSPARENT_BLACK :
                    VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                break;
              case Prospero::SamplerBorderColor::kOpaqueBlack:
                info.borderColor = integer_border ? VK_BORDER_COLOR_INT_OPAQUE_BLACK :
                    VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
                break;
              case Prospero::SamplerBorderColor::kOpaqueWhite:
                info.borderColor = integer_border ? VK_BORDER_COLOR_INT_OPAQUE_WHITE :
                    VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                break;
              default: FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                               "sampler border-color enum is invalid"); Discard(set); return set;
            }
            if (sampler.ForceUnormCoords()) {
              // Kyty's ForceUnormCoords is the hardware unnormalized-coordinate
              // mode. Vulkan requires this fixed subset while preserving the
              // decoded minification and magnification filters.
              if (info.magFilter != info.minFilter || compare) {
                FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                        "unnormalized sampler has incompatible filtering or comparison");
                Discard(set);
                return set;
              }
              info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
              info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
              info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
              info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
              info.minLod = 0.0f;
              info.maxLod = 0.0f;
              info.mipLodBias = 0.0f;
              info.anisotropyEnable = VK_FALSE;
              info.maxAnisotropy = 1.0f;
              info.compareEnable = VK_FALSE;
              info.unnormalizedCoordinates = VK_TRUE;
            }
            for (std::uint32_t index = 0; index < sampler_infos.size(); ++index) {
              if (SameSamplerCreateInfo(sampler_infos[index], info)) {
                sampler_lease[dense] = index;
                break;
              }
            }
            if (sampler_lease[dense] == UINT32_MAX) {
              VkSampler native = VK_NULL_HANDLE;
              if (d.create_sampler(context_.device(), &info, nullptr, &native) != VK_SUCCESS ||
                  native == VK_NULL_HANDLE) {
                FailSet(set, VulkanGuestImageSetStatus::kSamplerFailure, "vkCreateSampler failed");
                Discard(set);
                return set;
              }
              sampler_lease[dense] = static_cast<std::uint32_t>(set.samplers.size());
              set.samplers.push_back({native});
              sampler_infos.push_back(info);
            }
          }
          set.sampler_descriptors.push_back({group.binding, array_index, dense,
              sampler_lease[dense], set.samplers[sampler_lease[dense]].sampler});
        }
        continue;
      }
      if (!IsImageBinding(group.kind)) {
        FailSet(set, VulkanGuestImageSetStatus::kUnsupportedDescriptor,
                "translated descriptor group is outside image/sampler scope");
        Discard(set);
        return set;
      }
      const bool storage = IsStorageBinding(group.kind);
      for (std::uint32_t array_index = 0; array_index < group.resources.size(); ++array_index) {
        const std::uint32_t dense = group.resources[array_index];
        if (dense >= program.info.images.size() || dense >= snapshot.images.size()) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
                  "image descriptor has an invalid dense resource index");
          Discard(set);
          return set;
        }
        if (image_preparation[dense] == UINT32_MAX) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidSpecialization,
                  "image descriptor escaped translated image gather");
          Discard(set);
          return set;
        }
        const auto& prepared = set.images[image_preparation[dense]];
        const std::uint32_t descriptor_format =
            (snapshot.images[dense].dwords[1] >> 20u) & 0x1ffu;
        const std::uint32_t required_guest_format = storage
            ? Prospero::StorageAliasFormat(descriptor_format) : descriptor_format;
        const auto required_format = MapGuestImageFormat(required_guest_format);
        if (!required_format) {
          FailSet(set, VulkanGuestImageSetStatus::kInvalidDescriptor,
                  "descriptor lacks a compatible Vulkan image view format");
          Discard(set);
          return set;
        }
        const auto& view_request = requirements[dense].request;
        const VkFormat view_format = required_format->format;
        const VkImageViewType view_type = view_request.view_type.value_or(
            ImageViewType(view_request.input.image_type, prepared.layout.array_layers));
        const std::uint32_t level_count = view_request.view_level_count == 0 ?
            view_request.input.mip_count - view_request.view_base_mip_level :
            view_request.view_level_count;
        const std::uint32_t layer_count = view_request.view_layer_count == 0 ?
            (view_request.input.image_type == Prospero::ImageType::kColor3D ? 1 :
             prepared.layout.array_layers - view_request.view_base_array_layer) :
            view_request.view_layer_count;
        const auto& lease_request = lease_requests[image_preparation[dense]];
        const bool base_range = lease_request.view_type == view_request.view_type &&
            lease_request.view_base_mip_level == view_request.view_base_mip_level &&
            lease_request.view_level_count == view_request.view_level_count &&
            lease_request.view_base_array_layer == view_request.view_base_array_layer &&
            lease_request.view_layer_count == view_request.view_layer_count;
        VkImageView selected_view = VK_NULL_HANDLE;
        if (base_range) {
          if (prepared.format.format == view_format) selected_view = prepared.view;
          else if (prepared.format.sibling_format == view_format) selected_view = prepared.sibling_view;
        }
        if (selected_view == VK_NULL_HANDLE) {
          for (const auto& auxiliary : set.auxiliary_views) {
            if (auxiliary.preparation_index == image_preparation[dense] &&
                auxiliary.format == view_format && auxiliary.view_type == view_type &&
                auxiliary.base_mip_level == view_request.view_base_mip_level &&
                auxiliary.level_count == level_count &&
                auxiliary.base_array_layer == view_request.view_base_array_layer &&
                auxiliary.layer_count == layer_count) {
              selected_view = auxiliary.view;
              break;
            }
          }
          if (selected_view == VK_NULL_HANDLE) {
            Dispatch d;
            if (!Load(context_, d)) { FailSet(set, VulkanGuestImageSetStatus::kGuestImageFailure,
                "Vulkan image-view functions are unavailable"); Discard(set); return set; }
            VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            info.image = prepared.image; info.viewType = view_type; info.format = view_format;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel = view_request.view_base_mip_level;
            info.subresourceRange.levelCount = level_count;
            info.subresourceRange.baseArrayLayer = view_request.view_base_array_layer;
            info.subresourceRange.layerCount = layer_count;
            VkImageView view = VK_NULL_HANDLE;
            if (d.create_view(context_.device(), &info, nullptr, &view) != VK_SUCCESS ||
                view == VK_NULL_HANDLE) { FailSet(set, VulkanGuestImageSetStatus::kGuestImageFailure,
                "vkCreateImageView failed"); Discard(set); return set; }
            selected_view = view;
            set.auxiliary_views.push_back({image_preparation[dense], view_format, view_type,
                view_request.view_base_mip_level, level_count,
                view_request.view_base_array_layer, layer_count, view});
          }
        }
        set.image_descriptors.push_back({group.kind, group.binding, array_index, dense,
            image_preparation[dense], selected_view,
            storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            program.info.images[dense].read, storage &&
                (program.info.images[dense].written || program.info.images[dense].atomic)});
      }
    }
    set.status = VulkanGuestImageSetStatus::kOk;
    return set;
  } catch (...) {
    FailSet(set, VulkanGuestImageSetStatus::kResourceLimit,
            "translated image/sampler set allocation failed");
    Discard(set);
    return set;
  }
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
  to_transfer.subresourceRange.aspectMask = p.aspect_mask;
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
  to_transfer.subresourceRange.aspectMask = p.aspect_mask;
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
  if (p && !p.writable) return true;
  if (!p || !p.writable || !p.readback_recorded || p.gpu_dirty || p.resource == 0 ||
      !coherence_.MarkGpuWrite(p.resource)) return false;
  p.gpu_dirty = true;
  return true;
}

bool VulkanGuestImageCache::Complete(VulkanGuestImagePreparation& p) noexcept {
  try {
    if (p && !p.writable) return true;
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

void VulkanGuestImageCache::Discard(VulkanGuestImageSetPreparation& p) noexcept {
  Dispatch d;
  if (Load(context_, d)) {
    for (auto& view : p.auxiliary_views) {
      if (view.view != VK_NULL_HANDLE) {
        d.destroy_view(context_.device(), view.view, nullptr);
        view.view = VK_NULL_HANDLE;
      }
    }
  }
  if (LoadSamplers(context_, d)) {
    for (auto& sampler : p.samplers) {
      if (sampler.sampler != VK_NULL_HANDLE) {
        d.destroy_sampler(context_.device(), sampler.sampler, nullptr);
        sampler.sampler = VK_NULL_HANDLE;
      }
    }
  }
  for (auto& image : p.images) Discard(image);
  p.sampler_descriptors.clear();
  p.image_descriptors.clear();
  p.auxiliary_views.clear();
  p.samplers.clear();
  p.images.clear();
}

const char* VulkanGuestImageStatusName(VulkanGuestImageStatus status) noexcept {
  switch (status) { case VulkanGuestImageStatus::kOk: return "ok"; case VulkanGuestImageStatus::kInvalidLayout: return "invalid_layout"; case VulkanGuestImageStatus::kUnsupportedFormat: return "unsupported_format"; case VulkanGuestImageStatus::kUnsupportedTopology: return "unsupported_topology"; case VulkanGuestImageStatus::kGuestMemoryProtection: return "guest_memory_protection"; case VulkanGuestImageStatus::kGuestMemoryFault: return "guest_memory_fault"; case VulkanGuestImageStatus::kDeviceResourceFailure: return "device_resource_failure"; case VulkanGuestImageStatus::kResourceLimit: return "resource_limit"; } return "unknown";
}

const char* VulkanGuestImageSetStatusName(VulkanGuestImageSetStatus status) noexcept {
  switch (status) {
    case VulkanGuestImageSetStatus::kOk: return "ok";
    case VulkanGuestImageSetStatus::kInvalidSpecialization: return "invalid_specialization";
    case VulkanGuestImageSetStatus::kUnsupportedDescriptor: return "unsupported_descriptor";
    case VulkanGuestImageSetStatus::kInvalidDescriptor: return "invalid_descriptor";
    case VulkanGuestImageSetStatus::kGuestImageFailure: return "guest_image_failure";
    case VulkanGuestImageSetStatus::kSamplerFailure: return "sampler_failure";
    case VulkanGuestImageSetStatus::kResourceLimit: return "resource_limit";
  }
  return "unknown";
}
}  // namespace kajps5::gpu::vulkan
