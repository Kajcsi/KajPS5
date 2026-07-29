# Architecture

KajPS5 uses a C++20 core. The core owns guest memory, CPU execution, kernel
objects, graphics queues, audio, input, and the user interface.

## Upstream roles

KytyPS5 is the primary architecture reference. It is a native C++ project with
a CMake build, a Vulkan renderer, and PS5 guest runtime work. KajPS5 can port a
KytyPS5 component when its interface fits the KajPS5 core.

SharpEmu is a behavior and test reference. It has useful C# work for loading,
services, metadata, and diagnostics. KajPS5 does not embed a second emulator
runtime. A SharpEmu behavior must first become a small trace or a focused test.
The C++ core then implements the behavior.

## Integration rule

Each imported behavior needs these items:

1. A source commit and source path.
2. A focused test or trace.
3. A small KajPS5 interface.
4. The required copyright and license notice.
5. A result that does not reduce an existing test result.

This rule keeps one scheduler, one memory model, and one graphics ownership
model.

## First component boundaries

- `loader`: Load ELF and SELF metadata into a checked guest address space.
- `kernel`: Own handles, threads, events, files, and system call dispatch.
- `cpu`: Execute guest x86-64 code and handle guest exceptions.
- `gpu`: Decode guest commands and submit validated work to Vulkan.
- `services`: Implement only the service calls that a test needs.
- `trace`: Record stable events for comparison with upstream behavior.
- `app`: Select a title, configure a run, and show diagnostic results.

The foundation commit does not implement these components. It reserves their
boundaries so that later work has a stable direction.
