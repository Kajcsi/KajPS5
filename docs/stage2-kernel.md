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
- An unsatisfied event wait blocks the current thread on that event handle.
- Setting or deleting an event wakes its blocked threads in handle order.
- A woken thread rechecks its wait condition before it continues. This permits
  deterministic spurious wakes when a set operation does not satisfy it.

KajPS5 implements these behaviors in its own C++ interfaces. It does not copy
the upstream host-thread executor, continuation system, object ownership
model, or source code.

The event-wait bridge does not resume a saved guest continuation. The caller
must invoke the wait again after the scheduler selects the woken thread.
