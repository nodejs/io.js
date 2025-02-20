// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <utility>

#include "src/objects/objects-inl.h"
#include "src/objects/ordered-hash-table-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace test_orderedhashtable {

static Isolate* GetIsolateFrom(LocalContext* context) {
  return reinterpret_cast<Isolate*>((*context)->GetIsolate());
}

void CopyHashCode(DirectHandle<JSReceiver> from, DirectHandle<JSReceiver> to) {
  int hash = Smi::ToInt(Object::GetHash(*from));
  to->SetIdentityHash(hash);
}

void Verify(Isolate* isolate, DirectHandle<HeapObject> obj) {
#if VERIFY_HEAP
  Object::ObjectVerify(*obj, isolate);
#endif
}

// Helpers to abstract over differences in interfaces of the different ordered
// datastructures

template <typename T>
Handle<T> Add(Isolate* isolate, Handle<T> table, Handle<String> key1,
              Handle<String> value1, PropertyDetails details);

template <>
Handle<OrderedHashMap> Add(Isolate* isolate, Handle<OrderedHashMap> table,
                           Handle<String> key, Handle<String> value,
                           PropertyDetails details) {
  return OrderedHashMap::Add(isolate, table, key, value).ToHandleChecked();
}

template <>
Handle<OrderedHashSet> Add(Isolate* isolate, Handle<OrderedHashSet> table,
                           Handle<String> key, Handle<String> value,
                           PropertyDetails details) {
  return OrderedHashSet::Add(isolate, table, key).ToHandleChecked();
}

template <>
Handle<OrderedNameDictionary> Add(Isolate* isolate,
                                  Handle<OrderedNameDictionary> table,
                                  Handle<String> key, Handle<String> value,
                                  PropertyDetails details) {
  return OrderedNameDictionary::Add(isolate, table, key, value, details)
      .ToHandleChecked();
}

// version for
// OrderedHashMap, OrderedHashSet
template <typename T, template <typename> typename HandleType>
  requires(std::is_convertible_v<HandleType<T>, DirectHandle<T>>)
bool HasKey(Isolate* isolate, HandleType<T> table, Tagged<Object> key) {
  return T::HasKey(isolate, *table, key);
}

template <template <typename> typename HandleType>
  requires(std::is_convertible_v<HandleType<OrderedNameDictionary>,
                                 DirectHandle<OrderedNameDictionary>>)
bool HasKey(Isolate* isolate, HandleType<OrderedNameDictionary> table,
            Tagged<Object> key) {
  return table->FindEntry(isolate, key).is_found();
}

// version for
// OrderedHashTable, OrderedHashSet
template <typename T>
Handle<T> Delete(Isolate* isolate, Handle<T> table, Tagged<Object> key) {
  T::Delete(isolate, *table, key);
  return table;
}

template <>
Handle<OrderedNameDictionary> Delete(Isolate* isolate,
                                     Handle<OrderedNameDictionary> table,
                                     Tagged<Object> key) {
  // OrderedNameDictionary doesn't have Delete, but only DeleteEntry, which
  // requires the key to be deleted to be present
  InternalIndex entry = table->FindEntry(isolate, key);
  if (entry.is_not_found()) return table;

  return OrderedNameDictionary::DeleteEntry(isolate, table, entry);
}

TEST(SmallOrderedHashSetInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());

  // Add a new key.
  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!set->HasKey(isolate, key1));
  set = SmallOrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));

  // Add existing key.
  set = SmallOrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));

  DirectHandle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  CHECK(!set->HasKey(isolate, key2));
  set = SmallOrderedHashSet::Add(isolate, set, key2).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));

  set = SmallOrderedHashSet::Add(isolate, set, key2).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));

  DirectHandle<Symbol> key3 = factory->NewSymbol();
  CHECK(!set->HasKey(isolate, key3));
  set = SmallOrderedHashSet::Add(isolate, set, key3).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(3, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));

  set = SmallOrderedHashSet::Add(isolate, set, key3).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(3, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));

  DirectHandle<Object> key4 = factory->NewHeapNumber(42.0);
  CHECK(!set->HasKey(isolate, key4));
  set = SmallOrderedHashSet::Add(isolate, set, key4).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(4, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));
  CHECK(set->HasKey(isolate, key4));

  set = SmallOrderedHashSet::Add(isolate, set, key4).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(4, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));
  CHECK(set->HasKey(isolate, key4));
}

TEST(SmallOrderedHashMapInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashMap> map = factory->NewSmallOrderedHashMap();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());

  // Add a new key.
  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);
  CHECK(!map->HasKey(isolate, key1));
  map = SmallOrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));

  // Add existing key.
  map = SmallOrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));

  DirectHandle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  DirectHandle<String> value = factory->NewStringFromAsciiChecked("foo");
  CHECK(!map->HasKey(isolate, key2));
  map = SmallOrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));

  map = SmallOrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));

  DirectHandle<Symbol> key3 = factory->NewSymbol();
  CHECK(!map->HasKey(isolate, key3));
  map = SmallOrderedHashMap::Add(isolate, map, key3, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(3, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));
  CHECK(map->HasKey(isolate, key3));

  map = SmallOrderedHashMap::Add(isolate, map, key3, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(3, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));
  CHECK(map->HasKey(isolate, key3));

  DirectHandle<Object> key4 = factory->NewHeapNumber(42.0);
  CHECK(!map->HasKey(isolate, key4));
  map = SmallOrderedHashMap::Add(isolate, map, key4, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(4, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));
  CHECK(map->HasKey(isolate, key3));
  CHECK(map->HasKey(isolate, key4));

  map = SmallOrderedHashMap::Add(isolate, map, key4, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(4, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));
  CHECK(map->HasKey(isolate, key3));
  CHECK(map->HasKey(isolate, key4));
}

TEST(SmallOrderedHashSetDuplicateHashCode) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  DirectHandle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  set = SmallOrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));

  DirectHandle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  CopyHashCode(key1, key2);

  set = SmallOrderedHashSet::Add(isolate, set, key2).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
}

TEST(SmallOrderedHashMapDuplicateHashCode) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashMap> map = factory->NewSmallOrderedHashMap();
  DirectHandle<JSObject> value = factory->NewJSObjectWithNullProto();
  DirectHandle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  map = SmallOrderedHashMap::Add(isolate, map, key1, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));

  DirectHandle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  CopyHashCode(key1, key2);

  CHECK(!Object::SameValue(*key1, *key2));
  Tagged<Object> hash1 = Object::GetHash(*key1);
  Tagged<Object> hash2 = Object::GetHash(*key2);
  CHECK_EQ(hash1, hash2);

  map = SmallOrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));
}

