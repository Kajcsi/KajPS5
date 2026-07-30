# Stage 2 HLE research

KajPS5 has one import registry for future HLE trampolines. Each entry contains
a library name, symbol name, and nonzero target address. Exact duplicates are
rejected. Lookup follows the ELF's ordered needed-library list, while an
unscoped lookup succeeds only when one library owns the symbol.

The design review used these pinned references:

- KytyPS5 commit `f6e01e54031a3c615f089f061a4eab2f3c59acba`, symbol and
  library lookup in `src/loader/runtimeLinker.cpp`.
- SharpEmu commit `d5108e854d609808f17093a6f5dbbc711d09ad2e`, import handling
  in `src/SharpEmu.Core/Cpu/Native/StubManager.cs` and
  `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs`, plus the
  checked context and export boundary in `src/SharpEmu.HLE/CpuContext.cs` and
  `src/SharpEmu.HLE/ExportedFunction.cs`.

The C++ registry copies neither upstream ownership model nor executor. It gives
relocation and HLE dispatch one predictable name lookup. Resolved
`R_X86_64_GLOB_DAT` and `R_X86_64_JUMP_SLOT` targets are written only after the
entire relocation plan passes validation. Missing symbols stay unchanged and
produce structured diagnostics. A trace shows at most 32 imports and 128
hex-encoded input bytes per name, so guest data cannot inject lines or produce
unbounded output.

The registry does not generate general executable stubs. One redistributable
test ELF goes through parse, load, link, and native leaf execution to call a
no-argument HLE handler. It adjusts its stack for the host ABI, but it is not a
PS5 ABI bridge.

The platform-neutral HLE call context maps the six System V integer argument
registers and the return register. Integer and string access use guest memory,
and string reads stop at 4 KiB. If a bulk read crosses an unmapped boundary,
the context checks bytes individually so it can still accept an earlier null
terminator. A missing terminator and a memory fault remain distinct results.
Native trampolines do not capture this state yet.

The HLE export registry keeps C++ context handlers separate from executable
import targets. Dispatch uses the same ordered library scope as linking. An
ambiguous unscoped name does not run. The registry copies the selected handler
while locked, releases the lock, and only then calls it. Memory faults inside a
handler stay distinct from lookup failures.

The executable trace also compares required import relocations with the
built-in HLE registry. It reports both relocation references and unique
imports, ignores permitted weak imports, and never calls a handler or writes a
synthetic address. Repeated imports are grouped like SharpEmu's useful import
inventory, while lookup preserves KytyPS5's library scope. Missing imports are
ranked by relocation count. Names and scope use a bounded hex format, so guest
text cannot add trace lines.

Known runtime data never points into host memory. Startup maps one checked
16 KiB guest page for the stack guard, process name, and two libc need flags.
The four exact library and NID pairs come from KytyPS5. The bounded process
name, duplicate terminator canary, and useful data-symbol set adapt SharpEmu.
Registration is atomic. A conflict removes the new page and keeps the earlier
registry state. The normal relocation pass can then bind these data imports to
real guest addresses.

The first `libc` batch implements `__cxa_guard_acquire`,
`__cxa_guard_release`, and `__cxa_guard_abort`. It preserves the upper six
bytes of each guest guard word. One guest thread owns initialization, a
recursive acquire returns zero, and another guest thread blocks through the
shared scheduler. Release publishes the complete bit and wakes waiters. Abort
clears the low guard state and also wakes waiters. A blocked handler returns a
distinct HLE status; the general executor still needs a continuation path to
resume that call.

The first `libkernel` handler batch exposes
`sceKernelGetProcessTime`, `sceKernelGetProcessTimeCounter`, and
`sceKernelGetProcessTimeCounterFrequency`. All three use the shared kernel
clock service, so the microsecond value, nanosecond counter, and one-gigahertz
frequency agree. Batch registration validates every definition before it
changes the export table.

`sceKernelClockGettime` uses the same clock service and writes its 16-byte
timespec in one operation. A bad guest range leaves memory unchanged.
Guest-visible `EFAULT` and `EINVAL` values follow KytyPS5. SharpEmu labels its
different Gen5 values as synthetic, so KajPS5 does not use them as the kernel
ABI. `sceKernelGettimeofday` applies the same whole-range rule to its seconds
and microseconds fields.

`sceKernelMprotect`, `sceKernelMunmap`, and their POSIX aliases change the guest
memory owned by the active call context. Protection uses 16 KiB guest pages.
Unknown flags and overflowing ranges fail before a region changes, and GPU-only
flags never grant CPU access. Unmap requires a fully mapped range and clears
the released bytes. `getpagesize` reports the same 16 KiB size.
`sceKernelQueryMemoryProtection` returns the canonical start, exclusive end,
and full CPU/GPU protection mask. It checks every optional output before
writing any of them.

`sceKernelMapFlexibleMemory` and its named variants map 16 KiB-aligned ranges
inside the same guest address space. A nonfixed request uses the input address
as a search hint and returns the first aligned gap. A zero hint tries the PS5
default window, then the available guest range. Fixed mappings require an
aligned free range; they never replace an existing mapping. The handler checks
the output pointer, flags, protection, size, and optional 31-byte name before
mapping. A rejected request does not change the range table or output value.

The kernel runtime also owns one 13.5 GiB direct-memory range allocator.
`sceKernelGetDirectMemorySize`, availability queries, direct and main-direct
allocation, and checked or unchecked release use that shared allocator.
Allocation is first fit inside the requested physical range. Availability
returns the largest aligned gap. Partial release splits an allocation and
adjacent free ranges coalesce. Invalid output pointers cannot consume a range.
The three direct-map variants check the physical allocation, protection,
flags, alignment, output pointer, and optional 31-byte name before they map a
guest range. Fixed maps never replace an existing range. Nonfixed maps use a
hinted first-fit search, and seventh arguments are read from the checked guest
stack. `munmap` removes direct-alias records, and a physical allocation cannot
be released while an alias remains.

Direct mappings share one sparse physical backing store. Aliases see the same
contents across 16 KiB page boundaries, and contents survive unmap and remap.
Releasing an allocation clears its committed pages. The v2 memory type is
accepted but is not applied per mapping yet.
