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
namespace { constexpr wchar_t kClassName[] = L"KajPS5VulkanHiddenWindow"; LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) { auto* i=reinterpret_cast<VulkanWindow::Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA)); if(m==WM_NCCREATE){i=static_cast<VulkanWindow::Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(i));} if(i){if(m==WM_SIZE){i->extent={LOWORD(l),HIWORD(l)};i->minimized=w==SIZE_MINIMIZED||i->extent.width==0||i->extent.height==0;}else if(m==WM_CLOSE)i->closed=true;} return DefWindowProcW(h,m,w,l); } }
VulkanWindow::VulkanWindow(std::unique_ptr<Impl> impl) noexcept:impl_(std::move(impl)){} VulkanWindow::~VulkanWindow(){if(impl_&&impl_->hwnd)DestroyWindow(impl_->hwnd);}
std::unique_ptr<VulkanWindow> VulkanWindow::CreateHidden(std::uint32_t width,std::uint32_t height){auto i=std::make_unique<Impl>();i->instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=i->instance;wc.lpszClassName=kClassName;wc.lpfnWndProc=Proc;RegisterClassW(&wc);i->hwnd=CreateWindowExW(0,kClassName,L"KajPS5 Vulkan",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,static_cast<int>(width),static_cast<int>(height),nullptr,nullptr,i->instance,i.get());if(!i->hwnd)return nullptr;i->extent={width,height};return std::unique_ptr<VulkanWindow>(new VulkanWindow(std::move(i)));}
gpu::vulkan::VulkanSurfaceFactory VulkanWindow::surface_factory() const { gpu::vulkan::VulkanSurfaceFactory f;f.required_instance_extensions={"VK_KHR_surface","VK_KHR_win32_surface"};auto* i=impl_.get();f.create_surface=[i](VkInstance instance,PFN_vkGetInstanceProcAddr resolver,VkSurfaceKHR*out){auto create=reinterpret_cast<PFN_vkCreateWin32SurfaceKHRLocal>(resolver(instance,"vkCreateWin32SurfaceKHR"));if(!create||!i||!i->hwnd)return VK_ERROR_INITIALIZATION_FAILED;VkWin32SurfaceCreateInfoKHRLocal ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};ci.hinstance=i->instance;ci.hwnd=i->hwnd;return create(instance,&ci,nullptr,out);};return f; }
void VulkanWindow::PumpMessages() noexcept { MSG m{};while(PeekMessageW(&m,impl_->hwnd,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);} } void VulkanWindow::Resize(std::uint32_t w,std::uint32_t h) noexcept {SetWindowPos(impl_->hwnd,nullptr,0,0,static_cast<int>(w),static_cast<int>(h),SWP_NOMOVE|SWP_NOZORDER);impl_->extent={w,h};impl_->minimized=w==0||h==0;} VkExtent2D VulkanWindow::client_extent() const noexcept{return impl_->extent;}bool VulkanWindow::minimized()const noexcept{return impl_->minimized;}bool VulkanWindow::closed()const noexcept{return impl_->closed;}
}  // namespace kajps5::platform::windows
#endif
