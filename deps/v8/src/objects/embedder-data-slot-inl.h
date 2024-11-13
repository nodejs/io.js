// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_EMBEDDER_DATA_SLOT_INL_H_
#define V8_OBJECTS_EMBEDDER_DATA_SLOT_INL_H_

#include "src/base/memory.h"
#include "src/common/globals.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/embedder-data-array.h"
#include "src/objects/embedder-data-slot.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/sandbox/external-pointer-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

EmbedderDataSlot::EmbedderDataSlot(Tagged<EmbedderDataArray> array,
                                   int entry_index)
    : SlotBase(FIELD_ADDR(array,
                          EmbedderDataArray::OffsetOfElementAt(entry_index))) {}

EmbedderDataSlot::EmbedderDataSlot(Tagged<JSObject> object,
                                   int embedder_field_index)
    : SlotBase(FIELD_ADDR(
          object, object->GetEmbedderFieldOffset(embedder_field_index))) {}

void EmbedderDataSlot::Initialize(Tagged<Object> initial_value) {
  // TODO(v8) initialize the slot with Smi::zero() instead. This'll also
  // guarantee that we don't need a write barrier.
  DCHECK(IsSmi(initial_value) ||
         ReadOnlyHeap::Contains(Cast<HeapObject>(initial_value)));
  ObjectSlot(address() + kTaggedPayloadOffset).Relaxed_Store(initial_value);
#ifdef V8_COMPRESS_POINTERS
  ObjectSlot(address() + kRawPayloadOffset).Relaxed_Store(Smi::zero());
#endif
}

Tagged<Object> EmbedderDataSlot::load_tagged() const {
  return ObjectSlot(address() + kTaggedPayloadOffset).Relaxed_Load();
}

void EmbedderDataSlot::store_smi(Tagged<Smi> value) {
  ObjectSlot(address() + kTaggedPayloadOffset).Relaxed_Store(value);
#ifdef V8_COMPRESS_POINTERS
  // See gc_safe_store() for the reasons behind two stores.
  ObjectSlot(address() + kRawPayloadOffset).Relaxed_Store(Smi::zero());
#endif
}

// static
void EmbedderDataSlot::store_tagged(Tagged<EmbedderDataArray> array,
                                    int entry_index, Tagged<Object> value) {
#ifdef V8_COMPRESS_POINTERS
  CHECK(IsSmi(value) ||
        V8HeapCompressionScheme::GetPtrComprCageBaseAddress(value.ptr()) ==
            V8HeapCompressionScheme::GetPtrComprCageBaseAddress(array.ptr()));
#endif
  int slot_offset = EmbedderDataArray::OffsetOfElementAt(entry_index);
  ObjectSlot(FIELD_ADDR(array, slot_offset + kTaggedPayloadOffset))
      .Relaxed_Store(value);
  WRITE_BARRIER(array, slot_offset + kTaggedPayloadOffset, value);
#ifdef V8_COMPRESS_POINTERS
  // See gc_safe_store() for the reasons behind two stores.
  ObjectSlot(FIELD_ADDR(array, slot_offset + kRawPayloadOffset))
      .Relaxed_Store(Smi::zero());
#endif
}

// static
void EmbedderDataSlot::store_tagged(Tagged<JSObject> object,
                                    int embedder_field_index,
                                    Tagged<Object> value) {
#ifdef V8_COMPRESS_POINTERS
  CHECK(IsSmi(value) ||
        V8HeapCompressionScheme::GetPtrComprCageBaseAddress(value.ptr()) ==
            V8HeapCompressionScheme::GetPtrComprCageBaseAddress(object.ptr()));
#endif
  int slot_offset = object->GetEmbedderFieldOffset(embedder_field_index);
  ObjectSlot(FIELD_ADDR(object, slot_offset + kTaggedPayloadOffset))
      .Relaxed_Store(value);
  WRITE_BARRIER(object, slot_offset + kTaggedPayloadOffset, value);
#ifdef V8_COMPRESS_POINTERS
  // See gc_safe_store() for the reasons behind two stores.
  ObjectSlot(FIELD_ADDR(object, slot_offset + kRawPayloadOffset))
      .Relaxed_Store(Smi::zero());
#endif
}

bool EmbedderDataSlot::ToAlignedPointer(Isolate* isolate,
                                        void** out_pointer) const {
  // We don't care about atomicity of access here because embedder slots
  // are accessed this way only from the main thread via API during "mutator"
  // phase which is propely synched with GC (concurrent marker may still look
  // at the tagged part of the embedder slot but read-only access is ok).
#ifdef V8_ENABLE_SANDBOX
  // The raw part must always contain a valid external pointer table index.
  *out_pointer = reinterpret_cast<void*>(
      ReadExternalPointerField<kEmbedderDataSlotPayloadTag>(
          address() + kExternalPointerOffset, isolate));
  return true;
#else
  Address raw_value;
  if (COMPRESS_POINTERS_BOOL) {
    // TODO(ishell, v8:8875): When pointer compression is enabled 8-byte size
    // fields (external pointers, doubles and BigInt data) are only kTaggedSize
    // aligned so we have to use unaligned pointer friendly way of accessing
    // them in order to avoid undefined behavior in C++ code.
    raw_value = base::ReadUnalignedValue<Address>(address());
  } else {
    raw_value = *location();
  }
  *out_pointer = reinterpret_cast<void*>(raw_value);
  return HAS_SMI_TAG(raw_value);
#endif  // V8_ENABLE_SANDBOX
}

