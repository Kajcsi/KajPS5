# Third-party notices

KytyPS5 and SharpEmu serve as research references. The graphics section below
identifies the first close KytyPS5 adaptation. Other sections state when code
uses behavior only. Each section records the exact commit used, even after the
current reference pin moves. Current pins and refresh reviews are in
`docs/upstreams.md`.

## Kernel behavior

The clock, event-flag, file, semaphore, and scheduler behavior in `src/kernel/`
and the matching `tests/kernel_*_test.cpp` files was based on behavior observed
in KytyPS5 and SharpEmu. Scheduler deadlines use KytyPS5's monotonic deadline
model and adapt SharpEmu's separate timeout completion state without adding a
host timer thread. The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

KytyPS5 identifies as GPL-2.0-only. SharpEmu identifies as
GPL-2.0-or-later. No upstream host executor, continuation system, ownership
model, or source was copied.

## Loader and guest memory

The dynamic-table, dynamic-string, symbol, `RELA`, and launch-metadata behavior
in `src/loader/elf.cpp`, `src/loader/relocator.cpp`,
`src/loader/launch_metadata.cpp`, `src/loader/lifecycle_plan.cpp`,
`src/loader/module_export_registry.cpp`, `src/loader/module_plan.cpp`,
`src/loader/sce_symbol.cpp`, `tests/elf_dynamic_test.cpp`,
`tests/elf_sce_dynamic_test.cpp`, `tests/elf_relocation_test.cpp`,
`tests/relocation_kinds_test.cpp`, `tests/elf_symbol_test.cpp`,
`tests/launch_metadata_test.cpp`,
`tests/lifecycle_plan_test.cpp`,
`tests/module_export_registry_test.cpp`, `tests/module_plan_test.cpp`, and
`tests/self_loader_test.cpp` was based on behavior observed in the pinned
KytyPS5 and SharpEmu loader files. The PS5 metadata parser follows KytyPS5's
`PT_OS_DYNLIBDATA`, packed module, packed library, and SCE dynamic-tag
semantics. SELF parsing follows KytyPS5's header, segment, and containing
program-header model. Launch metadata follows KytyPS5's entry-point,
process-parameter, TLS, module-order, and `R_X86_64_DTPMOD64` behavior and
SharpEmu's split between initialized TLS bytes and per-thread zero fill. It
adapts SharpEmu's checked TLS module identity, structural SELF checks, checked
payload fallbacks, mixed standard and SCE dynamic fields, symbol-size
fallback, initializer discovery, and handling of already-dumped payloads.
Dynamic startup and shutdown tags and arrays follow KytyPS5's runtime-linker
model. The lifecycle planner combines KytyPS5's direct module hooks with
SharpEmu's checked array discovery and duplicate suppression.
The checked Variant II layout in `src/loader/static_tls_layout.*` adapts
KytyPS5's per-module TLS ownership and SharpEmu's aligned static-offset
calculation, duplicate registration checks, and bounded startup reservation.
The relocation planner adapts SharpEmu's checked formulas and write widths for
PC-relative, 32-bit, symbol-size, `RELATIVE64`, `DTPMOD64`, `DTPOFF64`, and
`TPOFF64` relocations. A narrow value must fit its signed or unsigned target
before the planner writes guest memory.
The module export registry adapts KytyPS5's versioned NID, library, and module
keys and its global or weak export rules. No upstream symbol-database source
was copied.

The reviewed sources are KytyPS5 `src/loader/elf.h` and
`src/loader/runtimeLinker.cpp` at commit
`f6e01e54031a3c615f089f061a4eab2f3c59acba`, and SharpEmu
`src/SharpEmu.Core/Loader/SelfLoader.cs` at commit
`d5108e854d609808f17093a6f5dbbc711d09ad2e`.

The SharpEmu reference file states:

`Copyright (C) 2026 SharpEmu Emulator Project`

No upstream loader source was copied.

The checked ELF load-bias path in `src/loader/elf.cpp` and its loader and
relocation tests follows the nonzero runtime image-base flow in pinned KytyPS5
`src/loader/runtimeLinker.cpp`. It keeps KajPS5's parsed metadata unchanged and
uses the existing checked relocation boundary. No upstream loader source was
copied.

Transactional guest-memory protection and unmap behavior in
`src/core/memory/guest_memory.cpp` and `tests/guest_memory_test.cpp` follows
behavior observed in KytyPS5 `src/common/virtualMemory.h` and
`src/common/virtualMemory.cpp`, and SharpEmu
`src/SharpEmu.HLE/IGuestAddressSpace.cs` and
`src/SharpEmu.Core/Memory/PhysicalVirtualMemory.cs` at the pinned commits.
No upstream virtual-memory source was copied.

