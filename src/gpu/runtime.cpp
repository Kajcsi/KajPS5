// Copyright (C) 2026 KajPS5 contributors
// Adapted from KytyPS5 src/libs/agc.cpp and
// src/graphics/guest_gpu/pm4.h at
// a65d17a5d689257a35644e01e9d15539361f0bf0.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-only

#include "gpu/runtime.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <new>
#include <vector>

#include "core/memory/guest_memory.h"

namespace kajps5::gpu {
namespace {

constexpr std::uint32_t kPm4Type3 = 0xc0000000U;
constexpr std::uint32_t kPm4LengthMask = 0x3fffU;
constexpr std::uint32_t kPm4RegisterMask = 0x3fU;
constexpr std::uint32_t kPm4NopOpcode = 0x10U;
constexpr std::uint32_t kPm4SetBaseOpcode = 0x11U;
constexpr std::uint32_t kPm4IndexBufferSizeOpcode = 0x13U;
constexpr std::uint32_t kPm4DispatchDirectOpcode = 0x15U;
constexpr std::uint32_t kPm4DispatchIndirectOpcode = 0x16U;
constexpr std::uint32_t kPm4SetPredicationOpcode = 0x20U;
constexpr std::uint32_t kPm4IndexBaseOpcode = 0x26U;
constexpr std::uint32_t kPm4DrawIndexOpcode = 0x27U;
constexpr std::uint32_t kPm4DrawIndexAutoOpcode = 0x2dU;
constexpr std::uint32_t kPm4NumInstancesOpcode = 0x2fU;
constexpr std::uint32_t kPm4DrawIndexOffsetOpcode = 0x35U;
constexpr std::uint32_t kPm4IndirectBufferOpcode = 0x3fU;
constexpr std::uint32_t kPm4EventWriteOpcode = 0x46U;
constexpr std::uint32_t kPm4RewindOpcode = 0x59U;
constexpr std::uint32_t kPm4SetContextRegisterOpcode = 0x69U;
constexpr std::uint32_t kPm4SetShRegisterOpcode = 0x76U;
constexpr std::uint32_t kPm4SetUconfigRegisterOpcode = 0x79U;
constexpr std::uint32_t kPm4SetUconfigRegisterIndexOpcode = 0x7aU;
constexpr std::uint32_t kPm4GetLodStatsOpcode = 0x8eU;
constexpr std::uint32_t kPm4WaitMemory32Register = 0x0aU;
constexpr std::uint32_t kPm4WaitMemory64Register = 0x16U;
constexpr std::uint32_t kPm4WriteDataRegister = 0x15U;
constexpr std::uint32_t kPm4ReleaseMemoryRegister = 0x18U;
constexpr std::uint32_t kVgtIndexTypeRegister = 0x243U;
constexpr std::uint32_t kDirectDispatchModifierMask = 0xa038U;
constexpr std::uint32_t kDirectDispatchRequiredBits = 0x41U;

bool Add(std::uint64_t left, std::uint64_t right,
         std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

std::uint32_t MakePm4Header(std::uint32_t dword_count,
                           std::uint32_t opcode,
                           std::uint32_t packet_register) noexcept {
  return kPm4Type3 |
         (((dword_count - 2U) & kPm4LengthMask) << 16U) |
         ((opcode & 0xffU) << 8U) |
         ((packet_register & kPm4RegisterMask) << 2U);
}

std::uint32_t DrawIndexInitiator(std::uint64_t modifier) noexcept {
  return (modifier & (1ULL << 32U)) != 0
             ? 0
             : (static_cast<std::uint32_t>(modifier) >> 3U) & 0x20U;
}

std::uint32_t WaitPoll(std::uint32_t poll_cycles) noexcept {
  return std::min(poll_cycles >> 4U, 0xffffU);
}

std::uint32_t Wait32Control(std::uint32_t compare_function,
                            std::uint32_t operation,
                            std::uint32_t cache_policy) noexcept {
  return 0x10U | (compare_function & 0x7U) |
         ((operation & 0x3U) << 8U) | ((operation & 0xcU) << 4U) |
         ((cache_policy & 0x3U) << 25U);
}

std::uint32_t Wait64Control(std::uint32_t compare_function,
                            std::uint32_t operation,
                            std::uint32_t cache_policy) noexcept {
  return 0x10U | (compare_function & 0x7U) |
         ((operation & 0x1U) << 8U) | ((operation & 0x6U) << 5U) |
         ((cache_policy & 0x3U) << 25U);
}

std::uint32_t Read32(std::span<const std::byte> bytes) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

std::uint64_t Read64(std::span<const std::byte> bytes) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

void Write32(std::span<std::byte> bytes, std::size_t offset,
             std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void Write64(std::span<std::byte> bytes, std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

}  // namespace

GpuRuntime::GpuRuntime(memory::GuestMemory& memory,
                       GpuSubmissionSink* submission_sink,
                       kernel::EventQueueService* event_queues)
    : memory_(memory),
      shader_runtime_(memory_),
      resource_coherence_(GpuResourceCoherence::Create(memory_)),
      submission_queue_(*this),
      submission_history_(4096),
      event_effects_(event_queues,
                     submission_sink != nullptr ? *submission_sink
                                                : submission_history_),
      submission_effects_(memory, event_effects_),
      submission_sink_(&submission_effects_) {}

vulkan::VulkanInitializationResult GpuRuntime::InitializeVulkan(
    const vulkan::VulkanContextOptions& options) {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_context_ != nullptr) {
    vulkan::VulkanInitializationResult result;
    result.status = vulkan::VulkanContextStatus::kAlreadyInitialized;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanDiagnosticCode::kDuplicateInitialization, std::nullopt,
         VK_SUCCESS,
         "GpuRuntime already owns a Vulkan device context; the duplicate "
         "request was rejected without changing it"});
    return result;
  }

