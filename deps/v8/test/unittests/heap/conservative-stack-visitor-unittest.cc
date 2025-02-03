// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/conservative-stack-visitor.h"

#include "src/codegen/assembler-inl.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

namespace {

// clang-format off
enum : int {
  kRegularObject = 0,
  kCodeObject = 1,
  kTrustedObject = 2,
  kNumberOfObjects
};
// clang-format on

class RecordingVisitor final : public RootVisitor {
 public:
  V8_NOINLINE explicit RecordingVisitor(Isolate* isolate) {
    HandleScope scope(isolate);
    // Allocate some regular object.
    the_object_[kRegularObject] = AllocateRegularObject(isolate, 256);
    // Allocate a code object.
    the_object_[kCodeObject] = AllocateCodeObject(isolate, 256);
    // Allocate a trusted object.
    the_object_[kTrustedObject] = AllocateTrustedObject(isolate, 256);
    // Mark the objects as not found;
    for (int i = 0; i < kNumberOfObjects; ++i) found_[i] = false;
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot current = start; current != end; ++current) {
      for (int i = 0; i < kNumberOfObjects; ++i)
        if ((*current).ptr() == the_object_[i].ptr()) found_[i] = true;
    }
  }

  bool found(int index) const {
    DCHECK_LE(0, index);
    DCHECK_LT(index, kNumberOfObjects);
    return found_[index];
  }

  Address base_address(int index) const { return the_object(index).address(); }
  Address tagged_address(int index) const { return the_object(index).ptr(); }
  Address inner_address(int index) const {
    return base_address(index) + 42 * kTaggedSize;
  }
#ifdef V8_COMPRESS_POINTERS
  uint32_t compr_address(int index) const {
    return static_cast<uint32_t>(
        V8HeapCompressionScheme::CompressAny(base_address(index)));
  }
  uint32_t compr_inner(int index) const {
    return static_cast<uint32_t>(
        V8HeapCompressionScheme::CompressAny(inner_address(index)));
  }
#endif

 private:
  Tagged<HeapObject> the_object(int index) const {
    DCHECK_LE(0, index);
    DCHECK_LT(index, kNumberOfObjects);
    return the_object_[index];
  }

  Tagged<FixedArray> AllocateRegularObject(Isolate* isolate, int size) {
    return *isolate->factory()->NewFixedArray(size, AllocationType::kOld);
  }

  Tagged<InstructionStream> AllocateCodeObject(Isolate* isolate, int size) {
    Assembler assm(AssemblerOptions{});

    for (int i = 0; i < size; ++i)
      assm.nop();  // supported on all architectures

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Tagged<Code> code =
        *Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    return code->instruction_stream();
  }

  Tagged<TrustedFixedArray> AllocateTrustedObject(Isolate* isolate, int size) {
    return *isolate->factory()->NewTrustedFixedArray(size);
  }

  // Some heap objects that we want to check if they are visited or not.
  Tagged<HeapObject> the_object_[kNumberOfObjects];

  // Have the objects been found?
  bool found_[kNumberOfObjects];
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
    volatile Address regular_ptr = recorder->base_address(kRegularObject);
    volatile Address code_ptr = recorder->base_address(kCodeObject);
    volatile Address trusted_ptr = recorder->base_address(kTrustedObject);

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointers alive.
    EXPECT_NE(kNullAddress, regular_ptr);
    EXPECT_NE(kNullAddress, code_ptr);
    EXPECT_NE(kNullAddress, trusted_ptr);
  }

  // The objects should have been visited.
  EXPECT_TRUE(recorder->found(kRegularObject));
  EXPECT_TRUE(recorder->found(kCodeObject));
  EXPECT_TRUE(recorder->found(kTrustedObject));
}

TEST_F(ConservativeStackVisitorTest, TaggedBasePointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address regular_ptr = recorder->tagged_address(kRegularObject);
    volatile Address code_ptr = recorder->tagged_address(kCodeObject);
    volatile Address trusted_ptr = recorder->tagged_address(kTrustedObject);

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointers alive.
    EXPECT_NE(kNullAddress, regular_ptr);
    EXPECT_NE(kNullAddress, code_ptr);
    EXPECT_NE(kNullAddress, trusted_ptr);
  }

  // The objects should have been visited.
  EXPECT_TRUE(recorder->found(kRegularObject));
  EXPECT_TRUE(recorder->found(kCodeObject));
  EXPECT_TRUE(recorder->found(kTrustedObject));
}