TEST(SmallOrderedHashSetGrow) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  std::vector<Handle<Object>> keys;
  for (int i = 0; i < 254; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 0; i < 4; i++) {
    set = SmallOrderedHashSet::Add(isolate, set, keys[i]).ToHandleChecked();
    Verify(isolate, set);
  }

  for (size_t i = 0; i < 4; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(isolate, set);
  }

  CHECK_EQ(4, set->NumberOfElements());
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(isolate, set);

  for (size_t i = 4; i < 8; i++) {
    set = SmallOrderedHashSet::Add(isolate, set, keys[i]).ToHandleChecked();
    Verify(isolate, set);
  }

  for (size_t i = 0; i < 8; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(isolate, set);
  }

  CHECK_EQ(8, set->NumberOfElements());
  CHECK_EQ(4, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(isolate, set);

  for (size_t i = 8; i < 16; i++) {
    set = SmallOrderedHashSet::Add(isolate, set, keys[i]).ToHandleChecked();
    Verify(isolate, set);
  }

  for (size_t i = 0; i < 16; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(isolate, set);
  }

  CHECK_EQ(16, set->NumberOfElements());
  CHECK_EQ(8, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(isolate, set);

  for (size_t i = 16; i < 32; i++) {
    set = SmallOrderedHashSet::Add(isolate, set, keys[i]).ToHandleChecked();
    Verify(isolate, set);
  }

  for (size_t i = 0; i < 32; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(isolate, set);
  }

  CHECK_EQ(32, set->NumberOfElements());
  CHECK_EQ(16, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(isolate, set);

  for (size_t i = 32; i < 64; i++) {
    set = SmallOrderedHashSet::Add(isolate, set, keys[i]).ToHandleChecked();
    Verify(isolate, set);
  }

  for (size_t i = 0; i < 64; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(isolate, set);
  }

  CHECK_EQ(64, set->NumberOfElements());
  CHECK_EQ(32, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(isolate, set);

  for (size_t i = 64; i < 128; i++) {
    set = SmallOrderedHashSet::Add(isolate, set, keys[i]).ToHandleChecked();
    Verify(isolate, set);
  }

  for (size_t i = 0; i < 128; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(isolate, set);
  }

  CHECK_EQ(128, set->NumberOfElements());
  CHECK_EQ(64, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(isolate, set);

  for (size_t i = 128; i < 254; i++) {
    set = SmallOrderedHashSet::Add(isolate, set, keys[i]).ToHandleChecked();
    Verify(isolate, set);
  }

  for (size_t i = 0; i < 254; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(isolate, set);
  }

  CHECK_EQ(254, set->NumberOfElements());
  CHECK_EQ(127, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(isolate, set);
}

TEST(SmallOrderedHashMapGrow) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashMap> map = factory->NewSmallOrderedHashMap();
  std::vector<Handle<Object>> keys;
  for (int i = 0; i < 254; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 0; i < 4; i++) {
    map = SmallOrderedHashMap::Add(isolate, map, keys[i], keys[i])
              .ToHandleChecked();
    Verify(isolate, map);
  }

  for (size_t i = 0; i < 4; i++) {
    CHECK(map->HasKey(isolate, keys[i]));
    Verify(isolate, map);
  }

  CHECK_EQ(4, map->NumberOfElements());
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  Verify(isolate, map);

  for (size_t i = 4; i < 8; i++) {
    map = SmallOrderedHashMap::Add(isolate, map, keys[i], keys[i])
              .ToHandleChecked();
    Verify(isolate, map);
  }

  for (size_t i = 0; i < 8; i++) {
    CHECK(map->HasKey(isolate, keys[i]));
    Verify(isolate, map);
  }

  CHECK_EQ(8, map->NumberOfElements());
  CHECK_EQ(4, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  Verify(isolate, map);

  for (size_t i = 8; i < 16; i++) {
    map = SmallOrderedHashMap::Add(isolate, map, keys[i], keys[i])
              .ToHandleChecked();
    Verify(isolate, map);
  }

  for (size_t i = 0; i < 16; i++) {
    CHECK(map->HasKey(isolate, keys[i]));
    Verify(isolate, map);
  }

  CHECK_EQ(16, map->NumberOfElements());
  CHECK_EQ(8, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  Verify(isolate, map);

  for (size_t i = 16; i < 32; i++) {
    map = SmallOrderedHashMap::Add(isolate, map, keys[i], keys[i])
              .ToHandleChecked();
    Verify(isolate, map);
  }

  for (size_t i = 0; i < 32; i++) {
    CHECK(map->HasKey(isolate, keys[i]));
    Verify(isolate, map);
  }

  CHECK_EQ(32, map->NumberOfElements());
  CHECK_EQ(16, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  Verify(isolate, map);

  for (size_t i = 32; i < 64; i++) {
    map = SmallOrderedHashMap::Add(isolate, map, keys[i], keys[i])
              .ToHandleChecked();
    Verify(isolate, map);
  }

  for (size_t i = 0; i < 64; i++) {
    CHECK(map->HasKey(isolate, keys[i]));
    Verify(isolate, map);
  }

  CHECK_EQ(64, map->NumberOfElements());
  CHECK_EQ(32, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  Verify(isolate, map);

  for (size_t i = 64; i < 128; i++) {
    map = SmallOrderedHashMap::Add(isolate, map, keys[i], keys[i])
              .ToHandleChecked();
    Verify(isolate, map);
  }

  for (size_t i = 0; i < 128; i++) {
    CHECK(map->HasKey(isolate, keys[i]));
    Verify(isolate, map);
  }

  CHECK_EQ(128, map->NumberOfElements());
  CHECK_EQ(64, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  Verify(isolate, map);

  for (size_t i = 128; i < 254; i++) {
    map = SmallOrderedHashMap::Add(isolate, map, keys[i], keys[i])
              .ToHandleChecked();
    Verify(isolate, map);
  }

  for (size_t i = 0; i < 254; i++) {
    CHECK(map->HasKey(isolate, keys[i]));
    Verify(isolate, map);
  }

  CHECK_EQ(254, map->NumberOfElements());
  CHECK_EQ(127, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  Verify(isolate, map);
}

TEST(OrderedHashTableInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedHashMap> map = factory->NewOrderedHashMap();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());

  // Add a new key.
  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));
  map = OrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));

  // Add existing key.
  map = OrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));

  DirectHandle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  DirectHandle<String> value = factory->NewStringFromAsciiChecked("bar");
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
  map = OrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));

  map = OrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));

  DirectHandle<Symbol> key3 = factory->NewSymbol();
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key3));
  map = OrderedHashMap::Add(isolate, map, key3, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(3, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key3));

  map = OrderedHashMap::Add(isolate, map, key3, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(3, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key3));

  DirectHandle<Object> key4 = factory->NewHeapNumber(42.0);
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key4));
  map = OrderedHashMap::Add(isolate, map, key4, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(4, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key3));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key4));

  map = OrderedHashMap::Add(isolate, map, key4, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(4, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key3));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key4));
}

TEST(OrderedHashMapDuplicateHashCode) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedHashMap> map = factory->NewOrderedHashMap();
  DirectHandle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  DirectHandle<JSObject> value = factory->NewJSObjectWithNullProto();
  map = OrderedHashMap::Add(isolate, map, key1, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));

  DirectHandle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  CopyHashCode(key1, key2);

  map = OrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));
}

