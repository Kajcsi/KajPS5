// Copyright (C) 2026 KajPS5 contributors
// Architecture reference: KytyPS5 src/graphics/host_gpu/memoryTracker.*,
// pageManager.*, and renderer/cache/gpuResourceManager.* at
// fb5ecec455cf6c67154134429485ffccbfc34203.
// Behavior and test reference: Copyright (C) 2026 SharpEmu Emulator Project,
// src/SharpEmu.HLE/GuestImageWriteTracker.cs and
// tests/SharpEmu.Libs.Tests/{Memory/GuestImageWriteTrackerTests.cs,
// VideoOut/VulkanGuestImageCpuSyncPolicyTests.cs} at
// 4b5ea6a79346cb4529fa531cf2c1973f3978eb22.
// SPDX-License-Identifier: GPL-2.0-only

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "core/memory/guest_memory.h"
#include "core/memory/shared_memory_backing.h"
#include "gpu/runtime.h"

namespace {

constexpr std::uint64_t kBase = 0x500000;
constexpr std::uint64_t kMemorySize = 0x1000;
constexpr std::uint64_t kResourceAddress = kBase + 0x200;
constexpr std::uint64_t kResourceSize = 0x40;

void Check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "gpu_resource_coherence_test: " << message << '\n';
    std::exit(1);
  }
}

struct Fixture {
  kajps5::memory::GuestMemory memory{
      kBase, kMemorySize,
      kajps5::memory::GuestMemoryProtection::kRead |
          kajps5::memory::GuestMemoryProtection::kWrite};
  kajps5::gpu::GpuRuntime runtime{memory};
};

[[nodiscard]] kajps5::gpu::GpuResourceCoherenceState State(
    kajps5::gpu::GpuResourceCoherence& coherence,
    kajps5::gpu::GpuResourceId resource) {
  const auto state = coherence.Query(resource);
  Check(state.has_value(), "registered resource became unknown");
  return *state;
}

void AcknowledgeInitialUpload(kajps5::gpu::GpuResourceCoherence& coherence,
                              kajps5::gpu::GpuResourceId resource) {
  const auto state = State(coherence, resource);
  Check(state.mapped && !state.mapping_changed && state.needs_cpu_upload &&
            state.cpu_generation_precise &&
            coherence.AcknowledgeCpuUpload(resource,
                                           state.cpu_write_generation),
        "initial upload could not be acknowledged");
}

