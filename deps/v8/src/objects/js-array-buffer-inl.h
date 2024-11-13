// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ARRAY_BUFFER_INL_H_
#define V8_OBJECTS_JS_ARRAY_BUFFER_INL_H_

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-array-buffer-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSArrayBuffer)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSArrayBufferView)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTypedArray)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSDataViewOrRabGsabDataView)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSDataView)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSRabGsabDataView)

ACCESSORS(JSTypedArray, base_pointer, Tagged<Object>, kBasePointerOffset)
RELEASE_ACQUIRE_ACCESSORS(JSTypedArray, base_pointer, Tagged<Object>,
                          kBasePointerOffset)

size_t JSArrayBuffer::byte_length() const {
  return ReadBoundedSizeField(kRawByteLengthOffset);
}

void JSArrayBuffer::set_byte_length(size_t value) {
  WriteBoundedSizeField(kRawByteLengthOffset, value);
}

size_t JSArrayBuffer::max_byte_length() const {
  return ReadBoundedSizeField(kRawMaxByteLengthOffset);
}

void JSArrayBuffer::set_max_byte_length(size_t value) {
  WriteBoundedSizeField(kRawMaxByteLengthOffset, value);
}

DEF_GETTER(JSArrayBuffer, backing_store, void*) {
  Address value = ReadSandboxedPointerField(kBackingStoreOffset, cage_base);
  return reinterpret_cast<void*>(value);
}

void JSArrayBuffer::set_backing_store(Isolate* isolate, void* value) {
  Address addr = reinterpret_cast<Address>(value);
  WriteSandboxedPointerField(kBackingStoreOffset, isolate, addr);
}

std::shared_ptr<BackingStore> JSArrayBuffer::GetBackingStore() const {
  if (!extension()) return nullptr;
  return extension()->backing_store();
}

size_t JSArrayBuffer::GetByteLength() const {
  if (V8_UNLIKELY(is_shared() && is_resizable_by_js())) {
    // Invariant: byte_length for GSAB is 0 (it needs to be read from the
    // BackingStore).
    DCHECK_EQ(0, byte_length());

    // If the byte length is read after the JSArrayBuffer object is allocated
    // but before it's attached to the backing store, GetBackingStore returns
    // nullptr. This is rare, but can happen e.g., when memory measurements
    // are enabled (via performance.measureMemory()).
    auto backing_store = GetBackingStore();
    if (!backing_store) {
      return 0;
    }

    return backing_store->byte_length(std::memory_order_seq_cst);
  }
  return byte_length();
}

uint32_t JSArrayBuffer::GetBackingStoreRefForDeserialization() const {
  return static_cast<uint32_t>(ReadField<Address>(kBackingStoreOffset));
}

void JSArrayBuffer::SetBackingStoreRefForSerialization(uint32_t ref) {
  WriteField<Address>(kBackingStoreOffset, static_cast<Address>(ref));
}

void JSArrayBuffer::init_extension() {
#if V8_COMPRESS_POINTERS
  // The extension field is lazily-initialized, so set it to null initially.
  base::AsAtomic32::Release_Store(extension_handle_location(),
                                  kNullExternalPointerHandle);
#else
  base::AsAtomicPointer::Release_Store(extension_location(), nullptr);
#endif  // V8_COMPRESS_POINTERS
}

ArrayBufferExtension* JSArrayBuffer::extension() const {
#if V8_COMPRESS_POINTERS
  // We need Acquire semantics here when loading the entry, see below.
  // Consider adding respective external pointer accessors if non-relaxed
  // ordering semantics are ever needed in other places as well.
  Isolate* isolate = GetIsolateFromWritableObject(*this);
  ExternalPointerHandle handle =
      base::AsAtomic32::Acquire_Load(extension_handle_location());
  return reinterpret_cast<ArrayBufferExtension*>(
      isolate->external_pointer_table().Get(handle, kArrayBufferExtensionTag));
#else
  return base::AsAtomicPointer::Acquire_Load(extension_location());
#endif  // V8_COMPRESS_POINTERS
}

