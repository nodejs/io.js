// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HASH_TABLE_INL_H_
#define V8_OBJECTS_HASH_TABLE_INL_H_

#include "src/execution/isolate-utils-inl.h"
#include "src/heap/heap.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/hash-table.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/objects-inl.h"
#include "src/roots/roots-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(HashTableBase, FixedArray)

template <typename Derived, typename Shape>
HashTable<Derived, Shape>::HashTable(Address ptr) : HashTableBase(ptr) {
  SLOW_DCHECK(IsHashTable(*this));
}

template <typename Derived, typename Shape>
ObjectHashTableBase<Derived, Shape>::ObjectHashTableBase(Address ptr)
    : HashTable<Derived, Shape>(ptr) {}

ObjectHashTable::ObjectHashTable(Address ptr)
    : ObjectHashTableBase<ObjectHashTable, ObjectHashTableShape>(ptr) {
  SLOW_DCHECK(IsObjectHashTable(*this));
}

RegisteredSymbolTable::RegisteredSymbolTable(Address ptr)
    : HashTable<RegisteredSymbolTable, RegisteredSymbolTableShape>(ptr) {
  SLOW_DCHECK(IsRegisteredSymbolTable(*this));
}

EphemeronHashTable::EphemeronHashTable(Address ptr)
    : ObjectHashTableBase<EphemeronHashTable, ObjectHashTableShape>(ptr) {
  SLOW_DCHECK(IsEphemeronHashTable(*this));
}

ObjectHashSet::ObjectHashSet(Address ptr)
    : HashTable<ObjectHashSet, ObjectHashSetShape>(ptr) {
  SLOW_DCHECK(IsObjectHashSet(*this));
}

NameToIndexHashTable::NameToIndexHashTable(Address ptr)
    : HashTable<NameToIndexHashTable, NameToIndexShape>(ptr) {
  SLOW_DCHECK(IsNameToIndexHashTable(*this));
}

template <typename Derived, int N>
ObjectMultiHashTableBase<Derived, N>::ObjectMultiHashTableBase(Address ptr)
    : HashTable<Derived, ObjectMultiHashTableShape<N>>(ptr) {}

ObjectTwoHashTable::ObjectTwoHashTable(Address ptr)
    : ObjectMultiHashTableBase<ObjectTwoHashTable, 2>(ptr) {
  SLOW_DCHECK(IsObjectTwoHashTable(*this));
}

void EphemeronHashTable::set_key(int index, Tagged<Object> value) {
  DCHECK_NE(GetReadOnlyRoots().fixed_cow_array_map(), map());
  DCHECK(IsEphemeronHashTable(*this));
  DCHECK_GE(index, 0);
  DCHECK_LT(index, this->length());
  int offset = kHeaderSize + index * kTaggedSize;
  RELAXED_WRITE_FIELD(*this, offset, value);
  EPHEMERON_KEY_WRITE_BARRIER(*this, offset, value);
}

void EphemeronHashTable::set_key(int index, Tagged<Object> value,
                                 WriteBarrierMode mode) {
  DCHECK_NE(GetReadOnlyRoots().fixed_cow_array_map(), map());
  DCHECK(IsEphemeronHashTable(*this));
  DCHECK_GE(index, 0);
  DCHECK_LT(index, this->length());
  int offset = kHeaderSize + index * kTaggedSize;
  RELAXED_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_EPHEMERON_KEY_WRITE_BARRIER(*this, offset, value, mode);
}

int HashTableBase::NumberOfElements() const {
  return Cast<Smi>(get(kNumberOfElementsIndex)).value();
}

int HashTableBase::NumberOfDeletedElements() const {
  return Cast<Smi>(get(kNumberOfDeletedElementsIndex)).value();
}

int HashTableBase::Capacity() const {
  return Cast<Smi>(get(kCapacityIndex)).value();
}

InternalIndex::Range HashTableBase::IterateEntries() const {
  return InternalIndex::Range(Capacity());
}

void HashTableBase::ElementAdded() {
  SetNumberOfElements(NumberOfElements() + 1);
}

void HashTableBase::ElementRemoved() {
  SetNumberOfElements(NumberOfElements() - 1);
  SetNumberOfDeletedElements(NumberOfDeletedElements() + 1);
}

void HashTableBase::ElementsRemoved(int n) {
  SetNumberOfElements(NumberOfElements() - n);
  SetNumberOfDeletedElements(NumberOfDeletedElements() + n);
}

