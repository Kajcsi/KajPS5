# Third-party notices

The current source does not copy source code from KytyPS5 or SharpEmu. It uses
both projects as research references. See `docs/stage1-loader.md`,
`docs/stage2-kernel.md`, `docs/stage2-cpu.md`, and `docs/stage2-hle.md` for the
reviewed files and pinned commits.

The clock, event-flag, file, semaphore, and scheduler behaviors in `src/kernel/`,
`tests/kernel_event_flag_test.cpp`, `tests/kernel_event_wait_test.cpp`, and
`tests/kernel_clock_test.cpp`, `tests/kernel_file_test.cpp`,
`tests/kernel_scheduler_test.cpp`, and `tests/kernel_semaphore_test.cpp` were
implemented from focused observations of KytyPS5 and SharpEmu. The SharpEmu
reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

Both references are GPL-2.0-or-later. No upstream host executor, continuation
system, ownership model, or source code was copied.

The checked dynamic-table, standard dynamic-string, and standard `RELA`
behavior in
`src/loader/elf.cpp`, `src/loader/relocator.cpp`,
`tests/elf_dynamic_test.cpp`, `tests/elf_relocation_test.cpp`, and
`tests/elf_symbol_test.cpp` was implemented
from focused observations of the pinned KytyPS5 and SharpEmu loader files. The
SharpEmu reference file states:

`Copyright (C) 2026 SharpEmu Emulator Project`

No upstream loader source code was copied.

The transactional guest-memory protection and unmap behavior in
`src/core/memory/guest_memory.cpp` and `tests/guest_memory_test.cpp` uses
focused observations from KytyPS5 `src/common/virtualMemory.h` and
`src/common/virtualMemory.cpp`, and SharpEmu
`src/SharpEmu.HLE/IGuestAddressSpace.cs` and
`src/SharpEmu.Core/Memory/PhysicalVirtualMemory.cs` at the pinned commits.
No upstream virtual-memory source code was copied.

The checked memory HLE handlers in `src/hle/kernel_memory_exports.cpp` and
`tests/hle_kernel_memory_exports_test.cpp` use focused behavior observations
from KytyPS5 `src/kernel/memory.cpp` and `src/libs/libKernel.cpp`, and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs` at the pinned commits.
They use the 16 KiB guest page size and the `mprotect`, `munmap`,
`getpagesize`, and memory-protection query names and NIDs confirmed by both
references. The query uses KytyPS5's exclusive range end. No memory-export
source code was copied.

The W^X native leaf-execution behavior in
`src/cpu/native_leaf_executor.cpp` and
`tests/native_leaf_executor_test.cpp` was implemented from focused
observations of the pinned KytyPS5 and SharpEmu host-memory and native-executor
files. The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

No upstream executor or host-memory source code was copied.

The library-scoped HLE import lookup and checked relocation connection in
`src/hle/import_registry.cpp`, `tests/hle_import_registry_test.cpp`, and
`tests/hle_import_link_test.cpp` was implemented from focused observations of
the pinned KytyPS5 and SharpEmu import resolvers. The test-only HLE call path in
`tests/hle_public_guest_test.cpp` also uses focused observations of import
thunks and dispatch in those pinned projects. No thunk source code was copied.
The bounded trace in `src/loader/relocation_trace.cpp` and
`tests/relocation_trace_test.cpp` adapts SharpEmu's focus on structured,
bounded import diagnostics. No diagnostic source code was copied.
The checked register and memory boundary in `src/hle/call_context.cpp` and
`tests/hle_call_context_test.cpp` uses focused behavior observations from
SharpEmu's `src/SharpEmu.HLE/CpuContext.cs`. No context source code was copied.
The scoped handler table in `src/hle/export_registry.cpp` and
`tests/hle_export_registry_test.cpp` uses focused behavior observations from
SharpEmu's `src/SharpEmu.HLE/ExportedFunction.cs` and KytyPS5's native symbol
database. No export registry source code was copied.
The process-time handlers in `src/hle/kernel_clock_exports.cpp` and
`tests/hle_kernel_clock_exports_test.cpp` adapt the matching process-time,
counter, and frequency behavior from the pinned KytyPS5
`src/kernel/pthread.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelRuntimeCompatExports.cs`. No clock export
source code was copied. Clock NIDs are confirmed by both pinned references.
The clock-gettime and gettimeofday handlers use KytyPS5's kernel-compatible
`EFAULT` and `EINVAL` values. SharpEmu marks its differing Gen5 error values as
synthetic.
The checked open, close, read, positioned-read, seek, stat, fstat, and
reachability handlers in
`src/hle/kernel_file_exports.cpp` and
`tests/hle_kernel_file_exports_test.cpp` use focused behavior observations from
KytyPS5 `src/kernel/fileSystem.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs` at the pinned commits.
They use KytyPS5's kernel-compatible file errors and the file-operation NIDs
confirmed by both references. The 120-byte stat field layout matches both
references. The stable path inode adapts SharpEmu's deterministic FNV-1a
file-entry hashing behavior. No file-service source code was copied.
The SharpEmu reference files state:

`Copyright (C) 2026 SharpEmu Emulator Project`

No upstream import resolver or stub source code was copied.

The non-blocking semaphore handlers in
`src/hle/kernel_semaphore_exports.cpp` and
`tests/hle_kernel_semaphore_exports_test.cpp` use focused behavior observations
from KytyPS5 `src/kernel/semaphore.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelSemaphoreCompatExports.cs` at the pinned
commits. They use the NIDs confirmed by both references and KytyPS5's
kernel-compatible semaphore errors. No semaphore source code was copied.

The non-blocking event-flag handlers in
`src/hle/kernel_event_flag_exports.cpp` and
`tests/hle_kernel_event_flag_exports_test.cpp` use focused behavior
observations from KytyPS5 `src/kernel/eventFlag.cpp` and SharpEmu
`src/SharpEmu.Libs/Kernel/KernelEventFlagCompatExports.cs` at the pinned
commits. They use the NIDs confirmed by both references, KytyPS5's
kernel-compatible errors, and SharpEmu's checked optional result-pattern
write before clear-mode mutation. No event-flag source code was copied.

Add an entry here when a later change imports or adapts code. Each entry must
state the project, commit, source path, destination path, copyright notice, and
license.
