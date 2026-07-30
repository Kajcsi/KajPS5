# Stage 2 CPU research

KajPS5 has two deliberately small native x86-64 execution tests. The first
loads an ELF built from constants, copies its six-byte leaf entry from guest
memory into a writable host allocation, changes that allocation to
read-execute, and calls it. The leaf returns 42 without using arguments,
memory, imports, system calls, or threads. The second fixture calls a
checked HLE handler with six integer arguments through a linked import and is
described in `stage2-hle.md`.

The design review used these pinned references:

- KytyPS5 commit `f6e01e54031a3c615f089f061a4eab2f3c59acba`,
  `src/common/virtualMemory.cpp`,
  `src/common/platform/sysWindowsVirtual.cpp`, and
  `src/common/platform/sysLinuxVirtual.cpp`.
- SharpEmu commit `d5108e854d609808f17093a6f5dbbc711d09ad2e`,
  `src/SharpEmu.HLE/HostMemory.cs` and
  `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.cs`.

The KajPS5 boundary is small: allocate writable memory, copy at most 4 KiB from
a readable and executable guest range, then remove write access before the
call. Windows uses `VirtualAlloc`, `VirtualProtect`, and
`FlushInstructionCache`. POSIX hosts use `mmap`, `mprotect`, and an
instruction-cache clear. Guest code and generated HLE trampolines use the same
write-then-execute buffer, so generated code is not left writable.

On Windows, a small entry bridge preserves the extra nonvolatile integer and
floating-point state required by the host ABI before it calls System V guest
code. This keeps optimized host callers intact even when guest code uses
registers that are volatile on PS5.

The executor is available only to tests. The first ABI bridge captures the six
System V integer arguments and returns the checked handler's `RAX` value. It
does not capture the guest stack, vector registers, a full CPU context, faults,
or timeouts. Until those pieces exist, it must run only controlled leaf
fixtures.
