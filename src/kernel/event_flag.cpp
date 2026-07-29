// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/event_flag.h"

#include <utility>

namespace kajps5::kernel {

EventFlag::EventFlag(std::string name, std::uint32_t attributes,
                     std::uint64_t initial_pattern)
    : KernelObject(KernelObjectType::kEventFlag), name_(std::move(name)),
      attributes_(attributes), bits_(initial_pattern) {}

const std::string &EventFlag::name() const noexcept { return name_; }

std::uint32_t EventFlag::attributes() const noexcept { return attributes_; }

std::uint64_t EventFlag::bits() const {
  std::lock_guard lock(mutex_);
  return bits_;
}

void EventFlag::Set(std::uint64_t pattern) {
  std::lock_guard lock(mutex_);
  bits_ |= pattern;
}

void EventFlag::Clear(std::uint64_t mask) {
  std::lock_guard lock(mutex_);
  bits_ &= mask;
}

EventFlagStateResult EventFlag::Poll(std::uint64_t pattern,
                                     EventFlagWaitCondition condition,
                                     EventFlagClearMode clear_mode) {
  std::lock_guard lock(mutex_);

  EventFlagStateResult result;
  result.observed_pattern = bits_;
  result.satisfied = condition == EventFlagWaitCondition::kAll
                         ? (bits_ & pattern) == pattern
                         : (bits_ & pattern) != 0;
  if (!result.satisfied) {
    return result;
  }

  switch (clear_mode) {
  case EventFlagClearMode::kNone:
    break;
  case EventFlagClearMode::kAll:
    bits_ = 0;
    break;
  case EventFlagClearMode::kPattern:
    bits_ &= ~pattern;
    break;
  }
  return result;
}

EventFlagService::EventFlagService(HandleTable &handles) noexcept
    : handles_(handles) {}

EventFlagCreateResult EventFlagService::Create(std::string name,
                                               std::uint32_t attributes,
                                               std::uint64_t initial_pattern) {
  if (name.size() > kMaximumEventFlagNameLength ||
      !IsValidAttributes(attributes)) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  auto object =
      std::make_shared<EventFlag>(std::move(name), attributes, initial_pattern);
  const auto handle = handles_.Insert(std::move(object));
  if (!handle) {
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }
  return {KernelStatus::kOk, *handle};
}

KernelStatus EventFlagService::Delete(KernelHandle handle) {
  return handles_.Remove(handle, KernelObjectType::kEventFlag)
             ? KernelStatus::kOk
             : KernelStatus::kNotFound;
}

KernelStatus EventFlagService::Set(KernelHandle handle, std::uint64_t pattern) {
  const auto event_flag = Find(handle);
  if (!event_flag) {
    return KernelStatus::kNotFound;
  }
  event_flag->Set(pattern);
  return KernelStatus::kOk;
}

KernelStatus EventFlagService::Clear(KernelHandle handle, std::uint64_t mask) {
  const auto event_flag = Find(handle);
  if (!event_flag) {
    return KernelStatus::kNotFound;
  }
  event_flag->Clear(mask);
  return KernelStatus::kOk;
}

EventFlagPollResult EventFlagService::Poll(KernelHandle handle,
                                           std::uint64_t pattern,
                                           std::uint32_t wait_mode) {
  const auto event_flag = Find(handle);
  if (!event_flag) {
    return {KernelStatus::kNotFound, 0};
  }

  EventFlagWaitCondition condition;
  EventFlagClearMode clear_mode;
  if (pattern == 0 || !DecodeWaitMode(wait_mode, condition, clear_mode)) {
    return {KernelStatus::kInvalidArgument, 0};
  }

  const auto result = event_flag->Poll(pattern, condition, clear_mode);
  return {result.satisfied ? KernelStatus::kOk : KernelStatus::kBusy,
          result.observed_pattern};
}

bool EventFlagService::IsValidAttributes(std::uint32_t attributes) noexcept {
  const auto queue_mode = attributes & 0x0fU;
  const auto thread_mode = attributes & 0xf0U;
  const auto queue_valid = queue_mode == 0 ||
                           queue_mode == kEventFlagThreadFifo ||
                           queue_mode == kEventFlagThreadPriority;
  const auto thread_valid = thread_mode == 0 ||
                            thread_mode == kEventFlagSingle ||
                            thread_mode == kEventFlagMulti;
  return queue_valid && thread_valid && (attributes & ~0x33U) == 0;
}

bool EventFlagService::DecodeWaitMode(std::uint32_t wait_mode,
                                      EventFlagWaitCondition &condition,
                                      EventFlagClearMode &clear_mode) noexcept {
  switch (wait_mode & 0x0fU) {
  case kEventFlagWaitAll:
    condition = EventFlagWaitCondition::kAll;
    break;
  case kEventFlagWaitAny:
    condition = EventFlagWaitCondition::kAny;
    break;
  default:
    return false;
  }

  switch (wait_mode & 0xf0U) {
  case 0:
    clear_mode = EventFlagClearMode::kNone;
    break;
  case kEventFlagClearAll:
    clear_mode = EventFlagClearMode::kAll;
    break;
  case kEventFlagClearPattern:
    clear_mode = EventFlagClearMode::kPattern;
    break;
  default:
    return false;
  }
  return (wait_mode & ~0x33U) == 0;
}

std::shared_ptr<EventFlag> EventFlagService::Find(KernelHandle handle) const {
  return std::static_pointer_cast<EventFlag>(
      handles_.Find(handle, KernelObjectType::kEventFlag));
}

} // namespace kajps5::kernel
