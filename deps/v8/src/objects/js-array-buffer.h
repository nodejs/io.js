// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ARRAY_BUFFER_H_
#define V8_OBJECTS_JS_ARRAY_BUFFER_H_

#include "include/v8-array-buffer.h"
#include "include/v8-typed-array.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/backing-store.h"
#include "src/objects/js-objects.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class ArrayBufferExtension;

#include "torque-generated/src/objects/js-array-buffer-tq.inc"

class JSArrayBuffer
    : public TorqueGeneratedJSArrayBuffer<JSArrayBuffer,
                                          JSAPIObjectWithEmbedderSlots> {
 public:
// The maximum length for JSArrayBuffer's supported by V8.
// On 32-bit architectures we limit this to 2GiB, so that
// we can continue to use CheckBounds with the Unsigned31
// restriction for the length.
#if V8_ENABLE_SANDBOX
  static constexpr size_t kMaxByteLength = kMaxSafeBufferSizeForSandbox;
#elif V8_HOST_ARCH_32_BIT
  static constexpr size_t kMaxByteLength = kMaxInt;
#else
  static constexpr size_t kMaxByteLength = kMaxSafeInteger;
#endif

  // [byte_length]: length in bytes
  DECL_PRIMITIVE_ACCESSORS(byte_length, size_t)

  // [max_byte_length]: maximum length in bytes
  DECL_PRIMITIVE_ACCESSORS(max_byte_length, size_t)

  // [backing_store]: backing memory for this array
  // It should not be assumed that this will be nullptr for empty ArrayBuffers.
  DECL_GETTER(backing_store, void*)
  inline void set_backing_store(Isolate* isolate, void* value);

  // [extension]: extension object used for GC
  DECL_PRIMITIVE_ACCESSORS(extension, ArrayBufferExtension*)
  inline void init_extension();

  // [bit_field]: boolean flags
  DECL_PRIMITIVE_ACCESSORS(bit_field, uint32_t)

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic. Depending on the V8 build mode there could be no padding.
  V8_INLINE void clear_padding();

  // Bit positions for [bit_field].
  DEFINE_TORQUE_GENERATED_JS_ARRAY_BUFFER_FLAGS()

  // [is_external]: true indicates that the embedder is in charge of freeing the
  // backing_store, while is_external == false means that v8 will free the
  // memory block once all ArrayBuffers referencing it are collected by the GC.
  DECL_BOOLEAN_ACCESSORS(is_external)

  // [is_detachable]: false => this buffer cannot be detached.
  DECL_BOOLEAN_ACCESSORS(is_detachable)

  // [was_detached]: true => the buffer was previously detached.
  DECL_BOOLEAN_ACCESSORS(was_detached)

  // [is_shared]: true if this is a SharedArrayBuffer or a
  // GrowableSharedArrayBuffer.
  DECL_BOOLEAN_ACCESSORS(is_shared)

  // [is_resizable_by_js]: true if this is a ResizableArrayBuffer or a
  // GrowableSharedArrayBuffer.
  DECL_BOOLEAN_ACCESSORS(is_resizable_by_js)

  // An ArrayBuffer is empty if its BackingStore is empty or if there is none.
  // An empty ArrayBuffer will have a byte_length of zero but not necessarily a
  // nullptr backing_store. An ArrayBuffer with a byte_length of zero may not
  // necessarily be empty though, as it may be a GrowableSharedArrayBuffer.
  // An ArrayBuffer with a size greater than zero is never empty.
  DECL_GETTER(IsEmpty, bool)

  DECL_ACCESSORS(detach_key, Tagged<Object>)

  // Initializes the fields of the ArrayBuffer. The provided backing_store can
  // be nullptr. If it is not nullptr, then the function registers it with
  // src/heap/array-buffer-tracker.h.
  V8_EXPORT_PRIVATE void Setup(SharedFlag shared, ResizableFlag resizable,
                               std::shared_ptr<BackingStore> backing_store,
                               Isolate* isolate);

  // Attaches the backing store to an already constructed empty ArrayBuffer.
  // This is intended to be used only in ArrayBufferConstructor builtin.
  V8_EXPORT_PRIVATE void Attach(std::shared_ptr<BackingStore> backing_store);
  // Detach the backing store from this array buffer if it is detachable.
  // This sets the internal pointer and length to 0 and unregisters the backing
  // store from the array buffer tracker. If the array buffer is not detachable,
  // this is a nop.
  //
  // Array buffers that wrap wasm memory objects are special in that they
  // are normally not detachable, but can become detached as a side effect
  // of growing the underlying memory object. The {force_for_wasm_memory} flag
  // is used by the implementation of Wasm memory growth in order to bypass the
  // non-detachable check.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static Maybe<bool> Detach(
      DirectHandle<JSArrayBuffer> buffer, bool force_for_wasm_memory = false,
      Handle<Object> key = Handle<Object>());

  // Get a reference to backing store of this array buffer, if there is a
  // backing store. Returns nullptr if there is no backing store (e.g. detached
  // or a zero-length array buffer).
  inline std::shared_ptr<BackingStore> GetBackingStore() const;

  inline size_t GetByteLength() const;

  static size_t GsabByteLength(Isolate* isolate, Address raw_array_buffer);

  static Maybe<bool> GetResizableBackingStorePageConfiguration(
      Isolate* isolate, size_t byte_length, size_t max_byte_length,
      ShouldThrow should_throw, size_t* page_size, size_t* initial_pages,
      size_t* max_pages);

  // Allocates an ArrayBufferExtension for this array buffer, unless it is
  // already associated with an extension.
  V8_EXPORT_PRIVATE ArrayBufferExtension* EnsureExtension();

  // Frees the associated ArrayBufferExtension and returns its backing store.
  std::shared_ptr<BackingStore> RemoveExtension();

  // Marks ArrayBufferExtension
  void MarkExtension();
  void YoungMarkExtension();
  void YoungMarkExtensionPromoted();

  //
  // Serializer/deserializer support.
  //

  // Backing stores are serialized/deserialized separately. During serialization
  // the backing store reference is stored in the backing store field and upon
  // deserialization it is converted back to actual external (off-heap) pointer
  // value.
  inline uint32_t GetBackingStoreRefForDeserialization() const;
  inline void SetBackingStoreRefForSerialization(uint32_t ref);

  // Dispatched behavior.
  DECL_PRINTER(JSArrayBuffer)
  DECL_VERIFIER(JSArrayBuffer)

  static constexpr int kSizeWithEmbedderFields =
      kHeaderSize +
      v8::ArrayBuffer::kEmbedderFieldCount * kEmbedderDataSlotSize;
  static constexpr bool kContainsEmbedderFields =
      v8::ArrayBuffer::kEmbedderFieldCount > 0;

  class BodyDescriptor;

 private:
  void DetachInternal(bool force_for_wasm_memory, Isolate* isolate);

