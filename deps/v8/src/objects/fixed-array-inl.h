// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FIXED_ARRAY_INL_H_
#define V8_OBJECTS_FIXED_ARRAY_INL_H_

#include <optional>

#include "src/common/globals.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/numbers/conversions.h"
#include "src/objects/bigint.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/fixed-array.h"
#include "src/objects/map.h"
#include "src/objects/maybe-object-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/slots-inl.h"
#include "src/objects/slots.h"
#include "src/roots/roots-inl.h"
#include "src/torque/runtime-macro-shims.h"
#include "src/torque/runtime-support.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/objects/fixed-array-tq-inl.inc"

template <class D, class S, class P>
int TaggedArrayBase<D, S, P>::capacity() const {
  return Smi::ToInt(TaggedField<Smi, D::kCapacityOffset>::load(*this));
}

template <class D, class S, class P>
int TaggedArrayBase<D, S, P>::capacity(AcquireLoadTag tag) const {
  return Smi::ToInt(TaggedField<Smi, D::kCapacityOffset>::Acquire_Load(*this));
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::set_capacity(int value) {
  TaggedField<Smi, D::kCapacityOffset>::store(*this, Smi::FromInt(value));
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::set_capacity(int value, ReleaseStoreTag tag) {
  TaggedField<Smi, D::kCapacityOffset>::Release_Store(*this,
                                                      Smi::FromInt(value));
}

template <class D, class S, class P>
template <typename, typename>
int TaggedArrayBase<D, S, P>::length() const {
  return capacity();
}

template <class D, class S, class P>
template <typename, typename>
int TaggedArrayBase<D, S, P>::length(AcquireLoadTag tag) const {
  return capacity(tag);
}

template <class D, class S, class P>
template <typename, typename>
void TaggedArrayBase<D, S, P>::set_length(int value) {
  set_capacity(value);
}

template <class D, class S, class P>
template <typename, typename>
void TaggedArrayBase<D, S, P>::set_length(int value, ReleaseStoreTag tag) {
  set_capacity(value, tag);
}

template <class D, class S, class P>
bool TaggedArrayBase<D, S, P>::IsInBounds(int index) const {
  return static_cast<unsigned>(index) < static_cast<unsigned>(capacity());
}

template <class D, class S, class P>
bool TaggedArrayBase<D, S, P>::IsCowArray() const {
  return this->map() ==
         this->EarlyGetReadOnlyRoots().unchecked_fixed_cow_array_map();
}

template <class D, class S, class P>
Tagged<typename TaggedArrayBase<D, S, P>::ElementT>
TaggedArrayBase<D, S, P>::get(int index) const {
  DCHECK(IsInBounds(index));
  // TODO(jgruber): This tag-less overload shouldn't be relaxed.
  return ElementFieldT::Relaxed_Load(*this, OffsetOfElementAt(index));
}

template <class D, class S, class P>
Tagged<typename TaggedArrayBase<D, S, P>::ElementT>
TaggedArrayBase<D, S, P>::get(int index, RelaxedLoadTag) const {
  DCHECK(IsInBounds(index));
  return ElementFieldT::Relaxed_Load(*this, OffsetOfElementAt(index));
}

template <class D, class S, class P>
Tagged<typename TaggedArrayBase<D, S, P>::ElementT>
TaggedArrayBase<D, S, P>::get(int index, AcquireLoadTag) const {
  DCHECK(IsInBounds(index));
  return ElementFieldT::Acquire_Load(*this, OffsetOfElementAt(index));
}

template <class D, class S, class P>
Tagged<typename TaggedArrayBase<D, S, P>::ElementT>
TaggedArrayBase<D, S, P>::get(int index, SeqCstAccessTag) const {
  DCHECK(IsInBounds(index));
  return ElementFieldT::SeqCst_Load(*this, OffsetOfElementAt(index));
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::ConditionalWriteBarrier(
    Tagged<HeapObject> object, int offset, Tagged<ElementT> value,
    WriteBarrierMode mode) {
  if constexpr (kElementsAreMaybeObject) {
    CONDITIONAL_WEAK_WRITE_BARRIER(object, offset, value, mode);
  } else {
    CONDITIONAL_WRITE_BARRIER(object, offset, value, mode);
  }
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<ElementT> value,
                                   WriteBarrierMode mode) {
  DCHECK(!IsCowArray());
  DCHECK(IsInBounds(index));
  // TODO(jgruber): This tag-less overload shouldn't be relaxed.
  const int offset = OffsetOfElementAt(index);
  ElementFieldT::Relaxed_Store(*this, offset, value);
  ConditionalWriteBarrier(*this, offset, value, mode);
}

template <class D, class S, class P>
template <typename, typename>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<Smi> value) {
  set(index, value, SKIP_WRITE_BARRIER);
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<ElementT> value,
                                   RelaxedStoreTag tag, WriteBarrierMode mode) {
  DCHECK(!IsCowArray());
  DCHECK(IsInBounds(index));
  const int offset = OffsetOfElementAt(index);
  ElementFieldT::Relaxed_Store(*this, offset, value);
  ConditionalWriteBarrier(*this, offset, value, mode);
}

template <class D, class S, class P>
template <typename, typename>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<Smi> value,
                                   RelaxedStoreTag tag) {
  set(index, value, tag, SKIP_WRITE_BARRIER);
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<ElementT> value,
                                   ReleaseStoreTag tag, WriteBarrierMode mode) {
  DCHECK(!IsCowArray());
  DCHECK(IsInBounds(index));
  const int offset = OffsetOfElementAt(index);
  ElementFieldT::Release_Store(*this, offset, value);
  ConditionalWriteBarrier(*this, offset, value, mode);
}

template <class D, class S, class P>
template <typename, typename>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<Smi> value,
                                   ReleaseStoreTag tag) {
  set(index, value, tag, SKIP_WRITE_BARRIER);
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<ElementT> value,
                                   SeqCstAccessTag tag, WriteBarrierMode mode) {
  DCHECK(!IsCowArray());
  DCHECK(IsInBounds(index));
  const int offset = OffsetOfElementAt(index);
  ElementFieldT::SeqCst_Store(*this, offset, value);
  ConditionalWriteBarrier(*this, offset, value, mode);
}

