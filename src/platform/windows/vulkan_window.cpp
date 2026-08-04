// Copyright (C) 2026 KajPS5 contributors
// Narrow Win32 surface boundary adapted from KytyPS5 vulkanWindow at
// fb5ecec455cf6c67154134429485ffccbfc34203. SPDX-License-Identifier: GPL-2.0-only

#include "platform/windows/vulkan_window.h"

#if defined(_WIN32)
#include <windows.h>

namespace kajps5::platform::windows {
struct VkWin32SurfaceCreateInfoKHRLocal {
  VkStructureType sType;
  const void* pNext;
  VkFlags flags;
  HINSTANCE hinstance;
  HWND hwnd;
};
using PFN_vkCreateWin32SurfaceKHRLocal = VkResult(VKAPI_PTR *)(
    VkInstance, const VkWin32SurfaceCreateInfoKHRLocal*,
    const VkAllocationCallbacks*, VkSurfaceKHR*);
struct VulkanWindow::Impl { HWND hwnd = nullptr; HINSTANCE instance = nullptr; VkExtent2D extent{}; bool minimized=false, closed=false; };
namespace {

constexpr wchar_t kClassName[] = L"KajPS5VulkanWindow";

void UpdateClientExtent(VulkanWindow::Impl& impl) noexcept {
  RECT client{};
  if (GetClientRect(impl.hwnd, &client) != 0) {
    impl.extent = {static_cast<std::uint32_t>(client.right - client.left),
                   static_cast<std::uint32_t>(client.bottom - client.top)};
  }
  impl.minimized = impl.extent.width == 0 || impl.extent.height == 0;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* impl = reinterpret_cast<VulkanWindow::Impl*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    impl = static_cast<VulkanWindow::Impl*>(
        reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(impl));
  }
  if (impl != nullptr) {
    if (message == WM_SIZE) {
      impl->extent = {LOWORD(lparam), HIWORD(lparam)};
      impl->minimized = wparam == SIZE_MINIMIZED || impl->extent.width == 0 ||
                        impl->extent.height == 0;
    } else if (message == WM_CLOSE) {
      // The owner destroys the HWND after the host loop has observed this.
      impl->closed = true;
      return 0;
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::unique_ptr<VulkanWindow::Impl> CreateImpl(std::uint32_t width,
                                                std::uint32_t height,
                                                bool visible) {
  auto impl = std::make_unique<VulkanWindow::Impl>();
  impl->instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class{};
  window_class.hInstance = impl->instance;
  window_class.lpszClassName = kClassName;
  window_class.lpfnWndProc = Proc;
  (void)RegisterClassW(&window_class);
  impl->hwnd = CreateWindowExW(
      0, kClassName, L"KajPS5 Vulkan", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
      CW_USEDEFAULT, static_cast<int>(width), static_cast<int>(height), nullptr,
      nullptr, impl->instance, impl.get());
  if (impl->hwnd == nullptr) {
    return nullptr;
  }
  UpdateClientExtent(*impl);
  if (visible) {
    ShowWindow(impl->hwnd, SW_SHOW);
    UpdateWindow(impl->hwnd);
    UpdateClientExtent(*impl);
  }
  return impl;
}

}  // namespace
VulkanWindow::VulkanWindow(std::unique_ptr<Impl> impl) noexcept:impl_(std::move(impl)){} VulkanWindow::~VulkanWindow(){if(impl_&&impl_->hwnd)DestroyWindow(impl_->hwnd);}
std::unique_ptr<VulkanWindow> VulkanWindow::CreateHidden(std::uint32_t width,std::uint32_t height){auto impl=CreateImpl(width, height, false);return impl?std::unique_ptr<VulkanWindow>(new VulkanWindow(std::move(impl))):nullptr;}
std::unique_ptr<VulkanWindow> VulkanWindow::CreateVisible(std::uint32_t width,std::uint32_t height){auto impl=CreateImpl(width, height, true);return impl?std::unique_ptr<VulkanWindow>(new VulkanWindow(std::move(impl))):nullptr;}
gpu::vulkan::VulkanSurfaceFactory VulkanWindow::surface_factory() const { gpu::vulkan::VulkanSurfaceFactory f;f.required_instance_extensions={"VK_KHR_surface","VK_KHR_win32_surface"};auto* i=impl_.get();f.create_surface=[i](VkInstance instance,PFN_vkGetInstanceProcAddr resolver,VkSurfaceKHR*out){auto create=reinterpret_cast<PFN_vkCreateWin32SurfaceKHRLocal>(resolver(instance,"vkCreateWin32SurfaceKHR"));if(!create||!i||!i->hwnd)return VK_ERROR_INITIALIZATION_FAILED;VkWin32SurfaceCreateInfoKHRLocal ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};ci.hinstance=i->instance;ci.hwnd=i->hwnd;return create(instance,&ci,nullptr,out);};return f; }
void VulkanWindow::PumpMessages() noexcept { MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){impl_->closed=true;continue;}TranslateMessage(&m);DispatchMessageW(&m);} } void VulkanWindow::Resize(std::uint32_t w,std::uint32_t h) noexcept {SetWindowPos(impl_->hwnd,nullptr,0,0,static_cast<int>(w),static_cast<int>(h),SWP_NOMOVE|SWP_NOZORDER);UpdateClientExtent(*impl_);} VkExtent2D VulkanWindow::client_extent() const noexcept{return impl_->extent;}bool VulkanWindow::minimized()const noexcept{return impl_->minimized;}bool VulkanWindow::closed()const noexcept{return impl_->closed;}
}  // namespace kajps5::platform::windows
#endif
