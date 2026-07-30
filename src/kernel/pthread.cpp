// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/pthread.h"

#include <limits>

namespace kajps5::kernel {

PthreadService::PthreadService(GuestScheduler& scheduler) noexcept
    : scheduler_(scheduler) {}

PthreadAttributeCreateResult PthreadService::CreateAttribute() {
  std::lock_guard lock(mutex_);
  if (next_attribute_id_ == 0 ||
      next_attribute_id_ >
          std::numeric_limits<std::uint64_t>::max() -
              kSyntheticAttributeHandleBase) {
    return {KernelStatus::kNoResources, 0};
  }

  const auto handle = kSyntheticAttributeHandleBase + next_attribute_id_++;
  attributes_.emplace(handle, PthreadAttribute{});
  return {KernelStatus::kOk, handle};
}

KernelStatus PthreadService::DestroyAttribute(std::uint64_t handle) {
  if (handle == 0) {
    return KernelStatus::kInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  return attributes_.erase(handle) == 1 ? KernelStatus::kOk
                                        : KernelStatus::kNotFound;
}

KernelStatus PthreadService::SetAttributeStackSize(
    std::uint64_t handle, std::uint64_t stack_size) {
  if (stack_size < kPthreadMinimumStackSize) {
    return KernelStatus::kInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  const auto found = attributes_.find(handle);
  if (found == attributes_.end()) {
    return KernelStatus::kNotFound;
  }
  found->second.stack_size = stack_size;
  return KernelStatus::kOk;
}

std::optional<PthreadAttribute> PthreadService::GetAttribute(
    std::uint64_t handle) const {
  std::lock_guard lock(mutex_);
  const auto found = attributes_.find(handle);
  if (found == attributes_.end()) {
    return std::nullopt;
  }
  return found->second;
}

PthreadThreadCreateResult PthreadService::CreateThread(
    std::string name, std::uint64_t attribute_handle,
    std::uint64_t entry_address, std::uint64_t argument) {
  if (entry_address == 0) {
    return {KernelStatus::kInvalidArgument, kInvalidKernelHandle};
  }

  PthreadAttribute attributes;
  if (attribute_handle != 0) {
    std::lock_guard lock(mutex_);
    const auto found = attributes_.find(attribute_handle);
    if (found != attributes_.end()) {
      attributes = found->second;
    }
  }

  const auto created = scheduler_.CreateThread(
      std::move(name), attributes.priority, entry_address, argument);
  if (!created) {
    return {created.status, kInvalidKernelHandle};
  }

  std::lock_guard lock(mutex_);
  threads_.emplace(created.handle,
                   PthreadThreadSnapshot{created.handle, attributes});
  return {KernelStatus::kOk, created.handle};
}

bool PthreadService::DiscardReadyThread(KernelHandle handle) {
  if (!scheduler_.DiscardReadyThread(handle)) {
    return false;
  }

  std::lock_guard lock(mutex_);
  threads_.erase(handle);
  specific_values_.erase(handle);
  return true;
}

GuestThreadJoinResult PthreadService::JoinThread(KernelHandle handle) {
  return scheduler_.JoinThread(handle);
}

bool PthreadService::ExitCurrent(std::uint64_t exit_value) {
  return scheduler_.ExitCurrent(exit_value);
}

std::optional<PthreadThreadSnapshot> PthreadService::GetThread(
    KernelHandle handle) const {
  std::lock_guard lock(mutex_);
  const auto found = threads_.find(handle);
  if (found == threads_.end()) {
    return std::nullopt;
  }
  return found->second;
}

PthreadKeyCreateResult PthreadService::CreateKey(
    std::uint64_t destructor_address) {
  std::lock_guard lock(mutex_);
  for (std::size_t index = 0; index < keys_.size(); ++index) {
    if (keys_[index]) {
      continue;
    }
    keys_[index] = KeyState{destructor_address};
    return {KernelStatus::kOk, static_cast<std::uint32_t>(index)};
  }
  return {KernelStatus::kNoResources, 0};
}

KernelStatus PthreadService::DeleteKey(std::uint32_t key) {
  std::lock_guard lock(mutex_);
  if (key >= keys_.size() || !keys_[key]) {
    return KernelStatus::kNotFound;
  }

  keys_[key].reset();
  for (auto values = specific_values_.begin();
       values != specific_values_.end();) {
    values->second.erase(key);
    if (values->second.empty()) {
      values = specific_values_.erase(values);
    } else {
      ++values;
    }
  }
  return KernelStatus::kOk;
}

KernelStatus PthreadService::SetSpecific(std::uint32_t key,
                                         std::uint64_t value) {
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return KernelStatus::kBusy;
  }

  std::lock_guard lock(mutex_);
  if (key >= keys_.size() || !keys_[key]) {
    return KernelStatus::kNotFound;
  }
  specific_values_[*current_thread][key] = value;
  return KernelStatus::kOk;
}

PthreadSpecificResult PthreadService::GetSpecific(std::uint32_t key) const {
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return {KernelStatus::kBusy, 0};
  }

  std::lock_guard lock(mutex_);
  if (key >= keys_.size() || !keys_[key]) {
    return {KernelStatus::kNotFound, 0};
  }
  const auto thread_values = specific_values_.find(*current_thread);
  if (thread_values == specific_values_.end()) {
    return {KernelStatus::kOk, 0};
  }
  const auto value = thread_values->second.find(key);
  return {KernelStatus::kOk,
          value == thread_values->second.end() ? 0 : value->second};
}

std::size_t PthreadService::attribute_count() const {
  std::lock_guard lock(mutex_);
  return attributes_.size();
}

std::size_t PthreadService::key_count() const {
  std::lock_guard lock(mutex_);
  std::size_t count = 0;
  for (const auto& key : keys_) {
    if (key) {
      ++count;
    }
  }
  return count;
}

}  // namespace kajps5::kernel
