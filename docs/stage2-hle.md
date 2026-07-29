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
  `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs`.

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
