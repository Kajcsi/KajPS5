# Roadmap

## Stage 0: Foundation

- Keep the C++20 project buildable at every step.
- Build and test on Windows and Linux.
- Pin the upstream commits used for research.
- Record source and license details before importing code.

## Stage 1: Loader and guest memory

- [x] Parse ELF metadata without executing guest code.
- [x] Validate all guest address ranges and access permissions.
- [x] Add transactional guest protection and unmap changes.
- [x] Load one small public test program.
- [x] Produce a stable loader trace.
- [x] Validate and record raw ELF64 dynamic-table entries.
- [x] Resolve checked standard ELF dynamic strings.
- [x] Validate standard x86-64 `RELA` metadata and targets.
- [x] Apply checked relative relocations without resolving imports.
- [x] Validate standard dynamic symbol names through `DT_HASH`.
- [x] Parse checked PS5 dynlib-data tables and module and library identities.
- [x] Parse checked SELF/FSELF containers and resolve their ELF payloads.
- [x] Resolve PS5 dynamic metadata from dynlib-data or load segments.
- [x] Run a recognized SELF through checked loading and relocation planning.
- [x] Validate entry-point, process-parameter, and TLS template metadata.
- [x] Validate module startup and shutdown functions and function arrays.

## Stage 2: CPU and kernel

- [x] Add typed kernel handles and deterministic event-flag polling.
- [x] Add one deterministic scheduler for ready, running, blocked, and exited
      guest thread states.
- [x] Connect event-flag waits to the shared scheduler with wake and recheck
      tests.
- [x] Add checked semaphore counts and scheduler wait integration.
- [x] Add typed user-event queues and scheduler wake integration.
- [x] Add portable realtime, monotonic, and process-clock conversions.
- [x] Add checked guest paths and memory-backed read-only files.
- [x] Add deterministic directory reads for the memory-backed namespace.
- [x] Add deterministic thread-exit joins.
- [x] Add deterministic library-scoped HLE import lookup.
- [x] Connect checked import lookup to standard relocation targets.
- [x] Resolve scoped PS5 NID symbols through parsed module and library IDs.
- [x] Add bounded stable diagnostics for unresolved imports.
- [x] Add deterministic dependency order for supplied module initializers.
- [x] Apply checked TLS module-ID relocations after TLS registration.
- [x] Add a platform-neutral checked HLE call context.
- [x] Add deterministic library-scoped HLE handler dispatch.
- [x] Register consistent libkernel process-time handlers.
- [x] Add a transactional libkernel clock-gettime handler.
- [x] Add a transactional libkernel gettimeofday handler.
- [x] Run a public no-import guest leaf test program.
- [x] Run a public guest program through checked HLE imports.
- Expand thread, event, file, and time coverage as their HLE bridges grow.
- Compare small, repeatable traces with both upstream projects.

## Stage 3: Graphics and audio

- Create a Vulkan device and a validation test.
- Decode a small public command-stream fixture.
- Add shader and resource tests before game tests.
- Add an audio queue test with stable timing.

## Stage 4: Title research

- Load legally obtained title data.
- Record the first unsupported operation.
- Fix one reusable operation at a time.
- Do not add title-specific success claims without repeatable evidence.
