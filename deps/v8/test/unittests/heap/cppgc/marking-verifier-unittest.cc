// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marking-verifier.h"

#include "include/cppgc/allocation.h"
#include "include/cppgc/member.h"
#include "include/cppgc/persistent.h"
#include "include/cppgc/prefinalizer.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class MarkingVerifierTest : public testing::TestWithHeap {
 public:
  V8_NOINLINE void VerifyMarking(HeapBase& heap, StackState stack_state,
                                 size_t expected_marked_bytes) {
    Heap::From(GetHeap())->object_allocator().ResetLinearAllocationBuffers();
    Heap::From(GetHeap())->stack()->SetMarkerAndCallback(
        [&heap, stack_state, expected_marked_bytes]() {
          MarkingVerifier verifier(heap, CollectionType::kMajor);
          verifier.Run(stack_state, expected_marked_bytes);
        });
  }
};

class GCed : public GarbageCollected<GCed> {
 public:
  void SetChild(GCed* child) { child_ = child; }
  void SetWeakChild(GCed* child) { weak_child_ = child; }
  GCed* child() const { return child_.Get(); }
  GCed* weak_child() const { return weak_child_.Get(); }
  void Trace(cppgc::Visitor* visitor) const {
    visitor->Trace(child_);
    visitor->Trace(weak_child_);
  }

 private:
  Member<GCed> child_;
  WeakMember<GCed> weak_child_;
};

template <typename T>
V8_NOINLINE T access(volatile const T& t) {
  return t;
}

bool MarkHeader(HeapObjectHeader& header) {
  if (header.TryMarkAtomic()) {
    BasePage::FromPayload(&header)->IncrementMarkedBytes(
        header.AllocatedSize());
    return true;
  }
  return false;
}

}  // namespace

// Following tests should not crash.

TEST_F(MarkingVerifierTest, DoesNotDieOnMarkedOnStackReference) {
  GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& header = HeapObjectHeader::FromObject(object);
  ASSERT_TRUE(MarkHeader(header));
  VerifyMarking(Heap::From(GetHeap())->AsBase(),
                StackState::kMayContainHeapPointers, header.AllocatedSize());
  access(object);
}

TEST_F(MarkingVerifierTest, DoesNotDieOnMarkedMember) {
  Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& parent_header = HeapObjectHeader::FromObject(parent.Get());
  ASSERT_TRUE(MarkHeader(parent_header));
  parent->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  auto& child_header = HeapObjectHeader::FromObject(parent->child());
  ASSERT_TRUE(MarkHeader(child_header));
  VerifyMarking(Heap::From(GetHeap())->AsBase(), StackState::kNoHeapPointers,
                parent_header.AllocatedSize() + child_header.AllocatedSize());
}

TEST_F(MarkingVerifierTest, DoesNotDieOnMarkedWeakMember) {
  Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& parent_header = HeapObjectHeader::FromObject(parent.Get());
  ASSERT_TRUE(MarkHeader(parent_header));
  parent->SetWeakChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  auto& child_header = HeapObjectHeader::FromObject(parent->weak_child());
  ASSERT_TRUE(MarkHeader(child_header));
  VerifyMarking(Heap::From(GetHeap())->AsBase(), StackState::kNoHeapPointers,
                parent_header.AllocatedSize() + child_header.AllocatedSize());
}

namespace {

class GCedWithCallback : public GarbageCollected<GCedWithCallback> {
 public:
  template <typename Callback>
  explicit GCedWithCallback(Callback callback) {
    callback(this);
  }
  void Trace(cppgc::Visitor* visitor) const {}
};

}  // namespace

TEST_F(MarkingVerifierTest, DoesNotDieOnInConstructionOnObject) {
  MakeGarbageCollected<GCedWithCallback>(
      GetAllocationHandle(), [this](GCedWithCallback* obj) {
        auto& header = HeapObjectHeader::FromObject(obj);
        CHECK(MarkHeader(header));
        VerifyMarking(Heap::From(GetHeap())->AsBase(),
                      StackState::kMayContainHeapPointers,
                      header.AllocatedSize());
      });
}