The memory HLE handlers in `src/hle/kernel_memory_exports.cpp` and
`tests/hle_kernel_memory_exports_test.cpp` follow behavior observed in KytyPS5
`src/kernel/memory.cpp` and `src/libs/libKernel.cpp`, and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs` at the pinned commits.
They use the 16 KiB guest page size and the `mprotect`, `munmap`,
`getpagesize`, and memory-protection query names and NIDs confirmed by both
references. Flexible mappings use the names and NIDs confirmed by both
references, KytyPS5's page, flag, protection, and name checks, and SharpEmu's
checked first-fit address search. The direct-memory range service in
`src/kernel/direct_memory.cpp` uses KytyPS5's physical-memory size, first-fit
allocation, largest-gap query, and partial-release rules. Its fragmentation
tests also adapt SharpEmu's checked split and coalesce behavior. Direct guest
mappings use the three names, NIDs, signatures, allocation checks, and name
limits confirmed by KytyPS5. They adapt SharpEmu's checked guest reservation,
hinted first-fit search, and seventh-argument stack read. The sparse physical
backing in `src/core/memory/shared_memory_backing.cpp` follows KytyPS5's rule
that direct aliases share contents and retain them across unmap and remap. The
protection query uses KytyPS5's exclusive range end. No memory-export,
allocator, or backing-store source was copied.

## Native execution, imports, and HLE

The W^X native leaf-execution behavior in
`src/cpu/native_leaf_executor.cpp` and
`tests/native_leaf_executor_test.cpp` was based on behavior observed in the
pinned KytyPS5 and SharpEmu host-memory and native-executor files. The
SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

No upstream executor or host-memory source was copied.

The host-mapped `GuestMemory` backing, page-aware ELF load plan, and direct
execution path in `src/core/memory/guest_memory.cpp`, `src/loader/elf.cpp`,
`src/cpu/native_leaf_executor.cpp`, and their focused tests adapt the
runtime-address allocation, aligned segment sizing, and host protection
boundary from KytyPS5 `src/common/virtualMemory.cpp`,
`src/common/platform/sysWindowsVirtual.cpp`,
`src/common/platform/sysLinuxVirtual.cpp`, and
`src/loader/runtimeLinker.cpp`, and the refreshed guest-address-space tests in
`tests/VirtualMemoryAllocationTests.cpp`. KajPS5 keeps its existing checked
region model and uses the host mapping as that model's storage. Unaligned ELF
segments are expanded to host pages. Compatible shared-page permissions are
combined, but a shared boundary cannot introduce a writable-executable page.
Checked initialization can cross those regions and restores each declared
protection. No upstream virtual-memory, loader, or executor source was copied.

The library-scoped HLE import lookup and relocation connection in
`src/hle/import_registry.cpp`, `tests/hle_import_registry_test.cpp`, and
`tests/hle_import_link_test.cpp` were based on behavior observed in the pinned
KytyPS5 and SharpEmu import resolvers. The scoped PS5 symbol handling in
`src/loader/relocator.cpp` follows KytyPS5's compact library and module ID
matching in `src/loader/runtimeLinker.cpp` and adapts SharpEmu's NID extraction
from `src/SharpEmu.Core/Loader/SelfLoader.cs` at the pinned commits. Absolute
symbol relocations follow the KytyPS5 `R_X86_64_64` flow and SharpEmu's
unresolved weak-symbol rule. The test-only HLE call path in
`tests/hle_public_guest_test.cpp` also follows the import thunk and dispatch
behavior in those pinned projects. The ordered resolver in
`src/loader/layered_import_resolver.cpp` keeps KytyPS5-style loaded-module
exports and then uses the SharpEmu-style HLE registry as a fallback. No thunk
or resolver source was copied.

The canonical `libkernel` registry name in `src/hle/kernel_exports.h` matches
the library version declared by pinned KytyPS5 in `src/libs/libKernel.cpp`.
No upstream registry source was copied.

The bounded trace in `src/loader/relocation_trace.cpp` and
`tests/relocation_trace_test.cpp` adapts SharpEmu's focus on structured,
bounded import diagnostics. No diagnostic source was copied.

The read-only HLE coverage inventory in `src/hle/import_coverage.cpp` and
`tests/hle_import_coverage_test.cpp` follows KytyPS5's scoped symbol lookup and
adapts SharpEmu's ordered unique-NID inventory and relocation descriptor
counts from `src/SharpEmu.Core/Loader/SelfLoader.cs` at the pinned commits. It
also checks the guest-owned data registry without reading or dispatching the
registered targets. It does not copy upstream source or create SharpEmu import
stubs.

The guest-owned HLE data page in `src/hle/data_symbols.cpp` and
`tests/hle_data_symbols_test.cpp` uses the four object NIDs and library scopes
confirmed by pinned KytyPS5 `src/libs/libKernel.cpp` and `src/libs/libC.cpp`.
Its bounded process name, duplicate terminator canary, and initial need flags
adapt pinned SharpEmu `src/SharpEmu.HLE/HleDataSymbols.cs`. KajPS5 stores every
address in checked guest memory instead of exposing a host pointer. No data
symbol source was copied.

The register and memory boundary in `src/hle/call_context.cpp` and
`tests/hle_call_context_test.cpp` follows behavior in SharpEmu's
`src/SharpEmu.HLE/CpuContext.cs`. No context source was copied.

The scoped handler table in `src/hle/export_registry.cpp` and
`tests/hle_export_registry_test.cpp` follows behavior in SharpEmu's
`src/SharpEmu.HLE/ExportedFunction.cs` and KytyPS5's native symbol database.
No export-registry source was copied.

The C++ initialization guard service in `src/kernel/cxa_guard.cpp`, the libc
handlers in `src/hle/libc_exports.cpp`, and
`tests/hle_libc_exports_test.cpp` adapt the checked guard-word states,
same-owner behavior, and NIDs from SharpEmu
`src/SharpEmu.Libs/CxxAbiExports.cs` at commit
`d5108e854d609808f17093a6f5dbbc711d09ad2e`. KajPS5 replaces SharpEmu's host
spin wait with its existing guest scheduler. No source was copied. The
SharpEmu reference file states:

`Copyright (C) 2026 SharpEmu Emulator Project`

The `__cxa_pure_virtual` name and NID in `src/hle/libc_exports.cpp` are
confirmed by pinned SharpEmu `scripts/ps5_names.txt` and
`scripts/aerolib_catalog.py`. KajPS5 treats a call as a fatal guest error; it
does not return a false success. No upstream handler source was copied.

The JSON value service in `src/kernel/json_value.cpp`, its HLE bridge in
`src/hle/json_exports.cpp`, and `tests/hle_json_exports_test.cpp` use the
constructor, destructor, and NIDs confirmed by pinned KytyPS5
`src/libs/libJson2.cpp`. The bounded address-keyed shadow model and the
`libSceJson` aliases adapt pinned SharpEmu
`src/SharpEmu.Libs/Json/JsonValueModel.cs`, `JsonValueExports.cs`, and
`JsonExports.cs`. KajPS5 also registers the observed `libSceJson2` scope. No
upstream JSON source or object layout was copied.

The AMPR command-buffer service in `src/kernel/ampr_command_buffer.*`, its HLE
bridge in `src/hle/ampr_exports.*`, and the matching focused tests adapt the
header fields, record sizes, NIDs, bounded append behavior, counters, reset,
clear, and write-address completion behavior from SharpEmu
`src/SharpEmu.Libs/Ampr/AmprExports.cs` and
`tests/SharpEmu.Libs.Tests/Ampr/AmprWriteAddressTests.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KytyPS5
`src/libs/libAmpr.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0` confirms the native C++ service
boundary and command-buffer role. KajPS5 keeps the state in its existing
kernel runtime and uses its checked guest-memory owner. APR file reads are not
registered as a success stub. The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

KytyPS5 identifies as GPL-2.0-only. SharpEmu identifies as
GPL-2.0-or-later. No upstream AMPR source was copied.

The process lifecycle service in `src/kernel/process_lifecycle.cpp` and its
libc bridge in `src/hle/libc_exports.cpp` use the argument, callback, and exit
behavior confirmed by pinned KytyPS5 `src/libs/libC.cpp`. The bounded,
thread-safe callback records and explicit exit request adapt pinned SharpEmu
`src/SharpEmu.Libs/Kernel/KernelExports.cs`. KajPS5 does not call a guest
callback until an executor can resume it safely. No upstream handler source
was copied.

