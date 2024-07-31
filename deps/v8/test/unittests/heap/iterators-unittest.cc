// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-object.h"
#include "src/api/api-inl.h"
#include "src/execution/isolate.h"
#include "src/heap/combined-heap.h"
#include "src/heap/heap.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/read-only-spaces.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects.h"
#include "src/roots/roots-inl.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::heap {

using IteratorsTest = TestWithNativeContext;

namespace {
template <typename T>
void TestIterator(T it) {
  while (!it.Next().is_null()) {
  }
  for (int i = 0; i < 20; i++) {
    CHECK(it.Next().is_null());
  }
}
}  // namespace

TEST_F(IteratorsTest, HeapObjectIteratorNullPastEnd) {
  TestIterator<HeapObjectIterator>(
      static_cast<v8::internal::HeapObjectIterator>(i_isolate()->heap()));
}

TEST_F(IteratorsTest, ReadOnlyHeapObjectIteratorNullPastEnd) {
  TestIterator<ReadOnlyHeapObjectIterator>(
      static_cast<v8::internal::ReadOnlyHeapObjectIterator>(
          i_isolate()->read_only_heap()));
}

TEST_F(IteratorsTest, CombinedHeapObjectIteratorNullPastEnd) {
  TestIterator<CombinedHeapObjectIterator>(i_isolate()->heap());
}

namespace {
// An arbitrary object guaranteed to live on the non-read-only heap.
Tagged<Object> CreateWritableObject(v8::Isolate* isolate) {
  return *v8::Utils::OpenDirectHandle(*v8::Object::New(isolate));
}
}  // namespace

TEST_F(IteratorsTest, ReadOnlyHeapObjectIterator) {
  HandleScope handle_scope(i_isolate());
  const Tagged<Object> sample_object = CreateWritableObject(v8_isolate());
  ReadOnlyHeapObjectIterator iterator(i_isolate()->read_only_heap());
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    CHECK(ReadOnlyHeap::Contains(obj));
    CHECK(!i_isolate()->heap()->Contains(obj));
    CHECK_NE(sample_object, obj);
  }
}

TEST_F(IteratorsTest, HeapObjectIterator) {
  Heap* const heap = i_isolate()->heap();
  HandleScope handle_scope(i_isolate());
  const Tagged<Object> sample_object = CreateWritableObject(v8_isolate());
  bool seen_sample_object = false;
  HeapObjectIterator iterator(heap);
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                  !ReadOnlyHeap::Contains(obj));
    CHECK(heap->Contains(obj));
    if (sample_object.SafeEquals(obj)) seen_sample_object = true;
  }
  CHECK(seen_sample_object);
}

TEST_F(IteratorsTest, CombinedHeapObjectIterator) {
  Heap* const heap = i_isolate()->heap();
  HandleScope handle_scope(i_isolate());
  const Tagged<Object> sample_object = CreateWritableObject(v8_isolate());
  bool seen_sample_object = false;
  CombinedHeapObjectIterator iterator(heap);
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    CHECK(IsValidHeapObject(heap, obj));
    if (sample_object.SafeEquals(obj)) seen_sample_object = true;
  }
  CHECK(seen_sample_object);
}

TEST_F(IteratorsTest, PagedSpaceIterator) {
  Heap* const heap = i_isolate()->heap();
  PagedSpaceIterator iterator(heap);
  CHECK_EQ(heap->old_space(), iterator.Next());
  CHECK_EQ(heap->code_space(), iterator.Next());
  CHECK_EQ(heap->trusted_space(), iterator.Next());
  for (int i = 0; i < 20; i++) {
    CHECK_NULL(iterator.Next());
  }
}

TEST_F(IteratorsTest, SpaceIterator) {
  auto* const read_only_space =
      i_isolate()->read_only_heap()->read_only_space();
  for (SpaceIterator it(i_isolate()->heap()); it.HasNext();) {
    // ReadOnlySpace is not actually a Space but is instead a BaseSpace, but
    // ensure it's not been inserted incorrectly.
    CHECK_NE(it.Next(), reinterpret_cast<BaseSpace*>(read_only_space));
  }
}

}  // namespace v8::internal::heap
