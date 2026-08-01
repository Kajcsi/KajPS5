# Stage 2 HLE research

KajPS5 builds one native HLE import table for each executable. The table owns
one trampoline for each resolved library and NID pair. It stays alive for the
relocation and execution lifetime. Lookup follows the ELF's ordered
needed-library list, while an unscoped lookup succeeds only when one library
owns the symbol.

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

The runtime inventories referenced imports, resolves each library-scoped NID,
builds executable trampolines, and gives the complete table to relocation.
Guest-owned data symbols remain a lower-priority resolver layer. One
redistributable test ELF goes through parse, load, automatic table creation,
link, and native leaf execution to call a checked HLE handler. Each trampoline
preserves the six System V integer registers and a declared, bounded number of
stack arguments across the Windows or POSIX host call. It also captures
XMM0-XMM7 and returns the handler's `RAX` and optional XMM0-XMM1 values.

The platform-neutral HLE call context maps the six System V integer argument
registers and the return register. Integer and string access use guest memory,
and string reads stop at one MiB. If a bulk read crosses an unmapped boundary,
the context checks bytes individually so it can still accept an earlier null
terminator. A missing terminator and a memory fault remain distinct results.
The first native trampoline captures those six integer registers, XMM0-XMM7,
and up to 16 declared stack arguments. A larger execution context still needs
the remaining machine state.

The libc heap service reserves checked read-write ranges inside guest memory.
It supports allocation, release, clearing, resizing, aligned allocation, and
usable-size queries. Allocation counts and copy sizes are bounded. Failed
requests return the libc result without mapping partial guest ranges.
Caller-owned mspaces use the same service but keep their own bounded free
ranges and allocation records. Destroying an mspace does not unmap the memory
that its caller supplied.

Common `memcpy`, `memmove`, `memset`, and `strcmp` calls use whole-range
guest-memory checks. Failed copies do not change the destination. Valid
copies use the coherent guest backing directly, including overlapping ranges.
The native HLE trampoline sends valid `memcpy` and `memmove` imports straight
to this checked copy operation. A rejected fast copy returns to the normal HLE
handler, so fault behavior does not change. Scalar libc math reads its System
V inputs from XMM registers and returns results through XMM0. The C++ scalar
allocation calls use the same checked libc heap. A stack-canary failure
returns a fatal guest status instead of pretending that execution can
continue.

Libc string scans have a one-megabyte limit. `wcscmp` reads little-endian
16-bit guest units, independent of the host's `wchar_t` size. `atof` parses a
bounded guest byte string and returns through XMM0. `sincos` and `sincosf`
check both optional output ranges before either value is written. Array
allocation uses the same checked heap as scalar C++ allocation.

The first formatted-output bridge handles `snprintf`, `vsnprintf`, `sprintf`,
and `vsprintf`. It keeps integer, XMM, and spilled stack arguments separate and
reads the standard System V AMD64 `va_list` layout. Integer, pointer, byte
string, byte character, floating-point, and count conversions have a one-MiB
output limit. Bounded calls keep the normal truncation and return-length rules.
All destination and count ranges are checked before output changes guest
memory. Wide-character conversions fail clearly until their ABI is supported.

The HLE export registry keeps C++ context handlers separate from executable
import targets. Dispatch uses the same ordered library scope as linking. An
ambiguous unscoped name does not run. The registry copies the selected handler
while locked, releases the lock, and only then calls it. Memory faults inside a
handler stay distinct from lookup failures.

The executable trace also compares required import relocations with the
built-in HLE registry. It reports both relocation references and unique
imports, ignores permitted weak imports, and never calls a handler or writes a
synthetic address. Its totals include known guest-owned data symbols. Repeated
imports are grouped like SharpEmu's useful import inventory, while lookup
preserves KytyPS5's library scope. Missing imports are ranked by relocation
count. Names and scope use a bounded hex format, so guest text cannot add trace
lines.

