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
- `runtime`: Compose loading, initialization, scheduled execution, and orderly
  shutdown without creating another scheduler or executor.
- `gpu`: Decode guest commands and submit validated work to Vulkan.
- `services`: Implement service calls as evidence and tests require them.
- `trace`: Record stable events for comparison with upstream behavior.
- `app`: Select a title, configure a run, and show diagnostic results.

The loader, memory, native CPU path, title session, HLE, and first kernel
services now follow these boundaries. The checked memory owner can use a
buffered backing for inspection or a host-mapped backing for controlled direct
execution. The GPU owner now decodes and schedules checked command buffers.
The shader compiler has Gen5 decoding, structured control flow, IR lowering,
and a tested public `ShaderRecompiler` unit that emits Kyty-derived SPIR-V.
The same GPU runtime now owns checked registered AGC shader images and uses
that registry to feed exact guest dwords to the recompiler. Its bounded
Kyty-derived Vulkan action, resource, and presentation path runs supported
work through that one owner, including checked linear and RenderTarget64KB
tiled image presentation. SharpEmu-derived fail-closed, recovery, and
coherence regressions constrain no-context VideoOut flips and retained work.
For Windows title runs, the app owns one visible Vulkan window, pumps messages,
forwards client-size changes, polls presentation, and runs bounded guest
chunks. `--headless` deliberately skips host graphics; it is not a successful
VideoOut fallback. Audio, input, the broader user interface, and complete GPU
and guest compatibility remain incomplete.
