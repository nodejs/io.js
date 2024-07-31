// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/member.h"

#include <algorithm>
#include <vector>

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/internal/member-storage.h"
#include "include/cppgc/internal/pointer-policies.h"
#include "include/cppgc/persistent.h"
#include "include/cppgc/sentinel-pointer.h"
#include "include/cppgc/type-traits.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

struct GCed : GarbageCollected<GCed> {
  double d;
  virtual void Trace(cppgc::Visitor*) const {}
};

struct DerivedMixin : GarbageCollectedMixin {
  void Trace(cppgc::Visitor* v) const override {}
};

struct DerivedGCed : GCed, DerivedMixin {
  void Trace(cppgc::Visitor* v) const override { GCed::Trace(v); }
};

// Compile tests.
static_assert(!IsWeakV<Member<GCed>>, "Member is always strong.");
static_assert(IsWeakV<WeakMember<GCed>>, "WeakMember is always weak.");

static_assert(IsMemberTypeV<Member<GCed>>, "Member must be Member.");
static_assert(IsMemberTypeV<const Member<GCed>>,
              "const Member must be Member.");
static_assert(IsMemberTypeV<const Member<GCed>&>,
              "const Member ref must be Member.");
static_assert(!IsMemberTypeV<WeakMember<GCed>>,
              "WeakMember must not be Member.");
static_assert(!IsMemberTypeV<UntracedMember<GCed>>,
              "UntracedMember must not be Member.");
static_assert(!IsMemberTypeV<int>, "int must not be Member.");
static_assert(!IsWeakMemberTypeV<Member<GCed>>,
              "Member must not be WeakMember.");
static_assert(IsWeakMemberTypeV<WeakMember<GCed>>,
              "WeakMember must be WeakMember.");
static_assert(!IsWeakMemberTypeV<UntracedMember<GCed>>,
              "UntracedMember must not be WeakMember.");
static_assert(!IsWeakMemberTypeV<int>, "int must not be WeakMember.");
static_assert(!IsUntracedMemberTypeV<Member<GCed>>,
              "Member must not be UntracedMember.");
static_assert(!IsUntracedMemberTypeV<WeakMember<GCed>>,
              "WeakMember must not be UntracedMember.");
static_assert(IsUntracedMemberTypeV<UntracedMember<GCed>>,
              "UntracedMember must be UntracedMember.");
static_assert(!IsUntracedMemberTypeV<int>, "int must not be UntracedMember.");
static_assert(IsMemberOrWeakMemberTypeV<Member<GCed>>,
              "Member must be Member.");
static_assert(IsMemberOrWeakMemberTypeV<WeakMember<GCed>>,
              "WeakMember must be WeakMember.");
static_assert(!IsMemberOrWeakMemberTypeV<UntracedMember<GCed>>,
              "UntracedMember is neither Member nor WeakMember.");
static_assert(!IsMemberOrWeakMemberTypeV<int>,
              "int is neither Member nor WeakMember.");
static_assert(IsAnyMemberTypeV<Member<GCed>>, "Member must be a member type.");
static_assert(IsAnyMemberTypeV<WeakMember<GCed>>,
              "WeakMember must be a member type.");
static_assert(IsAnyMemberTypeV<UntracedMember<GCed>>,
              "UntracedMember must be a member type.");
static_assert(!IsAnyMemberTypeV<int>, "int must not be a member type.");
static_assert(
    IsAnyMemberTypeV<
        internal::BasicMember<GCed, class SomeTag, NoWriteBarrierPolicy,
                              DefaultMemberCheckingPolicy, RawPointer>>,
    "Any custom member must be a member type.");

struct CustomWriteBarrierPolicy {
  static size_t InitializingWriteBarriersTriggered;
  static size_t AssigningWriteBarriersTriggered;
  static void InitializingBarrier(const void* slot, const void* value) {
    ++InitializingWriteBarriersTriggered;
  }
  template <WriteBarrierSlotType>
  static void AssigningBarrier(const void* slot, const void* value) {
    ++AssigningWriteBarriersTriggered;
  }
  template <WriteBarrierSlotType>
  static void AssigningBarrier(const void* slot, DefaultMemberStorage) {
    ++AssigningWriteBarriersTriggered;
  }
};
size_t CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered = 0;
size_t CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered = 0;

using MemberWithCustomBarrier =
    BasicMember<GCed, StrongMemberTag, CustomWriteBarrierPolicy>;