TEST(OrderedHashMapDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);
  DirectHandle<String> value = factory->NewStringFromAsciiChecked("bar");

  Handle<OrderedHashMap> map = factory->NewOrderedHashMap();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());

  // Delete from an empty hash table
  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!OrderedHashMap::Delete(isolate, *map, *key1));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));

  map = OrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));

  // Delete single existing key
  CHECK(OrderedHashMap::Delete(isolate, *map, *key1));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));

  map = OrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));

  DirectHandle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
  map = OrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));

  DirectHandle<Symbol> key3 = factory->NewSymbol();
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key3));
  map = OrderedHashMap::Add(isolate, map, key3, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(3, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key3));

  // Delete multiple existing keys
  CHECK(OrderedHashMap::Delete(isolate, *map, *key1));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK_EQ(2, map->NumberOfDeletedElements());
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key3));

  CHECK(OrderedHashMap::Delete(isolate, *map, *key2));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(3, map->NumberOfDeletedElements());
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key3));

  CHECK(OrderedHashMap::Delete(isolate, *map, *key3));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(4, map->NumberOfDeletedElements());
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key3));

  // Delete non existent key from non new hash table
  CHECK(!OrderedHashMap::Delete(isolate, *map, *key3));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(4, map->NumberOfDeletedElements());
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key3));

  // Delete non existent key from non empty hash table
  map = OrderedHashMap::Shrink(isolate, map);
  map = OrderedHashMap::Add(isolate, map, key1, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key3));
  CHECK(!OrderedHashMap::Delete(isolate, *map, *key2));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key3));
}

TEST(SmallOrderedHashMapDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);
  DirectHandle<String> value = factory->NewStringFromAsciiChecked("bar");

  Handle<SmallOrderedHashMap> map = factory->NewSmallOrderedHashMap();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());

  // Delete from an empty hash table
  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!SmallOrderedHashMap::Delete(isolate, *map, *key1));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(!map->HasKey(isolate, key1));

  map = SmallOrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key1));

  // Delete single existing key
  CHECK(SmallOrderedHashMap::Delete(isolate, *map, *key1));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(!map->HasKey(isolate, key1));

  map = SmallOrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key1));

  DirectHandle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  CHECK(!map->HasKey(isolate, key2));
  map = SmallOrderedHashMap::Add(isolate, map, key2, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key2));

  DirectHandle<Symbol> key3 = factory->NewSymbol();
  CHECK(!map->HasKey(isolate, key3));
  map = SmallOrderedHashMap::Add(isolate, map, key3, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(3, map->NumberOfElements());
  CHECK_EQ(1, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));
  CHECK(map->HasKey(isolate, key3));

  // Delete multiple existing keys
  CHECK(SmallOrderedHashMap::Delete(isolate, *map, *key1));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(2, map->NumberOfElements());
  CHECK_EQ(2, map->NumberOfDeletedElements());
  CHECK(!map->HasKey(isolate, key1));
  CHECK(map->HasKey(isolate, key2));
  CHECK(map->HasKey(isolate, key3));

  CHECK(SmallOrderedHashMap::Delete(isolate, *map, *key2));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(3, map->NumberOfDeletedElements());
  CHECK(!map->HasKey(isolate, key1));
  CHECK(!map->HasKey(isolate, key2));
  CHECK(map->HasKey(isolate, key3));

  CHECK(SmallOrderedHashMap::Delete(isolate, *map, *key3));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(4, map->NumberOfDeletedElements());
  CHECK(!map->HasKey(isolate, key1));
  CHECK(!map->HasKey(isolate, key2));
  CHECK(!map->HasKey(isolate, key3));

  // Delete non existent key from non new hash table
  CHECK(!SmallOrderedHashMap::Delete(isolate, *map, *key3));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(0, map->NumberOfElements());
  CHECK_EQ(4, map->NumberOfDeletedElements());
  CHECK(!map->HasKey(isolate, key1));
  CHECK(!map->HasKey(isolate, key2));
  CHECK(!map->HasKey(isolate, key3));

  // Delete non existent key from non empty hash table
  map = SmallOrderedHashMap::Add(isolate, map, key1, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(!map->HasKey(isolate, key2));
  CHECK(!map->HasKey(isolate, key3));
  CHECK(!SmallOrderedHashMap::Delete(isolate, *map, *key2));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(!map->HasKey(isolate, key2));
  CHECK(!map->HasKey(isolate, key3));
}

TEST(OrderedHashMapDuplicateHashCodeDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedHashMap> map = factory->NewOrderedHashMap();
  DirectHandle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  DirectHandle<JSObject> value = factory->NewJSObjectWithNullProto();
  map = OrderedHashMap::Add(isolate, map, key1, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));

  DirectHandle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  CopyHashCode(key1, key2);

  // We shouldn't be able to delete the key!
  CHECK(!OrderedHashMap::Delete(isolate, *map, *key2));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  CHECK(!OrderedHashMap::HasKey(isolate, *map, *key2));
}

TEST(SmallOrderedHashMapDuplicateHashCodeDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashMap> map = factory->NewSmallOrderedHashMap();
  DirectHandle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  DirectHandle<JSObject> value = factory->NewJSObjectWithNullProto();
  map = SmallOrderedHashMap::Add(isolate, map, key1, value).ToHandleChecked();
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key1));

  DirectHandle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  CopyHashCode(key1, key2);

  // We shouldn't be able to delete the key!
  CHECK(!SmallOrderedHashMap::Delete(isolate, *map, *key2));
  Verify(isolate, map);
  CHECK_EQ(2, map->NumberOfBuckets());
  CHECK_EQ(1, map->NumberOfElements());
  CHECK_EQ(0, map->NumberOfDeletedElements());
  CHECK(map->HasKey(isolate, key1));
  CHECK(!map->HasKey(isolate, key2));
}

TEST(OrderedHashSetDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedHashSet> set = factory->NewOrderedHashSet();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());

  // Delete from an empty hash table
  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!OrderedHashSet::Delete(isolate, *set, *key1));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key1));

  set = OrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));

  // Delete single existing key
  CHECK(OrderedHashSet::Delete(isolate, *set, *key1));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key1));

  set = OrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));

  DirectHandle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key2));
  set = OrderedHashSet::Add(isolate, set, key2).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key2));

  DirectHandle<Symbol> key3 = factory->NewSymbol();
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key3));
  set = OrderedHashSet::Add(isolate, set, key3).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(3, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key2));
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key3));

  // Delete multiple existing keys
  CHECK(OrderedHashSet::Delete(isolate, *set, *key1));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK_EQ(2, set->NumberOfDeletedElements());
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key2));
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key3));

  CHECK(OrderedHashSet::Delete(isolate, *set, *key2));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(3, set->NumberOfDeletedElements());
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key2));
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key3));

  CHECK(OrderedHashSet::Delete(isolate, *set, *key3));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(4, set->NumberOfDeletedElements());
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key2));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key3));

  // Delete non existent key from non new hash table
  CHECK(!OrderedHashSet::Delete(isolate, *set, *key3));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(4, set->NumberOfDeletedElements());
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key2));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key3));

  // Delete non existent key from non empty hash table
  set = OrderedHashSet::Shrink(isolate, set);
  set = OrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key2));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key3));
  CHECK(!OrderedHashSet::Delete(isolate, *set, *key2));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key2));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key3));
}