#if V8_COMPRESS_POINTERS
  // When pointer compression is enabled, the pointer to the extension is
  // stored in the external pointer table and the object itself only contains a
  // 32-bit external pointer handles. This simplifies alignment requirements
  // and is also necessary for the sandbox.
  inline ExternalPointerHandle* extension_handle_location() const;
#else
  inline ArrayBufferExtension** extension_location() const;
#endif  // V8_COMPRESS_POINTERS

  TQ_OBJECT_CONSTRUCTORS(JSArrayBuffer)
};

// Each JSArrayBuffer (with a backing store) has a corresponding native-heap
// allocated ArrayBufferExtension for GC purposes and storing the backing store.
// When marking a JSArrayBuffer, the GC also marks the native
// extension-object. The GC periodically iterates all extensions concurrently
// and frees unmarked ones.
// https://docs.google.com/document/d/1-ZrLdlFX1nXT3z-FAgLbKal1gI8Auiaya_My-a0UJ28/edit
class ArrayBufferExtension final
#ifdef V8_COMPRESS_POINTERS
    : public ExternalPointerTable::ManagedResource {
#else
    : public Malloced {
#endif  // V8_COMPRESS_POINTERS
 public:
  enum class Age : uint8_t { kYoung, kOld };

  ArrayBufferExtension() : backing_store_(std::shared_ptr<BackingStore>()) {}
  explicit ArrayBufferExtension(std::shared_ptr<BackingStore> backing_store)
      : backing_store_(backing_store) {}

  void Mark() { marked_.store(true, std::memory_order_relaxed); }
  void Unmark() { marked_.store(false, std::memory_order_relaxed); }
  bool IsMarked() const { return marked_.load(std::memory_order_relaxed); }

  void YoungMark() { set_young_gc_state(GcState::Copied); }
  void YoungMarkPromoted() { set_young_gc_state(GcState::Promoted); }
  void YoungUnmark() { set_young_gc_state(GcState::Dead); }
  bool IsYoungMarked() const { return young_gc_state() != GcState::Dead; }

  bool IsYoungPromoted() const { return young_gc_state() == GcState::Promoted; }

  std::shared_ptr<BackingStore> backing_store() { return backing_store_; }
  BackingStore* backing_store_raw() { return backing_store_.get(); }

  size_t accounting_length() const {
    return accounting_length_.load(std::memory_order_relaxed);
  }

  void set_accounting_length(size_t accounting_length) {
    accounting_length_.store(accounting_length, std::memory_order_relaxed);
  }

  size_t ClearAccountingLength() {
    return accounting_length_.exchange(0, std::memory_order_relaxed);
  }

  std::shared_ptr<BackingStore> RemoveBackingStore() {
    return std::move(backing_store_);
  }

  void set_backing_store(std::shared_ptr<BackingStore> backing_store) {
    backing_store_ = std::move(backing_store);
  }

  void reset_backing_store() { backing_store_.reset(); }

  ArrayBufferExtension* next() const { return next_; }
  void set_next(ArrayBufferExtension* extension) { next_ = extension; }

  Age age() const { return age_; }
  void set_age(Age age) { age_ = age; }

 private:
  enum class GcState : uint8_t { Dead = 0, Copied, Promoted };

  Age age_ = Age::kOld;
  std::atomic<bool> marked_{false};
  std::atomic<GcState> young_gc_state_{GcState::Dead};
  std::shared_ptr<BackingStore> backing_store_;
  ArrayBufferExtension* next_ = nullptr;
  std::atomic<size_t> accounting_length_{0};

  GcState young_gc_state() const {
    return young_gc_state_.load(std::memory_order_relaxed);
  }

  void set_young_gc_state(GcState value) {
    young_gc_state_.store(value, std::memory_order_relaxed);
  }
};

class JSArrayBufferView
    : public TorqueGeneratedJSArrayBufferView<JSArrayBufferView,
                                              JSAPIObjectWithEmbedderSlots> {
 public:
  class BodyDescriptor;

  // [byte_offset]: offset of typed array in bytes.
  DECL_PRIMITIVE_ACCESSORS(byte_offset, size_t)

  // [byte_length]: length of typed array in bytes.
  DECL_PRIMITIVE_ACCESSORS(byte_length, size_t)

  DECL_VERIFIER(JSArrayBufferView)

  // Bit positions for [bit_field].
  DEFINE_TORQUE_GENERATED_JS_ARRAY_BUFFER_VIEW_FLAGS()

  inline bool WasDetached() const;

  DECL_BOOLEAN_ACCESSORS(is_length_tracking)
  DECL_BOOLEAN_ACCESSORS(is_backed_by_rab)
  inline bool IsVariableLength() const;

  static_assert(IsAligned(kRawByteOffsetOffset, kUIntptrSize));
  static_assert(IsAligned(kRawByteLengthOffset, kUIntptrSize));

  TQ_OBJECT_CONSTRUCTORS(JSArrayBufferView)
};

class JSTypedArray
    : public TorqueGeneratedJSTypedArray<JSTypedArray, JSArrayBufferView> {
 public:
  static constexpr size_t kMaxByteLength = JSArrayBuffer::kMaxByteLength;
  static_assert(kMaxByteLength == v8::TypedArray::kMaxByteLength);

  // [length]: length of typed array in elements.
  DECL_PRIMITIVE_GETTER(length, size_t)

  DECL_GETTER(base_pointer, Tagged<Object>)
  DECL_ACQUIRE_GETTER(base_pointer, Tagged<Object>)

  // ES6 9.4.5.3
  V8_WARN_UNUSED_RESULT static Maybe<bool> DefineOwnProperty(
      Isolate* isolate, Handle<JSTypedArray> o, Handle<Object> key,
      PropertyDescriptor* desc, Maybe<ShouldThrow> should_throw);

  ExternalArrayType type();
  V8_EXPORT_PRIVATE size_t element_size() const;

  V8_EXPORT_PRIVATE Handle<JSArrayBuffer> GetBuffer();

  // The `DataPtr` is `base_ptr + external_pointer`, and `base_ptr` is nullptr
  // for off-heap typed arrays.
  static constexpr bool kOffHeapDataPtrEqualsExternalPointer = true;

  // Use with care: returns raw pointer into heap.
  inline void* DataPtr();

  inline void SetOffHeapDataPtr(Isolate* isolate, void* base, Address offset);

  // Whether the buffer's backing store is on-heap or off-heap.
  inline bool is_on_heap() const;
  inline bool is_on_heap(AcquireLoadTag tag) const;

  // Only valid to call when IsVariableLength() is true.
  size_t GetVariableLengthOrOutOfBounds(bool& out_of_bounds) const;

  inline size_t GetLengthOrOutOfBounds(bool& out_of_bounds) const;
  inline size_t GetLength() const;
  inline size_t GetByteLength() const;
  inline bool IsOutOfBounds() const;
  inline bool IsDetachedOrOutOfBounds() const;

  static inline void ForFixedTypedArray(ExternalArrayType array_type,
                                        size_t* element_size,
                                        ElementsKind* element_kind);

  static size_t LengthTrackingGsabBackedTypedArrayLength(Isolate* isolate,
                                                         Address raw_array);

  // Note: this is a pointer compression specific optimization.
  // Normally, on-heap typed arrays contain HeapObject value in |base_pointer|
  // field and an offset in |external_pointer|.
  // When pointer compression is enabled we want to combine decompression with
  // the offset addition. In order to do that we add an isolate root to the
  // |external_pointer| value and therefore the data pointer computation can
  // is a simple addition of a (potentially sign-extended) |base_pointer| loaded
  // as Tagged_t value and an |external_pointer| value.
  // For full-pointer mode the compensation value is zero.
  static inline Address ExternalPointerCompensationForOnHeapArray(
      PtrComprCageBase cage_base);

  //
  // Serializer/deserializer support.
  //

  // External backing stores are serialized/deserialized separately.
  // During serialization the backing store reference is stored in the typed
  // array object and upon deserialization it is converted back to actual
  // external (off-heap) pointer value.
  // The backing store reference is stored in the external_pointer field.
  inline uint32_t GetExternalBackingStoreRefForDeserialization() const;
  inline void SetExternalBackingStoreRefForSerialization(uint32_t ref);

  // Subtracts external pointer compensation from the external pointer value.
  inline void RemoveExternalPointerCompensationForSerialization(
      Isolate* isolate);
  // Adds external pointer compensation to the external pointer value.
  inline void AddExternalPointerCompensationForDeserialization(
      Isolate* isolate);

  static inline MaybeHandle<JSTypedArray> Validate(Isolate* isolate,
                                                   Handle<Object> receiver,
                                                   const char* method_name);

  // Dispatched behavior.
  DECL_PRINTER(JSTypedArray)
  DECL_VERIFIER(JSTypedArray)

  // TODO(v8:9287): Re-enable when GCMole stops mixing 32/64 bit configs.
  // static_assert(IsAligned(kLengthOffset, kTaggedSize));
  // static_assert(IsAligned(kExternalPointerOffset, kTaggedSize));

  static constexpr int kSizeWithEmbedderFields =
      kHeaderSize +
      v8::ArrayBufferView::kEmbedderFieldCount * kEmbedderDataSlotSize;
  static constexpr bool kContainsEmbedderFields =
      v8::ArrayBufferView::kEmbedderFieldCount > 0;

  class BodyDescriptor;

#ifdef V8_TYPED_ARRAY_MAX_SIZE_IN_HEAP
  static constexpr size_t kMaxSizeInHeap = V8_TYPED_ARRAY_MAX_SIZE_IN_HEAP;
#else
  static constexpr size_t kMaxSizeInHeap = 64;
#endif

 private:
  template <typename IsolateT>
  friend class Deserializer;
  friend class Factory;

  DECL_PRIMITIVE_SETTER(length, size_t)
  // Reads the "length" field, doesn't assert the TypedArray is not RAB / GSAB
  // backed.
  inline size_t LengthUnchecked() const;

  DECL_GETTER(external_pointer, Address)

  DECL_SETTER(base_pointer, Tagged<Object>)
  DECL_RELEASE_SETTER(base_pointer, Tagged<Object>)

  inline void set_external_pointer(Isolate* isolate, Address value);

  TQ_OBJECT_CONSTRUCTORS(JSTypedArray)
};

class JSDataViewOrRabGsabDataView
    : public TorqueGeneratedJSDataViewOrRabGsabDataView<
          JSDataViewOrRabGsabDataView, JSArrayBufferView> {
 public:
  // [data_pointer]: pointer to the actual data.
  DECL_GETTER(data_pointer, void*)
  inline void set_data_pointer(Isolate* isolate, void* value);

  // TODO(v8:9287): Re-enable when GCMole stops mixing 32/64 bit configs.
  // static_assert(IsAligned(kDataPointerOffset, kTaggedSize));

  static constexpr int kSizeWithEmbedderFields =
      kHeaderSize +
      v8::ArrayBufferView::kEmbedderFieldCount * kEmbedderDataSlotSize;
  static constexpr bool kContainsEmbedderFields =
      v8::ArrayBufferView::kEmbedderFieldCount > 0;

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSDataViewOrRabGsabDataView)
};

class JSDataView
    : public TorqueGeneratedJSDataView<JSDataView,
                                       JSDataViewOrRabGsabDataView> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(JSDataView)
  DECL_VERIFIER(JSDataView)

  TQ_OBJECT_CONSTRUCTORS(JSDataView)
};

class JSRabGsabDataView
    : public TorqueGeneratedJSRabGsabDataView<JSRabGsabDataView,
                                              JSDataViewOrRabGsabDataView> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(JSRabGsabDataView)
  DECL_VERIFIER(JSRabGsabDataView)

  inline size_t GetByteLength() const;
  inline bool IsOutOfBounds() const;

  TQ_OBJECT_CONSTRUCTORS(JSRabGsabDataView)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ARRAY_BUFFER_H_