template <class D, class S, class P>
template <typename, typename>
void TaggedArrayBase<D, S, P>::set(int index, Tagged<Smi> value,
                                   SeqCstAccessTag tag) {
  set(index, value, tag, SKIP_WRITE_BARRIER);
}

template <class D, class S, class P>
Tagged<typename TaggedArrayBase<D, S, P>::ElementT>
TaggedArrayBase<D, S, P>::swap(int index, Tagged<ElementT> value,
                               SeqCstAccessTag, WriteBarrierMode mode) {
  DCHECK(!IsCowArray());
  DCHECK(IsInBounds(index));
  Tagged<ElementT> previous_value =
      SEQ_CST_SWAP_FIELD(*this, OffsetOfElementAt(index), value);
  ConditionalWriteBarrier(*this, OffsetOfElementAt(index), value, mode);
  return previous_value;
}

template <class D, class S, class P>
Tagged<typename TaggedArrayBase<D, S, P>::ElementT>
TaggedArrayBase<D, S, P>::compare_and_swap(int index, Tagged<ElementT> expected,
                                           Tagged<ElementT> value,
                                           SeqCstAccessTag,
                                           WriteBarrierMode mode) {
  DCHECK(!IsCowArray());
  DCHECK(IsInBounds(index));
  Tagged<ElementT> previous_value = SEQ_CST_COMPARE_AND_SWAP_FIELD(
      *this, OffsetOfElementAt(index), expected, value);
  if (previous_value == expected) {
    ConditionalWriteBarrier(*this, OffsetOfElementAt(index), value, mode);
  }
  return previous_value;
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::MoveElements(Isolate* isolate, Tagged<D> dst,
                                            int dst_index, Tagged<D> src,
                                            int src_index, int len,
                                            WriteBarrierMode mode) {
  if (len == 0) return;

  DCHECK_GE(len, 0);
  DCHECK(dst->IsInBounds(dst_index));
  DCHECK_LE(dst_index + len, dst->length());
  DCHECK(src->IsInBounds(src_index));
  DCHECK_LE(src_index + len, src->length());

  DisallowGarbageCollection no_gc;
  SlotType dst_slot(dst->RawFieldOfElementAt(dst_index));
  SlotType src_slot(src->RawFieldOfElementAt(src_index));
  isolate->heap()->MoveRange(dst, dst_slot, src_slot, len, mode);
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::CopyElements(Isolate* isolate, Tagged<D> dst,
                                            int dst_index, Tagged<D> src,
                                            int src_index, int len,
                                            WriteBarrierMode mode) {
  if (len == 0) return;

  DCHECK_GE(len, 0);
  DCHECK(dst->IsInBounds(dst_index));
  DCHECK_LE(dst_index + len, dst->capacity());
  DCHECK(src->IsInBounds(src_index));
  DCHECK_LE(src_index + len, src->capacity());

  DisallowGarbageCollection no_gc;
  SlotType dst_slot(dst->RawFieldOfElementAt(dst_index));
  SlotType src_slot(src->RawFieldOfElementAt(src_index));
  isolate->heap()->CopyRange(dst, dst_slot, src_slot, len, mode);
}

template <class D, class S, class P>
void TaggedArrayBase<D, S, P>::RightTrim(Isolate* isolate, int new_capacity) {
  int old_capacity = capacity();
  CHECK_GT(new_capacity, 0);  // Due to possible canonicalization.
  CHECK_LE(new_capacity, old_capacity);
  if (new_capacity == old_capacity) return;
  isolate->heap()->RightTrimArray(Cast<D>(*this), new_capacity, old_capacity);
}

// Due to right-trimming (which creates a filler object before publishing the
// length through a release-store, see Heap::RightTrimArray), concurrent
// visitors need to read the length with acquire semantics.
template <class D, class S, class P>
int TaggedArrayBase<D, S, P>::AllocatedSize() const {
  return SizeFor(capacity(kAcquireLoad));
}

template <class D, class S, class P>
typename TaggedArrayBase<D, S, P>::SlotType
TaggedArrayBase<D, S, P>::RawFieldOfFirstElement() const {
  return RawFieldOfElementAt(0);
}

template <class D, class S, class P>
typename TaggedArrayBase<D, S, P>::SlotType
TaggedArrayBase<D, S, P>::RawFieldOfElementAt(int index) const {
  if constexpr (kElementsAreMaybeObject) {
    return this->RawMaybeWeakField(OffsetOfElementAt(index));
  } else {
    return this->RawField(OffsetOfElementAt(index));
  }
}

// static
template <class IsolateT>
Handle<FixedArray> FixedArray::New(IsolateT* isolate, int capacity,
                                   AllocationType allocation) {
  if (V8_UNLIKELY(static_cast<unsigned>(capacity) >
                  FixedArrayBase::kMaxLength)) {
    FATAL("Fatal JavaScript invalid size error %d (see crbug.com/1201626)",
          capacity);
  } else if (V8_UNLIKELY(capacity == 0)) {
    return isolate->factory()->empty_fixed_array();
  }

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<FixedArray> result =
      Cast<FixedArray>(Allocate(isolate, capacity, &no_gc, allocation));
  ReadOnlyRoots roots{isolate};
  MemsetTagged((*result)->RawFieldOfFirstElement(), roots.undefined_value(),
               capacity);
  return result;
}

// static
template <class IsolateT>
Handle<TrustedFixedArray> TrustedFixedArray::New(IsolateT* isolate,
                                                 int capacity) {
  if (V8_UNLIKELY(static_cast<unsigned>(capacity) >
                  TrustedFixedArray::kMaxLength)) {
    FATAL("Fatal JavaScript invalid size error %d (see crbug.com/1201626)",
          capacity);
  }
  // TODO(saelo): once we have trusted read-only roots, we can return the
  // empty_trusted_fixed_array here. Currently this isn't possible because the
  // (mutable) empty_trusted_fixed_array will be created via this function.
  // The same is true for the other trusted-space arrays below.

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<TrustedFixedArray> result = Cast<TrustedFixedArray>(
      Allocate(isolate, capacity, &no_gc, AllocationType::kTrusted));
  MemsetTagged((*result)->RawFieldOfFirstElement(), Smi::zero(), capacity);
  return result;
}

// static
template <class IsolateT>
Handle<ProtectedFixedArray> ProtectedFixedArray::New(IsolateT* isolate,
                                                     int capacity) {
  if (V8_UNLIKELY(static_cast<unsigned>(capacity) >
                  ProtectedFixedArray::kMaxLength)) {
    FATAL("Fatal JavaScript invalid size error %d (see crbug.com/1201626)",
          capacity);
  }

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<ProtectedFixedArray> result = Cast<ProtectedFixedArray>(
      Allocate(isolate, capacity, &no_gc, AllocationType::kTrusted));
  MemsetTagged((*result)->RawFieldOfFirstElement(), Smi::zero(), capacity);
  return result;
}

// static
template <class D, class S, class P>
template <class IsolateT>
Handle<D> TaggedArrayBase<D, S, P>::Allocate(
    IsolateT* isolate, int capacity,
    std::optional<DisallowGarbageCollection>* no_gc_out,
    AllocationType allocation) {
  // Note 0-capacity is explicitly allowed since not all subtypes can be
  // assumed to have canonical 0-capacity instances.
  DCHECK_GE(capacity, 0);
  DCHECK_LE(capacity, kMaxCapacity);
  DCHECK(!no_gc_out->has_value());

  Tagged<D> xs = UncheckedCast<D>(
      isolate->factory()->AllocateRawArray(SizeFor(capacity), allocation));

  ReadOnlyRoots roots{isolate};
  if (DEBUG_BOOL) no_gc_out->emplace();
  Tagged<Map> map = Cast<Map>(roots.object_at(S::kMapRootIndex));
  DCHECK(ReadOnlyHeap::Contains(map));

  xs->set_map_after_allocation(map, SKIP_WRITE_BARRIER);
  xs->set_capacity(capacity);

  return handle(xs, isolate);
}

// static
template <class D, class S, class P>
constexpr int TaggedArrayBase<D, S, P>::NewCapacityForIndex(int index,
                                                            int old_capacity) {
  DCHECK_GE(index, old_capacity);
  // Note this is currently based on JSObject::NewElementsCapacity.
  int capacity = old_capacity;
  do {
    capacity = capacity + (capacity >> 1) + 16;
  } while (capacity <= index);
  return capacity;
}

int FixedArrayBase::length() const {
  return Smi::ToInt(
      TaggedField<Smi, TaggedArrayShape::kCapacityOffset>::load(*this));
}

int FixedArrayBase::length(AcquireLoadTag tag) const {
  return Smi::ToInt(
      TaggedField<Smi, TaggedArrayShape::kCapacityOffset>::Acquire_Load(*this));
}

void FixedArrayBase::set_length(int value) {
  TaggedField<Smi, TaggedArrayShape::kCapacityOffset>::store(
      *this, Smi::FromInt(value));
}

void FixedArrayBase::set_length(int value, ReleaseStoreTag tag) {
  TaggedField<Smi, TaggedArrayShape::kCapacityOffset>::Release_Store(
      *this, Smi::FromInt(value));
}

OBJECT_CONSTRUCTORS_IMPL(WeakFixedArray, WeakFixedArray::Super)

OBJECT_CONSTRUCTORS_IMPL(TrustedWeakFixedArray, TrustedWeakFixedArray::Super)

TQ_OBJECT_CONSTRUCTORS_IMPL(WeakArrayList)

template <class D, class S, class P>
TaggedArrayBase<D, S, P>::TaggedArrayBase(Address ptr) : P(ptr) {}
template <class D, class S, class P>
PrimitiveArrayBase<D, S, P>::PrimitiveArrayBase(Address ptr) : P(ptr) {}

OBJECT_CONSTRUCTORS_IMPL(FixedArrayBase, HeapObject)

OBJECT_CONSTRUCTORS_IMPL(FixedArray, FixedArray::Super)

OBJECT_CONSTRUCTORS_IMPL(TrustedFixedArray, TrustedFixedArray::Super)

OBJECT_CONSTRUCTORS_IMPL(ProtectedFixedArray, ProtectedFixedArray::Super)

OBJECT_CONSTRUCTORS_IMPL(FixedDoubleArray, FixedDoubleArray::Super)

OBJECT_CONSTRUCTORS_IMPL(ByteArray, ByteArray::Super)

OBJECT_CONSTRUCTORS_IMPL(TrustedByteArray, TrustedByteArray::Super)

OBJECT_CONSTRUCTORS_IMPL(ExternalPointerArray, FixedArrayBase)

OBJECT_CONSTRUCTORS_IMPL(ArrayList, ArrayList::Super)

NEVER_READ_ONLY_SPACE_IMPL(WeakArrayList)

bool FixedArray::is_the_hole(Isolate* isolate, int index) {
  return IsTheHole(get(index), isolate);
}

void FixedArray::set_the_hole(Isolate* isolate, int index) {
  set_the_hole(ReadOnlyRoots(isolate), index);
}

void FixedArray::set_the_hole(ReadOnlyRoots ro_roots, int index) {
  set(index, ro_roots.the_hole_value(), SKIP_WRITE_BARRIER);
}

void FixedArray::FillWithHoles(int from, int to) {
  ReadOnlyRoots roots = GetReadOnlyRoots();
  for (int i = from; i < to; i++) {
    set(i, roots.the_hole_value(), SKIP_WRITE_BARRIER);
  }
}

void FixedArray::MoveElements(Isolate* isolate, int dst_index, int src_index,
                              int len, WriteBarrierMode mode) {
  MoveElements(isolate, *this, dst_index, *this, src_index, len, mode);
}

void FixedArray::CopyElements(Isolate* isolate, int dst_index,
                              Tagged<FixedArray> src, int src_index, int len,
                              WriteBarrierMode mode) {
  CopyElements(isolate, *this, dst_index, src, src_index, len, mode);
}

// static
Handle<FixedArray> FixedArray::Resize(Isolate* isolate,
                                      DirectHandle<FixedArray> xs,
                                      int new_capacity,
                                      AllocationType allocation,
                                      WriteBarrierMode mode) {
  Handle<FixedArray> ys = New(isolate, new_capacity, allocation);
  int elements_to_copy = std::min(new_capacity, xs->capacity());
  FixedArray::CopyElements(isolate, *ys, 0, *xs, 0, elements_to_copy, mode);
  return ys;
}

inline int WeakArrayList::AllocatedSize() const { return SizeFor(capacity()); }

// Perform a binary search in a fixed array.
template <SearchMode search_mode, typename T>
int BinarySearch(T* array, Tagged<Name> name, int valid_entries,
                 int* out_insertion_index) {
  DCHECK_IMPLIES(search_mode == VALID_ENTRIES, out_insertion_index == nullptr);
  int low = 0;
  // We have to search on all entries, even when search_mode == VALID_ENTRIES.
  // This is because the InternalIndex might be different from the SortedIndex
  // (i.e the first added item in {array} could be the last in the sorted
  // index). After doing the binary search and getting the correct internal
  // index we check to have the index lower than valid_entries, if needed.
  int high = array->number_of_entries() - 1;
  uint32_t hash = name->hash();
  int limit = high;

  DCHECK(low <= high);

  while (low != high) {
    int mid = low + (high - low) / 2;
    Tagged<Name> mid_name = array->GetSortedKey(mid);
    uint32_t mid_hash = mid_name->hash();

    if (mid_hash >= hash) {
      high = mid;
    } else {
      low = mid + 1;
    }
  }

  for (; low <= limit; ++low) {
    int sort_index = array->GetSortedKeyIndex(low);
    Tagged<Name> entry = array->GetKey(InternalIndex(sort_index));
    uint32_t current_hash = entry->hash();
    if (current_hash != hash) {
      // 'search_mode == ALL_ENTRIES' here and below is not needed since
      // 'out_insertion_index != nullptr' implies 'search_mode == ALL_ENTRIES'.
      // Having said that, when creating the template for <VALID_ENTRIES> these
      // ifs can be elided by the C++ compiler if we add 'search_mode ==
      // ALL_ENTRIES'.
      if (search_mode == ALL_ENTRIES && out_insertion_index != nullptr) {
        *out_insertion_index = sort_index + (current_hash > hash ? 0 : 1);
      }
      return T::kNotFound;
    }
    if (entry == name) {
      if (search_mode == ALL_ENTRIES || sort_index < valid_entries) {
        return sort_index;
      }
      return T::kNotFound;
    }
  }

  if (search_mode == ALL_ENTRIES && out_insertion_index != nullptr) {
    *out_insertion_index = limit + 1;
  }
  return T::kNotFound;
}

// Perform a linear search in this fixed array. len is the number of entry
// indices that are valid.
template <SearchMode search_mode, typename T>
int LinearSearch(T* array, Tagged<Name> name, int valid_entries,
                 int* out_insertion_index) {
  if (search_mode == ALL_ENTRIES && out_insertion_index != nullptr) {
    uint32_t hash = name->hash();
    int len = array->number_of_entries();
    for (int number = 0; number < len; number++) {
      int sorted_index = array->GetSortedKeyIndex(number);
      Tagged<Name> entry = array->GetKey(InternalIndex(sorted_index));
      uint32_t current_hash = entry->hash();
      if (current_hash > hash) {
        *out_insertion_index = sorted_index;
        return T::kNotFound;
      }
      if (entry == name) return sorted_index;
    }
    *out_insertion_index = len;
    return T::kNotFound;
  } else {
    DCHECK_LE(valid_entries, array->number_of_entries());
    DCHECK_NULL(out_insertion_index);  // Not supported here.
    for (int number = 0; number < valid_entries; number++) {
      if (array->GetKey(InternalIndex(number)) == name) return number;
    }
    return T::kNotFound;
  }
}

template <SearchMode search_mode, typename T>
int Search(T* array, Tagged<Name> name, int valid_entries,
           int* out_insertion_index, bool concurrent_search) {
  SLOW_DCHECK_IMPLIES(!concurrent_search, array->IsSortedNoDuplicates());

  if (valid_entries == 0) {
    if (search_mode == ALL_ENTRIES && out_insertion_index != nullptr) {
      *out_insertion_index = 0;
    }
    return T::kNotFound;
  }

  // Do linear search for small arrays, and for searches in the background
  // thread.
  const int kMaxElementsForLinearSearch = 8;
  if (valid_entries <= kMaxElementsForLinearSearch || concurrent_search) {
    return LinearSearch<search_mode>(array, name, valid_entries,
                                     out_insertion_index);
  }

  return BinarySearch<search_mode>(array, name, valid_entries,
                                   out_insertion_index);
}

template <class D, class S, class P>
int PrimitiveArrayBase<D, S, P>::length() const {
  return Smi::ToInt(TaggedField<Smi, D::kLengthOffset>::load(*this));
}

template <class D, class S, class P>
int PrimitiveArrayBase<D, S, P>::length(AcquireLoadTag tag) const {
  return Smi::ToInt(TaggedField<Smi, D::kLengthOffset>::Acquire_Load(*this));
}

template <class D, class S, class P>
void PrimitiveArrayBase<D, S, P>::set_length(int value) {
  TaggedField<Smi, D::kLengthOffset>::store(*this, Smi::FromInt(value));
}

template <class D, class S, class P>
void PrimitiveArrayBase<D, S, P>::set_length(int value, ReleaseStoreTag tag) {
  TaggedField<Smi, D::kLengthOffset>::Release_Store(*this, Smi::FromInt(value));
}

template <class D, class S, class P>
int PrimitiveArrayBase<D, S, P>::capacity() const {
  return length();
}
template <class D, class S, class P>
int PrimitiveArrayBase<D, S, P>::capacity(AcquireLoadTag tag) const {
  return length(tag);
}
template <class D, class S, class P>
void PrimitiveArrayBase<D, S, P>::set_capacity(int value) {
  set_length(value);
}
template <class D, class S, class P>
void PrimitiveArrayBase<D, S, P>::set_capacity(int value, ReleaseStoreTag tag) {
  set_length(value, tag);
}

template <class D, class S, class P>
bool PrimitiveArrayBase<D, S, P>::IsInBounds(int index) const {
  return static_cast<unsigned>(index) < static_cast<unsigned>(length());
}

template <class D, class S, class P>
typename S::ElementT PrimitiveArrayBase<D, S, P>::get(int index) const {
  DCHECK(IsInBounds(index));
  return this->template ReadField<typename S::ElementT>(
      OffsetOfElementAt(index));
}

template <class D, class S, class P>
void PrimitiveArrayBase<D, S, P>::set(int index, typename S::ElementT value) {
  DCHECK(IsInBounds(index));
  this->template WriteField<typename S::ElementT>(OffsetOfElementAt(index),
                                                  value);
}

// Due to right-trimming (which creates a filler object before publishing the
// length through a release-store, see Heap::RightTrimArray), concurrent
// visitors need to read the length with acquire semantics.
template <class D, class S, class P>
int PrimitiveArrayBase<D, S, P>::AllocatedSize() const {
  return SizeFor(length(kAcquireLoad));
}

template <class D, class S, class P>
typename S::ElementT* PrimitiveArrayBase<D, S, P>::AddressOfElementAt(
    int index) const {
  return reinterpret_cast<ElementT*>(
      this->field_address(OffsetOfElementAt(index)));
}

template <class D, class S, class P>
typename S::ElementT* PrimitiveArrayBase<D, S, P>::begin() const {
  return AddressOfElementAt(0);
}

template <class D, class S, class P>
typename S::ElementT* PrimitiveArrayBase<D, S, P>::end() const {
  return AddressOfElementAt(length());
}

template <class D, class S, class P>
int PrimitiveArrayBase<D, S, P>::DataSize() const {
  int data_size = SizeFor(length()) - S::kHeaderSize;
  DCHECK_EQ(data_size, OBJECT_POINTER_ALIGN(length() * S::kElementSize));
  return data_size;
}

// static
template <class D, class S, class P>
inline Tagged<D> PrimitiveArrayBase<D, S, P>::FromAddressOfFirstElement(
    Address address) {
  DCHECK_TAG_ALIGNED(address);
  return Cast<D>(Tagged<Object>(address - S::kHeaderSize + kHeapObjectTag));
}

// static
template <class IsolateT>
Handle<FixedArrayBase> FixedDoubleArray::New(IsolateT* isolate, int length,
                                             AllocationType allocation) {
  if (V8_UNLIKELY(static_cast<unsigned>(length) > kMaxLength)) {
    FATAL("Fatal JavaScript invalid size error %d (see crbug.com/1201626)",
          length);
  } else if (V8_UNLIKELY(length == 0)) {
    return isolate->factory()->empty_fixed_array();
  }

  std::optional<DisallowGarbageCollection> no_gc;
  return Cast<FixedDoubleArray>(Allocate(isolate, length, &no_gc, allocation));
}

// static
template <class D, class S, class P>
template <class IsolateT>
Handle<D> PrimitiveArrayBase<D, S, P>::Allocate(
    IsolateT* isolate, int length,
    std::optional<DisallowGarbageCollection>* no_gc_out,
    AllocationType allocation) {
  // Note 0-length is explicitly allowed since not all subtypes can be
  // assumed to have canonical 0-length instances.
  DCHECK_GE(length, 0);
  DCHECK_LE(length, kMaxLength);
  DCHECK(!no_gc_out->has_value());

  Tagged<D> xs = UncheckedCast<D>(
      isolate->factory()->AllocateRawArray(SizeFor(length), allocation));

  ReadOnlyRoots roots{isolate};
  if (DEBUG_BOOL) no_gc_out->emplace();
  Tagged<Map> map = Cast<Map>(roots.object_at(S::kMapRootIndex));
  DCHECK(ReadOnlyHeap::Contains(map));

  xs->set_map_after_allocation(map, SKIP_WRITE_BARRIER);
  xs->set_length(length);

  return handle(xs, isolate);
}

double FixedDoubleArray::get_scalar(int index) {
  DCHECK(!is_the_hole(index));
  return Super::get(index);
}

uint64_t FixedDoubleArray::get_representation(int index) {
  DCHECK(IsInBounds(index));
  // Bug(v8:8875): Doubles may be unaligned.
  return base::ReadUnalignedValue<uint64_t>(
      field_address(OffsetOfElementAt(index)));
}

Handle<Object> FixedDoubleArray::get(Tagged<FixedDoubleArray> array, int index,
                                     Isolate* isolate) {
  if (array->is_the_hole(index)) {
    return ReadOnlyRoots(isolate).the_hole_value_handle();
  } else {
    return isolate->factory()->NewNumber(array->get_scalar(index));
  }
}

void FixedDoubleArray::set(int index, double value) {
  if (std::isnan(value)) {
    value = std::numeric_limits<double>::quiet_NaN();
  }
  Super::set(index, value);
  DCHECK(!is_the_hole(index));
}

void FixedDoubleArray::set_the_hole(Isolate* isolate, int index) {
  set_the_hole(index);
}

void FixedDoubleArray::set_the_hole(int index) {
  DCHECK(IsInBounds(index));
  base::WriteUnalignedValue<uint64_t>(field_address(OffsetOfElementAt(index)),
                                      kHoleNanInt64);
}

bool FixedDoubleArray::is_the_hole(Isolate* isolate, int index) {
  return is_the_hole(index);
}

bool FixedDoubleArray::is_the_hole(int index) {
  return get_representation(index) == kHoleNanInt64;
}

void FixedDoubleArray::MoveElements(Isolate* isolate, int dst_index,
                                    int src_index, int len,
                                    WriteBarrierMode mode) {
  DCHECK_EQ(SKIP_WRITE_BARRIER, mode);
  MemMove(AddressOfElementAt(dst_index), AddressOfElementAt(src_index),
          len * Shape::kElementSize);
}

void FixedDoubleArray::FillWithHoles(int from, int to) {
  for (int i = from; i < to; i++) {
    set_the_hole(i);
  }
}

// static
template <class IsolateT>
Handle<WeakFixedArray> WeakFixedArray::New(IsolateT* isolate, int capacity,
                                           AllocationType allocation,
                                           MaybeHandle<Object> initial_value) {
  CHECK_LE(static_cast<unsigned>(capacity), kMaxCapacity);

  if (V8_UNLIKELY(capacity == 0)) {
    return isolate->factory()->empty_weak_fixed_array();
  }

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<WeakFixedArray> result =
      Cast<WeakFixedArray>(Allocate(isolate, capacity, &no_gc, allocation));
  ReadOnlyRoots roots{isolate};
  MemsetTagged((*result)->RawFieldOfFirstElement(),
               initial_value.is_null() ? roots.undefined_value()
                                       : *initial_value.ToHandleChecked(),
               capacity);
  return result;
}

template <class IsolateT>
Handle<TrustedWeakFixedArray> TrustedWeakFixedArray::New(IsolateT* isolate,
                                                         int capacity) {
  if (V8_UNLIKELY(static_cast<unsigned>(capacity) >
                  TrustedFixedArray::kMaxLength)) {
    FATAL("Fatal JavaScript invalid size error %d (see crbug.com/1201626)",
          capacity);
  }

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<TrustedWeakFixedArray> result = Cast<TrustedWeakFixedArray>(
      Allocate(isolate, capacity, &no_gc, AllocationType::kTrusted));
  MemsetTagged((*result)->RawFieldOfFirstElement(), Smi::zero(), capacity);
  return result;
}

Tagged<MaybeObject> WeakArrayList::Get(int index) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return Get(cage_base, index);
}
Tagged<MaybeObject> WeakArrayList::get(int index) const { return Get(index); }

Tagged<MaybeObject> WeakArrayList::Get(PtrComprCageBase cage_base,
                                       int index) const {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(capacity()));
  return objects(cage_base, index, kRelaxedLoad);
}

