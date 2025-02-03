// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(CPPGC_YOUNG_GENERATION)

#include <algorithm>
#include <memory>
#include <vector>

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/persistent.h"
#include "include/cppgc/testing.h"
#include "include/v8-context.h"
#include "include/v8-cppgc.h"
#include "include/v8-local-handle.h"
#include "include/v8-object.h"
#include "include/v8-traced-handle.h"
#include "src/api/api-inl.h"
#include "src/common/globals.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/objects/objects-inl.h"
#include "test/common/flag-utils.h"
#include "test/unittests/heap/cppgc-js/unified-heap-utils.h"
#include "test/unittests/heap/heap-utils.h"

namespace v8 {
namespace internal {

namespace {

bool IsHeapObjectYoung(void* obj) {
  return cppgc::internal::HeapObjectHeader::FromObject(obj).IsYoung();
}

bool IsHeapObjectOld(void* obj) { return !IsHeapObjectYoung(obj); }

class Wrappable final : public cppgc::GarbageCollected<Wrappable> {
 public:
  static size_t destructor_callcount;

  Wrappable() = default;
  Wrappable(v8::Isolate* isolate, v8::Local<v8::Object> local)
      : wrapper_(isolate, local) {}

  Wrappable(const Wrappable&) = default;
  Wrappable(Wrappable&&) = default;

  Wrappable& operator=(const Wrappable&) = default;
  Wrappable& operator=(Wrappable&&) = default;

  ~Wrappable() { destructor_callcount++; }

  void Trace(cppgc::Visitor* visitor) const { visitor->Trace(wrapper_); }

  void SetWrapper(v8::Isolate* isolate, v8::Local<v8::Object> wrapper) {
    wrapper_.Reset(isolate, wrapper);
  }

  TracedReference<v8::Object>& wrapper() { return wrapper_; }

 private:
  TracedReference<v8::Object> wrapper_;
};

size_t Wrappable::destructor_callcount = 0;

class MinorMSEnabler {
 public:
  MinorMSEnabler()
      : minor_ms_(&v8_flags.minor_ms, true),
        cppgc_young_generation_(&v8_flags.cppgc_young_generation, true) {}

 private:
  FlagScope<bool> minor_ms_;
  FlagScope<bool> cppgc_young_generation_;
};

class YoungWrapperCollector : public RootVisitor {
 public:
  using YoungWrappers = std::set<Address>;

  void VisitRootPointers(Root root, const char*, FullObjectSlot start,
                         FullObjectSlot end) override {
    for (FullObjectSlot p = start; p < end; ++p) {
      all_young_wrappers_.insert(*p.location());
    }
  }

  YoungWrappers get_wrappers() { return std::move(all_young_wrappers_); }

 private:
  YoungWrappers all_young_wrappers_;
};

class ExpectCppGCToV8GenerationalBarrierToFire {
 public:
  ExpectCppGCToV8GenerationalBarrierToFire(
      v8::Isolate& isolate, std::initializer_list<Address> expected_wrappers)
      : isolate_(reinterpret_cast<Isolate&>(isolate)),
        expected_wrappers_(expected_wrappers) {
    YoungWrapperCollector visitor;
    isolate_.traced_handles()->IterateYoungRootsWithOldHostsForTesting(
        &visitor);
    young_wrappers_before_ = visitor.get_wrappers();

    std::vector<Address> diff;
    std::set_intersection(young_wrappers_before_.begin(),
                          young_wrappers_before_.end(),
                          expected_wrappers_.begin(), expected_wrappers_.end(),
                          std::back_inserter(diff));
    EXPECT_TRUE(diff.empty());
  }

  ~ExpectCppGCToV8GenerationalBarrierToFire() {
    YoungWrapperCollector visitor;
    isolate_.traced_handles()->IterateYoungRootsWithOldHostsForTesting(
        &visitor);
    const auto young_wrappers_after = visitor.get_wrappers();
    EXPECT_GE(young_wrappers_after.size(), young_wrappers_before_.size());

    EXPECT_TRUE(
        std::includes(young_wrappers_after.begin(), young_wrappers_after.end(),
                      expected_wrappers_.begin(), expected_wrappers_.end()));
    EXPECT_EQ(expected_wrappers_.size(),
              young_wrappers_after.size() - young_wrappers_before_.size());
  }

 private:
  Isolate& isolate_;
  YoungWrapperCollector::YoungWrappers expected_wrappers_;
  YoungWrapperCollector::YoungWrappers young_wrappers_before_;
};

class ExpectCppGCToV8NoGenerationalBarrier {
 public:
  explicit ExpectCppGCToV8NoGenerationalBarrier(v8::Isolate& isolate)
      : isolate_(reinterpret_cast<Isolate&>(isolate)) {
    YoungWrapperCollector visitor;
    isolate_.traced_handles()->IterateYoungRootsWithOldHostsForTesting(
        &visitor);
    young_wrappers_before_ = visitor.get_wrappers();
  }

