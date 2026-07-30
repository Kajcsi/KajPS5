// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/pthread.h"

#include <algorithm>
#include <limits>
#include <vector>

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
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return false;
  }

  std::vector<std::string> wake_keys;
  {
    std::lock_guard lock(mutex_);
    for (auto& [handle, mutex] : mutexes_) {
      if (mutex.owner != *current_thread) {
        continue;
      }
      mutex.owner = kInvalidKernelHandle;
      mutex.recursion_count = 0;
      mutex.granted_waiters.erase(*current_thread);
      std::string wake_key;
      GrantNextMutexWaiterLocked(handle, mutex, wake_key);
      if (!wake_key.empty()) {
        wake_keys.push_back(std::move(wake_key));
      }
    }
  }
  for (const auto& wake_key : wake_keys) {
    (void)scheduler_.WakeBlockedThreads(wake_key, 1);
  }
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

PthreadAttributeCreateResult PthreadService::CreateMutexAttribute() {
  std::lock_guard lock(mutex_);
  if (next_mutex_attribute_id_ == 0 ||
      next_mutex_attribute_id_ >
          std::numeric_limits<std::uint64_t>::max() -
              kSyntheticMutexAttributeHandleBase) {
    return {KernelStatus::kNoResources, 0};
  }
  const auto handle =
      kSyntheticMutexAttributeHandleBase + next_mutex_attribute_id_++;
  mutex_attributes_.emplace(handle, PthreadMutexAttribute{});
  return {KernelStatus::kOk, handle};
}

