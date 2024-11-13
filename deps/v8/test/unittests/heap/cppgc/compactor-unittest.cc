// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/compactor.h"

#include "include/cppgc/allocation.h"
#include "include/cppgc/custom-space.h"
#include "include/cppgc/persistent.h"
#include "src/heap/cppgc/garbage-collector.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/stats-collector.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {

class CompactableCustomSpace : public CustomSpace<CompactableCustomSpace> {
 public:
  static constexpr size_t kSpaceIndex = 0;
  static constexpr bool kSupportsCompaction = true;
};

namespace internal {

namespace {

struct CompactableGCed : public GarbageCollected<CompactableGCed> {
 public:
  ~CompactableGCed() { ++g_destructor_callcount; }
  void Trace(Visitor* visitor) const {
    VisitorBase::TraceRawForTesting(visitor,
                                    const_cast<const CompactableGCed*>(other));
    visitor->RegisterMovableReference(
        const_cast<const CompactableGCed**>(&other));
  }
  static size_t g_destructor_callcount;
  CompactableGCed* other = nullptr;
  size_t id = 0;
};
// static
size_t CompactableGCed::g_destructor_callcount = 0;

template <int kNumObjects>
struct CompactableHolder
    : public GarbageCollected<CompactableHolder<kNumObjects>> {
 public:
  explicit CompactableHolder(cppgc::AllocationHandle& allocation_handle) {
    for (int i = 0; i < kNumObjects; ++i)
      objects[i] = MakeGarbageCollected<CompactableGCed>(allocation_handle);
  }

  void Trace(Visitor* visitor) const {
    for (int i = 0; i < kNumObjects; ++i) {
      VisitorBase::TraceRawForTesting(
          visitor, const_cast<const CompactableGCed*>(objects[i]));
      visitor->RegisterMovableReference(
          const_cast<const CompactableGCed**>(&objects[i]));
    }
  }
  CompactableGCed* objects[kNumObjects]{};
};

class CompactorTest : public testing::TestWithPlatform {
 public:
  CompactorTest() {
    Heap::HeapOptions options;
    options.custom_spaces.emplace_back(
        std::make_unique<CompactableCustomSpace>());
    heap_ = Heap::Create(platform_, std::move(options));
  }

  void StartCompaction() {
    compactor().EnableForNextGCForTesting();
    compactor().InitializeIfShouldCompact(GCConfig::MarkingType::kIncremental,
                                          StackState::kNoHeapPointers);
    EXPECT_TRUE(compactor().IsEnabledForTesting());
  }

  void FinishCompaction() { compactor().CompactSpacesIfEnabled(); }

  void StartGC() {
    CompactableGCed::g_destructor_callcount = 0u;
    StartCompaction();
    heap()->StartIncrementalGarbageCollection(
        GCConfig::PreciseIncrementalConfig());
  }

  void EndGC() {
    heap()->marker()->FinishMarking(StackState::kNoHeapPointers);
    heap()->GetMarkerRefForTesting().reset();
    FinishCompaction();
    // Sweeping also verifies the object start bitmap.
    const SweepingConfig sweeping_config{
        SweepingConfig::SweepingType::kAtomic,
        SweepingConfig::CompactableSpaceHandling::kIgnore};
    heap()->sweeper().Start(sweeping_config);
    heap()->sweeper().FinishIfRunning();
  }

  Heap* heap() { return Heap::From(heap_.get()); }
  cppgc::AllocationHandle& GetAllocationHandle() {
    return heap_->GetAllocationHandle();
  }
  Compactor& compactor() { return heap()->compactor(); }

 private:
  std::unique_ptr<cppgc::Heap> heap_;
};

}  // namespace

}  // namespace internal

template <>
struct SpaceTrait<internal::CompactableGCed> {
  using Space = CompactableCustomSpace;
};