struct CustomCheckingPolicy {
  static std::vector<GCed*> Cached;
  static size_t ChecksTriggered;
  void CheckPointer(const void* ptr) {
    EXPECT_NE(Cached.cend(), std::find(Cached.cbegin(), Cached.cend(), ptr));
    ++ChecksTriggered;
  }
};
std::vector<GCed*> CustomCheckingPolicy::Cached;
size_t CustomCheckingPolicy::ChecksTriggered = 0;

using MemberWithCustomChecking =
    BasicMember<GCed, StrongMemberTag, DijkstraWriteBarrierPolicy,
                CustomCheckingPolicy>;

class MemberTest : public testing::TestSupportingAllocationOnly {};

}  // namespace

template <template <typename> class MemberType>
void EmptyTest() {
  {
    MemberType<GCed> empty;
    EXPECT_EQ(nullptr, empty.Get());
    EXPECT_EQ(nullptr, empty.Release());
  }
  {
    MemberType<GCed> empty = nullptr;
    EXPECT_EQ(nullptr, empty.Get());
    EXPECT_EQ(nullptr, empty.Release());
  }
  {
    // Move-constructs empty from another Member that is created from nullptr.
    MemberType<const GCed> empty = nullptr;
    EXPECT_EQ(nullptr, empty.Get());
    EXPECT_EQ(nullptr, empty.Release());
  }
}

TEST_F(MemberTest, Empty) {
  EmptyTest<Member>();
  EmptyTest<WeakMember>();
  EmptyTest<UntracedMember>();
}

template <template <typename> class MemberType>
void AtomicCtorTest(cppgc::Heap* heap) {
  {
    GCed* gced = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType<GCed> member(gced,
                            typename MemberType<GCed>::AtomicInitializerTag());
    EXPECT_EQ(gced, member.Get());
  }
  {
    GCed* gced = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType<GCed> member(*gced,
                            typename MemberType<GCed>::AtomicInitializerTag());
    EXPECT_EQ(gced, member.Get());
  }
  {
    MemberType<GCed> member(nullptr,
                            typename MemberType<GCed>::AtomicInitializerTag());
    EXPECT_FALSE(member.Get());
  }
  {
    SentinelPointer s;
    MemberType<GCed> member(s,
                            typename MemberType<GCed>::AtomicInitializerTag());
    EXPECT_EQ(s, member.Get());
  }
}

TEST_F(MemberTest, AtomicCtor) {
  cppgc::Heap* heap = GetHeap();
  AtomicCtorTest<Member>(heap);
  AtomicCtorTest<WeakMember>(heap);
  AtomicCtorTest<UntracedMember>(heap);
}

template <template <typename> class MemberType>
void ClearTest(cppgc::Heap* heap) {
  MemberType<GCed> member =
      MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
  EXPECT_NE(nullptr, member.Get());
  member.Clear();
  EXPECT_EQ(nullptr, member.Get());
}

TEST_F(MemberTest, Clear) {
  cppgc::Heap* heap = GetHeap();
  ClearTest<Member>(heap);
  ClearTest<WeakMember>(heap);
  ClearTest<UntracedMember>(heap);
}

template <template <typename> class MemberType>
void ReleaseTest(cppgc::Heap* heap) {
  GCed* gced = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
  MemberType<GCed> member = gced;
  EXPECT_NE(nullptr, member.Get());
  GCed* raw = member.Release();
  EXPECT_EQ(gced, raw);
  EXPECT_EQ(nullptr, member.Get());
}

TEST_F(MemberTest, Release) {
  cppgc::Heap* heap = GetHeap();
  ReleaseTest<Member>(heap);
  ReleaseTest<WeakMember>(heap);
  ReleaseTest<UntracedMember>(heap);
}

template <template <typename> class MemberType1,
          template <typename> class MemberType2>
void SwapTest(cppgc::Heap* heap) {
  GCed* gced1 = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
  GCed* gced2 = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
  MemberType1<GCed> member1 = gced1;
  MemberType2<GCed> member2 = gced2;
  EXPECT_EQ(gced1, member1.Get());
  EXPECT_EQ(gced2, member2.Get());
  member1.Swap(member2);
  EXPECT_EQ(gced2, member1.Get());
  EXPECT_EQ(gced1, member2.Get());
}