  ~ExpectCppGCToV8NoGenerationalBarrier() {
    YoungWrapperCollector visitor;
    isolate_.traced_handles()->IterateYoungRootsWithOldHostsForTesting(
        &visitor);
    const auto young_wrappers_after = visitor.get_wrappers();
    EXPECT_EQ(young_wrappers_before_, young_wrappers_after);
  }

 private:
  Isolate& isolate_;
  YoungWrapperCollector::YoungWrappers young_wrappers_before_;
};

}  // namespace

class YoungUnifiedHeapTest : public MinorMSEnabler, public UnifiedHeapTest {
 public:
  YoungUnifiedHeapTest() {
    // Enable young generation flag and run GC. After the first run the heap
    // will enable minor GC.
    CollectGarbageWithoutEmbedderStack();
  }
};

TEST_F(YoungUnifiedHeapTest, OnlyGC) { CollectYoungGarbageWithEmbedderStack(); }

TEST_F(YoungUnifiedHeapTest, CollectUnreachableCppGCObject) {
  cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());
  v8::Local<v8::Object> api_object =
      WrapperHelper::CreateWrapper(context(), nullptr);
  EXPECT_FALSE(api_object.IsEmpty());

  Wrappable::destructor_callcount = 0;
  CollectYoungGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(1u, Wrappable::destructor_callcount);
}

TEST_F(YoungUnifiedHeapTest, FindingV8ToCppGCReference) {
  auto* wrappable_object =
      cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());
  v8::Local<v8::Object> api_object =
      WrapperHelper::CreateWrapper(context(), wrappable_object);
  EXPECT_FALSE(api_object.IsEmpty());
  // With direct locals, api_object may be invalid after a stackless GC.
  auto handle_api_object = v8::Utils::OpenIndirectHandle(*api_object);

  Wrappable::destructor_callcount = 0;
  CollectYoungGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(0u, Wrappable::destructor_callcount);

  WrapperHelper::ResetWrappableConnection(
      v8_isolate(), v8::Utils::ToLocal(handle_api_object));
  CollectGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(1u, Wrappable::destructor_callcount);
}

TEST_F(YoungUnifiedHeapTest, FindingCppGCToV8Reference) {
  auto* wrappable_object =
      cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());

  {
    v8::HandleScope inner_handle_scope(v8_isolate());
    auto local = v8::Object::New(v8_isolate());
    EXPECT_TRUE(local->IsObject());
    wrappable_object->SetWrapper(v8_isolate(), local);
  }

  CollectYoungGarbageWithEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  auto local = wrappable_object->wrapper().Get(v8_isolate());
  EXPECT_TRUE(local->IsObject());
}

TEST_F(YoungUnifiedHeapTest, GenerationalBarrierV8ToCppGCReference) {
  if (i::v8_flags.single_generation) return;

  FlagScope<bool> no_incremental_marking(&v8_flags.incremental_marking, false);

  v8::Local<v8::Object> api_object =
      WrapperHelper::CreateWrapper(context(), nullptr);
  // With direct locals, api_object may be invalid after a stackless GC.
  auto handle_api_object = v8::Utils::OpenIndirectHandle(*api_object);

  EXPECT_TRUE(Heap::InYoungGeneration(*handle_api_object));
  InvokeMemoryReducingMajorGCs();
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
  EXPECT_FALSE(Heap::InYoungGeneration(*handle_api_object));

  auto* wrappable = cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());
  WrapperHelper::SetWrappableConnection(
      v8_isolate(), v8::Utils::ToLocal(handle_api_object), wrappable);

  Wrappable::destructor_callcount = 0;
  CollectYoungGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
}

TEST_F(YoungUnifiedHeapTest,
       GenerationalBarrierCppGCToV8NoInitializingStoreBarrier) {
  if (i::v8_flags.single_generation) return;

  FlagScope<bool> no_incremental_marking(&v8_flags.incremental_marking, false);

  auto local = v8::Object::New(v8_isolate());
  {
    ExpectCppGCToV8NoGenerationalBarrier expect_no_barrier(*v8_isolate());
    auto* wrappable = cppgc::MakeGarbageCollected<Wrappable>(
        allocation_handle(), v8_isolate(), local);
    auto* copied_wrappable =
        cppgc::MakeGarbageCollected<Wrappable>(allocation_handle(), *wrappable);
    auto* moved_wrappable = cppgc::MakeGarbageCollected<Wrappable>(
        allocation_handle(), std::move(*wrappable));
    USE(moved_wrappable);
    USE(copied_wrappable);
    USE(wrappable);
  }
}

