// Copyright (C) 2026 KajPS5 contributors
// Vulkan API declarations are vendored from KhronosGroup/Vulkan-Headers at
// 2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350 under Apache-2.0.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

// All Vulkan entry points in this component are resolved from a loader owned
// by VulkanLoader. Keeping prototypes disabled prevents an SDK import library
// from becoming an accidental link-time dependency.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vulkan/vulkan.h>