void WeakArrayList::Set(int index, Tagged<MaybeObject> value,
                        WriteBarrierMode mode) {
  set_objects(index, value, mode);
}

void WeakArrayList::Set(int index, Tagged<Smi> value) {
  Set(index, value, SKIP_WRITE_BARRIER);
}

MaybeObjectSlot WeakArrayList::data_start() {
  return RawMaybeWeakField(kObjectsOffset);
}

void WeakArrayList::CopyElements(Isolate* isolate, int dst_index,
                                 Tagged<WeakArrayList> src, int src_index,
                                 int len, WriteBarrierMode mode) {
  if (len == 0) return;
  DCHECK_LE(dst_index + len, capacity());
  DCHECK_LE(src_index + len, src->capacity());
  DisallowGarbageCollection no_gc;

  MaybeObjectSlot dst_slot(data_start() + dst_index);
  MaybeObjectSlot src_slot(src->data_start() + src_index);
  isolate->heap()->CopyRange(*this, dst_slot, src_slot, len, mode);
}

Tagged<HeapObject> WeakArrayList::Iterator::Next() {
  if (!array_.is_null()) {
    while (index_ < array_->length()) {
      Tagged<MaybeObject> item = array_->Get(index_++);
      DCHECK(item.IsWeakOrCleared());
      if (!item.IsCleared()) return item.GetHeapObjectAssumeWeak();
    }
    array_ = WeakArrayList();
  }
  return Tagged<HeapObject>();
}

