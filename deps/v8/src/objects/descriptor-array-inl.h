// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DESCRIPTOR_ARRAY_INL_H_
#define V8_OBJECTS_DESCRIPTOR_ARRAY_INL_H_

#include "src/execution/isolate.h"
#include "src/handles/maybe-handles-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/heap.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/field-type.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/lookup-cache-inl.h"
#include "src/objects/maybe-object-inl.h"
#include "src/objects/property.h"
#include "src/objects/struct-inl.h"
#include "src/objects/tagged-field-inl.h"
#include "src/torque/runtime-macro-shims.h"
#include "src/torque/runtime-support.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/descriptor-array-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(DescriptorArray)
TQ_OBJECT_CONSTRUCTORS_IMPL(EnumCache)

RELAXED_INT16_ACCESSORS(DescriptorArray, number_of_all_descriptors,
                        kNumberOfAllDescriptorsOffset)
RELAXED_INT16_ACCESSORS(DescriptorArray, number_of_descriptors,
                        kNumberOfDescriptorsOffset)
RELAXED_UINT32_ACCESSORS(DescriptorArray, raw_gc_state, kRawGcStateOffset)

inline int16_t DescriptorArray::number_of_slack_descriptors() const {
  return number_of_all_descriptors() - number_of_descriptors();
}

inline int DescriptorArray::number_of_entries() const {
  return number_of_descriptors();
}

void DescriptorArray::CopyEnumCacheFrom(Tagged<DescriptorArray> array) {
  set_enum_cache(array->enum_cache());
}

InternalIndex DescriptorArray::Search(Tagged<Name> name, int valid_descriptors,
                                      bool concurrent_search) {
  DCHECK(IsUniqueName(name));
  return InternalIndex(internal::Search<VALID_ENTRIES>(
      this, name, valid_descriptors, nullptr, concurrent_search));
}

InternalIndex DescriptorArray::Search(Tagged<Name> name, Tagged<Map> map,
                                      bool concurrent_search) {
  DCHECK(IsUniqueName(name));
  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  if (number_of_own_descriptors == 0) return InternalIndex::NotFound();
  return Search(name, number_of_own_descriptors, concurrent_search);
}

InternalIndex DescriptorArray::Search(int field_index, int valid_descriptors) {
  for (int desc_index = field_index; desc_index < valid_descriptors;
       ++desc_index) {
    PropertyDetails details = GetDetails(InternalIndex(desc_index));
    if (details.location() != PropertyLocation::kField) continue;
    if (field_index == details.field_index()) {
      return InternalIndex(desc_index);
    }
    DCHECK_LT(details.field_index(), field_index);
  }
  return InternalIndex::NotFound();
}

InternalIndex DescriptorArray::Search(int field_index, Tagged<Map> map) {
  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  if (number_of_own_descriptors == 0) return InternalIndex::NotFound();
  return Search(field_index, number_of_own_descriptors);
}

InternalIndex DescriptorArray::SearchWithCache(Isolate* isolate,
                                               Tagged<Name> name,
                                               Tagged<Map> map) {
  DCHECK(IsUniqueName(name));
  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  if (number_of_own_descriptors == 0) return InternalIndex::NotFound();

  DescriptorLookupCache* cache = isolate->descriptor_lookup_cache();
  int number = cache->Lookup(map, name);

  if (number == DescriptorLookupCache::kAbsent) {
    InternalIndex result = Search(name, number_of_own_descriptors);
    number = result.is_found() ? result.as_int() : DescriptorArray::kNotFound;
    cache->Update(map, name, number);
  }
  if (number == DescriptorArray::kNotFound) return InternalIndex::NotFound();
  return InternalIndex(number);
}

ObjectSlot DescriptorArray::GetFirstPointerSlot() {
  static_assert(kEndOfStrongFieldsOffset == kStartOfWeakFieldsOffset,
                "Weak and strong fields are continuous.");
  static_assert(kEndOfWeakFieldsOffset == kHeaderSize,
                "Weak fields extend up to the end of the header.");
  return RawField(DescriptorArray::kStartOfStrongFieldsOffset);
}

ObjectSlot DescriptorArray::GetDescriptorSlot(int descriptor) {
  // Allow descriptor == number_of_all_descriptors() for computing the slot
  // address that comes after the last descriptor (for iterating).
  DCHECK_LE(descriptor, number_of_all_descriptors());
  return RawField(OffsetOfDescriptorAt(descriptor));
}

Tagged<Name> DescriptorArray::GetKey(InternalIndex descriptor_number) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return GetKey(cage_base, descriptor_number);
}

Tagged<Name> DescriptorArray::GetKey(PtrComprCageBase cage_base,
                                     InternalIndex descriptor_number) const {
  DCHECK_LT(descriptor_number.as_int(), number_of_descriptors());
  int entry_offset = OffsetOfDescriptorAt(descriptor_number.as_int());
  return Name::cast(
      EntryKeyField::Relaxed_Load(cage_base, *this, entry_offset));
}