TEST_F(ConservativeStackVisitorTest, InnerPointer) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile Address regular_ptr = recorder->inner_address(kRegularObject);
    volatile Address code_ptr = recorder->inner_address(kCodeObject);
    volatile Address trusted_ptr = recorder->inner_address(kTrustedObject);

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointers alive.
    EXPECT_NE(kNullAddress, regular_ptr);
    EXPECT_NE(kNullAddress, code_ptr);
    EXPECT_NE(kNullAddress, trusted_ptr);
  }

  // The objects should have been visited.
  EXPECT_TRUE(recorder->found(kRegularObject));
  EXPECT_TRUE(recorder->found(kCodeObject));
  EXPECT_TRUE(recorder->found(kTrustedObject));
}

#ifdef V8_COMPRESS_POINTERS

TEST_F(ConservativeStackVisitorTest, HalfWord1) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t regular_ptr[] = {recorder->compr_address(kRegularObject),
                                       0};
    volatile uint32_t code_ptr[] = {recorder->compr_address(kCodeObject), 0};
    volatile uint32_t trusted_ptr[] = {recorder->compr_address(kTrustedObject),
                                       0};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointers alive.
    EXPECT_NE(static_cast<uint32_t>(0), regular_ptr[0]);
    EXPECT_NE(static_cast<uint32_t>(0), code_ptr[0]);
    EXPECT_NE(static_cast<uint32_t>(0), trusted_ptr[0]);
  }

  // The objects should have been visited.
  EXPECT_TRUE(recorder->found(kRegularObject));
  EXPECT_TRUE(recorder->found(kCodeObject));
  EXPECT_TRUE(recorder->found(kTrustedObject));
}

TEST_F(ConservativeStackVisitorTest, HalfWord2) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t regular_ptr[] = {0,
                                       recorder->compr_address(kRegularObject)};
    volatile uint32_t code_ptr[] = {0, recorder->compr_address(kCodeObject)};
    volatile uint32_t trusted_ptr[] = {0,
                                       recorder->compr_address(kTrustedObject)};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointers alive.
    EXPECT_NE(static_cast<uint32_t>(0), regular_ptr[1]);
    EXPECT_NE(static_cast<uint32_t>(0), code_ptr[1]);
    EXPECT_NE(static_cast<uint32_t>(0), trusted_ptr[1]);
  }

  // The objects should have been visited.
  EXPECT_TRUE(recorder->found(kRegularObject));
  EXPECT_TRUE(recorder->found(kCodeObject));
  EXPECT_TRUE(recorder->found(kTrustedObject));
}

TEST_F(ConservativeStackVisitorTest, InnerHalfWord1) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t regular_ptr[] = {recorder->compr_inner(kRegularObject),
                                       0};
    volatile uint32_t code_ptr[] = {recorder->compr_inner(kCodeObject), 0};
    volatile uint32_t trusted_ptr[] = {recorder->compr_inner(kTrustedObject),
                                       0};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointers alive.
    EXPECT_NE(static_cast<uint32_t>(0), regular_ptr[0]);
    EXPECT_NE(static_cast<uint32_t>(0), code_ptr[0]);
    EXPECT_NE(static_cast<uint32_t>(0), trusted_ptr[0]);
  }

  // The objects should have been visited.
  EXPECT_TRUE(recorder->found(kRegularObject));
  EXPECT_TRUE(recorder->found(kCodeObject));
  EXPECT_TRUE(recorder->found(kTrustedObject));
}

TEST_F(ConservativeStackVisitorTest, InnerHalfWord2) {
  auto recorder = std::make_unique<RecordingVisitor>(isolate());

  // Ensure the heap is iterable before CSS.
  IsolateSafepointScope safepoint_scope(heap());
  heap()->MakeHeapIterable();

  {
    volatile uint32_t regular_ptr[] = {0,
                                       recorder->compr_inner(kRegularObject)};
    volatile uint32_t code_ptr[] = {0, recorder->compr_inner(kCodeObject)};
    volatile uint32_t trusted_ptr[] = {0,
                                       recorder->compr_inner(kTrustedObject)};

    ConservativeStackVisitor stack_visitor(isolate(), recorder.get());
    heap()->stack().IteratePointersForTesting(&stack_visitor);

    // Make sure to keep the pointers alive.
    EXPECT_NE(static_cast<uint32_t>(0), regular_ptr[1]);
    EXPECT_NE(static_cast<uint32_t>(0), code_ptr[1]);
    EXPECT_NE(static_cast<uint32_t>(0), trusted_ptr[1]);
  }

  // The objects should have been visited.
  EXPECT_TRUE(recorder->found(kRegularObject));
  EXPECT_TRUE(recorder->found(kCodeObject));
  EXPECT_TRUE(recorder->found(kTrustedObject));
}

#endif  // V8_COMPRESS_POINTERS

}  // namespace internal
}  // namespace v8
