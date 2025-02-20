// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/garbage-collected.h"

#include "include/cppgc/allocation.h"
#include "include/cppgc/type-traits.h"
#include "src/base/platform/mutex.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class GCed : public GarbageCollected<GCed> {
 public:
  void Trace(Visitor*) const {}
};
class NotGCed {};
class Mixin : public GarbageCollectedMixin {};
class GCedWithMixin : public GarbageCollected<GCedWithMixin>, public Mixin {};
class OtherMixin : public GarbageCollectedMixin {};
class MergedMixins : public Mixin, public OtherMixin {
 public:
  void Trace(cppgc::Visitor* visitor) const override {
    Mixin::Trace(visitor);
    OtherMixin::Trace(visitor);
  }
};
class GCWithMergedMixins : public GCed, public MergedMixins {
 public:
  void Trace(cppgc::Visitor* visitor) const override {
    MergedMixins::Trace(visitor);
  }
};

class GarbageCollectedTestWithHeap
    : public testing::TestSupportingAllocationOnly {};

}  // namespace

TEST(GarbageCollectedTest, GarbageCollectedTrait) {
  static_assert(!IsGarbageCollectedTypeV<int>);
  static_assert(!IsGarbageCollectedTypeV<NotGCed>);
  static_assert(IsGarbageCollectedTypeV<GCed>);
  static_assert(!IsGarbageCollectedTypeV<Mixin>);
  static_assert(IsGarbageCollectedTypeV<GCedWithMixin>);
  static_assert(!IsGarbageCollectedTypeV<MergedMixins>);
  static_assert(IsGarbageCollectedTypeV<GCWithMergedMixins>);
}

TEST(GarbageCollectedTest, GarbageCollectedMixinTrait) {
  static_assert(!IsGarbageCollectedMixinTypeV<int>);
  static_assert(!IsGarbageCollectedMixinTypeV<GCed>);
  static_assert(!IsGarbageCollectedMixinTypeV<NotGCed>);
  static_assert(IsGarbageCollectedMixinTypeV<Mixin>);
  static_assert(!IsGarbageCollectedMixinTypeV<GCedWithMixin>);
  static_assert(IsGarbageCollectedMixinTypeV<MergedMixins>);
  static_assert(!IsGarbageCollectedMixinTypeV<GCWithMergedMixins>);
}

TEST(GarbageCollectedTest, GarbageCollectedOrMixinTrait) {
  static_assert(!IsGarbageCollectedOrMixinTypeV<int>);
  static_assert(IsGarbageCollectedOrMixinTypeV<GCed>);
  static_assert(!IsGarbageCollectedOrMixinTypeV<NotGCed>);
  static_assert(IsGarbageCollectedOrMixinTypeV<Mixin>);
  static_assert(IsGarbageCollectedOrMixinTypeV<GCedWithMixin>);
  static_assert(IsGarbageCollectedOrMixinTypeV<MergedMixins>);
  static_assert(IsGarbageCollectedOrMixinTypeV<GCWithMergedMixins>);
}

TEST(GarbageCollectedTest, GarbageCollectedWithMixinTrait) {
  static_assert(!IsGarbageCollectedWithMixinTypeV<int>);
  static_assert(!IsGarbageCollectedWithMixinTypeV<GCed>);
  static_assert(!IsGarbageCollectedWithMixinTypeV<NotGCed>);
  static_assert(!IsGarbageCollectedWithMixinTypeV<Mixin>);
  static_assert(IsGarbageCollectedWithMixinTypeV<GCedWithMixin>);
  static_assert(!IsGarbageCollectedWithMixinTypeV<MergedMixins>);
  static_assert(IsGarbageCollectedWithMixinTypeV<GCWithMergedMixins>);
}

namespace {

class ForwardDeclaredType;

}  // namespace

TEST(GarbageCollectedTest, CompleteTypeTrait) {
  static_assert(IsCompleteV<GCed>);
  static_assert(!IsCompleteV<ForwardDeclaredType>);
}

TEST_F(GarbageCollectedTestWithHeap, GetObjectStartReturnsCurrentAddress) {
  GCed* gced = MakeGarbageCollected<GCed>(GetAllocationHandle());
  GCedWithMixin* gced_with_mixin =
      MakeGarbageCollected<GCedWithMixin>(GetAllocationHandle());
  const void* base_object_payload = TraceTrait<Mixin>::GetTraceDescriptor(
                                        static_cast<Mixin*>(gced_with_mixin))
                                        .base_object_payload;
  EXPECT_EQ(gced_with_mixin, base_object_payload);
  EXPECT_NE(gced, base_object_payload);
}

namespace {

class GCedWithPostConstructionCallback final : public GCed {
 public:
  static size_t cb_callcount;
  GCedWithPostConstructionCallback() { cb_callcount = 0; }
};
size_t GCedWithPostConstructionCallback::cb_callcount;

class MixinWithPostConstructionCallback {
 public:
  static size_t cb_callcount;
  MixinWithPostConstructionCallback() { cb_callcount = 0; }
  using MarkerForMixinWithPostConstructionCallback = int;
};
size_t MixinWithPostConstructionCallback::cb_callcount;

class GCedWithMixinWithPostConstructionCallback final
    : public GCed,
      public MixinWithPostConstructionCallback {};

}  // namespace
}  // namespace internal