namespace {
class GCedWithCallbackAndChild final
    : public GarbageCollected<GCedWithCallbackAndChild> {
 public:
  template <typename Callback>
  GCedWithCallbackAndChild(GCed* gced, Callback callback) : child_(gced) {
    callback(this);
  }
  void Trace(cppgc::Visitor* visitor) const { visitor->Trace(child_); }

 private:
  Member<GCed> child_;
};

template <typename T>
struct Holder : public GarbageCollected<Holder<T>> {
 public:
  void Trace(cppgc::Visitor* visitor) const { visitor->Trace(object); }
  Member<T> object = nullptr;
};
}  // namespace

TEST_F(MarkingVerifierTest, DoesntDieOnInConstructionObjectWithWriteBarrier) {
  // Regression test: https://crbug.com/v8/10989.
  // GCedWithCallbackAndChild is marked by write barrier and then discarded by
  // FlushNotFullyConstructedObjects because it is already marked.
  Persistent<Holder<GCedWithCallbackAndChild>> persistent =
      MakeGarbageCollected<Holder<GCedWithCallbackAndChild>>(
          GetAllocationHandle());
  GCConfig config = GCConfig::PreciseIncrementalConfig();
  Heap::From(GetHeap())->StartIncrementalGarbageCollection(config);
  MakeGarbageCollected<GCedWithCallbackAndChild>(
      GetAllocationHandle(), MakeGarbageCollected<GCed>(GetAllocationHandle()),
      [&persistent](GCedWithCallbackAndChild* obj) {
        persistent->object = obj;
      });
  GetMarkerRef()->IncrementalMarkingStepForTesting(StackState::kNoHeapPointers);
  Heap::From(GetHeap())->FinalizeIncrementalGarbageCollectionIfRunning(config);
}

// Death tests.

namespace {

class MarkingVerifierDeathTest : public MarkingVerifierTest {
 protected:
  template <template <typename T> class Reference>
  void TestResurrectingPreFinalizer();
};

}  // namespace

TEST_F(MarkingVerifierDeathTest, DieOnUnmarkedOnStackReference) {
  GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& header = HeapObjectHeader::FromObject(object);
  USE(header);
  EXPECT_DEATH_IF_SUPPORTED(VerifyMarking(Heap::From(GetHeap())->AsBase(),
                                          StackState::kMayContainHeapPointers,
                                          header.AllocatedSize()),
                            "");
  access(object);
}

TEST_F(MarkingVerifierDeathTest, DieOnUnmarkedMember) {
  Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& parent_header = HeapObjectHeader::FromObject(parent);
  ASSERT_TRUE(parent_header.TryMarkAtomic());
  parent->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  EXPECT_DEATH_IF_SUPPORTED(
      VerifyMarking(Heap::From(GetHeap())->AsBase(),
                    StackState::kNoHeapPointers, parent_header.AllocatedSize()),
      "");
}

TEST_F(MarkingVerifierDeathTest, DieOnUnmarkedWeakMember) {
  Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& parent_header = HeapObjectHeader::FromObject(parent);
  ASSERT_TRUE(parent_header.TryMarkAtomic());
  parent->SetWeakChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  EXPECT_DEATH_IF_SUPPORTED(
      VerifyMarking(Heap::From(GetHeap())->AsBase(),
                    StackState::kNoHeapPointers, parent_header.AllocatedSize()),
      "");
}

#ifdef CPPGC_VERIFY_HEAP

TEST_F(MarkingVerifierDeathTest, DieOnUnexpectedLiveByteCount) {
  GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& header = HeapObjectHeader::FromObject(object);
  ASSERT_TRUE(header.TryMarkAtomic());
  EXPECT_DEATH_IF_SUPPORTED(VerifyMarking(Heap::From(GetHeap())->AsBase(),
                                          StackState::kMayContainHeapPointers,
                                          header.AllocatedSize() - 1),
                            "");
}