The guest libc heap service in `src/kernel/libc_heap.cpp`, its bridge in
`src/hle/libc_exports.cpp`, and `tests/hle_libc_heap_test.cpp` adapt the
allocation, clearing, alignment, resize, and failure behavior in pinned
SharpEmu `src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs`. KajPS5 keeps
the allocations in its checked guest-memory model instead of exposing host
heap addresses. The default 16-byte alignment follows the application-heap
boundary in pinned KytyPS5 `src/libs/libKernel.cpp`. No upstream allocator
source was copied.

The caller-owned mspace model in the same files extends that checked heap
behavior with fixed guest ranges, bounded allocation records, and in-place or
moving resize operations. Its export names and NIDs are confirmed by pinned
SharpEmu `scripts/ps5_names.txt` and `scripts/aerolib_catalog.py`. Mspace
backing remains under the existing KajPS5 guest-memory owner. No upstream
mspace implementation was copied.

The checked libc memory, string, scalar math, and C++ allocation handlers in
`src/hle/libc_exports.cpp`, plus the stack-check handler in
`src/hle/kernel_exports.cpp`, use export identities confirmed by pinned
SharpEmu `scripts/ps5_names.txt` and `scripts/aerolib_catalog.py`.
Memory and string behavior follows pinned SharpEmu
`src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs` and the direct libc
bridges in pinned KytyPS5 `src/libs/libC.cpp`. The fatal stack-check result
adapts pinned SharpEmu
`src/SharpEmu.Libs/Kernel/KernelRuntimeCompatExports.cs`. Scalar math follows
KytyPS5's direct host-math bridge but uses KajPS5's checked XMM call context.
C++ allocation remains inside the existing guest heap. No upstream handler
source was copied.

The checked `GuestMemory::Copy` operation and the native `memcpy` and
`memmove` short path adapt behavior from SharpEmu
`src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs` at commit
`532251c0c39b976f1e5ce7058f3a563461fa9a07`. KajPS5 first checks complete
source and destination ranges, then copies through its existing coherent C++
guest-memory owner. A rejected short path uses the normal HLE handler. No
SharpEmu source was copied verbatim.

The bounded `strlen` and 16-bit `wcscmp` behavior also follows pinned
SharpEmu `src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs`. The
`sincos` and `sincosf` output rules follow pinned KytyPS5
`src/libs/libC.cpp`. Array allocation shares KajPS5's existing scalar C++
allocation path. No upstream string, math, or allocation source was copied.

The formatted-output bridge in `src/hle/libc_format.cpp` and its focused test
adapt the bounded format parsing, separate integer and XMM argument streams,
stack spill order, and System V AMD64 `va_list` cursor from pinned SharpEmu
`src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs`. Export identities are
confirmed by the pinned SharpEmu catalogs. Pinned KytyPS5
`src/libs/libC.cpp` confirms the direct `snprintf` and `memmove` model and their
NIDs. KajPS5 implements the behavior in its checked C++ guest-memory boundary.
No upstream formatter source was copied.

The native HLE bridge in `src/cpu/native_hle_trampoline.cpp` adapts the
register-pack and host-call boundary from SharpEmu
`src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. Its shared write-then-execute
buffer follows the virtual-memory boundary in KytyPS5
`src/common/virtualMemory.cpp` and the matching platform files at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. KajPS5 captures the six System V
integer registers and a declared, bounded number of stack arguments. It also
transfers XMM0-XMM7 and optional XMM0-XMM1 returns through an FXSAVE64 image.
The Windows entry bridge preserves host-only nonvolatile state around the
System V call. Valid libc memory copies can bypass context and vector
marshalling after library and symbol resolution.

The per-executable table in `src/cpu/native_hle_import_table.cpp` and its
public guest tests adapt KytyPS5's call-table installation before relocation
from `src/loader/runtimeLinker.cpp` at the same commit, and SharpEmu's complete
import inventory and stub setup from
`src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.cs` at the same SharpEmu
commit. KajPS5 keeps the table inside its C++ runtime, resolves only registered
library and NID pairs, and layers guest data symbols below function targets.
No upstream trampoline bytes, table source, or handler source were copied.

The guest-entry bridge in `src/cpu/native_guest_executor.cpp` adapts the entry
parameter layout, root frame, guest-stack switch, and host-state preservation
from KytyPS5 `src/loader/runtimeLinker.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. The HLE trampoline's saved host
stack follows SharpEmu `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.cs`
at commit `7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KajPS5 implements a small C++
execution context and checked guest-memory boundary. No upstream bridge or
trampoline bytes were copied.

The Windows guest-fault boundary in `src/cpu/native_guest_executor.cpp`
adapts the host-exception classification and mutable native-context boundary
from KytyPS5 `src/common/hostException.h` and
`src/common/hostException.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. Returning from a guest fault on
the saved host stack follows SharpEmu
`src/SharpEmu.Core/Cpu/Native/Windows/WindowsFaultHandling.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KajPS5 handles only exceptions
whose instruction address is inside the active guest mapping. No upstream
exception-handler or recovery-bridge source was copied.

The blocked-import continuation in `src/cpu/native_guest_executor.cpp` and
`src/cpu/native_hle_trampoline.cpp` adapts the call-frame capture, host-yield,
wake, handler retry, and guest-resume behavior in SharpEmu
`src/SharpEmu.HLE/GuestThreadExecution.cs` and
`src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KajPS5 stores the continuation
in a checked C++ record, parks it per guest thread, and reuses its existing
scheduler and shared native execution lane. No upstream continuation or
trampoline source was copied.

The cooperative native-yield control path also adapts SharpEmu's active guest
yield request and return-to-host behavior in
`src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.cs` and
`src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs` at the same
commit. KajPS5 records the completed HLE return and resumes it without a
second dispatch. No upstream yield source or stub bytes were copied.