TEST_F(YoungUnifiedHeapTest, GenerationalBarrierCppGCToV8ReferenceReset) {
  if (i::v8_flags.single_generation) return;

  FlagScope<bool> no_incremental_marking(&v8_flags.incremental_marking, false);

  cppgc::Persistent<Wrappable> wrappable_object =
      cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());

  EXPECT_TRUE(IsHeapObjectYoung(wrappable_object.Get()));
  InvokeMemoryReducingMajorGCs();
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
  EXPECT_TRUE(IsHeapObjectOld(wrappable_object.Get()));

  {
    v8::HandleScope inner_handle_scope(v8_isolate());
    auto local = v8::Object::New(v8_isolate());
    EXPECT_TRUE(local->IsObject());
    {
      ExpectCppGCToV8GenerationalBarrierToFire expect_barrier(
          *v8_isolate(), {i::ValueHelper::ValueAsAddress(*local)});
      wrappable_object->SetWrapper(v8_isolate(), local);
    }
  }

  CollectYoungGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  auto local = wrappable_object->wrapper().Get(v8_isolate());
  EXPECT_TRUE(local->IsObject());
}

TEST_F(YoungUnifiedHeapTest, GenerationalBarrierCppGCToV8ReferenceCopy) {
  if (i::v8_flags.single_generation) return;

  FlagScope<bool> no_incremental_marking(&v8_flags.incremental_marking, false);

  cppgc::Persistent<Wrappable> wrappable_object =
      cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());

  EXPECT_TRUE(IsHeapObjectYoung(wrappable_object.Get()));
  InvokeMemoryReducingMajorGCs();
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
  EXPECT_TRUE(IsHeapObjectOld(wrappable_object.Get()));

  {
    v8::HandleScope inner_handle_scope(v8_isolate());
    auto local = v8::Object::New(v8_isolate());
    EXPECT_TRUE(local->IsObject());

    Wrappable* another_wrappable_object = nullptr;
    {
      // Assign to young host and expect no barrier.
      ExpectCppGCToV8NoGenerationalBarrier expect_no_barrier(*v8_isolate());
      another_wrappable_object =
          cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());
      another_wrappable_object->SetWrapper(v8_isolate(), local);
    }
    {
      // Assign to old object using TracedReference::operator= and expect
      // the barrier to trigger.
      ExpectCppGCToV8GenerationalBarrierToFire expect_barrier(
          *v8_isolate(), {i::ValueHelper::ValueAsAddress(*local)});
      *wrappable_object = *another_wrappable_object;
    }
  }

  CollectYoungGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  auto local = wrappable_object->wrapper().Get(v8_isolate());
  EXPECT_TRUE(local->IsObject());
}

TEST_F(YoungUnifiedHeapTest, GenerationalBarrierCppGCToV8ReferenceMove) {
  if (i::v8_flags.single_generation) return;

  FlagScope<bool> no_incremental_marking(&v8_flags.incremental_marking, false);

  cppgc::Persistent<Wrappable> wrappable_object =
      cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());

  EXPECT_TRUE(IsHeapObjectYoung(wrappable_object.Get()));
  InvokeMemoryReducingMajorGCs();
  EXPECT_EQ(0u, Wrappable::destructor_callcount);
  EXPECT_TRUE(IsHeapObjectOld(wrappable_object.Get()));

  {
    v8::HandleScope inner_handle_scope(v8_isolate());
    auto local = v8::Object::New(v8_isolate());
    EXPECT_TRUE(local->IsObject());

    Wrappable* another_wrappable_object = nullptr;
    {
      // Assign to young host and expect no barrier.
      ExpectCppGCToV8NoGenerationalBarrier expect_no_barrier(*v8_isolate());
      another_wrappable_object =
          cppgc::MakeGarbageCollected<Wrappable>(allocation_handle());
      another_wrappable_object->SetWrapper(v8_isolate(), local);
    }
    {
      // Assign to old object using TracedReference::operator= and expect
      // the barrier to trigger.
      ExpectCppGCToV8GenerationalBarrierToFire expect_barrier(
          *v8_isolate(), {i::ValueHelper::ValueAsAddress(*local)});
      *wrappable_object = std::move(*another_wrappable_object);
    }
  }

  CollectYoungGarbageWithoutEmbedderStack(cppgc::Heap::SweepingType::kAtomic);
  auto local = wrappable_object->wrapper().Get(v8_isolate());
  EXPECT_TRUE(local->IsObject());
}

}  // namespace internal
}  // namespace v8

#endif  // defined(CPPGC_YOUNG_GENERATION)
