// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_FIELD_INL_H_
#define V8_OBJECTS_TAGGED_FIELD_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/objects/tagged-field.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

// static
template <typename T, typename CompressionScheme>
Address TaggedMember<T, CompressionScheme>::tagged_to_full(
    Tagged_t tagged_value) {
#ifdef V8_COMPRESS_POINTERS
  if constexpr (std::is_same_v<Smi, T>) {
    V8_ASSUME(HAS_SMI_TAG(tagged_value));
    return CompressionScheme::DecompressTaggedSigned(tagged_value);
  } else {
    return CompressionScheme::DecompressTagged(GetPtrComprCageBase(),
                                               tagged_value);
  }
#else
  return tagged_value;
#endif
}

// static
template <typename T, typename CompressionScheme>
Tagged_t TaggedMember<T, CompressionScheme>::full_to_tagged(Address value) {
#ifdef V8_COMPRESS_POINTERS
  return CompressionScheme::CompressObject(value);
#else
  return value;
#endif
}

template <typename T, typename CompressionScheme>
Tagged<T> TaggedMember<T, CompressionScheme>::load() const {
  return Tagged<T>(tagged_to_full(ptr()));
}

template <typename T, typename CompressionScheme>
void TaggedMember<T, CompressionScheme>::store(HeapObjectLayout* host,
                                               Tagged<T> value,
                                               WriteBarrierMode mode) {
  store_no_write_barrier(value);

#ifndef V8_DISABLE_WRITE_BARRIERS
  if constexpr (!std::is_same_v<Smi, T>) {
#if V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS
    mode = UPDATE_WRITE_BARRIER;
#endif
    DCHECK_NOT_NULL(GetHeapFromWritableObject(Tagged(host)));
    CombinedWriteBarrier(host, this, value, mode);
  }
#endif
}

template <typename T, typename CompressionScheme>
Tagged<T> TaggedMember<T, CompressionScheme>::Relaxed_Load() const {
  return Tagged<T>(
      tagged_to_full(AsAtomicTagged::Relaxed_Load(this->ptr_location())));
}

template <typename T, typename CompressionScheme>
void TaggedMember<T, CompressionScheme>::store_no_write_barrier(
    Tagged<T> value) {
#ifdef V8_ATOMIC_OBJECT_FIELD_WRITES
  Relaxed_Store_no_write_barrier(value);
#else
  *this->ptr_location() = full_to_tagged(value.ptr());
#endif
}