void TestCheckedWritesAndOverlapBoundaries() {
  Fixture fixture;
  auto& coherence = fixture.runtime.resource_coherence();
  const auto resource =
      coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(resource.has_value(), "mapped resource range was rejected");

  AcknowledgeInitialUpload(coherence, *resource);
  const auto clean = State(coherence, *resource);
  Check(!clean.needs_cpu_upload,
        "acknowledged resource still requested an upload");

  const std::array<std::byte, 4> bytes = {
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
  Check(fixture.memory.Write(kBase + 0x100, bytes),
        "unrelated checked write failed");
  const auto unrelated = State(coherence, *resource);
  Check(!unrelated.needs_cpu_upload &&
            unrelated.cpu_write_generation == clean.cpu_write_generation,
        "unrelated write dirtied the resource");

  Check(fixture.memory.Write(kResourceAddress - bytes.size(), bytes) &&
            fixture.memory.Write(kResourceAddress + kResourceSize, bytes),
        "exact-boundary checked write failed");
  const auto boundary = State(coherence, *resource);
  Check(!boundary.needs_cpu_upload &&
            boundary.cpu_write_generation == clean.cpu_write_generation,
        "exact non-overlap boundary dirtied the resource");

  const std::array<std::byte, 2> partial = {std::byte{0x7a},
                                             std::byte{0x7b}};
  Check(fixture.memory.Write(kResourceAddress - 1, partial),
        "leading partial-overlap checked write failed");
  const auto leading = State(coherence, *resource);
  const auto repeated = State(coherence, *resource);
  Check(leading.needs_cpu_upload && repeated.needs_cpu_upload &&
            leading.cpu_write_generation == repeated.cpu_write_generation &&
            leading.cpu_write_generation > clean.cpu_write_generation,
        "overlapping write was not stably dirty before acknowledgement");
  Check(coherence.AcknowledgeCpuUpload(*resource,
                                       leading.cpu_write_generation),
        "observed leading generation was not acknowledged");
  const auto acknowledged = State(coherence, *resource);
  Check(!acknowledged.needs_cpu_upload,
        "acknowledgement did not clear the observed generation");

  Check(fixture.memory.Write(kResourceAddress + kResourceSize - 1, partial),
        "trailing partial-overlap checked write failed");
  const auto trailing = State(coherence, *resource);
  Check(trailing.needs_cpu_upload &&
            trailing.cpu_write_generation > leading.cpu_write_generation &&
            !coherence.AcknowledgeCpuUpload(*resource,
                                             leading.cpu_write_generation),
        "later overlap did not reject a stale acknowledgement");
  Check(State(coherence, *resource).needs_cpu_upload &&
            coherence.AcknowledgeCpuUpload(*resource,
                                           trailing.cpu_write_generation) &&
            !State(coherence, *resource).needs_cpu_upload,
        "later overlap did not dirty and clear exactly once");
}

void TestRangeFailureAndMappingLifetime() {
  Fixture fixture;
  auto& coherence = fixture.runtime.resource_coherence();
  Check(!coherence.RegisterResource(kResourceAddress, 0).has_value() &&
            !coherence
                 .RegisterResource(std::numeric_limits<std::uint64_t>::max() -
                                       1,
                                   2)
                 .has_value() &&
            !coherence.RegisterResource(kBase + kMemorySize - 8, 16)
                 .has_value(),
        "invalid, overflowing, or unmapped range was registered");

  const auto resource =
      coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(resource.has_value() && *resource == 1,
        "rejected registration changed resource publication state");
  AcknowledgeInitialUpload(coherence, *resource);

  const auto protection = kajps5::memory::GuestMemoryProtection::kRead |
                          kajps5::memory::GuestMemoryProtection::kWrite;
  Check(fixture.memory.Unmap(kResourceAddress, kResourceSize),
        "resource range could not be unmapped");
  const auto unmapped = State(coherence, *resource);
  Check(!unmapped.mapped && unmapped.mapping_changed &&
            !coherence.AcknowledgeCpuUpload(*resource,
                                             unmapped.cpu_write_generation) &&
            !coherence.MarkGpuWrite(*resource),
        "unmapped resource retained valid coherence state");

  Check(fixture.memory.Map(kResourceAddress, kResourceSize, protection),
        "resource range could not be remapped");
  const auto remapped = State(coherence, *resource);
  Check(remapped.mapped && remapped.mapping_changed &&
            !coherence.InvalidateGpuWrite(*resource),
        "remapped range reused the old resource lifetime");
  Check(coherence.UnregisterResource(*resource),
        "old mapping-lifetime record could not be removed");
  const auto replacement =
      coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(replacement.has_value() && *replacement != *resource &&
            State(coherence, *replacement).needs_cpu_upload,
        "remapped range did not require a fresh resource record and upload");
}

void TestRangeLocalMappingTokens() {
  Fixture fixture;
  auto& coherence = fixture.runtime.resource_coherence();
  const auto resource =
      coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(resource.has_value(), "range-token resource was rejected");
  AcknowledgeInitialUpload(coherence, *resource);

  const auto protection = kajps5::memory::GuestMemoryProtection::kRead |
                          kajps5::memory::GuestMemoryProtection::kWrite;
  constexpr auto kUnrelatedAddress = kBase + 0x100;
  constexpr auto kUnrelatedSize = std::uint64_t{0x40};
  Check(fixture.memory.Unmap(kUnrelatedAddress, kUnrelatedSize),
        "unrelated range could not be unmapped");
  const auto unrelated_unmapped = State(coherence, *resource);
  Check(unrelated_unmapped.mapped && !unrelated_unmapped.mapping_changed &&
            coherence.MarkGpuWrite(*resource) &&
            coherence.InvalidateGpuWrite(*resource),
        "unrelated unmap invalidated the resource mapping token");

  Check(fixture.memory.Map(kUnrelatedAddress, kUnrelatedSize, protection),
        "unrelated range could not be remapped");
  const auto unrelated_remapped = State(coherence, *resource);
  Check(unrelated_remapped.mapped && !unrelated_remapped.mapping_changed &&
            !unrelated_remapped.needs_cpu_upload,
        "unrelated remap invalidated the resource mapping token");

  constexpr auto kPartialOffset = std::uint64_t{0x10};
  constexpr auto kPartialSize = std::uint64_t{0x10};
  Check(fixture.memory.Unmap(kResourceAddress + kPartialOffset, kPartialSize),
        "partially overlapping range could not be unmapped");
  const auto partially_unmapped = State(coherence, *resource);
  Check(!partially_unmapped.mapped && partially_unmapped.mapping_changed &&
            !coherence.MarkGpuWrite(*resource),
        "partially overlapping unmap retained the old mapping token");

  Check(fixture.memory.Map(kResourceAddress + kPartialOffset, kPartialSize,
                           protection),
        "partially overlapping range could not be remapped");
  const auto partially_remapped = State(coherence, *resource);
  Check(partially_remapped.mapped && partially_remapped.mapping_changed &&
            !coherence.InvalidateGpuWrite(*resource),
        "partially overlapping remap reused the old mapping token");
}

void TestLatePartialMutationFailureIsObserved() {
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;
  using kajps5::memory::GuestMemoryTestFaultPoint;
  using kajps5::memory::SharedMemoryBacking;

  constexpr auto kPartialBase = kBase + 0x2000;
  constexpr auto kSegmentSize = std::uint64_t{4};
  constexpr auto kDestinationSize = kSegmentSize * 2;
  const auto protection = GuestMemoryProtection::kRead |
                          GuestMemoryProtection::kWrite;
  GuestMemory memory(kPartialBase, 0x40, GuestMemoryProtection::kNone);
  const auto backing = std::make_shared<SharedMemoryBacking>(kSegmentSize);
  Check(memory.Map(kPartialBase, kSegmentSize, protection) &&
            memory.MapShared(kPartialBase + kSegmentSize, kSegmentSize,
                             protection, backing, 0) &&
            memory.Map(kPartialBase + 0x10, kDestinationSize, protection),
        "partial-mutation backing layout could not be mapped");

  const std::array<std::byte, kDestinationSize> source = {
      std::byte{0x10}, std::byte{0x11}, std::byte{0x12}, std::byte{0x13},
      std::byte{0x20}, std::byte{0x21}, std::byte{0x22}, std::byte{0x23},
  };
  Check(memory.Initialize(kPartialBase + 0x10, source),
        "partial-mutation copy source could not be initialized");

  kajps5::gpu::GpuRuntime runtime(memory);
  auto& coherence = runtime.resource_coherence();
  const auto resource =
      coherence.RegisterResource(kPartialBase, kDestinationSize);
  Check(resource.has_value(), "partial-mutation resource was rejected");
  AcknowledgeInitialUpload(coherence, *resource);

  const auto CheckLateFailure = [&](auto&& mutation, const char* name) {
    const auto before = State(coherence, *resource);
    GuestMemory::SetWriteTestFaultForTesting(
        GuestMemoryTestFaultPoint::kFailWriteOrFillAfterFirstChunk);
    const auto completed = mutation();
    GuestMemory::ClearWriteTestFaultForTesting();
    const auto after = State(coherence, *resource);
    Check(!completed && after.needs_cpu_upload &&
              after.cpu_write_generation > before.cpu_write_generation &&
              coherence.AcknowledgeCpuUpload(*resource,
                                             after.cpu_write_generation) &&
              !State(coherence, *resource).needs_cpu_upload,
          name);
  };

  CheckLateFailure([&] { return memory.Write(kPartialBase, source); },
                   "late failed Write did not report its changed prefix");
  std::array<std::byte, kDestinationSize> output{};
  Check(memory.Read(kPartialBase, output) &&
            std::equal(output.begin(), output.begin() + kSegmentSize,
                       source.begin()) &&
            std::all_of(output.begin() + kSegmentSize, output.end(),
                        [](std::byte value) { return value == std::byte{0}; }),
        "late failed Write did not stop after its first changed segment");

  CheckLateFailure(
      [&] { return memory.Fill(kPartialBase, kDestinationSize, std::byte{0x7a}); },
      "late failed Fill did not report its changed prefix");
  CheckLateFailure(
      [&] {
        return memory.Copy(kPartialBase, kPartialBase + 0x10,
                           kDestinationSize);
      },
      "late failed Copy did not report its changed prefix");
  CheckLateFailure(
      [&] { return memory.Initialize(kPartialBase, source); },
      "late failed Initialize did not report its changed prefix");
  CheckLateFailure(
      [&] {
        return memory.InitializeFill(kPartialBase, kDestinationSize,
                                     std::byte{0x33});
      },
      "late failed InitializeFill did not report its changed prefix");

  const auto clean = State(coherence, *resource);
  Check(!memory.Write(kPartialBase + 0x20, source) &&
            !State(coherence, *resource).needs_cpu_upload &&
            State(coherence, *resource).cpu_write_generation ==
                clean.cpu_write_generation,
        "failure without a mutation dirtied the resource");
}

void TestSharedBackingAliasesAreCoherent() {
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;
  using kajps5::memory::GuestMemoryTestFaultPoint;
  using kajps5::memory::SharedMemoryBacking;

  constexpr auto kAliasBase = kBase + 0x3000;
  constexpr auto kAliasSize = std::uint64_t{8};
  constexpr auto kFirstAlias = kAliasBase;
  constexpr auto kSecondAlias = kAliasBase + 0x20;
  constexpr auto kCopySource = kAliasBase + 0x40;
  const auto protection = GuestMemoryProtection::kRead |
                          GuestMemoryProtection::kWrite;
  GuestMemory memory(kAliasBase, 0x80, GuestMemoryProtection::kNone);
  const auto backing = std::make_shared<SharedMemoryBacking>(0x10);
  Check(memory.MapShared(kFirstAlias, kAliasSize, protection, backing, 0) &&
            memory.MapShared(kSecondAlias, kAliasSize, protection, backing,
                             2) &&
            memory.Map(kCopySource, 4, protection),
        "shared backing aliases could not be mapped");

  const std::array<std::byte, 4> bytes = {
      std::byte{0x31}, std::byte{0x32}, std::byte{0x33}, std::byte{0x34}};
  Check(memory.Initialize(kCopySource, bytes),
        "shared-alias copy source could not be initialized");

  kajps5::gpu::GpuRuntime runtime(memory);
  auto& coherence = runtime.resource_coherence();
  const auto resource = coherence.RegisterResource(kSecondAlias + 2, 4);
  Check(resource.has_value(), "second shared alias resource was rejected");
  AcknowledgeInitialUpload(coherence, *resource);

  const auto AcknowledgeAliasWrite = [&](const char* message) {
    const auto state = State(coherence, *resource);
    Check(state.needs_cpu_upload &&
              coherence.AcknowledgeCpuUpload(*resource,
                                             state.cpu_write_generation) &&
              !State(coherence, *resource).needs_cpu_upload,
          message);
  };

  const auto clean = State(coherence, *resource);
  Check(memory.Write(kFirstAlias, bytes),
        "shared-alias boundary write failed");
  Check(!State(coherence, *resource).needs_cpu_upload &&
            State(coherence, *resource).cpu_write_generation ==
                clean.cpu_write_generation,
        "shared-alias boundary write dirtied an exact-boundary resource");

  const std::array<std::byte, 2> partial = {std::byte{0x61},
                                             std::byte{0x62}};
  Check(memory.Write(kFirstAlias + 3, partial),
        "shared-alias partial write failed");
  AcknowledgeAliasWrite(
      "shared-alias partial Write did not dirty the second alias");

  Check(memory.Fill(kFirstAlias + 4, 4, std::byte{0x7a}),
        "shared-alias Fill failed");
  AcknowledgeAliasWrite("shared-alias Fill did not dirty the second alias");

  Check(memory.Copy(kFirstAlias + 4, kCopySource, 4),
        "shared-alias forward Copy failed");
  AcknowledgeAliasWrite(
      "shared-alias forward Copy did not dirty the second alias");

  Check(memory.Copy(kFirstAlias + 4, kFirstAlias + 3, 4),
        "shared-alias backward Copy failed");
  AcknowledgeAliasWrite(
      "shared-alias backward Copy did not dirty the second alias");

  Check(memory.Initialize(kFirstAlias + 4, bytes),
        "shared-alias Initialize failed");
  AcknowledgeAliasWrite(
      "shared-alias Initialize did not dirty the second alias");

  Check(memory.InitializeFill(kFirstAlias + 4, 4, std::byte{0x44}),
        "shared-alias InitializeFill failed");
  AcknowledgeAliasWrite(
      "shared-alias InitializeFill did not dirty the second alias");

  GuestMemory::SetWriteTestFaultForTesting(
      GuestMemoryTestFaultPoint::kFailWriteOrFillAfterFirstChunk);
  const auto late_completed = memory.Write(kFirstAlias + 4, bytes);
  GuestMemory::ClearWriteTestFaultForTesting();
  Check(!late_completed,
        "shared-alias late-failure write unexpectedly completed");
  AcknowledgeAliasWrite(
      "shared-alias late failure did not report its changed alias range");
}

void TestOutOfOrderWriteNotificationsDoNotRegressResource() {
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryTestSynchronizationPoint;

  Fixture fixture;
  auto& coherence = fixture.runtime.resource_coherence();
  const std::array<std::byte, 1> older_bytes = {std::byte{0x71}};
  const std::array<std::byte, 1> newer_bytes = {std::byte{0x72}};

  GuestMemory::ArmTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint::kWriteObserver);
  bool older_completed = false;
  std::thread older([&] {
    older_completed = fixture.memory.Write(kResourceAddress, older_bytes);
  });
  const auto older_paused = GuestMemory::WaitForTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint::kWriteObserver, 1'000'000);
  if (!older_paused) {
    GuestMemory::ResumeTestSynchronizationForTesting(
        GuestMemoryTestSynchronizationPoint::kWriteObserver);
    older.join();
    Check(false, "older write callback did not reach the deterministic pause");
    return;
  }

  const auto resource =
      coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(resource.has_value(),
        "registration behind a paused write callback was rejected");
  Check(fixture.memory.Write(kResourceAddress + 1, newer_bytes),
        "newer write behind a paused callback failed");
  const auto newer = State(coherence, *resource);
  Check(newer.needs_cpu_upload &&
            coherence.AcknowledgeCpuUpload(*resource,
                                           newer.cpu_write_generation),
        "newer generation could not be acknowledged before old callback release");

  GuestMemory::ResumeTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint::kWriteObserver);
  older.join();
  const auto final = State(coherence, *resource);
  Check(older_completed && !final.needs_cpu_upload &&
            final.cpu_write_generation == newer.cpu_write_generation,
        "delayed older callback regressed an acknowledged newer generation");
}

