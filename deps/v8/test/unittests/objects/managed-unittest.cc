// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using ManagedTest = TestWithIsolate;

class DeleteCounter {
 public:
  static constexpr ExternalPointerTag kManagedTag = kGenericManagedTag;

  explicit DeleteCounter(int* deleted) : deleted_(deleted) { *deleted_ = 0; }
  ~DeleteCounter() { (*deleted_)++; }
  static void Deleter(void* arg) {
    delete reinterpret_cast<DeleteCounter*>(arg);
  }

 private:
  int* deleted_;
};

TEST_F(ManagedTest, GCCausesDestruction) {
  int deleted1 = 0;
  int deleted2 = 0;
  auto d2 = std::make_unique<DeleteCounter>(&deleted2);
  {
    HandleScope scope(isolate());
    USE(Managed<DeleteCounter>::From(
        isolate(), 0, std::make_shared<DeleteCounter>(&deleted1)));
  }

  // We need to invoke GC without stack, otherwise the objects may survive.
  DisableConservativeStackScanningScopeForTesting scope(isolate()->heap());
  InvokeMemoryReducingMajorGCs(isolate());

  CHECK_EQ(1, deleted1);
  CHECK_EQ(0, deleted2);
  d2.reset();
  CHECK_EQ(1, deleted2);
}

TEST_F(ManagedTest, DisposeCausesDestruction1) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = isolate()->array_buffer_allocator();

  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  isolate->Enter();
  int deleted1 = 0;
  {
    HandleScope scope(i_isolate);
    USE(Managed<DeleteCounter>::From(
        i_isolate, 0, std::make_shared<DeleteCounter>(&deleted1)));
  }
  isolate->Exit();
  isolate->Dispose();
  CHECK_EQ(1, deleted1);
}

TEST_F(ManagedTest, DisposeCausesDestruction2) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = isolate()->array_buffer_allocator();

  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  isolate->Enter();
  int deleted1 = 0;
  int deleted2 = 0;
  {
    HandleScope scope(i_isolate);
    USE(Managed<DeleteCounter>::From(
        i_isolate, 0, std::make_shared<DeleteCounter>(&deleted1)));
  }
  DeleteCounter* d2 = new DeleteCounter(&deleted2);
  ManagedPtrDestructor* destructor =
      new ManagedPtrDestructor(0, d2, DeleteCounter::Deleter);
  i_isolate->RegisterManagedPtrDestructor(destructor);

  isolate->Exit();
  isolate->Dispose();
  CHECK_EQ(1, deleted1);
  CHECK_EQ(1, deleted2);
}

TEST_F(ManagedTest, DisposeWithAnotherSharedPtr) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = isolate()->array_buffer_allocator();

  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  isolate->Enter();
  int deleted1 = 0;
  {
    auto shared = std::make_shared<DeleteCounter>(&deleted1);
    {
      HandleScope scope(i_isolate);
      USE(Managed<DeleteCounter>::From(i_isolate, 0, shared));
    }
    isolate->Exit();
    isolate->Dispose();
    CHECK_EQ(0, deleted1);
  }
  // Should be deleted after the second shared pointer is destroyed.
  CHECK_EQ(1, deleted1);
}

TEST_F(ManagedTest, DisposeAcrossIsolates) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = isolate()->array_buffer_allocator();

  int deleted = 0;

  v8::Isolate* isolate1 = v8::Isolate::New(create_params);
  Isolate* i_isolate1 = reinterpret_cast<i::Isolate*>(isolate1);
  isolate1->Enter();
  {
    HandleScope scope1(i_isolate1);
    auto handle1 = Managed<DeleteCounter>::From(
        i_isolate1, 0, std::make_shared<DeleteCounter>(&deleted));

    v8::Isolate* isolate2 = v8::Isolate::New(create_params);
    Isolate* i_isolate2 = reinterpret_cast<i::Isolate*>(isolate2);
    isolate2->Enter();
    {
      HandleScope scope(i_isolate2);
      USE(Managed<DeleteCounter>::From(i_isolate2, 0, handle1->get()));
    }
    isolate2->Exit();
    isolate2->Dispose();
    CHECK_EQ(0, deleted);
  }
  // Should be deleted after the first isolate is destroyed.
  isolate1->Exit();
  isolate1->Dispose();
  CHECK_EQ(1, deleted);
}

TEST_F(ManagedTest, CollectAcrossIsolates) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = isolate()->array_buffer_allocator();

  int deleted = 0;

  v8::Isolate* isolate1 = v8::Isolate::New(create_params);
  Isolate* i_isolate1 = reinterpret_cast<i::Isolate*>(isolate1);
  isolate1->Enter();
  {
    HandleScope scope1(i_isolate1);
    auto handle1 = Managed<DeleteCounter>::From(
        i_isolate1, 0, std::make_shared<DeleteCounter>(&deleted));

    v8::Isolate* isolate2 = v8::Isolate::New(create_params);
    Isolate* i_isolate2 = reinterpret_cast<i::Isolate*>(isolate2);
    isolate2->Enter();
    {
      HandleScope scope(i_isolate2);
      USE(Managed<DeleteCounter>::From(i_isolate2, 0, handle1->get()));
    }
    InvokeMemoryReducingMajorGCs(i_isolate2);
    CHECK_EQ(0, deleted);
    isolate2->Exit();
    isolate2->Dispose();
    CHECK_EQ(0, deleted);
  }
  // Should be deleted after the first isolate is destroyed.
  // We need to invoke GC without stack, otherwise the object may survive.
  {
    DisableConservativeStackScanningScopeForTesting scope(i_isolate1->heap());
    InvokeMemoryReducingMajorGCs(i_isolate1);
  }
  CHECK_EQ(1, deleted);
  isolate1->Exit();
  isolate1->Dispose();
  CHECK_EQ(1, deleted);
}

}  // namespace internal
}  // namespace v8