The native pthread entry path follows KytyPS5's guest-stack and System V entry
argument behavior in `src/kernel/pthread.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. No upstream assembly or source
was copied.

The native thread runner follows KytyPS5's guest entry, guarded stack mapping,
zero-fill, release, and thread-exit flow in `src/kernel/pthread.cpp`. Its ready,
blocked, resumed, and exited state transitions also adapt SharpEmu's behavior in
`src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. No upstream runner source was
copied. The runner uses the same native lane for the KytyPS5-style process
entry and pthread entry ABIs. Its owned main stack and fixed three-pointer
argument block follow KytyPS5 `src/loader/runtimeLinker.cpp` and SharpEmu
`src/SharpEmu.Core/Cpu/CpuDispatcher.cs` at the pinned commits. SharpEmu's
checked UTF-8 argument placement is adapted without its managed CPU runtime.
The process launcher connects checked loader metadata to that entry path and
keeps SharpEmu's explicit startup failure boundary. It does not copy SharpEmu
runtime code or add a second dispatcher.
The general guest-function entry path extends the same bridge to the six System
V integer argument registers. This supports KytyPS5's three-argument module
initializer ABI and SharpEmu's zeroed module frame without another executor.
General guest functions also use the existing guest scheduler, continuation
state, and guarded stack ownership.
The checked process startup sequence adapts KytyPS5's module initializer call
order and SharpEmu's stop-on-failure behavior. It keeps initializer work in the
same scheduler lane and creates the main thread only after startup succeeds.

The title session and loader in `src/runtime/title_session.*`,
`src/runtime/title_loader.*`, `src/app/main.cpp`, and their focused tests adapt
KytyPS5's executable lifecycle order and boot composition from
`src/emulator.cpp` and `src/loader/runtimeLinker.{h,cpp}` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. The explicit startup, blocked,
running, finalizing, exited, and failed states adapt SharpEmu
`src/SharpEmu.Core/Runtime/SharpEmuRuntime.cs` and
`src/SharpEmu.Core/Loader/SelfLoader.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KajPS5 composes its existing
loader metadata, kernel runtime, scheduler, native executor, guarded stacks,
and process lifecycle service. Initializers, main, exit callbacks, and
finalizers all use the same C++ runtime and native execution lane. The checked
run path also keeps SharpEmu's explicit pre-execution import coverage boundary.
No upstream runtime source was copied.

The multi-module title runtime in `src/runtime/module_runtime.*`, its
connection through `src/runtime/title_loader.*`, `src/runtime/title_session.*`,
and `src/app/main.cpp`, and the focused module and title tests adapt KytyPS5's
loaded-program placement, export registration, TLS registration, relocation,
and module start and stop ordering from `src/loader/runtimeLinker.{h,cpp}` at
commit `a65d17a5d689257a35644e01e9d15539361f0bf0`. Batched module intake,
parse-before-use behavior, initialization before process entry, and
all-or-nothing session exposure adapt SharpEmu
`src/SharpEmu.Core/Runtime/SharpEmuRuntime.cs`,
`src/SharpEmu.Core/Loader/SelfLoader.cs`, and
`src/SharpEmu.HLE/ModuleManager.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KajPS5 keeps one C++ guest-memory
owner, HLE table, scheduler, and native execution lane. No upstream runtime or
module-manager source was copied.

The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

The process-time handlers in `src/hle/kernel_clock_exports.cpp` and
`tests/hle_kernel_clock_exports_test.cpp` adapt the matching process-time,
counter, and frequency behavior from the pinned KytyPS5
`src/kernel/pthread.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelRuntimeCompatExports.cs`. No clock-export
source was copied. Clock NIDs are confirmed by both pinned references.
The clock-gettime and gettimeofday handlers use KytyPS5's kernel-compatible
`EFAULT` and `EINVAL` values. SharpEmu marks its differing Gen5 error values as
synthetic.

## Graphics

The checked AGC command-buffer owner in `src/gpu/runtime.*` closely adapts the
command-buffer allocation and type-3 PM4 packet algorithms from KytyPS5
`src/libs/agc.cpp` and `src/graphics/guest_gpu/pm4.h` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. These destination files use
`GPL-2.0-only`, matching KytyPS5. They replace raw guest pointers and the
upstream callback call with KajPS5's checked guest-memory boundary and an
explicit callback-required result. KytyPS5 does not place a separate
copyright header in these source files. The adapted packet families cover
NOP, direct and indirect dispatch, direct register writes, index state,
indexed draws, indirect-buffer jumps, rewinds, predication, guest-backed data
writes, level-of-detail statistics, waits, packet length, and packet
predication.

The HLE bridge in `src/hle/agc_exports.*` and
`tests/hle_agc_exports_test.cpp` use the names and NIDs confirmed by the same
KytyPS5 sources and by SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. SharpEmu supplies independent
bounds, reserved-space, packed-register ABI, stack arguments, packet words,
modifier rules, and invalid-pointer behavior.

The checked `sceAgcCreateShader` path in `src/gpu/shader_runtime.*`,
`src/gpu/runtime.*`, `src/hle/agc_exports.*`,
`tests/gpu_shader_runtime_test.cpp`, and `tests/hle_agc_exports_test.cpp`
adapts the Gen5 header layout, relative-pointer relocation, and program-address
register pairs from KytyPS5 `src/libs/agc.cpp` and
`src/graphics/shader/shader.h` at commit
`fb5ecec455cf6c67154134429485ffccbfc34203`. It independently re-expresses
the checked full-table search and GS/HS front-half register-table behavior in
SharpEmu `src/SharpEmu.Libs/Agc/AgcExports.cs` at commit
`cf3bd0b4f2016eede08692110b6c14f08b5a912c`. The new
`src/gpu/shader_runtime.*` source carries `GPL-2.0-only`, matching the
directly adapted KytyPS5 algorithms; the SharpEmu behavior and test reference
is `GPL-2.0-or-later` and states
`Copyright (C) 2026 SharpEmu Emulator Project`. KajPS5 stores only checked
guest addresses and scalar metadata in its existing GPU runtime; no SharpEmu
runtime or renderer source was copied.

