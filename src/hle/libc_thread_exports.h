// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hle/export_registry.h"

namespace kajps5::kernel {
class PthreadService;
}

namespace kajps5::hle {

inline constexpr auto kLibcMtxInitName = "_Mtx_init";
inline constexpr auto kLibcMtxInitNid = "YaHc3GS7y7g";
inline constexpr auto kLibcMtxInitWithNameName = "_Mtx_init_with_name";
inline constexpr auto kLibcMtxInitWithNameNid = "tgioGpKtmbE";
inline constexpr auto kLibcMtxInitWithDefaultNameOverrideName =
    "_Mtx_init_with_default_name_override";
inline constexpr auto kLibcMtxInitWithDefaultNameOverrideNid = "JHp7ogc1+HY";
inline constexpr auto kLibcMtxDestroyName = "_Mtx_destroy";
inline constexpr auto kLibcMtxDestroyNid = "5Lf51jvohTQ";
inline constexpr auto kLibcMtxLockName = "_Mtx_lock";
inline constexpr auto kLibcMtxLockNid = "iS4aWbUonl0";
inline constexpr auto kLibcMtxTrylockName = "_Mtx_trylock";
inline constexpr auto kLibcMtxTrylockNid = "k6pGNMwJB08";
inline constexpr auto kLibcMtxTimedlockName = "_Mtx_timedlock";
inline constexpr auto kLibcMtxTimedlockNid = "hPzYSd5Nasc";
inline constexpr auto kLibcMtxUnlockName = "_Mtx_unlock";
inline constexpr auto kLibcMtxUnlockNid = "gTuXQwP9rrs";
inline constexpr auto kLibcMtxCurrentOwnsName = "_Mtx_current_owns";
inline constexpr auto kLibcMtxCurrentOwnsNid = "VYQwFs4CC4Y";

[[nodiscard]] ExportRegistryStatus RegisterLibcThreadExports(
    ExportRegistry& registry, kernel::PthreadService& pthreads);

}  // namespace kajps5::hle