TEST(SmallOrderedHashSetDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());

  // Delete from an empty hash table
  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!SmallOrderedHashSet::Delete(isolate, *set, *key1));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(!set->HasKey(isolate, key1));

  set = SmallOrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key1));

  // Delete single existing key
  CHECK(SmallOrderedHashSet::Delete(isolate, *set, *key1));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(!set->HasKey(isolate, key1));

  set = SmallOrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key1));

  DirectHandle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  CHECK(!set->HasKey(isolate, key2));
  set = SmallOrderedHashSet::Add(isolate, set, key2).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key2));

  DirectHandle<Symbol> key3 = factory->NewSymbol();
  CHECK(!set->HasKey(isolate, key3));
  set = SmallOrderedHashSet::Add(isolate, set, key3).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(3, set->NumberOfElements());
  CHECK_EQ(1, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));

  // Delete multiple existing keys
  CHECK(SmallOrderedHashSet::Delete(isolate, *set, *key1));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK_EQ(2, set->NumberOfDeletedElements());
  CHECK(!set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));

  CHECK(SmallOrderedHashSet::Delete(isolate, *set, *key2));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(3, set->NumberOfDeletedElements());
  CHECK(!set->HasKey(isolate, key1));
  CHECK(!set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));

  CHECK(SmallOrderedHashSet::Delete(isolate, *set, *key3));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(4, set->NumberOfDeletedElements());
  CHECK(!set->HasKey(isolate, key1));
  CHECK(!set->HasKey(isolate, key2));
  CHECK(!set->HasKey(isolate, key3));

  // Delete non existent key from non new hash table
  CHECK(!SmallOrderedHashSet::Delete(isolate, *set, *key3));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());
  CHECK_EQ(4, set->NumberOfDeletedElements());
  CHECK(!set->HasKey(isolate, key1));
  CHECK(!set->HasKey(isolate, key2));
  CHECK(!set->HasKey(isolate, key3));

  // Delete non existent key from non empty hash table
  set = SmallOrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(!set->HasKey(isolate, key2));
  CHECK(!set->HasKey(isolate, key3));
  CHECK(!SmallOrderedHashSet::Delete(isolate, *set, *key2));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(!set->HasKey(isolate, key2));
  CHECK(!set->HasKey(isolate, key3));
}

TEST(OrderedHashSetDuplicateHashCodeDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedHashSet> set = factory->NewOrderedHashSet();
  DirectHandle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  set = OrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));

  DirectHandle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  CopyHashCode(key1, key2);

  // We shouldn't be able to delete the key!
  CHECK(!OrderedHashSet::Delete(isolate, *set, *key2));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));
  CHECK(!OrderedHashSet::HasKey(isolate, *set, *key2));
}

TEST(SmallOrderedHashSetDuplicateHashCodeDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  DirectHandle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  set = SmallOrderedHashSet::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key1));

  DirectHandle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  CopyHashCode(key1, key2);

  // We shouldn't be able to delete the key!
  CHECK(!SmallOrderedHashSet::Delete(isolate, *set, *key2));
  Verify(isolate, set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(!set->HasKey(isolate, key2));
}

TEST(OrderedHashSetHandlerInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

  Handle<HeapObject> set =
      OrderedHashSetHandler::Allocate(isolate, 4).ToHandleChecked();
  Verify(isolate, set);

  // Add a new key.
  Handle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!OrderedHashSetHandler::HasKey(isolate, set, key1));
  set = OrderedHashSetHandler::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK(OrderedHashSetHandler::HasKey(isolate, set, key1));

  // Add existing key.
  set = OrderedHashSetHandler::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK(OrderedHashSetHandler::HasKey(isolate, set, key1));
  CHECK(SmallOrderedHashSet::Is(set));

  for (int i = 0; i < 1024; i++) {
    DirectHandle<Smi> key_i(Smi::FromInt(i), isolate);
    set = OrderedHashSetHandler::Add(isolate, set, key_i).ToHandleChecked();
    Verify(isolate, set);
    for (int j = 0; j <= i; j++) {
      Handle<Smi> key_j(Smi::FromInt(j), isolate);
      CHECK(OrderedHashSetHandler::HasKey(isolate, set, key_j));
    }
  }
  CHECK(OrderedHashSet::Is(set));
}

TEST(OrderedHashMapHandlerInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

  Handle<HeapObject> map =
      OrderedHashMapHandler::Allocate(isolate, 4).ToHandleChecked();
  Verify(isolate, map);

  // Add a new key.
  Handle<Smi> key1(Smi::FromInt(1), isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);
  CHECK(!OrderedHashMapHandler::HasKey(isolate, map, key1));
  map =
      OrderedHashMapHandler::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK(OrderedHashMapHandler::HasKey(isolate, map, key1));

  // Add existing key.
  map =
      OrderedHashMapHandler::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK(OrderedHashMapHandler::HasKey(isolate, map, key1));
  CHECK(SmallOrderedHashMap::Is(map));

  for (int i = 0; i < 1024; i++) {
    DirectHandle<Smi> key_i(Smi::FromInt(i), isolate);
    DirectHandle<Smi> value_i(Smi::FromInt(i), isolate);
    map = OrderedHashMapHandler::Add(isolate, map, key_i, value_i)
              .ToHandleChecked();
    Verify(isolate, map);
    for (int j = 0; j <= i; j++) {
      Handle<Smi> key_j(Smi::FromInt(j), isolate);
      CHECK(OrderedHashMapHandler::HasKey(isolate, map, key_j));
    }
  }
  CHECK(OrderedHashMap::Is(map));
}

TEST(OrderedHashSetHandlerDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

  Handle<HeapObject> set =
      OrderedHashSetHandler::Allocate(isolate, 4).ToHandleChecked();
  Verify(isolate, set);

  // Add a new key.
  Handle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!OrderedHashSetHandler::HasKey(isolate, set, key1));
  set = OrderedHashSetHandler::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK(OrderedHashSetHandler::HasKey(isolate, set, key1));

  // Add existing key.
  set = OrderedHashSetHandler::Add(isolate, set, key1).ToHandleChecked();
  Verify(isolate, set);
  CHECK(OrderedHashSetHandler::HasKey(isolate, set, key1));
  CHECK(SmallOrderedHashSet::Is(set));

  // Remove a non-existing key.
  Handle<Smi> key2(Smi::FromInt(2), isolate);
  OrderedHashSetHandler::Delete(isolate, set, key2);
  Verify(isolate, set);
  CHECK(OrderedHashSetHandler::HasKey(isolate, set, key1));
  CHECK(!OrderedHashSetHandler::HasKey(isolate, set, key2));
  CHECK(SmallOrderedHashSet::Is(set));

  // Remove an existing key.
  OrderedHashSetHandler::Delete(isolate, set, key1);
  Verify(isolate, set);
  CHECK(!OrderedHashSetHandler::HasKey(isolate, set, key1));
  CHECK(SmallOrderedHashSet::Is(set));
}

TEST(OrderedHashMapHandlerDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

  Handle<HeapObject> map =
      OrderedHashMapHandler::Allocate(isolate, 4).ToHandleChecked();
  Verify(isolate, map);

  // Add a new key.
  Handle<Smi> key1(Smi::FromInt(1), isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);
  CHECK(!OrderedHashMapHandler::HasKey(isolate, map, key1));
  map =
      OrderedHashMapHandler::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK(OrderedHashMapHandler::HasKey(isolate, map, key1));

  // Add existing key.
  map =
      OrderedHashMapHandler::Add(isolate, map, key1, value1).ToHandleChecked();
  Verify(isolate, map);
  CHECK(OrderedHashMapHandler::HasKey(isolate, map, key1));
  CHECK(SmallOrderedHashMap::Is(map));

  // Remove a non-existing key.
  Handle<Smi> key2(Smi::FromInt(2), isolate);
  OrderedHashMapHandler::Delete(isolate, map, key2);
  Verify(isolate, map);
  CHECK(OrderedHashMapHandler::HasKey(isolate, map, key1));
  CHECK(!OrderedHashMapHandler::HasKey(isolate, map, key2));
  CHECK(SmallOrderedHashMap::Is(map));

  // Remove an existing key.
  OrderedHashMapHandler::Delete(isolate, map, key1);
  Verify(isolate, map);
  CHECK(!OrderedHashMapHandler::HasKey(isolate, map, key1));
  CHECK(SmallOrderedHashMap::Is(map));
}