The shader-program binding handoff in `src/gpu/command_processor.*`,
`src/gpu/shader_runtime.*`, and `tests/gpu_shader_binding_test.cpp` adapts the
stage-specific SH program-register pairs and address reconstruction from
KytyPS5 `src/libs/agc.cpp` at commit
`fb5ecec455cf6c67154134429485ffccbfc34203`. Its checked direct program index,
stable stage snapshots, unregistered-program diagnostics, and exact-entry
recompile tests re-express behavior from SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs` at commit
`4b5ea6a79346cb4529fa531cf2c1973f3978eb22`. The KajPS5 runtime remains the
only GPU and guest-memory owner, and shader compilation stays outside command
submission.

The first Vulkan-independent resource-coherence seam in
`src/core/memory/guest_memory.*`, `src/gpu/resource_coherence.*`,
`src/gpu/runtime.*`, and `tests/gpu_resource_coherence_test.cpp` adapts the
ownership boundary from KytyPS5
`src/graphics/host_gpu/memoryTracker.*`, `pageManager.*`, and
`renderer/cache/gpuResourceManager.*` at commit
`fb5ecec455cf6c67154134429485ffccbfc34203`. It also re-expresses the monotonic
write-generation, acknowledged re-upload, overlap, and CPU/GPU-divergence
behavior from SharpEmu `src/SharpEmu.HLE/GuestImageWriteTracker.cs`,
`src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs`, and
`tests/SharpEmu.Libs.Tests/{Memory/GuestImageWriteTrackerTests.cs,
VideoOut/VulkanGuestImageCpuSyncPolicyTests.cs}` at commit
`4b5ea6a79346cb4529fa531cf2c1973f3978eb22`. KajPS5 does not import Kyty's
page manager, host-fault handler, renderer cache, or SharpEmu's Vulkan code.
`GuestMemory` remains the sole checked guest-memory owner and reports every
range actually changed by its checked mutation APIs, including a changed prefix
when an operation fails late; it does not claim to observe native direct guest
stores. A changed shared backing range is reported through every overlapping
guest alias using a bounded exact event set and a conservative whole-memory
fallback; its range-local mapping tokens are captured atomically with mapping
updates, but remain snapshots rather than backend-lifetime reservations.
`GpuRuntime` owns the scalar resource records, which preserve an explicit
GPU-write-pending state and monotonic CPU-write generations despite
out-of-order callbacks.

The bounded guest-buffer preparation seam in
`src/gpu/vulkan/buffer_cache.*` follows the cache ownership shape observed in
KytyPS5 `src/graphics/host_gpu/renderer/cache/bufferCache.*` at commit
`fb5ecec455cf6c67154134429485ffccbfc34203`. Its checked upload, bounded
versioned backing pool, and live-CPU-preserving writeback are informed by SharpEmu commit
`9e10d7c44a2821cfd5ccd3417c09c0cf269285a4` and the reviewed changes
`f3d9439952a40c5b81b0d0dec443184e82a683d1` and `26bda04`. No upstream cache,
page manager, scheduler, address-space owner, or source code was copied.
The Vulkan backend performs checked upload and fence-complete readback through
the existing `GuestMemory` and resource-coherence owners.
Host-mapped initialization serializes its
temporary native protection transitions. A future fault-backed observer must
defer a verified fault into GuestMemory's ordinary write-observation funnel; it
cannot invoke a GPU callback from fault context. The new GPU source and test use
`GPL-2.0-only`, matching the close Kyty architecture reference; the existing
guest-memory source retains `GPL-2.0-or-later`. SharpEmu states
`Copyright (C) 2026 SharpEmu Emulator Project`. No upstream source was copied.

The public guest-image layout model in `src/gpu/image_layout.*`, its small
storage-alias helper in `src/gpu/format.*`, and
`tests/gpu_image_layout_test.cpp` adapt the non-owning image-cache layout shape
from KytyPS5 `src/graphics/guest_gpu/gpu_format.*`,
`src/graphics/host_gpu/renderer/image/{imageInfo,textureCommon,tiler}.*`, and
`src/graphics/host_gpu/renderer/cache/textureCache.*` at commit
`fb5ecec455cf6c67154134429485ffccbfc34203`. They independently re-express
the byte-count, compressed-block, volume/array, sRGB-view, and checked failure
behavior in SharpEmu `VulkanGuestImageByteCountTests.cs`,
`VulkanGuestImageTypeTests.cs`, `VulkanPresentEncodeFormatTests.cs`,
`GuestImageWriteTracker`, and `Gfx10UnifiedFormat` at commit
`9e10d7c44a2821cfd5ccd3417c09c0cf269285a4`. The KajPS5 sources use
`GPL-2.0-only`; SharpEmu states `Copyright (C) 2026 SharpEmu Emulator Project`
and `GPL-2.0-or-later`. No upstream code, renderer, image cache, page table,
or guest-memory owner was copied. The result is scalar layout metadata only;
`GuestMemory` remains the sole guest-memory owner.

The transactional Vulkan image preparation seam in
`src/gpu/vulkan/image_cache.*` and `tests/gpu_vulkan_image_cache_test.cpp`
adapts the ownership, image/view compatibility, and cache-shape evidence in
KytyPS5 `src/graphics/host_gpu/renderer/image/{image,imageView,textureCommon}.*`,
`renderer/cache/textureCache.*`, and `gpuResourceManager.*` at commit
`fb5ecec455cf6c67154134429485ffccbfc34203`. It re-expresses SharpEmu's
guest-image byte sizing, compatible format aliases, and write-tracker behavior
at commit `9e10d7c44a2821cfd5ccd3417c09c0cf269285a4`. The KajPS5 source is
`GPL-2.0-only`; no upstream Vulkan renderer, image cache, or memory owner was
copied. Staging is an owned Vulkan lease only and all guest memory access and
generation acknowledgement, command completion, and dirty-byte writeback remain
through the existing owners.

Translated image/sampler preparation additionally adapts the descriptor and
sampler-cache behavior of the pinned KytyPS5 sources named above. It creates
only owned Vulkan leases over checked GuestMemory ranges; no upstream renderer,
sampler cache, or memory owner was copied.

The checked PM4 processor in `src/gpu/command_processor.*` closely adapts
KytyPS5 `src/graphics/guest_gpu/command_processor/pm4Dispatch.cpp`,
`src/graphics/guest_gpu/command_processor/pm4Handlers.cpp`, and
`src/graphics/guest_gpu/graphicsRun.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. It converts the upstream raw
pointer dispatch into checked guest reads, one persistent KajPS5 GPU state,
bounded indirect-buffer traversal, explicit parse results, and an injected
submission sink. These destination files use `GPL-2.0-only`, matching
KytyPS5.

The persistent context and shader register tests in
`tests/gpu_command_processor_test.cpp` re-express SharpEmu
`tests/SharpEmu.Libs.Tests/Agc/AgcContextRegisterTests.cs` and
`tests/SharpEmu.Libs.Tests/Agc/AgcShaderStageRegisterTests.cs` at commit
`ea9be7484f7679e3d0f060ee4722e480d755623a`. The bounded wait, malformed
packet, and indirect traversal cases also use behavior from SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs` and
`src/SharpEmu.Libs/Agc/GpuWaitRegistry.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`.

The snapshot-backed continuation and submission queue in
`src/gpu/command_processor.*` and `src/gpu/submission_queue.*` adapt the
submission ordering in KytyPS5 `src/libs/agc.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. The queue-owned blocked cursor,
fixed-point cross-queue drain, and no-replay tests re-express SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs`,
`src/SharpEmu.Libs/Agc/GpuWaitRegistry.cs`, and
`tests/SharpEmu.Libs.Tests/Agc/AgcWaitRegMemTests.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. The KajPS5 destination source
files use `GPL-2.0-only`, matching KytyPS5. The behavior tests use
`GPL-2.0-or-later`.