void TestHostInitializersSerializeProtectionTransitions() {
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;
  using kajps5::memory::GuestMemoryTestSynchronizationPoint;

  const auto page_size = GuestMemory::HostMappingGranularity();
  if (page_size == 0) {
    return;
  }
  auto memory =
      GuestMemory::CreateHostMapped(page_size, GuestMemoryProtection::kNone);
  if (!memory) {
    return;
  }
  const auto base = memory->base_address();
  Check(memory->Map(base, page_size, GuestMemoryProtection::kRead),
        "read-only host page could not be mapped");

  const std::array<std::byte, 4> initialized = {
      std::byte{0x91}, std::byte{0x92}, std::byte{0x93}, std::byte{0x94}};
  GuestMemory::ArmTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint::kHostInitializeAfterWritable);
  bool first_completed = false;
  bool second_completed = false;
  std::thread first([&] {
    first_completed = memory->Initialize(base + 0x20, initialized);
  });
  const auto first_paused = GuestMemory::WaitForTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint::kHostInitializeAfterWritable,
      1'000'000);
  if (!first_paused) {
    GuestMemory::ResumeTestSynchronizationForTesting(
        GuestMemoryTestSynchronizationPoint::kHostInitializeAfterWritable);
    first.join();
    Check(false, "first host initializer did not reach the deterministic pause");
    return;
  }

  std::thread second([&] {
    second_completed =
        memory->InitializeFill(base + 0x40, 4, std::byte{0xa5});
  });
  bool second_attempted = false;
  for (std::size_t yield_count = 0; yield_count < 1'000'000; ++yield_count) {
    if (GuestMemory::HostInitializeLockAttemptsForTesting() >= 2) {
      second_attempted = true;
      break;
    }
    std::this_thread::yield();
  }
  const auto acquisitions_before_release =
      GuestMemory::HostInitializeLockAcquisitionsForTesting();
  GuestMemory::ResumeTestSynchronizationForTesting(
      GuestMemoryTestSynchronizationPoint::kHostInitializeAfterWritable);
  first.join();
  second.join();

  std::array<std::byte, 4> first_output{};
  std::array<std::byte, 4> second_output{};
  const auto region = memory->QueryRegion(base);
  Check(second_attempted && acquisitions_before_release == 1 &&
            first_completed && second_completed &&
            memory->Read(base + 0x20, first_output) &&
            memory->Read(base + 0x40, second_output) &&
            first_output == initialized &&
            std::all_of(second_output.begin(), second_output.end(),
                        [](std::byte value) { return value == std::byte{0xa5}; }) &&
            region.has_value() &&
            region->protection == GuestMemoryProtection::kRead &&
            !memory->Write(base + 0x20, initialized),
        "host initializers did not serialize protection restoration");
}