SMI_ACCESSORS(ArrayList, length, ArrayList::Shape::kLengthOffset)

// static
template <class IsolateT>
Handle<ArrayList> ArrayList::New(IsolateT* isolate, int capacity,
                                 AllocationType allocation) {
  if (capacity == 0) return isolate->factory()->empty_array_list();

  DCHECK_GT(capacity, 0);
  DCHECK_LE(capacity, kMaxCapacity);

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<ArrayList> result =
      Cast<ArrayList>(Allocate(isolate, capacity, &no_gc, allocation));
  result->set_length(0);
  ReadOnlyRoots roots{isolate};
  MemsetTagged(result->RawFieldOfFirstElement(), roots.undefined_value(),
               capacity);
  return result;
}

// static
template <class IsolateT>
Handle<ByteArray> ByteArray::New(IsolateT* isolate, int length,
                                 AllocationType allocation) {
  if (V8_UNLIKELY(static_cast<unsigned>(length) > kMaxLength)) {
    FATAL("Fatal JavaScript invalid size error %d", length);
  } else if (V8_UNLIKELY(length == 0)) {
    return isolate->factory()->empty_byte_array();
  }

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<ByteArray> result =
      Cast<ByteArray>(Allocate(isolate, length, &no_gc, allocation));

  int padding_size = SizeFor(length) - OffsetOfElementAt(length);
  memset(result->AddressOfElementAt(length), 0, padding_size);

  return result;
}

