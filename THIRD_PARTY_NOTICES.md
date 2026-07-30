# Third-party notices

The current source does not copy code from KytyPS5 or SharpEmu. Both projects
serve as research references. The reviewed files and pinned commits are listed
in `docs/stage1-loader.md`, `docs/stage2-kernel.md`, `docs/stage2-cpu.md`, and
`docs/stage2-hle.md`.

## Kernel behavior

The clock, event-flag, file, semaphore, and scheduler behavior in `src/kernel/`
and the matching `tests/kernel_*_test.cpp` files was based on behavior observed
in KytyPS5 and SharpEmu. The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

Both references are GPL-2.0-or-later. No upstream host executor, continuation
system, ownership model, or source was copied.

## Loader and guest memory

The dynamic-table, dynamic-string, symbol, and `RELA` behavior in
`src/loader/elf.cpp`, `src/loader/relocator.cpp`,
`tests/elf_dynamic_test.cpp`, `tests/elf_sce_dynamic_test.cpp`,
`tests/elf_relocation_test.cpp`, and `tests/elf_symbol_test.cpp` was based on
behavior observed in the pinned KytyPS5 and SharpEmu loader files. The PS5
metadata parser follows KytyPS5's `PT_OS_DYNLIBDATA`, packed module, packed
library, and SCE dynamic-tag semantics. It adapts SharpEmu's preference for
SCE table tags and its fixed-size fallback when optional SCE entry-size tags
are absent.

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
references. The query uses KytyPS5's exclusive range end. No memory-export
source was copied.

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
from `src/SharpEmu.Core/Loader/SelfLoader.cs` at the pinned commits. The
test-only HLE call path in `tests/hle_public_guest_test.cpp` also follows the
import thunk and dispatch behavior in those pinned projects. No thunk or
resolver source was copied.

The bounded trace in `src/loader/relocation_trace.cpp` and
`tests/relocation_trace_test.cpp` adapts SharpEmu's focus on structured,
bounded import diagnostics. No diagnostic source was copied.

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

The AI-assisted contribution guidance in `CONTRIBUTING.md` and
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