void JSArrayBuffer::set_extension(ArrayBufferExtension* extension) {
#if V8_COMPRESS_POINTERS
  // TODO(saelo): if we ever use the external pointer table for all external
  // pointer fields in the no-sandbox-ptr-compression config, replace this code
  // here and above with the respective external pointer accessors.
  IsolateForPointerCompression isolate = GetIsolateFromWritableObject(*this);
  const ExternalPointerTag tag = kArrayBufferExtensionTag;
  Address value = reinterpret_cast<Address>(extension);
  ExternalPointerTable& table = isolate.GetExternalPointerTableFor(tag);

  ExternalPointerHandle current_handle =
      base::AsAtomic32::Relaxed_Load(extension_handle_location());
  if (current_handle == kNullExternalPointerHandle) {
    // We need Release semantics here, see above.
    ExternalPointerHandle handle = table.AllocateAndInitializeEntry(
        isolate.GetExternalPointerTableSpaceFor(tag, address()), value, tag);
    base::AsAtomic32::Release_Store(extension_handle_location(), handle);
  } else {
    table.Set(current_handle, value, tag);
  }
#else
  base::AsAtomicPointer::Release_Store(extension_location(), extension);
#endif  // V8_COMPRESS_POINTERS
  WriteBarrier::Marking(*this, extension);
}

#if V8_COMPRESS_POINTERS
ExternalPointerHandle* JSArrayBuffer::extension_handle_location() const {
  Address location = field_address(kExtensionOffset);
  return reinterpret_cast<ExternalPointerHandle*>(location);
}
#else
ArrayBufferExtension** JSArrayBuffer::extension_location() const {
  Address location = field_address(kExtensionOffset);
  return reinterpret_cast<ArrayBufferExtension**>(location);
}
#endif  // V8_COMPRESS_POINTERS

void JSArrayBuffer::clear_padding() {
  if (FIELD_SIZE(kOptionalPaddingOffset) != 0) {
    DCHECK_EQ(4, FIELD_SIZE(kOptionalPaddingOffset));
    memset(reinterpret_cast<void*>(address() + kOptionalPaddingOffset), 0,
           FIELD_SIZE(kOptionalPaddingOffset));
  }
}

ACCESSORS(JSArrayBuffer, detach_key, Tagged<Object>, kDetachKeyOffset)

void JSArrayBuffer::set_bit_field(uint32_t bits) {
  RELAXED_WRITE_UINT32_FIELD(*this, kBitFieldOffset, bits);
}

uint32_t JSArrayBuffer::bit_field() const {
  return RELAXED_READ_UINT32_FIELD(*this, kBitFieldOffset);
}

// |bit_field| fields.
BIT_FIELD_ACCESSORS(JSArrayBuffer, bit_field, is_external,
                    JSArrayBuffer::IsExternalBit)
BIT_FIELD_ACCESSORS(JSArrayBuffer, bit_field, is_detachable,
                    JSArrayBuffer::IsDetachableBit)
BIT_FIELD_ACCESSORS(JSArrayBuffer, bit_field, was_detached,
                    JSArrayBuffer::WasDetachedBit)
BIT_FIELD_ACCESSORS(JSArrayBuffer, bit_field, is_shared,
                    JSArrayBuffer::IsSharedBit)
BIT_FIELD_ACCESSORS(JSArrayBuffer, bit_field, is_resizable_by_js,
                    JSArrayBuffer::IsResizableByJsBit)

bool JSArrayBuffer::IsEmpty() const {
  auto backing_store = GetBackingStore();
  bool is_empty = !backing_store || backing_store->IsEmpty();
  DCHECK_IMPLIES(is_empty, byte_length() == 0);
  return is_empty;
}

size_t JSArrayBufferView::byte_offset() const {
  return ReadBoundedSizeField(kRawByteOffsetOffset);
}

void JSArrayBufferView::set_byte_offset(size_t value) {
  WriteBoundedSizeField(kRawByteOffsetOffset, value);
}

size_t JSArrayBufferView::byte_length() const {
  return ReadBoundedSizeField(kRawByteLengthOffset);
}

void JSArrayBufferView::set_byte_length(size_t value) {
  WriteBoundedSizeField(kRawByteLengthOffset, value);
}

bool JSArrayBufferView::WasDetached() const {
  return Cast<JSArrayBuffer>(buffer())->was_detached();
}

