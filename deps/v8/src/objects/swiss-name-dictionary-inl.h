// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_
#define V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_

#include <algorithm>

#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/heap/heap.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/js-collection-iterator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/smi.h"
#include "src/objects/swiss-name-dictionary.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/swiss-name-dictionary-tq-inl.inc"

CAST_ACCESSOR(SwissNameDictionary)
OBJECT_CONSTRUCTORS_IMPL(SwissNameDictionary, HeapObject)

swiss_table::ctrl_t* SwissNameDictionary::CtrlTable() {
  return reinterpret_cast<ctrl_t*>(
      field_address(CtrlTableStartOffset(Capacity())));
}

uint8_t* SwissNameDictionary::PropertyDetailsTable() {
  return reinterpret_cast<uint8_t*>(
      field_address(PropertyDetailsTableStartOffset(Capacity())));
}

int SwissNameDictionary::Capacity() {
  return ReadField<int32_t>(CapacityOffset());
}

void SwissNameDictionary::SetCapacity(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  WriteField<int32_t>(CapacityOffset(), capacity);
}

int SwissNameDictionary::NumberOfElements() {
  return GetMetaTableField(kMetaTableElementCountFieldIndex);
}

int SwissNameDictionary::NumberOfDeletedElements() {
  return GetMetaTableField(kMetaTableDeletedElementCountFieldIndex);
}

void SwissNameDictionary::SetNumberOfElements(int elements) {
  SetMetaTableField(kMetaTableElementCountFieldIndex, elements);
}

void SwissNameDictionary::SetNumberOfDeletedElements(int deleted_elements) {
  SetMetaTableField(kMetaTableDeletedElementCountFieldIndex, deleted_elements);
}

int SwissNameDictionary::UsedCapacity() {
  return NumberOfElements() + NumberOfDeletedElements();
}

// static
constexpr bool SwissNameDictionary::IsValidCapacity(int capacity) {
  return capacity == 0 || (capacity >= kInitialCapacity &&
                           // Must be power of 2.
                           ((capacity & (capacity - 1)) == 0));
}

// static
constexpr int SwissNameDictionary::DataTableSize(int capacity) {
  return capacity * kTaggedSize * kDataTableEntryCount;
}

// static
constexpr int SwissNameDictionary::CtrlTableSize(int capacity) {
  // Doing + |kGroupWidth| due to the copy of first group at the end of control
  // table.
  return (capacity + kGroupWidth) * kOneByteSize;
}

// static
constexpr int SwissNameDictionary::SizeFor(int capacity) {
  DCHECK(IsValidCapacity(capacity));
  return PropertyDetailsTableStartOffset(capacity) + capacity;
}

// We use 7/8th as maximum load factor for non-special cases.
// For 16-wide groups, that gives an average of two empty slots per group.
// Similar to Abseil's CapacityToGrowth.
// static
constexpr int SwissNameDictionary::MaxUsableCapacity(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  if (Group::kWidth == 8 && capacity == 4) {
    // If the group size is 16 we can fully utilize capacity 4: There will be
    // enough kEmpty entries in the ctrl table.
    return 3;
  }
  return capacity - capacity / 8;
}

// Returns |at_least_space_for| * 8/7 for non-special cases. Similar to Abseil's
// GrowthToLowerboundCapacity.
// static
int SwissNameDictionary::CapacityFor(int at_least_space_for) {
  if (at_least_space_for <= 4) {
    if (at_least_space_for == 0) {
      return 0;
    } else if (at_least_space_for < 4) {
      return 4;
    } else if (kGroupWidth == 16) {
      DCHECK_EQ(4, at_least_space_for);
      return 4;
    } else if (kGroupWidth == 8) {
      DCHECK_EQ(4, at_least_space_for);
      return 8;
    }
  }

  int non_normalized = at_least_space_for + at_least_space_for / 7;
  return base::bits::RoundUpToPowerOfTwo32(non_normalized);
}

int SwissNameDictionary::EntryForEnumerationIndex(int enumeration_index) {
  DCHECK_LT(enumeration_index, UsedCapacity());
  return GetMetaTableField(kMetaTableEnumerationDataStartIndex +
                           enumeration_index);
}

