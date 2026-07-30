# KajPS5

KajPS5 is an early PlayStation 5 emulator research project. It builds, runs a
growing test suite, and can inspect public ELF files. It does not run
PlayStation 5 games yet.

The goal is to combine the strengths of KytyPS5 and SharpEmu where that makes
sense. KytyPS5 is the main reference for the native C++ architecture and
runtime. SharpEmu provides useful loader and service behavior, diagnostics,
and tests.

KajPS5 is not a 50/50 source merge. It keeps one C++20 runtime, scheduler,
guest-memory model, and graphics owner. KytyPS5 components can be adapted when
they fit that design. SharpEmu behavior first becomes a small trace or test,
then a small implementation in the same C++ core.

## Current status

The current core includes:

- A guest address space with whole-range access checks and transactional map,
  protection, and unmap operations. Released bytes are cleared before the
  range can be reused.
- An ELF64 loader that checks program headers, standard and PS5 dynamic-link
  metadata, x86-64 `RELA` relocations, and symbols before it changes guest
  memory. It preserves each `PT_LOAD` segment's read, write, and execute flags.
- Library-scoped import linking and HLE dispatch, with bounded diagnostics for
  unresolved symbols and checked guest register and memory access.
- Typed kernel handles and one cooperative scheduler for ready, running,
  blocked, and exited guest threads.
- Event flags, semaphores, user-event queues, portable clocks, guest memory
  operations, and a read-only in-memory file namespace. The matching
  `libKernel` handlers share the same services.
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

Inspect and load-check a public decrypted ELF without running guest code:

```powershell
_Build\src\Release\kajps5.exe --trace-elf R:\path\sample.elf
```

The command reads at most 512 MiB and rejects a guest range larger than
512 MiB. Its stable summary is suitable for trace comparisons.

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