namespace {
void EscapeControlRegexCharacters(std::string& s) {
  for (std::string::size_type start_pos = 0;
       (start_pos = s.find_first_of("().*+\\", start_pos)) != std::string::npos;
       start_pos += 2) {
    s.insert(start_pos, "\\");
  }
}
}  // anonymous namespace

TEST_F(MarkingVerifierDeathTest, DieWithDebugInfoOnUnexpectedLiveByteCount) {
  using ::testing::AllOf;
  using ::testing::ContainsRegex;
  GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto& header = HeapObjectHeader::FromObject(object);
  ASSERT_TRUE(header.TryMarkAtomic());
  size_t allocated = header.AllocatedSize();
  size_t expected = allocated - 1;
  std::string regex_total =
      "\n<--- Mismatch in marking verifier --->"
      "\nMarked bytes: expected " +
      std::to_string(expected) + " vs. verifier found " +
      std::to_string(allocated) + ",";
  std::string class_name =
      header.GetName(HeapObjectNameForUnnamedObject::kUseClassNameIfSupported)
          .value;
  EscapeControlRegexCharacters(class_name);
  std::string regex_page =
      "\nNormal page in space \\d+:"
      "\nMarked bytes: expected 0 vs. verifier found " +
      std::to_string(allocated) +
      ",.*"
      "\n- " +
      class_name + " at .*, size " + std::to_string(header.ObjectSize()) +
      ", marked\n";
  EXPECT_DEATH_IF_SUPPORTED(
      VerifyMarking(Heap::From(GetHeap())->AsBase(),
                    StackState::kNoHeapPointers, expected),
      AllOf(ContainsRegex(regex_total), ContainsRegex(regex_page)));
}

#endif  // CPPGC_VERIFY_HEAP

namespace {

template <template <typename T> class Reference>
class ResurrectingPreFinalizer
    : public GarbageCollected<ResurrectingPreFinalizer<Reference>> {
  CPPGC_USING_PRE_FINALIZER(ResurrectingPreFinalizer<Reference>, Dispose);

 public:
  class Storage : public GarbageCollected<Storage> {
   public:
    void Trace(Visitor* visitor) const { visitor->Trace(ref); }

    Reference<GCed> ref;
  };

  ResurrectingPreFinalizer(Storage* storage, GCed* object_that_dies)
      : storage_(storage), object_that_dies_(object_that_dies) {}

  void Trace(Visitor* visitor) const {
    visitor->Trace(storage_);
    visitor->Trace(object_that_dies_);
  }

 private:
  void Dispose() { storage_->ref = object_that_dies_; }

  Member<Storage> storage_;
  Member<GCed> object_that_dies_;
};

}  // namespace

template <template <typename T> class Reference>
void MarkingVerifierDeathTest::TestResurrectingPreFinalizer() {
  Persistent<typename ResurrectingPreFinalizer<Reference>::Storage> storage(
      MakeGarbageCollected<
          typename ResurrectingPreFinalizer<Reference>::Storage>(
          GetAllocationHandle()));
  MakeGarbageCollected<ResurrectingPreFinalizer<Reference>>(
      GetAllocationHandle(), storage.Get(),
      MakeGarbageCollected<GCed>(GetAllocationHandle()));
  EXPECT_DEATH_IF_SUPPORTED(PreciseGC(), "");
}

#if CPPGC_VERIFY_HEAP

TEST_F(MarkingVerifierDeathTest, DiesOnResurrectedMember) {
  TestResurrectingPreFinalizer<Member>();
}

TEST_F(MarkingVerifierDeathTest, DiesOnResurrectedWeakMember) {
  TestResurrectingPreFinalizer<WeakMember>();
}

#endif  // CPPGC_VERIFY_HEAP

}  // namespace internal
}  // namespace cppgc