uint32_t ByteArray::get_int(int offset) const {
  DCHECK(IsInBounds(offset));
  DCHECK_LE(offset + sizeof(uint32_t), length());
  return ReadField<uint32_t>(OffsetOfElementAt(offset));
}

void ByteArray::set_int(int offset, uint32_t value) {
  DCHECK(IsInBounds(offset));
  DCHECK_LE(offset + sizeof(uint32_t), length());
  WriteField<uint32_t>(OffsetOfElementAt(offset), value);
}

// static
template <class IsolateT>
Handle<TrustedByteArray> TrustedByteArray::New(IsolateT* isolate, int length,
                                               AllocationType allocation_type) {
  DCHECK(allocation_type == AllocationType::kTrusted ||
         allocation_type == AllocationType::kSharedTrusted);
  if (V8_UNLIKELY(static_cast<unsigned>(length) > kMaxLength)) {
    FATAL("Fatal JavaScript invalid size error %d", length);
  }

  std::optional<DisallowGarbageCollection> no_gc;
  Handle<TrustedByteArray> result = Cast<TrustedByteArray>(
      Allocate(isolate, length, &no_gc, allocation_type));

  int padding_size = SizeFor(length) - OffsetOfElementAt(length);
  memset(result->AddressOfElementAt(length), 0, padding_size);

  return result;
}

