# Roadmap

## Stage 0: Foundation

- Keep a buildable C++20 program.
- Build and test on Windows and Linux.
- Pin the upstream commits used for research.
- Define source and license records before code import.

## Stage 1: Loader and guest memory

- [x] Parse ELF metadata without executing guest code.
- [x] Validate all guest address ranges and access permissions.
- [x] Load one small public test program.
- [x] Produce a stable loader trace.
- [x] Validate and record raw ELF64 dynamic-table entries.
- [x] Resolve checked standard ELF dynamic strings.

## Stage 2: CPU and kernel

- [x] Add typed kernel handles and deterministic event-flag polling.
- [x] Add one deterministic scheduler for ready, running, blocked, and exited
      guest thread states.
- [x] Connect event-flag waits to the shared scheduler with wake and recheck
      tests.
- [x] Add checked semaphore counts and scheduler wait integration.
- [x] Add portable realtime, monotonic, and process-clock conversions.
- [x] Add checked guest paths and memory-backed read-only files.
- [x] Add deterministic thread-exit joins.
- Run a public guest test program.
- Add thread, event, file, and time tests.
- Compare focused traces with the two upstream projects.

## Stage 3: Graphics and audio

- Create a Vulkan device and a validation test.
- Decode a small public command-stream fixture.
- Add shader and resource tests before game tests.
- Add an audio queue test with stable timing.

## Stage 4: Title research

- Load legally obtained title data.
- Record the first unsupported operation.
- Fix one generic operation at a time.
- Do not add title-specific success claims without repeatable evidence.