The `sceAgcDriverSubmitDcb` and `sceAgcDriverSubmitAcb` handlers in
`src/hle/agc_exports.*` adapt the packet layout, NIDs, and queue split from
KytyPS5 `src/libs/agc.cpp` and `src/libs/libGraphicsDriver.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. Their descriptor validation,
graphics and owner-scoped compute queues, and wait-resume tests also re-express
SharpEmu `src/SharpEmu.Libs/Agc/AgcExports.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`.

The direct and multi-buffer submission handlers in `src/hle/agc_exports.*`
and the transactional batch queue in `src/gpu/submission_queue.*` adapt the
entry-point signatures, NIDs, and queue selection from KytyPS5
`src/libs/agc.h`, `src/libs/agc.cpp`, and `src/libs/libGraphicsDriver.cpp` at
commit `a65d17a5d689257a35644e01e9d15539361f0bf0`. The multi-DCB array ABI and
4096-entry bound are independently confirmed by SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. KajPS5 adds checked guest reads
and all-or-nothing queue commits. No upstream queue implementation was copied.

The AGC NOP-wrapper `WRITE_DATA` encoding, the separate standard PM4 control
decoder, and the checked ordered guest-memory effect in
`src/gpu/runtime.cpp` and `src/gpu/command_processor.*` re-express SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. The standard-control regression
keeps its low reserved byte deliberately nonzero. The memory-target behavior
also follows KytyPS5 `src/graphics/guest_gpu/graphicsRun.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`, but retains KajPS5 checked guest
access instead of using raw pointers.

The `sceAgcCbReleaseMem` packet writer, decoder, and ordered guest-memory
effects in `src/gpu/runtime.*`, `src/gpu/command_processor.*`, and
`src/hle/agc_exports.*` adapt KytyPS5 `src/libs/agc.cpp`,
`src/libs/libGraphicsDriver.cpp`, and
`src/graphics/guest_gpu/command_processor/pm4Handlers.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. The standard-packet decoder and
monotonic counter regression re-express SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. KajPS5 applies the effects through
checked guest memory and does not import either upstream's GPU owner.

The DCB and ACB `EVENT_WRITE` packet writers, decoder, graphics event bridge,
and `sceAgcDriverAddEqEvent` and `sceAgcDriverDeleteEqEvent` handlers in
`src/gpu/runtime.*`, `src/gpu/command_processor.*`, and
`src/hle/agc_exports.*` adapt packet layouts and export identities from
KytyPS5 `src/libs/agc.cpp`, `src/libs/libGraphicsDriver.cpp`,
`src/graphics/guest_gpu/command_processor/pm4Handlers.cpp`, and
`src/graphics/host_gpu/renderer/sync.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. Filter-based delivery and the
focused regression re-express SharpEmu
`src/SharpEmu.Libs/Agc/AgcExports.cs`,
`src/SharpEmu.Libs/Kernel/KernelEventQueueCompatExports.cs`, and
`tests/SharpEmu.Libs.Tests/Agc/AgcEventQueueTests.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. KajPS5 keeps its existing event
queue and GPU owner. No upstream graphics runtime was copied.

The SharpEmu reference file states:

`Copyright (C) 2026 SharpEmu Emulator Project`

No SharpEmu graphics source or runtime was copied. KajPS5 keeps one C++ GPU
owner and does not load an external compatibility library.

The Gen5 shader decoder under
`src/gpu/shader/recompiler/decompiler` directly adapts the KytyPS5
`src/graphics/shader/recompiler/decompiler` source set. `ImageOps.cpp`,
`ShaderDecoder.cpp`, and `ShaderDecoder.h` follow commit
`59b8fad34189816137c5cbe1982e9fd499532b6f`; the remaining decoder files
retain their predecessor provenance at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. The SPIR-V section builder in
`src/gpu/shader/spirv_builder.*` directly adapts KytyPS5
`src/graphics/shader/recompiler/emitter/SpirvBuilder.*` at the same commit.
The destination files keep `GPL-2.0-only`, matching KytyPS5. The adaptation
changes namespaces, removes KytyPS5 common-library coupling, and uses the C++20
standard formatting library. Opcode tables, instruction families, operand
metadata, diagnostic text, section order, and SPIR-V version remain aligned
with the pinned source.

The focused tests in `tests/gpu_shader_decoder_test.cpp` and
`tests/gpu_spirv_builder_test.cpp` re-express KytyPS5
`tests/shaderCfgTests.cpp` and SharpEmu
`src/SharpEmu.ShaderCompiler.Vulkan/SpirvModuleBuilder.cs` and
`scripts/validate-synthetic-spirv.sh` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. No SharpEmu compiler source was
copied.

The shader control-flow and IR implementation under
`src/gpu/shader/recompiler/cfg` and `src/gpu/shader/recompiler/ir` directly
adapts KytyPS5 `src/graphics/shader/recompiler/cfg` and
`src/graphics/shader/recompiler/ir` at commit
`59b8fad34189816137c5cbe1982e9fd499532b6f`. Its compiler-facing GPU types,
format tables, bindings, wave-mask helper, and buffer-format metadata adapt
KytyPS5 `src/graphics/guest_gpu/gpu_defs.h`,
`src/graphics/guest_gpu/gpu_format.*`, `src/graphics/shader/shaderBindings.h`,
`src/graphics/shader/shader.h`, and
`src/graphics/shader/recompiler/{BufferFormat,ExecMask}.*` at the same commit.
The destination files keep `GPL-2.0-only`, matching KytyPS5. KajPS5 retains
its existing GPU runtime and imports only compiler data and algorithms.

