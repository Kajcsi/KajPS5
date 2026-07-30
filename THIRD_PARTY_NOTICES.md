# Third-party notices

The current source does not copy code from KytyPS5 or SharpEmu. Both projects
serve as research references. The reviewed files and pinned commits are listed
in `docs/stage1-loader.md`, `docs/stage2-kernel.md`, `docs/stage2-cpu.md`, and
`docs/stage2-hle.md`.

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

The bounded trace in `src/loader/relocation_trace.cpp` and
`tests/relocation_trace_test.cpp` adapts SharpEmu's focus on structured,
bounded import diagnostics. No diagnostic source was copied.

The read-only HLE coverage inventory in `src/hle/import_coverage.cpp` and
`tests/hle_import_coverage_test.cpp` follows KytyPS5's scoped symbol lookup and
adapts SharpEmu's ordered unique-NID inventory and relocation descriptor
counts from `src/SharpEmu.Core/Loader/SelfLoader.cs` at the pinned commits. It
does not copy upstream source or create SharpEmu import stubs.

The register and memory boundary in `src/hle/call_context.cpp` and
`tests/hle_call_context_test.cpp` follows behavior in SharpEmu's
`src/SharpEmu.HLE/CpuContext.cs`. No context source was copied.

The scoped handler table in `src/hle/export_registry.cpp` and
`tests/hle_export_registry_test.cpp` follows behavior in SharpEmu's
`src/SharpEmu.HLE/ExportedFunction.cs` and KytyPS5's native symbol database.
No export-registry source was copied.

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
`d5108e854d609808f17093a6f5dbbc711d09ad2e`. The implementation uses Kyty's
typed user-event contract and SharpEmu's deterministic pending-trigger
coalescing behavior. The SharpEmu reference states:

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
