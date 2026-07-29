# Stage 2 CPU research

KajPS5 has one narrow native x86-64 execution test. It loads a public ELF made
from constants, copies its six-byte leaf entry from checked guest memory into a
writable host allocation, changes the allocation to read-execute, and calls
it. The leaf returns 42 and does not use arguments, memory, imports, system
calls, or threads.

The design review used these pinned references:

- KytyPS5 commit `f6e01e54031a3c615f089f061a4eab2f3c59acba`,
  `src/common/virtualMemory.cpp`,
  `src/common/platform/sysWindowsVirtual.cpp`, and
  `src/common/platform/sysLinuxVirtual.cpp`.
- SharpEmu commit `d5108e854d609808f17093a6f5dbbc711d09ad2e`,
  `src/SharpEmu.HLE/HostMemory.cs` and
  `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.cs`.

The KajPS5 implementation is a new small C++ boundary. It allocates writable
memory, copies at most 4 KiB from a readable and executable guest range, and
then removes write access before execution. Windows uses `VirtualAlloc`,
`VirtualProtect`, and `FlushInstructionCache`. POSIX hosts use `mmap`,
`mprotect`, and an instruction-cache clear.

The test-only executor is not an ELF command-line option. It has no guest CPU
context, ABI bridge, import dispatch, signal or exception recovery, timeout,
or instruction validation. It must only run controlled leaf fixtures until
those boundaries exist.
