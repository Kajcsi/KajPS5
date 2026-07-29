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
a deterministic name boundary for later relocation and HLE dispatch work. It
does not generate executable stubs or call host services yet.
