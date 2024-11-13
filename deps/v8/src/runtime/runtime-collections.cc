// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-collection-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_TheHole) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(0, args.length());
  return ReadOnlyRoots(isolate).the_hole_value();
}

RUNTIME_FUNCTION(Runtime_OrderedHashSetGrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<OrderedHashSet> table = args.at<OrderedHashSet>(0);
  Handle<String> method_name = args.at<String>(1);
  MaybeHandle<OrderedHashSet> table_candidate =
      OrderedHashSet::EnsureCapacityForAdding(isolate, table);
  if (!table_candidate.ToHandle(&table)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kOutOfMemory, method_name));
  }
  return *table;
}

RUNTIME_FUNCTION(Runtime_SetGrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSSet> holder = args.at<JSSet>(0);
  Handle<OrderedHashSet> table(Cast<OrderedHashSet>(holder->table()), isolate);
  MaybeHandle<OrderedHashSet> table_candidate =
      OrderedHashSet::EnsureCapacityForAdding(isolate, table);
  if (!table_candidate.ToHandle(&table)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewRangeError(MessageTemplate::kCollectionGrowFailed,
                      isolate->factory()->NewStringFromAsciiChecked("Set")));
  }
  holder->set_table(*table);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_SetShrink) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSSet> holder = args.at<JSSet>(0);
  Handle<OrderedHashSet> table(Cast<OrderedHashSet>(holder->table()), isolate);
  table = OrderedHashSet::Shrink(isolate, table);
  holder->set_table(*table);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_OrderedHashSetShrink) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<OrderedHashSet> table = args.at<OrderedHashSet>(0);
  table = OrderedHashSet::Shrink(isolate, table);
  return *table;
}

RUNTIME_FUNCTION(Runtime_MapShrink) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSMap> holder = args.at<JSMap>(0);
  Handle<OrderedHashMap> table(Cast<OrderedHashMap>(holder->table()), isolate);
  table = OrderedHashMap::Shrink(isolate, table);
  holder->set_table(*table);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_MapGrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSMap> holder = args.at<JSMap>(0);
  Handle<OrderedHashMap> table(Cast<OrderedHashMap>(holder->table()), isolate);
  MaybeHandle<OrderedHashMap> table_candidate =
      OrderedHashMap::EnsureCapacityForAdding(isolate, table);
  if (!table_candidate.ToHandle(&table)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewRangeError(MessageTemplate::kCollectionGrowFailed,
                      isolate->factory()->NewStringFromAsciiChecked("Map")));
  }
  holder->set_table(*table);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_OrderedHashMapGrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<OrderedHashMap> table = args.at<OrderedHashMap>(0);
  Handle<String> methodName = args.at<String>(1);
  MaybeHandle<OrderedHashMap> table_candidate =
      OrderedHashMap::EnsureCapacityForAdding(isolate, table);
  if (!table_candidate.ToHandle(&table)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kOutOfMemory, methodName));
  }
  return *table;
}

RUNTIME_FUNCTION(Runtime_WeakCollectionDelete) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  DirectHandle<JSWeakCollection> weak_collection = args.at<JSWeakCollection>(0);
  Handle<Object> key = args.at(1);
  int hash = args.smi_value_at(2);

#ifdef DEBUG
  DCHECK(Object::CanBeHeldWeakly(*key));
  DCHECK(EphemeronHashTable::IsKey(ReadOnlyRoots(isolate), *key));
  DirectHandle<EphemeronHashTable> table(
      Cast<EphemeronHashTable>(weak_collection->table()), isolate);
  // Should only be called when shrinking the table is necessary. See
  // HashTable::Shrink().
  DCHECK(table->NumberOfElements() - 1 <= (table->Capacity() >> 2) &&
         table->NumberOfElements() - 1 >= 16);
#endif

  bool was_present = JSWeakCollection::Delete(weak_collection, key, hash);
  return isolate->heap()->ToBoolean(was_present);
}

RUNTIME_FUNCTION(Runtime_WeakCollectionSet) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  DirectHandle<JSWeakCollection> weak_collection = args.at<JSWeakCollection>(0);
  Handle<Object> key = args.at(1);
  DirectHandle<Object> value = args.at(2);
  int hash = args.smi_value_at(3);

#ifdef DEBUG
  DCHECK(Object::CanBeHeldWeakly(*key));
  DCHECK(EphemeronHashTable::IsKey(ReadOnlyRoots(isolate), *key));
  DirectHandle<EphemeronHashTable> table(
      Cast<EphemeronHashTable>(weak_collection->table()), isolate);
  // Should only be called when rehashing or resizing the table is necessary.
  // See EphemeronHashTable::Put() and HashTable::HasSufficientCapacityToAdd().
  DCHECK((table->NumberOfDeletedElements() << 1) > table->NumberOfElements() ||
         !table->HasSufficientCapacityToAdd(1));
#endif

  JSWeakCollection::Set(weak_collection, key, value, hash);
  return *weak_collection;
}

}  // namespace internal
}  // namespace v8
