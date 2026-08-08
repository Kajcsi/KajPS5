// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loader/static_tls_instance.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace kajps5::loader {
namespace {

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

bool AlignUp(std::uint64_t value, std::uint64_t alignment,
             std::uint64_t& result) noexcept {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    return false;
  }
  const auto mask = alignment - 1;
  if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
    return false;
  }
  result = (value + mask) & ~mask;
  return true;
}

bool WriteUInt64(memory::GuestMemory& memory, std::uint64_t address,
                 std::uint64_t value) noexcept {
  std::array<std::byte, sizeof(value)> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
  return memory.Write(address, bytes);
}

}  // namespace

StaticTlsInstanceResult CreateStaticTlsInstance(
    memory::GuestMemory& memory, const StaticTlsLayout& layout,
    std::span<const StaticTlsTemplateModule> modules,
    std::uint64_t search_start) {
  StaticTlsInstanceResult result;
  if (!memory.host_mapped() || layout.module_count() == 0 ||
      layout.module_count() != modules.size()) {
    result.status = StaticTlsInstanceStatus::kInvalidArgument;
    return result;
  }

  std::uint64_t maximum_module_id = 0;
  for (const auto& module : modules) {
    const auto registered = layout.FindModule(module.module_id);
    if (module.module_id == 0 || !registered ||
        registered->memory_size != module.memory_size ||
        registered->static_offset != module.static_offset ||
        module.memory_size == 0 ||
        module.initial_size > module.memory_size ||
        module.static_offset < module.memory_size ||
        module.static_offset > layout.total_size() ||
        module.module_id <= maximum_module_id) {
      result.status = StaticTlsInstanceStatus::kInvalidArgument;
      return result;
    }
    maximum_module_id = module.module_id;
    if (module.initial_size != 0 &&
        !memory.CanAccess(module.image_address, module.initial_size,
                          memory::GuestMemoryProtection::kRead)) {
      result.status = StaticTlsInstanceStatus::kInvalidArgument;
      return result;
    }
  }

  const auto thread_pointer_alignment =
      std::max(kStaticTlsThreadPointerAlignment, layout.maximum_alignment());
  if ((thread_pointer_alignment & (thread_pointer_alignment - 1)) != 0) {
    result.status = StaticTlsInstanceStatus::kInvalidArgument;
    return result;
  }
  const auto allocation_alignment =
      std::max(memory.mapping_granularity(), thread_pointer_alignment);

  std::uint64_t data_bytes = 0;
  std::uint64_t dtv_entries_bytes = 0;
  std::uint64_t content_bytes = 0;
  std::uint64_t allocation_size = 0;
  if (!AlignUp(layout.total_size(), thread_pointer_alignment,
               data_bytes) ||
      maximum_module_id >
          (std::numeric_limits<std::uint64_t>::max() -
           kStaticTlsDtvHeaderBytes) /
              sizeof(std::uint64_t) ||
      !Add(kStaticTlsDtvHeaderBytes,
           maximum_module_id * sizeof(std::uint64_t), dtv_entries_bytes) ||
      !Add(data_bytes, kStaticTlsThreadControlBlockBytes, content_bytes) ||
      !Add(content_bytes, dtv_entries_bytes, content_bytes) ||
      !AlignUp(content_bytes, allocation_alignment, allocation_size)) {
    result.status = StaticTlsInstanceStatus::kOverflow;
    return result;
  }

  const auto allocation =
      memory.FindUnmappedRange(search_start, allocation_size, allocation_alignment);
  if (!allocation ||
      *allocation > std::numeric_limits<std::uint64_t>::max() - data_bytes) {
    result.status = StaticTlsInstanceStatus::kAllocationFailed;
    return result;
  }
  constexpr auto read_write = memory::GuestMemoryProtection::kRead |
                              memory::GuestMemoryProtection::kWrite;
  if (!memory.Map(*allocation, allocation_size, read_write) ||
      !memory.Fill(*allocation, allocation_size, std::byte{0})) {
    if (memory.IsMapped(*allocation, allocation_size)) {
      (void)memory.Unmap(*allocation, allocation_size);
    }
    result.status = StaticTlsInstanceStatus::kAllocationFailed;
    return result;
  }

  const auto thread_pointer = *allocation + data_bytes;
  const auto dtv_address = thread_pointer + kStaticTlsThreadControlBlockBytes;
  bool failed = false;
  StaticTlsInstanceStatus failure_status = StaticTlsInstanceStatus::kWriteFailed;
  for (const auto& module : modules) {
    const auto destination = thread_pointer - module.static_offset;
    if (module.initial_size != 0 &&
        !memory.Copy(destination, module.image_address, module.initial_size)) {
      failed = true;
      failure_status = StaticTlsInstanceStatus::kCopyFailed;
      break;
    }
  }
  if (!failed &&
      (!WriteUInt64(memory, thread_pointer, thread_pointer) ||
       !WriteUInt64(memory,
                    thread_pointer + sizeof(std::uint64_t), dtv_address) ||
       !WriteUInt64(memory, thread_pointer + 2 * sizeof(std::uint64_t),
                    thread_pointer) ||
       !WriteUInt64(memory, thread_pointer + 0x28, kStaticTlsStackGuard) ||
       !WriteUInt64(memory, thread_pointer + 0x60, thread_pointer) ||
       !WriteUInt64(memory, dtv_address, kStaticTlsDtvGeneration) ||
       !WriteUInt64(memory, dtv_address + sizeof(std::uint64_t),
                    maximum_module_id))) {
    failed = true;
  }
  for (std::size_t index = 0; !failed && index < modules.size(); ++index) {
    const auto& module = modules[index];
    if (!WriteUInt64(memory,
                     dtv_address + kStaticTlsDtvHeaderBytes +
                         (module.module_id - 1) * sizeof(std::uint64_t),
                     thread_pointer - module.static_offset)) {
      failed = true;
      break;
    }
  }
  if (failed) {
    (void)memory.Unmap(*allocation, allocation_size);
    result.status = failure_status;
    return result;
  }

  result.instance.allocation_address = *allocation;
  result.instance.allocation_size = allocation_size;
  result.instance.thread_pointer = thread_pointer;
  result.instance.dtv_address = dtv_address;
  return result;
}

bool DestroyStaticTlsInstance(memory::GuestMemory& memory,
                              const StaticTlsInstance& instance) noexcept {
  if (instance.allocation_address == 0 || instance.allocation_size == 0) {
    return true;
  }
  return memory.Unmap(instance.allocation_address, instance.allocation_size);
}

std::string_view StaticTlsInstanceStatusName(
    StaticTlsInstanceStatus status) noexcept {
  switch (status) {
    case StaticTlsInstanceStatus::kOk:
      return "ok";
    case StaticTlsInstanceStatus::kInvalidArgument:
      return "invalid-argument";
    case StaticTlsInstanceStatus::kOverflow:
      return "overflow";
    case StaticTlsInstanceStatus::kAllocationFailed:
      return "allocation-failed";
    case StaticTlsInstanceStatus::kCopyFailed:
      return "copy-failed";
    case StaticTlsInstanceStatus::kWriteFailed:
      return "write-failed";
  }
  return "unknown";
}

}  // namespace kajps5::loader
