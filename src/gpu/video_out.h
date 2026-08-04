// Copyright (C) 2026 KajPS5 contributors
// Architecture adapted from KytyPS5 src/graphics/presentation/videoOut.{h,cpp}
// at fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior adapted from SharpEmu src/SharpEmu.Libs/VideoOut/VideoOutExports.cs
// at 9e10d7c44a2821cfd5ccd3417c09c0cf269285a4.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>

#include "gpu/image_layout.h"
#include "gpu/vulkan/presentation.h"
#include "kernel/handle_table.h"

namespace kajps5::memory {
class GuestMemory;
}

namespace kajps5::kernel {
class EventQueueService;
}

namespace kajps5::gpu {
class GpuRuntime;

inline constexpr std::int32_t kVideoOutErrorInvalidValue =
    static_cast<std::int32_t>(0x80290001U);
inline constexpr std::int32_t kVideoOutErrorInvalidAddress =
    static_cast<std::int32_t>(0x80290002U);
inline constexpr std::int32_t kVideoOutErrorResourceBusy =
    static_cast<std::int32_t>(0x80290009U);
inline constexpr std::int32_t kVideoOutErrorInvalidIndex =
    static_cast<std::int32_t>(0x8029000aU);
inline constexpr std::int32_t kVideoOutErrorInvalidHandle =
    static_cast<std::int32_t>(0x8029000bU);
inline constexpr std::int32_t kVideoOutErrorInvalidEventQueue =
    static_cast<std::int32_t>(0x8029000cU);
inline constexpr std::int32_t kVideoOutErrorInvalidEvent =
    static_cast<std::int32_t>(0x8029000dU);
inline constexpr std::int32_t kVideoOutErrorInvalidOption =
    static_cast<std::int32_t>(0x8029001aU);

inline constexpr std::size_t kVideoOutMaximumBuffers = 16;
inline constexpr std::size_t kVideoOutMaximumGroups = 4;
inline constexpr std::uint64_t kVideoOutFlipEventIdent = 0x6;

struct VideoOutBufferAttribute {
  std::uint32_t tiling_mode = 0;
  std::uint32_t aspect_ratio = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pitch_in_pixels = 0;
  std::uint64_t option = 0;
  std::uint64_t pixel_format = 0;
  std::uint64_t dcc_clear = 0;
  std::uint32_t dcc_control = 0;
};

struct VideoOutBufferEntry {
  std::uint64_t data_address = 0;
  std::uint64_t metadata_address = 0;
};

struct VideoOutFlipStatus {
  std::uint64_t count = 0;
  std::int64_t flip_arg = -1;
  std::int32_t pending_count = 0;
  std::int32_t current_buffer = -1;
  std::uint64_t timeline = 0;
};

class VideoOutService final {
 public:
  struct PresentationCallbacks {
    std::function<vulkan::VulkanPresentationResult(
        const GuestImageLayoutInput&)> present;
    std::function<vulkan::VulkanPresentationResult()> poll;
  };

  VideoOutService(memory::GuestMemory& memory, GpuRuntime& gpu_runtime,
                  kernel::EventQueueService& event_queues) noexcept;
  VideoOutService(const VideoOutService&) = delete;
  VideoOutService& operator=(const VideoOutService&) = delete;

  void SetPresentationCallbacksForTesting(PresentationCallbacks callbacks);
  [[nodiscard]] std::int32_t Open(std::int32_t user_id, std::int32_t bus_type,
                                  std::int32_t index);
  [[nodiscard]] std::int32_t Close(std::int32_t handle);
  [[nodiscard]] std::int32_t SetFlipRate(std::int32_t handle,
                                         std::int32_t rate);
  [[nodiscard]] std::int32_t RegisterBuffers(
      std::int32_t handle, std::int32_t start_index,
      std::span<const std::uint64_t> addresses,
      const VideoOutBufferAttribute& attribute);
  [[nodiscard]] std::int32_t RegisterBuffers2(
      std::int32_t handle, std::int32_t group_index, std::int32_t start_index,
      std::span<const VideoOutBufferEntry> buffers,
      const VideoOutBufferAttribute& attribute, std::uint32_t category,
      std::uint64_t option);
  [[nodiscard]] std::int32_t UnregisterBuffers(std::int32_t handle,
                                                std::int32_t group_index);
  [[nodiscard]] std::int32_t AddFlipEvent(kernel::KernelHandle queue,
                                          std::int32_t handle,
                                          std::uint64_t user_data);
  [[nodiscard]] std::int32_t DeleteFlipEvent(kernel::KernelHandle queue,
                                             std::int32_t handle);
  [[nodiscard]] std::int32_t SubmitFlip(std::int32_t handle,
                                        std::int32_t buffer_index,
                                        std::int32_t flip_mode,
                                        std::int64_t flip_arg);
  [[nodiscard]] std::int32_t GetFlipStatus(std::int32_t handle,
                                           VideoOutFlipStatus& status);
  [[nodiscard]] std::int32_t IsFlipPending(std::int32_t handle);
  [[nodiscard]] vulkan::VulkanPresentationResult
  last_presentation_result() const;

 private:
  struct BufferSlot {
    std::int32_t group_index = -1;
    VideoOutBufferEntry entry;
  };
  struct Group {
    bool occupied = false;
    std::uint32_t category = 0;
    VideoOutBufferAttribute attribute;
  };

  [[nodiscard]] std::int32_t Register(
      std::int32_t handle, std::int32_t group_index, std::int32_t start_index,
      std::span<const VideoOutBufferEntry> buffers,
      const VideoOutBufferAttribute& attribute, std::uint32_t category);
  [[nodiscard]] bool PollPending();
  [[nodiscard]] static bool IsSupportedFormat(std::uint64_t pixel_format,
                                              std::uint32_t& format) noexcept;
  [[nodiscard]] static bool IsSupportedTileMode(std::uint32_t raw,
                                                Prospero::TileMode& mode) noexcept;
  [[nodiscard]] bool IsOpenLocked(std::int32_t handle) const noexcept;
  void CompleteLocked();

  memory::GuestMemory& memory_;
  GpuRuntime& gpu_runtime_;
  kernel::EventQueueService& event_queues_;
  mutable std::mutex mutex_;
  bool open_ = false;
  std::int32_t flip_rate_ = 0;
  std::array<BufferSlot, kVideoOutMaximumBuffers> buffers_{};
  std::array<Group, kVideoOutMaximumGroups> groups_{};
  std::array<kernel::KernelHandle, kVideoOutMaximumBuffers> event_queues{};
  std::size_t event_queue_count_ = 0;
  VideoOutFlipStatus flip_status_;
  bool pending_ = false;
  std::int64_t pending_flip_arg_ = -1;
  vulkan::VulkanPresentationResult last_presentation_;
  PresentationCallbacks callbacks_;
};

}  // namespace kajps5::gpu
