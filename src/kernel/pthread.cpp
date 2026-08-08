// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/pthread.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "core/memory/guest_memory.h"

namespace kajps5::kernel {

PthreadService::PthreadService(GuestScheduler& scheduler,
                               KernelClockService& clock) noexcept
    : scheduler_(scheduler), clock_(clock) {}

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
    const auto completed = completed_condition_waits_.find(*current_thread);
    if (completed != completed_condition_waits_.end()) {
      const auto condition =
          conditions_.find(completed->second.condition_handle);
      if (condition != conditions_.end() &&
          condition->second.active_waiter_count != 0) {
        --condition->second.active_waiter_count;
      }
      const auto waiting_mutex =
          mutexes_.find(completed->second.mutex_handle);
      if (waiting_mutex != mutexes_.end() &&
          waiting_mutex->second.condition_waiter_count != 0) {
        --waiting_mutex->second.condition_waiter_count;
      }
      completed_condition_waits_.erase(completed);
    }
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
  return CreateMutexLocked(attribute_handle, static_type, std::nullopt);
}

bool PthreadService::ConfigureGuestMutexArena(memory::GuestMemory& memory,
                                              std::uint64_t address) {
  std::lock_guard lock(mutex_);
  const auto has_guest_mutex = std::any_of(
      mutexes_.begin(), mutexes_.end(), [](const auto& entry) {
        return entry.second.guest_object_slot.has_value();
      });
  if (guest_mutex_memory_ != nullptr || has_guest_mutex || address == 0 ||
      address > std::numeric_limits<std::uint64_t>::max() -
                    kPthreadMutexArenaSize ||
      !memory.CanAccess(
          address, kPthreadMutexArenaSize,
          memory::GuestMemoryProtection::kWrite) ||
      !memory.Fill(address, kPthreadMutexArenaSize, std::byte{0})) {
    return false;
  }
  guest_mutex_memory_ = &memory;
  guest_mutex_arena_address_ = address;
  guest_mutex_slots_.fill(false);
  return true;
}

bool PthreadService::ReleaseGuestMutexArena() {
  std::lock_guard lock(mutex_);
  const auto has_guest_mutex = std::any_of(
      mutexes_.begin(), mutexes_.end(), [](const auto& entry) {
        return entry.second.guest_object_slot.has_value();
      });
  if (guest_mutex_memory_ == nullptr || has_guest_mutex) {
    return false;
  }
  guest_mutex_memory_ = nullptr;
  guest_mutex_arena_address_ = 0;
  guest_mutex_slots_.fill(false);
  return true;
}

PthreadMutexCreateResult PthreadService::CreateGuestMutex(
    std::uint64_t attribute_handle, int static_type) {
  std::lock_guard lock(mutex_);
  if (guest_mutex_memory_ == nullptr) {
    return {KernelStatus::kNoResources, 0};
  }
  if (static_type != 0 && !IsMutexTypeValid(static_type)) {
    return {KernelStatus::kInvalidArgument, 0};
  }
  for (std::size_t slot = 0; slot < guest_mutex_slots_.size(); ++slot) {
    if (guest_mutex_slots_[slot]) {
      continue;
    }
    const auto slot_offset = slot * kPthreadMutexObjectSize;
    if (guest_mutex_arena_address_ >
        std::numeric_limits<std::uint64_t>::max() - slot_offset) {
      return {KernelStatus::kNoResources, 0};
    }
    const auto object_address = guest_mutex_arena_address_ + slot_offset;
    if (mutexes_.find(object_address) != mutexes_.end()) {
      continue;
    }
    if (!guest_mutex_memory_->Fill(object_address, kPthreadMutexObjectSize,
                                   std::byte{0})) {
      return {KernelStatus::kNoResources, 0};
    }
    const auto created =
        CreateMutexLocked(attribute_handle, static_type, slot);
    if (!created) {
      (void)guest_mutex_memory_->Fill(object_address,
                                      kPthreadMutexObjectSize, std::byte{0});
      return created;
    }
    const auto state = mutexes_.find(created.handle);
    if (state == mutexes_.end()) {
      (void)guest_mutex_memory_->Fill(object_address,
                                      kPthreadMutexObjectSize, std::byte{0});
      return {KernelStatus::kNoResources, 0};
    }
    const auto type = static_cast<std::uint32_t>(state->second.type);
    const auto protocol = static_cast<std::uint32_t>(state->second.protocol);
    if (!guest_mutex_memory_->Write(
            object_address + kPthreadMutexObjectTypeOffset,
            std::as_bytes(std::span(&type, std::size_t{1}))) ||
        !guest_mutex_memory_->Write(
            object_address + kPthreadMutexObjectProtocolOffset,
            std::as_bytes(std::span(&protocol, std::size_t{1})))) {
      mutexes_.erase(created.handle);
      (void)guest_mutex_memory_->Fill(object_address,
                                      kPthreadMutexObjectSize, std::byte{0});
      return {KernelStatus::kNoResources, 0};
    }
    guest_mutex_slots_[slot] = true;
    return {KernelStatus::kOk, object_address};
  }
  return {KernelStatus::kNoResources, 0};
}

