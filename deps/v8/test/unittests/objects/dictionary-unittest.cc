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

#include "src/builtins/builtins-constructor.h"
#include "src/debug/debug.h"
#include "src/execution/execution.h"
#include "src/handles/global-handles.h"
#include "src/heap/factory.h"
#include "src/heap/spaces.h"
#include "src/init/v8.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/objects-inl.h"
#include "src/roots/roots.h"
#include "test/unittests/heap/heap-utils.h"

namespace v8 {
namespace internal {

class DictionaryTest : public TestWithHeapInternalsAndContext {
 public:
  template <typename HashMap>
  void TestHashMap(Handle<HashMap> table) {
    Factory* factory = isolate()->factory();

    Handle<JSObject> a = factory->NewJSArray(7);
    Handle<JSObject> b = factory->NewJSArray(11);
    table = HashMap::Put(table, a, b);
    CHECK_EQ(1, table->NumberOfElements());
    CHECK_EQ(table->Lookup(a), *b);
    // When the key does not exist in the map, Lookup returns the hole.
    ReadOnlyRoots roots(heap());
    CHECK_EQ(table->Lookup(b), roots.the_hole_value());

    // Keys still have to be valid after objects were moved.
    InvokeMinorGC();
    CHECK_EQ(1, table->NumberOfElements());
    CHECK_EQ(table->Lookup(a), *b);
    CHECK_EQ(table->Lookup(b), roots.the_hole_value());

    // Keys that are overwritten should not change number of elements.
    table = HashMap::Put(table, a, factory->NewJSArray(13));
    CHECK_EQ(1, table->NumberOfElements());
    CHECK_NE(table->Lookup(a), *b);

    // Keys that have been removed are mapped to the hole.
    bool was_present = false;
    table = HashMap::Remove(isolate(), table, a, &was_present);
    CHECK(was_present);
    CHECK_EQ(0, table->NumberOfElements());
    CHECK_EQ(table->Lookup(a), roots.the_hole_value());

    // Keys should map back to their respective values and also should get
    // an identity hash code generated.
    for (int i = 0; i < 100; i++) {
      Handle<JSReceiver> key = factory->NewJSArray(7);
      Handle<JSObject> value = factory->NewJSArray(11);
      table = HashMap::Put(table, key, value);
      CHECK_EQ(table->NumberOfElements(), i + 1);
      CHECK(table->FindEntry(isolate(), key).is_found());
      CHECK_EQ(table->Lookup(key), *value);
      CHECK(IsSmi(key->GetIdentityHash()));
    }

    // Keys never added to the map which already have an identity hash
    // code should not be found.
    for (int i = 0; i < 100; i++) {
      Handle<JSReceiver> key = factory->NewJSArray(7);
      CHECK(IsSmi(key->GetOrCreateIdentityHash(isolate())));
      CHECK(table->FindEntry(isolate(), key).is_not_found());
      CHECK_EQ(table->Lookup(key), roots.the_hole_value());
      CHECK(IsSmi(key->GetIdentityHash()));
    }

    // Keys that don't have an identity hash should not be found and also
    // should not get an identity hash code generated.
    for (int i = 0; i < 100; i++) {
      Handle<JSReceiver> key = factory->NewJSArray(7);
      CHECK_EQ(table->Lookup(key), roots.the_hole_value());
      Tagged<Object> identity_hash = key->GetIdentityHash();
      CHECK_EQ(roots.undefined_value(), identity_hash);
    }
  }