TEST(OrderedNameDictionaryInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedNameDictionary> dict =
      OrderedNameDictionary::Allocate(isolate, 2).ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key1).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = OrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());

  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  CHECK(dict->FindEntry(isolate, *key2).is_not_found());
  dict = OrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));
  CHECK_EQ(InternalIndex(1), dict->FindEntry(isolate, *key2));
}

TEST(OrderedNameDictionaryFindEntry) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedNameDictionary> dict =
      OrderedNameDictionary::Allocate(isolate, 2).ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  PropertyDetails details = PropertyDetails::Empty();
  dict = OrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());

  InternalIndex entry = dict->FindEntry(isolate, *key1);
  CHECK_EQ(entry, InternalIndex(0));
  CHECK(entry.is_found());

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  dict = OrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());

  entry = dict->FindEntry(isolate, *key1);
  CHECK(entry.is_found());
  CHECK_EQ(entry, InternalIndex(0));

  entry = dict->FindEntry(isolate, *key2);
  CHECK(entry.is_found());
  CHECK_EQ(entry, InternalIndex(1));
}

TEST(OrderedNameDictionaryValueAtAndValueAtPut) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedNameDictionary> dict =
      OrderedNameDictionary::Allocate(isolate, 2).ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key1).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = OrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));

  InternalIndex entry = dict->FindEntry(isolate, *key1);
  DirectHandle<Object> found(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *value);

  // Change the value
  DirectHandle<String> other_value =
      isolate->factory()->InternalizeUtf8String("baz");
  dict->ValueAtPut(entry, *other_value);

  entry = dict->FindEntry(isolate, *key1);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  CHECK(dict->FindEntry(isolate, *key2).is_not_found());
  dict = OrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));
  CHECK_EQ(InternalIndex(1), dict->FindEntry(isolate, *key2));

  entry = dict->FindEntry(isolate, *key1);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);

  entry = dict->FindEntry(isolate, *key2);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *value);

  // Change the value
  dict->ValueAtPut(entry, *other_value);

  entry = dict->FindEntry(isolate, *key1);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);
}

TEST(OrderedNameDictionaryDetailsAtAndDetailsAtPut) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedNameDictionary> dict =
      OrderedNameDictionary::Allocate(isolate, 2).ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key1).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = OrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));

  InternalIndex entry = dict->FindEntry(isolate, *key1);
  PropertyDetails found = dict->DetailsAt(entry);
  CHECK_EQ(PropertyDetails::Empty().AsSmi(), found.AsSmi());

  PropertyDetails other = PropertyDetails(PropertyKind::kAccessor, READ_ONLY,
                                          PropertyCellType::kNoCell);
  dict->DetailsAtPut(entry, other);

  found = dict->DetailsAt(entry);
  CHECK_NE(PropertyDetails::Empty().AsSmi(), found.AsSmi());
  CHECK_EQ(other.AsSmi(), found.AsSmi());

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  CHECK(dict->FindEntry(isolate, *key2).is_not_found());
  dict = OrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));
  CHECK_EQ(InternalIndex(1), dict->FindEntry(isolate, *key2));

  entry = dict->FindEntry(isolate, *key1);
  found = dict->DetailsAt(entry);
  CHECK_EQ(other.AsSmi(), found.AsSmi());
  CHECK_NE(PropertyDetails::Empty().AsSmi(), found.AsSmi());

  entry = dict->FindEntry(isolate, *key2);
  dict->DetailsAtPut(entry, other);

  found = dict->DetailsAt(entry);
  CHECK_NE(PropertyDetails::Empty().AsSmi(), found.AsSmi());
  CHECK_EQ(other.AsSmi(), found.AsSmi());
}

TEST(SmallOrderedNameDictionaryInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key1).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  CHECK(dict->FindEntry(isolate, *key2).is_not_found());
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));
  CHECK_EQ(InternalIndex(1), dict->FindEntry(isolate, *key2));
}

TEST(SmallOrderedNameDictionaryInsertionMax) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  PropertyDetails details = PropertyDetails::Empty();

  char buf[10];
  for (int i = 0; i < SmallOrderedNameDictionary::kMaxCapacity; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    DirectHandle<String> key = isolate->factory()->InternalizeUtf8String(buf);
    dict = SmallOrderedNameDictionary::Add(isolate, dict, key, value, details)
               .ToHandleChecked();
    Verify(isolate, dict);
  }

  CHECK_EQ(SmallOrderedNameDictionary::kMaxCapacity /
               SmallOrderedNameDictionary::kLoadFactor,
           dict->NumberOfBuckets());
  CHECK_EQ(SmallOrderedNameDictionary::kMaxCapacity, dict->NumberOfElements());

  // This should overflow and fail.
  CHECK(SmallOrderedNameDictionary::Add(isolate, dict, value, value, details)
            .is_null());
}

TEST(SmallOrderedNameDictionaryFindEntry) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key1).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();

  dict = SmallOrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));

  InternalIndex entry = dict->FindEntry(isolate, *key1);
  CHECK(entry.is_found());

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  CHECK(dict->FindEntry(isolate, *key2).is_not_found());
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());

  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));
  CHECK_EQ(InternalIndex(1), dict->FindEntry(isolate, *key2));
}

TEST(SmallOrderedNameDictionaryValueAtAndValueAtPut) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key1).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));

  InternalIndex entry = dict->FindEntry(isolate, *key1);
  DirectHandle<Object> found(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *value);

  // Change the value
  DirectHandle<String> other_value =
      isolate->factory()->InternalizeUtf8String("baz");
  dict->ValueAtPut(entry, *other_value);

  entry = dict->FindEntry(isolate, *key1);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  CHECK(dict->FindEntry(isolate, *key2).is_not_found());
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));
  CHECK_EQ(InternalIndex(1), dict->FindEntry(isolate, *key2));

  entry = dict->FindEntry(isolate, *key1);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);

  entry = dict->FindEntry(isolate, *key2);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *value);

  // Change the value
  dict->ValueAtPut(entry, *other_value);

  entry = dict->FindEntry(isolate, *key1);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);
}

