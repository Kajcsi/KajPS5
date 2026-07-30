// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cpu/host_executable_buffer.h"

#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif

namespace kajps5::cpu {
namespace {

void* AllocateWritable(std::size_t size) noexcept {
#if defined(_WIN32)
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__unix__) || defined(__APPLE__)
  auto* allocation =
      mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
           -1, 0);
  return allocation == MAP_FAILED ? nullptr : allocation;
#else
  (void)size;
  return nullptr;
#endif
}

bool MakeExecutable(void* address, std::size_t size) noexcept {
#if defined(_WIN32)
  DWORD old_protection = 0;
  if (VirtualProtect(address, size, PAGE_EXECUTE_READ, &old_protection) == 0) {
    return false;
  }
  return FlushInstructionCache(GetCurrentProcess(), address, size) != 0;
#elif defined(__unix__) || defined(__APPLE__)
  if (mprotect(address, size, PROT_READ | PROT_EXEC) != 0) {
    return false;
  }
  auto* begin = static_cast<char*>(address);
  __builtin___clear_cache(begin, begin + size);
  return true;
#else
  (void)address;
  (void)size;
  return false;
#endif
}

void FreeAllocation(void* address, std::size_t size) noexcept {
  if (address == nullptr) {
    return;
  }
#if defined(_WIN32)
  (void)size;
  (void)VirtualFree(address, 0, MEM_RELEASE);
#elif defined(__unix__) || defined(__APPLE__)
  (void)munmap(address, size);
#else
  (void)size;
#endif
}

}  // namespace

HostExecutableBuffer::HostExecutableBuffer(std::size_t size) noexcept
    : address_(size == 0 ? nullptr : AllocateWritable(size)), size_(size) {}

HostExecutableBuffer::~HostExecutableBuffer() {
  FreeAllocation(address_, size_);
}

bool HostExecutableBuffer::allocated() const noexcept {
  return address_ != nullptr;
}

bool HostExecutableBuffer::Write(std::span<const std::byte> bytes,
                                 std::size_t offset) noexcept {
  if (sealed_ || address_ == nullptr || offset > size_ ||
      bytes.size() > size_ - offset) {
    return false;
  }
  std::memcpy(static_cast<std::byte*>(address_) + offset, bytes.data(),
              bytes.size());
  return true;
}

bool HostExecutableBuffer::Seal() noexcept {
  if (sealed_ || address_ == nullptr || !MakeExecutable(address_, size_)) {
    return false;
  }
  sealed_ = true;
  return true;
}

void* HostExecutableBuffer::address() const noexcept { return address_; }

}  // namespace kajps5::cpu