void SwissNameDictionary::SetEntryForEnumerationIndex(int enumeration_index,
                                                      int entry) {
  DCHECK_LT(enumeration_index, UsedCapacity());
  DCHECK_LT(static_cast<unsigned>(entry), static_cast<unsigned>(Capacity()));
  DCHECK(IsFull(GetCtrl(entry)));

  SetMetaTableField(kMetaTableEnumerationDataStartIndex + enumeration_index,
                    entry);
}

template <typename IsolateT>
InternalIndex SwissNameDictionary::FindEntry(IsolateT* isolate,
                                             Tagged<Object> key) {
  Tagged<Name> name = Name::cast(key);
  DCHECK(IsUniqueName(name));
  uint32_t hash = name->hash();

  // We probe the hash table in groups of |kGroupWidth| buckets. One bucket
  // corresponds to a 1-byte entry in the control table.
  // Each group can be uniquely identified by the index of its first bucket,
  // which must be a value between 0 (inclusive) and Capacity() (exclusive).
  // Note that logically, groups wrap around after index Capacity() - 1. This
  // means that probing the group starting at, for example, index Capacity() - 1
  // means probing CtrlTable()[Capacity() - 1] followed by CtrlTable()[0] to
  // CtrlTable()[6], assuming a group width of 8. However, in memory, this is
  // achieved by maintaining an additional |kGroupWidth| bytes after the first
  // Capacity() entries of the control table. These contain a copy of the first
  // max(Capacity(), kGroupWidth) entries of the control table. If Capacity() <
  // |kGroupWidth|, then the remaining |kGroupWidth| - Capacity() control bytes
  // are left as |kEmpty|.
  // This means that actually, probing the group starting
  // at index Capacity() - 1 is achieved by probing CtrlTable()[Capacity() - 1],
  // followed by CtrlTable()[Capacity()] to CtrlTable()[Capacity() + 7].

  ctrl_t* ctrl = CtrlTable();
  auto seq = probe(hash, Capacity());
  // At this point, seq.offset() denotes the index of the first bucket in the
  // first group to probe. Note that this doesn't have to be divisible by
  // |kGroupWidth|, but can have any value between 0 (inclusive) and Capacity()
  // (exclusive).
  while (true) {
    Group g{ctrl + seq.offset()};
    for (int i : g.Match(swiss_table::H2(hash))) {
      int candidate_entry = seq.offset(i);
      Tagged<Object> candidate_key = KeyAt(candidate_entry);
      // This key matching is SwissNameDictionary specific!
      if (candidate_key == key) return InternalIndex(candidate_entry);
    }
    if (g.MatchEmpty()) return InternalIndex::NotFound();

    // The following selects the next group to probe. Note that seq.offset()
    // always advances by a multiple of |kGroupWidth|, modulo Capacity(). This
    // is done in a way such that we visit Capacity() / |kGroupWidth|
    // non-overlapping (!) groups before we would visit the same group (or
    // bucket) again.
    seq.next();

    // If the following DCHECK weren't true, we would have probed all Capacity()
    // different buckets without finding one containing |kEmpty| (which would
    // haved triggered the g.MatchEmpty() check above). This must not be the
    // case because the maximum load factor of 7/8 guarantees that there must
    // always remain empty buckets.
    //
    // The only exception from this rule are small tables, where 2 * Capacity()
    // < |kGroupWidth|, in which case all Capacity() entries can be filled
    // without leaving empty buckets. The layout of the control
    // table guarantees that after the first Capacity() entries of the control
    // table, the control table contains a copy of those first Capacity()
    // entries, followed by kGroupWidth - 2 * Capacity() entries containing
    // |kEmpty|. This guarantees that the g.MatchEmpty() check above will
    // always trigger if the element wasn't found, correctly preventing us from
    // probing more than one group in this special case.
    DCHECK_LT(seq.index(), Capacity());
  }
}

template <typename IsolateT>
InternalIndex SwissNameDictionary::FindEntry(IsolateT* isolate,
                                             Handle<Object> key) {
  return FindEntry(isolate, *key);
}

Tagged<Object> SwissNameDictionary::LoadFromDataTable(int entry,
                                                      int data_offset) {
  return LoadFromDataTable(GetPtrComprCageBase(*this), entry, data_offset);
}