TEST(SmallOrderedNameDictionaryDetailsAtAndDetailsAtPut) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key1).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key1, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));

  InternalIndex entry = dict->FindEntry(isolate, *key1);
  PropertyDetails found = dict->DetailsAt(entry);
  CHECK_EQ(PropertyDetails::Empty().AsSmi(), found.AsSmi());

  PropertyDetails other = PropertyDetails(PropertyKind::kAccessor, READ_ONLY,
                                          PropertyCellType::kNoCell);
  dict->DetailsAtPut(entry, other);

  found = dict->DetailsAt(entry);
  CHECK_NE(PropertyDetails::Empty().AsSmi(), found.AsSmi());
  CHECK_EQ(other.AsSmi(), found.AsSmi());

  DirectHandle<Symbol> key2 = factory->NewSymbol();
  CHECK(dict->FindEntry(isolate, *key2).is_not_found());
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key2, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(2, dict->NumberOfElements());
  CHECK_EQ(InternalIndex(0), dict->FindEntry(isolate, *key1));
  CHECK_EQ(InternalIndex(1), dict->FindEntry(isolate, *key2));

  entry = dict->FindEntry(isolate, *key1);
  found = dict->DetailsAt(entry);
  CHECK_NE(PropertyDetails::Empty().AsSmi(), found.AsSmi());
  CHECK_EQ(other.AsSmi(), found.AsSmi());

  entry = dict->FindEntry(isolate, *key2);
  dict->DetailsAtPut(entry, other);

  found = dict->DetailsAt(entry);
  CHECK_NE(PropertyDetails::Empty().AsSmi(), found.AsSmi());
  CHECK_EQ(other.AsSmi(), found.AsSmi());
}

TEST(SmallOrderedNameDictionarySetAndMigrateHash) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  PropertyDetails details = PropertyDetails::Empty();

  CHECK_EQ(PropertyArray::kNoHashSentinel, dict->Hash());
  dict->SetHash(100);
  CHECK_EQ(100, dict->Hash());

  char buf[10];
  for (int i = 0; i < SmallOrderedNameDictionary::kMaxCapacity; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    DirectHandle<String> key = isolate->factory()->InternalizeUtf8String(buf);
    dict = SmallOrderedNameDictionary::Add(isolate, dict, key, value, details)
               .ToHandleChecked();
    Verify(isolate, dict);
    CHECK_EQ(100, dict->Hash());
  }
}

TEST(OrderedNameDictionarySetAndMigrateHash) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);
  Handle<OrderedNameDictionary> dict =
      OrderedNameDictionary::Allocate(isolate, 2).ToHandleChecked();
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  PropertyDetails details = PropertyDetails::Empty();

  CHECK_EQ(PropertyArray::kNoHashSentinel, dict->Hash());
  dict->SetHash(100);
  CHECK_EQ(100, dict->Hash());

  char buf[10];
  for (int i = 0; i <= 1024; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    DirectHandle<String> key = isolate->factory()->InternalizeUtf8String(buf);
    dict = OrderedNameDictionary::Add(isolate, dict, key, value, details)
               .ToHandleChecked();
    Verify(isolate, dict);
    CHECK_EQ(100, dict->Hash());
  }
}

TEST(OrderedNameDictionaryHandlerInsertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

  Handle<HeapObject> table =
      OrderedNameDictionaryHandler::Allocate(isolate, 4).ToHandleChecked();
  CHECK(IsSmallOrderedNameDictionary(*table));
  Verify(isolate, table);

  // Add a new key.
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  DirectHandle<String> key = isolate->factory()->InternalizeUtf8String("foo");
  PropertyDetails details = PropertyDetails::Empty();

  table = OrderedNameDictionaryHandler::Add(isolate, table, key, value, details)
              .ToHandleChecked();
  DCHECK(IsUniqueName(*key));
  Verify(isolate, table);
  CHECK(IsSmallOrderedNameDictionary(*table));
  CHECK(OrderedNameDictionaryHandler::FindEntry(isolate, *table, *key)
            .is_found());

  char buf[10];
  for (int i = 0; i < 1024; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    key = isolate->factory()->InternalizeUtf8String(buf);
    table =
        OrderedNameDictionaryHandler::Add(isolate, table, key, value, details)
            .ToHandleChecked();
    DCHECK(IsUniqueName(*key));
    Verify(isolate, table);

    for (int j = 0; j <= i; j++) {
      CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", j));
      DirectHandle<Name> key_j = isolate->factory()->InternalizeUtf8String(buf);
      CHECK(OrderedNameDictionaryHandler::FindEntry(isolate, *table, *key_j)
                .is_found());
    }

    for (int j = i + 1; j < 1024; j++) {
      CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", j));
      DirectHandle<Name> key_j = isolate->factory()->InternalizeUtf8String(buf);
      CHECK(OrderedNameDictionaryHandler::FindEntry(isolate, *table, *key_j)
                .is_not_found());
    }
  }

  CHECK(IsOrderedNameDictionary(*table));
}

TEST(OrderedNameDictionaryHandlerDeletion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

  Handle<HeapObject> table =
      OrderedNameDictionaryHandler::Allocate(isolate, 4).ToHandleChecked();
  CHECK(IsSmallOrderedNameDictionary(*table));
  Verify(isolate, table);

  // Add a new key.
  DirectHandle<String> value = isolate->factory()->InternalizeUtf8String("bar");
  DirectHandle<String> key = isolate->factory()->InternalizeUtf8String("foo");
  DirectHandle<String> key2 = isolate->factory()->InternalizeUtf8String("foo2");
  PropertyDetails details = PropertyDetails::Empty();

  table = OrderedNameDictionaryHandler::Add(isolate, table, key, value, details)
              .ToHandleChecked();
  DCHECK(IsUniqueName(*key));
  Verify(isolate, table);
  CHECK(IsSmallOrderedNameDictionary(*table));
  CHECK(OrderedNameDictionaryHandler::FindEntry(isolate, *table, *key)
            .is_found());

  // Remove a non-existing key.
  OrderedNameDictionaryHandler::Delete(isolate, table, key2);
  Verify(isolate, table);
  CHECK(IsSmallOrderedNameDictionary(*table));
  CHECK(OrderedNameDictionaryHandler::FindEntry(isolate, *table, *key2)
            .is_not_found());
  CHECK(OrderedNameDictionaryHandler::FindEntry(isolate, *table, *key)
            .is_found());

  // Remove an existing key.
  OrderedNameDictionaryHandler::Delete(isolate, table, key);
  Verify(isolate, table);
  CHECK(IsSmallOrderedNameDictionary(*table));
  CHECK(OrderedNameDictionaryHandler::FindEntry(isolate, *table, *key)
            .is_not_found());

  CHECK(IsSmallOrderedNameDictionary(*table));
}

TEST(OrderedNameDictionarySetEntry) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedNameDictionary> dict =
      OrderedNameDictionary::Allocate(isolate, 2).ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());
  CHECK_EQ(0, dict->NumberOfDeletedElements());

  DirectHandle<String> key = factory->InternalizeUtf8String("foo");
  DirectHandle<String> value = factory->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = OrderedNameDictionary::Add(isolate, dict, key, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());

  InternalIndex entry = dict->FindEntry(isolate, *key);
  CHECK_EQ(InternalIndex(0), entry);
  DirectHandle<Object> found(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *value);

  // Change the value
  DirectHandle<String> other_value =
      isolate->factory()->InternalizeUtf8String("baz");
  PropertyDetails other_details = PropertyDetails(
      PropertyKind::kAccessor, READ_ONLY, PropertyCellType::kNoCell);
  dict->SetEntry(entry, *key, *other_value, other_details);

  entry = dict->FindEntry(isolate, *key);
  CHECK_EQ(InternalIndex(0), entry);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);
  found = direct_handle(dict->KeyAt(entry), isolate);
  CHECK_EQ(*found, *key);
  PropertyDetails found_details = dict->DetailsAt(entry);
  CHECK_EQ(found_details.AsSmi(), other_details.AsSmi());
}

