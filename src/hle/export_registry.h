// Copyright (C) 2026 KajPS5 contributors
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hle/call_context.h"

namespace kajps5::hle {

inline constexpr std::size_t kMaximumExportLibraryLength = 127;
inline constexpr std::size_t kMaximumExportSymbolLength = 255;

using HleHandler = std::function<HleContextStatus(HleCallContext&)>;

struct HleExportDefinition {
  std::string library;
  std::string symbol;
  HleHandler handler;
};

enum class ExportRegistryStatus {
  kOk,
  kInvalidArgument,
  kAlreadyExists,
  kNotFound,
  kAmbiguous,
};

struct HleDispatchResult {
  ExportRegistryStatus status = ExportRegistryStatus::kOk;
  HleContextStatus handler_status = HleContextStatus::kOk;
  std::string library;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == ExportRegistryStatus::kOk &&
           handler_status == HleContextStatus::kOk;
  }
};

class ExportRegistry final {
 public:
  [[nodiscard]] ExportRegistryStatus Register(std::string library,
                                              std::string symbol,
                                              HleHandler handler);
  [[nodiscard]] ExportRegistryStatus RegisterBatch(
      std::vector<HleExportDefinition> exports);
  [[nodiscard]] HleDispatchResult Dispatch(
      std::string_view symbol, std::span<const std::string> library_order,
      HleCallContext& context) const;
  [[nodiscard]] HleDispatchResult Dispatch(
      std::string_view symbol, HleCallContext& context) const;
  [[nodiscard]] std::size_t size() const;

 private:
  using Key = std::pair<std::string, std::string>;

  mutable std::mutex mutex_;
  std::map<Key, HleHandler> entries_;
};

[[nodiscard]] std::string_view ExportRegistryStatusName(
    ExportRegistryStatus status) noexcept;

}  // namespace kajps5::hle
