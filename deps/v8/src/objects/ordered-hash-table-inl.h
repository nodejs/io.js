// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ORDERED_HASH_TABLE_INL_H_
#define V8_OBJECTS_ORDERED_HASH_TABLE_INL_H_

#include "src/objects/ordered-hash-table.h"

#include "src/heap/heap.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/js-collection-iterator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/ordered-hash-table-tq-inl.inc"

CAST_ACCESSOR(OrderedNameDictionary)
CAST_ACCESSOR(SmallOrderedNameDictionary)
CAST_ACCESSOR(OrderedHashMap)
CAST_ACCESSOR(OrderedHashSet)
CAST_ACCESSOR(SmallOrderedHashMap)
CAST_ACCESSOR(SmallOrderedHashSet)

template <class Derived, int entrysize>
OrderedHashTable<Derived, entrysize>::OrderedHashTable(Address ptr)
    : FixedArray(ptr) {}

template <class Derived, int entrysize>
bool OrderedHashTable<Derived, entrysize>::IsKey(ReadOnlyRoots roots,
                                                 Tagged<Object> k) {
  return k != roots.the_hole_value();
}

OrderedHashSet::OrderedHashSet(Address ptr)
    : OrderedHashTable<OrderedHashSet, 1>(ptr) {
  SLOW_DCHECK(IsOrderedHashSet(*this));
}

OrderedHashMap::OrderedHashMap(Address ptr)
    : OrderedHashTable<OrderedHashMap, 2>(ptr) {
  SLOW_DCHECK(IsOrderedHashMap(*this));
}

OrderedNameDictionary::OrderedNameDictionary(Address ptr)
    : OrderedHashTable<OrderedNameDictionary, 3>(ptr) {
  SLOW_DCHECK(IsOrderedNameDictionary(*this));
}

template <class Derived>
SmallOrderedHashTable<Derived>::SmallOrderedHashTable(Address ptr)
    : HeapObject(ptr) {}

template <class Derived>
Tagged<Object> SmallOrderedHashTable<Derived>::KeyAt(
    InternalIndex entry) const {
  DCHECK_LT(entry.as_int(), Capacity());
  Offset entry_offset = GetDataEntryOffset(entry.as_int(), Derived::kKeyIndex);
  return TaggedField<Object>::load(*this, entry_offset);
}

template <class Derived>
Tagged<Object> SmallOrderedHashTable<Derived>::GetDataEntry(
    int entry, int relative_index) {
  DCHECK_LT(entry, Capacity());
  DCHECK_LE(static_cast<unsigned>(relative_index), Derived::kEntrySize);
  Offset entry_offset = GetDataEntryOffset(entry, relative_index);
  return TaggedField<Object>::load(*this, entry_offset);
}

OBJECT_CONSTRUCTORS_IMPL(SmallOrderedHashSet,
                         SmallOrderedHashTable<SmallOrderedHashSet>)
OBJECT_CONSTRUCTORS_IMPL(SmallOrderedHashMap,
                         SmallOrderedHashTable<SmallOrderedHashMap>)
OBJECT_CONSTRUCTORS_IMPL(SmallOrderedNameDictionary,
                         SmallOrderedHashTable<SmallOrderedNameDictionary>)

Handle<Map> OrderedHashSet::GetMap(ReadOnlyRoots roots) {
  return roots.ordered_hash_set_map_handle();
}

Handle<Map> OrderedHashMap::GetMap(ReadOnlyRoots roots) {
  return roots.ordered_hash_map_map_handle();
}

Handle<Map> OrderedNameDictionary::GetMap(ReadOnlyRoots roots) {
  return roots.ordered_name_dictionary_map_handle();
}

Handle<Map> SmallOrderedNameDictionary::GetMap(ReadOnlyRoots roots) {
  return roots.small_ordered_name_dictionary_map_handle();
}

Handle<Map> SmallOrderedHashMap::GetMap(ReadOnlyRoots roots) {
  return roots.small_ordered_hash_map_map_handle();
}

Handle<Map> SmallOrderedHashSet::GetMap(ReadOnlyRoots roots) {
  return roots.small_ordered_hash_set_map_handle();
}

inline Tagged<Object> OrderedHashMap::ValueAt(InternalIndex entry) {
  DCHECK_LT(entry.as_int(), UsedCapacity());
  return get(EntryToIndex(entry) + kValueOffset);
}

inline Tagged<Object> OrderedNameDictionary::ValueAt(InternalIndex entry) {
  DCHECK_LT(entry.as_int(), UsedCapacity());
  return get(EntryToIndex(entry) + kValueOffset);
}

Tagged<Name> OrderedNameDictionary::NameAt(InternalIndex entry) {
  return Name::cast(KeyAt(entry));
}