bool EmbedderDataSlot::store_aligned_pointer(Isolate* isolate,
                                             Tagged<HeapObject> host,
                                             void* ptr) {
  Address value = reinterpret_cast<Address>(ptr);
  if (!HAS_SMI_TAG(value)) return false;
#ifdef V8_ENABLE_SANDBOX
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  // When the sandbox is enabled, the external pointer handles in
  // EmbedderDataSlots are lazily initialized: initially they contain the null
  // external pointer handle (see EmbedderDataSlot::Initialize), and only once
  // an external pointer is stored in them are they properly initialized.
  WriteLazilyInitializedExternalPointerField<kEmbedderDataSlotPayloadTag>(
      host.address(), address() + kExternalPointerOffset, isolate, value);
  ObjectSlot(address() + kTaggedPayloadOffset).Relaxed_Store(Smi::zero());
  return true;
#else
  gc_safe_store(isolate, value);
  return true;
#endif  // V8_ENABLE_SANDBOX
}

EmbedderDataSlot::RawData EmbedderDataSlot::load_raw(
    Isolate* isolate, const DisallowGarbageCollection& no_gc) const {
  // We don't care about atomicity of access here because embedder slots
  // are accessed this way only by serializer from the main thread when
  // GC is not active (concurrent marker may still look at the tagged part
  // of the embedder slot but read-only access is ok).
#ifdef V8_COMPRESS_POINTERS
  // TODO(ishell, v8:8875): When pointer compression is enabled 8-byte size
  // fields (external pointers, doubles and BigInt data) are only kTaggedSize
  // aligned so we have to use unaligned pointer friendly way of accessing them
  // in order to avoid undefined behavior in C++ code.
  return base::ReadUnalignedValue<EmbedderDataSlot::RawData>(address());
#else
  return *location();
#endif
}

void EmbedderDataSlot::store_raw(Isolate* isolate,
                                 EmbedderDataSlot::RawData data,
                                 const DisallowGarbageCollection& no_gc) {
  gc_safe_store(isolate, data);
}

void EmbedderDataSlot::gc_safe_store(Isolate* isolate, Address value) {
#ifdef V8_COMPRESS_POINTERS
  static_assert(kSmiShiftSize == 0);
  static_assert(SmiValuesAre31Bits());
  static_assert(kTaggedSize == kInt32Size);

  // We have to do two 32-bit stores here because
  // 1) tagged part modifications must be atomic to be properly synchronized
  //    with the concurrent marker.
  // 2) atomicity of full pointer store is not guaranteed for embedder slots
  //    since the address of the slot may not be kSystemPointerSize aligned
  //    (only kTaggedSize alignment is guaranteed).
  // TODO(ishell, v8:8875): revisit this once the allocation alignment
  // inconsistency is fixed.
  Address lo = static_cast<intptr_t>(static_cast<int32_t>(value));
  ObjectSlot(address() + kTaggedPayloadOffset).Relaxed_Store(Tagged<Smi>(lo));
  Tagged_t hi = static_cast<Tagged_t>(value >> 32);
  // The raw part of the payload does not contain a valid tagged value, so we
  // need to use a raw store operation for it here.
  AsAtomicTagged::Relaxed_Store(
      reinterpret_cast<AtomicTagged_t*>(address() + kRawPayloadOffset), hi);
#else
  ObjectSlot(address() + kTaggedPayloadOffset)
      .Relaxed_Store(Tagged<Smi>(value));
#endif
}

bool EmbedderDataSlot::MustClearDuringSerialization(
    const DisallowGarbageCollection& no_gc) {
  // Serialization must avoid writing external pointer handles.  If we were to
  // accidentally write an external pointer handle, that ends up deserializing
  // as a dangling pointer.  For consistency it would be nice to avoid writing
  // external pointers also in the wide-pointer case, but as we can't
  // distinguish between Smi values and pointers we just leave them be.
#ifdef V8_ENABLE_SANDBOX
  auto* location = reinterpret_cast<ExternalPointerHandle*>(
      address() + kExternalPointerOffset);
  return base::AsAtomic32::Relaxed_Load(location) != kNullExternalPointerHandle;
#else   // !V8_ENABLE_SANDBOX
  return false;
#endif  // !V8_ENABLE_SANDBOX
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_EMBEDDER_DATA_SLOT_INL_H_