void DescriptorArray::SetKey(InternalIndex descriptor_number,
                             Tagged<Name> key) {
  DCHECK_LT(descriptor_number.as_int(), number_of_descriptors());
  int entry_offset = OffsetOfDescriptorAt(descriptor_number.as_int());
  EntryKeyField::Relaxed_Store(*this, entry_offset, key);
  WRITE_BARRIER(*this, entry_offset + kEntryKeyOffset, key);
}

int DescriptorArray::GetSortedKeyIndex(int descriptor_number) {
  return GetDetails(InternalIndex(descriptor_number)).pointer();
}

Tagged<Name> DescriptorArray::GetSortedKey(int descriptor_number) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return GetSortedKey(cage_base, descriptor_number);
}

Tagged<Name> DescriptorArray::GetSortedKey(PtrComprCageBase cage_base,
                                           int descriptor_number) {
  return GetKey(cage_base, InternalIndex(GetSortedKeyIndex(descriptor_number)));
}

void DescriptorArray::SetSortedKey(int descriptor_number, int pointer) {
  PropertyDetails details = GetDetails(InternalIndex(descriptor_number));
  SetDetails(InternalIndex(descriptor_number), details.set_pointer(pointer));
}

Tagged<Object> DescriptorArray::GetStrongValue(
    InternalIndex descriptor_number) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return GetStrongValue(cage_base, descriptor_number);
}

Tagged<Object> DescriptorArray::GetStrongValue(
    PtrComprCageBase cage_base, InternalIndex descriptor_number) {
  return Tagged<Object>::cast(GetValue(cage_base, descriptor_number));
}

void DescriptorArray::SetValue(InternalIndex descriptor_number,
                               Tagged<MaybeObject> value) {
  DCHECK_LT(descriptor_number.as_int(), number_of_descriptors());
  int entry_offset = OffsetOfDescriptorAt(descriptor_number.as_int());
  EntryValueField::Relaxed_Store(*this, entry_offset, value);
  WEAK_WRITE_BARRIER(*this, entry_offset + kEntryValueOffset, value);
}

Tagged<MaybeObject> DescriptorArray::GetValue(InternalIndex descriptor_number) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return GetValue(cage_base, descriptor_number);
}

Tagged<MaybeObject> DescriptorArray::GetValue(PtrComprCageBase cage_base,
                                              InternalIndex descriptor_number) {
  DCHECK_LT(descriptor_number.as_int(), number_of_descriptors());
  int entry_offset = OffsetOfDescriptorAt(descriptor_number.as_int());
  return EntryValueField::Relaxed_Load(cage_base, *this, entry_offset);
}

PropertyDetails DescriptorArray::GetDetails(InternalIndex descriptor_number) {
  DCHECK_LT(descriptor_number.as_int(), number_of_descriptors());
  int entry_offset = OffsetOfDescriptorAt(descriptor_number.as_int());
  Tagged<Smi> details = EntryDetailsField::Relaxed_Load(*this, entry_offset);
  return PropertyDetails(details);
}

void DescriptorArray::SetDetails(InternalIndex descriptor_number,
                                 PropertyDetails details) {
  DCHECK_LT(descriptor_number.as_int(), number_of_descriptors());
  int entry_offset = OffsetOfDescriptorAt(descriptor_number.as_int());
  EntryDetailsField::Relaxed_Store(*this, entry_offset, details.AsSmi());
}

int DescriptorArray::GetFieldIndex(InternalIndex descriptor_number) {
  DCHECK_EQ(GetDetails(descriptor_number).location(), PropertyLocation::kField);
  return GetDetails(descriptor_number).field_index();
}

Tagged<FieldType> DescriptorArray::GetFieldType(
    InternalIndex descriptor_number) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return GetFieldType(cage_base, descriptor_number);
}

Tagged<FieldType> DescriptorArray::GetFieldType(
    PtrComprCageBase cage_base, InternalIndex descriptor_number) {
  DCHECK_EQ(GetDetails(descriptor_number).location(), PropertyLocation::kField);
  Tagged<MaybeObject> wrapped_type = GetValue(cage_base, descriptor_number);
  return Map::UnwrapFieldType(wrapped_type);
}

void DescriptorArray::Set(InternalIndex descriptor_number, Tagged<Name> key,
                          Tagged<MaybeObject> value, PropertyDetails details) {
  SetKey(descriptor_number, key);
  SetDetails(descriptor_number, details);
  SetValue(descriptor_number, value);
}

void DescriptorArray::Set(InternalIndex descriptor_number, Descriptor* desc) {
  Tagged<Name> key = *desc->GetKey();
  Tagged<MaybeObject> value = *desc->GetValue();
  Set(descriptor_number, key, value, desc->GetDetails());
}