Tagged<Object> SwissNameDictionary::LoadFromDataTable(
    PtrComprCageBase cage_base, int entry, int data_offset) {
  DCHECK_LT(static_cast<unsigned>(entry), static_cast<unsigned>(Capacity()));
  int offset = DataTableStartOffset() +
               (entry * kDataTableEntryCount + data_offset) * kTaggedSize;
  return TaggedField<Object>::Relaxed_Load(cage_base, *this, offset);
}

void SwissNameDictionary::StoreToDataTable(int entry, int data_offset,
                                           Tagged<Object> data) {
  DCHECK_LT(static_cast<unsigned>(entry), static_cast<unsigned>(Capacity()));

  int offset = DataTableStartOffset() +
               (entry * kDataTableEntryCount + data_offset) * kTaggedSize;

  RELAXED_WRITE_FIELD(*this, offset, data);
  WRITE_BARRIER(*this, offset, data);
}

void SwissNameDictionary::StoreToDataTableNoBarrier(int entry, int data_offset,
                                                    Tagged<Object> data) {
  DCHECK_LT(static_cast<unsigned>(entry), static_cast<unsigned>(Capacity()));

  int offset = DataTableStartOffset() +
               (entry * kDataTableEntryCount + data_offset) * kTaggedSize;

  RELAXED_WRITE_FIELD(*this, offset, data);
}

void SwissNameDictionary::ClearDataTableEntry(Isolate* isolate, int entry) {
  ReadOnlyRoots roots(isolate);

  StoreToDataTable(entry, kDataTableKeyEntryIndex, roots.the_hole_value());
  StoreToDataTable(entry, kDataTableValueEntryIndex, roots.the_hole_value());
}

void SwissNameDictionary::ValueAtPut(int entry, Tagged<Object> value) {
  DCHECK(!IsTheHole(value));
  StoreToDataTable(entry, kDataTableValueEntryIndex, value);
}

void SwissNameDictionary::ValueAtPut(InternalIndex entry,
                                     Tagged<Object> value) {
  ValueAtPut(entry.as_int(), value);
}

void SwissNameDictionary::SetKey(int entry, Tagged<Object> key) {
  DCHECK(!IsTheHole(key));
  StoreToDataTable(entry, kDataTableKeyEntryIndex, key);
}

void SwissNameDictionary::DetailsAtPut(int entry, PropertyDetails details) {
  DCHECK_LT(static_cast<unsigned>(entry), static_cast<unsigned>(Capacity()));
  uint8_t encoded_details = details.ToByte();
  PropertyDetailsTable()[entry] = encoded_details;
}

void SwissNameDictionary::DetailsAtPut(InternalIndex entry,
                                       PropertyDetails details) {
  DetailsAtPut(entry.as_int(), details);
}

Tagged<Object> SwissNameDictionary::KeyAt(int entry) {
  return LoadFromDataTable(entry, kDataTableKeyEntryIndex);
}

Tagged<Object> SwissNameDictionary::KeyAt(InternalIndex entry) {
  return KeyAt(entry.as_int());
}

Tagged<Name> SwissNameDictionary::NameAt(InternalIndex entry) {
  return Name::cast(KeyAt(entry));
}

// This version can be called on empty buckets.
Tagged<Object> SwissNameDictionary::ValueAtRaw(int entry) {
  return LoadFromDataTable(entry, kDataTableValueEntryIndex);
}

Tagged<Object> SwissNameDictionary::ValueAt(InternalIndex entry) {
  DCHECK(IsFull(GetCtrl(entry.as_int())));
  return ValueAtRaw(entry.as_int());
}

base::Optional<Tagged<Object>> SwissNameDictionary::TryValueAt(
    InternalIndex entry) {
#if DEBUG
  Isolate* isolate;
  GetIsolateFromHeapObject(*this, &isolate);
  DCHECK_NE(isolate, nullptr);
  SLOW_DCHECK(!isolate->heap()->IsPendingAllocation(Tagged(*this)));
#endif  // DEBUG
  // We can read Capacity() in a non-atomic way since we are reading an
  // initialized object which is not pending allocation.
  if (static_cast<unsigned>(entry.as_int()) >=
      static_cast<unsigned>(Capacity())) {
    return {};
  }
  return ValueAtRaw(entry.as_int());
}

