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
- [x] Apply a checked runtime load bias to complete ELF segment mappings.
- [x] Produce a stable loader trace.
- [x] Validate and record raw ELF64 dynamic-table entries.
- [x] Resolve checked standard ELF dynamic strings.
- [x] Validate standard x86-64 `RELA` metadata and targets.
- [x] Apply checked relative relocations without resolving imports.
- [x] Apply checked PC-relative, narrow, size, and `RELATIVE64` relocations.
- [x] Validate standard dynamic symbol names through `DT_HASH`.
- [x] Parse checked PS5 dynlib-data tables and module and library identities.
- [x] Parse checked SELF/FSELF containers and resolve their ELF payloads.
- [x] Resolve PS5 dynamic metadata from dynlib-data or load segments.
- [x] Run a recognized SELF through checked loading and relocation planning.
- [x] Validate entry-point, process-parameter, and TLS template metadata.
- [x] Validate module startup and shutdown functions and function arrays.
- [x] Build checked lifecycle call lists from relocated guest memory.

## Stage 2: CPU and kernel

- [x] Add typed kernel handles and deterministic event-flag polling.
- [x] Add one deterministic scheduler for ready, running, blocked, and exited
      guest thread states.
- [x] Add monotonic scheduler deadlines and deterministic timeout wakeups.
- [x] Connect event-flag waits to the shared scheduler with wake and recheck
      tests.
- [x] Add checked semaphore counts and scheduler wait integration.
- [x] Add typed user-event queues and scheduler wake integration.
- [x] Add graphics event registrations, filter-based delivery, and generation
      checks for reserved events.
- [x] Expose scheduler-backed event-queue waits, checked event records, timed
      waits, and event-field accessors through `libkernel`.
- [x] Add portable realtime, monotonic, and process-clock conversions.
- [x] Add checked fixed and first-fit flexible-memory mappings.
- [x] Add checked direct-memory range allocation and partial release.
- [x] Add checked fixed, hinted, v2, and named direct-memory mappings.
- [x] Share sparse physical contents between direct-memory aliases.
- [x] Add checked guest paths and memory-backed read-only files.
- [x] Add deterministic directory reads for the memory-backed namespace.
- [x] Add deterministic thread-exit joins.
- [x] Add checked pthread attributes, bounded TLS keys, and scheduler-aware
      thread identity and yielding.
- [x] Preserve pthread entry points and arguments across checked create, join,
      and exit calls.
- [x] Add checked pthread mutex attributes, static initialization, recursive
      ownership, FIFO handoff, and exit cleanup.
- [x] Add scheduler-backed pthread condition waits, one-waiter signals, and
      broadcasts with required mutex reacquisition.
- [x] Add deterministic relative and absolute pthread condition timeouts.
- [x] Add deterministic library-scoped HLE import lookup.
- [x] Connect checked import lookup to standard relocation targets.
- [x] Resolve scoped PS5 NID symbols through parsed module and library IDs.
- [x] Add bounded stable diagnostics for unresolved imports.
- [x] Add deterministic dependency order for supplied module initializers.
- [x] Resolve versioned exports between supplied PS5 modules.
- [x] Apply checked TLS module-ID relocations after TLS registration.
- [x] Build a checked, bounded Variant II static TLS module layout.
- [x] Apply checked module-relative and thread-pointer-relative TLS offsets.
- [x] Add a platform-neutral checked HLE call context.
- [x] Add deterministic library-scoped HLE handler dispatch.
- [x] Relocate known runtime data imports to checked guest memory.
- [x] Add bounded JSON value construction and destruction shadows.
- [x] Add checked libc startup, exit callback, and termination state.
- [x] Add checked guest-memory allocation for common libc heap calls.
- [x] Add bounded caller-owned libc mspaces with aligned allocation.
- [x] Add checked libc memory, string, scalar math, and C++ allocation calls.
- [x] Add bounded libc formatted output with register and `va_list` arguments.
- [x] Add scheduler-aware C++ static initialization guards.
- [x] Connect the complete libc C++ mutex ABI to the shared pthread service.
- [x] Register consistent libkernel process-time handlers.
- [x] Add a transactional libkernel clock-gettime handler.
- [x] Add a transactional libkernel gettimeofday handler.
- [x] Run a public no-import guest leaf test program.
- [x] Run a public guest program through checked HLE imports.
- [x] Capture System V register and bounded stack arguments in a native HLE
      trampoline.