void TestGpuWriteStateIsExplicit() {
  Fixture fixture;
  auto& coherence = fixture.runtime.resource_coherence();
  const auto resource =
      coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(resource.has_value(), "GPU-state test resource was rejected");
  AcknowledgeInitialUpload(coherence, *resource);

  Check(coherence.MarkGpuWrite(*resource), "GPU write was rejected");
  const auto gpu_dirty = State(coherence, *resource);
  Check(gpu_dirty.gpu_write_pending && !gpu_dirty.needs_cpu_upload,
        "GPU write did not expose CPU-side stale state");

  const std::array<std::byte, 1> byte = {std::byte{0x5c}};
  Check(fixture.memory.Write(kResourceAddress, byte),
        "checked CPU write over GPU-dirty resource failed");
  const auto divergent = State(coherence, *resource);
  Check(divergent.gpu_write_pending && divergent.needs_cpu_upload,
        "CPU/GPU divergence was not explicit");
  Check(coherence.InvalidateGpuWrite(*resource) &&
            !State(coherence, *resource).gpu_write_pending &&
            State(coherence, *resource).needs_cpu_upload &&
            coherence.AcknowledgeCpuUpload(
                *resource, divergent.cpu_write_generation) &&
            !State(coherence, *resource).needs_cpu_upload,
        "explicit GPU invalidation did not preserve CPU upload state");
}

