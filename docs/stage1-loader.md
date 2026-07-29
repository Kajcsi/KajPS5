# Stage 1 loader evidence

KajPS5 now has one checked guest-memory buffer and one ELF64 metadata loader.
The code is an original KajPS5 implementation of the public ELF64 format. It
does not copy source code from either upstream project.

The design review used these pinned references:

- KytyPS5 commit `f6e01e54031a3c615f089f061a4eab2f3c59acba`,
  `src/loader/elf.h`, `src/loader/elf.cpp`, and dynamic metadata handling in
  `src/loader/runtimeLinker.cpp`: native C++ ELF, program-header, and dynamic
  table boundaries for x86-64 PlayStation images.
- SharpEmu commit `d5108e854d609808f17093a6f5dbbc711d09ad2e`,
  `src/SharpEmu.Core/Loader/SelfLoader.cs` and
  `tests/SharpEmu.Libs.Tests/Loader/SelfLoaderTests.cs`: early header checks,
  checked file ranges, bare ELF test behavior, and checked dynamic-table
  slicing.
- The same SharpEmu commit,
  `src/SharpEmu.Core/Memory/VirtualMemory.cs` and
  `tests/SharpEmu.Libs.Tests/Memory/VirtualMemoryTests.cs`: whole-range access
  checks and no partial writes after a rejected access.

The guest-memory model keeps sorted, non-overlapping mapped regions. Every
read, write, and execute check must pass across the complete range before an
access starts. The ELF loader rejects overlapping load segments and mapping
conflicts before it creates any new region. It then preserves each segment's
`R/W/X` flags during initialization.

The public test fixture is generated from constants in
`tests/elf_loader_test.cpp`. It has no external or proprietary bytes. The tests
check metadata, file copies, zero fill, truncated input, integer overflow,
alignment, permissions, gaps, overlap, guest-memory rejection, and terminated
16-byte dynamic entries. A rejected load does not change guest memory.

This milestone does not map host pages, relocate a dynamic image, resolve its
symbols, parse SELF containers, or run guest code.

See `public-elf-validation.md` for the external PS5 homebrew ELF check.