TEST_F(MemberTest, Swap) {
  cppgc::Heap* heap = GetHeap();
  SwapTest<Member, Member>(heap);
  SwapTest<Member, WeakMember>(heap);
  SwapTest<Member, UntracedMember>(heap);
  SwapTest<WeakMember, Member>(heap);
  SwapTest<WeakMember, WeakMember>(heap);
  SwapTest<WeakMember, UntracedMember>(heap);
  SwapTest<UntracedMember, Member>(heap);
  SwapTest<UntracedMember, WeakMember>(heap);
  SwapTest<UntracedMember, UntracedMember>(heap);
}

template <template <typename> class MemberType1,
          template <typename> class MemberType2>
void MoveTest(cppgc::Heap* heap) {
  {
    GCed* gced1 = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType1<GCed> member1 = gced1;
    MemberType2<GCed> member2(std::move(member1));
    // Move-from member must be in empty state.
    EXPECT_FALSE(member1);
    EXPECT_EQ(gced1, member2.Get());
  }
  {
    GCed* gced1 = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType1<GCed> member1 = gced1;
    MemberType2<GCed> member2;
    member2 = std::move(member1);
    // Move-from member must be in empty state.
    EXPECT_FALSE(member1);
    EXPECT_EQ(gced1, member2.Get());
  }
}

TEST_F(MemberTest, Move) {
  cppgc::Heap* heap = GetHeap();
  MoveTest<Member, Member>(heap);
  MoveTest<Member, WeakMember>(heap);
  MoveTest<Member, UntracedMember>(heap);
  MoveTest<WeakMember, Member>(heap);
  MoveTest<WeakMember, WeakMember>(heap);
  MoveTest<WeakMember, UntracedMember>(heap);
  MoveTest<UntracedMember, Member>(heap);
  MoveTest<UntracedMember, WeakMember>(heap);
  MoveTest<UntracedMember, UntracedMember>(heap);
}

template <template <typename> class MemberType1,
          template <typename> class MemberType2>