  auto created = vulkan::VulkanDeviceContext::Create(options);
  if (!created) {
    return std::move(created.initialization);
  }
  vulkan_context_ = std::move(created.context);
  return std::move(created.initialization);
}

vulkan::VulkanInitializationResult GpuRuntime::InitializeVulkan(
    vulkan::VulkanLoader loader, const vulkan::VulkanContextOptions& options) {
  std::lock_guard lock(vulkan_mutex_);
  if (vulkan_context_ != nullptr) {
    vulkan::VulkanInitializationResult result;
    result.status = vulkan::VulkanContextStatus::kAlreadyInitialized;
    result.diagnostics.push_back(
        {vulkan::VulkanDiagnosticSeverity::kError,
         vulkan::VulkanDiagnosticCode::kDuplicateInitialization, std::nullopt,
         VK_SUCCESS,
         "GpuRuntime already owns a Vulkan device context; the duplicate "
         "request was rejected without changing it"});
    return result;
  }

  auto created = vulkan::VulkanDeviceContext::Create(std::move(loader), options);
  if (!created) {
    return std::move(created.initialization);
  }
  vulkan_context_ = std::move(created.context);
  return std::move(created.initialization);
}

bool GpuRuntime::has_vulkan_context() const noexcept {
  std::lock_guard lock(vulkan_mutex_);
  return vulkan_context_ != nullptr;
}

vulkan::VulkanDeviceContext* GpuRuntime::vulkan_context() noexcept {
  std::lock_guard lock(vulkan_mutex_);
  return vulkan_context_.get();
}

const vulkan::VulkanDeviceContext* GpuRuntime::vulkan_context() const
    noexcept {
  std::lock_guard lock(vulkan_mutex_);
  return vulkan_context_.get();
}

ShaderMapResult GpuRuntime::CreateShader(std::uint64_t destination_address,
                                         std::uint64_t header_address,
                                         std::uint64_t code_address) {
  return shader_runtime_.CreateShader(destination_address, header_address,
                                      code_address);
}

std::optional<RegisteredShader> GpuRuntime::LookupRegisteredShader(
    std::uint64_t code_address) const {
  return shader_runtime_.Lookup(code_address);
}

ShaderCompileResult GpuRuntime::RecompileRegisteredShader(
    std::uint64_t code_address,
    const shader::recompiler::CompileOptions& options,
    shader::recompiler::CompileResult& result) {
  return shader_runtime_.Recompile(code_address, options, result);
}

GpuPacketResult GpuRuntime::AppendPacket(
    std::uint64_t command_buffer, std::span<const std::uint32_t> packet) {
  if (command_buffer == 0 || packet.size() < 2 ||
      packet.size() > kMaximumPm4PacketDwords) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }

