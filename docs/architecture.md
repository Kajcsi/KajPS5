# Architecture

KajPS5 uses one C++20 core. That core owns guest memory, CPU execution, kernel
objects, graphics work, audio, input, and the user interface.

## Upstream roles

KytyPS5 is the main architecture reference. Its native C++ runtime, CMake
build, and Vulkan renderer are the shortest path toward a useful emulator.
KajPS5 can adapt a KytyPS5 component when it fits the core cleanly.

SharpEmu is the behavior and test reference. Its C# code contains useful work
on loading, services, metadata, and diagnostics. KajPS5 does not embed that
runtime. Instead, a useful behavior becomes a small trace or test and is then
implemented in the C++ core.

## Integration rule

Each adapted behavior needs:

1. A source commit and source path.
2. A small test or trace.
3. A small KajPS5 interface.
4. The required copyright and license notice.
5. Evidence that existing behavior still works.

This keeps the runtime understandable: one scheduler, one memory model, and
one owner for graphics state.

## Component boundaries

- `loader`: Load ELF and SELF metadata into a checked guest address space.
- `kernel`: Own handles, threads, events, files, and system call dispatch.
- `cpu`: Execute guest x86-64 code and handle guest exceptions.
- `gpu`: Decode guest commands and submit validated work to Vulkan.
- `services`: Implement service calls as evidence and tests require them.
- `trace`: Record stable events for comparison with upstream behavior.
- `app`: Select a title, configure a run, and show diagnostic results.

The loader, memory, CPU test path, HLE, and first kernel services now follow
these boundaries. The checked memory owner can use a buffered backing for
inspection or a host-mapped backing for controlled direct execution. Graphics,
audio, input, and the user interface are still future work.