PropertyDetails SwissNameDictionary::DetailsAt(int entry) {
  // GetCtrl(entry) does a bounds check for |entry| value.
  DCHECK(IsFull(GetCtrl(entry)));

  uint8_t encoded_details = PropertyDetailsTable()[entry];
  return PropertyDetails::FromByte(encoded_details);
}

PropertyDetails SwissNameDictionary::DetailsAt(InternalIndex entry) {
  return DetailsAt(entry.as_int());
}

// static
template <typename IsolateT>
Handle<SwissNameDictionary> SwissNameDictionary::EnsureGrowable(
    IsolateT* isolate, Handle<SwissNameDictionary> table) {
  int capacity = table->Capacity();

  if (table->UsedCapacity() < MaxUsableCapacity(capacity)) {
    // We have room for at least one more entry, nothing to do.
    return table;
  }

  int new_capacity = capacity == 0 ? kInitialCapacity : capacity * 2;
  return Rehash(isolate, table, new_capacity);
}

swiss_table::ctrl_t SwissNameDictionary::GetCtrl(int entry) {
  DCHECK_LT(static_cast<unsigned>(entry), static_cast<unsigned>(Capacity()));

  return CtrlTable()[entry];
}

void SwissNameDictionary::SetCtrl(int entry, ctrl_t h) {
  int capacity = Capacity();
  DCHECK_LT(static_cast<unsigned>(entry), static_cast<unsigned>(capacity));

  ctrl_t* ctrl = CtrlTable();
  ctrl[entry] = h;

  // The ctrl table contains a copy of the first group (i.e., the group starting
  // at entry 0) after the first |capacity| entries of the ctrl table. This
  // means that the ctrl table always has size |capacity| + |kGroupWidth|.
  // However, note that we may have |capacity| < |kGroupWidth|. For example, if
  // Capacity() == 8 and |kGroupWidth| == 16, then ctrl[0] is copied to ctrl[8],
  // ctrl[1] to ctrl[9], etc. In this case, ctrl[16] to ctrl[23] remain unused,
  // which means that their values are always Ctrl::kEmpty.
  // We achieve the necessary copying without branching here using some bit
  // magic: We set {copy_entry = entry} in those cases where we don't actually
  // have to perform a copy (meaning that we just repeat the {ctrl[entry] = h}
  // from above). If we do need to do some actual copying, we set {copy_entry =
  // Capacity() + entry}.

  int mask = capacity - 1;
  int copy_entry =
      ((entry - Group::kWidth) & mask) + 1 + ((Group::kWidth - 1) & mask);
  DCHECK_IMPLIES(entry < static_cast<int>(Group::kWidth),
                 copy_entry == capacity + entry);
  DCHECK_IMPLIES(entry >= static_cast<int>(Group::kWidth), copy_entry == entry);
  ctrl[copy_entry] = h;
}

// static
inline int SwissNameDictionary::FindFirstEmpty(uint32_t hash) {
  // See SwissNameDictionary::FindEntry for description of probing algorithm.

  auto seq = probe(hash, Capacity());
  while (true) {
    Group g{CtrlTable() + seq.offset()};
    auto mask = g.MatchEmpty();
    if (mask) {
      // Note that picking the lowest bit set here means using the leftmost
      // empty bucket in the group. Here, "left" means smaller entry/bucket
      // index.
      return seq.offset(mask.LowestBitSet());
    }
    seq.next();
    DCHECK_LT(seq.index(), Capacity());
  }
}

void SwissNameDictionary::SetMetaTableField(int field_index, int value) {
  // See the STATIC_ASSERTs on |kMax1ByteMetaTableCapacity| and
  // |kMax2ByteMetaTableCapacity| in the .cc file for an explanation of these
  // constants.
  int capacity = Capacity();
  Tagged<ByteArray> meta_table = this->meta_table();
  if (capacity <= kMax1ByteMetaTableCapacity) {
    SetMetaTableField<uint8_t>(meta_table, field_index, value);
  } else if (capacity <= kMax2ByteMetaTableCapacity) {
    SetMetaTableField<uint16_t>(meta_table, field_index, value);
  } else {
    SetMetaTableField<uint32_t>(meta_table, field_index, value);
  }
}

