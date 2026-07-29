# Stage 2 kernel research

KajPS5 now has one typed kernel-handle table and deterministic event-flag
polling. It does not implement blocking waits or a guest scheduler yet.

The behavior review used these pinned upstream files:

- KytyPS5 `src/kernel/eventFlag.h` and `src/kernel/eventFlag.cpp` at
  `f6e01e54031a3c615f089f061a4eab2f3c59acba`.
- SharpEmu
  `src/SharpEmu.Libs/Kernel/KernelEventFlagCompatExports.cs` at
  `d5108e854d609808f17093a6f5dbbc711d09ad2e`.

The focused tests record these shared behaviors:

- Setting a pattern uses bitwise OR.
- Clearing an event flag retains the bits selected by the supplied mask.
- Polling supports all-bit and any-bit conditions.
- A successful poll can clear all bits or only the requested pattern.
- Polling returns the observed pattern before it applies a clear mode.
- Invalid attributes, wait modes, zero patterns, and stale handles fail.

KajPS5 implements these behaviors in its own C++ interfaces. It does not copy
the upstream scheduler, object ownership model, or source code.
