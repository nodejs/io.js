// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_EMBEDDER_DATA_SLOT_H_
#define V8_OBJECTS_EMBEDDER_DATA_SLOT_H_

#include <utility>

#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/objects/slots.h"
#include "src/sandbox/isolate.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class EmbedderDataArray;
class JSObject;
class Object;

// An EmbedderDataSlot instance describes a kEmbedderDataSlotSize field ("slot")
// holding an embedder data which may contain raw aligned pointer or a tagged
// pointer (smi or heap object).
// Its address() is the address of the slot.
// The slot's contents can be read and written using respective load_XX() and
// store_XX() methods.
// Storing heap object through this slot may require triggering write barriers
// so this operation must be done via static store_tagged() methods.
class EmbedderDataSlot
    : public SlotBase<EmbedderDataSlot, Address, kTaggedSize> {
 public:
#ifdef V8_ENABLE_SANDBOX
  // When the sandbox is enabled, an EmbedderDataSlot always contains a valid
  // external pointer table index (initially, zero) in it's "raw" part and a
  // valid tagged value in its 32-bit "tagged" part.
  //
  // Layout (sandbox):
  // +-----------------------------------+-----------------------------------+
  // | Tagged (Smi/CompressedPointer)    | External Pointer Table Index      |
  // +-----------------------------------+-----------------------------------+
  // ^                                   ^
  // kTaggedPayloadOffset                kRawPayloadOffset
  //                                     kExternalPointerOffset
  static constexpr int kTaggedPayloadOffset = 0;
  static constexpr int kRawPayloadOffset = kTaggedSize;
  static constexpr int kExternalPointerOffset = kRawPayloadOffset;
#elif defined(V8_COMPRESS_POINTERS) && defined(V8_TARGET_BIG_ENDIAN)
  // The raw payload is located in the other "tagged" part of the full pointer
  // and cotains the upper part of an aligned address. The raw part is not
  // expected to look like a tagged value.
  //
  // Layout (big endian pointer compression):
  // +-----------------------------------+-----------------------------------+
  // | External Pointer (high word)      | Tagged (Smi/CompressedPointer)    |
  // |                                   | OR External Pointer (low word)    |
  // +-----------------------------------+-----------------------------------+
  // ^                                   ^
  // kRawPayloadOffset                   kTaggedayloadOffset
  // kExternalPointerOffset
  static constexpr int kExternalPointerOffset = 0;
  static constexpr int kRawPayloadOffset = 0;
  static constexpr int kTaggedPayloadOffset = kTaggedSize;
#elif defined(V8_COMPRESS_POINTERS) && defined(V8_TARGET_LITTLE_ENDIAN)
  // Layout (little endian pointer compression):
  // +-----------------------------------+-----------------------------------+
  // | Tagged (Smi/CompressedPointer)    | External Pointer (high word)      |
  // | OR External Pointer (low word)    |                                   |
  // +-----------------------------------+-----------------------------------+
  // ^                                   ^
  // kTaggedPayloadOffset                kRawPayloadOffset
  // kExternalPointerOffset
  static constexpr int kExternalPointerOffset = 0;
  static constexpr int kTaggedPayloadOffset = 0;
  static constexpr int kRawPayloadOffset = kTaggedSize;
#else
  // Layout (no pointer compression):
  // +-----------------------------------------------------------------------+
  // | Tagged (Smi/Pointer) OR External Pointer                              |
  // +-----------------------------------------------------------------------+
  // ^
  // kTaggedPayloadOffset
  // kExternalPointerOffset
  static constexpr int kTaggedPayloadOffset = 0;
  static constexpr int kExternalPointerOffset = 0;
#endif  // V8_ENABLE_SANDBOX

  static constexpr int kRequiredPtrAlignment = kSmiTagSize;

  EmbedderDataSlot() : SlotBase(kNullAddress) {}
  V8_INLINE EmbedderDataSlot(Tagged<EmbedderDataArray> array, int entry_index);
  V8_INLINE EmbedderDataSlot(Tagged<JSObject> object, int embedder_field_index);

  // Opaque type used for storing raw embedder data.
  using RawData = Address;

  V8_INLINE void Initialize(Tagged<Object> initial_value);

  V8_INLINE Tagged<Object> load_tagged() const;
  V8_INLINE void store_smi(Tagged<Smi> value);

  // Setting an arbitrary tagged value requires triggering a write barrier
  // which requires separate object and offset values, therefore these static
  // functions also has the target object parameter.
  static V8_INLINE void store_tagged(Tagged<EmbedderDataArray> array,
                                     int entry_index, Tagged<Object> value);
  static V8_INLINE void store_tagged(Tagged<JSObject> object,
                                     int embedder_field_index,
                                     Tagged<Object> value);

  // Tries reinterpret the value as an aligned pointer and sets *out_result to
  // the pointer-like value. Note, that some Smis could still look like an
  // aligned pointers.
  // Returns true on success.
  // When the sandbox is enabled, calling this method when the raw part of the
  // slot does not contain valid external pointer table index is undefined
  // behaviour and most likely result in crashes.
  V8_INLINE bool ToAlignedPointer(IsolateForSandbox isolate,
                                  void** out_result) const;

  // Returns true if the pointer was successfully stored or false it the pointer
  // was improperly aligned.
  V8_INLINE V8_WARN_UNUSED_RESULT bool store_aligned_pointer(
      IsolateForSandbox isolate, Tagged<HeapObject> host, void* ptr);

  V8_INLINE bool MustClearDuringSerialization(
      const DisallowGarbageCollection& no_gc);
  V8_INLINE RawData load_raw(IsolateForSandbox isolate,
                             const DisallowGarbageCollection& no_gc) const;
  V8_INLINE void store_raw(IsolateForSandbox isolate, RawData data,
                           const DisallowGarbageCollection& no_gc);

 private:
  // Stores given value to the embedder data slot in a concurrent-marker
  // friendly manner (tagged part of the slot is written atomically).
  V8_INLINE void gc_safe_store(IsolateForSandbox isolate, Address value);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_EMBEDDER_DATA_SLOT_H_
