# Stage 2 kernel research

KajPS5 now has one kernel runtime, one typed handle table, deterministic
event-flag polling, and one cooperative guest scheduler. It does not execute
guest CPU instructions or resume blocked guest calls yet.

The behavior review used these pinned upstream files:

- KytyPS5 `src/kernel/eventFlag.h` and `src/kernel/eventFlag.cpp` at
  `f6e01e54031a3c615f089f061a4eab2f3c59acba`.
- SharpEmu
  `src/SharpEmu.Libs/Kernel/KernelEventFlagCompatExports.cs` at
  `d5108e854d609808f17093a6f5dbbc711d09ad2e`.
- SharpEmu `src/SharpEmu.HLE/GuestThreadExecution.cs` and
  `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.cs` at the same commit.
- KytyPS5 `src/kernel/semaphore.h` and `src/kernel/semaphore.cpp` at the pinned
  KytyPS5 commit.
- SharpEmu `src/SharpEmu.Libs/Kernel/KernelSemaphoreCompatExports.cs` at the
  pinned SharpEmu commit.
- KytyPS5 `src/kernel/pthread.h` and the clock functions in
  `src/kernel/pthread.cpp` at the pinned KytyPS5 commit.
- SharpEmu `src/SharpEmu.Libs/Kernel/KernelRuntimeCompatExports.cs` at the
  pinned SharpEmu commit.
- KytyPS5 `src/kernel/fileSystem.h` and `src/kernel/fileSystem.cpp` at the
  pinned KytyPS5 commit.
- SharpEmu open, close, read, seek, and stat behavior in
  `src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs` at the pinned
  SharpEmu commit.

The focused tests record these shared behaviors:

- Setting a pattern uses bitwise OR.
- Clearing an event flag retains the bits selected by the supplied mask.
- Polling supports all-bit and any-bit conditions.
- A successful poll can clear all bits or only the requested pattern.
- Polling returns the observed pattern before it applies a clear mode.
- Invalid attributes, wait modes, zero patterns, and stale handles fail.
- Ready threads use deterministic FIFO selection.
- Only one guest thread can be running at a time.
- Blocking records a wait key. A bounded wake moves matching threads back to
  the ready queue in handle order.
- Yielding requeues the current thread. Exiting preserves its result for
  snapshots.
- Joining a live thread blocks the caller. Thread exit wakes all joiners, and a
  repeated join returns the preserved exit value.
- Self joins and stale thread handles fail without changing scheduler state.
- An unsatisfied event wait blocks the current thread on that event handle.
- Setting or deleting an event wakes its blocked threads in handle order.
- A woken thread rechecks its wait condition before it continues. This permits
  deterministic spurious wakes when a set operation does not satisfy it.
- Semaphore creation checks its attributes, initial count, and maximum count.
- Poll and wait operations acquire counts atomically. Signals cannot exceed the
  maximum count.
- Semaphore signal and delete operations use the same scheduler wake and
  recheck contract as event flags.
- Priority queue attributes are validated but currently use deterministic
  handle-order wake behavior.
- Realtime values use Unix time. Monotonic values do not move backward under a
  valid source.
- Process time starts with the kernel runtime. Its counter uses nanoseconds and
  reports a matching one-gigahertz frequency.
- The first HLE clock handlers expose process microseconds, the same nanosecond
  counter, and its one-gigahertz frequency through `libKernel`.
- Clock handlers register both readable export names and the NIDs confirmed by
  the two pinned references.
- The clock-gettime HLE handler writes both timespec fields atomically and
  returns kernel-compatible `EFAULT` or `EINVAL` results.
- The gettimeofday HLE handler writes both timeval fields through the same
  checked boundary.
- Clock conversion tests use an injected source and do not depend on host time.
- Guest paths use forward slashes, collapse empty and current-directory
  components, and reject relative paths, parent traversal, and embedded nulls.
- The initial file service is read-only. It exposes only files registered in
  memory and never resolves an untrusted guest path against the host file
  system.
- Read, positioned-read, seek, close, and size operations use typed handles and
  checked offsets.
- The first file HLE bridge reads a bounded guest path, maps service failures to
  kernel-compatible results, and registers open and close by name and NID.
- Checked read and positioned-read handlers preflight the full guest output
  range and use bounded temporary chunks. Seek validates signed offsets and
  origins before it changes the descriptor position.
- Stat and fstat write the shared 120-byte regular-file layout atomically.
  Registered memory files report a stable path-based inode, their size,
  512-byte block accounting, and deterministic zero timestamps.
- One atomic export batch binds the clock and file handlers to the same kernel
  runtime. A registration conflict leaves the destination registry unchanged.

KajPS5 implements these behaviors in its own C++ interfaces. It does not copy
the upstream host-thread executor, continuation system, object ownership
model, or source code.

The wait bridges do not resume a saved guest continuation. The caller must
invoke the wait again after the scheduler selects the woken thread.
