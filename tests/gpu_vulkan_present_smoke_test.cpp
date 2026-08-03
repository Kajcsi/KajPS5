// Copyright (C) 2026 KajPS5 contributors
// Hidden Win32 Vulkan presentation smoke. SPDX-License-Identifier: GPL-2.0-only

#include <array>
#include <cstddef>
#include <iostream>
#include <span>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "platform/windows/vulkan_window.h"

int main() {
#if !defined(_WIN32)
  return 77;
#else
  using namespace kajps5;
  constexpr std::uint64_t address = 0x10000;
  constexpr std::uint32_t width = 64, height = 64;
  std::array<std::byte, width * height * 4> pixels{};
  for (std::size_t i = 0; i < pixels.size(); i += 4) { pixels[i] = std::byte{0x20}; pixels[i + 1] = std::byte{0x80}; pixels[i + 2] = std::byte{0xf0}; pixels[i + 3] = std::byte{0xff}; }
  memory::GuestMemory memory(address, pixels.size(), memory::GuestMemoryProtection::kRead | memory::GuestMemoryProtection::kWrite | memory::GuestMemoryProtection::kGpuRead | memory::GuestMemoryProtection::kGpuWrite);
  if (!memory.Initialize(address, std::as_bytes(std::span(pixels)))) return 1;
  auto window = platform::windows::VulkanWindow::CreateHidden(320, 240);
  if (!window) { std::cout << "SKIP: hidden Win32 window unavailable\n"; return 77; }
  gpu::GpuRuntime runtime(memory); auto init = runtime.InitializeVulkanPresentation(window->surface_factory());
  if (!init) { std::cout << "SKIP: presentation context unavailable\n"; return 77; }
  gpu::GuestImageLayoutInput input{}; input.guest_address = address; input.format = static_cast<std::uint32_t>(gpu::Prospero::BufferFormat::k8_8_8_8UNorm); input.width = width; input.height = height; input.depth = 1; input.image_type = gpu::Prospero::ImageType::kColor2D; input.tile_mode = gpu::Prospero::TileMode::kLinear;
  for (int frame = 0; frame != 2; ++frame) { auto present = runtime.PresentVulkanGuestFrame(input); if (!present && present.status != gpu::vulkan::VulkanPresentationStatus::kRecreateRequired) { std::cerr << "FAIL: present " << gpu::vulkan::VulkanPresentationStatusName(present.status) << '\n'; return 1; } for (int spin = 0; spin != 100; ++spin) { window->PumpMessages(); auto poll = runtime.PollVulkanPresentation(); if (poll.status == gpu::vulkan::VulkanPresentationStatus::kOk) break; if (poll.status != gpu::vulkan::VulkanPresentationStatus::kRetainedWorkPending && poll.status != gpu::vulkan::VulkanPresentationStatus::kRecreateRequired) { std::cerr << "FAIL: poll\n"; return 1; } } if (frame == 0) { window->Resize(400, 300); (void)runtime.ResizeVulkanPresentation(window->client_extent()); } }
  const auto* context = runtime.vulkan_context(); std::cout << "present smoke device=\"" << context->properties().name << "\" format/mode selected frames=2\n"; return 0;
#endif
}