template <>
struct PostConstructionCallbackTrait<
    internal::GCedWithPostConstructionCallback> {
  static void Call(internal::GCedWithPostConstructionCallback* object) {
    EXPECT_FALSE(
        internal::HeapObjectHeader::FromObject(object).IsInConstruction());
    internal::GCedWithPostConstructionCallback::cb_callcount++;
  }
};

template <typename T>
struct PostConstructionCallbackTrait<
    T, std::void_t<typename T::MarkerForMixinWithPostConstructionCallback>> {
  // The parameter could just be T*.
  static void Call(
      internal::GCedWithMixinWithPostConstructionCallback* object) {
    EXPECT_FALSE(
        internal::HeapObjectHeader::FromObject(object).IsInConstruction());
    internal::GCedWithMixinWithPostConstructionCallback::cb_callcount++;
  }
};

namespace internal {

TEST_F(GarbageCollectedTestWithHeap, PostConstructionCallback) {
  EXPECT_EQ(0u, GCedWithPostConstructionCallback::cb_callcount);
  MakeGarbageCollected<GCedWithPostConstructionCallback>(GetAllocationHandle());
  EXPECT_EQ(1u, GCedWithPostConstructionCallback::cb_callcount);
}

TEST_F(GarbageCollectedTestWithHeap, PostConstructionCallbackForMixin) {
  EXPECT_EQ(0u, MixinWithPostConstructionCallback::cb_callcount);
  MakeGarbageCollected<GCedWithMixinWithPostConstructionCallback>(
      GetAllocationHandle());
  EXPECT_EQ(1u, MixinWithPostConstructionCallback::cb_callcount);
}

namespace {

int GetDummyValue() {
  static v8::base::SpinningMutex mutex;
  static int ret = 43;
  // Global lock access to avoid reordering.
  v8::base::SpinningMutexGuard guard(&mutex);
  return ret;
}

class CheckObjectInConstructionBeforeInitializerList final
    : public GarbageCollected<CheckObjectInConstructionBeforeInitializerList> {
 public:
  CheckObjectInConstructionBeforeInitializerList()
      : in_construction_before_initializer_list_(
            HeapObjectHeader::FromObject(this).IsInConstruction()),
        unused_int_(GetDummyValue()) {
    EXPECT_TRUE(in_construction_before_initializer_list_);
    EXPECT_TRUE(HeapObjectHeader::FromObject(this).IsInConstruction());
  }

  void Trace(Visitor*) const {}

 private:
  bool in_construction_before_initializer_list_;
  int unused_int_;
};

class CheckMixinInConstructionBeforeInitializerList
    : public GarbageCollectedMixin {
 public:
  explicit CheckMixinInConstructionBeforeInitializerList(void* payload_start)
      : in_construction_before_initializer_list_(
            HeapObjectHeader::FromObject(payload_start).IsInConstruction()),
        unused_int_(GetDummyValue()) {
    EXPECT_TRUE(in_construction_before_initializer_list_);
    EXPECT_TRUE(HeapObjectHeader::FromObject(payload_start).IsInConstruction());
  }

  void Trace(Visitor*) const override {}

 private:
  bool in_construction_before_initializer_list_;
  int unused_int_;
};

class UnmanagedMixinForcingVTable {
 protected:
  virtual void ForceVTable() {}
};

class CheckGCedWithMixinInConstructionBeforeInitializerList
    : public GarbageCollected<
          CheckGCedWithMixinInConstructionBeforeInitializerList>,
      public UnmanagedMixinForcingVTable,
      public CheckMixinInConstructionBeforeInitializerList {
 public:
  CheckGCedWithMixinInConstructionBeforeInitializerList()
      : CheckMixinInConstructionBeforeInitializerList(this) {
    // Ensure that compiler indeed generated an inner object.
    CHECK_NE(
        this,
        static_cast<void*>(
            static_cast<CheckMixinInConstructionBeforeInitializerList*>(this)));
  }
};

}  // namespace

TEST_F(GarbageCollectedTestWithHeap, GarbageCollectedInConstructionDuringCtor) {
  MakeGarbageCollected<CheckObjectInConstructionBeforeInitializerList>(
      GetAllocationHandle());
}

TEST_F(GarbageCollectedTestWithHeap,
       GarbageCollectedMixinInConstructionDuringCtor) {
  MakeGarbageCollected<CheckGCedWithMixinInConstructionBeforeInitializerList>(
      GetAllocationHandle());
}

namespace {

struct MixinA : GarbageCollectedMixin {};
struct MixinB : GarbageCollectedMixin {};
struct GCed1 : GarbageCollected<GCed>, MixinA, MixinB {};
struct GCed2 : MixinA, MixinB {};

static_assert(
    sizeof(GCed1) == sizeof(GCed2),
    "Check that empty base optimization always works for GarbageCollected");
}  // namespace

}  // namespace internal
}  // namespace cppgc
