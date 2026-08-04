// Copyright (C) 2026 KajPS5 contributors
// Hidden Win32 Vulkan presentation smoke. SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <vector>

#include "core/memory/guest_memory.h"
#include "gpu/format.h"
#include "gpu/image_layout.h"
#include "gpu/runtime.h"
#include "gpu/tile_layout.h"
#include "platform/windows/vulkan_window.h"

#if defined(_WIN32)
namespace {

bool FillRenderTarget64KB(const kajps5::gpu::GuestImageLayout& layout,
                          std::span<const std::byte> linear,
                          std::span<std::byte> tiled) {
  using namespace kajps5::gpu;
  const auto bytes_per_element = Prospero::NumBytesPerElement(layout.view_format);
  const auto& mip = layout.mips[0];
  TileBlockLayout block{};
  if (!TileGetBlockLayout(TileBlockFamily::RenderTarget64KB, bytes_per_element, block) ||
      linear.size() != layout.total_bytes || tiled.size() != layout.guest_storage_bytes ||
      mip.row_bytes % bytes_per_element != 0) {
    return false;
  }

  std::fill(tiled.begin(), tiled.end(), std::byte{0x5d});
  const std::uint64_t pitch = mip.row_bytes / bytes_per_element;
  const std::uint64_t columns = pitch / block.block_width +
                                (pitch % block.block_width == 0 ? 0 : 1);
  bool non_linear = false;
  for (std::uint32_t y = 0; y < mip.height; ++y) {
    for (std::uint32_t x = 0; x < mip.width; ++x) {
      const std::uint32_t block_x = x / block.block_width;
      const std::uint32_t block_y = y / block.block_height;
      const std::uint32_t local_x = x % block.block_width;
      const std::uint32_t local_y = y % block.block_height;
      std::uint32_t local_offset = 0;
      std::uint32_t block_xor = 0;
      if (!TileGetBlockOffset(block, local_x, local_y, 0, local_offset) ||
          !TileGetBlockXor(block, block_x, block_y, 0, block_xor)) {
        return false;
      }
      const std::uint64_t tiled_offset =
          (static_cast<std::uint64_t>(block_y) * columns + block_x) * block.block_size +
          (local_offset ^ block_xor);
      const std::uint64_t linear_offset =
          static_cast<std::uint64_t>(y) * mip.row_bytes + x * bytes_per_element;
      if (tiled_offset + bytes_per_element > tiled.size() ||
          linear_offset + bytes_per_element > linear.size()) {
        return false;
      }
      non_linear = non_linear || tiled_offset != linear_offset;
      for (std::uint32_t byte = 0; byte < bytes_per_element; ++byte) {
        tiled[tiled_offset + byte] = linear[linear_offset + byte];
      }
    }
  }
  return non_linear;
}

bool PresentAndPoll(kajps5::gpu::GpuRuntime& runtime,
                    kajps5::platform::windows::VulkanWindow& window,
                    const kajps5::gpu::GuestImageLayoutInput& input,
                    std::optional<kajps5::gpu::vulkan::VulkanImageFormat> format_override,
                    const char* frame_name) {
  using kajps5::gpu::vulkan::VulkanPresentationStatus;
  const auto present = runtime.PresentVulkanGuestFrame(input, 50'000'000ULL,
                                                       format_override);
  if (present.status != VulkanPresentationStatus::kOk &&
      present.status != VulkanPresentationStatus::kRecreateRequired) {
    std::cerr << "FAIL: " << frame_name << " present "
              << kajps5::gpu::vulkan::VulkanPresentationStatusName(present.status) << '\n';
    return false;
  }
  for (int spin = 0; spin != 1'000; ++spin) {
    window.PumpMessages();
    const auto poll = runtime.PollVulkanPresentation();
    if (poll.status == VulkanPresentationStatus::kOk) {
      return true;
    }
    if (poll.status != VulkanPresentationStatus::kRetainedWorkPending &&
        poll.status != VulkanPresentationStatus::kRecreateRequired) {
      std::cerr << "FAIL: " << frame_name << " poll "
                << kajps5::gpu::vulkan::VulkanPresentationStatusName(poll.status) << '\n';
      return false;
    }
  }
  std::cerr << "FAIL: " << frame_name << " presentation did not complete\n";
  return false;
}

}  // namespace
#endif

