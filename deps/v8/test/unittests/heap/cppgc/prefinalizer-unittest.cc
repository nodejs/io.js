// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/prefinalizer.h"

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/persistent.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class PrefinalizerTest : public testing::TestWithHeap {};

class GCed : public GarbageCollected<GCed> {
  CPPGC_USING_PRE_FINALIZER(GCed, PreFinalizer);

 public:
  void Trace(Visitor*) const {}
  void PreFinalizer() { ++prefinalizer_callcount; }

  static size_t prefinalizer_callcount;
};
size_t GCed::prefinalizer_callcount = 0;

}  // namespace

TEST_F(PrefinalizerTest, PrefinalizerCalledOnDeadObject) {
  GCed::prefinalizer_callcount = 0;
  auto* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  USE(object);
  EXPECT_EQ(0u, GCed::prefinalizer_callcount);
  PreciseGC();
  EXPECT_EQ(1u, GCed::prefinalizer_callcount);
  PreciseGC();
  EXPECT_EQ(1u, GCed::prefinalizer_callcount);
}

TEST_F(PrefinalizerTest, PrefinalizerNotCalledOnLiveObject) {
  GCed::prefinalizer_callcount = 0;
  {
    Persistent<GCed> object = MakeGarbageCollected<GCed>(GetAllocationHandle());
    EXPECT_EQ(0u, GCed::prefinalizer_callcount);
    PreciseGC();
    EXPECT_EQ(0u, GCed::prefinalizer_callcount);
  }
  PreciseGC();
  EXPECT_EQ(1u, GCed::prefinalizer_callcount);
}

namespace {

class Mixin : public GarbageCollectedMixin {
  CPPGC_USING_PRE_FINALIZER(Mixin, PreFinalizer);

 public:
  void PreFinalizer() { ++prefinalizer_callcount; }

  static size_t prefinalizer_callcount;
};
size_t Mixin::prefinalizer_callcount = 0;

class GCedWithMixin : public GarbageCollected<GCedWithMixin>, public Mixin {};

}  // namespace

TEST_F(PrefinalizerTest, PrefinalizerCalledOnDeadMixinObject) {
  Mixin::prefinalizer_callcount = 0;
  auto* object = MakeGarbageCollected<GCedWithMixin>(GetAllocationHandle());
  USE(object);
  EXPECT_EQ(0u, Mixin::prefinalizer_callcount);
  PreciseGC();
  EXPECT_EQ(1u, Mixin::prefinalizer_callcount);
  PreciseGC();
  EXPECT_EQ(1u, Mixin::prefinalizer_callcount);
}

TEST_F(PrefinalizerTest, PrefinalizerNotCalledOnLiveMixinObject) {
  Mixin::prefinalizer_callcount = 0;
  {
    Persistent<GCedWithMixin> object =
        MakeGarbageCollected<GCedWithMixin>(GetAllocationHandle());
    EXPECT_EQ(0u, Mixin::prefinalizer_callcount);
    PreciseGC();
    EXPECT_EQ(0u, Mixin::prefinalizer_callcount);
  }
  PreciseGC();
  EXPECT_EQ(1u, Mixin::prefinalizer_callcount);
}

namespace {

class BaseMixin : public GarbageCollectedMixin {
  CPPGC_USING_PRE_FINALIZER(BaseMixin, PreFinalizer);

 public:
  void PreFinalizer();

  static size_t prefinalizer_callcount;
};
size_t BaseMixin::prefinalizer_callcount = 0;

class InheritingMixin : public BaseMixin {
  CPPGC_USING_PRE_FINALIZER(InheritingMixin, PreFinalizer);

 public:
  void PreFinalizer();

  static size_t prefinalizer_callcount;
};
size_t InheritingMixin::prefinalizer_callcount = 0;

class GCedWithMixins : public GarbageCollected<GCedWithMixins>,
                       public InheritingMixin {
  CPPGC_USING_PRE_FINALIZER(GCedWithMixins, PreFinalizer);

 public:
  void PreFinalizer();

  static size_t prefinalizer_callcount;
};
size_t GCedWithMixins::prefinalizer_callcount = 0;

void BaseMixin::PreFinalizer() {
  EXPECT_EQ(1u, GCedWithMixins::prefinalizer_callcount);
  EXPECT_EQ(1u, InheritingMixin::prefinalizer_callcount);
  EXPECT_EQ(0u, BaseMixin::prefinalizer_callcount);
  ++BaseMixin::prefinalizer_callcount;
}

void InheritingMixin::PreFinalizer() {
  EXPECT_EQ(1u, GCedWithMixins::prefinalizer_callcount);
  EXPECT_EQ(0u, InheritingMixin::prefinalizer_callcount);
  EXPECT_EQ(0u, BaseMixin::prefinalizer_callcount);
  InheritingMixin::prefinalizer_callcount = true;
}

void GCedWithMixins::PreFinalizer() {
  EXPECT_EQ(0u, GCedWithMixins::prefinalizer_callcount);
  EXPECT_EQ(0u, InheritingMixin::prefinalizer_callcount);
  EXPECT_EQ(0u, BaseMixin::prefinalizer_callcount);
  GCedWithMixins::prefinalizer_callcount = true;
}
}  // namespace