template <typename T, typename CompressionScheme>
void TaggedMember<T, CompressionScheme>::Relaxed_Store_no_write_barrier(
    Tagged<T> value) {
  AsAtomicTagged::Relaxed_Store(this->ptr_location(),
                                full_to_tagged(value.ptr()));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
Address TaggedField<T, kFieldOffset, CompressionScheme>::address(
    Tagged<HeapObject> host, int offset) {
  return host.address() + kFieldOffset + offset;
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
Tagged_t* TaggedField<T, kFieldOffset, CompressionScheme>::location(
    Tagged<HeapObject> host, int offset) {
  return reinterpret_cast<Tagged_t*>(address(host, offset));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
template <typename TOnHeapAddress>
Address TaggedField<T, kFieldOffset, CompressionScheme>::tagged_to_full(
    TOnHeapAddress on_heap_addr, Tagged_t tagged_value) {
#ifdef V8_COMPRESS_POINTERS
  if constexpr (kIsSmi) {
    V8_ASSUME(HAS_SMI_TAG(tagged_value));
    return CompressionScheme::DecompressTaggedSigned(tagged_value);
  } else {
    return CompressionScheme::DecompressTagged(on_heap_addr, tagged_value);
  }
#else
  return tagged_value;
#endif
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
Tagged_t TaggedField<T, kFieldOffset, CompressionScheme>::full_to_tagged(
    Address value) {
#ifdef V8_COMPRESS_POINTERS
  if constexpr (kIsSmi) V8_ASSUME(HAS_SMI_TAG(value));
  return CompressionScheme::CompressObject(value);
#else
  return value;
#endif
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::load(Tagged<HeapObject> host,
                                                      int offset) {
  Tagged_t value = *location(host, offset);
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(host.ptr(), value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::load(
    PtrComprCageBase cage_base, Tagged<HeapObject> host, int offset) {
  Tagged_t value = *location(host, offset);
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(cage_base, value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::store(
    Tagged<HeapObject> host, PtrType value) {
#ifdef V8_ATOMIC_OBJECT_FIELD_WRITES
  Relaxed_Store(host, value);
#else
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset, HeapObject::kMapOffset);
  *location(host) = full_to_tagged(ptr);
#endif
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::store(
    Tagged<HeapObject> host, int offset, PtrType value) {
#ifdef V8_ATOMIC_OBJECT_FIELD_WRITES
  Relaxed_Store(host, offset, value);
#else
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  *location(host, offset) = full_to_tagged(ptr);
#endif
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::Relaxed_Load(
    Tagged<HeapObject> host, int offset) {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location(host, offset));
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(host.ptr(), value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::Relaxed_Load(
    PtrComprCageBase cage_base, Tagged<HeapObject> host, int offset) {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location(host, offset));
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(cage_base, value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::Relaxed_Load_Map_Word(
    PtrComprCageBase cage_base, Tagged<HeapObject> host) {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location(host, 0));
  return PtrType(tagged_to_full(cage_base, value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::Relaxed_Store_Map_Word(
    Tagged<HeapObject> host, PtrType value) {
  AsAtomicTagged::Relaxed_Store(location(host), full_to_tagged(value.ptr()));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::Relaxed_Store(
    Tagged<HeapObject> host, PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset, HeapObject::kMapOffset);
  AsAtomicTagged::Relaxed_Store(location(host), full_to_tagged(ptr));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::Relaxed_Store(
    Tagged<HeapObject> host, int offset, PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  AsAtomicTagged::Relaxed_Store(location(host, offset), full_to_tagged(ptr));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::Acquire_Load(
    Tagged<HeapObject> host, int offset) {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location(host, offset));
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(host.ptr(), value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::Acquire_Load_No_Unpack(
    PtrComprCageBase cage_base, Tagged<HeapObject> host, int offset) {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location(host, offset));
  return PtrType(tagged_to_full(cage_base, value));
}

template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::Acquire_Load(
    PtrComprCageBase cage_base, Tagged<HeapObject> host, int offset) {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location(host, offset));
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(cage_base, value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::Release_Store(
    Tagged<HeapObject> host, PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset, HeapObject::kMapOffset);
  AsAtomicTagged::Release_Store(location(host), full_to_tagged(ptr));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::Release_Store_Map_Word(
    Tagged<HeapObject> host, PtrType value) {
  Address ptr = value.ptr();
  AsAtomicTagged::Release_Store(location(host), full_to_tagged(ptr));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::Release_Store(
    Tagged<HeapObject> host, int offset, PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  AsAtomicTagged::Release_Store(location(host, offset), full_to_tagged(ptr));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
Tagged_t
TaggedField<T, kFieldOffset, CompressionScheme>::Release_CompareAndSwap(
    Tagged<HeapObject> host, PtrType old, PtrType value) {
  Tagged_t old_value = full_to_tagged(old.ptr());
  Tagged_t new_value = full_to_tagged(value.ptr());
  Tagged_t result = AsAtomicTagged::Release_CompareAndSwap(
      location(host), old_value, new_value);
  return result;
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::SeqCst_Load(
    Tagged<HeapObject> host, int offset) {
  AtomicTagged_t value = AsAtomicTagged::SeqCst_Load(location(host, offset));
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(host.ptr(), value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::SeqCst_Load(
    PtrComprCageBase cage_base, Tagged<HeapObject> host, int offset) {
  AtomicTagged_t value = AsAtomicTagged::SeqCst_Load(location(host, offset));
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  return PtrType(tagged_to_full(cage_base, value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::SeqCst_Store(
    Tagged<HeapObject> host, PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset, HeapObject::kMapOffset);
  AsAtomicTagged::SeqCst_Store(location(host), full_to_tagged(ptr));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
void TaggedField<T, kFieldOffset, CompressionScheme>::SeqCst_Store(
    Tagged<HeapObject> host, int offset, PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  AsAtomicTagged::SeqCst_Store(location(host, offset), full_to_tagged(ptr));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>

typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::SeqCst_Swap(
    Tagged<HeapObject> host, int offset, PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  AtomicTagged_t old_value =
      AsAtomicTagged::SeqCst_Swap(location(host, offset), full_to_tagged(ptr));
  return PtrType(tagged_to_full(host.ptr(), old_value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>

typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::SeqCst_Swap(
    PtrComprCageBase cage_base, Tagged<HeapObject> host, int offset,
    PtrType value) {
  Address ptr = value.ptr();
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  AtomicTagged_t old_value =
      AsAtomicTagged::SeqCst_Swap(location(host, offset), full_to_tagged(ptr));
  return PtrType(tagged_to_full(cage_base, old_value));
}

// static
template <typename T, int kFieldOffset, typename CompressionScheme>
typename TaggedField<T, kFieldOffset, CompressionScheme>::PtrType
TaggedField<T, kFieldOffset, CompressionScheme>::SeqCst_CompareAndSwap(
    Tagged<HeapObject> host, int offset, PtrType old, PtrType value) {
  Address ptr = value.ptr();
  Address old_ptr = old.ptr();
  DCHECK_NE(kFieldOffset + offset, HeapObject::kMapOffset);
  AtomicTagged_t old_value = AsAtomicTagged::SeqCst_CompareAndSwap(
      location(host, offset), full_to_tagged(old_ptr), full_to_tagged(ptr));
  return TaggedField<T, kFieldOffset, CompressionScheme>::PtrType(
      tagged_to_full(host.ptr(), old_value));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_TAGGED_FIELD_INL_H_