BIT_FIELD_ACCESSORS(JSArrayBufferView, bit_field, is_length_tracking,
                    JSArrayBufferView::IsLengthTrackingBit)
BIT_FIELD_ACCESSORS(JSArrayBufferView, bit_field, is_backed_by_rab,
                    JSArrayBufferView::IsBackedByRabBit)

bool JSArrayBufferView::IsVariableLength() const {
  return is_length_tracking() || is_backed_by_rab();
}

size_t JSTypedArray::GetLengthOrOutOfBounds(bool& out_of_bounds) const {
  DCHECK(!out_of_bounds);
  if (WasDetached()) return 0;
  if (IsVariableLength()) {
    return GetVariableLengthOrOutOfBounds(out_of_bounds);
  }
  return LengthUnchecked();
}

size_t JSTypedArray::GetLength() const {
  bool out_of_bounds = false;
  return GetLengthOrOutOfBounds(out_of_bounds);
}

size_t JSTypedArray::GetByteLength() const {
  return GetLength() * element_size();
}

bool JSTypedArray::IsOutOfBounds() const {
  bool out_of_bounds = false;
  GetLengthOrOutOfBounds(out_of_bounds);
  return out_of_bounds;
}

bool JSTypedArray::IsDetachedOrOutOfBounds() const {
  if (WasDetached()) {
    return true;
  }
  if (!is_backed_by_rab()) {
    // TypedArrays backed by GSABs or regular AB/SABs are never out of bounds.
    // This shortcut is load-bearing; this enables determining
    // IsDetachedOrOutOfBounds without consulting the BackingStore.
    return false;
  }
  return IsOutOfBounds();
}

// static
inline void JSTypedArray::ForFixedTypedArray(ExternalArrayType array_type,
                                             size_t* element_size,
                                             ElementsKind* element_kind) {
  switch (array_type) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) \
  case kExternal##Type##Array:                    \
    *element_size = sizeof(ctype);                \
    *element_kind = TYPE##_ELEMENTS;              \
    return;

    TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
  }
  UNREACHABLE();
}

size_t JSTypedArray::length() const {
  DCHECK(!is_length_tracking());
  DCHECK(!is_backed_by_rab());
  return ReadBoundedSizeField(kRawLengthOffset);
}

size_t JSTypedArray::LengthUnchecked() const {
  return ReadBoundedSizeField(kRawLengthOffset);
}

void JSTypedArray::set_length(size_t value) {
  WriteBoundedSizeField(kRawLengthOffset, value);
}

DEF_GETTER(JSTypedArray, external_pointer, Address) {
  return ReadSandboxedPointerField(kExternalPointerOffset, cage_base);
}

void JSTypedArray::set_external_pointer(Isolate* isolate, Address value) {
  WriteSandboxedPointerField(kExternalPointerOffset, isolate, value);
}

Address JSTypedArray::ExternalPointerCompensationForOnHeapArray(
    PtrComprCageBase cage_base) {
#ifdef V8_COMPRESS_POINTERS
  return cage_base.address();
#else
  return 0;
#endif
}

uint32_t JSTypedArray::GetExternalBackingStoreRefForDeserialization() const {
  DCHECK(!is_on_heap());
  return static_cast<uint32_t>(ReadField<Address>(kExternalPointerOffset));
}

void JSTypedArray::SetExternalBackingStoreRefForSerialization(uint32_t ref) {
  DCHECK(!is_on_heap());
  WriteField<Address>(kExternalPointerOffset, static_cast<Address>(ref));
}

void JSTypedArray::RemoveExternalPointerCompensationForSerialization(
    Isolate* isolate) {
  DCHECK(is_on_heap());
  Address offset =
      external_pointer() - ExternalPointerCompensationForOnHeapArray(isolate);
  WriteField<Address>(kExternalPointerOffset, offset);
}

void JSTypedArray::AddExternalPointerCompensationForDeserialization(
    Isolate* isolate) {
  DCHECK(is_on_heap());
  Address pointer = ReadField<Address>(kExternalPointerOffset) +
                    ExternalPointerCompensationForOnHeapArray(isolate);
  set_external_pointer(isolate, pointer);
}