void HeterogeneousConversionTest(cppgc::Heap* heap) {
  {
    MemberType1<GCed> member1 =
        MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType2<GCed> member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
  {
    MemberType1<DerivedGCed> member1 =
        MakeGarbageCollected<DerivedGCed>(heap->GetAllocationHandle());
    MemberType2<GCed> member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
  {
    MemberType1<GCed> member1 =
        MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType2<GCed> member2;
    member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
  {
    MemberType1<DerivedGCed> member1 =
        MakeGarbageCollected<DerivedGCed>(heap->GetAllocationHandle());
    MemberType2<GCed> member2;
    member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
}

TEST_F(MemberTest, HeterogeneousInterface) {
  cppgc::Heap* heap = GetHeap();
  HeterogeneousConversionTest<Member, Member>(heap);
  HeterogeneousConversionTest<Member, WeakMember>(heap);
  HeterogeneousConversionTest<Member, UntracedMember>(heap);
  HeterogeneousConversionTest<WeakMember, Member>(heap);
  HeterogeneousConversionTest<WeakMember, WeakMember>(heap);
  HeterogeneousConversionTest<WeakMember, UntracedMember>(heap);
  HeterogeneousConversionTest<UntracedMember, Member>(heap);
  HeterogeneousConversionTest<UntracedMember, WeakMember>(heap);
  HeterogeneousConversionTest<UntracedMember, UntracedMember>(heap);
}

template <template <typename> class MemberType,
          template <typename> class PersistentType>
void PersistentConversionTest(cppgc::Heap* heap) {
  {
    PersistentType<GCed> persistent =
        MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType<GCed> member = persistent;
    EXPECT_EQ(persistent.Get(), member.Get());
  }
  {
    PersistentType<DerivedGCed> persistent =
        MakeGarbageCollected<DerivedGCed>(heap->GetAllocationHandle());
    MemberType<GCed> member = persistent;
    EXPECT_EQ(persistent.Get(), member.Get());
  }
  {
    PersistentType<GCed> persistent =
        MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType<GCed> member;
    member = persistent;
    EXPECT_EQ(persistent.Get(), member.Get());
  }
  {
    PersistentType<DerivedGCed> persistent =
        MakeGarbageCollected<DerivedGCed>(heap->GetAllocationHandle());
    MemberType<GCed> member;
    member = persistent;
    EXPECT_EQ(persistent.Get(), member.Get());
  }
}

TEST_F(MemberTest, PersistentConversion) {
  cppgc::Heap* heap = GetHeap();
  PersistentConversionTest<Member, Persistent>(heap);
  PersistentConversionTest<Member, WeakPersistent>(heap);
  PersistentConversionTest<WeakMember, Persistent>(heap);
  PersistentConversionTest<WeakMember, WeakPersistent>(heap);
  PersistentConversionTest<UntracedMember, Persistent>(heap);
  PersistentConversionTest<UntracedMember, WeakPersistent>(heap);
}

template <template <typename> class MemberType1,
          template <typename> class MemberType2>
void EqualityTest(cppgc::Heap* heap) {
  {
    GCed* gced = MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType1<GCed> member1 = gced;
    MemberType2<GCed> member2 = gced;
    EXPECT_TRUE(member1 == member2);
    EXPECT_TRUE(member1 == gced);
    EXPECT_TRUE(member2 == gced);
    EXPECT_FALSE(member1 != member2);
    EXPECT_FALSE(member1 != gced);
    EXPECT_FALSE(member2 != gced);

    member2 = member1;
    EXPECT_TRUE(member1 == member2);
    EXPECT_TRUE(member1 == gced);
    EXPECT_TRUE(member2 == gced);
    EXPECT_FALSE(member1 != member2);
    EXPECT_FALSE(member1 != gced);
    EXPECT_FALSE(member2 != gced);
  }
  {
    MemberType1<GCed> member1 =
        MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    MemberType2<GCed> member2 =
        MakeGarbageCollected<GCed>(heap->GetAllocationHandle());
    EXPECT_TRUE(member1 != member2);
    EXPECT_TRUE(member1 != member2.Get());
    EXPECT_FALSE(member1 == member2);
    EXPECT_FALSE(member1 == member2.Get());
  }
}

TEST_F(MemberTest, EqualityTest) {
  cppgc::Heap* heap = GetHeap();
  EqualityTest<Member, Member>(heap);
  EqualityTest<Member, WeakMember>(heap);
  EqualityTest<Member, UntracedMember>(heap);
  EqualityTest<WeakMember, Member>(heap);
  EqualityTest<WeakMember, WeakMember>(heap);
  EqualityTest<WeakMember, UntracedMember>(heap);
  EqualityTest<UntracedMember, Member>(heap);
  EqualityTest<UntracedMember, WeakMember>(heap);
  EqualityTest<UntracedMember, UntracedMember>(heap);
}

TEST_F(MemberTest, HeterogeneousEqualityTest) {
  cppgc::Heap* heap = GetHeap();
  {
    auto* gced = MakeGarbageCollected<DerivedGCed>(heap->GetAllocationHandle());
    auto* derived = static_cast<DerivedMixin*>(gced);
    ASSERT_NE(reinterpret_cast<void*>(gced), reinterpret_cast<void*>(derived));
  }
  {
    auto* gced = MakeGarbageCollected<DerivedGCed>(heap->GetAllocationHandle());
    Member<DerivedGCed> member = gced;
#define EXPECT_MIXIN_EQUAL(Mixin) \
  EXPECT_TRUE(member == mixin);   \
  EXPECT_TRUE(member == gced);    \
  EXPECT_TRUE(mixin == gced);     \
  EXPECT_FALSE(member != mixin);  \
  EXPECT_FALSE(member != gced);   \
  EXPECT_FALSE(mixin != gced);
    {
      // Construct from raw.
      Member<DerivedMixin> mixin = gced;
      EXPECT_MIXIN_EQUAL(mixin);
    }
    {
      // Copy construct from member.
      Member<DerivedMixin> mixin = member;
      EXPECT_MIXIN_EQUAL(mixin);
    }
    {
      // Move construct from member.
      Member<DerivedMixin> mixin = std::move(member);
      member = gced;
      EXPECT_MIXIN_EQUAL(mixin);
    }
    {
      // Copy assign from member.
      Member<DerivedMixin> mixin;
      mixin = member;
      EXPECT_MIXIN_EQUAL(mixin);
    }
    {
      // Move assign from member.
      Member<DerivedMixin> mixin;
      mixin = std::move(member);
      member = gced;
      EXPECT_MIXIN_EQUAL(mixin);
    }
#undef EXPECT_MIXIN_EQUAL
  }
}

TEST_F(MemberTest, WriteBarrierTriggered) {
  CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered = 0;
  CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered = 0;
  GCed* gced = MakeGarbageCollected<GCed>(GetAllocationHandle());
  MemberWithCustomBarrier member1 = gced;
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(0u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member1 = gced;
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member1 = nullptr;
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  MemberWithCustomBarrier member2 = nullptr;
  // No initializing barriers for std::nullptr_t.
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member2 = kSentinelPointer;
  EXPECT_EQ(kSentinelPointer, member2.Get());
  EXPECT_EQ(kSentinelPointer, member2);
  // No initializing barriers for pointer sentinel.
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member2.Swap(member1);
  EXPECT_EQ(3u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
}

TEST_F(MemberTest, CheckingPolicy) {
  static constexpr size_t kElements = 64u;
  CustomCheckingPolicy::ChecksTriggered = 0u;

  for (std::size_t i = 0; i < kElements; ++i) {
    CustomCheckingPolicy::Cached.push_back(
        MakeGarbageCollected<GCed>(GetAllocationHandle()));
  }

  MemberWithCustomChecking member;
  for (GCed* item : CustomCheckingPolicy::Cached) {
    member = item;
  }
  EXPECT_EQ(CustomCheckingPolicy::Cached.size(),
            CustomCheckingPolicy::ChecksTriggered);
}

namespace {

class MemberHeapTest : public testing::TestWithHeap {};

class GCedWithMembers final : public GarbageCollected<GCedWithMembers> {
 public:
  static size_t live_count_;

  GCedWithMembers() : GCedWithMembers(nullptr, nullptr) {}
  explicit GCedWithMembers(GCedWithMembers* strong, GCedWithMembers* weak)
      : strong_nested_(strong), weak_nested_(weak) {
    ++live_count_;
  }

  ~GCedWithMembers() { --live_count_; }

  void Trace(cppgc::Visitor* visitor) const {
    visitor->Trace(strong_nested_);
    visitor->Trace(weak_nested_);
  }

  bool WasNestedCleared() const { return !weak_nested_; }

 private:
  Member<GCedWithMembers> strong_nested_;
  WeakMember<GCedWithMembers> weak_nested_;
};
size_t GCedWithMembers::live_count_ = 0;

}  // namespace

TEST_F(MemberHeapTest, MemberRetainsObject) {
  EXPECT_EQ(0u, GCedWithMembers::live_count_);
  {
    GCedWithMembers* nested_object =
        MakeGarbageCollected<GCedWithMembers>(GetAllocationHandle());
    Persistent<GCedWithMembers> gced_with_members =
        MakeGarbageCollected<GCedWithMembers>(GetAllocationHandle(),
                                              nested_object, nested_object);
    EXPECT_EQ(2u, GCedWithMembers::live_count_);
    PreciseGC();
    EXPECT_EQ(2u, GCedWithMembers::live_count_);
    EXPECT_FALSE(gced_with_members->WasNestedCleared());
  }
  PreciseGC();
  EXPECT_EQ(0u, GCedWithMembers::live_count_);
  {
    GCedWithMembers* nested_object =
        MakeGarbageCollected<GCedWithMembers>(GetAllocationHandle());
    GCedWithMembers* gced_with_members = MakeGarbageCollected<GCedWithMembers>(
        GetAllocationHandle(), nested_object, nested_object);
    EXPECT_EQ(2u, GCedWithMembers::live_count_);
    ConservativeGC();
    EXPECT_EQ(2u, GCedWithMembers::live_count_);
    EXPECT_FALSE(gced_with_members->WasNestedCleared());
  }
  PreciseGC();
  EXPECT_EQ(0u, GCedWithMembers::live_count_);
}

TEST_F(MemberHeapTest, WeakMemberDoesNotRetainObject) {
  EXPECT_EQ(0u, GCedWithMembers::live_count_);
  auto* weak_nested =
      MakeGarbageCollected<GCedWithMembers>(GetAllocationHandle());
  Persistent<GCedWithMembers> gced_with_members(
      MakeGarbageCollected<GCedWithMembers>(GetAllocationHandle(), nullptr,
                                            weak_nested));
  PreciseGC();
  EXPECT_EQ(1u, GCedWithMembers::live_count_);
  EXPECT_TRUE(gced_with_members->WasNestedCleared());
}

namespace {
class GCedWithConstWeakMember
    : public GarbageCollected<GCedWithConstWeakMember> {
 public:
  explicit GCedWithConstWeakMember(const GCedWithMembers* weak)
      : weak_member_(weak) {}

  void Trace(Visitor* visitor) const { visitor->Trace(weak_member_); }

  const GCedWithMembers* weak_member() const { return weak_member_; }

 private:
  const WeakMember<const GCedWithMembers> weak_member_;
};
}  // namespace

TEST_F(MemberHeapTest, ConstWeakRefIsClearedOnGC) {
  const WeakPersistent<const GCedWithMembers> weak_persistent =
      MakeGarbageCollected<GCedWithMembers>(GetAllocationHandle());
  Persistent<GCedWithConstWeakMember> persistent =
      MakeGarbageCollected<GCedWithConstWeakMember>(GetAllocationHandle(),
                                                    weak_persistent);
  PreciseGC();
  EXPECT_FALSE(weak_persistent);
  EXPECT_FALSE(persistent->weak_member());
}

#if V8_ENABLE_CHECKS

namespace {
class MemberHeapDeathTest : public testing::TestWithHeap {};

class LinkedNode final : public GarbageCollected<LinkedNode> {
 public:
  explicit LinkedNode(LinkedNode* next) : next_(next) {}
  void Trace(Visitor* v) const { v->Trace(next_); }

  void SetNext(LinkedNode* next) { next_ = next; }

 private:
  Member<LinkedNode> next_;
};

}  // namespace

// The following tests create multiple heaps per thread, which is not supported
// with pointer compression enabled.
#if !defined(CPPGC_POINTER_COMPRESSION)
TEST_F(MemberHeapDeathTest, CheckForOffHeapMemberCrashesOnReassignment) {
  std::vector<Member<LinkedNode>> off_heap_member;
  // Verification state is constructed on first assignment.
  off_heap_member.emplace_back(
      MakeGarbageCollected<LinkedNode>(GetAllocationHandle(), nullptr));
  {
    auto tmp_heap = cppgc::Heap::Create(platform_);
    auto* tmp_obj = MakeGarbageCollected<LinkedNode>(
        tmp_heap->GetAllocationHandle(), nullptr);
    EXPECT_DEATH_IF_SUPPORTED(off_heap_member[0] = tmp_obj, "");
  }
}

TEST_F(MemberHeapDeathTest, CheckForOnStackMemberCrashesOnReassignment) {
  Member<LinkedNode> stack_member;
  // Verification state is constructed on first assignment.
  stack_member =
      MakeGarbageCollected<LinkedNode>(GetAllocationHandle(), nullptr);
  {
    auto tmp_heap = cppgc::Heap::Create(platform_);
    auto* tmp_obj = MakeGarbageCollected<LinkedNode>(
        tmp_heap->GetAllocationHandle(), nullptr);
    EXPECT_DEATH_IF_SUPPORTED(stack_member = tmp_obj, "");
  }
}

TEST_F(MemberHeapDeathTest, CheckForOnHeapMemberCrashesOnInitialAssignment) {
  auto* obj = MakeGarbageCollected<LinkedNode>(GetAllocationHandle(), nullptr);
  {
    auto tmp_heap = cppgc::Heap::Create(platform_);
    EXPECT_DEATH_IF_SUPPORTED(
        // For regular on-heap Member references the verification state is
        // constructed eagerly on creating the reference.
        MakeGarbageCollected<LinkedNode>(tmp_heap->GetAllocationHandle(), obj),
        "");
  }
}
#endif  // defined(CPPGC_POINTER_COMPRESSION)

#if defined(CPPGC_POINTER_COMPRESSION)
TEST_F(MemberTest, CompressDecompress) {
  CompressedPointer cp;
  EXPECT_EQ(nullptr, cp.Load());

  Member<GCed> member;
  cp.Store(member.Get());
  EXPECT_EQ(nullptr, cp.Load());

  cp.Store(kSentinelPointer);
  EXPECT_EQ(kSentinelPointer, cp.Load());

  member = kSentinelPointer;
  cp.Store(member.Get());
  EXPECT_EQ(kSentinelPointer, cp.Load());

  member = MakeGarbageCollected<GCed>(GetAllocationHandle());
  cp.Store(member.Get());
  EXPECT_EQ(member.Get(), cp.Load());
}
#endif  // defined(CPPGC_POINTER_COMPRESSION)

#endif  // V8_ENABLE_CHECKS

}  // namespace internal
}  // namespace cppgc
