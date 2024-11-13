// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <utility>

#include "src/execution/isolate.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace test_weaksets {

class WeakSetsTest : public TestWithHeapInternalsAndContext {
 public:
  Handle<JSWeakSet> AllocateJSWeakSet() {
    Factory* factory = i_isolate()->factory();
    DirectHandle<Map> map = factory->NewContextfulMapForCurrentContext(
        JS_WEAK_SET_TYPE, JSWeakSet::kHeaderSize);
    DirectHandle<JSObject> weakset_obj = factory->NewJSObjectFromMap(map);
    Handle<JSWeakSet> weakset(Cast<JSWeakSet>(*weakset_obj), i_isolate());
    // Do not leak handles for the hash table, it would make entries strong.
    {
      HandleScope scope(i_isolate());
      DirectHandle<EphemeronHashTable> table =
          EphemeronHashTable::New(i_isolate(), 1);
      weakset->set_table(*table);
    }
    return weakset;
  }
};

namespace {
static int NumberOfWeakCalls = 0;
static void WeakPointerCallback(const v8::WeakCallbackInfo<void>& data) {
  std::pair<v8::Persistent<v8::Value>*, int>* p =
      reinterpret_cast<std::pair<v8::Persistent<v8::Value>*, int>*>(
          data.GetParameter());
  CHECK_EQ(1234, p->second);
  NumberOfWeakCalls++;
  p->first->Reset();
}
}  // namespace

TEST_F(WeakSetsTest, WeakSet_Weakness) {
  v8_flags.incremental_marking = false;
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());
  IndirectHandle<JSWeakSet> weakset = AllocateJSWeakSet();
  GlobalHandles* global_handles = i_isolate()->global_handles();

  // Keep global reference to the key.
  Handle<Object> key;
  {
    HandleScope inner_scope(i_isolate());
    DirectHandle<Map> map = factory->NewContextfulMapForCurrentContext(
        JS_OBJECT_TYPE, JSObject::kHeaderSize);
    DirectHandle<JSObject> object = factory->NewJSObjectFromMap(map);
    key = global_handles->Create(*object);
  }
  CHECK(!global_handles->IsWeak(key.location()));

  // Put entry into weak set.
  {
    HandleScope inner_scope(i_isolate());
    DirectHandle<Smi> smi(Smi::FromInt(23), i_isolate());
    int32_t hash = Object::GetOrCreateHash(*key, i_isolate()).value();
    JSWeakCollection::Set(weakset, key, smi, hash);
  }
  CHECK_EQ(1, Cast<EphemeronHashTable>(weakset->table())->NumberOfElements());

  // Force a full GC.
  InvokeAtomicMajorGC();
  CHECK_EQ(0, NumberOfWeakCalls);
  CHECK_EQ(1, Cast<EphemeronHashTable>(weakset->table())->NumberOfElements());
  CHECK_EQ(
      0, Cast<EphemeronHashTable>(weakset->table())->NumberOfDeletedElements());

  // Make the global reference to the key weak.
  std::pair<Handle<Object>*, int> handle_and_id(&key, 1234);
  GlobalHandles::MakeWeak(
      key.location(), reinterpret_cast<void*>(&handle_and_id),
      &WeakPointerCallback, v8::WeakCallbackType::kParameter);
  CHECK(global_handles->IsWeak(key.location()));

  // We need to invoke GC without stack here, otherwise the object may survive.
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      isolate()->heap());
  InvokeAtomicMajorGC();
  CHECK_EQ(1, NumberOfWeakCalls);
  CHECK_EQ(0, Cast<EphemeronHashTable>(weakset->table())->NumberOfElements());
  CHECK_EQ(
      1, Cast<EphemeronHashTable>(weakset->table())->NumberOfDeletedElements());
}

TEST_F(WeakSetsTest, WeakSet_Shrinking) {
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());
  DirectHandle<JSWeakSet> weakset = AllocateJSWeakSet();

  // Check initial capacity.
  CHECK_EQ(32, Cast<EphemeronHashTable>(weakset->table())->Capacity());

  // Fill up weak set to trigger capacity change.
  {
    HandleScope inner_scope(i_isolate());
    DirectHandle<Map> map = factory->NewContextfulMapForCurrentContext(
        JS_OBJECT_TYPE, JSObject::kHeaderSize);
    for (int i = 0; i < 32; i++) {
      Handle<JSObject> object = factory->NewJSObjectFromMap(map);
      DirectHandle<Smi> smi(Smi::FromInt(i), i_isolate());
      int32_t hash = Object::GetOrCreateHash(*object, i_isolate()).value();
      JSWeakCollection::Set(weakset, object, smi, hash);
    }
  }

  // Check increased capacity.
  CHECK_EQ(128, Cast<EphemeronHashTable>(weakset->table())->Capacity());

  // Force a full GC.
  CHECK_EQ(32, Cast<EphemeronHashTable>(weakset->table())->NumberOfElements());
  CHECK_EQ(
      0, Cast<EphemeronHashTable>(weakset->table())->NumberOfDeletedElements());
  InvokeAtomicMajorGC();
  CHECK_EQ(0, Cast<EphemeronHashTable>(weakset->table())->NumberOfElements());
  CHECK_EQ(
      32,
      Cast<EphemeronHashTable>(weakset->table())->NumberOfDeletedElements());

  // Check shrunk capacity.
  CHECK_EQ(32, Cast<EphemeronHashTable>(weakset->table())->Capacity());
}

