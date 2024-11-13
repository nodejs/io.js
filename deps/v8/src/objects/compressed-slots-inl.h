// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_COMPRESSED_SLOTS_INL_H_
#define V8_OBJECTS_COMPRESSED_SLOTS_INL_H_

#ifdef V8_COMPRESS_POINTERS

#include "src/common/ptr-compr-inl.h"
#include "src/objects/casting.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/maybe-object-inl.h"

namespace v8::internal {

//
// CompressedObjectSlot implementation.
//

CompressedObjectSlot::CompressedObjectSlot(Tagged<Object>* object)
    : SlotBase(reinterpret_cast<Address>(&object->ptr_)) {}

bool CompressedObjectSlot::contains_map_value(Address raw_value) const {
  DCHECK(!V8_MAP_PACKING_BOOL);
  Tagged_t value = *location();
  return static_cast<uint32_t>(value) ==
         static_cast<uint32_t>(static_cast<Tagged_t>(raw_value));
}

bool CompressedObjectSlot::Relaxed_ContainsMapValue(Address raw_value) const {
  DCHECK(!V8_MAP_PACKING_BOOL);
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location());
  return static_cast<uint32_t>(value) ==
         static_cast<uint32_t>(static_cast<Tagged_t>(raw_value));
}

Tagged<Object> CompressedObjectSlot::operator*() const {
  Tagged_t value = *location();
  return Tagged<Object>(TCompressionScheme::DecompressTagged(address(), value));
}

Tagged<Object> CompressedObjectSlot::load() const {
  AtomicTagged_t value = *location();
  return Tagged<Object>(TCompressionScheme::DecompressTagged(address(), value));
}

Tagged<Object> CompressedObjectSlot::load(PtrComprCageBase cage_base) const {
  Tagged_t value = *location();
  return Tagged<Object>(TCompressionScheme::DecompressTagged(cage_base, value));
}

void CompressedObjectSlot::store(Tagged<Object> value) const {
  *location() = TCompressionScheme::CompressObject(value.ptr());
}

void CompressedObjectSlot::store_map(Tagged<Map> map) const {
  // Simply forward to store because map packing is not supported with pointer
  // compression.
  DCHECK(!V8_MAP_PACKING_BOOL);
  store(map);
}

Tagged<Map> CompressedObjectSlot::load_map() const {
  // Simply forward to Relaxed_Load because map packing is not supported with
  // pointer compression.
  DCHECK(!V8_MAP_PACKING_BOOL);
  return UncheckedCast<Map>(Relaxed_Load());
}

Tagged<Object> CompressedObjectSlot::Acquire_Load() const {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location());
  return Tagged<Object>(TCompressionScheme::DecompressTagged(address(), value));
}

Tagged<Object> CompressedObjectSlot::Relaxed_Load() const {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location());
  return Tagged<Object>(TCompressionScheme::DecompressTagged(address(), value));
}

Tagged<Object> CompressedObjectSlot::Relaxed_Load(
    PtrComprCageBase cage_base) const {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location());
  return Tagged<Object>(TCompressionScheme::DecompressTagged(cage_base, value));
}

Tagged_t CompressedObjectSlot::Relaxed_Load_Raw() const {
  return static_cast<Tagged_t>(AsAtomicTagged::Relaxed_Load(location()));
}

// static
Tagged<Object> CompressedObjectSlot::RawToTagged(PtrComprCageBase cage_base,
                                                 Tagged_t raw) {
  return Tagged<Object>(TCompressionScheme::DecompressTagged(cage_base, raw));
}

void CompressedObjectSlot::Relaxed_Store(Tagged<Object> value) const {
  Tagged_t ptr = TCompressionScheme::CompressObject(value.ptr());
  AsAtomicTagged::Relaxed_Store(location(), ptr);
}

void CompressedObjectSlot::Release_Store(Tagged<Object> value) const {
  Tagged_t ptr = TCompressionScheme::CompressObject(value.ptr());
  AsAtomicTagged::Release_Store(location(), ptr);
}

Tagged<Object> CompressedObjectSlot::Release_CompareAndSwap(
    Tagged<Object> old, Tagged<Object> target) const {
  Tagged_t old_ptr = TCompressionScheme::CompressObject(old.ptr());
  Tagged_t target_ptr = TCompressionScheme::CompressObject(target.ptr());
  Tagged_t result =
      AsAtomicTagged::Release_CompareAndSwap(location(), old_ptr, target_ptr);
  return Tagged<Object>(
      TCompressionScheme::DecompressTagged(address(), result));
}

//
// CompressedMaybeObjectSlot implementation.
//

Tagged<MaybeObject> CompressedMaybeObjectSlot::operator*() const {
  Tagged_t value = *location();
  return Tagged<MaybeObject>(
      TCompressionScheme::DecompressTagged(address(), value));
}

Tagged<MaybeObject> CompressedMaybeObjectSlot::load(
    PtrComprCageBase cage_base) const {
  Tagged_t value = *location();
  return Tagged<MaybeObject>(
      TCompressionScheme::DecompressTagged(cage_base, value));
}

void CompressedMaybeObjectSlot::store(Tagged<MaybeObject> value) const {
  *location() = TCompressionScheme::CompressObject(value.ptr());
}

Tagged<MaybeObject> CompressedMaybeObjectSlot::Relaxed_Load() const {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location());
  return Tagged<MaybeObject>(
      TCompressionScheme::DecompressTagged(address(), value));
}

Tagged<MaybeObject> CompressedMaybeObjectSlot::Relaxed_Load(
    PtrComprCageBase cage_base) const {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location());
  return Tagged<MaybeObject>(
      TCompressionScheme::DecompressTagged(cage_base, value));
}