TEST(SmallOrderedNameDictionarySetEntry) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key = factory->InternalizeUtf8String("foo");
  DirectHandle<String> value = factory->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(0, dict->NumberOfDeletedElements());

  InternalIndex entry = dict->FindEntry(isolate, *key);
  CHECK_EQ(InternalIndex(0), entry);
  DirectHandle<Object> found(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *value);

  // Change the value
  DirectHandle<String> other_value = factory->InternalizeUtf8String("baz");
  PropertyDetails other_details = PropertyDetails(
      PropertyKind::kAccessor, READ_ONLY, PropertyCellType::kNoCell);
  dict->SetEntry(entry, *key, *other_value, other_details);

  entry = dict->FindEntry(isolate, *key);
  CHECK_EQ(InternalIndex(0), entry);
  found = direct_handle(dict->ValueAt(entry), isolate);
  CHECK_EQ(*found, *other_value);
  found = direct_handle(dict->KeyAt(entry), isolate);
  CHECK_EQ(*found, *key);
  PropertyDetails found_details = dict->DetailsAt(entry);
  CHECK_EQ(found_details.AsSmi(), other_details.AsSmi());
}

TEST(OrderedNameDictionaryDeleteEntry) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<OrderedNameDictionary> dict =
      OrderedNameDictionary::Allocate(isolate, 2).ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key = factory->InternalizeUtf8String("foo");
  DirectHandle<String> value = factory->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = OrderedNameDictionary::Add(isolate, dict, key, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(0, dict->NumberOfDeletedElements());

  InternalIndex entry = dict->FindEntry(isolate, *key);
  CHECK_EQ(InternalIndex(0), entry);
  dict = OrderedNameDictionary::DeleteEntry(isolate, dict, entry);
  entry = dict->FindEntry(isolate, *key);
  CHECK(entry.is_not_found());
  CHECK_EQ(0, dict->NumberOfElements());

  char buf[10];
  // Make sure we grow at least once.
  CHECK_LT(OrderedNameDictionaryHandler::Capacity(*dict), 100);
  for (int i = 0; i < 100; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    key = factory->InternalizeUtf8String(buf);
    dict = OrderedNameDictionary::Add(isolate, dict, key, value, details)
               .ToHandleChecked();
    DCHECK(IsUniqueName(*key));
    Verify(isolate, dict);
  }

  CHECK_EQ(100, dict->NumberOfElements());
  // Initial dictionary has grown.
  CHECK_EQ(0, dict->NumberOfDeletedElements());

  for (int i = 0; i < 100; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    key = factory->InternalizeUtf8String(buf);
    entry = dict->FindEntry(isolate, *key);

    dict = OrderedNameDictionary::DeleteEntry(isolate, dict, entry);
    Verify(isolate, dict);

    entry = dict->FindEntry(isolate, *key);
    CHECK(entry.is_not_found());
  }
  CHECK_EQ(0, dict->NumberOfElements());
  // Dictionary shrunk again.
  CHECK_EQ(0, dict->NumberOfDeletedElements());
}

TEST(SmallOrderedNameDictionaryDeleteEntry) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedNameDictionary> dict =
      factory->NewSmallOrderedNameDictionary();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(0, dict->NumberOfElements());

  DirectHandle<String> key = factory->InternalizeUtf8String("foo");
  DirectHandle<String> value = factory->InternalizeUtf8String("bar");
  CHECK(dict->FindEntry(isolate, *key).is_not_found());
  PropertyDetails details = PropertyDetails::Empty();
  dict = SmallOrderedNameDictionary::Add(isolate, dict, key, value, details)
             .ToHandleChecked();
  Verify(isolate, dict);
  CHECK_EQ(2, dict->NumberOfBuckets());
  CHECK_EQ(1, dict->NumberOfElements());
  CHECK_EQ(0, dict->NumberOfDeletedElements());

  InternalIndex entry = dict->FindEntry(isolate, *key);
  CHECK_EQ(InternalIndex(0), entry);
  dict = SmallOrderedNameDictionary::DeleteEntry(isolate, dict, entry);
  entry = dict->FindEntry(isolate, *key);
  CHECK(entry.is_not_found());

  char buf[10];
  // Make sure we grow at least once.
  CHECK_LT(dict->Capacity(), SmallOrderedNameDictionary::kMaxCapacity);

  for (int i = 0; i < SmallOrderedNameDictionary::kMaxCapacity; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    key = factory->InternalizeUtf8String(buf);
    dict = SmallOrderedNameDictionary::Add(isolate, dict, key, value, details)
               .ToHandleChecked();
    DCHECK(IsUniqueName(*key));
    Verify(isolate, dict);
  }

  CHECK_EQ(SmallOrderedNameDictionary::kMaxCapacity, dict->NumberOfElements());
  // Dictionary has grown.
  CHECK_EQ(0, dict->NumberOfDeletedElements());

  for (int i = 0; i < SmallOrderedNameDictionary::kMaxCapacity; i++) {
    CHECK_LT(0, snprintf(buf, sizeof(buf), "foo%d", i));
    key = factory->InternalizeUtf8String(buf);

    entry = dict->FindEntry(isolate, *key);
    dict = SmallOrderedNameDictionary::DeleteEntry(isolate, dict, entry);
    Verify(isolate, dict);

    entry = dict->FindEntry(isolate, *key);
    CHECK(entry.is_not_found());
  }

  CHECK_EQ(0, dict->NumberOfElements());
  // Dictionary shrunk.
  CHECK_EQ(0, dict->NumberOfDeletedElements());
}

template <typename T>
void TestEmptyOrderedHashTable(Isolate* isolate, Factory* factory,
                               Handle<T> table) {
  CHECK_EQ(0, table->NumberOfElements());

  PropertyDetails details = PropertyDetails::Empty();

  Handle<String> key1 = isolate->factory()->InternalizeUtf8String("key1");
  Handle<String> value1 = isolate->factory()->InternalizeUtf8String("value1");
  table = Add(isolate, table, key1, value1, details);
  Verify(isolate, table);
  CHECK_EQ(1, table->NumberOfElements());
  CHECK(HasKey(isolate, table, *key1));

  Handle<String> key2 = factory->InternalizeUtf8String("key2");
  Handle<String> value2 = factory->InternalizeUtf8String("value2");
  CHECK(!HasKey(isolate, table, *key2));
  table = Add(isolate, table, key2, value2, details);
  Verify(isolate, table);
  CHECK_EQ(2, table->NumberOfElements());
  CHECK(HasKey(isolate, table, *key1));
  CHECK(HasKey(isolate, table, *key2));

  Handle<String> key3 = factory->InternalizeUtf8String("key3");
  Handle<String> value3 = factory->InternalizeUtf8String("value3");
  CHECK(!HasKey(isolate, table, *key3));
  table = Add(isolate, table, key3, value3, details);
  Verify(isolate, table);
  CHECK_EQ(3, table->NumberOfElements());
  CHECK(HasKey(isolate, table, *key1));
  CHECK(HasKey(isolate, table, *key2));
  CHECK(HasKey(isolate, table, *key3));

  Handle<String> key4 = factory->InternalizeUtf8String("key4");
  Handle<String> value4 = factory->InternalizeUtf8String("value4");
  CHECK(!HasKey(isolate, table, *key4));
  table = Delete(isolate, table, *key4);
  Verify(isolate, table);
  CHECK_EQ(3, table->NumberOfElements());
  CHECK_EQ(0, table->NumberOfDeletedElements());
  CHECK(!HasKey(isolate, table, *key4));

  table = Add(isolate, table, key4, value4, details);
  Verify(isolate, table);
  CHECK_EQ(4, table->NumberOfElements());
  CHECK_EQ(0, table->NumberOfDeletedElements());
  CHECK(HasKey(isolate, table, *key4));

  CHECK(HasKey(isolate, table, *key4));
  table = Delete(isolate, table, *key4);
  Verify(isolate, table);
  CHECK_EQ(3, table->NumberOfElements());
  CHECK_EQ(1, table->NumberOfDeletedElements());
  CHECK(!HasKey(isolate, table, *key4));
}

