// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <set>
#include <vector>

#include "src/heap/heap.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using StrongRootAllocatorTest = TestWithHeapInternals;

TEST_F(StrongRootAllocatorTest, AddressRetained) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  StrongRootAllocator<Address> allocator(heap());
  Address* allocated = allocator.allocate(10);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    allocated[7] = h->ptr();
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_FALSE(weak.IsEmpty());

  allocator.deallocate(allocated, 10);

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, StructNotRetained) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  struct Wrapped {
    Address content;
  };

  StrongRootAllocator<Wrapped> allocator(heap());
  Wrapped* allocated = allocator.allocate(10);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    allocated[7].content = h->ptr();
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());

  allocator.deallocate(allocated, 10);
}

TEST_F(StrongRootAllocatorTest, VectorRetained) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  {
    StrongRootAllocator<Address> allocator(heap());
    std::vector<Address, StrongRootAllocator<Address>> v(10, allocator);

    {
      v8::HandleScope scope(v8_isolate());
      Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
      v[7] = h->ptr();
      Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
      weak.Reset(v8_isolate(), l);
      weak.SetWeak();
    }

    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
      InvokeMajorGC();
    }
    EXPECT_FALSE(weak.IsEmpty());
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, VectorOfStructNotRetained) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  struct Wrapped {
    Address content;
  };

  StrongRootAllocator<Wrapped> allocator(heap());
  std::vector<Wrapped, StrongRootAllocator<Wrapped>> v(10, allocator);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    v[7].content = h->ptr();
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, ListNotRetained) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  StrongRootAllocator<Address> allocator(heap());
  std::list<Address, StrongRootAllocator<Address>> l(allocator);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    l.push_back(h->ptr());
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, SetNotRetained) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  StrongRootAllocator<Address> allocator(heap());
  std::set<Address, std::less<Address>, StrongRootAllocator<Address>> s(
      allocator);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    s.insert(h->ptr());
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, LocalVector) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  {
    v8::HandleScope outer_scope(v8_isolate());
    // LocalVector uses the StrongRootAllocator for its backing store.
    LocalVector<v8::FixedArray> v(v8_isolate(), 10);

    {
      v8::EscapableHandleScope inner_scope(v8_isolate());
      Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
      Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
      weak.Reset(v8_isolate(), l);
      weak.SetWeak();
      v[7] = inner_scope.Escape(l);
    }

    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
      InvokeMajorGC();
    }
    EXPECT_FALSE(weak.IsEmpty());
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

#ifdef V8_ENABLE_DIRECT_HANDLE
TEST_F(StrongRootAllocatorTest, LocalVectorWithDirect) {
  ManualGCScope manual_gc_scope(i_isolate());
  Global<v8::FixedArray> weak;

  {
    // LocalVector uses the StrongRootAllocator for its backing store.
    LocalVector<v8::FixedArray> v(v8_isolate(), 10);

    {
      v8::HandleScope scope(v8_isolate());
      Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
      Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
      // This is legal without escaping, because locals are direct.
      v[7] = l;
      weak.Reset(v8_isolate(), l);
      weak.SetWeak();
    }

    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
      InvokeMajorGC();
    }
    EXPECT_FALSE(weak.IsEmpty());
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}
#endif  // V8_ENABLE_DIRECT_HANDLE

}  // namespace internal
}  // namespace v8