int SwissNameDictionary::GetMetaTableField(int field_index) {
  // See the STATIC_ASSERTs on |kMax1ByteMetaTableCapacity| and
  // |kMax2ByteMetaTableCapacity| in the .cc file for an explanation of these
  // constants.
  int capacity = Capacity();
  Tagged<ByteArray> meta_table = this->meta_table();
  if (capacity <= kMax1ByteMetaTableCapacity) {
    return GetMetaTableField<uint8_t>(meta_table, field_index);
  } else if (capacity <= kMax2ByteMetaTableCapacity) {
    return GetMetaTableField<uint16_t>(meta_table, field_index);
  } else {
    return GetMetaTableField<uint32_t>(meta_table, field_index);
  }
}

// static
template <typename T>
void SwissNameDictionary::SetMetaTableField(Tagged<ByteArray> meta_table,
                                            int field_index, int value) {
  static_assert((std::is_same<T, uint8_t>::value) ||
                (std::is_same<T, uint16_t>::value) ||
                (std::is_same<T, uint32_t>::value));
  DCHECK_LE(value, std::numeric_limits<T>::max());
  DCHECK_LT(meta_table->begin() + field_index * sizeof(T), meta_table->end());
  T* raw_data = reinterpret_cast<T*>(meta_table->begin());
  raw_data[field_index] = value;
}

// static
template <typename T>
int SwissNameDictionary::GetMetaTableField(Tagged<ByteArray> meta_table,
                                           int field_index) {
  static_assert((std::is_same<T, uint8_t>::value) ||
                (std::is_same<T, uint16_t>::value) ||
                (std::is_same<T, uint32_t>::value));
  DCHECK_LT(meta_table->begin() + field_index * sizeof(T), meta_table->end());
  T* raw_data = reinterpret_cast<T*>(meta_table->begin());
  return raw_data[field_index];
}

constexpr int SwissNameDictionary::MetaTableSizePerEntryFor(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  // See the STATIC_ASSERTs on |kMax1ByteMetaTableCapacity| and
  // |kMax2ByteMetaTableCapacity| in the .cc file for an explanation of these
  // constants.
  if (capacity <= kMax1ByteMetaTableCapacity) {
    return sizeof(uint8_t);
  } else if (capacity <= kMax2ByteMetaTableCapacity) {
    return sizeof(uint16_t);
  } else {
    return sizeof(uint32_t);
  }
}

constexpr int SwissNameDictionary::MetaTableSizeFor(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  int per_entry_size = MetaTableSizePerEntryFor(capacity);

  // The enumeration table only needs to have as many slots as there can be
  // present + deleted entries in the hash table (= maximum load factor *
  // capactiy). Two more slots to store the number of present and deleted
  // entries.
  return per_entry_size * (MaxUsableCapacity(capacity) + 2);
}

bool SwissNameDictionary::IsKey(ReadOnlyRoots roots,
                                Tagged<Object> key_candidate) {
  return key_candidate != roots.the_hole_value();
}

bool SwissNameDictionary::ToKey(ReadOnlyRoots roots, int entry,
                                Tagged<Object>* out_key) {
  Tagged<Object> k = KeyAt(entry);
  if (!IsKey(roots, k)) return false;
  *out_key = k;
  return true;
}

bool SwissNameDictionary::ToKey(ReadOnlyRoots roots, InternalIndex entry,
                                Tagged<Object>* out_key) {
  return ToKey(roots, entry.as_int(), out_key);
}

// static
template <typename IsolateT>
Handle<SwissNameDictionary> SwissNameDictionary::Add(
    IsolateT* isolate, Handle<SwissNameDictionary> original_table,
    Handle<Name> key, Handle<Object> value, PropertyDetails details,
    InternalIndex* entry_out) {
  DCHECK(original_table->FindEntry(isolate, *key).is_not_found());

  Handle<SwissNameDictionary> table = EnsureGrowable(isolate, original_table);
  DisallowGarbageCollection no_gc;
  Tagged<SwissNameDictionary> raw_table = *table;
  int nof = raw_table->NumberOfElements();
  int nod = raw_table->NumberOfDeletedElements();
  int new_enum_index = nof + nod;

  int new_entry = raw_table->AddInternal(*key, *value, details);

  raw_table->SetNumberOfElements(nof + 1);
  raw_table->SetEntryForEnumerationIndex(new_enum_index, new_entry);

  if (entry_out) {
    *entry_out = InternalIndex(new_entry);
  }

  return table;
}

