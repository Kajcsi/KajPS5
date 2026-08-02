// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// src/graphics/presentation/window/vulkanWindow.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/vulkan/loader.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace kajps5::gpu::vulkan {

VulkanLoader::VulkanLoader(
    void* library_handle,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr) noexcept
    : library_handle_(library_handle),
      get_instance_proc_addr_(get_instance_proc_addr) {}

VulkanLoader::~VulkanLoader() {
  Reset();
}

VulkanLoader::VulkanLoader(VulkanLoader&& other) noexcept
    : library_handle_(std::exchange(other.library_handle_, nullptr)),
      get_instance_proc_addr_(
          std::exchange(other.get_instance_proc_addr_, nullptr)) {}

VulkanLoader& VulkanLoader::operator=(VulkanLoader&& other) noexcept {
  if (this != &other) {
    Reset();
    library_handle_ = std::exchange(other.library_handle_, nullptr);
    get_instance_proc_addr_ =
        std::exchange(other.get_instance_proc_addr_, nullptr);
  }
  return *this;
}

std::optional<VulkanLoader> VulkanLoader::Open(std::string& diagnostic) {
  diagnostic.clear();

#if defined(_WIN32)
  const HMODULE library = ::LoadLibraryA("vulkan-1.dll");
  if (library == nullptr) {
    diagnostic = "could not load vulkan-1.dll";
    return std::nullopt;
  }

  const auto get_instance_proc_addr =
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(
          ::GetProcAddress(library, "vkGetInstanceProcAddr"));
  if (get_instance_proc_addr == nullptr) {
    diagnostic = "vulkan-1.dll does not export vkGetInstanceProcAddr";
    ::FreeLibrary(library);
    return std::nullopt;
  }

  return VulkanLoader(reinterpret_cast<void*>(library),
                      get_instance_proc_addr);
#elif defined(__linux__)
  void* library = ::dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) {
    const char* error = ::dlerror();
    diagnostic = "could not load libvulkan.so.1";
    if (error != nullptr) {
      diagnostic += ": ";
      diagnostic += error;
    }
    return std::nullopt;
  }

  const auto get_instance_proc_addr =
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(
          ::dlsym(library, "vkGetInstanceProcAddr"));
  if (get_instance_proc_addr == nullptr) {
    diagnostic = "libvulkan.so.1 does not export vkGetInstanceProcAddr";
    ::dlclose(library);
    return std::nullopt;
  }

  return VulkanLoader(library, get_instance_proc_addr);
#else
  diagnostic = "dynamic Vulkan loading is unsupported on this platform";
  return std::nullopt;
#endif
}

VulkanLoader VulkanLoader::FromGetInstanceProcAddr(
    PFN_vkGetInstanceProcAddr get_instance_proc_addr) noexcept {
  return VulkanLoader(nullptr, get_instance_proc_addr);
}

void VulkanLoader::Reset() noexcept {
  get_instance_proc_addr_ = nullptr;
  if (library_handle_ == nullptr) {
    return;
  }

#if defined(_WIN32)
  ::FreeLibrary(reinterpret_cast<HMODULE>(library_handle_));
#elif defined(__linux__)
  ::dlclose(library_handle_);
#endif
  library_handle_ = nullptr;
}

}  // namespace kajps5::gpu::vulkan
