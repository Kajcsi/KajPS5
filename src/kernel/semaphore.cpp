// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/semaphore.h"

#include <charconv>
#include <utility>

#include "kernel/guest_scheduler.h"

namespace kajps5::kernel {

Semaphore::Semaphore(std::string name, std::uint32_t attributes,
                     std::int32_t initial_count, std::int32_t maximum_count)
    : KernelObject(KernelObjectType::kSemaphore), name_(std::move(name)),
      attributes_(attributes), maximum_count_(maximum_count),
      count_(initial_count) {}

const std::string &Semaphore::name() const noexcept { return name_; }

std::uint32_t Semaphore::attributes() const noexcept { return attributes_; }

std::int32_t Semaphore::maximum_count() const noexcept {
  return maximum_count_;
}

std::int32_t Semaphore::count() const {
  std::lock_guard lock(mutex_);
  return count_;
}

KernelStatus Semaphore::Signal(std::int32_t count) {
  if (count <= 0) {
    return KernelStatus::kInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  if (count > maximum_count_ - count_) {
    return KernelStatus::kInvalidArgument;
  }
  count_ += count;
  return KernelStatus::kOk;
}

KernelStatus Semaphore::TryAcquire(std::int32_t count,
                                   std::int32_t &remaining_count) {
  if (count <= 0 || count > maximum_count_) {
    return KernelStatus::kInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  if (count_ < count) {
    remaining_count = count_;
    return KernelStatus::kBusy;
  }
  count_ -= count;
  remaining_count = count_;
  return KernelStatus::kOk;
}

SemaphoreService::SemaphoreService(HandleTable &handles,
                                   GuestScheduler &scheduler) noexcept
    : handles_(handles), scheduler_(scheduler) {}

SemaphoreCreateResult SemaphoreService::Create(std::string name,
                                               std::uint32_t attributes,
                                               std::int32_t initial_count,
                                               std::int32_t maximum_count) {
  if (name.size() > kMaximumSemaphoreNameLength ||
      !IsValidAttributes(attributes) || initial_count < 0 ||
      maximum_count <= 0 || initial_count > maximum_count) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  auto object = std::make_shared<Semaphore>(std::move(name), attributes,
                                            initial_count, maximum_count);
  const auto handle = handles_.Insert(std::move(object));
  if (!handle) {
    return {KernelStatus::kNoResources, kInvalidKernelHandle};
  }
  return {KernelStatus::kOk, *handle};
}

KernelStatus SemaphoreService::Delete(KernelHandle handle) {
  std::lock_guard wait_lock(wait_mutex_);
  if (!handles_.Remove(handle, KernelObjectType::kSemaphore)) {
    return KernelStatus::kNotFound;
  }
  (void)scheduler_.WakeBlockedThreads(MakeWaitKey(handle));
  return KernelStatus::kOk;
}

KernelStatus SemaphoreService::Signal(KernelHandle handle, std::int32_t count) {
  std::lock_guard wait_lock(wait_mutex_);
  const auto semaphore = Find(handle);
  if (!semaphore) {
    return KernelStatus::kNotFound;
  }

  const auto status = semaphore->Signal(count);
  if (status == KernelStatus::kOk) {
    (void)scheduler_.WakeBlockedThreads(MakeWaitKey(handle));
  }
  return status;
}

SemaphoreCountResult SemaphoreService::Wait(KernelHandle handle,
                                            std::int32_t count) {
  std::lock_guard wait_lock(wait_mutex_);
  const auto result = Poll(handle, count);
  if (result.status != KernelStatus::kBusy) {
    return result;
  }
  if (!scheduler_.BlockCurrent(MakeWaitKey(handle))) {
    return result;
  }
  return {KernelStatus::kWouldBlock, result.remaining_count};
}

SemaphoreCountResult SemaphoreService::Poll(KernelHandle handle,
                                            std::int32_t count) {
  const auto semaphore = Find(handle);
  if (!semaphore) {
    return {KernelStatus::kNotFound, 0};
  }

  std::int32_t remaining_count = 0;
  const auto status = semaphore->TryAcquire(count, remaining_count);
  return {status, remaining_count};
}

SemaphoreCountResult SemaphoreService::GetCount(KernelHandle handle) const {
  const auto semaphore = Find(handle);
  if (!semaphore) {
    return {KernelStatus::kNotFound, 0};
  }
  return {KernelStatus::kOk, semaphore->count()};
}

bool SemaphoreService::IsValidAttributes(std::uint32_t attributes) noexcept {
  return attributes == 0 || attributes == kSemaphoreThreadFifo ||
         attributes == kSemaphoreThreadPriority;
}

std::shared_ptr<Semaphore> SemaphoreService::Find(KernelHandle handle) const {
  return std::static_pointer_cast<Semaphore>(
      handles_.Find(handle, KernelObjectType::kSemaphore));
}

std::string SemaphoreService::MakeWaitKey(KernelHandle handle) {
  char digits[20]{};
  const auto converted =
      std::to_chars(digits, digits + sizeof(digits), handle, 16);
  return "semaphore:" +
         std::string(digits, static_cast<std::size_t>(converted.ptr - digits));
}

} // namespace kajps5::kernel
