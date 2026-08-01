# KajPS5

KajPS5 is an early PlayStation 5 emulator research project. It builds, runs a
growing test suite, and can inspect ELF64 and recognized SELF/FSELF files. It
does not run PlayStation 5 games yet.

The goal is to combine the strengths of KytyPS5 and SharpEmu where that makes
sense. KytyPS5 is the main reference for the native C++ architecture and
runtime. SharpEmu provides useful loader and service behavior, diagnostics,
and tests.

KajPS5 is not a 50/50 source merge. It keeps one C++20 runtime, scheduler,
guest-memory model, and graphics owner. KytyPS5 components can be adapted when
they fit that design. When a complete KytyPS5 subsystem fits, KajPS5 adapts
that path instead of rebuilding it one small part at a time. SharpEmu behavior
first becomes a focused trace or test, then an implementation in the same C++
core.

## Current status

The current core includes:

- A guest address space with whole-range access checks and checked map,
  protection, and unmap operations. Failed range validation leaves mappings
  unchanged. Released bytes are cleared before reuse. A host-mapped backing
  keeps controlled native guest execution and HLE access on the same bytes.
- An executable loader that checks bare ELF64 and recognized SELF/FSELF
  containers, program headers, standard and PS5 dynamic-link metadata, x86-64
  `RELA` relocations, and symbols before it changes guest memory. Relocation
  support includes 32-bit, 64-bit, PC-relative, symbol-size, relative, import,
  and TLS module-ID writes with checked bounds and overflow. It preserves each
  `PT_LOAD` segment's read, write, and execute flags. The inspection path also
  reports unresolved imports and validates the entry point, process
  parameters, TLS template, and module startup and shutdown metadata without
  running code. A checked load bias can place every segment at a selected
  runtime base before relocation. The same trace measures how many required
  import relocations and unique imports match the built-in HLE handlers. After
  relocation, it builds bounded startup and shutdown call lists from checked
  guest memory.
- Versioned PS5 NID linking between supplied modules and library-scoped HLE
  dispatch, with bounded diagnostics for unresolved symbols, deterministic
  dependency order, and checked guest register and memory access.
- Typed kernel handles and one cooperative scheduler for ready, running,
  blocked, and exited guest threads. Initial pthread support includes guest
  attributes, bounded thread-local keys, per-thread values, identity, equality,
  scheduler-aware yielding, checked create, join, and exit calls, and
  scheduler-backed mutexes and timed condition variables. Checked C++ static
  initialization guards and the complete C++ mutex ABI use the same guest
  scheduler.
- Event flags, semaphores, user-event queues, portable clocks, guest memory
  protection, flexible and direct-memory mappings, direct-memory range
  allocation, and a read-only in-memory file namespace. The matching
  `libkernel` handlers share the same services.
- Checked JSON value construction and destruction through a bounded shadow
  store, without writing an assumed guest object layout.
- Checked process arguments, bounded exit callbacks, and explicit guest exit
  requests for the first libc lifecycle imports.
- Checked libc memory, byte-string, and 16-bit wide-string calls, scalar math
  through XMM registers, bounded formatted output, C++ allocation, a
  guest-memory heap, and caller-owned mspaces.
- A native HLE table that resolves each known executable import and owns its
  trampoline for the full link and execution lifetime. The trampolines carry
  register and bounded stack arguments, including XMM values, into the checked
  C++ call context. A biased public ELF can run directly from host-mapped guest
  memory and call the linked table without a copied code buffer. Controlled
  entry tests use the real guest stack and entry arguments. HLE dispatch moves
  back to the saved host stack before a C++ handler runs.
- Small public and generated test fixtures, including an ELF that loads
  without running guest code and controlled x86-64 leaf programs used only by
  tests.

KajPS5 still lacks general guest CPU execution, SELF decryption, resumable
blocked HLE calls, graphics, audio, and title compatibility. The repository
contains no games, firmware, keys, proprietary modules, or encrypted
executables.

## Build

Install CMake 3.24 or a newer version and a C++20 compiler.

```powershell
cmake -S . -B _Build
cmake --build _Build --config Release
ctest --test-dir _Build -C Release --output-on-failure
```

On Windows, the executable is usually in `_Build/src/Release/kajps5.exe`.
When MSVC AddressSanitizer is enabled, CMake copies its required runtime DLL
beside each executable so CTest and direct launches use the same build tree.

Inspect and load-check a public decrypted ELF or FSELF without running guest
code:

```powershell
_Build\src\Release\kajps5.exe --trace-elf R:\path\sample.bin
```

The command reads at most 512 MiB and rejects a guest range larger than
512 MiB. Its stable summary is suitable for trace comparisons. Import coverage
is read-only: it does not call HLE handlers or write fake target addresses. A
separate guest-owned data page supplies four known runtime values during the
load check. KajPS5 does not decrypt retail SELF payloads. It accepts only
payload bytes that are already available in the input file.

## Legal use

KajPS5 is for research and education. The project does not include games,
firmware, keys, or proprietary system software. Use only data that you have a
legal right to use.

KajPS5 is not affiliated with Sony Interactive Entertainment or PlayStation.
Product names can be trademarks of their owners.

## License

Original KajPS5 code is available under GPL-2.0-or-later. Imported code must
keep its original copyright and license notices. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[docs/upstreams.md](docs/upstreams.md).
