// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kernel/process_lifecycle.h"

namespace kajps5::kernel {

void ProcessLifecycleService::ResetEnvironment() {
  std::lock_guard lock(mutex_);
  environment_ = {};
}

KernelStatus ProcessLifecycleService::InitializeEnvironment(
    std::int32_t argc, std::uint64_t argv_address,
    std::uint64_t envp_address) {
  if (argc < 0 || argv_address == 0 || envp_address == 0) {
    return KernelStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  environment_ = {true, argc, argv_address, envp_address};
  return KernelStatus::kOk;
}

GuestProcessEnvironment ProcessLifecycleService::environment() const {
  std::lock_guard lock(mutex_);
  return environment_;
}

KernelStatus ProcessLifecycleService::RegisterAtexit(std::uint64_t function) {
  if (function == 0) {
    return KernelStatus::kOk;
  }
  std::lock_guard lock(mutex_);
  if (atexit_callbacks_.size() + cxa_destructors_.size() >=
      kMaximumProcessExitCallbacks) {
    return KernelStatus::kNoResources;
  }
  atexit_callbacks_.push_back(function);
  return KernelStatus::kOk;
}

KernelStatus ProcessLifecycleService::RegisterCxaDestructor(
    GuestCxaDestructor destructor) {
  if (destructor.function == 0) {
    return KernelStatus::kOk;
  }
  std::lock_guard lock(mutex_);
  if (atexit_callbacks_.size() + cxa_destructors_.size() >=
      kMaximumProcessExitCallbacks) {
    return KernelStatus::kNoResources;
  }
  cxa_destructors_.push_back(destructor);
  return KernelStatus::kOk;
}

std::vector<std::uint64_t>
ProcessLifecycleService::PendingAtexitCallbacks() const {
  std::lock_guard lock(mutex_);
  return {atexit_callbacks_.rbegin(), atexit_callbacks_.rend()};
}

std::vector<GuestCxaDestructor>
ProcessLifecycleService::PendingCxaDestructors(
    std::optional<std::uint64_t> module) const {
  std::lock_guard lock(mutex_);
  std::vector<GuestCxaDestructor> result;
  result.reserve(cxa_destructors_.size());
  for (auto destructor = cxa_destructors_.rbegin();
       destructor != cxa_destructors_.rend(); ++destructor) {
    if (!module || destructor->module == *module) {
      result.push_back(*destructor);
    }
  }
  return result;
}

std::size_t ProcessLifecycleService::callback_count() const {
  std::lock_guard lock(mutex_);
  return atexit_callbacks_.size() + cxa_destructors_.size();
}

void ProcessLifecycleService::RequestExit(std::int32_t status) {
  std::lock_guard lock(mutex_);
  if (!exit_request_.requested) {
    exit_request_ = {true, status};
  }
}

GuestProcessExitRequest ProcessLifecycleService::exit_request() const {
  std::lock_guard lock(mutex_);
  return exit_request_;
}

}  // namespace kajps5::kernel