// static
int HashTableBase::ComputeCapacity(int at_least_space_for) {
  // Add 50% slack to make slot collisions sufficiently unlikely.
  // See matching computation in HashTable::HasSufficientCapacityToAdd().
  // Must be kept in sync with CodeStubAssembler::HashTableComputeCapacity().
  int raw_cap = at_least_space_for + (at_least_space_for >> 1);
  int capacity = base::bits::RoundUpToPowerOfTwo32(raw_cap);
  return std::max({capacity, kMinCapacity});
}

void HashTableBase::SetInitialNumberOfElements(int nof) {
  DCHECK_EQ(NumberOfElements(), 0);
  set(kNumberOfElementsIndex, Smi::FromInt(nof));
}

void HashTableBase::SetNumberOfElements(int nof) {
  set(kNumberOfElementsIndex, Smi::FromInt(nof));
}

void HashTableBase::SetNumberOfDeletedElements(int nod) {
  set(kNumberOfDeletedElementsIndex, Smi::FromInt(nod));
}

// static
template <typename Derived, typename Shape>
Handle<Map> HashTable<Derived, Shape>::GetMap(ReadOnlyRoots roots) {
  return roots.hash_table_map_handle();
}

// static
Handle<Map> NameToIndexHashTable::GetMap(ReadOnlyRoots roots) {
  return roots.name_to_index_hash_table_map_handle();
}

// static
Handle<Map> RegisteredSymbolTable::GetMap(ReadOnlyRoots roots) {
  return roots.registered_symbol_table_map_handle();
}

// static
Handle<Map> EphemeronHashTable::GetMap(ReadOnlyRoots roots) {
  return roots.ephemeron_hash_table_map_handle();
}

template <typename Derived, typename Shape>
template <typename IsolateT>
InternalIndex HashTable<Derived, Shape>::FindEntry(IsolateT* isolate, Key key) {
  ReadOnlyRoots roots(isolate);
  return FindEntry(isolate, roots, key, TodoShape::Hash(roots, key));
}

// Find entry for key otherwise return kNotFound.
template <typename Derived, typename Shape>
InternalIndex HashTable<Derived, Shape>::FindEntry(PtrComprCageBase cage_base,
                                                   ReadOnlyRoots roots, Key key,
                                                   int32_t hash) {
  DisallowGarbageCollection no_gc;
  uint32_t capacity = Capacity();
  uint32_t count = 1;
  Tagged<Object> undefined = roots.undefined_value();
  Tagged<Object> the_hole = roots.the_hole_value();
  DCHECK_EQ(TodoShape::Hash(roots, key), static_cast<uint32_t>(hash));
  // EnsureCapacity will guarantee the hash table is never full.
  for (InternalIndex entry = FirstProbe(hash, capacity);;
       entry = NextProbe(entry, count++, capacity)) {
    Tagged<Object> element = KeyAt(cage_base, entry);
    // Empty entry. Uses raw unchecked accessors because it is called by the
    // string table during bootstrapping.
    if (element == undefined) return InternalIndex::NotFound();
    if (TodoShape::kMatchNeedsHoleCheck && element == the_hole) continue;
    if (TodoShape::IsMatch(key, element)) return entry;
  }
}

template <typename Derived, typename Shape>
template <typename IsolateT>
InternalIndex HashTable<Derived, Shape>::FindInsertionEntry(IsolateT* isolate,
                                                            uint32_t hash) {
  return FindInsertionEntry(isolate, ReadOnlyRoots(isolate), hash);
}

// static
template <typename Derived, typename Shape>
bool HashTable<Derived, Shape>::IsKey(ReadOnlyRoots roots, Tagged<Object> k) {
  // TODO(leszeks): Dictionaries that don't delete could skip the hole check.
  return k != roots.unchecked_undefined_value() &&
         k != roots.unchecked_the_hole_value();
}

template <typename Derived, typename Shape>
bool HashTable<Derived, Shape>::ToKey(ReadOnlyRoots roots, InternalIndex entry,
                                      Tagged<Object>* out_k) {
  Tagged<Object> k = KeyAt(entry);
  if (!IsKey(roots, k)) return false;
  *out_k = TodoShape::Unwrap(k);
  return true;
}

template <typename Derived, typename Shape>
bool HashTable<Derived, Shape>::ToKey(PtrComprCageBase cage_base,
                                      InternalIndex entry,
                                      Tagged<Object>* out_k) {
  Tagged<Object> k = KeyAt(cage_base, entry);
  if (!IsKey(GetReadOnlyRoots(cage_base), k)) return false;
  *out_k = TodoShape::Unwrap(k);
  return true;
}

template <typename Derived, typename Shape>
Tagged<Object> HashTable<Derived, Shape>::KeyAt(InternalIndex entry) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return KeyAt(cage_base, entry);
}