TEST_F(PrefinalizerTest, PrefinalizerInvocationPreservesOrder) {
  BaseMixin::prefinalizer_callcount = 0;
  InheritingMixin::prefinalizer_callcount = 0;
  GCedWithMixins::prefinalizer_callcount = 0;
  auto* object = MakeGarbageCollected<GCedWithMixins>(GetAllocationHandle());
  USE(object);
  EXPECT_EQ(0u, GCedWithMixins::prefinalizer_callcount);
  EXPECT_EQ(0u, InheritingMixin::prefinalizer_callcount);
  EXPECT_EQ(0u, BaseMixin::prefinalizer_callcount);
  PreciseGC();
  EXPECT_EQ(1u, GCedWithMixins::prefinalizer_callcount);
  EXPECT_EQ(1u, InheritingMixin::prefinalizer_callcount);
  EXPECT_EQ(1u, BaseMixin::prefinalizer_callcount);
  PreciseGC();
  EXPECT_EQ(1u, GCedWithMixins::prefinalizer_callcount);
  EXPECT_EQ(1u, InheritingMixin::prefinalizer_callcount);
  EXPECT_EQ(1u, BaseMixin::prefinalizer_callcount);
}

namespace {

class LinkedNode final : public GarbageCollected<LinkedNode> {
 public:
  explicit LinkedNode(LinkedNode* next) : next_(next) {}

  void Trace(Visitor* visitor) const { visitor->Trace(next_); }

  LinkedNode* next() const { return next_; }

  void RemoveNext() {
    CHECK(next_);
    next_ = next_->next_;
  }

 private:
  Member<LinkedNode> next_;
};

class MutatingPrefinalizer final
    : public GarbageCollected<MutatingPrefinalizer> {
  CPPGC_USING_PRE_FINALIZER(MutatingPrefinalizer, PreFinalizer);

 public:
  void PreFinalizer() {
    // Pre-finalizers are generally used to mutate the object graph. The API
    // does not allow distinguishing between live and dead objects. It is
    // generally safe to re-write the dead *or* the live object graph. Adding
    // a dead object to the live graph must not happen.
    //
    // RemoveNext() must not trigger a write barrier. In the case all LinkedNode
    // objects die at the same time, the graph is mutated with a dead object.
    // This is only safe when the dead object is added to a dead subgraph.
    parent_node_->RemoveNext();
  }

  explicit MutatingPrefinalizer(LinkedNode* parent) : parent_node_(parent) {}

  void Trace(Visitor* visitor) const { visitor->Trace(parent_node_); }

 private:
  Member<LinkedNode> parent_node_;
};

}  // namespace

TEST_F(PrefinalizerTest, PrefinalizerCanRewireGraphWithLiveObjects) {
  Persistent<LinkedNode> root{MakeGarbageCollected<LinkedNode>(
      GetAllocationHandle(),
      MakeGarbageCollected<LinkedNode>(
          GetAllocationHandle(),
          MakeGarbageCollected<LinkedNode>(GetAllocationHandle(), nullptr)))};
  CHECK(root->next());
  MakeGarbageCollected<MutatingPrefinalizer>(GetAllocationHandle(), root.Get());
  PreciseGC();
}

namespace {

class PrefinalizerDeathTest : public testing::TestWithHeap {};

class AllocatingPrefinalizer : public GarbageCollected<AllocatingPrefinalizer> {
  CPPGC_USING_PRE_FINALIZER(AllocatingPrefinalizer, PreFinalizer);

 public:
  explicit AllocatingPrefinalizer(cppgc::Heap* heap) : heap_(heap) {}
  void Trace(Visitor*) const {}
  void PreFinalizer() {
    MakeGarbageCollected<GCed>(heap_->GetAllocationHandle());
  }

 private:
  cppgc::Heap* heap_;
};

}  // namespace

#ifdef CPPGC_ALLOW_ALLOCATIONS_IN_PREFINALIZERS
TEST_F(PrefinalizerTest, PrefinalizerDoesNotFailOnAllcoation) {
  auto* object = MakeGarbageCollected<AllocatingPrefinalizer>(
      GetAllocationHandle(), GetHeap());
  PreciseGC();
  USE(object);
}
#else
#ifdef DEBUG
TEST_F(PrefinalizerDeathTest, PrefinalizerFailsOnAllcoation) {
  auto* object = MakeGarbageCollected<AllocatingPrefinalizer>(
      GetAllocationHandle(), GetHeap());
  USE(object);
  EXPECT_DEATH_IF_SUPPORTED(PreciseGC(), "");
}
#endif  // DEBUG
#endif  // CPPGC_ALLOW_ALLOCATIONS_IN_PREFINALIZERS

