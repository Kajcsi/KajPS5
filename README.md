# KajPS5

KajPS5 is an early PlayStation 5 emulator research project. The repository
contains a small buildable foundation. It does not run PlayStation 5 games.

The project goal is to combine the strengths of KytyPS5 and SharpEmu where
possible. KytyPS5 guides the native C++ architecture and runtime. SharpEmu
guides loader and service behavior, diagnostics, and focused tests.

The project uses one clear design:

- Use a native C++ runtime as the primary implementation.
- Study KytyPS5 for proven native runtime and graphics behavior.
- Study SharpEmu for proven loader, service, and test behavior.
- Port one behavior only after a focused test proves that it is useful.

KajPS5 is not a direct 50/50 merge. The two upstream projects use different
languages and different ownership models. A direct merge would make the
runtime difficult to test and maintain. KajPS5 keeps one C++ runtime and uses
trace data and tests as the boundary for C# reference work.

## Current status

The program prints its version and project status. The core also has a checked
guest-memory address space and an ELF64 metadata loader. The loader validates
all segments before it changes memory. It maps each `PT_LOAD` range with its
read, write, and execute flags, copies its file bytes, and clears its
zero-filled tail. It validates raw non-null `PT_DYNAMIC` entries and resolves
checked standard ELF string-table references for `DT_NEEDED` and `DT_SONAME`.
It also validates and records standard x86-64 `RELA` and PLT relocation
metadata. A checked relocation pass applies `R_X86_64_RELATIVE` entries and
reports unresolved import relocations without changing them.
The loader also reads standard `DT_HASH` symbol counts and validates dynamic
symbol names.
One checked HLE registry resolves symbols by ordered library name and rejects
ambiguous unscoped lookups. Checked import linking writes resolved
`GLOB_DAT` and `JUMP_SLOT` targets and reports each unresolved symbol. It does
not generate general callable stubs yet. Bounded relocation traces hex-encode
at most 128 bytes from each untrusted symbol name. A test-only redistributable
ELF fixture calls one no-argument HLE handler through a linked `JUMP_SLOT`.
One platform-neutral HLE call context maps the six integer argument registers,
tracks return writes, and uses checked guest-memory reads and writes.
A deterministic export registry dispatches context handlers by ordered library
name and does not run ambiguous unscoped symbols.
The first registered `libKernel` handlers expose consistent process time,
counter, counter-frequency, and checked `sceKernelClockGettime` behavior from
the shared kernel clock. `sceKernelGettimeofday` uses the same checked output
boundary. Each clock handler is available by export name and NID.
The kernel foundation has typed handles and deterministic event-flag polling.
One cooperative scheduler owns guest thread state and
deterministic ready, block, wake, yield, and exit transitions. A test-only
native x86-64 path runs one controlled no-import leaf entry from checked guest
memory. General guest CPU execution and continuation-based blocked-call
resumption are not implemented.
Thread joins, event waits, and semaphore waits use an explicit block, wake,
and recheck contract.
The kernel clock uses portable host clocks and keeps its process counter and
frequency consistent.
The file foundation normalizes guest paths and reads registered memory-backed
files. Checked open, close, read, positioned-read, seek, stat, and fstat
handlers expose that same service by export name and NID. Metadata uses the
120-byte kernel stat layout and deterministic values for registered regular
files. The service does not expose the host file system.
One atomic default registration binds all current clock and file handlers to
the same kernel runtime.

The tests build small ELF images in memory, including a six-byte leaf program
that returns 42. The repository does not contain a game, firmware, system
module, or encrypted executable. General guest execution and SELF decryption
are not implemented.

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

Inspect and load-check a public decrypted ELF without executing guest code:

```powershell
_Build\src\Release\kajps5.exe --trace-elf R:\path\sample.elf
```

The command reads at most 512 MiB and refuses a guest range larger than
512 MiB. It prints stable ELF and load summary fields for test comparison.

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