PthreadMutexCreateResult PthreadService::CreateMutexLocked(
    std::uint64_t attribute_handle, int static_type,
    std::optional<std::size_t> guest_object_slot) {
  if (static_type != 0 && !IsMutexTypeValid(static_type)) {
    return {KernelStatus::kInvalidArgument, 0};
  }
  PthreadMutexAttribute attributes;
  const auto supplied = mutex_attributes_.find(attribute_handle);
  if (supplied != mutex_attributes_.end()) {
    attributes = supplied->second;
  }
  if (static_type != 0) {
    attributes.type = static_type;
  }

  MutexState mutex;
  mutex.type = attributes.type;
  mutex.protocol = attributes.protocol;
  mutex.guest_object_slot = guest_object_slot;
  if (guest_object_slot) {
    const auto slot = *guest_object_slot;
    const auto slot_offset = slot * kPthreadMutexObjectSize;
    if (slot >= guest_mutex_slots_.size() ||
        guest_mutex_arena_address_ >
            std::numeric_limits<std::uint64_t>::max() - slot_offset) {
      return {KernelStatus::kNoResources, 0};
    }
    const auto handle = guest_mutex_arena_address_ + slot_offset;
    const auto [entry, inserted] = mutexes_.emplace(handle, std::move(mutex));
    (void)entry;
    return inserted ? PthreadMutexCreateResult{KernelStatus::kOk, handle}
                    : PthreadMutexCreateResult{KernelStatus::kNoResources, 0};
  }

  const auto maximum_id = std::numeric_limits<std::uint64_t>::max() -
                          kSyntheticMutexHandleBase;
  if (next_mutex_id_ == 0 || next_mutex_id_ > maximum_id) {
    return {KernelStatus::kNoResources, 0};
  }
  auto candidate_id = next_mutex_id_;
  const auto maximum_probes = mutexes_.size();
  for (std::size_t probes = 0;;) {
    const auto handle = kSyntheticMutexHandleBase + candidate_id;
    if (mutexes_.find(handle) == mutexes_.end()) {
      const auto [entry, inserted] = mutexes_.emplace(handle, std::move(mutex));
      (void)entry;
      if (!inserted) {
        return {KernelStatus::kNoResources, 0};
      }
      next_mutex_id_ = candidate_id == maximum_id ? 0 : candidate_id + 1;
      return {KernelStatus::kOk, handle};
    }
    if (candidate_id == maximum_id || probes == maximum_probes) {
      break;
    }
    ++candidate_id;
    ++probes;
  }
  return {KernelStatus::kNoResources, 0};
}

KernelStatus PthreadService::CanDestroyMutex(std::uint64_t handle) const {
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
      !found->second.granted_waiters.empty() ||
      found->second.condition_waiter_count != 0) {
    return KernelStatus::kBusy;
  }
  return KernelStatus::kOk;
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
      !found->second.granted_waiters.empty() ||
      found->second.condition_waiter_count != 0) {
    return KernelStatus::kBusy;
  }
  if (found->second.guest_object_slot) {
    const auto slot = *found->second.guest_object_slot;
    if (slot >= guest_mutex_slots_.size() ||
        guest_mutex_arena_address_ >
            std::numeric_limits<std::uint64_t>::max() -
                slot * kPthreadMutexObjectSize) {
      return KernelStatus::kNoResources;
    }
    const auto object_address =
        guest_mutex_arena_address_ + slot * kPthreadMutexObjectSize;
    if (guest_mutex_memory_ == nullptr ||
        !guest_mutex_memory_->Fill(object_address, kPthreadMutexObjectSize,
                                   std::byte{0})) {
      return KernelStatus::kNoResources;
    }
    guest_mutex_slots_[slot] = false;
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
                              mutex.waiters.size(),
                              mutex.condition_waiter_count};
}

