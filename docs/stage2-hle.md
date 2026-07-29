# Stage 2 HLE research

KajPS5 has one checked import registry for future HLE trampolines. Each entry
has a library name, a symbol name, and a nonzero target address. Exact duplicate
entries fail. Lookup follows the ELF's ordered needed-library list. An unscoped
lookup succeeds only when one library owns the symbol.

The design review used these pinned references:

- KytyPS5 commit `f6e01e54031a3c615f089f061a4eab2f3c59acba`, symbol and
  library lookup in `src/loader/runtimeLinker.cpp`.
- SharpEmu commit `d5108e854d609808f17093a6f5dbbc711d09ad2e`, import handling
  in `src/SharpEmu.Core/Cpu/Native/StubManager.cs` and
  `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs`, plus the
  checked context and export boundary in `src/SharpEmu.HLE/CpuContext.cs` and
  `src/SharpEmu.HLE/ExportedFunction.cs`.

The C++ registry does not copy either ownership model or executor. It provides
a deterministic name boundary for relocation and HLE dispatch work. The
relocation pass writes resolved `R_X86_64_GLOB_DAT` and
`R_X86_64_JUMP_SLOT` targets only after it validates the complete plan. Missing
symbols remain unchanged and produce structured diagnostics. Stable diagnostic
text limits detail to 32 imports and 128 bytes per hex-encoded symbol name, so
guest data cannot add trace lines or create unbounded detail. It does not
generate general executable stubs. One
test-only redistributable ELF fixture uses the complete parse, load, link, and
native leaf path to call a no-argument HLE handler. The fixture adjusts its
stack for the host ABI. It is not a PS5 ABI bridge.

The platform-neutral HLE call context maps the six System V integer argument
registers and the return register. Integer and string access goes through the
checked guest-memory model. String reads are limited to 4 KiB. If a bulk read
crosses an unmapped boundary, the context checks one byte at a time so it can
still accept a terminator before the boundary. A missing terminator and a
memory fault are different results. Native trampoline state capture is not
connected yet.

The HLE export registry stores C++ context handlers separately from executable
import targets. Dispatch follows the same ordered needed-library scope as
linking. An unscoped duplicate name is ambiguous and does not run. The registry
copies the selected handler under its lock and runs it after it releases the
lock. Handler memory faults remain distinct from lookup failures.

The first generic `libKernel` handler batch exposes
`sceKernelGetProcessTime`, `sceKernelGetProcessTimeCounter`, and
`sceKernelGetProcessTimeCounterFrequency`. All three use the shared kernel
clock service, so the microsecond value, nanosecond counter, and one-gigahertz
frequency stay consistent. Batch registration validates all definitions before
it changes the export table.

`sceKernelClockGettime` uses the same clock service and writes its 16-byte
timespec in one checked operation. A bad guest range leaves memory unchanged.
Guest-visible `EFAULT` and `EINVAL` values follow the pinned KytyPS5 kernel
contract. SharpEmu's corresponding Gen5 values are documented as synthetic,
so KajPS5 does not use them as the kernel ABI.