template <typename Base>
Address FixedAddressArrayBase<Base>::get_sandboxed_pointer(int offset) const {
  DCHECK_GE(offset, 0);
  DCHECK_GT(this->length(), offset);
  PtrComprCageBase sandbox_base = GetPtrComprCageBase(*this);
  return this->ReadSandboxedPointerField(
      FixedAddressArrayBase::OffsetOfElementAt(offset), sandbox_base);
}

template <typename Base>
void FixedAddressArrayBase<Base>::set_sandboxed_pointer(int offset,
                                                        Address value) {
  DCHECK_GE(offset, 0);
  DCHECK_GT(this->length(), offset);
  PtrComprCageBase sandbox_base = GetPtrComprCageBase(*this);
  this->WriteSandboxedPointerField(
      FixedAddressArrayBase::OffsetOfElementAt(offset), sandbox_base, value);
}

template <typename Base>
template <typename... MoreArgs>
// static
Handle<FixedAddressArrayBase<Base>> FixedAddressArrayBase<Base>::New(
    Isolate* isolate, int length, MoreArgs&&... more_args) {
  return Cast<FixedAddressArrayBase>(
      Underlying::New(isolate, length, std::forward<MoreArgs>(more_args)...));
}

template <typename Base>
FixedAddressArrayBase<Base>::FixedAddressArrayBase(Address ptr)
    : Underlying(ptr) {}