void DescriptorArray::Append(Descriptor* desc) {
  DisallowGarbageCollection no_gc;
  int descriptor_number = number_of_descriptors();
  DCHECK_LE(descriptor_number + 1, number_of_all_descriptors());
  set_number_of_descriptors(descriptor_number + 1);
  Set(InternalIndex(descriptor_number), desc);

  uint32_t desc_hash = desc->GetKey()->hash();
  // Hash value can't be zero, see String::ComputeAndSetHash()
  uint32_t collision_hash = 0;

  int insertion;

  for (insertion = descriptor_number; insertion > 0; --insertion) {
    Tagged<Name> key = GetSortedKey(insertion - 1);
    collision_hash = key->hash();
    if (collision_hash <= desc_hash) break;
    SetSortedKey(insertion, GetSortedKeyIndex(insertion - 1));
  }

  SetSortedKey(insertion, descriptor_number);

  if (V8_LIKELY(collision_hash != desc_hash)) return;

  CheckNameCollisionDuringInsertion(desc, desc_hash, insertion);
}

void DescriptorArray::SwapSortedKeys(int first, int second) {
  int first_key = GetSortedKeyIndex(first);
  SetSortedKey(first, GetSortedKeyIndex(second));
  SetSortedKey(second, first_key);
}

// static
bool DescriptorArrayMarkingState::TryUpdateIndicesToMark(
    unsigned gc_epoch, Tagged<DescriptorArray> array,
    DescriptorIndex index_to_mark) {
  const auto current_epoch = gc_epoch & Epoch::kMask;
  while (true) {
    const RawGCStateType raw_gc_state = array->raw_gc_state(kRelaxedLoad);
    const auto epoch_from_state = Epoch::decode(raw_gc_state);
    RawGCStateType new_raw_gc_state = 0;
    if (current_epoch != epoch_from_state) {
      // If the epochs do not match, then either the raw_gc_state is zero
      // (freshly allocated descriptor array) or the epoch from value lags
      // by 1.
      DCHECK_IMPLIES(raw_gc_state != 0,
                     Epoch::decode(epoch_from_state + 1) == current_epoch);
      new_raw_gc_state = NewState(current_epoch, 0, index_to_mark);
    } else {
      const DescriptorIndex already_marked = Marked::decode(raw_gc_state);
      const DescriptorIndex delta = Delta::decode(raw_gc_state);
      if ((already_marked + delta) >= index_to_mark) {
        return false;
      }
      new_raw_gc_state = NewState(current_epoch, already_marked,
                                  index_to_mark - already_marked);
    }
    if (SwapState(array, raw_gc_state, new_raw_gc_state)) {
      return true;
    }
  }
}

// static
std::pair<DescriptorArrayMarkingState::DescriptorIndex,
          DescriptorArrayMarkingState::DescriptorIndex>
DescriptorArrayMarkingState::AcquireDescriptorRangeToMark(
    unsigned gc_epoch, Tagged<DescriptorArray> array) {
  const auto current_epoch = gc_epoch & Epoch::kMask;
  while (true) {
    const RawGCStateType raw_gc_state = array->raw_gc_state(kRelaxedLoad);
    const DescriptorIndex marked = Marked::decode(raw_gc_state);
    const DescriptorIndex delta = Delta::decode(raw_gc_state);
    // We may encounter an array here that was merely pushed to the marker. In
    // such a case, we process all descriptors (if we succeed). The cases to
    // check are:
    // 1. Epoch mismatch: Happens when descriptors survive a GC cycle.
    // 2. Epoch matches but marked/delta is 0: Can happen when descriptors are
    //    newly allocated in the current cycle.
    if (current_epoch != Epoch::decode(raw_gc_state) || (marked + delta) == 0) {
      // In case number of descriptors is 0 and we reach the array through roots
      // marking, mark also slack to get a proper transition from 0 marked to X
      // marked. Otherwise, we would need to treat the state [0,0[ for marked
      // and delta as valid state which leads to double-accounting through the
      // marking barrier (when nof>1 in the barrier).
      const int16_t number_of_descriptors =
          array->number_of_descriptors() ? array->number_of_descriptors()
                                         : array->number_of_all_descriptors();
      DCHECK_GT(number_of_descriptors, 0);
      if (SwapState(array, raw_gc_state,
                    NewState(current_epoch, number_of_descriptors, 0))) {
        return {0, number_of_descriptors};
      }
      continue;
    }

    // The delta is 0, so everything has been processed. Return the marked
    // indices.
    if (delta == 0) {
      return {marked, marked};
    }

    if (SwapState(array, raw_gc_state,
                  NewState(current_epoch, marked + delta, 0))) {
      return {marked, marked + delta};
    }
  }
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DESCRIPTOR_ARRAY_INL_H_