void TestInFlightOverlapQueryIsCheckedAndNonConsuming() {
  Fixture fixture;
  auto& coherence = fixture.runtime.resource_coherence();
  const auto resource = coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(resource.has_value(), "in-flight query fixture resource was rejected");
  Check(!coherence.HasGpuWritePendingOverlap(kResourceAddress, kResourceSize) &&
            !coherence.HasGpuWritePendingOverlap(kResourceAddress + kResourceSize,
                                                 1),
        "clean or exact-boundary resource reported an in-flight GPU write");
  Check(coherence.MarkGpuWrite(*resource) &&
            coherence.HasGpuWritePendingOverlap(kResourceAddress - 1, 2) &&
            coherence.HasGpuWritePendingOverlap(kResourceAddress + kResourceSize - 1,
                                                 2),
        "partial checked overlap did not find a pending GPU write");
  Check(coherence.HasGpuWritePendingOverlap(
            std::numeric_limits<std::uint64_t>::max(), 1),
        "overflowing overlap query did not fail closed");
  Check(coherence.InvalidateGpuWrite(*resource) &&
            !coherence.HasGpuWritePendingOverlap(kResourceAddress, kResourceSize),
        "in-flight query consumed or retained an invalidated GPU write");
}

void TestGpuOwnerCompositionFailureIsTransactional() {
  using kajps5::gpu::GpuRuntime;
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory memory(kBase, kMemorySize,
                     GuestMemoryProtection::kRead |
                         GuestMemoryProtection::kWrite);
  GpuRuntime owner(memory);
  bool rejected = false;
  try {
    GpuRuntime duplicate(memory);
  } catch (const std::logic_error&) {
    rejected = true;
  }
  Check(rejected, "second GPU owner was accepted for one GuestMemory");

  auto& coherence = owner.resource_coherence();
  const auto resource =
      coherence.RegisterResource(kResourceAddress, kResourceSize);
  Check(resource.has_value() && *resource == 1,
        "failed GPU owner construction changed the existing owner state");
  AcknowledgeInitialUpload(coherence, *resource);
}

