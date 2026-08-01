# Third-party notices

The current source does not copy code from KytyPS5 or SharpEmu. Both projects
serve as research references. Each stage document records the exact commit
used for its behavior, even after the current reference pin moves. Current
pins and refresh reviews are in `docs/upstreams.md`.

## Kernel behavior

The clock, event-flag, file, semaphore, and scheduler behavior in `src/kernel/`
and the matching `tests/kernel_*_test.cpp` files was based on behavior observed
in KytyPS5 and SharpEmu. Scheduler deadlines use KytyPS5's monotonic deadline
model and adapt SharpEmu's separate timeout completion state without adding a
host timer thread. The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

Both references are GPL-2.0-or-later. No upstream host executor, continuation
system, ownership model, or source was copied.

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
The relocation planner adapts SharpEmu's checked formulas and write widths for
PC-relative, 32-bit, symbol-size, and `RELATIVE64` relocations. A narrow value
must fit its signed or unsigned target before the planner writes guest memory.
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
region model and uses the host mapping as that model's storage. No upstream
virtual-memory, loader, or executor source was copied.

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

The process-time handlers in `src/hle/kernel_clock_exports.cpp` and
`tests/hle_kernel_clock_exports_test.cpp` adapt the matching process-time,
counter, and frequency behavior from the pinned KytyPS5
`src/kernel/pthread.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelRuntimeCompatExports.cs`. No clock-export
source was copied. Clock NIDs are confirmed by both pinned references.
The clock-gettime and gettimeofday handlers use KytyPS5's kernel-compatible
`EFAULT` and `EINVAL` values. SharpEmu marks its differing Gen5 error values as
synthetic.

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

Both upstreams are GPL-2.0-or-later. No upstream pthread source was copied
verbatim, and KajPS5 keeps all pthread state in its one kernel runtime and
scheduler.

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

Both upstreams are GPL-2.0-or-later. No upstream event-queue source was copied
verbatim. Blocking wait dispatch remains deferred until the runtime can resume
a saved guest continuation.

## Contributor guidance

The contribution quality guidance in `CONTRIBUTING.md` and
`.github/pull_request_template.md` adapts policy concepts from KytyPS5
`README.md` at commit `f6e01e54031a3c615f089f061a4eab2f3c59acba`,
and SharpEmu `CONTRIBUTING.md` and `.github/pull_request_template.md` at commit
`d5108e854d609808f17093a6f5dbbc711d09ad2e`. The SharpEmu reference states:

`Copyright (C) 2026 SharpEmu Emulator Project`

Both references are GPL-2.0-or-later. The KajPS5 text is adapted for this
project and does not copy the upstream wording verbatim.

## New adaptations

Add an entry when a later change imports or adapts code. Include the project,
commit, source path, destination path, copyright notice, and license.