- [x] Capture XMM arguments and return XMM0-XMM1 through native HLE.
- [x] Share one host-mapped backing between native guest code and checked HLE.
- [x] Run a checked guest entry on its guest stack and return HLE work to the
      saved host stack.
- [x] Contain Windows guest access and instruction faults and restore the host
      execution state.
- [x] Save and resume a blocked HLE continuation after the shared scheduler
      wakes its guest thread.
- [x] Park a blocked continuation so the shared native lane can run another
      selected guest thread.
- [x] Return a native pthread yield to the scheduler and resume it without a
      second HLE dispatch.
- [x] Run selected native guest threads through one shared execution lane with
      per-thread continuations and registered guest stacks.
- [x] Allocate checked pthread stacks with a guard page and release owned
      mappings after each thread exits.
- [x] Prepare ready pthread stacks automatically before native execution.
- [x] Run process and pthread entry ABIs through the same scheduler lane.
- [x] Allocate the main guarded stack and checked inline arguments as one owned
      process mapping.
- [x] Create and roll back the main scheduler thread from checked launch
      metadata.
- [x] Pass up to six System V integer arguments through the shared native guest
      entry bridge.
- [x] Run general guest functions through the shared scheduler and continuation
      lane.
- [x] Run checked preinitializers and initializers in order before main-thread
      creation, with explicit blocked and failure states.
- [x] Compose initializers, main, exit callbacks, and finalizers in one checked
      title session and scheduler lane.
- [x] Load, relocate, and execute a small public ELF through `--run-elf`.
- [x] Build and own native HLE trampolines for all resolved executable imports.
- [x] Mount a read-only host title folder under a confined guest path and list
      adjacent module files in stable order.
- [x] Discover and parse bounded adjacent `.prx` and `.sprx` files as one
      all-or-nothing module batch.
- [x] Load the main image and adjacent modules into one guest address space,
      register their exports and TLS layouts, relocate them together, and
      combine their lifecycle order.
- [x] Add checked AMPR command-buffer state, record layouts, measurements, and
      write-address completion without a second memory owner.
- Expand thread, event, file, and time coverage as their HLE bridges grow.
- Compare small, repeatable traces with both upstream projects.

## Stage 3: Graphics and audio

- [x] Add one GPU runtime owner with checked AGC command-buffer allocation.
- [x] Encode tested AGC NOP and direct-dispatch PM4 packets.
- [x] Decode packet sizes and apply packet predication through checked guest
      memory.
- [x] Encode checked direct-register, index, draw, indirect-dispatch,
      control-flow, write-data, statistics, and wait packet families.
- [x] Decode bounded synthetic PM4 streams through checked guest memory.
- [x] Preserve blocked DCB and ACB submissions in owned graphics and compute
      queues without replaying completed actions.
- [x] Apply ordered memory-target `WRITE_DATA` effects through checked guest
      memory so they can release GPU waits.
- [x] Decode standard and AGC-wrapper `RELEASE_MEM` packets, including ordered
      32-bit, 64-bit, and monotonic counter writes.
- [x] Connect `EVENT_WRITE` packets to graphics event registrations without
      confusing hardware event types with guest event IDs.
- [x] Expose AGC event type and context queries over the shared kernel event
      record.
- [x] Submit checked DCB and ACB batches atomically through the existing
      graphics and owner-scoped compute queues.
- [x] Adapt KytyPS5's complete Gen5 instruction decoder and SPIR-V section
      builder behind the KajPS5 shader namespace.
- [x] Create a Vulkan device and a validation test.
- [x] Execute validated recompiler SPIR-V through the optional, singular
      runtime-owned Vulkan compute executor with finite fence waits and safe
      retained-work polling.
- [x] Bind recompiler-produced buffer descriptor arrays, push constants, and
      host-visible Vulkan storage allocations; retain them through fence
      completion and publish GPU changes without overwriting newer CPU bytes.
- [x] Define a checked, backend-neutral guest-image layout and storage-alias
      model for the singular Vulkan image cache, with linear footprints only
      until tiled detiling is proven.
- Add shader control-flow, IR, emission, and resource tests before game tests.
- Add an audio queue test with stable timing.

## Stage 4: Title research

- Load legally obtained title data.
- Record the first unsupported operation.
- Fix one reusable operation at a time.
- Do not add title-specific success claims without repeatable evidence.