KernelStatus PthreadService::DestroyMutexAttribute(std::uint64_t handle) {
  if (handle == 0) {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  return mutex_attributes_.erase(handle) == 1 ? KernelStatus::kOk
                                              : KernelStatus::kNotFound;
}

KernelStatus PthreadService::SetMutexAttributeType(std::uint64_t handle,
                                                   int type) {
  if (!IsMutexTypeValid(type)) {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  const auto found = mutex_attributes_.find(handle);
  if (found == mutex_attributes_.end()) {
    return KernelStatus::kNotFound;
  }
  found->second.type = type;
  return KernelStatus::kOk;
}

KernelStatus PthreadService::SetMutexAttributeProtocol(std::uint64_t handle,
                                                       int protocol) {
  if (!IsMutexProtocolValid(protocol)) {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  const auto found = mutex_attributes_.find(handle);
  if (found == mutex_attributes_.end()) {
    return KernelStatus::kNotFound;
  }
  found->second.protocol = protocol;
  return KernelStatus::kOk;
}

std::optional<PthreadMutexAttribute> PthreadService::GetMutexAttribute(
    std::uint64_t handle) const {
  std::lock_guard lock(mutex_);
  const auto found = mutex_attributes_.find(handle);
  if (found == mutex_attributes_.end()) {
    return std::nullopt;
  }
  return found->second;
}

PthreadMutexCreateResult PthreadService::CreateMutex(
    std::uint64_t attribute_handle, int static_type) {
  std::lock_guard lock(mutex_);
  if (static_type != 0 && !IsMutexTypeValid(static_type)) {
    return {KernelStatus::kInvalidArgument, 0};
  }
  if (next_mutex_id_ == 0 ||
      next_mutex_id_ > std::numeric_limits<std::uint64_t>::max() -
                           kSyntheticMutexHandleBase) {
    return {KernelStatus::kNoResources, 0};
  }

  PthreadMutexAttribute attributes;
  const auto supplied = mutex_attributes_.find(attribute_handle);
  if (supplied != mutex_attributes_.end()) {
    attributes = supplied->second;
  }
  if (static_type != 0) {
    attributes.type = static_type;
  }

  const auto handle = kSyntheticMutexHandleBase + next_mutex_id_++;
  MutexState mutex;
  mutex.type = attributes.type;
  mutex.protocol = attributes.protocol;
  mutexes_.emplace(handle, std::move(mutex));
  return {KernelStatus::kOk, handle};
}

KernelStatus PthreadService::DestroyMutex(std::uint64_t handle) {
  if (handle == 0) {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  const auto found = mutexes_.find(handle);
  if (found == mutexes_.end()) {
    return KernelStatus::kNotFound;
  }
  if (found->second.owner != kInvalidKernelHandle ||
      !found->second.waiters.empty() ||
      !found->second.granted_waiters.empty()) {
    return KernelStatus::kBusy;
  }
  mutexes_.erase(found);
  return KernelStatus::kOk;
}

KernelStatus PthreadService::LockMutex(std::uint64_t handle, bool try_only) {
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return KernelStatus::kBusy;
  }

  std::string wait_key;
  {
    std::lock_guard lock(mutex_);
    const auto found = mutexes_.find(handle);
    if (found == mutexes_.end()) {
      return KernelStatus::kNotFound;
    }
    auto& mutex = found->second;
    if (mutex.owner == kInvalidKernelHandle) {
      mutex.owner = *current_thread;
      mutex.recursion_count = 1;
      return KernelStatus::kOk;
    }
    if (mutex.owner == *current_thread) {
      if (mutex.granted_waiters.erase(*current_thread) != 0) {
        return KernelStatus::kOk;
      }
      if (mutex.type == kPthreadMutexRecursive) {
        if (mutex.recursion_count ==
            std::numeric_limits<std::uint32_t>::max()) {
          return KernelStatus::kNoResources;
        }
        ++mutex.recursion_count;
        return KernelStatus::kOk;
      }
      if (mutex.type == kPthreadMutexNormal && !try_only) {
        if (mutex.recursion_count ==
            std::numeric_limits<std::uint32_t>::max()) {
          return KernelStatus::kNoResources;
        }
        ++mutex.recursion_count;
        return KernelStatus::kOk;
      }
      if (mutex.type == kPthreadMutexAdaptive && !try_only) {
        return KernelStatus::kOk;
      }
      return try_only ? KernelStatus::kBusy
                      : KernelStatus::kInvalidArgument;
    }
    if (try_only) {
      return KernelStatus::kBusy;
    }
    if (std::find(mutex.waiters.begin(), mutex.waiters.end(),
                  *current_thread) == mutex.waiters.end()) {
      mutex.waiters.push_back(*current_thread);
    }
    wait_key = MutexWaitKey(handle, *current_thread);
  }

  if (scheduler_.BlockCurrent(wait_key)) {
    return KernelStatus::kWouldBlock;
  }

  std::lock_guard lock(mutex_);
  const auto found = mutexes_.find(handle);
  if (found != mutexes_.end()) {
    std::erase(found->second.waiters, *current_thread);
  }
  return KernelStatus::kBusy;
}

KernelStatus PthreadService::UnlockMutex(std::uint64_t handle) {
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return KernelStatus::kBusy;
  }

  std::string wake_key;
  {
    std::lock_guard lock(mutex_);
    const auto found = mutexes_.find(handle);
    if (found == mutexes_.end()) {
      return KernelStatus::kNotFound;
    }
    auto& mutex = found->second;
    if (mutex.owner != *current_thread) {
      return KernelStatus::kPermissionDenied;
    }
    if (mutex.recursion_count == 0) {
      return KernelStatus::kInvalidArgument;
    }
    --mutex.recursion_count;
    if (mutex.recursion_count != 0) {
      return KernelStatus::kOk;
    }
    mutex.owner = kInvalidKernelHandle;
    mutex.granted_waiters.erase(*current_thread);
    GrantNextMutexWaiterLocked(handle, mutex, wake_key);
  }
  if (!wake_key.empty()) {
    (void)scheduler_.WakeBlockedThreads(wake_key, 1);
  }
  return KernelStatus::kOk;
}

std::optional<PthreadMutexSnapshot> PthreadService::GetMutex(
    std::uint64_t handle) const {
  std::lock_guard lock(mutex_);
  const auto found = mutexes_.find(handle);
  if (found == mutexes_.end()) {
    return std::nullopt;
  }
  const auto& mutex = found->second;
  return PthreadMutexSnapshot{handle,
                              mutex.type,
                              mutex.protocol,
                              mutex.owner,
                              mutex.recursion_count,
                              mutex.waiters.size()};
}

std::string PthreadService::MutexWaitKey(std::uint64_t mutex_handle,
                                         KernelHandle thread_handle) {
  return "pthread-mutex:" + std::to_string(mutex_handle) + ":" +
         std::to_string(thread_handle);
}

bool PthreadService::IsMutexTypeValid(int type) noexcept {
  return type >= kPthreadMutexErrorCheck && type <= kPthreadMutexAdaptive;
}

bool PthreadService::IsMutexProtocolValid(int protocol) noexcept {
  return protocol >= 0 && protocol <= 2;
}

void PthreadService::GrantNextMutexWaiterLocked(
    std::uint64_t mutex_handle, MutexState& mutex, std::string& wake_key) {
  if (mutex.waiters.empty()) {
    return;
  }
  const auto next_thread = mutex.waiters.front();
  mutex.waiters.pop_front();
  mutex.owner = next_thread;
  mutex.recursion_count = 1;
  mutex.granted_waiters.insert(next_thread);
  wake_key = MutexWaitKey(mutex_handle, next_thread);
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