template <typename T, typename Base>
FixedIntegerArrayBase<T, Base>::FixedIntegerArrayBase(Address ptr) : Base(ptr) {
  DCHECK_EQ(Base::length() % sizeof(T), 0);
}

template <typename T, typename Base>
template <typename... MoreArgs>
// static
Handle<FixedIntegerArrayBase<T, Base>> FixedIntegerArrayBase<T, Base>::New(
    Isolate* isolate, int length, MoreArgs&&... more_args) {
  int byte_length;
  CHECK(!base::bits::SignedMulOverflow32(length, sizeof(T), &byte_length));
  return Cast<FixedIntegerArrayBase<T, Base>>(
      Base::New(isolate, byte_length, std::forward<MoreArgs>(more_args)...));
}

template <typename T, typename Base>
T FixedIntegerArrayBase<T, Base>::get(int index) const {
  static_assert(std::is_integral<T>::value);
  DCHECK_GE(index, 0);
  DCHECK_LT(index, length());
  return this->template ReadField<T>(Base::kHeaderSize + index * sizeof(T));
}

template <typename T, typename Base>
void FixedIntegerArrayBase<T, Base>::set(int index, T value) {
  static_assert(std::is_integral<T>::value);
  DCHECK_GE(index, 0);
  DCHECK_LT(index, length());
  this->template WriteField<T>(Base::kHeaderSize + index * sizeof(T), value);
}

