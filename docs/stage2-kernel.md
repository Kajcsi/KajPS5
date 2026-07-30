# Stage 2 kernel research

KajPS5 has one kernel runtime, one typed handle table, user-event queues, and a
cooperative guest scheduler. It does not execute general guest code or resume
a blocked guest call yet.

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
- KytyPS5 `src/kernel/pthread.h` and `src/kernel/pthread.cpp` at the pinned
  KytyPS5 commit.
- SharpEmu `src/SharpEmu.Libs/Kernel/KernelRuntimeCompatExports.cs`,
  `KernelExports.cs`, `KernelPthreadCompatExports.cs`, and
  `KernelPthreadExtendedCompatExports.cs` at the pinned SharpEmu commit.
- KytyPS5 `src/kernel/fileSystem.h` and `src/kernel/fileSystem.cpp` at the
  pinned KytyPS5 commit.
- SharpEmu open, close, read, seek, stat, and directory-read behavior in
  `src/SharpEmu.Libs/Kernel/KernelMemoryCompatExports.cs` at the pinned
  SharpEmu commit.
- KytyPS5 `src/kernel/eventQueue.h` and `src/kernel/eventQueue.cpp` at the
  pinned KytyPS5 commit.
- SharpEmu
  `src/SharpEmu.Libs/Kernel/KernelEventQueueCompatExports.cs` at the pinned
  SharpEmu commit.

The tests capture the behavior below.

## Threads and synchronization

- Ready threads run in FIFO order, and only one guest thread runs at a time.
  Yield puts the current thread at the end of the ready queue.
- Blocking records a wait key. Wake operations return matching threads to the
  ready queue in handle order, with an optional wake limit.
- Thread exit keeps the return value and wakes every joiner. Joining a live
  thread blocks; joining an exited thread returns its saved value. Self joins
  and stale handles leave scheduler state unchanged.
- Pthread attributes use guest-visible synthetic handles instead of exposing
  host pointers. Their default affinity, guard size, stack size, scheduler
  policy, and priority match KytyPS5. Stack sizes below 16 KiB are rejected.
- Pthread TLS has 256 reusable keys. Values belong to the current guest thread,
  key deletion clears every stored value, and no host thread-local state leaks
  into the guest model.
- Pthread identity and equality use guest scheduler handles. Pthread yield
  returns the current thread to the shared ready queue.
- Pthread creation checks output, attribute, entry-point, and optional name
  pointers before it changes scheduler state. It records the guest entry point,
  argument, priority, and attributes without creating a second host runtime.
- Pthread join uses the scheduler's existing block, wake, and recheck path.
  Pthread exit preserves the guest return value and wakes all joiners. POSIX
  handlers return POSIX pthread errors; `scePthread` handlers return kernel
  errors.
- Pthread mutexes use guest-visible synthetic handles and guest scheduler
  handles for ownership. Error-checking, recursive, normal, and adaptive types
  have separate relock behavior. Protocol values are checked before mutation.
- A zero or adaptive static initializer creates a mutex on first use. A
  contended lock blocks through the shared scheduler. Unlock grants ownership
  to the first waiter before wakeup, and thread exit releases owned mutexes so
  a stopped worker cannot strand later work.
- Setting an event flag uses bitwise OR. Clearing retains the bits selected by
  the supplied mask. Poll supports all-bit and any-bit conditions and can clear
  all bits or only the requested pattern after returning the observed value.
- Invalid event attributes, wait modes, zero patterns, and stale handles fail.
  A blocked event wait rechecks its condition after every wake, including a
  wake that does not satisfy it.
- Semaphore creation validates its attributes and count range. Poll and wait
  acquire counts atomically, while signal never exceeds the maximum. Signal
  and delete follow the same wake-and-recheck rule as event flags.
- Priority attributes are validated. Wake order remains handle-based until the
  scheduler implements priority selection.
- Event queues follow KytyPS5's typed registration, trigger, and user-event
  flag contract. Records use the 32-byte kernel layout. Repeated pending
  triggers for one identifier follow SharpEmu's coalescing behavior: new data
  replaces old data, `fflags` counts extra triggers, and queue order stays put.
  Trigger and delete operations wake threads through the shared scheduler.

## Time

- Realtime values use Unix time. Monotonic values do not move backward when
  the injected source is valid.
- Process time starts with the kernel runtime. Its nanosecond counter reports a
  matching one-gigahertz frequency.
- The first `libKernel` clock handlers expose process microseconds, that same
  counter, and its frequency by readable name and confirmed NID.
- `sceKernelClockGettime` and `sceKernelGettimeofday` write their complete
  output structures atomically. Invalid clocks and bad guest pointers return
  kernel-compatible results.
- Clock tests inject their time source instead of depending on the host clock.

## Files and directories

- Guest paths use forward slashes, collapse empty and `.` components, and
  reject relative paths, `..`, and embedded nulls.
- The initial file service is read-only and exposes only files registered in
  memory. An untrusted guest path is never resolved against the host file
  system.
- Open, read, positioned read, seek, close, and size operations use typed
  handles and validated offsets. Guest output ranges are checked before reads,
  and large reads use bounded temporary chunks. A failed write does not advance
  the file position.
- Stat and fstat write the full 120-byte regular-file layout at once. Memory
  files report a stable path-based inode, 512-byte block accounting, and zero
  timestamps.
- A directory handle captures its immediate children when opened. Entries
  begin with `.` and `..`, then follow stable case-insensitive name order.
- `sceKernelGetdents` writes one zero-filled 512-byte record per call and uses
  FNV-1a name hashes. It distinguishes regular-file handles, stale handles,
  short requests, and bad output ranges.
- `sceKernelGetdirentries` uses the same record format and can write the
  captured entry position to an optional base pointer. Every output is checked
  before the cursor moves.
- Reachability queries inspect only the registered guest namespace and return
  different results for missing, invalid, and unreadable paths.

## HLE registration

- One atomic batch binds the current clock, event-queue, event-flag, file,
  memory, pthread, and semaphore handlers to the same kernel runtime. A
  conflict leaves the registry unchanged.
- Semaphore handlers create, delete, poll, and signal the same objects used by
  the scheduler. Event-flag handlers create, delete, set, clear, and poll their
  shared objects. Handle and result outputs are checked before state changes.
- Memory handlers expose `sceKernelMprotect`, `sceKernelMunmap`, their POSIX
  aliases, and the protection query. They use 16 KiB guest pages and keep CPU
  and GPU permission bits in the shared region table. Invalid or partly
  unmapped ranges leave the table unchanged.
- Event-queue handlers expose create, delete, user-event add, edge add,
  trigger, and removal by name and NID.
- Blocking wait exports remain deferred until the runtime can save and resume
  a guest call continuation. A live pthread join uses the same temporary
  recheck contract, as does a contended pthread mutex lock.

KajPS5 implements this behavior through its own C++ interfaces. It does not
copy an upstream executor, continuation system, ownership model, or source
code.

The current wait bridges cannot resume a saved guest continuation. A caller
must invoke the wait again after the scheduler selects the woken thread.