namespace {

template <template <typename T> class RefType>
class RessurectingPrefinalizer
    : public GarbageCollected<RessurectingPrefinalizer<RefType>> {
  CPPGC_USING_PRE_FINALIZER(RessurectingPrefinalizer, PreFinalizer);

 public:
  explicit RessurectingPrefinalizer(RefType<GCed>& ref, GCed* obj)
      : ref_(ref), obj_(obj) {}
  void Trace(Visitor*) const {}
  void PreFinalizer() { ref_ = obj_; }

 private:
  RefType<GCed>& ref_;
  GCed* obj_;
};

class GCedHolder : public GarbageCollected<GCedHolder> {
 public:
  void Trace(Visitor* v) const { v->Trace(member_); }

  Member<GCed> member_;
};

}  // namespace

#if DEBUG
#ifdef CPPGC_VERIFY_HEAP

TEST_F(PrefinalizerDeathTest, PrefinalizerCanRewireGraphWithDeadObjects) {
  // Prefinalizers are allowed to rewire dead object to dead objects as that
  // doesn't affect the live object graph.
  Persistent<LinkedNode> root{MakeGarbageCollected<LinkedNode>(
      GetAllocationHandle(),
      MakeGarbageCollected<LinkedNode>(
          GetAllocationHandle(),
          MakeGarbageCollected<LinkedNode>(GetAllocationHandle(), nullptr)))};
  CHECK(root->next());
  MakeGarbageCollected<MutatingPrefinalizer>(GetAllocationHandle(), root.Get());
  // All LinkedNode objects will die on the following GC. The pre-finalizer may
  // still operate with them but not add them to a live object.
  root.Clear();
  PreciseGC();
}

#ifdef CPPGC_ENABLE_SLOW_API_CHECKS

TEST_F(PrefinalizerDeathTest, PrefinalizerCantRessurectObjectOnStack) {
  Persistent<GCed> persistent;
  MakeGarbageCollected<RessurectingPrefinalizer<Persistent>>(
      GetAllocationHandle(), persistent,
      MakeGarbageCollected<GCed>(GetAllocationHandle()));
  EXPECT_DEATH_IF_SUPPORTED(PreciseGC(), "");
}

#endif  // CPPGC_ENABLE_SLOW_API_CHECKS

TEST_F(PrefinalizerDeathTest, PrefinalizerCantRessurectObjectOnHeap) {
  Persistent<GCedHolder> persistent(
      MakeGarbageCollected<GCedHolder>(GetAllocationHandle()));
  MakeGarbageCollected<RessurectingPrefinalizer<Member>>(
      GetAllocationHandle(), persistent->member_,
      MakeGarbageCollected<GCed>(GetAllocationHandle()));
  EXPECT_DEATH_IF_SUPPORTED(PreciseGC(), "");
}

#endif  // CPPGC_VERIFY_HEAP
#endif  // DEBUG

#ifdef CPPGC_ALLOW_ALLOCATIONS_IN_PREFINALIZERS
TEST_F(PrefinalizerTest, AllocatingPrefinalizersInMultipleGCCycles) {
  auto* object = MakeGarbageCollected<AllocatingPrefinalizer>(
      GetAllocationHandle(), GetHeap());
  PreciseGC();
  auto* other_object = MakeGarbageCollected<AllocatingPrefinalizer>(
      GetAllocationHandle(), GetHeap());
  PreciseGC();
  USE(object);
  USE(other_object);
}
#endif

class GCedBase : public GarbageCollected<GCedBase> {
  CPPGC_USING_PRE_FINALIZER(GCedBase, PreFinalize);

 public:
  void Trace(Visitor*) const {}
  virtual void PreFinalize() { ++prefinalizer_count_; }
  static size_t prefinalizer_count_;
};
size_t GCedBase::prefinalizer_count_ = 0u;

class GCedInherited : public GCedBase {
 public:
  void PreFinalize() override { ++prefinalizer_count_; }
  static size_t prefinalizer_count_;
};
size_t GCedInherited::prefinalizer_count_ = 0u;

TEST_F(PrefinalizerTest, VirtualPrefinalizer) {
  MakeGarbageCollected<GCedInherited>(GetAllocationHandle());
  GCedBase::prefinalizer_count_ = 0u;
  GCedInherited::prefinalizer_count_ = 0u;
  PreciseGC();
  EXPECT_EQ(0u, GCedBase::prefinalizer_count_);
  EXPECT_LT(0u, GCedInherited::prefinalizer_count_);
}

}  // namespace internal
}  // namespace cppgc