The focused CFG and IR tests in `tests/gpu_shader_cfg_test.cpp` and
`tests/gpu_shader_ir_test.cpp` re-express KytyPS5
`tests/shaderCfgTests.cpp` at commit
`59b8fad34189816137c5cbe1982e9fd499532b6f`. The scalar merge cases also
re-express SharpEmu
`tests/SharpEmu.ShaderCompiler.Tests/Gen5ScalarSsaTests.cs`, and the explicit
floating-point sign-modifier case follows
`src/SharpEmu.ShaderCompiler.Vulkan/Gen5SpirvTranslator.Alu.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. The SharpEmu files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

No SharpEmu compiler source was copied.

The SPIR-V emitter and public recompiler entry point under
`src/gpu/shader/recompiler/emitter`,
`src/gpu/shader/recompiler/ShaderRecompiler.*`, and
`src/gpu/shader/bindings.cpp` directly adapt KytyPS5
`src/graphics/shader/recompiler/emitter/*`,
`src/graphics/shader/recompiler/ShaderRecompiler.*`, and
`src/graphics/shader/shaderBindings.cpp` at commit
`59b8fad34189816137c5cbe1982e9fd499532b6f`. They retain the
`GPL-2.0-only` provenance headers and use KajPS5's existing C++20 SPIR-V
section builder, checked SRT reader, and runtime resource snapshots.

`tests/gpu_shader_emitter_test.cpp` applies focused behavioral coverage
informed by SharpEmu
`src/SharpEmu.ShaderCompiler.Vulkan/Gen5SpirvTranslator.Alu.cs` and
`tests/SharpEmu.ShaderCompiler.Tests/Gen5ScalarSsaTests.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. No SharpEmu compiler source
was copied.

The optional Vulkan owner in `src/gpu/vulkan/{loader,device,execution}.*`,
`src/gpu/vulkan/vulkan_types.h`, `src/gpu/runtime.*`, and
`tests/gpu_vulkan_{device,execution,smoke,compute_smoke}_test.cpp` adapts
KytyPS5's instance, physical-device, universal queue, logical-device,
queue-mutex, command-scheduler, and completion-ownership shape from
`src/graphics/host_gpu/vulkanInstance.h`,
`src/graphics/presentation/window/vulkanWindow.cpp`, and
`src/graphics/host_gpu/renderer/{context,commandScheduler,masterSemaphore,render}.*` at commit
`fb5ecec455cf6c67154134429485ffccbfc34203`. It preserves KytyPS5's complete
non-surface renderer-ready core feature baseline, but intentionally does not
import a window, surface, allocator, renderer, global dispatch table, or a
second GPU runtime. The child compute owner accepts precompiled SPIR-V and
owns one command/fence/pipeline transaction per dispatch until completion.
Device ranking, capability diagnostics, finite timeout retention, later fence
status collection, and focused injected tests re-express the device-scoring,
capability reporting, and abandoned-submission behavior in SharpEmu
`src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs` and
`tests/SharpEmu.Libs.Tests/VideoOut/VulkanPhysicalDeviceScoringTests.cs` at
commit `4b5ea6a79346cb4529fa531cf2c1973f3978eb22`. The SharpEmu reference
states `Copyright (C) 2026 SharpEmu Emulator Project`. KajPS5 creates no
second GPU runtime or global Vulkan dispatch; it dynamically loads one
optional, runtime-owned device context.

The exact declarations required by that context are vendored from the
KytyPS5 `3rdparty/Vulkan-Headers` submodule commit
`2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350`, which is the
KhronosGroup/Vulkan-Headers commit used by the pinned KytyPS5 tree. The import
contains only `include/vulkan/{vulkan.h,vulkan_core.h,vk_platform.h}` and the
required `include/vk_video/*.h` transitive headers, at
`src/gpu/vulkan/third_party/vulkan_headers/include/`. These files retain their
original `Copyright 2014-2025 The Khronos Group Inc.` or
`Copyright 2015-2025 The Khronos Group Inc.` notices and
`SPDX-License-Identifier: Apache-2.0`. The exact upstream `LICENSE.md` is
preserved at `LICENSES/Vulkan-Headers-LICENSE.md`. The standard Apache-2.0
license text is separately preserved at `LICENSES/Apache-2.0.txt`, copied from
the upstream `LICENSES/Apache-2.0.txt` at the same commit. No Vulkan loader,
SDK binary, or import library is redistributed or required for a build.

## Files

The open, close, read, positioned-read, seek, stat, fstat,
reachability, and directory-read handlers in
`src/hle/kernel_file_exports.cpp` and
`tests/hle_kernel_file_exports_test.cpp` follow behavior in KytyPS5
`src/kernel/fileSystem.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs` at the pinned commits.
They use KytyPS5's kernel-compatible file errors and the file-operation NIDs
confirmed by both references. The 120-byte stat field layout matches both
references. The stable path inode adapts SharpEmu's deterministic FNV-1a
file-entry hashing behavior. The directory tests adapt SharpEmu's captured
entry list, `.` and `..` prefix, case-insensitive ordering, typed descriptor
failure, fixed 512-byte record behavior, and optional directory base-position
output. No file-service source was copied.

The read-only host mount in `src/kernel/virtual_file_system.*`, its connection
to the existing file service, and `tests/host_vfs_test.cpp` follow the mount
boundary in KytyPS5 `src/kernel/fileSystem.{h,cpp}` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. Path normalization, default-deny
resolution, host case behavior, canonical root confinement, and symlink escape
tests adapt behavior from SharpEmu `src/SharpEmu.Core/IFileSystem.cs`,
`src/SharpEmu.Core/PhysicalFileSystem.cs`,
`tests/SharpEmu.Libs.Tests/Kernel/KernelPathCaseSensitivityTests.cs`, and
`tests/SharpEmu.Libs.Tests/Kernel/KernelSandboxEscapeTests.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KajPS5 keeps mount state in its
single C++ file-service owner and exposes only read access. No upstream
file-service source was copied.

Adjacent module discovery in `src/loader/module_loader.*` and
`tests/module_loader_test.cpp` adapts KytyPS5's `.prx` and `.sprx` selection,
core-runtime exclusion, and `sce_module` and `sce_modules` search from
`src/loader/runtimeLinker.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. Its stable enumeration, bounded
complete reads, and checked parse-before-use boundary also adapt SharpEmu
`src/SharpEmu.Core/Runtime/SharpEmuRuntime.cs` and
`src/SharpEmu.Core/Loader/SelfLoader.cs` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f`. KajPS5 uses its existing C++
file service, ELF parser, and module planner. No upstream loader source was
copied.

The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

No upstream file-service, import-resolver, or stub source was copied.

## Pthreads

The pthread state in `src/kernel/pthread.cpp`, its handlers in
`src/hle/kernel_pthread_exports.cpp`, and
`tests/hle_kernel_pthread_exports_test.cpp` adapt behavior from KytyPS5
`src/kernel/pthread.h`, `src/kernel/pthread.cpp`, and `src/libs/libKernel.cpp`
at commit `f6e01e54031a3c615f089f061a4eab2f3c59acba`, and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelExports.cs`,
`src/SharpEmu.Libs/Kernel/KernelPthreadCompatExports.cs`, and
`src/SharpEmu.Libs/Kernel/KernelPthreadExtendedCompatExports.cs` at commit
`d5108e854d609808f17093a6f5dbbc711d09ad2e`. Attribute defaults, the 16 KiB
minimum stack, the 256-key limit, and POSIX error results follow KytyPS5.
Synthetic guest attribute handles, checked guest writes, and guest-thread TLS
isolation adapt SharpEmu behavior. Thread creation keeps KytyPS5's entry,
argument, attribute, and return-value model while adapting SharpEmu's checked
guest output and shared-scheduler start, join, and exit flow. The SharpEmu
mutex behavior also supplies checked static initialization, direct waiter
handoff, distinct type behavior, and abandoned-owner cleanup. KytyPS5 supplies
the mutex attribute defaults, accepted types and protocols, guest ABI, and
POSIX error mapping. Condition names, NIDs, and absolute-time semantics follow
KytyPS5. Synthetic condition handles, checked static initialization, FIFO
waiters, busy destruction, atomic mutex release, signal and broadcast
selection, required mutex reacquisition, checked absolute-time conversion, and
separate timeout completion adapt SharpEmu behavior. Relative and absolute
timeout signatures, monotonic waiting, timeout results, and NIDs follow
KytyPS5. The SharpEmu files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

KytyPS5 identifies as GPL-2.0-only. SharpEmu identifies as
GPL-2.0-or-later. No upstream pthread source was copied verbatim, and KajPS5
keeps all pthread state in its one kernel runtime and scheduler.

The C++ mutex handlers in `src/hle/libc_thread_exports.cpp` and their focused
test adapt the full `_Mtx_*` ABI, result values, recursive flag, and scheduler
connection from KytyPS5 `src/libs/libC.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. SharpEmu
`scripts/ps5_names.txt` at commit
`7c9740fee8a633e17b145c6bc6d794e41d46c73f` confirms the public mutex names.
KajPS5 uses its existing checked guest memory and pthread owner. It also
reports real current-thread ownership instead of KytyPS5's fixed placeholder.
No upstream mutex source was copied verbatim.

## Semaphores, event flags, and event queues

The non-blocking semaphore handlers in
`src/hle/kernel_semaphore_exports.cpp` and
`tests/hle_kernel_semaphore_exports_test.cpp` follow behavior in KytyPS5
`src/kernel/semaphore.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelSemaphoreCompatExports.cs` at the pinned
commits. They use the NIDs confirmed by both references and KytyPS5's
kernel-compatible semaphore errors. No semaphore source was copied.

The non-blocking event-flag handlers in
`src/hle/kernel_event_flag_exports.cpp` and
`tests/hle_kernel_event_flag_exports_test.cpp` follow behavior in KytyPS5
`src/kernel/eventFlag.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelEventFlagCompatExports.cs` at the pinned
commits. They use the NIDs confirmed by both references, KytyPS5's
kernel-compatible errors, and SharpEmu's rule that an optional result-pattern
write must succeed before clear-mode mutation. No event-flag source was copied.

The typed event-queue architecture in `src/kernel/event_queue.h` and
`src/kernel/event_queue.cpp`, the nonblocking handlers in
`src/hle/kernel_event_queue_exports.cpp`, and their matching tests adapt
behavior from KytyPS5 `src/kernel/eventQueue.h` and
`src/kernel/eventQueue.cpp` at commit
`f6e01e54031a3c615f089f061a4eab2f3c59acba`, and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelEventQueueCompatExports.cs` at commit
`816ec4ad27662bb8e505aeeec65ef9c621478d6c`. The implementation uses Kyty's
typed user-event contract and adapts SharpEmu's user-event data placement,
edge and level delivery, per-waiter reservation, and deleted-queue completion
behavior. The SharpEmu reference states:

`Copyright (C) 2026 SharpEmu Emulator Project`

KytyPS5 identifies as GPL-2.0-only. SharpEmu identifies as
GPL-2.0-or-later. No upstream event-queue source was copied verbatim.

The scheduler-backed `sceKernelWaitEqueue` handler, 32-byte event record,
microsecond timeout behavior, and event-field accessors in
`src/kernel/event_queue.*` and `src/hle/kernel_event_queue_exports.*` adapt
the ABI and field rules from KytyPS5 `src/kernel/eventQueue.h`,
`src/kernel/eventQueue.cpp`, and `src/libs/libKernel.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. The checked record writes,
continuation retry, and late-event timeout regression re-express SharpEmu
`src/SharpEmu.Libs/Kernel/KernelEventQueueCompatExports.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. KajPS5 uses its existing event
queue, clock, scheduler, and HLE continuation. No upstream wait loop was
copied.

Graphics-filter registration and delivery in `src/kernel/event_queue.*` and
`tests/kernel_event_queue_test.cpp` adapt KytyPS5
`src/kernel/eventQueue.cpp` and
`src/graphics/host_gpu/renderer/sync.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. Filter-based delivery when the
hardware event type differs from the guest identifier re-expresses SharpEmu
`src/SharpEmu.Libs/Kernel/KernelEventQueueCompatExports.cs` and
`tests/SharpEmu.Libs.Tests/Agc/AgcEventQueueTests.cs` at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. KajPS5's internal registration
generation is original code that rejects stale reserved events.

The `sceAgcDriverGetEqEventType` and `sceAgcDriverGetEqContextId` handlers in
`src/hle/agc_exports.*` adapt KytyPS5 `src/libs/agc.cpp` and
`src/libs/libGraphicsDriver.cpp` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0`. The focused graphics-event case
also uses SharpEmu's independent evidence that the registered event ID and
hardware event type occupy separate event fields at commit
`5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`.

## Contributor guidance

The AI-use guidance in `CONTRIBUTING.md` reproduces the contributor rules from
KytyPS5 `README.md` at commit
`a65d17a5d689257a35644e01e9d15539361f0bf0` and SharpEmu
`CONTRIBUTING.md` at commit
`d5108e854d609808f17093a6f5dbbc711d09ad2e`. The pull-request checklist also
uses SharpEmu `.github/pull_request_template.md` at that commit. The SharpEmu
reference states:

`Copyright (C) 2026 SharpEmu Emulator Project`

KytyPS5 identifies as GPL-2.0-only. SharpEmu identifies as
GPL-2.0-or-later. The combined guidance is distributed under GPL-2.0-only.

## New adaptations

Add an entry when a later change imports or adapts code. Include the project,
commit, source path, destination path, copyright notice, and license.