  std::lock_guard lock(mutex_);
  std::uint64_t cursor_up_address = 0;
  std::uint64_t cursor_down_address = 0;
  std::uint64_t callback_address = 0;
  std::uint64_t reserved_address = 0;
  if (!Add(command_buffer, kAgcCommandBufferCursorUpOffset,
           cursor_up_address) ||
      !Add(command_buffer, kAgcCommandBufferCursorDownOffset,
           cursor_down_address) ||
      !Add(command_buffer, kAgcCommandBufferCallbackOffset,
           callback_address) ||
      !Add(command_buffer, kAgcCommandBufferReservedDwordsOffset,
           reserved_address)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  std::array<std::byte, sizeof(std::uint64_t)> cursor_up_bytes{};
  std::array<std::byte, sizeof(std::uint64_t)> cursor_down_bytes{};
  std::array<std::byte, sizeof(std::uint64_t)> callback_bytes{};
  std::array<std::byte, sizeof(std::uint32_t)> reserved_bytes{};
  if (!memory_.Read(cursor_up_address, cursor_up_bytes) ||
      !memory_.Read(cursor_down_address, cursor_down_bytes) ||
      !memory_.Read(callback_address, callback_bytes) ||
      !memory_.Read(reserved_address, reserved_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  const auto cursor_up = Read64(cursor_up_bytes);
  const auto cursor_down = Read64(cursor_down_bytes);
  const auto callback = Read64(callback_bytes);
  const auto reserved_dwords = Read32(reserved_bytes);
  if (cursor_up == 0 || cursor_down <= cursor_up ||
      (cursor_up & 3U) != 0 || (cursor_down & 3U) != 0) {
    return {GpuRuntimeStatus::kBufferTooSmall};
  }

  const auto available_bytes = cursor_down - cursor_up;
  const auto available_dwords = available_bytes / sizeof(std::uint32_t);
  const auto usable_dwords = available_dwords > reserved_dwords
                                 ? available_dwords - reserved_dwords
                                 : 0;
  if (packet.size() > usable_dwords) {
    return {callback != 0 ? GpuRuntimeStatus::kCallbackRequired
                          : GpuRuntimeStatus::kBufferTooSmall};
  }

  const auto packet_bytes_size =
      static_cast<std::uint64_t>(packet.size() * sizeof(std::uint32_t));
  std::uint64_t next_cursor = 0;
  if (!Add(cursor_up, packet_bytes_size, next_cursor) ||
      !memory_.CanAccess(cursor_up, packet_bytes_size,
                         memory::GuestMemoryProtection::kWrite) ||
      !memory_.CanAccess(cursor_up_address, sizeof(std::uint64_t),
                         memory::GuestMemoryProtection::kWrite)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }

  std::vector<std::byte> packet_bytes;
  try {
    packet_bytes.resize(static_cast<std::size_t>(packet_bytes_size));
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  for (std::size_t index = 0; index < packet.size(); ++index) {
    Write32(packet_bytes, index * sizeof(std::uint32_t), packet[index]);
  }
  std::array<std::byte, sizeof(std::uint64_t)> next_cursor_bytes{};
  Write64(next_cursor_bytes, next_cursor);
  if (!memory_.Write(cursor_up, packet_bytes) ||
      !memory_.Write(cursor_up_address, next_cursor_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }
  return {GpuRuntimeStatus::kOk, cursor_up};
}

GpuPacketResult GpuRuntime::WriteNop(std::uint64_t command_buffer,
                                     std::uint32_t dword_count) {
  if (dword_count < 2 || dword_count > kMaximumPm4PacketDwords) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  std::vector<std::uint32_t> packet;
  try {
    packet.assign(dword_count, 0);
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  packet[0] = MakePm4Header(dword_count, kPm4NopOpcode, 0);
  return AppendPacket(command_buffer, packet);
}

GpuPacketResult GpuRuntime::WriteDispatch(
    std::uint64_t command_buffer, std::uint32_t group_count_x,
    std::uint32_t group_count_y, std::uint32_t group_count_z,
    std::uint32_t modifier) {
  const std::array packet = {
      MakePm4Header(5, kPm4DispatchDirectOpcode, 0), group_count_x,
      group_count_y, group_count_z,
      (modifier & kDirectDispatchModifierMask) |
          kDirectDispatchRequiredBits};
  return AppendPacket(command_buffer, packet);
}

GpuPacketResult GpuRuntime::WriteAgcPacket(
    AgcPacketType type, std::span<const std::uint64_t> arguments) {
  if (arguments.empty()) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  const auto argument = [arguments](std::size_t index) noexcept {
    return index < arguments.size() ? arguments[index] : 0;
  };
  const auto command_buffer = argument(0);
  const auto append = [this, command_buffer](
                          std::initializer_list<std::uint32_t> words) {
    return AppendPacket(
        command_buffer,
        std::span<const std::uint32_t>(words.begin(), words.size()));
  };

  try {
    if (type == AgcPacketType::kSetShRegisterDirect ||
        type == AgcPacketType::kSetCxRegisterDirect ||
        type == AgcPacketType::kSetUcRegisterDirect) {
      const auto packed_register = argument(1);
      auto opcode = kPm4SetShRegisterOpcode;
      if (type == AgcPacketType::kSetCxRegisterDirect) {
        opcode = kPm4SetContextRegisterOpcode;
      } else if (type == AgcPacketType::kSetUcRegisterDirect) {
        opcode = kPm4SetUconfigRegisterOpcode;
      }
      return append({MakePm4Header(3, opcode, 0),
                     static_cast<std::uint32_t>(packed_register) & 0xffffU,
                     static_cast<std::uint32_t>(packed_register >> 32U)});
    }
    if (type == AgcPacketType::kSetIndexSize) {
      const auto index_size = static_cast<std::uint32_t>(argument(1));
      const auto cache_policy = static_cast<std::uint32_t>(argument(2));
      return append(
          {MakePm4Header(3, kPm4SetUconfigRegisterIndexOpcode, 0),
           0x20000000U | kVgtIndexTypeRegister,
           0x400U | (index_size & 0x3U) | ((cache_policy & 0x3U) << 6U)});
    }
    if (type == AgcPacketType::kSetIndexBuffer) {
      const auto address = argument(1);
      if ((address & 1U) != 0) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      return append({MakePm4Header(3, kPm4IndexBaseOpcode, 0),
                     static_cast<std::uint32_t>(address),
                     static_cast<std::uint32_t>(address >> 32U)});
    }
    if (type == AgcPacketType::kSetIndexCount) {
      return append({MakePm4Header(2, kPm4IndexBufferSizeOpcode, 0),
                     static_cast<std::uint32_t>(argument(1))});
    }
    if (type == AgcPacketType::kSetNumInstances) {
      return append({MakePm4Header(2, kPm4NumInstancesOpcode, 0),
                     static_cast<std::uint32_t>(argument(1))});
    }
    if (type == AgcPacketType::kDrawIndex) {
      const auto count = static_cast<std::uint32_t>(argument(1));
      const auto address = argument(2);
      const auto modifier = argument(3);
      if (address == 0 || (address & 1U) != 0) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      return append({MakePm4Header(6, kPm4DrawIndexOpcode, 0),
                     count == 0 ? 1U : count,
                     static_cast<std::uint32_t>(address),
                     static_cast<std::uint32_t>(address >> 32U), count,
                     DrawIndexInitiator(modifier)});
    }
    if (type == AgcPacketType::kDrawIndexMultiInstanced) {
      const auto count = static_cast<std::uint32_t>(argument(1));
      const auto index_address = argument(2);
      const auto object_address = argument(3);
      const auto instance_count = static_cast<std::uint32_t>(argument(4));
      if (index_address == 0 || object_address == 0 ||
          (index_address & 1U) != 0) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      return append(
          {MakePm4Header(9, 0x3aU, 0), count,
           static_cast<std::uint32_t>(index_address),
           static_cast<std::uint32_t>(index_address >> 32U),
           instance_count == 0 ? 1U : instance_count,
           static_cast<std::uint32_t>(object_address),
           static_cast<std::uint32_t>(object_address >> 32U), instance_count,
           DrawIndexInitiator(argument(5)) | 0x80U});
    }
    if (type == AgcPacketType::kDrawIndexAuto) {
      return append({MakePm4Header(3, kPm4DrawIndexAutoOpcode, 0),
                     static_cast<std::uint32_t>(argument(1)),
                     DrawIndexInitiator(argument(2)) | 0x2U});
    }
    if (type == AgcPacketType::kDrawIndexOffset) {
      const auto count = static_cast<std::uint32_t>(argument(2));
      return append({MakePm4Header(5, kPm4DrawIndexOffsetOpcode, 0),
                     count == 0 ? 1U : count,
                     static_cast<std::uint32_t>(argument(1)), count,
                     DrawIndexInitiator(argument(3))});
    }
    if (type == AgcPacketType::kSetBaseIndirectArgs) {
      const auto shader_type = static_cast<std::uint32_t>(argument(1));
      const auto address = argument(2);
      return append(
          {MakePm4Header(4, kPm4SetBaseOpcode, 0) | (shader_type << 1U), 1U,
           static_cast<std::uint32_t>(address) & ~0x7U,
           static_cast<std::uint32_t>(address >> 32U)});
    }
    if (type == AgcPacketType::kDispatchIndirect) {
      const auto flags = static_cast<std::uint32_t>(argument(2));
      return append({MakePm4Header(3, kPm4DispatchIndirectOpcode, 0),
                     static_cast<std::uint32_t>(argument(1)),
                     (flags & kDirectDispatchModifierMask) |
                         kDirectDispatchRequiredBits});
    }
    if (type == AgcPacketType::kJump) {
      const auto mode = static_cast<std::uint32_t>(argument(1));
      const auto cache_policy = static_cast<std::uint32_t>(argument(2));
      const auto target = argument(3);
      const auto size = static_cast<std::uint32_t>(argument(4));
      return append(
          {MakePm4Header(4, kPm4IndirectBufferOpcode, 0),
           static_cast<std::uint32_t>(target) & ~0x3U,
           static_cast<std::uint32_t>(target >> 32U),
           0x0f200000U | ((cache_policy & 0x3U) << 28U) |
               ((mode & 0x1U) << 20U) | (size & 0xfffffU)});
    }
    if (type == AgcPacketType::kRewind) {
      return append({MakePm4Header(2, kPm4RewindOpcode, 0),
                     (static_cast<std::uint32_t>(argument(1)) & 1U) << 31U});
    }
    if (type == AgcPacketType::kSetPredication) {
      const auto condition = static_cast<std::uint32_t>(argument(1));
      const auto operation = static_cast<std::uint32_t>(argument(2));
      const auto wait_operation = static_cast<std::uint32_t>(argument(3));
      const auto address = argument(4);
      return append(
          {MakePm4Header(4, kPm4SetPredicationOpcode, 0),
           ((condition & 1U) << 8U) | ((wait_operation & 1U) << 12U) |
               ((operation & 7U) << 16U),
           static_cast<std::uint32_t>(address) & ~0xfU,
           static_cast<std::uint32_t>(address >> 32U)});
    }
    if (type == AgcPacketType::kWriteData) {
      const auto data_address = argument(4);
      const auto dword_count = static_cast<std::uint32_t>(argument(5));
      if (data_address == 0 || dword_count > 0x3ffdU) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      const auto byte_count =
          static_cast<std::size_t>(dword_count) * sizeof(std::uint32_t);
      std::vector<std::byte> data(byte_count);
      if (byte_count != 0 && !memory_.Read(data_address, data)) {
        return {GpuRuntimeStatus::kMemoryFault};
      }
      std::vector<std::uint32_t> packet(4 + dword_count);
      const auto destination = static_cast<std::uint32_t>(argument(1));
      const auto cache_policy = static_cast<std::uint32_t>(argument(2));
      const auto increment = static_cast<std::uint32_t>(argument(6));
      const auto write_confirm = destination == 0
                                     ? 0U
                                     : static_cast<std::uint32_t>(argument(7)) &
                                           1U;
      packet[0] =
          MakePm4Header(4 + dword_count, kPm4NopOpcode,
                        kPm4WriteDataRegister);
      packet[1] = (destination & 0xffU) |
                  ((cache_policy & 0xffU) << 8U) |
                  ((increment & 0xffU) << 16U) |
                  ((write_confirm & 0xffU) << 24U);
      packet[2] = static_cast<std::uint32_t>(argument(3)) & ~0x3U;
      packet[3] = static_cast<std::uint32_t>(argument(3) >> 32U);
      for (std::size_t index = 0; index < dword_count; ++index) {
        packet[4 + index] = Read32(
            std::span<const std::byte>(data).subspan(index * 4U, 4U));
      }
      return AppendPacket(command_buffer, packet);
    }
    if (type == AgcPacketType::kReleaseMemory) {
      const auto action = static_cast<std::uint32_t>(argument(1));
      auto gcr_control = static_cast<std::uint32_t>(argument(2));
      const auto destination = static_cast<std::uint32_t>(argument(3));
      const auto cache_policy = static_cast<std::uint32_t>(argument(4));
      auto destination_address = argument(5);
      const auto data_selection = static_cast<std::uint32_t>(argument(6));
      auto data = argument(7);
      const auto gds_offset = static_cast<std::uint32_t>(argument(8));
      const auto gds_size = static_cast<std::uint32_t>(argument(9));
      const auto interrupt = static_cast<std::uint32_t>(argument(10));
      const auto interrupt_context =
          static_cast<std::uint32_t>(argument(11));
      if (destination > 1 ||
          (data_selection > 3 && data_selection != 5) || interrupt > 4) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      if ((gcr_control & 0x300U) == 0x100U) {
        gcr_control |= 0x200U;
      }
      if (interrupt == 4) {
        destination_address = 0;
        data = 0;
      } else if (data_selection == 5) {
        data = static_cast<std::uint64_t>(gds_offset & 0xffffU) |
               (static_cast<std::uint64_t>(gds_size & 0xffffU) << 16U);
      }
      const auto event_index = action >= 0x2fU ? 6U : 5U;
      return append(
          {MakePm4Header(8, kPm4NopOpcode, kPm4ReleaseMemoryRegister),
           (action & 0x3fU) | (event_index << 8U) |
               ((gcr_control & 0xfffU) << 12U) |
               ((cache_policy & 3U) << 25U),
           ((destination & 3U) << 16U) | ((interrupt & 7U) << 24U) |
               ((data_selection & 7U) << 29U),
           static_cast<std::uint32_t>(destination_address) & ~3U,
           static_cast<std::uint32_t>(destination_address >> 32U),
           static_cast<std::uint32_t>(data),
           static_cast<std::uint32_t>(data >> 32U),
          interrupt_context & 0x07ffffffU});
    }
    if (type == AgcPacketType::kEventWrite) {
      const auto event_type = static_cast<std::uint32_t>(argument(1));
      const auto address = argument(2);
      if (event_type > 0x3fU) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      if ((event_type & 0xfeU) == 0x38U) {
        return append(
            {MakePm4Header(4, kPm4EventWriteOpcode, 0),
             0x100U | event_type,
             static_cast<std::uint32_t>(address) & ~7U,
             static_cast<std::uint32_t>(address >> 32U)});
      }
      const auto control = event_type == 7 || event_type == 15 ||
                                   event_type == 16
                               ? 0x400U | event_type
                               : event_type;
      return append({MakePm4Header(2, kPm4EventWriteOpcode, 0), control});
    }
    if (type == AgcPacketType::kGetLodStats) {
      const auto address = argument(2);
      const auto cache_policy = static_cast<std::uint32_t>(argument(1));
      const auto reset_count = static_cast<std::uint32_t>(argument(4));
      const auto force_reset = static_cast<std::uint32_t>(argument(5));
      const auto report_and_reset = static_cast<std::uint32_t>(argument(6));
      const auto interval = static_cast<std::uint32_t>(argument(7));
      return append(
          {MakePm4Header(5, kPm4GetLodStatsOpcode, 0),
           static_cast<std::uint32_t>(argument(3)),
           static_cast<std::uint32_t>(address) & 0xffffffc0U,
           static_cast<std::uint32_t>(address >> 32U),
           ((cache_policy & 3U) << 28U) |
               ((report_and_reset & 1U) << 19U) |
               ((force_reset & 1U) << 18U) |
               ((reset_count & 0xffU) << 10U) | ((interval & 0xffU) << 2U)});
    }
    if (type == AgcPacketType::kWaitRegMem) {
      const auto size = static_cast<std::uint32_t>(argument(1));
      const auto compare = static_cast<std::uint32_t>(argument(2));
      const auto operation = static_cast<std::uint32_t>(argument(3));
      const auto cache_policy = static_cast<std::uint32_t>(argument(4));
      const auto address = argument(5);
      const auto reference = argument(6);
      const auto mask = argument(7);
      const auto poll = WaitPoll(static_cast<std::uint32_t>(argument(8)));
      if (size > 1 || compare > 7 || operation > 4 || cache_policy > 3) {
        return {GpuRuntimeStatus::kInvalidArgument};
      }
      if (size == 0) {
        return append(
            {MakePm4Header(7, kPm4NopOpcode, kPm4WaitMemory32Register),
             static_cast<std::uint32_t>(address) & ~0x3U,
             static_cast<std::uint32_t>(address >> 32U) & 0x3ffffU,
             static_cast<std::uint32_t>(mask),
             static_cast<std::uint32_t>(reference),
             Wait32Control(compare, operation, cache_policy), poll});
      }
      return append(
          {MakePm4Header(9, kPm4NopOpcode, kPm4WaitMemory64Register),
           static_cast<std::uint32_t>(address) & ~0x7U,
           static_cast<std::uint32_t>(address >> 32U) & 0x3ffffU,
           static_cast<std::uint32_t>(mask),
           static_cast<std::uint32_t>(mask >> 32U),
           static_cast<std::uint32_t>(reference),
           static_cast<std::uint32_t>(reference >> 32U),
           Wait64Control(compare, operation, cache_policy), poll});
    }
  } catch (const std::bad_alloc&) {
    return {GpuRuntimeStatus::kResourceLimit};
  }
  return {GpuRuntimeStatus::kInvalidArgument};
}

GpuPacketSizeResult GpuRuntime::GetPacketSize(
    std::uint64_t packet_address) const noexcept {
  if (packet_address == 0) {
    return {GpuRuntimeStatus::kInvalidArgument};
  }
  std::array<std::byte, sizeof(std::uint32_t)> header_bytes{};
  if (!memory_.Read(packet_address, header_bytes)) {
    return {GpuRuntimeStatus::kMemoryFault};
  }
  const auto header = Read32(header_bytes);
  if ((header & 0x3fffff00U) == 0x3fff1000U) {
    return {GpuRuntimeStatus::kOk, 1};
  }
  return {GpuRuntimeStatus::kOk,
          static_cast<std::uint32_t>(((header >> 16U) & 0x3fffU) + 2U)};
}

GpuRuntimeStatus GpuRuntime::SetPacketPredication(
    std::uint64_t packet_address, std::uint32_t predication) noexcept {
  if (packet_address == 0) {
    return GpuRuntimeStatus::kInvalidArgument;
  }
  std::lock_guard lock(mutex_);
  std::array<std::byte, sizeof(std::uint32_t)> header_bytes{};
  if (!memory_.Read(packet_address, header_bytes)) {
    return GpuRuntimeStatus::kMemoryFault;
  }
  auto header = Read32(header_bytes);
  header = (header & ~1U) | (predication == 1 ? 1U : 0U);
  Write32(header_bytes, 0, header);
  return memory_.Write(packet_address, header_bytes)
             ? GpuRuntimeStatus::kOk
             : GpuRuntimeStatus::kMemoryFault;
}

const char* GpuRuntimeStatusName(GpuRuntimeStatus status) noexcept {
  switch (status) {
    case GpuRuntimeStatus::kOk:
      return "ok";
    case GpuRuntimeStatus::kInvalidArgument:
      return "invalid-argument";
    case GpuRuntimeStatus::kMemoryFault:
      return "memory-fault";
    case GpuRuntimeStatus::kBufferTooSmall:
      return "buffer-too-small";
    case GpuRuntimeStatus::kCallbackRequired:
      return "callback-required";
    case GpuRuntimeStatus::kResourceLimit:
      return "resource-limit";
  }
  return "unknown";
}

}  // namespace kajps5::gpu