template <typename T, typename Base>
int FixedIntegerArrayBase<T, Base>::length() const {
  DCHECK_EQ(Base::length() % sizeof(T), 0);
  return Base::length() / sizeof(T);
}

template <ExternalPointerTag tag>
inline Address ExternalPointerArray::get(int index, Isolate* isolate) {
  return ReadExternalPointerField<tag>(OffsetOfElementAt(index), isolate);
}

template <ExternalPointerTag tag>
inline void ExternalPointerArray::set(int index, Isolate* isolate,
                                      Address value) {
  WriteLazilyInitializedExternalPointerField<tag>(OffsetOfElementAt(index),
                                                  isolate, value);
}

// static
Handle<ExternalPointerArray> ExternalPointerArray::New(
    Isolate* isolate, int length, AllocationType allocation) {
  return isolate->factory()->NewExternalPointerArray(length, allocation);
}

template <class T, class Super>
int PodArrayBase<T, Super>::length() const {
  return Super::length() / sizeof(T);
}

template <class T, class Super>
PodArrayBase<T, Super>::PodArrayBase(Address ptr) : Super(ptr) {}

// static
template <class T>
Handle<PodArray<T>> PodArray<T>::New(Isolate* isolate, int length,
                                     AllocationType allocation) {
  int byte_length;
  CHECK(!base::bits::SignedMulOverflow32(length, sizeof(T), &byte_length));
  return Cast<PodArray<T>>(
      isolate->factory()->NewByteArray(byte_length, allocation));
}

// static
template <class T>
Handle<PodArray<T>> PodArray<T>::New(LocalIsolate* isolate, int length,
                                     AllocationType allocation) {
  int byte_length;
  CHECK(!base::bits::SignedMulOverflow32(length, sizeof(T), &byte_length));
  return Cast<PodArray<T>>(
      isolate->factory()->NewByteArray(byte_length, allocation));
}

template <class T>
PodArray<T>::PodArray(Address ptr) : PodArrayBase<T, ByteArray>(ptr) {}

// static
template <class T>
Handle<TrustedPodArray<T>> TrustedPodArray<T>::New(Isolate* isolate,
                                                   int length) {
  int byte_length;
  CHECK(!base::bits::SignedMulOverflow32(length, sizeof(T), &byte_length));
  return Cast<TrustedPodArray<T>>(
      isolate->factory()->NewTrustedByteArray(byte_length));
}

// static
template <class T>
Handle<TrustedPodArray<T>> TrustedPodArray<T>::New(LocalIsolate* isolate,
                                                   int length) {
  int byte_length;
  CHECK(!base::bits::SignedMulOverflow32(length, sizeof(T), &byte_length));
  return Cast<TrustedPodArray<T>>(
      isolate->factory()->NewTrustedByteArray(byte_length));
}

template <class T>
TrustedPodArray<T>::TrustedPodArray(Address ptr)
    : PodArrayBase<T, TrustedByteArray>(ptr) {}

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FIXED_ARRAY_INL_H_