int main() {
#if !defined(_WIN32)
  return 77;
#else
  using namespace kajps5;
  constexpr std::uint64_t address = 0x10000;
  constexpr std::uint64_t tiled_address = 0x20000;
  constexpr std::uint32_t width = 64, height = 64;
  constexpr std::uint32_t tiled_width = 17, tiled_height = 9;
  constexpr std::uint64_t tiled_storage_bytes = 64 * 1024ULL;
  std::array<std::byte, width * height * 4> pixels{};
  for (std::size_t i = 0; i < pixels.size(); i += 4) { pixels[i] = std::byte{0x20}; pixels[i + 1] = std::byte{0x80}; pixels[i + 2] = std::byte{0xf0}; pixels[i + 3] = std::byte{0xff}; }
  gpu::GuestImageLayoutInput tiled_input{};
  tiled_input.guest_address = tiled_address;
  tiled_input.format = gpu::Prospero::GpuEnumValue(
      gpu::Prospero::BufferFormat::k8_8_8_8Srgb);
  tiled_input.width = tiled_width;
  tiled_input.height = tiled_height;
  tiled_input.depth = 1;
  tiled_input.layers = 1;
  tiled_input.mip_count = 1;
  tiled_input.image_type = gpu::Prospero::ImageType::kColor2D;
  tiled_input.tile_mode = gpu::Prospero::TileMode::kRenderTarget;
  tiled_input.tightly_packed = true;
  const auto tiled_layout = gpu::CalculateGuestImageLayout(tiled_input);
  if (!tiled_layout.ok() || !tiled_layout.needs_detile ||
      tiled_layout.mips[0].row_bytes < tiled_width * 4ULL ||
      tiled_layout.total_bytes != tiled_layout.mips[0].row_bytes * tiled_height ||
      tiled_layout.mips[0].slice_bytes != tiled_layout.total_bytes ||
      tiled_layout.mips[0].layer_bytes != tiled_layout.total_bytes ||
      tiled_layout.mips[0].byte_count != tiled_layout.total_bytes ||
      tiled_layout.guest_storage_bytes != tiled_storage_bytes) {
    std::cerr << "FAIL: tiled guest layout expected pitched 17x9 staging and storage="
              << tiled_storage_bytes << " got row=" << tiled_layout.mips[0].row_bytes
              << " total=" << tiled_layout.total_bytes
              << " storage=" << tiled_layout.guest_storage_bytes << '\n';
    return 1;
  }
  std::vector<std::byte> tiled_linear(tiled_layout.total_bytes, std::byte{0xb2});
  for (std::uint32_t y = 0; y < tiled_height; ++y) {
    for (std::uint32_t x = 0; x < tiled_width; ++x) {
      const auto offset = y * tiled_layout.mips[0].row_bytes + x * 4;
      tiled_linear[offset] = static_cast<std::byte>((x * 17U + y * 43U) & 0xffU);
      tiled_linear[offset + 1] = static_cast<std::byte>((x * 29U + y * 11U) & 0xffU);
      tiled_linear[offset + 2] = static_cast<std::byte>((x * 7U + y * 59U) & 0xffU);
      tiled_linear[offset + 3] = std::byte{0xff};
    }
  }
  std::array<std::byte, tiled_storage_bytes> tiled_pixels{};
  if (!FillRenderTarget64KB(tiled_layout, tiled_linear, tiled_pixels)) {
    std::cerr << "FAIL: tiled reference generation did not produce bounded non-linear offsets\n";
    return 1;
  }
  memory::GuestMemory memory(
      address, tiled_address + tiled_storage_bytes - address,
      memory::GuestMemoryProtection::kRead | memory::GuestMemoryProtection::kWrite |
          memory::GuestMemoryProtection::kGpuRead | memory::GuestMemoryProtection::kGpuWrite);
  if (!memory.Initialize(address, std::as_bytes(std::span(pixels))) ||
      !memory.Initialize(tiled_address, std::as_bytes(std::span(tiled_pixels)))) {
    return 1;
  }
  auto window = platform::windows::VulkanWindow::CreateHidden(320, 240);
  if (!window) { std::cout << "SKIP: hidden Win32 window unavailable\n"; return 77; }
  gpu::GpuRuntime runtime(memory); auto init = runtime.InitializeVulkanPresentation(window->surface_factory());
  if (!init) { std::cout << "SKIP: presentation context unavailable\n"; return 77; }
  gpu::GuestImageLayoutInput input{}; input.guest_address = address; input.format = static_cast<std::uint32_t>(gpu::Prospero::BufferFormat::k8_8_8_8UNorm); input.width = width; input.height = height; input.depth = 1; input.image_type = gpu::Prospero::ImageType::kColor2D; input.tile_mode = gpu::Prospero::TileMode::kLinear;
  if (!PresentAndPoll(runtime, *window, input, std::nullopt, "linear frame 1")) return 1;
  window->Resize(400, 300);
  (void)runtime.ResizeVulkanPresentation(window->client_extent());
  if (!PresentAndPoll(runtime, *window, input, std::nullopt, "linear frame 2 after resize")) return 1;
  const gpu::vulkan::VulkanImageFormat tiled_override{
      VK_FORMAT_R8G8B8A8_SRGB, gpu::vulkan::VulkanImageStorageClass::kR8G8B8A8,
      std::nullopt};
  if (!PresentAndPoll(runtime, *window, tiled_input, tiled_override, "tiled frame")) return 1;
  const auto* context = runtime.vulkan_context(); std::cout << "present smoke device=\"" << context->properties().name << "\" linear frames=2 tiled frames=1 resize=completed\n"; return 0;
#endif
}