// Parameter |roots| only here for compatibility with HashTable<...>::ToKey.
template <class Derived, int entrysize>
bool OrderedHashTable<Derived, entrysize>::ToKey(ReadOnlyRoots roots,
                                                 InternalIndex entry,
                                                 Tagged<Object>* out_key) {
  Tagged<Object> k = KeyAt(entry);
  if (!IsKey(roots, k)) return false;
  *out_key = k;
  return true;
}

// Set the value for entry.
inline void OrderedNameDictionary::ValueAtPut(InternalIndex entry,
                                              Tagged<Object> value) {
  DCHECK_LT(entry.as_int(), UsedCapacity());
  this->set(EntryToIndex(entry) + kValueOffset, value);
}

// Returns the property details for the property at entry.
inline PropertyDetails OrderedNameDictionary::DetailsAt(InternalIndex entry) {
  DCHECK_LT(entry.as_int(), this->UsedCapacity());
  // TODO(gsathya): Optimize the cast away.
  return PropertyDetails(
      Smi::cast(get(EntryToIndex(entry) + kPropertyDetailsOffset)));
}

inline void OrderedNameDictionary::DetailsAtPut(InternalIndex entry,
                                                PropertyDetails value) {
  DCHECK_LT(entry.as_int(), this->UsedCapacity());
  // TODO(gsathya): Optimize the cast away.
  this->set(EntryToIndex(entry) + kPropertyDetailsOffset, value.AsSmi());
}

inline Tagged<Object> SmallOrderedNameDictionary::ValueAt(InternalIndex entry) {
  return this->GetDataEntry(entry.as_int(), kValueIndex);
}

// Set the value for entry.
inline void SmallOrderedNameDictionary::ValueAtPut(InternalIndex entry,
                                                   Tagged<Object> value) {
  this->SetDataEntry(entry.as_int(), kValueIndex, value);
}

// Returns the property details for the property at entry.
inline PropertyDetails SmallOrderedNameDictionary::DetailsAt(
    InternalIndex entry) {
  // TODO(gsathya): Optimize the cast away. And store this in the data table.
  return PropertyDetails(
      Smi::cast(this->GetDataEntry(entry.as_int(), kPropertyDetailsIndex)));
}

// Set the details for entry.
inline void SmallOrderedNameDictionary::DetailsAtPut(InternalIndex entry,
                                                     PropertyDetails value) {
  // TODO(gsathya): Optimize the cast away. And store this in the data table.
  this->SetDataEntry(entry.as_int(), kPropertyDetailsIndex, value.AsSmi());
}

inline bool OrderedHashSet::Is(Handle<HeapObject> table) {
  return IsOrderedHashSet(*table);
}

inline bool OrderedHashMap::Is(Handle<HeapObject> table) {
  return IsOrderedHashMap(*table);
}

inline bool OrderedNameDictionary::Is(Handle<HeapObject> table) {
  return IsOrderedNameDictionary(*table);
}

inline bool SmallOrderedHashSet::Is(Handle<HeapObject> table) {
  return IsSmallOrderedHashSet(*table);
}

inline bool SmallOrderedNameDictionary::Is(Handle<HeapObject> table) {
  return IsSmallOrderedNameDictionary(*table);
}

inline bool SmallOrderedHashMap::Is(Handle<HeapObject> table) {
  return IsSmallOrderedHashMap(*table);
}

template <class Derived>
void SmallOrderedHashTable<Derived>::SetDataEntry(int entry, int relative_index,
                                                  Tagged<Object> value) {
  DCHECK_NE(kNotFound, entry);
  int entry_offset = GetDataEntryOffset(entry, relative_index);
  RELAXED_WRITE_FIELD(*this, entry_offset, value);
  WRITE_BARRIER(*this, entry_offset, value);
}

template <class Derived, class TableType>
Tagged<Object> OrderedHashTableIterator<Derived, TableType>::CurrentKey() {
  Tagged<TableType> table = TableType::cast(this->table());
  int index = Smi::ToInt(this->index());
  DCHECK_LE(0, index);
  InternalIndex entry(index);
  Tagged<Object> key = table->KeyAt(entry);
  DCHECK(!IsHashTableHole(key));
  return key;
}

inline void SmallOrderedNameDictionary::SetHash(int hash) {
  DCHECK(PropertyArray::HashField::is_valid(hash));
  WriteField<int>(PrefixOffset(), hash);
}

inline int SmallOrderedNameDictionary::Hash() {
  int hash = ReadField<int>(PrefixOffset());
  DCHECK(PropertyArray::HashField::is_valid(hash));
  return hash;
}

inline void OrderedNameDictionary::SetHash(int hash) {
  DCHECK(PropertyArray::HashField::is_valid(hash));
  this->set(HashIndex(), Smi::FromInt(hash));
}

inline int OrderedNameDictionary::Hash() {
  Tagged<Object> hash_obj = this->get(HashIndex());
  int hash = Smi::ToInt(hash_obj);
  DCHECK(PropertyArray::HashField::is_valid(hash));
  return hash;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ORDERED_HASH_TABLE_INL_H_
