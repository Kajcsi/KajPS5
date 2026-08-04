// Copyright (C) 2026 KajPS5 contributors
// Architecture adapted from KytyPS5 src/graphics/presentation/videoOut.cpp at
// fb5ecec455cf6c67154134429485ffccbfc34203. Behavior adapted from SharpEmu
// src/SharpEmu.Libs/VideoOut/VideoOutExports.cs at
// 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/video_out.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "core/memory/guest_memory.h"
#include "gpu/runtime.h"
#include "kernel/event_queue.h"

namespace kajps5::gpu {
namespace {

constexpr std::uint32_t kVideoOutCategoryUncompressed = 0;
constexpr std::uint32_t kVideoOutCategoryCompressed = 1;
constexpr std::uint32_t kVideoOutTilingModeRenderTarget = 0;
constexpr std::uint32_t kVideoOutTilingModeLinear = 1;
constexpr std::uint64_t kVideoOutRenderTargetAlignment = 64 * 1024ULL;

constexpr std::uint64_t kPixelFormat2R8G8B8A8Srgb = 0x8000000022000000ULL;
constexpr std::uint64_t kPixelFormat2B8G8R8A8Srgb = 0x8000000000000000ULL;
constexpr std::uint64_t kPixelFormat2R10G10B10A2UNorm =
    0x8100000022000000ULL;
constexpr std::uint64_t kPixelFormat2B10G10R10A2UNorm =
    0x8100000000000000ULL;

}  // namespace

VideoOutService::VideoOutService(memory::GuestMemory& memory,
                                 GpuRuntime& gpu_runtime,
                                 kernel::EventQueueService& event_queues) noexcept
    : memory_(memory), gpu_runtime_(gpu_runtime), event_queues_(event_queues) {}

void VideoOutService::SetPresentationCallbacksForTesting(
    PresentationCallbacks callbacks) {
  std::lock_guard lock(mutex_);
  callbacks_ = std::move(callbacks);
}

bool VideoOutService::IsOpenLocked(std::int32_t handle) const noexcept {
  return open_ && handle == 1;
}

std::int32_t VideoOutService::Open(std::int32_t user_id, std::int32_t bus_type,
                                   std::int32_t index) {
  (void)PollPending();
  if ((user_id != 0 && user_id != 255) || bus_type != 0 || index != 0) {
    return kVideoOutErrorInvalidValue;
  }
  std::lock_guard lock(mutex_);
  if (open_) {
    return kVideoOutErrorResourceBusy;
  }
  open_ = true;
  flip_rate_ = 0;
  buffers_ = {};
  groups_ = {};
  event_queues = {};
  event_queue_count_ = 0;
  flip_status_ = {};
  flip_status_.flip_arg = -1;
  flip_status_.current_buffer = -1;
  pending_ = false;
  pending_flip_arg_ = -1;
  last_presentation_ = {};
  return 1;
}

std::int32_t VideoOutService::Close(std::int32_t handle) {
  (void)PollPending();
  std::array<kernel::KernelHandle, kVideoOutMaximumBuffers> queues{};
  std::size_t queue_count = 0;
  {
    std::lock_guard lock(mutex_);
    if (!IsOpenLocked(handle)) {
      return kVideoOutErrorInvalidHandle;
    }
    queues = event_queues;
    queue_count = event_queue_count_;
    open_ = false;
    buffers_ = {};
    groups_ = {};
    event_queues = {};
    event_queue_count_ = 0;
    pending_ = false;
  }
  for (std::size_t index = 0; index < queue_count; ++index) {
    (void)event_queues_.DeleteVideoOutEvent(queues[index], kVideoOutFlipEventIdent);
  }
  return 0;
}

std::int32_t VideoOutService::SetFlipRate(std::int32_t handle,
                                          std::int32_t rate) {
  (void)PollPending();
  if (rate < 0 || rate > 2) {
    return kVideoOutErrorInvalidValue;
  }
  std::lock_guard lock(mutex_);
  if (!IsOpenLocked(handle)) {
    return kVideoOutErrorInvalidHandle;
  }
  flip_rate_ = rate;
  return 0;
}

std::int32_t VideoOutService::RegisterBuffers(
    std::int32_t handle, std::int32_t start_index,
    std::span<const std::uint64_t> addresses,
    const VideoOutBufferAttribute& attribute) {
  std::array<VideoOutBufferEntry, kVideoOutMaximumBuffers> entries{};
  if (addresses.empty() || addresses.size() > entries.size()) {
    return kVideoOutErrorInvalidValue;
  }
  for (std::size_t index = 0; index < addresses.size(); ++index) {
    entries[index].data_address = addresses[index];
  }
  return Register(handle, 0, start_index,
                  std::span(entries).first(addresses.size()), attribute,
                  kVideoOutCategoryUncompressed);
}

std::int32_t VideoOutService::RegisterBuffers2(
    std::int32_t handle, std::int32_t group_index, std::int32_t start_index,
    std::span<const VideoOutBufferEntry> buffers,
    const VideoOutBufferAttribute& attribute, std::uint32_t category,
    std::uint64_t option) {
  if (option != 0 || category != kVideoOutCategoryUncompressed &&
                         category != kVideoOutCategoryCompressed) {
    return kVideoOutErrorInvalidValue;
  }
  return Register(handle, group_index, start_index, buffers, attribute,
                  category);
}

std::int32_t VideoOutService::Register(
    std::int32_t handle, std::int32_t group_index, std::int32_t start_index,
    std::span<const VideoOutBufferEntry> buffers,
    const VideoOutBufferAttribute& attribute, std::uint32_t category) {
  (void)PollPending();
  if (group_index < 0 || group_index >= static_cast<std::int32_t>(groups_.size()) ||
      start_index < 0 || buffers.empty() || buffers.size() > buffers_.size() ||
      static_cast<std::size_t>(start_index) > buffers_.size() - buffers.size() ||
      attribute.width == 0 || attribute.height == 0) {
    return kVideoOutErrorInvalidValue;
  }
  for (const auto& buffer : buffers) {
    if (buffer.data_address == 0) {
      return kVideoOutErrorInvalidAddress;
    }
  }
  std::lock_guard lock(mutex_);
  if (!IsOpenLocked(handle)) {
    return kVideoOutErrorInvalidHandle;
  }
  if (groups_[group_index].occupied) {
    return kVideoOutErrorResourceBusy;
  }
  for (std::size_t index = 0; index < buffers.size(); ++index) {
    if (buffers_[static_cast<std::size_t>(start_index) + index].group_index >= 0) {
      return kVideoOutErrorResourceBusy;
    }
  }
  groups_[group_index] = {true, category, attribute};
  for (std::size_t index = 0; index < buffers.size(); ++index) {
    buffers_[static_cast<std::size_t>(start_index) + index] = {
        group_index, buffers[index]};
  }
  return group_index;
}

std::int32_t VideoOutService::UnregisterBuffers(std::int32_t handle,
                                                 std::int32_t group_index) {
  (void)PollPending();
  if (group_index < 0 || group_index >= static_cast<std::int32_t>(groups_.size())) {
    return kVideoOutErrorInvalidIndex;
  }
  std::lock_guard lock(mutex_);
  if (!IsOpenLocked(handle)) {
    return kVideoOutErrorInvalidHandle;
  }
  if (!groups_[group_index].occupied) {
    return kVideoOutErrorInvalidIndex;
  }
  groups_[group_index] = {};
  for (auto& buffer : buffers_) {
    if (buffer.group_index == group_index) {
      buffer = {};
    }
  }
  return 0;
}

std::int32_t VideoOutService::AddFlipEvent(kernel::KernelHandle queue,
                                           std::int32_t handle,
                                           std::uint64_t user_data) {
  (void)PollPending();
  std::lock_guard lock(mutex_);
  if (!IsOpenLocked(handle)) {
    return kVideoOutErrorInvalidHandle;
  }
  if (event_queue_count_ == event_queues.size() &&
      std::find(event_queues.begin(), event_queues.end(), queue) == event_queues.end()) {
    return kVideoOutErrorResourceBusy;
  }
  const auto status = event_queues_.AddVideoOutEvent(
      queue, kVideoOutFlipEventIdent, user_data);
  if (status != kernel::KernelStatus::kOk) {
    return kVideoOutErrorInvalidEventQueue;
  }
  if (std::find(event_queues.begin(), event_queues.end(), queue) == event_queues.end()) {
    event_queues[event_queue_count_++] = queue;
  }
  return 0;
}

std::int32_t VideoOutService::DeleteFlipEvent(kernel::KernelHandle queue,
                                              std::int32_t handle) {
  (void)PollPending();
  std::lock_guard lock(mutex_);
  if (!IsOpenLocked(handle)) {
    return kVideoOutErrorInvalidHandle;
  }
  const auto status = event_queues_.DeleteVideoOutEvent(
      queue, kVideoOutFlipEventIdent);
  if (status != kernel::KernelStatus::kOk) {
    return status == kernel::KernelStatus::kNotFound
               ? kVideoOutErrorInvalidEventQueue
               : kVideoOutErrorInvalidEvent;
  }
  const auto found = std::find(event_queues.begin(), event_queues.end(), queue);
  if (found != event_queues.end()) {
    std::move(found + 1, event_queues.begin() + event_queue_count_, found);
    --event_queue_count_;
    event_queues[event_queue_count_] = kernel::kInvalidKernelHandle;
  }
  return 0;
}

bool VideoOutService::IsSupportedFormat(std::uint64_t pixel_format,
                                        std::uint32_t& guest_format,
                                        vulkan::VulkanImageFormat& format_override) noexcept {
  switch (pixel_format) {
  case kPixelFormat2R8G8B8A8Srgb:
    guest_format = Prospero::GpuEnumValue(Prospero::BufferFormat::k8_8_8_8Srgb);
    format_override = {VK_FORMAT_R8G8B8A8_SRGB,
                       vulkan::VulkanImageStorageClass::kR8G8B8A8};
    return true;
  case kPixelFormat2B8G8R8A8Srgb:
    guest_format = Prospero::GpuEnumValue(Prospero::BufferFormat::k8_8_8_8Srgb);
    format_override = {VK_FORMAT_B8G8R8A8_SRGB,
                       vulkan::VulkanImageStorageClass::kR8G8B8A8};
    return true;
  case kPixelFormat2R10G10B10A2UNorm:
    guest_format = Prospero::GpuEnumValue(Prospero::BufferFormat::k10_10_10_2UNorm);
    format_override = {VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                       vulkan::VulkanImageStorageClass::kA2B10G10R10};
    return true;
  case kPixelFormat2B10G10R10A2UNorm:
    guest_format = Prospero::GpuEnumValue(Prospero::BufferFormat::k10_10_10_2UNorm);
    format_override = {VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                       vulkan::VulkanImageStorageClass::kA2B10G10R10};
    return true;
  default:
    return false;
  }
}

bool VideoOutService::IsSupportedTileMode(std::uint32_t raw,
                                          Prospero::TileMode& mode) noexcept {
  if (raw == kVideoOutTilingModeRenderTarget) {
    mode = Prospero::TileMode::kRenderTarget;
    return true;
  }
  if (raw == kVideoOutTilingModeLinear) {
    mode = Prospero::TileMode::kLinear;
    return true;
  }
  return false;
}

void VideoOutService::CompleteLocked() {
  pending_ = false;
  flip_status_.pending_count = 0;
  flip_status_.flip_arg = pending_flip_arg_;
  ++flip_status_.count;
  (void)event_queues_.TriggerVideoOutEvents(
      static_cast<std::uint64_t>(pending_flip_arg_));
}

bool VideoOutService::PollPending() {
  PresentationCallbacks callbacks;
  {
    std::lock_guard lock(mutex_);
    if (!pending_) {
      return false;
    }
    callbacks = callbacks_;
  }
  const auto result = callbacks.poll ? callbacks.poll()
                                     : gpu_runtime_.PollVulkanPresentation();
  std::lock_guard lock(mutex_);
  if (!pending_) {
    return false;
  }
  if (result.status == vulkan::VulkanPresentationStatus::kOk ||
      result.status == vulkan::VulkanPresentationStatus::kContextUnavailable) {
    last_presentation_ = result;
    flip_status_.timeline = result.timeline;
    CompleteLocked();
    return true;
  }
  if (result.status == vulkan::VulkanPresentationStatus::kRetainedWorkPending) {
    last_presentation_ = result;
    flip_status_.timeline = result.timeline;
  }
  return false;
}

std::int32_t VideoOutService::SubmitFlip(std::int32_t handle,
                                         std::int32_t buffer_index,
                                         std::int32_t flip_mode,
                                         std::int64_t flip_arg) {
  (void)PollPending();
  if (buffer_index < 0 || buffer_index >= static_cast<std::int32_t>(buffers_.size())) {
    return kVideoOutErrorInvalidIndex;
  }
  if (flip_mode != 1 && flip_mode != 4) {
    return kVideoOutErrorInvalidValue;
  }
  GuestImageLayoutInput input;
  vulkan::VulkanImageFormat format_override;
  PresentationCallbacks callbacks;
  std::int32_t group_index = -1;
  VideoOutBufferEntry entry;
  VideoOutBufferAttribute attribute;
  std::uint32_t category = 0;
  {
    std::lock_guard lock(mutex_);
    if (!IsOpenLocked(handle)) {
      return kVideoOutErrorInvalidHandle;
    }
    if (pending_) {
      return kVideoOutErrorResourceBusy;
    }
    const auto& slot = buffers_[buffer_index];
    if (slot.group_index < 0) {
      return kVideoOutErrorInvalidIndex;
    }
    const auto& group = groups_[slot.group_index];
    std::uint32_t guest_format = 0;
    Prospero::TileMode tile_mode{};
    if (!group.occupied ||
        !IsSupportedFormat(group.attribute.pixel_format, guest_format, format_override) ||
        !IsSupportedTileMode(group.attribute.tiling_mode, tile_mode)) {
      return kVideoOutErrorInvalidValue;
    }
    if (tile_mode == Prospero::TileMode::kRenderTarget) {
      if (group.attribute.pitch_in_pixels != 0) {
        return kVideoOutErrorInvalidValue;
      }
      if ((slot.entry.data_address & (kVideoOutRenderTargetAlignment - 1U)) != 0) {
        return kVideoOutErrorInvalidAddress;
      }
      input = {slot.entry.data_address, guest_format, group.attribute.width,
               group.attribute.height, 1, 1, 1,
               0, 0, Prospero::ImageType::kColor2D, tile_mode, true};
    } else {
      const auto pitch = group.attribute.pitch_in_pixels == 0
                             ? group.attribute.width
                             : group.attribute.pitch_in_pixels;
      if (pitch < group.attribute.width ||
          pitch > std::numeric_limits<std::uint64_t>::max() / 4U) {
        return kVideoOutErrorInvalidValue;
      }
      const auto row_pitch_bytes = static_cast<std::uint64_t>(pitch) * 4U;
      if (row_pitch_bytes >
          std::numeric_limits<std::uint64_t>::max() / group.attribute.height) {
        return kVideoOutErrorInvalidValue;
      }
      const auto slice_pitch_bytes = row_pitch_bytes * group.attribute.height;
      input = {slot.entry.data_address, guest_format, group.attribute.width,
               group.attribute.height, 1, 1, 1,
               row_pitch_bytes, slice_pitch_bytes,
               Prospero::ImageType::kColor2D, tile_mode, false};
    }
    group_index = slot.group_index;
    entry = slot.entry;
    attribute = group.attribute;
    category = group.category;
    callbacks = callbacks_;
  }
  const auto layout = CalculateGuestImageLayout(input);
  if (!layout.ok()) {
    return kVideoOutErrorInvalidValue;
  }
  if (!memory_.CanAccess(layout.storage_key.guest_address, layout.guest_storage_bytes,
                         memory::GuestMemoryProtection::kRead |
                             memory::GuestMemoryProtection::kGpuRead)) {
    return kVideoOutErrorInvalidAddress;
  }
  const auto result = callbacks.present
                          ? callbacks.present(input, format_override)
                          : gpu_runtime_.PresentVulkanGuestFrame(
                                input, 50'000'000ULL, format_override);
  std::lock_guard lock(mutex_);
  if (!IsOpenLocked(handle)) {
    return kVideoOutErrorInvalidHandle;
  }
  const auto& slot = buffers_[buffer_index];
  const auto& group = groups_[group_index];
  if (slot.group_index != group_index || slot.entry.data_address != entry.data_address ||
      slot.entry.metadata_address != entry.metadata_address || !group.occupied ||
      group.category != category || group.attribute.tiling_mode != attribute.tiling_mode ||
      group.attribute.aspect_ratio != attribute.aspect_ratio ||
      group.attribute.width != attribute.width || group.attribute.height != attribute.height ||
      group.attribute.pitch_in_pixels != attribute.pitch_in_pixels ||
      group.attribute.option != attribute.option ||
      group.attribute.pixel_format != attribute.pixel_format ||
      group.attribute.dcc_clear != attribute.dcc_clear ||
      group.attribute.dcc_control != attribute.dcc_control) {
    return kVideoOutErrorInvalidIndex;
  }
  if (result.status == vulkan::VulkanPresentationStatus::kOk ||
      result.status == vulkan::VulkanPresentationStatus::kContextUnavailable) {
    flip_status_.current_buffer = buffer_index;
    pending_flip_arg_ = flip_arg;
    last_presentation_ = result;
    flip_status_.timeline = result.timeline;
    CompleteLocked();
    return 0;
  }
  if (result.status == vulkan::VulkanPresentationStatus::kRetainedWorkPending) {
    flip_status_.current_buffer = buffer_index;
    pending_flip_arg_ = flip_arg;
    last_presentation_ = result;
    flip_status_.timeline = result.timeline;
    pending_ = true;
    flip_status_.pending_count = 1;
    return 0;
  }
  return result.status == vulkan::VulkanPresentationStatus::kInvalidFrame
             ? kVideoOutErrorInvalidValue
             : kVideoOutErrorResourceBusy;
}

std::int32_t VideoOutService::GetFlipStatus(std::int32_t handle,
                                            VideoOutFlipStatus& status) {
  (void)PollPending();
  std::lock_guard lock(mutex_);
  if (!IsOpenLocked(handle)) {
    return kVideoOutErrorInvalidHandle;
  }
  status = flip_status_;
  return 0;
}

std::int32_t VideoOutService::IsFlipPending(std::int32_t handle) {
  (void)PollPending();
  std::lock_guard lock(mutex_);
  return IsOpenLocked(handle) ? flip_status_.pending_count
                              : kVideoOutErrorInvalidHandle;
}

vulkan::VulkanPresentationResult
VideoOutService::last_presentation_result() const {
  std::lock_guard lock(mutex_);
  return last_presentation_;
}

}  // namespace kajps5::gpu