class ReentrantObserver final : public kajps5::memory::GuestMemoryWriteObserver {
 public:
  explicit ReentrantObserver(kajps5::memory::GuestMemory& memory) noexcept
      : memory_(memory) {}

  void OnGuestMemoryWrite(
      const kajps5::memory::GuestMemoryWriteEvent& event) noexcept override {
    const auto snapshot = memory_.coherence_snapshot();
    const auto mapping_token =
        memory_.CaptureMappingToken(event.address, event.size);
    observed_sequence_.store(snapshot.write_generation.sequence,
                             std::memory_order_relaxed);
    observed_event_sequence_.store(event.generation.sequence,
                                   std::memory_order_relaxed);
    mapping_captured_.store(mapping_token.has_value() &&
                                !mapping_token->generation_exhausted,
                            std::memory_order_release);
    called_.store(true, std::memory_order_release);
  }

  [[nodiscard]] bool called() const noexcept {
    return called_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::uint64_t observed_sequence() const noexcept {
    return observed_sequence_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t observed_event_sequence() const noexcept {
    return observed_event_sequence_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool mapping_captured() const noexcept {
    return mapping_captured_.load(std::memory_order_acquire);
  }

 private:
  kajps5::memory::GuestMemory& memory_;
  std::atomic<bool> called_ = false;
  std::atomic<std::uint64_t> observed_sequence_ = 0;
  std::atomic<std::uint64_t> observed_event_sequence_ = 0;
  std::atomic<bool> mapping_captured_ = false;
};

void TestObserverIsUnlockedAndConcurrentWritesAreSafe() {
  using kajps5::memory::GuestMemory;
  using kajps5::memory::GuestMemoryProtection;

  GuestMemory observer_memory(kBase, kMemorySize,
                              GuestMemoryProtection::kRead |
                                  GuestMemoryProtection::kWrite);
  const auto observer = std::make_shared<ReentrantObserver>(observer_memory);
  Check(observer_memory.SetWriteObserver(observer),
        "write observer was not registered");
  const std::array<std::byte, 1> byte = {std::byte{0x42}};
  Check(observer_memory.Write(kBase, byte) && observer->called() &&
            observer->observed_sequence() ==
                observer->observed_event_sequence() &&
            observer->mapping_captured(),
        "write observer was not called after releasing GuestMemory locks");
  observer_memory.ClearWriteObserver(observer.get());

  Fixture fixture;
  auto& coherence = fixture.runtime.resource_coherence();
  const auto left = coherence.RegisterResource(kBase + 0x400, 0x40);
  const auto right = coherence.RegisterResource(kBase + 0x500, 0x40);
  Check(left.has_value() && right.has_value(),
        "concurrency test resources were rejected");
  AcknowledgeInitialUpload(coherence, *left);
  AcknowledgeInitialUpload(coherence, *right);
  const auto left_initial = State(coherence, *left).cpu_write_generation;
  const auto right_initial = State(coherence, *right).cpu_write_generation;

  std::atomic<bool> start = false;
  std::atomic<bool> write_failed = false;
  const auto writer = [&](std::uint64_t address, std::byte value) {
    const std::array<std::byte, 1> write = {value};
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (std::size_t index = 0; index < 128; ++index) {
      if (!fixture.memory.Write(address, write)) {
        write_failed.store(true, std::memory_order_release);
      }
    }
  };
  std::thread first(writer, kBase + 0x400, std::byte{0x10});
  std::thread second(writer, kBase + 0x500, std::byte{0x20});
  start.store(true, std::memory_order_release);
  first.join();
  second.join();

  const auto left_final = State(coherence, *left);
  const auto right_final = State(coherence, *right);
  Check(!write_failed.load(std::memory_order_acquire) &&
            left_final.needs_cpu_upload && right_final.needs_cpu_upload &&
            left_final.cpu_write_generation > left_initial &&
            right_final.cpu_write_generation > right_initial,
        "concurrent checked writes lost a coherence update");
}

void TestGenerationWrapPolicy() {
  using kajps5::memory::AdvanceGuestMemoryGeneration;
  using kajps5::memory::GuestMemoryGeneration;

  GuestMemoryGeneration rollover{
      .epoch = 7,
      .sequence = std::numeric_limits<std::uint64_t>::max(),
  };
  const auto before_rollover = rollover;
  Check(AdvanceGuestMemoryGeneration(rollover) && rollover.epoch == 8 &&
            rollover.sequence == 1 && rollover > before_rollover,
        "generation sequence rollover was not monotonic");

  GuestMemoryGeneration exhausted{
      .epoch = std::numeric_limits<std::uint64_t>::max(),
      .sequence = std::numeric_limits<std::uint64_t>::max(),
  };
  const auto before_exhaustion = exhausted;
  Check(!AdvanceGuestMemoryGeneration(exhausted) &&
            exhausted == before_exhaustion,
        "fully exhausted generation reused a stale value");
}

}  // namespace

int main() {
  TestCheckedWritesAndOverlapBoundaries();
  TestRangeFailureAndMappingLifetime();
  TestRangeLocalMappingTokens();
  TestLatePartialMutationFailureIsObserved();
  TestSharedBackingAliasesAreCoherent();
  TestOutOfOrderWriteNotificationsDoNotRegressResource();
  TestHostInitializersSerializeProtectionTransitions();
  TestGpuWriteStateIsExplicit();
  TestInFlightOverlapQueryIsCheckedAndNonConsuming();
  TestGpuOwnerCompositionFailureIsTransactional();
  TestObserverIsUnlockedAndConcurrentWritesAreSafe();
  TestGenerationWrapPolicy();
  return 0;
}