template <typename Derived, typename Shape>
Tagged<Object> HashTable<Derived, Shape>::KeyAt(PtrComprCageBase cage_base,
                                                InternalIndex entry) {
  return get(EntryToIndex(entry) + kEntryKeyIndex);
}

template <typename Derived, typename Shape>
Tagged<Object> HashTable<Derived, Shape>::KeyAt(InternalIndex entry,
                                                RelaxedLoadTag tag) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return KeyAt(cage_base, entry, tag);
}

template <typename Derived, typename Shape>
Tagged<Object> HashTable<Derived, Shape>::KeyAt(PtrComprCageBase cage_base,
                                                InternalIndex entry,
                                                RelaxedLoadTag tag) {
  return get(EntryToIndex(entry) + kEntryKeyIndex, tag);
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::SetKeyAt(InternalIndex entry,
                                         Tagged<Object> value,
                                         WriteBarrierMode mode) {
  set_key(EntryToIndex(entry), value, mode);
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::set_key(int index, Tagged<Object> value) {
  DCHECK(!IsEphemeronHashTable(*this));
  FixedArray::set(index, value);
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::set_key(int index, Tagged<Object> value,
                                        WriteBarrierMode mode) {
  DCHECK(!IsEphemeronHashTable(*this));
  FixedArray::set(index, value, mode);
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::SetCapacity(int capacity) {
  // To scale a computed hash code to fit within the hash table, we
  // use bit-wise AND with a mask, so the capacity must be positive
  // and non-zero.
  DCHECK_GT(capacity, 0);
  DCHECK_LE(capacity, kMaxCapacity);
  set(kCapacityIndex, Smi::FromInt(capacity));
}

bool ObjectHashSet::Has(Isolate* isolate, Handle<Object> key, int32_t hash) {
  return FindEntry(isolate, ReadOnlyRoots(isolate), key, hash).is_found();
}

bool ObjectHashSet::Has(Isolate* isolate, Handle<Object> key) {
  Tagged<Object> hash = Object::GetHash(*key);
  if (!IsSmi(hash)) return false;
  return FindEntry(isolate, ReadOnlyRoots(isolate), key, Smi::ToInt(hash))
      .is_found();
}

bool ObjectHashTableShape::IsMatch(DirectHandle<Object> key,
                                   Tagged<Object> other) {
  return Object::SameValue(*key, other);
}

bool RegisteredSymbolTableShape::IsMatch(DirectHandle<String> key,
                                         Tagged<Object> value) {
  DCHECK(IsString(value));
  return key->Equals(Cast<String>(value));
}

uint32_t RegisteredSymbolTableShape::Hash(ReadOnlyRoots roots,
                                          DirectHandle<String> key) {
  return key->EnsureHash();
}

uint32_t RegisteredSymbolTableShape::HashForObject(ReadOnlyRoots roots,
                                                   Tagged<Object> object) {
  return Cast<String>(object)->EnsureHash();
}

bool NameToIndexShape::IsMatch(DirectHandle<Name> key, Tagged<Object> other) {
  return *key == other;
}

uint32_t NameToIndexShape::HashForObject(ReadOnlyRoots roots,
                                         Tagged<Object> other) {
  return Cast<Name>(other)->hash();
}

uint32_t NameToIndexShape::Hash(ReadOnlyRoots roots, DirectHandle<Name> key) {
  return key->hash();
}

uint32_t ObjectHashTableShape::Hash(ReadOnlyRoots roots,
                                    DirectHandle<Object> key) {
  return Smi::ToInt(Object::GetHash(*key));
}

uint32_t ObjectHashTableShape::HashForObject(ReadOnlyRoots roots,
                                             Tagged<Object> other) {
  return Smi::ToInt(Object::GetHash(other));
}

template <typename IsolateT>
Handle<NameToIndexHashTable> NameToIndexHashTable::Add(
    IsolateT* isolate, Handle<NameToIndexHashTable> table,
    IndirectHandle<Name> key, int32_t index) {
  DCHECK_GE(index, 0);
  // Validate that the key is absent.
  SLOW_DCHECK(table->FindEntry(isolate, key).is_not_found());
  // Check whether the dictionary should be extended.
  table = EnsureCapacity(isolate, table);
  DisallowGarbageCollection no_gc;
  Tagged<NameToIndexHashTable> raw_table = *table;
  // Compute the key object.
  InternalIndex entry = raw_table->FindInsertionEntry(isolate, key->hash());
  raw_table->set(EntryToIndex(entry), *key);
  raw_table->set(EntryToValueIndex(entry), Smi::FromInt(index));
  raw_table->ElementAdded();
  return table;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HASH_TABLE_INL_H_