TEST(ZeroSizeOrderedHashMap) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  ReadOnlyRoots ro_roots(isolate);

  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);

  Handle<OrderedHashMap> empty =
      Handle<OrderedHashMap>(ro_roots.empty_ordered_hash_map(), isolate);
  {
    Handle<OrderedHashMap> map = empty;

    CHECK_EQ(0, map->NumberOfBuckets());
    CHECK_EQ(0, map->NumberOfElements());
    CHECK(!OrderedHashMap::HasKey(isolate, *map, *key1));

    TestEmptyOrderedHashTable(isolate, factory, map);
  }
  {
    Handle<OrderedHashMap> map = empty;

    map =
        OrderedHashMap::EnsureCapacityForAdding(isolate, map).ToHandleChecked();

    CHECK_LT(0, map->NumberOfBuckets());
    CHECK_EQ(0, map->NumberOfElements());
  }
  {
    Handle<OrderedHashMap> map = empty;

    CHECK(map->FindEntry(isolate, *key1).is_not_found());

    TestEmptyOrderedHashTable(isolate, factory, map);
  }
  {
    Handle<OrderedHashMap> map = empty;

    map = OrderedHashMap::Add(isolate, map, key1, value1).ToHandleChecked();

    CHECK_EQ(1, map->NumberOfElements());
    CHECK(OrderedHashMap::HasKey(isolate, *map, *key1));
  }
  {
    Handle<OrderedHashMap> map = empty;

    map = OrderedHashMap::Clear(isolate, map);

    TestEmptyOrderedHashTable(isolate, factory, map);
  }
  {
    Handle<OrderedHashMap> map = empty;

    map = OrderedHashMap::Rehash(isolate, map).ToHandleChecked();

    TestEmptyOrderedHashTable(isolate, factory, map);
  }
  {
    Handle<OrderedHashMap> map = empty;

    map = OrderedHashMap::Shrink(isolate, map);

    TestEmptyOrderedHashTable(isolate, factory, map);
  }
  {
    Handle<OrderedHashMap> map = empty;

    OrderedHashMap::Delete(isolate, *map, *key1);

    TestEmptyOrderedHashTable(isolate, factory, map);
  }
}

TEST(ZeroSizeOrderedHashSet) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  ReadOnlyRoots ro_roots(isolate);

  DirectHandle<Smi> key1(Smi::FromInt(1), isolate);
  DirectHandle<Smi> value1(Smi::FromInt(1), isolate);

  Handle<OrderedHashSet> empty =
      Handle<OrderedHashSet>(ro_roots.empty_ordered_hash_set(), isolate);

  {
    Handle<OrderedHashSet> set = empty;

    CHECK_EQ(0, set->NumberOfBuckets());
    CHECK_EQ(0, set->NumberOfElements());
    CHECK(!OrderedHashSet::HasKey(isolate, *set, *key1));

    TestEmptyOrderedHashTable(isolate, factory, set);
  }
  {
    Handle<OrderedHashSet> set = empty;

    set =
        OrderedHashSet::EnsureCapacityForAdding(isolate, set).ToHandleChecked();

    CHECK_LT(0, set->NumberOfBuckets());
    CHECK_EQ(0, set->NumberOfElements());
  }
  {
    Handle<OrderedHashSet> set = empty;

    CHECK(set->FindEntry(isolate, *key1).is_not_found());

    TestEmptyOrderedHashTable(isolate, factory, set);
  }
  {
    Handle<OrderedHashSet> set = empty;

    set = OrderedHashSet::Add(isolate, set, key1).ToHandleChecked();

    CHECK_EQ(1, set->NumberOfElements());
    CHECK(OrderedHashSet::HasKey(isolate, *set, *key1));
  }
  {
    Handle<OrderedHashSet> set = empty;

    set = OrderedHashSet::Clear(isolate, set);

    TestEmptyOrderedHashTable(isolate, factory, set);
  }
  {
    Handle<OrderedHashSet> set = empty;

    set = OrderedHashSet::Rehash(isolate, set).ToHandleChecked();

    TestEmptyOrderedHashTable(isolate, factory, set);
  }
  {
    Handle<OrderedHashSet> set = empty;

    set = OrderedHashSet::Shrink(isolate, set);

    TestEmptyOrderedHashTable(isolate, factory, set);
  }
  {
    Handle<OrderedHashSet> set = empty;

    OrderedHashSet::Delete(isolate, *set, *key1);

    TestEmptyOrderedHashTable(isolate, factory, set);
  }
}

TEST(ZeroSizeOrderedNameDictionary) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  ReadOnlyRoots ro_roots(isolate);

  DirectHandle<String> key1 = isolate->factory()->InternalizeUtf8String("key1");
  DirectHandle<String> value1 =
      isolate->factory()->InternalizeUtf8String("value1");
  PropertyDetails details = PropertyDetails::Empty();

  Handle<OrderedNameDictionary> empty = Handle<OrderedNameDictionary>(
      ro_roots.empty_ordered_property_dictionary(), isolate);

  {
    Handle<OrderedNameDictionary> dict = empty;

    CHECK_EQ(0, dict->NumberOfBuckets());
    CHECK_EQ(0, dict->NumberOfElements());
    CHECK(!HasKey(isolate, dict, *key1));

    TestEmptyOrderedHashTable(isolate, factory, dict);
  }
  {
    Handle<OrderedNameDictionary> dict = empty;

    CHECK(dict->FindEntry(isolate, *key1).is_not_found());

    TestEmptyOrderedHashTable(isolate, factory, dict);
  }
  {
    Handle<OrderedNameDictionary> dict = empty;

    dict = OrderedNameDictionary::Add(isolate, dict, key1, value1, details)
               .ToHandleChecked();
    CHECK_EQ(1, dict->NumberOfElements());
    CHECK(HasKey(isolate, dict, *key1));
  }
  {
    Handle<OrderedNameDictionary> dict = empty;

    dict = OrderedNameDictionary::Rehash(isolate, dict, 0).ToHandleChecked();

    TestEmptyOrderedHashTable(isolate, factory, dict);
  }
  {
    Handle<OrderedNameDictionary> dict = empty;

    dict = OrderedNameDictionary::Shrink(isolate, dict);

    TestEmptyOrderedHashTable(isolate, factory, dict);
  }
}

}  // namespace test_orderedhashtable
}  // namespace internal
}  // namespace v8
