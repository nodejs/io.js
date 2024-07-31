// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/conservative-stack-visitor.h"

#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

namespace {

class RecordingVisitor final : public RootVisitor {
 public:
  V8_NOINLINE explicit RecordingVisitor(Isolate* isolate) {
    // Allocate the object.
    auto h = isolate->factory()->NewFixedArray(256, AllocationType::kOld);
    the_object_ = *h;
    base_address_ = the_object_.address();
    tagged_address_ = the_object_.ptr();
    inner_address_ = base_address_ + 42 * kTaggedSize;
#ifdef V8_COMPRESS_POINTERS
    compr_address_ = static_cast<uint32_t>(
        V8HeapCompressionScheme::CompressAny(base_address_));
    compr_inner_ = static_cast<uint32_t>(
        V8HeapCompressionScheme::CompressAny(inner_address_));
#else
    compr_address_ = static_cast<uint32_t>(base_address_);
    compr_inner_ = static_cast<uint32_t>(inner_address_);
#endif
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot current = start; current != end; ++current) {
      if (*current == the_object_) found_ = true;
    }
  }

  void Reset() { found_ = false; }
  bool found() const { return found_; }

  Address base_address() const { return base_address_; }
  Address tagged_address() const { return tagged_address_; }
  Address inner_address() const { return inner_address_; }
  uint32_t compr_address() const { return compr_address_; }
  uint32_t compr_inner() const { return compr_inner_; }

 private:
  // Some heap object that we want to check if it is visited or not.
  Tagged<HeapObject> the_object_;

  // Addresses of this object.
  Address base_address_;    // Uncompressed base address
  Address tagged_address_;  // Tagged uncompressed base address
  Address inner_address_;   // Some inner address
  uint32_t compr_address_;  // Compressed base address
  uint32_t compr_inner_;    // Compressed inner address

  // Has the object been found?
  bool found_ = false;
};

}  // namespace

using ConservativeStackVisitorTest = TestWithHeapInternalsAndContext;

// In the following, we avoid negative tests, i.e., tests checking that objects
// are not visited when there are no pointers to them on the stack. Such tests
// are generally fragile and could fail on some platforms because of unforeseen
// compiler optimizations. In general we cannot ensure in a portable way that
// no pointer remained on the stack (or in some register) after the
// initialization of RecordingVisitor and until the invocation of
// Stack::IteratePointers.

TEST_F(ConservativeStackVisitorTest, DirectBasePointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address ptr = recorder->base_address();

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(kNullAddress, ptr);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, TaggedBasePointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address ptr = recorder->tagged_address();

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(kNullAddress, ptr);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, InnerPointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address ptr = recorder->inner_address();

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(kNullAddress, ptr);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

#ifdef V8_COMPRESS_POINTERS

TEST_F(ConservativeStackVisitorTest, HalfWord1) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {recorder->compr_address(), 0};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[0]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, HalfWord2) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {0, recorder->compr_address()};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[1]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, InnerHalfWord1) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {recorder->compr_inner(), 0};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[0]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

TEST_F(ConservativeStackVisitorTest, InnerHalfWord2) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t ptr[] = {0, recorder->compr_inner()};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointer alive.
    EXPECT_NE(static_cast<uint32_t>(0), ptr[1]);
  }

  // The object should have been visited.
  EXPECT_TRUE(recorder->found());
}

#endif  // V8_COMPRESS_POINTERS

}  // namespace internal
}  // namespace v8