Tagged_t CompressedMaybeObjectSlot::Relaxed_Load_Raw() const {
  return static_cast<Tagged_t>(AsAtomicTagged::Relaxed_Load(location()));
}

// static
Tagged<Object> CompressedMaybeObjectSlot::RawToTagged(
    PtrComprCageBase cage_base, Tagged_t raw) {
  return Tagged<Object>(TCompressionScheme::DecompressTagged(cage_base, raw));
}

void CompressedMaybeObjectSlot::Relaxed_Store(Tagged<MaybeObject> value) const {
  Tagged_t ptr = TCompressionScheme::CompressObject(value.ptr());
  AsAtomicTagged::Relaxed_Store(location(), ptr);
}

void CompressedMaybeObjectSlot::Release_CompareAndSwap(
    Tagged<MaybeObject> old, Tagged<MaybeObject> target) const {
  Tagged_t old_ptr = TCompressionScheme::CompressObject(old.ptr());
  Tagged_t target_ptr = TCompressionScheme::CompressObject(target.ptr());
  AsAtomicTagged::Release_CompareAndSwap(location(), old_ptr, target_ptr);
}

//
// CompressedHeapObjectSlot implementation.
//

Tagged<HeapObjectReference> CompressedHeapObjectSlot::operator*() const {
  Tagged_t value = *location();
  return Cast<HeapObjectReference>(Tagged<MaybeObject>(
      TCompressionScheme::DecompressTagged(address(), value)));
}

Tagged<HeapObjectReference> CompressedHeapObjectSlot::load(
    PtrComprCageBase cage_base) const {
  Tagged_t value = *location();
  return Cast<HeapObjectReference>(Tagged<MaybeObject>(
      TCompressionScheme::DecompressTagged(cage_base, value)));
}

void CompressedHeapObjectSlot::store(Tagged<HeapObjectReference> value) const {
  *location() = TCompressionScheme::CompressObject(value.ptr());
}

Tagged<HeapObject> CompressedHeapObjectSlot::ToHeapObject() const {
  Tagged_t value = *location();
  DCHECK(HAS_STRONG_HEAP_OBJECT_TAG(value));
  return Cast<HeapObject>(
      Tagged<Object>(TCompressionScheme::DecompressTagged(address(), value)));
}

void CompressedHeapObjectSlot::StoreHeapObject(Tagged<HeapObject> value) const {
  *location() = TCompressionScheme::CompressObject(value.ptr());
}

//
// OffHeapCompressedObjectSlot implementation.
//

template <typename CompressionScheme>
Tagged<Object> OffHeapCompressedObjectSlot<CompressionScheme>::load() const {
  Tagged_t value = *TSlotBase::location();
  return Tagged<Object>(
      CompressionScheme::DecompressTagged(TSlotBase::address(), value));
}

template <typename CompressionScheme>
Tagged<Object> OffHeapCompressedObjectSlot<CompressionScheme>::load(
    PtrComprCageBase cage_base) const {
  Tagged_t value = *TSlotBase::location();
  return Tagged<Object>(CompressionScheme::DecompressTagged(cage_base, value));
}

template <typename CompressionScheme>
void OffHeapCompressedObjectSlot<CompressionScheme>::store(
    Tagged<Object> value) const {
  *TSlotBase::location() = CompressionScheme::CompressObject(value.ptr());
}

template <typename CompressionScheme>
Tagged<Object> OffHeapCompressedObjectSlot<CompressionScheme>::Relaxed_Load()
    const {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(TSlotBase::location());
  return Tagged<Object>(
      CompressionScheme::DecompressTagged(TSlotBase::address(), value));
}

template <typename CompressionScheme>
Tagged<Object> OffHeapCompressedObjectSlot<CompressionScheme>::Relaxed_Load(
    PtrComprCageBase cage_base) const {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(TSlotBase::location());
  return Tagged<Object>(CompressionScheme::DecompressTagged(cage_base, value));
}

template <typename CompressionScheme>
Tagged<Object> OffHeapCompressedObjectSlot<CompressionScheme>::Acquire_Load()
    const {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(TSlotBase::location());
  return Tagged<Object>(
      CompressionScheme::DecompressTagged(TSlotBase::address(), value));
}

template <typename CompressionScheme>
Tagged<Object> OffHeapCompressedObjectSlot<CompressionScheme>::Acquire_Load(
    PtrComprCageBase cage_base) const {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(TSlotBase::location());
  return Tagged<Object>(CompressionScheme::DecompressTagged(cage_base, value));
}

template <typename CompressionScheme>
void OffHeapCompressedObjectSlot<CompressionScheme>::Relaxed_Store(
    Tagged<Object> value) const {
  Tagged_t ptr = CompressionScheme::CompressObject(value.ptr());
  AsAtomicTagged::Relaxed_Store(TSlotBase::location(), ptr);
}

template <typename CompressionScheme>
void OffHeapCompressedObjectSlot<CompressionScheme>::Release_Store(
    Tagged<Object> value) const {
  Tagged_t ptr = CompressionScheme::CompressObject(value.ptr());
  AsAtomicTagged::Release_Store(TSlotBase::location(), ptr);
}

template <typename CompressionScheme>
void OffHeapCompressedObjectSlot<CompressionScheme>::Release_CompareAndSwap(
    Tagged<Object> old, Tagged<Object> target) const {
  Tagged_t old_ptr = CompressionScheme::CompressObject(old.ptr());
  Tagged_t target_ptr = CompressionScheme::CompressObject(target.ptr());
  AsAtomicTagged::Release_CompareAndSwap(TSlotBase::location(), old_ptr,
                                         target_ptr);
}

}  // namespace v8::internal

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_OBJECTS_COMPRESSED_SLOTS_INL_H_