int SwissNameDictionary::AddInternal(Tagged<Name> key, Tagged<Object> value,
                                     PropertyDetails details) {
  DisallowHeapAllocation no_gc;

  DCHECK(IsUniqueName(key));
  DCHECK_LE(UsedCapacity(), MaxUsableCapacity(Capacity()));

  uint32_t hash = key->hash();

  // For now we don't re-use deleted buckets (due to enumeration table
  // complications), which is why we only look for empty buckets here, not
  // deleted ones.
  int target = FindFirstEmpty(hash);

  SetCtrl(target, swiss_table::H2(hash));
  SetKey(target, key);
  ValueAtPut(target, value);
  DetailsAtPut(target, details);

  // Note that we do not update the number of elements or the enumeration table
  // in this function.

  return target;
}

template <typename IsolateT>
void SwissNameDictionary::Initialize(IsolateT* isolate,
                                     Tagged<ByteArray> meta_table,
                                     int capacity) {
  DCHECK(IsValidCapacity(capacity));
  DisallowHeapAllocation no_gc;
  ReadOnlyRoots roots(isolate);

  SetCapacity(capacity);
  SetHash(PropertyArray::kNoHashSentinel);

  memset(CtrlTable(), Ctrl::kEmpty, CtrlTableSize(capacity));

  MemsetTagged(RawField(DataTableStartOffset()), roots.the_hole_value(),
               capacity * kDataTableEntryCount);

  set_meta_table(meta_table);

  SetNumberOfElements(0);
  SetNumberOfDeletedElements(0);

  // We leave the enumeration table PropertyDetails table and uninitialized.
}

SwissNameDictionary::IndexIterator::IndexIterator(
    Handle<SwissNameDictionary> dict, int start)
    : enum_index_{start}, dict_{dict} {
  if (dict.is_null()) {
    used_capacity_ = 0;
  } else {
    used_capacity_ = dict->UsedCapacity();
  }
}

SwissNameDictionary::IndexIterator&
SwissNameDictionary::IndexIterator::operator++() {
  DCHECK_LT(enum_index_, used_capacity_);
  ++enum_index_;
  return *this;
}

bool SwissNameDictionary::IndexIterator::operator==(
    const SwissNameDictionary::IndexIterator& b) const {
  DCHECK_LE(enum_index_, used_capacity_);
  DCHECK_LE(b.enum_index_, used_capacity_);
  DCHECK(dict_.equals(b.dict_));

  return this->enum_index_ == b.enum_index_;
}

bool SwissNameDictionary::IndexIterator::operator!=(
    const IndexIterator& b) const {
  return !(*this == b);
}

InternalIndex SwissNameDictionary::IndexIterator::operator*() {
  DCHECK_LE(enum_index_, used_capacity_);

  if (enum_index_ == used_capacity_) return InternalIndex::NotFound();

  return InternalIndex(dict_->EntryForEnumerationIndex(enum_index_));
}

SwissNameDictionary::IndexIterable::IndexIterable(
    Handle<SwissNameDictionary> dict)
    : dict_{dict} {}

SwissNameDictionary::IndexIterator SwissNameDictionary::IndexIterable::begin() {
  return IndexIterator(dict_, 0);
}

SwissNameDictionary::IndexIterator SwissNameDictionary::IndexIterable::end() {
  if (dict_.is_null()) {
    return IndexIterator(dict_, 0);
  } else {
    DCHECK(!dict_.is_null());
    return IndexIterator(dict_, dict_->UsedCapacity());
  }
}