  template <typename HashSet>
  void TestHashSet(Handle<HashSet> table) {
    Factory* factory = isolate()->factory();

    Handle<JSObject> a = factory->NewJSArray(7);
    Handle<JSObject> b = factory->NewJSArray(11);
    table = HashSet::Add(isolate(), table, a);
    CHECK_EQ(1, table->NumberOfElements());
    CHECK(table->Has(isolate(), a));
    CHECK(!table->Has(isolate(), b));

    // Keys still have to be valid after objects were moved.
    InvokeMinorGC();
    CHECK_EQ(1, table->NumberOfElements());
    CHECK(table->Has(isolate(), a));
    CHECK(!table->Has(isolate(), b));

    // Keys that are overwritten should not change number of elements.
    table = HashSet::Add(isolate(), table, a);
    CHECK_EQ(1, table->NumberOfElements());
    CHECK(table->Has(isolate(), a));
    CHECK(!table->Has(isolate(), b));

    // Keys that have been removed are mapped to the hole.
    // TODO(cbruni): not implemented yet.
    // bool was_present = false;
    // table = HashSet::Remove(table, a, &was_present);
    // CHECK(was_present);
    // CHECK_EQ(0, table->NumberOfElements());
    // CHECK(!table->Has(a));
    // CHECK(!table->Has(b));

    // Keys should map back to their respective values and also should get
    // an identity hash code generated.
    for (int i = 0; i < 100; i++) {
      Handle<JSReceiver> key = factory->NewJSArray(7);
      table = HashSet::Add(isolate(), table, key);
      CHECK_EQ(table->NumberOfElements(), i + 2);
      CHECK(table->Has(isolate(), key));
      CHECK(IsSmi(key->GetIdentityHash()));
    }

    // Keys never added to the map which already have an identity hash
    // code should not be found.
    for (int i = 0; i < 100; i++) {
      Handle<JSReceiver> key = factory->NewJSArray(7);
      CHECK(IsSmi(key->GetOrCreateIdentityHash(isolate())));
      CHECK(!table->Has(isolate(), key));
      CHECK(IsSmi(key->GetIdentityHash()));
    }

    // Keys that don't have an identity hash should not be found and also
    // should not get an identity hash code generated.
    for (int i = 0; i < 100; i++) {
      Handle<JSReceiver> key = factory->NewJSArray(7);
      CHECK(!table->Has(isolate(), key));
      Tagged<Object> identity_hash = key->GetIdentityHash();
      CHECK_EQ(ReadOnlyRoots(heap()).undefined_value(), identity_hash);
    }
  }

#ifdef DEBUG
  template <class HashSet>
  void TestHashSetCausesGC(Handle<HashSet> table) {
    Factory* factory = isolate()->factory();

    Handle<JSObject> key = factory->NewJSArray(0);

    // Simulate a full heap so that generating an identity hash code
    // in subsequent calls will request GC.
    SimulateFullSpace(heap()->new_space());
    SimulateFullSpace(heap()->old_space());

    // Calling Contains() should not cause GC ever.
    int gc_count = heap()->gc_count();
    CHECK(!table->Contains(key));
    CHECK(gc_count == heap()->gc_count());

    // Calling Remove() will not cause GC in this case.
    bool was_present = false;
    table = HashSet::Remove(table, key, &was_present);
    CHECK(!was_present);
    CHECK(gc_count == heap()->gc_count());

    // Calling Add() should cause GC.
    table = HashSet::Add(table, key);
    CHECK(gc_count < heap()->gc_count());
  }
#endif

#ifdef DEBUG
  template <class HashMap>
  void TestHashMapDoesNotCauseGC(Handle<HashMap> table) {
    Factory* factory = isolate()->factory();

    Handle<JSObject> key = factory->NewJSArray(0);

    // Even though we simulate a full heap, generating an identity hash
    // code in subsequent calls will not request GC.
    if (!v8_flags.single_generation) {
      SimulateFullSpace(heap()->new_space());
    }
    SimulateFullSpace(heap()->old_space());

    // Calling Lookup() should not cause GC ever.
    CHECK(IsTheHole(table->Lookup(key), isolate()));

    // Calling Put() should request GC by returning a failure.
    int gc_count = heap()->gc_count();
    HashMap::Put(table, key, key);
    CHECK(gc_count == heap()->gc_count());
  }
#endif
};

TEST_F(DictionaryTest, HashMap) {
  TestHashMap(ObjectHashTable::New(isolate(), 23));
}

TEST_F(DictionaryTest, HashSet) {
  TestHashSet(ObjectHashSet::New(isolate(), 23));
}

class ObjectHashTableTest {
 public:
  explicit ObjectHashTableTest(Tagged<ObjectHashTable> o) : table_(o) {}

  // For every object, add a `->` operator which returns a pointer to this
  // object. This will allow smoother transition between T and Tagged<T>.
  ObjectHashTableTest* operator->() { return this; }
  const ObjectHashTableTest* operator->() const { return this; }

  void insert(InternalIndex entry, int key, int value) {
    table_->set(table_->EntryToIndex(entry), Smi::FromInt(key));
    table_->set(table_->EntryToIndex(entry) + 1, Smi::FromInt(value));
  }

  int lookup(int key, Isolate* isolate) {
    Handle<Object> key_obj(Smi::FromInt(key), isolate);
    return Smi::ToInt(table_->Lookup(key_obj));
  }

  int capacity() { return table_->Capacity(); }

  void Rehash(Isolate* isolate) { table_->Rehash(isolate); }

 private:
  Tagged<ObjectHashTable> table_;
};

TEST_F(DictionaryTest, HashTableRehash) {
  // Test almost filled table.
  {
    DirectHandle<ObjectHashTable> table = ObjectHashTable::New(isolate(), 100);
    ObjectHashTableTest t(*table);
    int capacity = t->capacity();
    for (int i = 0; i < capacity - 1; i++) {
      t->insert(InternalIndex(i), i * i, i);
    }
    t->Rehash(isolate());
    for (int i = 0; i < capacity - 1; i++) {
      CHECK_EQ(i, t->lookup(i * i, isolate()));
    }
  }
  // Test half-filled table.
  {
    DirectHandle<ObjectHashTable> table = ObjectHashTable::New(isolate(), 100);
    ObjectHashTableTest t(*table);
    int capacity = t->capacity();
    for (int i = 0; i < capacity / 2; i++) {
      t->insert(InternalIndex(i), i * i, i);
    }
    t->Rehash(isolate());
    for (int i = 0; i < capacity / 2; i++) {
      CHECK_EQ(i, t->lookup(i * i, isolate()));
    }
  }
}

#ifdef DEBUG
TEST_F(DictionaryTest, ObjectHashTableCausesGC) {
  i::v8_flags.stress_compaction = false;
  // For SimulateFullSpace in TestHashMapDoesNotCauseGC.
  i::v8_flags.stress_concurrent_allocation = false;
  TestHashMapDoesNotCauseGC(ObjectHashTable::New(isolate(), 1));
}
#endif

TEST_F(DictionaryTest, MaximumClonedShallowObjectProperties) {
  // Assert that a NameDictionary with kMaximumClonedShallowObjectProperties is
  // not in large-object space.
  const int max_capacity = NameDictionary::ComputeCapacity(
      ConstructorBuiltins::kMaximumClonedShallowObjectProperties);
  const InternalIndex max_literal_entry(max_capacity /
                                        NameDictionary::kEntrySize);
  const int max_literal_index = NameDictionary::EntryToIndex(max_literal_entry);
  CHECK_LE(NameDictionary::OffsetOfElementAt(max_literal_index),
           kMaxRegularHeapObjectSize);
}

}  // namespace internal
}  // namespace v8
