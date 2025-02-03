// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROPERTY_ARRAY_INL_H_
#define V8_OBJECTS_PROPERTY_ARRAY_INL_H_

#include "src/objects/property-array.h"

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/property-array-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(PropertyArray)

SMI_ACCESSORS(PropertyArray, length_and_hash, kLengthAndHashOffset)
RELEASE_ACQUIRE_SMI_ACCESSORS(PropertyArray, length_and_hash,
                              kLengthAndHashOffset)

Tagged<JSAny> PropertyArray::get(int index) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return get(cage_base, index);
}

Tagged<JSAny> PropertyArray::get(PtrComprCageBase cage_base, int index) const {
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length(kAcquireLoad)));
  return TaggedField<JSAny>::Relaxed_Load(cage_base, *this,
                                          OffsetOfElementAt(index));
}

Tagged<JSAny> PropertyArray::get(int index, SeqCstAccessTag tag) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return get(cage_base, index, tag);
}

Tagged<JSAny> PropertyArray::get(PtrComprCageBase cage_base, int index,
                                 SeqCstAccessTag tag) const {
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length(kAcquireLoad)));
  return TaggedField<JSAny>::SeqCst_Load(cage_base, *this,
                                         OffsetOfElementAt(index));
}

void PropertyArray::set(int index, Tagged<Object> value) {
  DCHECK(IsPropertyArray(*this));
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length(kAcquireLoad)));
  int offset = OffsetOfElementAt(index);
  RELAXED_WRITE_FIELD(*this, offset, value);
  WRITE_BARRIER(*this, offset, value);
}

void PropertyArray::set(int index, Tagged<Object> value,
                        WriteBarrierMode mode) {
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length(kAcquireLoad)));
  int offset = OffsetOfElementAt(index);
  RELAXED_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
}

void PropertyArray::set(int index, Tagged<Object> value, SeqCstAccessTag tag) {
  DCHECK(IsPropertyArray(*this));
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length(kAcquireLoad)));
  DCHECK(IsShared(value));
  int offset = OffsetOfElementAt(index);
  SEQ_CST_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, UPDATE_WRITE_BARRIER);
}

Tagged<Object> PropertyArray::Swap(int index, Tagged<Object> value,
                                   SeqCstAccessTag tag) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return Swap(cage_base, index, value, tag);
}

Tagged<Object> PropertyArray::Swap(PtrComprCageBase cage_base, int index,
                                   Tagged<Object> value, SeqCstAccessTag tag) {
  DCHECK(IsPropertyArray(*this));
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length(kAcquireLoad)));
  DCHECK(IsShared(value));
  Tagged<Object> result = TaggedField<Object>::SeqCst_Swap(
      cage_base, *this, OffsetOfElementAt(index), value);
  CONDITIONAL_WRITE_BARRIER(*this, OffsetOfElementAt(index), value,
                            UPDATE_WRITE_BARRIER);
  return result;
}

Tagged<Object> PropertyArray::CompareAndSwap(int index, Tagged<Object> expected,
                                             Tagged<Object> value,
                                             SeqCstAccessTag tag) {
  DCHECK(IsPropertyArray(*this));
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(this->length(kAcquireLoad)));
  DCHECK(IsShared(value));
  Tagged<Object> result = TaggedField<Object>::SeqCst_CompareAndSwap(
      *this, OffsetOfElementAt(index), expected, value);
  if (result == expected) {
    CONDITIONAL_WRITE_BARRIER(*this, OffsetOfElementAt(index), value,
                              UPDATE_WRITE_BARRIER);
  }
  return result;
}

ObjectSlot PropertyArray::data_start() { return RawFieldOfElementAt(0); }

ObjectSlot PropertyArray::RawFieldOfElementAt(int index) {
  return RawField(OffsetOfElementAt(index));
}

int PropertyArray::length() const {
  return LengthField::decode(length_and_hash());
}

void PropertyArray::initialize_length(int len) {
  DCHECK(LengthField::is_valid(len));
  set_length_and_hash(len);
}

int PropertyArray::length(AcquireLoadTag) const {
  return LengthField::decode(length_and_hash(kAcquireLoad));
}

int PropertyArray::Hash() const { return HashField::decode(length_and_hash()); }

void PropertyArray::SetHash(int hash) {
  int value = length_and_hash();
  value = HashField::update(value, hash);
  set_length_and_hash(value, kReleaseStore);
}

// static
void PropertyArray::CopyElements(Isolate* isolate, Tagged<PropertyArray> dst,
                                 int dst_index, Tagged<PropertyArray> src,
                                 int src_index, int len,
                                 WriteBarrierMode mode) {
  if (len == 0) return;
  DisallowGarbageCollection no_gc;
  ObjectSlot dst_slot(dst->data_start() + dst_index);
  ObjectSlot src_slot(src->data_start() + src_index);
  isolate->heap()->CopyRange(dst, dst_slot, src_slot, len, mode);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_PROPERTY_ARRAY_INL_H_
