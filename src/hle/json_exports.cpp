// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// Behavior reference: Copyright (C) 2026 SharpEmu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hle/json_exports.h"

#include <array>
#include <utility>
#include <vector>

#include "kernel/json_value.h"

namespace kajps5::hle {
namespace {

HleContextStatus JsonValueConstruct(HleCallContext& context,
                                    kernel::JsonValueService& values) {
  const auto address = context.Argument(0).value_or(0);
  if (address == 0) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }
  if (!context.QueryMemoryRegion(address).has_value()) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  const auto constructed = values.Construct(address);
  if (constructed == kernel::KernelStatus::kNoResources) {
    context.SetReturn(0);
    return HleContextStatus::kResourceLimit;
  }
  if (constructed != kernel::KernelStatus::kOk) {
    context.SetReturn(0);
    return HleContextStatus::kInvalidArgument;
  }
  context.SetReturn(address);
  return HleContextStatus::kOk;
}

HleContextStatus JsonValueDestroy(HleCallContext& context,
                                  kernel::JsonValueService& values) {
  const auto address = context.Argument(0).value_or(0);
  if (address != 0 && !context.QueryMemoryRegion(address).has_value()) {
    context.SetReturn(0);
    return HleContextStatus::kMemoryFault;
  }
  (void)values.Destroy(address);
  context.SetReturn(0);
  return HleContextStatus::kOk;
}

template <typename Handler>
void AddAliases(std::vector<HleExportDefinition>& exports,
                const char* name, const char* nid, Handler handler) {
  constexpr std::array libraries = {kJson2LibraryName, kJsonLibraryName};
  for (const auto* library : libraries) {
    exports.push_back({library, name, handler});
    exports.push_back({library, nid, handler});
  }
}

}  // namespace

ExportRegistryStatus RegisterJsonExports(
    ExportRegistry& registry, kernel::JsonValueService& values) {
  auto* const value_view = &values;
  std::vector<HleExportDefinition> exports;
  exports.reserve(16);
  AddAliases(exports, kJsonValueConstructorName, kJsonValueConstructorNid,
             [value_view](HleCallContext& context) {
               return JsonValueConstruct(context, *value_view);
             });
  AddAliases(exports, kJsonValueBaseConstructorName,
             kJsonValueBaseConstructorNid,
             [value_view](HleCallContext& context) {
               return JsonValueConstruct(context, *value_view);
             });
  AddAliases(exports, kJsonValueDestructorName, kJsonValueDestructorNid,
             [value_view](HleCallContext& context) {
               return JsonValueDestroy(context, *value_view);
             });
  AddAliases(exports, kJsonValueBaseDestructorName,
             kJsonValueBaseDestructorNid,
             [value_view](HleCallContext& context) {
               return JsonValueDestroy(context, *value_view);
             });
  return registry.RegisterBatch(std::move(exports));
}

}  // namespace kajps5::hle
