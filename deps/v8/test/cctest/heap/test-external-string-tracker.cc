// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/api/api.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/spaces.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

#define TEST_STR "tests are great!"

namespace v8 {
namespace internal {
namespace heap {

// Adapted from cctest/test-api.cc
class TestOneByteResource : public v8::String::ExternalOneByteStringResource {
 public:
  explicit TestOneByteResource(const char* data, int* counter = nullptr,
                               size_t offset = 0)
      : orig_data_(data),
        data_(data + offset),
        length_(strlen(data) - offset),
        counter_(counter) {}

  ~TestOneByteResource() override {
    i::DeleteArray(orig_data_);
    if (counter_ != nullptr) ++*counter_;
  }

  const char* data() const override { return data_; }

  size_t length() const override { return length_; }

 private:
  const char* orig_data_;
  const char* data_;
  size_t length_;
  int* counter_;
};

TEST(ExternalString_ExternalBackingStoreSizeIncreases) {
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  ExternalBackingStoreType type = ExternalBackingStoreType::kExternalString;

  const size_t backing_store_before =
      heap->old_space()->ExternalBackingStoreBytes(type);

  {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::String> es = v8::String::NewExternalOneByte(
        isolate, new TestOneByteResource(i::StrDup(TEST_STR))).ToLocalChecked();
    USE(es);

    const size_t backing_store_after =
        heap->old_space()->ExternalBackingStoreBytes(type);

    CHECK_EQ(es->Length(), backing_store_after - backing_store_before);
  }
}

TEST(ExternalString_ExternalBackingStoreSizeDecreases) {
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  ExternalBackingStoreType type = ExternalBackingStoreType::kExternalString;

  const size_t backing_store_before =
      heap->old_space()->ExternalBackingStoreBytes(type);

  {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::String> es = v8::String::NewExternalOneByte(
        isolate, new TestOneByteResource(i::StrDup(TEST_STR))).ToLocalChecked();
    USE(es);
  }

  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeAtomicMajorGC(heap);
  }

  const size_t backing_store_after =
      heap->old_space()->ExternalBackingStoreBytes(type);
  CHECK_EQ(0, backing_store_after - backing_store_before);
}

TEST(ExternalString_ExternalBackingStoreSizeIncreasesMarkCompact) {
  if (!v8_flags.compact) return;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  heap::AbandonCurrentlyFreeMemory(heap->old_space());
  ExternalBackingStoreType type = ExternalBackingStoreType::kExternalString;

  const size_t backing_store_before =
      heap->old_space()->ExternalBackingStoreBytes(type);

  {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::String> es = v8::String::NewExternalOneByte(
        isolate, new TestOneByteResource(i::StrDup(TEST_STR))).ToLocalChecked();
    v8::internal::DirectHandle<v8::internal::String> esh =
        v8::Utils::OpenDirectHandle(*es);

    PageMetadata* page_before_gc = PageMetadata::FromHeapObject(*esh);
    heap::ForceEvacuationCandidate(page_before_gc);

    heap::InvokeMajorGC(heap);

    const size_t backing_store_after =
        heap->old_space()->ExternalBackingStoreBytes(type);
    CHECK_EQ(es->Length(), backing_store_after - backing_store_before);
  }

  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeAtomicMajorGC(heap);
  }

  const size_t backing_store_after =
      heap->old_space()->ExternalBackingStoreBytes(type);
  CHECK_EQ(0, backing_store_after - backing_store_before);
}

TEST(ExternalString_ExternalBackingStoreSizeIncreasesAfterExternalization) {
  if (v8_flags.single_generation) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  ExternalBackingStoreType type = ExternalBackingStoreType::kExternalString;
  size_t old_backing_store_before = 0, new_backing_store_before = 0;

  {
    v8::HandleScope handle_scope(isolate);

    new_backing_store_before =
        heap->new_space()->ExternalBackingStoreBytes(type);
    old_backing_store_before =
        heap->old_space()->ExternalBackingStoreBytes(type);

    // Allocate normal string in the new gen.
    v8::Local<v8::String> str =
        v8::String::NewFromUtf8Literal(isolate, TEST_STR);

    CHECK_EQ(0, heap->new_space()->ExternalBackingStoreBytes(type) -
                    new_backing_store_before);

    // Trigger full GC so that the newly allocated string moves to old gen.
    heap::InvokeAtomicMajorGC(heap);

    bool success = str->MakeExternal(
        isolate, new TestOneByteResource(i::StrDup(TEST_STR)));
    CHECK(success);

    CHECK_EQ(str->Length(), heap->old_space()->ExternalBackingStoreBytes(type) -
                                old_backing_store_before);
  }

  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeAtomicMajorGC(heap);
  }

  const size_t backing_store_after =
      heap->old_space()->ExternalBackingStoreBytes(type);
  CHECK_EQ(0, backing_store_after - old_backing_store_before);
}

TEST(ExternalString_PromotedThinString) {
  if (v8_flags.single_generation) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  i::Isolate* i_isolate = CcTest::i_isolate();
  i::Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();

  {
    v8::HandleScope handle_scope(isolate);

    // New external string in the old space.
    v8::internal::Handle<v8::internal::String> string1 =
        factory
            ->NewExternalStringFromOneByte(
                new TestOneByteResource(i::StrDup(TEST_STR)))
            .ToHandleChecked();

    // Internalize external string.
    i::Handle<i::String> isymbol1 = factory->InternalizeString(string1);
    CHECK(IsInternalizedString(*isymbol1));
    CHECK(IsExternalString(*string1));
    CHECK(!HeapLayout::InYoungGeneration(*isymbol1));

    // Collect thin string. References to the thin string will be updated to
    // point to the actual external string in the old space.
    heap::InvokeAtomicMinorGC(heap);

    USE(isymbol1);
  }
}
}  // namespace heap
}  // namespace internal
}  // namespace v8

#undef TEST_STR
