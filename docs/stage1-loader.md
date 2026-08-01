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

The loader can add one checked runtime bias to every load segment. It rejects
an overflowing or conflicting biased range before creating any mapping and
keeps parsed ELF addresses unchanged. The same bias then feeds relocation,
launch metadata, exports, and lifecycle planning. This follows KytyPS5's
runtime image-base flow and prepares one coherent address space for direct
native execution.

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
`R_X86_64_64`, `PC32`, `PLT32`, `GLOB_DAT`, `JUMP_SLOT`, `RELATIVE`, `32`,
`32S`, `DTPMOD64`, `PC64`, `SIZE32`, `SIZE64`, and `RELATIVE64`. PC-relative
writes use `S + A - P`; size writes use `Z + A`; and relative writes use
`B + A`. Four-byte targets are checked as four-byte ranges. Signed and
unsigned overflow stops the full pass before any write. Absolute relocations
use defined symbols, resolved imports, or zero for an unresolved weak symbol.
`R_X86_64_DTPMOD64` writes a checked nonzero TLS module ID after TLS
registration. `R_X86_64_DTPOFF64` writes a checked module-relative symbol
offset. `R_X86_64_TPOFF64` writes the Variant II offset from the thread pointer
after static TLS layout. Missing TLS identity or layout, invalid TLS or relative
symbols, target overflow, and unmapped writes also fail. The HLE layer resolves
symbol relocations by ordered needed-library name. Relocation traces encode untrusted
names as hex, report a rejected numeric relocation type, and show at most 32
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

Some PS5 executables keep SCE size tags but use standard table locations, and
some keep the tables in a load segment instead of `PT_SCE_DYNLIBDATA`. The
loader resolves each table field independently. It first checks an absolute
guest address and then a load-base-relative address. If an SCE symbol-table
size is absent, relocation symbol indexes provide a checked minimum size. This
adapts SharpEmu's useful mixed-tag and size fallback behavior.

Packed KytyPS5 module and library records become typed metadata. Each record
keeps its numeric ID, version, and checked name from the SCE string table. Both
known tag generations are accepted. The stable trace reports the string-table
source and import and export counts without printing untrusted names.

Import relocations recognize the PS5 `NID#library#module` symbol form. The
linker decodes the compact IDs, requires both IDs to exist in the parsed
metadata, extracts the NID as SharpEmu does, and limits lookup to the selected
library. A malformed or unknown scope remains unresolved and cannot fall back
to an unrelated HLE library.

The layered import resolver keeps an exact export from an already loaded
module. When that lookup has no result, it checks the same library in the HLE
address table. An unresolved import stays unchanged and is reported by the
existing bounded relocation trace.

SELF/FSELF parsing follows KytyPS5's native header, segment table, and
program-header containment model. SharpEmu's structural header checks,
checked fallback offsets, and already-dumped encrypted or compressed payload
handling add useful input coverage. Runtime decryption and decompression are
not implemented. A payload marked as encrypted or compressed is accepted only
when the required bytes are already present and in range.

Synthetic tests cover SELF header variants, embedded ELF offsets, exact and
containing segment mappings, dumped payload flags, unavailable encrypted or
compressed payloads, SCE precedence, load-backed and mixed-tag symbols,
size-based symbols, relocations, module and library identities, repeated
dynlib-data segments, truncated ranges, and invalid table and name offsets.
No fixture contains proprietary data. The valid fixtures also run through
checked guest loading, scoped NID resolution, and transactional relocation
writes. One generated PS5 SELF covers the full container, dynamic-metadata,
load, relative-relocation, import-link, and launch-metadata sequence.

Launch metadata follows the image-base flow used by both references. A nonzero
entry point must be inside an executable load segment. Only one SCE process
parameter segment and one TLS segment are accepted. Process parameters must be
inside loaded memory. The initialized TLS bytes must be loaded, while the
larger zero-filled TLS area remains per-thread storage and does not need its
own file-backed load range. The loader also records `DT_INIT`, `DT_FINI`, and
the pre-initializer, initializer, and finalizer arrays. Function addresses must
be executable. Array storage must be loaded, complete, and made of 64-bit
entries. This prepares the data needed to build the call order without
executing guest code.

After relocation, the lifecycle planner reads each function array through the
guest-memory access checks. It ignores null and all-ones sentinel entries,
removes repeated calls within one lifecycle phase, and requires every retained
target to be executable. Pre-initializers keep array order. Initializers use
`DT_INIT` before `DT_INIT_ARRAY`. Finalizers use `DT_FINI_ARRAY` in reverse
order before `DT_FINI`. The planner has a 65,536-call limit and returns no
partial plan after an error. It still does not execute guest functions.

Adjacent module intake lists `.prx` and `.sprx` files in `/app0/sce_module`
and `/app0/sce_modules` through the checked file service. It skips core runtime
images, bounds each file and the complete batch, and parses every image before
it returns any module. A malformed image leaves no partial batch.

The module planner accepts those parsed module descriptions. It matches file
and shared-object names without case sensitivity, starts available dependencies
first, reports missing dependencies once, and preserves input order for a
remaining cycle. It does not assume that firmware is available. A caller can
combine legally supplied modules with the same HLE registry used for unresolved
system libraries.

The module export registry follows KytyPS5's split between imported and
exported symbols. It indexes nonzero global or weak function, object, and
untyped exports. Each key includes the decoded NID, library name and version,
and module name and version. A consumer relocation must match the full PS5
scope. Unscoped lookup succeeds only for one matching export. Registration is
bounded and transactional, so a duplicate or overflowing address adds no
partial module. This lets legally supplied modules link to each other without
changing the HLE registry or guest-memory model.

The title loader now composes the main executable and the complete adjacent
module batch in one host-mapped guest address space. It gives each image a
stable module ID and a separate aligned load bias. All TLS descriptions and
exports are registered before one layered relocation pass checks module
exports first and the HLE table second. It builds lifecycle plans only after
relocation, starts adjacent modules in dependency order, and stops them in
reverse order. A failed batch does not expose a title session. Static TLS
layout is preserved, but title execution still stops until the native thread
path can install that layout.

The run command can add up to 16 explicit read-only module directories. Each
directory receives its own confined guest mount and joins the same bounded,
parse-before-use module batch. The loader does not copy a module into the title
folder or the repository.

The separate controlled native tests are documented in `stage2-cpu.md` and
`stage2-hle.md`.

See `public-elf-validation.md` for the external PS5 homebrew ELF check.
