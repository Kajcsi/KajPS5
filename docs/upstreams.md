# Upstream projects

KajPS5 uses these independent projects as references:

| Project | Role | Language | License | Pinned commit |
| --- | --- | --- | --- | --- |
| [KytyPS5](https://github.com/KytyPS5/KytyPS5) | Primary native architecture reference | C++ | GPL-2.0-only, with original Kyty portions under MIT | `fb5ecec455cf6c67154134429485ffccbfc34203` |
| [SharpEmu](https://github.com/sharpemu/sharpemu) | Behavior and test reference | C# | GPL-2.0-or-later | `9e10d7c44a2821cfd5ccd3417c09c0cf269285a4` |

## Hardware references

Use AMD's official [RDNA 2 Shader Instruction Set Architecture](https://docs.amd.com/v/u/en-US/rdna2-shader-instruction-set-architecture)
guide to verify shader instruction encodings and behavior when the emulator
references do not provide enough evidence. Treat it as a hardware baseline;
PS5-specific behavior still needs separate evidence and tests.

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

### 2026-08-04

- Final M8 presentation and title-run refresh retained the current KytyPS5 pin
  `fb5ecec455cf6c67154134429485ffccbfc34203` after reviewing its Vulkan,
  shader, resource, tile, and presentation/window architecture. SharpEmu
  remained pinned at `9e10d7c44a2821cfd5ccd3417c09c0cf269285a4` for
  fail-closed VideoOut behavior and retry/recovery/merge/coherence regressions.
  KajPS5
  keeps one C++ runtime, guest-memory owner, image cache, Vulkan context, and
  presentation owner: it supports checked linear and RenderTarget64KB tiled
  frames, bounded Vulkan action/presentation work, and one visible Windows
  title window with message, resize, and retained-work polling. Explicit
  headless mode skips host graphics and does not complete VideoOut flips
  without a presentation context. No upstream source was copied verbatim and
  this remains research infrastructure, not a game-compatibility claim.

- Final M8 validation at KajPS5 `57aff2a` recorded 94 configured tests passing
  in Debug and Release, focused AddressSanitizer coverage, and live/repeated
  presentation smoke on an NVIDIA GeForce RTX 4090. GitHub Actions run
  `30935269714` recorded the corresponding CI evidence.

### 2026-08-03

- M8 translated offscreen draw execution reviewed KytyPS5
  `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp` and
  `renderDraw.cpp` at `fb5ecec455cf6c67154134429485ffccbfc34203`, together
  with SharpEmu's pinned `VulkanVideoPresenter.cs` fence/retained-work
  behavior at `9e10d7c44a2821cfd5ccd3417c09c0cf269285a4`. KajPS5 re-expresses
  only an exact descriptor-free graphics-pipeline key, dynamic-rendering
  recording, finite-fence retention, and checked guest-color readback in its
  existing C++ runtime/cache owners. It imports no renderer, presenter,
  swapchain, surface, title-rendering path, or second memory owner. The live
  smoke is a 4x4 linear RGBA8 guest-backed offscreen draw, not presentation.

- M8 Vulkan image-resource preparation retained KytyPS5 at
  `fb5ecec455cf6c67154134429485ffccbfc34203` after reviewing its
  `image/{image,imageView,textureCommon}.*`, `textureCache.*`, and
  `gpuResourceManager.*` ownership and aliasing paths. SharpEmu remained at
  `9e10d7c44a2821cfd5ccd3417c09c0cf269285a4` for guest-image sizing, alias,
  and write-tracker behavior. KajPS5 creates one transactional image lease and
  optional compatible sibling view over its same allocation; it imports no
  cache, renderer, page table, memory observer, or second owner.
  The follow-up command seam uses only Vulkan transfer barriers/copies and the
  existing `GpuResourceCoherence` completion contract; it imports no upstream
  command scheduler or renderer path.

- M8 translated image/sampler preparation reviewed KytyPS5
  `renderer/pipeline/descriptors.cpp` and `renderer/cache/samplerCache.cpp` at
  `fb5ecec455cf6c67154134429485ffccbfc34203`, plus the pinned SharpEmu image
  alias and write-tracker behavior. KajPS5 re-expresses only checked descriptor
  fields, alias ownership, and Vulkan lease topology; no renderer, cache, or
  memory owner was copied.

- M8 translated descriptor execution additionally reviewed KytyPS5
  `renderer/pipeline/descriptors.cpp`, `renderer/renderCompute.cpp`, and
  synchronization ownership at `fb5ecec455cf6c67154134429485ffccbfc34203`,
  alongside SharpEmu's guest-image write tracking and finite-fence behavior.
  KajPS5 keeps those operations within its existing runtime, cache, and
  coherence owners: grouped Vulkan descriptors, image transfer commands, and
  fence-complete checked readback are not a renderer or a second memory owner.

- M8 guest-image layout work retained KytyPS5 at
  `fb5ecec455cf6c67154134429485ffccbfc34203` after reviewing
  `src/graphics/guest_gpu/gpu_format.*`,
  `src/graphics/host_gpu/renderer/image/{imageInfo,textureCommon,tiler}.*`,
  `src/graphics/host_gpu/renderer/cache/textureCache.*`, and
  `tests/{ImagePageTableTests,ResourceTrackingTests}.cpp`. SharpEmu remained
  at `9e10d7c44a2821cfd5ccd3417c09c0cf269285a4` after reviewing
  `VulkanGuestImageByteCountTests.cs`, `VulkanGuestImageTypeTests.cs`,
  `VulkanPresentEncodeFormatTests.cs`, `GuestImageWriteTracker`, and
  `Gfx10UnifiedFormat`. KajPS5 adapts only the checked layout/alias shape and
  re-expresses its public byte-count, dimensional, sRGB-alias, and failure
  behavior in C++; it imports no upstream renderer, image cache, page table,
  memory owner, or C# source. Tiled layouts now fail closed pending a proven
  detiler.

- M8 guest-buffer work retained KytyPS5 at `fb5ecec455cf6c67154134429485ffccbfc34203` after review through `d09c81d`: the relevant buffer-cache ownership model did not change. SharpEmu moved to reviewed commit `9e10d7c44a2821cfd5ccd3417c09c0cf269285a4`; `f3d9439952a40c5b81b0d0dec443184e82a683d1` and `26bda04` were also reviewed for preserving newer CPU bytes during GPU writeback and for overflow/degenerate-range guards. KajPS5 retains one GpuRuntime owner and one checked GuestMemory observer; no upstream scheduler, page manager, or address-space owner was imported.

### 2026-08-02

- M8 resource-coherence review used the current KytyPS5 pin's
  `src/graphics/host_gpu/memoryTracker.*`, `pageManager.*`, and
  `renderer/cache/gpuResourceManager.*`, plus the current SharpEmu pin's
  `src/SharpEmu.HLE/GuestImageWriteTracker.cs`,
  `src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs`, and focused
  guest-image write-generation tests. The first KajPS5 slice preserves one
  checked guest-memory model and one GpuRuntime owner: it adds actual-write
  events (including a changed prefix when a checked mutation fails late),
  generation-tagged resource records, explicit GPU-write-pending metadata, and
  range-local map-lifetime rejection. Shared backing aliases fan out each
  actual backing-byte mutation to every overlapping guest alias through a
  bounded exact report, with a conservative whole-GuestMemory fallback if that
  report is exhausted. GuestMemory stores internal mapping-
  lifetime intervals alongside its canonical region view, so unrelated
  Map/Unmap operations leave a resource current while an overlapping
  unmap/remap changes its token.
  It deliberately does not port a page manager, renderer cache, host-fault
  observer, or a second address-space owner. Native direct guest writes remain
  unobserved until GuestMemory gains a fault-backed source; such a source must
  defer verified faults to the ordinary GuestMemory observation funnel.
- M8 Vulkan-device and compute-execution bootstrap reviewed the current
  KytyPS5 pin's
  `src/graphics/presentation/window/vulkanWindow.cpp`,
  `src/graphics/host_gpu/vulkanInstance.h`, and
  `src/graphics/host_gpu/renderer/{context,commandScheduler,masterSemaphore,render}.*`,
  including its complete
  non-surface renderer-ready core feature baseline, universal graphics/compute
  queue, queue mutex, command ownership, and completion model. It also reviewed
  the current
  SharpEmu pin's `src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs` and
  `tests/SharpEmu.Libs.Tests/VideoOut/VulkanPhysicalDeviceScoringTests.cs` for
  deterministic device ranking, capability diagnostics, finite fence timeout,
  and later abandoned-submission collection behavior. The related Kyty
  `3rdparty/Vulkan-Headers` submodule entry is
  `2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350`; KajPS5 vendors only its exact C
  declarations, upstream `LICENSE.md`, and Apache-2.0 license text needed for
  dynamic loading, rather than a loader binary, SDK, VMA, window system, or
  renderer dependency. The new optional context remains the one C++
  `GpuRuntime` owner. Its sole child executor dynamically resolves only core
  compute functions, accepts already-validated recompiler SPIR-V, and gives
  each dispatch a private primary command buffer, fence, shader module, empty
  pipeline layout, and compute pipeline. A finite timeout retains those
  in-flight objects for status polling rather than freeing or reusing them;
  it does not create buffers, descriptors, graphics pipelines, a surface,
  swapchain, or presentation path. Surface/swapchain/format work and the
  extension-specific color-write and depth-clip feature gates remain deferred.
- Mapping-token capture is serialized with GuestMemory Map/MapShared/Protect/
  Unmap operations, and callbacks run after GuestMemory releases its mapping
  and coherence locks. The token is a checked snapshot rather than a mapping
  reservation: a future backend that spans independent calls must synchronize
  its own mapping lifetime and revalidate before committing work.
- Per-resource CPU write generations are monotonic even when concurrent
  callbacks arrive out of order; a generation-exhaustion precision loss is
  sticky and remains fail-closed. Host-mapped Initialize and InitializeFill
  hold the exclusive mapping transition while temporarily changing native page
  protections, so a concurrent initializer cannot restore a read-only page
  under another write.
- M8 resource-coherence validation: all 76 configured CTest cases passed in
  Windows Debug, Release, and AddressSanitizer builds. The seven external
  `spirv-val --target-env vulkan1.2` checks also passed in each build.
- M8 Vulkan-device validation: all 78 configured CTest cases passed in Windows
  Debug, Release, and AddressSanitizer builds. The seven external
  `spirv-val --target-env vulkan1.2` checks passed in each build. The live smoke
  test selected an NVIDIA GeForce RTX 4090 with Vulkan 1.4.341 and queue family
  0 in all three configurations.
- M8 Vulkan-compute validation adds injected transactional creation, recording,
  submit, finite-timeout, retained-work, and resource-bound coverage plus a
  live public-recompiler smoke dispatch. The latter must fail after device
  selection/execution errors and skips only when the Vulkan host is unavailable
  or cannot meet the renderer-ready device requirements. All 80 configured
  CTest cases passed in Windows Debug, Release, and AddressSanitizer builds.
  The seven external `spirv-val --target-env vulkan1.2` checks passed in each
  build. Both Vulkan smokes passed; the compute smoke selected an NVIDIA
  GeForce RTX 4090 with Vulkan 1.4.341, queue family 0, and completed a 1/1/1
  dispatch with timeline 1.
- KytyPS5 moved from `a65d17a5d689257a35644e01e9d15539361f0bf0`
  to `59b8fad34189816137c5cbe1982e9fd499532b6f`. The review covered all 23
  commits and the complete changed-path list. The current M7 work uses the
  loop-control, loop-normalization, and shared-exit corrections in
  `src/graphics/shader/recompiler/cfg/ShaderCFG.cpp` and
  `tests/shaderCfgTests.cpp`. The packed 10-10-10-2 format addition is also
  covered by the new IR test. Renderer, audio, loader, kernel, and AGC changes
  remain recorded for their later milestones; this change does not import
  those owners.
- SharpEmu moved from `5ee7cd1dfafdeb0ce0e458a365692df4b2e1c445`
  to `cf3bd0b4f2016eede08692110b6c14f08b5a912c`. The review covered
  `src/SharpEmu.Libs/Agc/AgcExports.cs`, including checked CreateShader
  header relocation, full register-table search, and GS/HS front-half table
  handling. KajPS5 adapts that behavior through its existing C++ GPU runtime;
  it does not import a renderer or a second runtime.
- KytyPS5 moved from `59b8fad34189816137c5cbe1982e9fd499532b6f`
  to `fb5ecec455cf6c67154134429485ffccbfc34203`. The review covered the
  AGC submission-buffer cleanup, the GPU resource-unmap ordering fix, the
  depth-only target correction, the new renderer regressions, and the Net ABI
  addition. The shader-header and `GraphicsCreateShader` behavior used here did
  not change. The renderer and unmap corrections remain evidence for M8; they
  do not justify importing a second memory or submission owner.
- SharpEmu then moved from `cf3bd0b4f2016eede08692110b6c14f08b5a912c`
  to `4b5ea6a79346cb4529fa531cf2c1973f3978eb22`. The review covered its
  zero-color DCC fast-clear recognition and next-render-pass attachment clear.
  `CreateShader` did not change. The clear behavior remains focused evidence
  for KajPS5's later M8 renderer integration; no SharpEmu renderer code is
  imported here.
- Shader-registration validation: all 74 configured CTest cases passed in
  Windows Debug, Release, and AddressSanitizer builds. This includes the new
  public AGC export, the focused shader-runtime tests, and seven external
  `spirv-val --target-env vulkan1.2` checks.
- Shader-binding validation: all 75 configured CTest cases passed in Windows
  Debug, Release, and AddressSanitizer builds. The new focused test covers
  stable draw and dispatch stage snapshots, direct program lookup, nonzero
  entry offsets, unregistered programs, index conflicts, and failed-publication
  rollback. The seven external `spirv-val --target-env vulkan1.2` checks also
  passed in each build.
- Validation: all 65 tests passed in Windows Debug, Release, and AddressSanitizer
  builds. Gitleaks, actionlint, upstream-pin, commit-email, tracked-AGENTS,
  repository-privacy, and diff checks passed.
- ShaderRecompiler validation: Debug 73/73, Release 73/73, and MSVC
  AddressSanitizer 73/73 passed with `KAJPS5_REQUIRE_SPIRV_VAL=ON`. Seven
  representative emitted modules were externally validated by pinned
  `spirv-val --target-env vulkan1.2`: minimal compute, vertex, pixel,
  descriptor/resource compute, wave-32 compute, float-modifier compute, and
  pixel-parameter collision. Live AGC/Vulkan execution remains incomplete.

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
- KytyPS5 supplies the Gen5 DCB and ACB `EVENT_WRITE` packet layouts, export
  identities, and driver event registration ABI. SharpEmu supplies independent
  evidence that delivery must match the graphics filter, retain the guest
  event ID and user data, and report the hardware event type separately.
  KajPS5 applies that behavior through its existing GPU runtime and event queue.
- KytyPS5 supplies the direct and multi-buffer DCB and ACB entry points and
  their NIDs. SharpEmu independently confirms the multi-DCB array ABI and a
  4096-entry input bound. KajPS5 first checks and snapshots every supplied
  command buffer, then commits the complete batch to its existing queues.
- KytyPS5 supplies the 32-byte event record, blocking wait ABI, field
  accessors, and AGC event query rules. SharpEmu independently confirms the
  record offsets, checked guest writes, continuation retry, and filter-based
  graphics payload. KajPS5 connects these rules to its existing scheduler and
  rejects late events after a timed wait expires.
- The complete KytyPS5 Gen5 instruction decoder is adapted under
  `src/gpu/shader/recompiler/decompiler`. Its scalar, vector, memory, image,
  export, SDWA, and DPP families move together as one compiler component. The
  KytyPS5 SPIR-V section builder is adapted under `src/gpu/shader`. SharpEmu's
  independent SPIR-V builder and synthetic-module checks supply the
  deterministic output and instruction-boundary test behavior.

When KajPS5 imports code, identify the upstream file and commit, keep its
copyright and license notice, and update `THIRD_PARTY_NOTICES.md` in the same
commit.

Do not copy proprietary files, game data, keys, firmware, or system modules
into this repository.