namespace internal {

TEST_F(CompactorTest, NothingToCompact) {
  StartCompaction();
  heap()->stats_collector()->NotifyMarkingStarted(
      CollectionType::kMajor, GCConfig::MarkingType::kAtomic,
      GCConfig::IsForcedGC::kNotForced);
  heap()->stats_collector()->NotifyMarkingCompleted(0);
  FinishCompaction();
  heap()->stats_collector()->NotifySweepingCompleted(
      GCConfig::SweepingType::kAtomic);
}

TEST_F(CompactorTest, NonEmptySpaceAllLive) {
  static constexpr int kNumObjects = 10;
  Persistent<CompactableHolder<kNumObjects>> holder =
      MakeGarbageCollected<CompactableHolder<kNumObjects>>(
          GetAllocationHandle(), GetAllocationHandle());
  CompactableGCed* references[kNumObjects] = {nullptr};
  for (int i = 0; i < kNumObjects; ++i) {
    references[i] = holder->objects[i];
  }
  StartGC();
  EndGC();
  EXPECT_EQ(0u, CompactableGCed::g_destructor_callcount);
  for (int i = 0; i < kNumObjects; ++i) {
    EXPECT_EQ(holder->objects[i], references[i]);
  }
}

TEST_F(CompactorTest, NonEmptySpaceAllDead) {
  static constexpr int kNumObjects = 10;
  Persistent<CompactableHolder<kNumObjects>> holder =
      MakeGarbageCollected<CompactableHolder<kNumObjects>>(
          GetAllocationHandle(), GetAllocationHandle());
  CompactableGCed::g_destructor_callcount = 0u;
  StartGC();
  for (int i = 0; i < kNumObjects; ++i) {
    holder->objects[i] = nullptr;
  }
  EndGC();
  EXPECT_EQ(10u, CompactableGCed::g_destructor_callcount);
}

TEST_F(CompactorTest, NonEmptySpaceHalfLive) {
  static constexpr int kNumObjects = 10;
  Persistent<CompactableHolder<kNumObjects>> holder =
      MakeGarbageCollected<CompactableHolder<kNumObjects>>(
          GetAllocationHandle(), GetAllocationHandle());
  CompactableGCed* references[kNumObjects] = {nullptr};
  for (int i = 0; i < kNumObjects; ++i) {
    references[i] = holder->objects[i];
  }
  StartGC();
  for (int i = 0; i < kNumObjects; i += 2) {
    holder->objects[i] = nullptr;
  }
  EndGC();
  // Half of object were destroyed.
  EXPECT_EQ(5u, CompactableGCed::g_destructor_callcount);
  // Remaining objects are compacted.
  for (int i = 1; i < kNumObjects; i += 2) {
    EXPECT_EQ(holder->objects[i], references[i / 2]);
  }
}

TEST_F(CompactorTest, CompactAcrossPages) {
  Persistent<CompactableHolder<1>> holder =
      MakeGarbageCollected<CompactableHolder<1>>(GetAllocationHandle(),
                                                 GetAllocationHandle());
  CompactableGCed* reference = holder->objects[0];
  static constexpr size_t kObjectsPerPage =
      kPageSize / (sizeof(CompactableGCed) + sizeof(HeapObjectHeader));
  for (size_t i = 0; i < kObjectsPerPage; ++i) {
    holder->objects[0] =
        MakeGarbageCollected<CompactableGCed>(GetAllocationHandle());
  }
  // Last allocated object should be on a new page.
  EXPECT_NE(reference, holder->objects[0]);
  EXPECT_NE(BasePage::FromInnerAddress(heap(), reference),
            BasePage::FromInnerAddress(heap(), holder->objects[0]));
  StartGC();
  EndGC();
  // Half of object were destroyed.
  EXPECT_EQ(kObjectsPerPage, CompactableGCed::g_destructor_callcount);
  EXPECT_EQ(reference, holder->objects[0]);
}

TEST_F(CompactorTest, InteriorSlotToPreviousObject) {
  static constexpr int kNumObjects = 3;
  Persistent<CompactableHolder<kNumObjects>> holder =
      MakeGarbageCollected<CompactableHolder<kNumObjects>>(
          GetAllocationHandle(), GetAllocationHandle());
  CompactableGCed* references[kNumObjects] = {nullptr};
  for (int i = 0; i < kNumObjects; ++i) {
    references[i] = holder->objects[i];
  }
  holder->objects[2]->other = holder->objects[1];
  holder->objects[1] = nullptr;
  holder->objects[0] = nullptr;
  StartGC();
  EndGC();
  EXPECT_EQ(1u, CompactableGCed::g_destructor_callcount);
  EXPECT_EQ(references[1], holder->objects[2]);
  EXPECT_EQ(references[0], holder->objects[2]->other);
}

TEST_F(CompactorTest, InteriorSlotToNextObject) {
  static constexpr int kNumObjects = 3;
  Persistent<CompactableHolder<kNumObjects>> holder =
      MakeGarbageCollected<CompactableHolder<kNumObjects>>(
          GetAllocationHandle(), GetAllocationHandle());
  CompactableGCed* references[kNumObjects] = {nullptr};
  for (int i = 0; i < kNumObjects; ++i) {
    references[i] = holder->objects[i];
  }
  holder->objects[1]->other = holder->objects[2];
  holder->objects[2] = nullptr;
  holder->objects[0] = nullptr;
  StartGC();
  EndGC();
  EXPECT_EQ(1u, CompactableGCed::g_destructor_callcount);
  EXPECT_EQ(references[0], holder->objects[1]);
  EXPECT_EQ(references[1], holder->objects[1]->other);
}

TEST_F(CompactorTest, OnStackSlotShouldBeFiltered) {
  StartGC();
  const CompactableGCed* compactable_object =
      MakeGarbageCollected<CompactableGCed>(GetAllocationHandle());
  heap()->marker()->Visitor().RegisterMovableReference(&compactable_object);
  EndGC();
}

}  // namespace internal
}  // namespace cppgc
