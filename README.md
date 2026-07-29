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
zero-filled tail. It also validates and records raw non-null `PT_DYNAMIC`
entries. The kernel foundation has typed handles and deterministic event-flag
polling. One cooperative scheduler owns guest thread state and
deterministic ready, block, wake, yield, and exit transitions. Guest CPU
execution and continuation-based blocked-call resumption are not implemented.
Event and semaphore waits use an explicit block, wake, and recheck contract.
The kernel clock uses portable host clocks and keeps its process counter and
frequency consistent.
The file foundation normalizes guest paths and reads registered memory-backed
files. It does not expose the host file system.

The tests build a small ELF image in memory. The repository does not contain a
game, firmware, system module, or encrypted executable. Guest execution and
SELF decryption are not implemented.

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