std::optional<bool> PthreadService::CurrentThreadOwnsMutex(
    std::uint64_t handle) const {
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return false;
  }
  std::lock_guard lock(mutex_);
  const auto found = mutexes_.find(handle);
  if (found == mutexes_.end()) {
    return std::nullopt;
  }
  return found->second.owner == *current_thread;
}

PthreadConditionCreateResult PthreadService::CreateCondition() {
  std::lock_guard lock(mutex_);
  if (next_condition_id_ == 0 ||
      next_condition_id_ >
          std::numeric_limits<std::uint64_t>::max() -
              kSyntheticConditionHandleBase) {
    return {KernelStatus::kNoResources, 0};
  }
  const auto handle = kSyntheticConditionHandleBase + next_condition_id_++;
  conditions_.emplace(handle, ConditionState{});
  return {KernelStatus::kOk, handle};
}

KernelStatus PthreadService::DestroyCondition(std::uint64_t handle) {
  if (handle == 0) {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  const auto found = conditions_.find(handle);
  if (found == conditions_.end()) {
    return KernelStatus::kNotFound;
  }
  if (found->second.active_waiter_count != 0 ||
      !found->second.waiters.empty()) {
    return KernelStatus::kBusy;
  }
  conditions_.erase(found);
  return KernelStatus::kOk;
}

KernelStatus PthreadService::WaitCondition(std::uint64_t condition_handle,
                                           std::uint64_t mutex_handle) {
  return WaitConditionInternal(condition_handle, mutex_handle, std::nullopt);
}

KernelStatus PthreadService::WaitConditionFor(
    std::uint64_t condition_handle, std::uint64_t mutex_handle,
    std::uint64_t timeout_microseconds) {
  constexpr auto kNanosecondsPerMicrosecond = std::uint64_t{1'000};
  const auto now = clock_.MonotonicNanoseconds();
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto remaining =
      timeout_microseconds > maximum / kNanosecondsPerMicrosecond
          ? maximum
          : timeout_microseconds * kNanosecondsPerMicrosecond;
  const auto deadline = remaining > maximum - now ? maximum : now + remaining;
  return WaitConditionInternal(condition_handle, mutex_handle, deadline);
}

KernelStatus PthreadService::WaitConditionUntilRealtime(
    std::uint64_t condition_handle, std::uint64_t mutex_handle,
    const KernelTimespec& deadline) {
  const auto monotonic_deadline = RealtimeDeadlineToMonotonic(deadline);
  if (!monotonic_deadline) {
    return KernelStatus::kInvalidArgument;
  }
  return WaitConditionInternal(condition_handle, mutex_handle,
                               *monotonic_deadline);
}

KernelStatus PthreadService::WaitConditionInternal(
    std::uint64_t condition_handle, std::uint64_t mutex_handle,
    std::optional<std::uint64_t> deadline_nanoseconds) {
  const auto current_thread = scheduler_.current_thread();
  if (!current_thread) {
    return KernelStatus::kBusy;
  }

  const auto wait_key = ConditionWaitKey(condition_handle, *current_thread);
  const auto timeout_wakeup =
      scheduler_.ConsumeCurrentThreadTimeout(wait_key);
  bool completing = false;
  {
    std::lock_guard lock(mutex_);
    auto completed = completed_condition_waits_.find(*current_thread);
    if (completed != completed_condition_waits_.end()) {
      if (completed->second.condition_handle != condition_handle ||
          completed->second.mutex_handle != mutex_handle) {
        return KernelStatus::kInvalidArgument;
      }
      completing = true;
    } else if (timeout_wakeup) {
      const auto condition = conditions_.find(condition_handle);
      if (condition == conditions_.end()) {
        return KernelStatus::kNotFound;
      }
      auto& waiters = condition->second.waiters;
      const auto waiter = std::find_if(
          waiters.begin(), waiters.end(),
          [current_thread, mutex_handle](const ConditionWaiter& candidate) {
            return candidate.thread == *current_thread &&
                   candidate.mutex_handle == mutex_handle;
          });
      if (waiter == waiters.end() || !waiter->deadline_nanoseconds ||
          *waiter->deadline_nanoseconds > clock_.MonotonicNanoseconds()) {
        return KernelStatus::kInvalidArgument;
      }
      auto timed_out_waiter = *waiter;
      timed_out_waiter.timed_out = true;
      completed_condition_waits_.insert_or_assign(*current_thread,
                                                   timed_out_waiter);
      waiters.erase(waiter);
      completing = true;
    }
  }
  if (completing) {
    const auto lock_status = LockMutex(mutex_handle, false);
    if (lock_status != KernelStatus::kOk) {
      return lock_status;
    }
    std::lock_guard lock(mutex_);
    const auto completed = completed_condition_waits_.find(*current_thread);
    if (completed == completed_condition_waits_.end()) {
      return KernelStatus::kInvalidArgument;
    }
    const auto condition = conditions_.find(condition_handle);
    const auto waiting_mutex = mutexes_.find(mutex_handle);
    if (condition == conditions_.end() || waiting_mutex == mutexes_.end() ||
        condition->second.active_waiter_count == 0 ||
        waiting_mutex->second.condition_waiter_count == 0) {
      return KernelStatus::kInvalidArgument;
    }
    --condition->second.active_waiter_count;
    --waiting_mutex->second.condition_waiter_count;
    const auto timed_out = completed->second.timed_out;
    completed_condition_waits_.erase(completed);
    return timed_out ? KernelStatus::kTimedOut : KernelStatus::kOk;
  }

  {
    std::lock_guard lock(mutex_);
    const auto condition = conditions_.find(condition_handle);
    if (condition == conditions_.end()) {
      return KernelStatus::kNotFound;
    }
    const auto waiting_mutex = mutexes_.find(mutex_handle);
    if (waiting_mutex == mutexes_.end()) {
      return KernelStatus::kNotFound;
    }
    if (waiting_mutex->second.owner != *current_thread) {
      return KernelStatus::kPermissionDenied;
    }
    if (waiting_mutex->second.recursion_count != 1) {
      return KernelStatus::kInvalidArgument;
    }
    condition->second.waiters.push_back(
        {*current_thread, condition_handle, mutex_handle,
         deadline_nanoseconds, false});
    ++condition->second.active_waiter_count;
    ++waiting_mutex->second.condition_waiter_count;
  }

  const auto unlock_status = UnlockMutex(mutex_handle);
  if (unlock_status != KernelStatus::kOk) {
    std::lock_guard lock(mutex_);
    const auto condition = conditions_.find(condition_handle);
    const auto waiting_mutex = mutexes_.find(mutex_handle);
    if (condition != conditions_.end()) {
      std::erase_if(condition->second.waiters,
                    [current_thread](const ConditionWaiter& waiter) {
                      return waiter.thread == *current_thread;
                    });
      if (condition->second.active_waiter_count != 0) {
        --condition->second.active_waiter_count;
      }
    }
    if (waiting_mutex != mutexes_.end() &&
        waiting_mutex->second.condition_waiter_count != 0) {
      --waiting_mutex->second.condition_waiter_count;
    }
    return unlock_status;
  }

  const auto blocked = deadline_nanoseconds
                           ? scheduler_.BlockCurrentUntil(wait_key,
                                                          *deadline_nanoseconds)
                           : scheduler_.BlockCurrent(wait_key);
  if (blocked) {
    return KernelStatus::kWouldBlock;
  }
  {
    std::lock_guard lock(mutex_);
    const auto condition = conditions_.find(condition_handle);
    const auto waiting_mutex = mutexes_.find(mutex_handle);
    if (condition != conditions_.end()) {
      std::erase_if(condition->second.waiters,
                    [current_thread](const ConditionWaiter& waiter) {
                      return waiter.thread == *current_thread;
                    });
      if (condition->second.active_waiter_count != 0) {
        --condition->second.active_waiter_count;
      }
    }
    if (waiting_mutex != mutexes_.end() &&
        waiting_mutex->second.condition_waiter_count != 0) {
      --waiting_mutex->second.condition_waiter_count;
    }
  }
  const auto relock_status = LockMutex(mutex_handle, false);
  return relock_status == KernelStatus::kOk ? KernelStatus::kBusy
                                            : relock_status;
}

KernelStatus PthreadService::SignalCondition(std::uint64_t handle,
                                             bool broadcast) {
  std::vector<std::string> wake_keys;
  {
    std::lock_guard lock(mutex_);
    const auto found = conditions_.find(handle);
    if (found == conditions_.end()) {
      return KernelStatus::kNotFound;
    }
    auto& waiters = found->second.waiters;
    const auto now = clock_.MonotonicNanoseconds();
    while (!waiters.empty()) {
      auto waiter = waiters.front();
      waiters.pop_front();
      if (waiter.deadline_nanoseconds &&
          *waiter.deadline_nanoseconds <= now) {
        waiter.timed_out = true;
        completed_condition_waits_.insert_or_assign(waiter.thread, waiter);
        wake_keys.push_back(ConditionWaitKey(handle, waiter.thread));
        continue;
      }
      completed_condition_waits_[waiter.thread] = waiter;
      wake_keys.push_back(ConditionWaitKey(handle, waiter.thread));
      if (!broadcast) {
        break;
      }
    }
  }
  for (const auto& wake_key : wake_keys) {
    (void)scheduler_.WakeBlockedThreads(wake_key, 1);
  }
  return KernelStatus::kOk;
}

std::optional<std::uint64_t> PthreadService::RealtimeDeadlineToMonotonic(
    const KernelTimespec& deadline) const {
  constexpr auto kNanosecondsPerSecond = std::uint64_t{1'000'000'000};
  if (deadline.seconds < 0 || deadline.nanoseconds < 0 ||
      deadline.nanoseconds >=
          static_cast<std::int64_t>(kNanosecondsPerSecond)) {
    return std::nullopt;
  }

  const auto now_result = clock_.ClockGettime(kClockRealtime);
  if (!now_result) {
    return std::nullopt;
  }
  const auto& now = now_result.value;
  if (deadline.seconds < now.seconds ||
      (deadline.seconds == now.seconds &&
       deadline.nanoseconds <= now.nanoseconds)) {
    return clock_.MonotonicNanoseconds();
  }

  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t seconds_delta = 0;
  if (now.seconds >= 0) {
    seconds_delta = static_cast<std::uint64_t>(deadline.seconds - now.seconds);
  } else {
    const auto negative_now =
        static_cast<std::uint64_t>(-(now.seconds + 1)) + 1;
    const auto target_seconds =
        static_cast<std::uint64_t>(deadline.seconds);
    seconds_delta = target_seconds > maximum - negative_now
                        ? maximum
                        : target_seconds + negative_now;
  }

  std::uint64_t nanoseconds_delta = 0;
  if (deadline.nanoseconds < now.nanoseconds) {
    --seconds_delta;
    nanoseconds_delta =
        kNanosecondsPerSecond -
        static_cast<std::uint64_t>(now.nanoseconds - deadline.nanoseconds);
  } else {
    nanoseconds_delta =
        static_cast<std::uint64_t>(deadline.nanoseconds - now.nanoseconds);
  }

  std::uint64_t remaining = maximum;
  if (seconds_delta <= maximum / kNanosecondsPerSecond) {
    remaining = seconds_delta * kNanosecondsPerSecond;
    remaining = nanoseconds_delta > maximum - remaining
                    ? maximum
                    : remaining + nanoseconds_delta;
  }
  const auto monotonic_now = clock_.MonotonicNanoseconds();
  return remaining > maximum - monotonic_now ? maximum
                                              : monotonic_now + remaining;
}

std::optional<PthreadConditionSnapshot> PthreadService::GetCondition(
    std::uint64_t handle) const {
  std::lock_guard lock(mutex_);
  const auto found = conditions_.find(handle);
  if (found == conditions_.end()) {
    return std::nullopt;
  }
  return PthreadConditionSnapshot{handle, found->second.waiters.size(),
                                  found->second.active_waiter_count};
}

std::string PthreadService::MutexWaitKey(std::uint64_t mutex_handle,
                                         KernelHandle thread_handle) {
  return "pthread-mutex:" + std::to_string(mutex_handle) + ":" +
         std::to_string(thread_handle);
}

std::string PthreadService::ConditionWaitKey(
    std::uint64_t condition_handle, KernelHandle thread_handle) {
  return "pthread-condition:" + std::to_string(condition_handle) + ":" +
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
