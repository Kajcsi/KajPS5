# Upstream projects

KajPS5 uses these independent projects as references:

| Project | Role | Language | License | Pinned commit |
| --- | --- | --- | --- | --- |
| [KytyPS5](https://github.com/KytyPS5/KytyPS5) | Primary native architecture reference | C++ | GPL-2.0-only, with original Kyty portions under MIT | `a65d17a5d689257a35644e01e9d15539361f0bf0` |
| [SharpEmu](https://github.com/sharpemu/sharpemu) | Behavior and test reference | C# | GPL-2.0-or-later | `5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445` |

The AGC command-buffer core closely adapts selected KytyPS5 algorithms. Other
current integrations use upstream behavior as evidence. Exact provenance is
recorded in `THIRD_PARTY_NOTICES.md`.

The pinned KytyPS5 README explicitly identifies the project as
`GPL-2.0-only`; its root `LICENSE` contains GPL version 2. KytyPS5 also keeps
the original Kyty MIT notice in `LICENSES/Kyty-MIT.txt`. A direct source import
must retain the exact notice and license that applies to that source. Do not
combine GPL-2.0-only KytyPS5 code with GPL-3.0-only code unless the copyright
holders supply a compatible grant.

At the start of a development session, run the read-only update check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-upstreams.ps1
```

The command compares each pin with the upstream default branch. It changes
neither the working tree nor the lock file. A reported update is a reason to
review the new work, not a reason to move the pin automatically.

Move a pin only when a newer commit supplies behavior, a correction, or a test
needed by the active KajPS5 milestone. Review the relevant upstream diff
first. The same commit must update the lock file, the table above, and the
refresh record below. Record the old and new commits, reviewed paths, reason,
and KajPS5 validation. Update `THIRD_PARTY_NOTICES.md` when the referenced or
adapted paths change.

## Refresh record

### 2026-08-01

- KytyPS5 moved from `f6e01e54031a3c615f089f061a4eab2f3c59acba`
  to `a65d17a5d689257a35644e01e9d15539361f0bf0`. The review covered the
  intervening commit list and the guest address-space, virtual-memory,
  runtime-linker, and allocation-test changes in `src/common/virtualMemory.*`,
  `src/common/platform/sysLinuxVirtual.cpp`, `src/kernel/memory.*`,
  `src/kernel/memoryAddressSpace.inc`, `src/loader/runtimeLinker.*`, and
  `tests/VirtualMemoryAllocationTests.cpp`. The new owner, rollback, range,
  and writable-host-patch tests apply to KajPS5's coherent guest-memory work.
- SharpEmu moved from `d5108e854d609808f17093a6f5dbbc711d09ad2e`
  to `7c9740fee8a633e17b145c6bc6d794e41d46c73f`. The review covered the
  intervening commit list, direct-execution memory-copy fast paths, event-queue
  waiter lifetime and generation tests, and new generic import coverage in
  `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend*.cs`,
  `src/SharpEmu.Libs/Kernel/KernelEventQueueCompatExports.cs`, and the matching
  kernel tests. These changes supply focused behavior and performance evidence
  for the next C++ HLE milestones.
- Validation: the update checker reported both pins current; all 46 KajPS5
  tests passed in Debug, Release, and AddressSanitizer builds. Gitleaks and
  actionlint also passed.
- A normalized library-scope comparison found SharpEmu implementations that
  do not have matching KytyPS5 `LIB_VERSION` families:
  `libSceBluetoothHid`, `libSceDiscMap`, `libSceNpCppWebApi`,
  `libSceNpManagerForToolkit`, `libSceShareUtility`, and
  `libSceVideoRecording`. SharpEmu also has `libSceJson` and
  `libSceVideodec` aliases for KytyPS5's `Json2` and `Videodec2` families.
  Its `libfmod` surface is third-party-facing and needs separate provenance.
  These scopes remain SharpEmu coverage targets where title evidence requires
  them.
- SharpEmu moved from `7c9740fee8a633e17b145c6bc6d794e41d46c73f`
  to `5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`. The review covered all four
  intervening commits. The M6-relevant change was
  `ea9be7484f7679e3d0f060ee4722e480d755623a`, including
  `src/SharpEmu.Libs/Agc/AgcExports.cs` and the new context-register and
  shader-register tests. It confirms that direct and indirect register writes
  share persistent keys across submissions. The review also recorded the
  shader and Vulkan corrections in
  `a8fa9c96dce5fe7e3424f811cab8e15790a11339` for M7 and M8. The two GUI-only
  commits do not affect the current runtime milestone.
- Validation: the new checked PM4 processor and all 60 KajPS5 tests passed in
  Debug, Release, and AddressSanitizer builds. Gitleaks, actionlint, commit
  email, and repository privacy checks also passed.
- The M6 submission path snapshots each reached command buffer, preserves the
  exact nested cursor at an unsatisfied wait, and drains graphics and compute
  queues to a fixed point. This follows KytyPS5 submission ordering and
  re-expresses SharpEmu wait-resume behavior without importing another GPU or
  scheduler owner. All 61 tests pass in Debug, Release, and AddressSanitizer
  builds.
- The first `libSceAgcDriver` bridge uses the KytyPS5 packet ABI and NIDs for
  DCB and owner-scoped ACB submission. SharpEmu supplies independent evidence
  for descriptor checks, queue separation, and resuming earlier blocked work
  before later work.
- SharpEmu's separate AGC-wrapper and standard PM4 `WRITE_DATA` control layouts
  are preserved. Memory destinations now apply ordered, checked writes. A
  focused regression proves that reserved low-byte noise in a standard packet
  does not change its destination selector.
- KytyPS5 supplies the native Gen5 `sceAgcCbReleaseMem` packet layout and
  validation. SharpEmu supplies the independent standard `RELEASE_MEM` control
  decoder and counter-write behavior. Both packet forms use the same ordered,
  checked memory path in KajPS5.
- KytyPS5 supplies the graphics event registration shape and filter value.
  SharpEmu supplies independent filter-based wake behavior when a hardware
  event type differs from the guest event identifier. KajPS5 also gives each
  registration a private generation to reject already-reserved stale events.

When KajPS5 imports code, identify the upstream file and commit, keep its
copyright and license notice, and update `THIRD_PARTY_NOTICES.md` in the same
commit.

Do not copy proprietary files, game data, keys, firmware, or system modules
into this repository.