void* JSTypedArray::DataPtr() {
  // Zero-extend Tagged_t to Address according to current compression scheme
  // so that the addition with |external_pointer| (which already contains
  // compensated offset value) will decompress the tagged value.
  // See JSTypedArray::ExternalPointerCompensationForOnHeapArray() for details.
  static_assert(kOffHeapDataPtrEqualsExternalPointer);
  return reinterpret_cast<void*>(external_pointer() +
                                 static_cast<Tagged_t>(base_pointer().ptr()));
}

void JSTypedArray::SetOffHeapDataPtr(Isolate* isolate, void* base,
                                     Address offset) {
  Address address = reinterpret_cast<Address>(base) + offset;
  set_external_pointer(isolate, address);
  // This is the only spot in which the `base_pointer` field can be mutated
  // after object initialization. Note this can happen at most once, when
  // `JSTypedArray::GetBuffer` transitions from an on- to off-heap
  // representation.
  // To play well with Turbofan concurrency requirements, `base_pointer` is set
  // with a release store, after external_pointer has been set.
  set_base_pointer(Smi::zero(), kReleaseStore, SKIP_WRITE_BARRIER);
  DCHECK_EQ(address, reinterpret_cast<Address>(DataPtr()));
}

bool JSTypedArray::is_on_heap() const {
  // Keep synced with `is_on_heap(AcquireLoadTag)`.
  DisallowGarbageCollection no_gc;
  return base_pointer() != Smi::zero();
}

bool JSTypedArray::is_on_heap(AcquireLoadTag tag) const {
  // Keep synced with `is_on_heap()`.
  // Note: For Turbofan concurrency requirements, it's important that this
  // function reads only `base_pointer`.
  DisallowGarbageCollection no_gc;
  return base_pointer(tag) != Smi::zero();
}

// static
MaybeHandle<JSTypedArray> JSTypedArray::Validate(Isolate* isolate,
                                                 Handle<Object> receiver,
                                                 const char* method_name) {
  if (V8_UNLIKELY(!IsJSTypedArray(*receiver))) {
    const MessageTemplate message = MessageTemplate::kNotTypedArray;
    THROW_NEW_ERROR(isolate, NewTypeError(message));
  }

  Handle<JSTypedArray> array = Cast<JSTypedArray>(receiver);
  if (V8_UNLIKELY(array->WasDetached())) {
    const MessageTemplate message = MessageTemplate::kDetachedOperation;
    Handle<String> operation =
        isolate->factory()->NewStringFromAsciiChecked(method_name);
    THROW_NEW_ERROR(isolate, NewTypeError(message, operation));
  }

  if (V8_UNLIKELY(array->IsVariableLength() && array->IsOutOfBounds())) {
    const MessageTemplate message = MessageTemplate::kDetachedOperation;
    Handle<String> operation =
        isolate->factory()->NewStringFromAsciiChecked(method_name);
    THROW_NEW_ERROR(isolate, NewTypeError(message, operation));
  }

  // spec describes to return `buffer`, but it may disrupt current
  // implementations, and it's much useful to return array for now.
  return array;
}

DEF_GETTER(JSDataViewOrRabGsabDataView, data_pointer, void*) {
  Address value = ReadSandboxedPointerField(kDataPointerOffset, cage_base);
  return reinterpret_cast<void*>(value);
}

void JSDataViewOrRabGsabDataView::set_data_pointer(Isolate* isolate,
                                                   void* ptr) {
  Address value = reinterpret_cast<Address>(ptr);
  WriteSandboxedPointerField(kDataPointerOffset, isolate, value);
}

size_t JSRabGsabDataView::GetByteLength() const {
  if (IsOutOfBounds()) {
    return 0;
  }
  if (is_length_tracking()) {
    // Invariant: byte_length of length tracking DataViews is 0.
    DCHECK_EQ(0, byte_length());
    return buffer()->GetByteLength() - byte_offset();
  }
  return byte_length();
}

bool JSRabGsabDataView::IsOutOfBounds() const {
  if (!is_backed_by_rab()) {
    return false;
  }
  if (is_length_tracking()) {
    return byte_offset() > buffer()->GetByteLength();
  }
  return byte_offset() + byte_length() > buffer()->GetByteLength();
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ARRAY_BUFFER_INL_H_