The same inventory now creates the runtime call table before relocation. This
follows KytyPS5's per-program call-table lifecycle and SharpEmu's executable
import-stub setup. Table construction is transactional: malformed metadata or
a failed executable allocation leaves no callable entry. Duplicate relocation
references share one trampoline.

The native guest-entry bridge now uses KytyPS5's two-argument entry ABI, root
frame, and separate guest stack. It checks the executable entry, full stack,
and parameter block before changing RSP. A linked HLE trampoline follows
SharpEmu's host-return design: it saves the guest call frame, runs the C++
handler on the original host stack, then restores the guest frame and return
register. An inactive execution context cannot switch stacks. Controlled tests
cover direct return and linked HLE return. On Windows, a vectored exception
boundary accepts faults only from guest memory, restores the saved host state,
and reports the exception code, instruction address, and accessed address.
When a handler blocks the current scheduler thread, the trampoline saves the
guest return address, stack, nonvolatile registers, and floating-point state.
It then returns to the host. The continuation can leave the shared execution
lane while another selected scheduler thread runs on a separate guest stack.
After the first thread wakes and is selected again, the executor retries the
handler and resumes after the import with its final return value. A
continuation is bound to its original guest-memory owner. Controlled tests
cover this full block, park, interleave, wake, retry, and resume sequence.
Cooperative pthread yield uses the same saved continuation. It runs the next
ready scheduler thread and resumes after the completed yield import without
calling that handler a second time.
Native pthread entries use their assigned guest stack and receive the thread
argument in the System V `RDI` register.
The native thread runner selects work from the existing scheduler and keeps one
continuation per registered guest thread. For threads without a supplied stack,
it maps the requested pthread stack size, protects its guard page, zeroes the
writable range, and releases the mapping after exit. Process-entry integration
and portable fault containment are still required before the command-line tool
can run a title.

Known runtime data never points into host memory. Startup maps one checked
16 KiB guest page for the stack guard, process name, and two libc need flags.
The four exact library and NID pairs come from KytyPS5. The bounded process
name, duplicate terminator canary, and useful data-symbol set adapt SharpEmu.
Registration is atomic. A conflict removes the new page and keeps the earlier
registry state. The normal relocation pass can then bind these data imports to
real guest addresses.

The libc registry also recognizes `__cxa_pure_virtual`. Calling it returns a
fatal guest status and never reports success or a return value. A later guest
executor can use that status to end the process cleanly.

The libc lifecycle bridge checks `_init_env`, keeps bounded `atexit` and
`__cxa_atexit` registrations in the single runtime, and preserves their
last-in, first-out order. `exit` and `catchReturnFromMain` record the first
guest exit status and return an explicit guest-exit result to the future
executor. `abort` returns a fatal guest result. Exit callbacks are not called
until the executor has a safe guest callback path.

The first JSON bridge handles complete and base `Value` construction and
destruction for `libSceJson2` and `libSceJson`. The single runtime owns a
bounded map keyed by checked guest object addresses. Constructors create a
null shadow, repeated construction resets it, and destructors remove it.
KajPS5 does not write a guessed JSON object layout into guest stack memory.

The first `libc` batch implements `__cxa_guard_acquire`,
`__cxa_guard_release`, and `__cxa_guard_abort`. It preserves the upper six
bytes of each guest guard word. One guest thread owns initialization, a
recursive acquire returns zero, and another guest thread blocks through the
shared scheduler. Release publishes the complete bit and wakes waiters. Abort
clears the low guard state and also wakes waiters. A blocked handler returns a
distinct HLE status; the general executor still needs a continuation path to
resume that call.

The libc C++ mutex family uses the same scheduler-backed pthread service. It
includes plain, recursive, named, try-lock, timed-lock, ownership, unlock, and
destroy entry points under both their public names and PS5 NIDs. Guest mutex
storage holds a checked runtime handle. Failed initialization, invalid memory,
and busy destruction cannot leave a partial guest object. The timed entry
currently follows KytyPS5 and performs a normal scheduler-backed lock; the
general executor still needs the same continuation path for a blocked call.

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