// Test that weak set values on an evacuation candidate which are not reachable
// by other paths are correctly recorded in the slots buffer.
TEST_F(WeakSetsTest, WeakSet_Regress2060a) {
  if (!i::v8_flags.compact) return;
  if (i::v8_flags.enable_third_party_heap) return;
  v8_flags.compact_on_every_full_gc = true;
  v8_flags.stress_concurrent_allocation = false;  // For SimulateFullSpace.
  ManualGCScope manual_gc_scope(i_isolate());
  Factory* factory = i_isolate()->factory();
  Heap* heap = i_isolate()->heap();
  HandleScope scope(i_isolate());
  Handle<JSFunction> function =
      factory->NewFunctionForTesting(factory->function_string());
  Handle<JSObject> key = factory->NewJSObject(function);
  DirectHandle<JSWeakSet> weakset = AllocateJSWeakSet();

  // Start second old-space page so that values land on evacuation candidate.
  PageMetadata* first_page = heap->old_space()->first_page();
  SimulateFullSpace(heap->old_space());

  // Fill up weak set with values on an evacuation candidate.
  {
    HandleScope inner_scope(i_isolate());
    for (int i = 0; i < 32; i++) {
      DirectHandle<JSObject> object =
          factory->NewJSObject(function, AllocationType::kOld);
      CHECK(!Heap::InYoungGeneration(*object));
      CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                    !first_page->Contains(object->address()));
      int32_t hash = Object::GetOrCreateHash(*key, i_isolate()).value();
      JSWeakCollection::Set(weakset, key, object, hash);
    }
  }

  // Force compacting garbage collection.
  CHECK(v8_flags.compact_on_every_full_gc);
  // We need to invoke GC without stack, otherwise no compaction is performed.
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
  InvokeMajorGC();
}

// Test that weak set keys on an evacuation candidate which are reachable by
// other strong paths are correctly recorded in the slots buffer.
TEST_F(WeakSetsTest, WeakSet_Regress2060b) {
  if (!i::v8_flags.compact) return;
  if (i::v8_flags.enable_third_party_heap) return;
  v8_flags.compact_on_every_full_gc = true;
#ifdef VERIFY_HEAP
  v8_flags.verify_heap = true;
#endif
  v8_flags.stress_concurrent_allocation = false;  // For SimulateFullSpace.

  ManualGCScope manual_gc_scope(i_isolate());
  Factory* factory = i_isolate()->factory();
  Heap* heap = i_isolate()->heap();
  HandleScope scope(i_isolate());
  Handle<JSFunction> function =
      factory->NewFunctionForTesting(factory->function_string());

  // Start second old-space page so that keys land on evacuation candidate.
  PageMetadata* first_page = heap->old_space()->first_page();
  SimulateFullSpace(heap->old_space());

  // Fill up weak set with keys on an evacuation candidate.
  Handle<JSObject> keys[32];
  for (int i = 0; i < 32; i++) {
    keys[i] = factory->NewJSObject(function, AllocationType::kOld);
    CHECK(!Heap::InYoungGeneration(*keys[i]));
    CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                  !first_page->Contains(keys[i]->address()));
  }
  DirectHandle<JSWeakSet> weakset = AllocateJSWeakSet();
  for (int i = 0; i < 32; i++) {
    DirectHandle<Smi> smi(Smi::FromInt(i), i_isolate());
    int32_t hash = Object::GetOrCreateHash(*keys[i], i_isolate()).value();
    JSWeakCollection::Set(weakset, keys[i], smi, hash);
  }

  // Force compacting garbage collection. The subsequent collections are used
  // to verify that key references were actually updated.
  CHECK(v8_flags.compact_on_every_full_gc);
  // We need to invoke GC without stack, otherwise no compaction is performed.
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
  InvokeMajorGC();
  InvokeMajorGC();
  InvokeMajorGC();
}

}  // namespace test_weaksets
}  // namespace internal
}  // namespace v8
