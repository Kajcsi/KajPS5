# Stage 1 loader evidence

KajPS5 has one guest-memory model and one ELF64 loader. The implementation is
original KajPS5 code based on the public ELF64 format; it does not copy source
from either upstream project.

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
- The pinned KytyPS5 `src/common/virtualMemory.h` and
  `src/common/virtualMemory.cpp`, plus SharpEmu
  `src/SharpEmu.HLE/IGuestAddressSpace.cs` and
  `src/SharpEmu.Core/Memory/PhysicalVirtualMemory.cs`: protection changes and
  released-range behavior.

Guest-memory regions stay sorted and never overlap. A complete range must pass
its read, write, or execute check before access begins. The ELF loader rejects
overlapping segments and mapping conflicts before it creates a region, then
keeps each segment's `R/W/X` flags while copying its data.

The public test fixture is built from constants in
`tests/elf_loader_test.cpp`; it contains no external or proprietary bytes. The
tests cover metadata, file copies, zero fill, truncated input, integer
overflow, alignment, permissions, gaps, overlap, guest-memory rejection, and
terminated 16-byte dynamic entries. Other cases cover file-backed string
tables, needed libraries, shared-object names, string offsets, and missing
terminators.

Repeated decoded strings share one parse budget across needed libraries,
SONAME, and symbols. The budget is four times the input size, with a 64 KiB
minimum and a 64 MiB maximum. This keeps repeated references from expanding
into unbounded copies while preserving ordinary duplicate names.

Relocation tests cover standard 24-byte `RELA` entries, PLT format,
file-backed tables, mapped targets, and malformed metadata. A rejected load
does not change guest memory.

The relocation pass validates its full plan before writing memory. It applies
`R_X86_64_RELATIVE`, skips no-operation entries, and counts unresolved
`R_X86_64_GLOB_DAT` and `R_X86_64_JUMP_SLOT` imports. Unsupported types,
invalid relative symbols, target overflow, and unmapped writes fail. The HLE
layer can resolve `GLOB_DAT` and `JUMP_SLOT` imports by ordered needed-library
name. Relocation traces encode untrusted names as hex and show at most 32
records with 128 input bytes per name.

Protection and unmap changes are transactional. The full requested range must
be mapped before its metadata changes. Protection can split and merge regions
into a canonical layout. Unmapping clears released bytes so a later mapping
cannot expose stale guest data. Region queries keep CPU and GPU permission bits
separate.

The standard System V hash header supplies the dynamic symbol count. The loader
checks the full hash and symbol-table ranges, requires 24-byte x86-64 symbols,
and resolves every name inside the dynamic string table. ELFs that use only a
different hash format still load, but their symbols are not indexed yet.

PS5 dynamic-link tables use the checked `PT_SCE_DYNLIBDATA` file range. SCE
string, symbol, `RELA`, and PLT tags take precedence over standard tags when
both forms are present. The loader accepts the fixed 24-byte symbol and
relocation layout when an optional SCE entry-size or PLT-format tag is absent.
This matches SharpEmu's useful handling of dumped metadata while keeping
KytyPS5's native linker layout.

Packed KytyPS5 module and library records become typed metadata. Each record
keeps its numeric ID, version, and checked name from the SCE string table. Both
known tag generations are accepted. The stable trace reports the string-table
source and import and export counts without printing untrusted names.

Synthetic tests cover SCE precedence, size-based symbols, relocations,
module and library identities, missing and repeated dynlib-data segments,
truncated ranges, and invalid table and name offsets. No fixture contains
proprietary data.

This milestone does not parse SELF containers. The separate controlled native
tests are documented in `stage2-cpu.md` and `stage2-hle.md`.

See `public-elf-validation.md` for the external PS5 homebrew ELF check.