SwissNameDictionary::IndexIterable
SwissNameDictionary::IterateEntriesOrdered() {
  // If we are supposed to iterate the empty dictionary (which is non-writable),
  // we have no simple way to get the isolate, which we would need to create a
  // handle.
  // TODO(emrich): Consider always using roots.empty_swiss_dictionary_handle()
  // in the condition once this function gets Isolate as a parameter in order to
  // avoid empty dict checks.
  if (Capacity() == 0) {
    return IndexIterable(Handle<SwissNameDictionary>::null());
  }

  Isolate* isolate;
  GetIsolateFromHeapObject(*this, &isolate);
  DCHECK_NE(isolate, nullptr);
  return IndexIterable(handle(*this, isolate));
}

SwissNameDictionary::IndexIterable SwissNameDictionary::IterateEntries() {
  return IterateEntriesOrdered();
}

void SwissNameDictionary::SetHash(int32_t hash) {
  WriteField(PrefixOffset(), hash);
}

int SwissNameDictionary::Hash() { return ReadField<int32_t>(PrefixOffset()); }

// static
constexpr int SwissNameDictionary::MaxCapacity() {
  int const_size =
      DataTableStartOffset() + ByteArray::kHeaderSize +
      // Size for present and deleted element count at max capacity:
      2 * sizeof(uint32_t);
  int per_entry_size =
      // size of data table entries:
      kDataTableEntryCount * kTaggedSize +
      // ctrl table entry size:
      kOneByteSize +
      // PropertyDetails table entry size:
      kOneByteSize +
      // Enumeration table entry size at maximum capacity:
      sizeof(uint32_t);

  int result = (FixedArrayBase::kMaxSize - const_size) / per_entry_size;
  DCHECK_GE(Smi::kMaxValue, result);

  return result;
}

// static
constexpr int SwissNameDictionary::PrefixOffset() {
  return HeapObject::kHeaderSize;
}

// static
constexpr int SwissNameDictionary::CapacityOffset() {
  return PrefixOffset() + sizeof(uint32_t);
}

// static
constexpr int SwissNameDictionary::MetaTablePointerOffset() {
  return CapacityOffset() + sizeof(int32_t);
}

// static
constexpr int SwissNameDictionary::DataTableStartOffset() {
  return MetaTablePointerOffset() + kTaggedSize;
}

// static
constexpr int SwissNameDictionary::DataTableEndOffset(int capacity) {
  return CtrlTableStartOffset(capacity);
}

// static
constexpr int SwissNameDictionary::CtrlTableStartOffset(int capacity) {
  return DataTableStartOffset() + DataTableSize(capacity);
}

// static
constexpr int SwissNameDictionary::PropertyDetailsTableStartOffset(
    int capacity) {
  return CtrlTableStartOffset(capacity) + CtrlTableSize(capacity);
}

// static
bool SwissNameDictionary::IsEmpty(ctrl_t c) { return c == Ctrl::kEmpty; }

// static
bool SwissNameDictionary::IsFull(ctrl_t c) {
  static_assert(Ctrl::kEmpty < 0);
  static_assert(Ctrl::kDeleted < 0);
  static_assert(Ctrl::kSentinel < 0);
  return c >= 0;
}

// static
bool SwissNameDictionary::IsDeleted(ctrl_t c) { return c == Ctrl::kDeleted; }

// static
bool SwissNameDictionary::IsEmptyOrDeleted(ctrl_t c) {
  static_assert(Ctrl::kDeleted < Ctrl::kSentinel);
  static_assert(Ctrl::kEmpty < Ctrl::kSentinel);
  static_assert(Ctrl::kSentinel < 0);
  return c < Ctrl::kSentinel;
}

// static
swiss_table::ProbeSequence<SwissNameDictionary::kGroupWidth>
SwissNameDictionary::probe(uint32_t hash, int capacity) {
  // If |capacity| is 0, we must produce 1 here, such that the - 1 below
  // yields 0, which is the correct modulo mask for a table of capacity 0.
  int non_zero_capacity = capacity | (capacity == 0);
  return swiss_table::ProbeSequence<SwissNameDictionary::kGroupWidth>(
      swiss_table::H1(hash), static_cast<uint32_t>(non_zero_capacity - 1));
}

ACCESSORS_CHECKED2(SwissNameDictionary, meta_table, Tagged<ByteArray>,
                   MetaTablePointerOffset(), true,
                   value->length() >= kMetaTableEnumerationDataStartIndex)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SWISS_NAME_DICTIONARY_INL_H_
