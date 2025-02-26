// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_COMPRESSED_SLOTS_H_
#define V8_OBJECTS_COMPRESSED_SLOTS_H_

#include "include/v8config.h"
#include "src/common/globals.h"
#include "src/common/ptr-compr.h"
#include "src/objects/slots.h"
#include "src/objects/tagged-field.h"

namespace v8::internal {

#ifdef V8_COMPRESS_POINTERS

// A CompressedObjectSlot instance describes a kTaggedSize-sized field ("slot")
// holding a compressed tagged pointer (smi or heap object).
// Its address() is the address of the slot.
// The slot's contents can be read and written using operator* and store().
class CompressedObjectSlot : public SlotBase<CompressedObjectSlot, Tagged_t> {
 public:
  using TCompressionScheme = V8HeapCompressionScheme;
  using TObject = Tagged<Object>;
  using THeapObjectSlot = CompressedHeapObjectSlot;

  static constexpr bool kCanBeWeak = false;

  CompressedObjectSlot() : SlotBase(kNullAddress) {}
  explicit CompressedObjectSlot(Address ptr) : SlotBase(ptr) {}
  explicit CompressedObjectSlot(Address* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  inline explicit CompressedObjectSlot(Tagged<Object>* object);
  explicit CompressedObjectSlot(Tagged<Object> const* const* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  explicit CompressedObjectSlot(const TaggedMemberBase* member)
      : SlotBase(reinterpret_cast<Address>(member->ptr_location())) {}
  template <typename T>
  explicit CompressedObjectSlot(SlotBase<T, TData, kSlotDataAlignment> slot)
      : SlotBase(slot.address()) {}

  // Compares memory representation of a value stored in the slot with given
  // raw value without decompression.
  inline bool contains_map_value(Address raw_value) const;
  inline bool Relaxed_ContainsMapValue(Address raw_value) const;

  // TODO(leszeks): Consider deprecating the operator* load, and always pass the
  // Isolate.
  inline Tagged<Object> operator*() const;
  // TODO(saelo): it would be nice if we could have two load variants: one that
  // takes no arguments (which should normally be used), and one that takes an
  // Isolate* or an IsolateForSandbox to be compatible with the
  // IndirectPointerSlot. Then, all slots that contain HeapObject references
  // would have at least a `load(isolate)` variant, and so could that could be
  // used in cases where only the slots content matters.
  inline Tagged<Object> load() const;
  inline Tagged<Object> load(PtrComprCageBase cage_base) const;
  inline void store(Tagged<Object> value) const;
  inline void store_map(Tagged<Map> map) const;

  inline Tagged<Map> load_map() const;

  inline Tagged<Object> Acquire_Load() const;
  inline Tagged<Object> Relaxed_Load() const;
  inline Tagged<Object> Relaxed_Load(PtrComprCageBase cage_base) const;
  inline Tagged_t Relaxed_Load_Raw() const;
  static inline Tagged<Object> RawToTagged(PtrComprCageBase cage_base,
                                           Tagged_t raw);
  inline void Relaxed_Store(Tagged<Object> value) const;
  inline void Release_Store(Tagged<Object> value) const;
  inline Tagged<Object> Release_CompareAndSwap(Tagged<Object> old,
                                               Tagged<Object> target) const;
};

// A CompressedMaybeObjectSlot instance describes a kTaggedSize-sized field
// ("slot") holding a possibly-weak compressed tagged pointer
// (think: Tagged<MaybeObject>).
// Its address() is the address of the slot.
// The slot's contents can be read and written using operator* and store().
class CompressedMaybeObjectSlot
    : public SlotBase<CompressedMaybeObjectSlot, Tagged_t> {
 public:
  using TCompressionScheme = V8HeapCompressionScheme;
  using TObject = Tagged<MaybeObject>;
  using THeapObjectSlot = CompressedHeapObjectSlot;

  static constexpr bool kCanBeWeak = true;

  CompressedMaybeObjectSlot() : SlotBase(kNullAddress) {}
  explicit CompressedMaybeObjectSlot(Address ptr) : SlotBase(ptr) {}
  explicit CompressedMaybeObjectSlot(Tagged<Object>* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  explicit CompressedMaybeObjectSlot(Tagged<MaybeObject>* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  explicit CompressedMaybeObjectSlot(const TaggedMemberBase* member)
      : SlotBase(reinterpret_cast<Address>(member->ptr_location())) {}
  template <typename T>
  explicit CompressedMaybeObjectSlot(
      SlotBase<T, TData, kSlotDataAlignment> slot)
      : SlotBase(slot.address()) {}

  inline Tagged<MaybeObject> operator*() const;
  inline Tagged<MaybeObject> load() const;
  inline Tagged<MaybeObject> load(PtrComprCageBase cage_base) const;
  inline void store(Tagged<MaybeObject> value) const;

  inline Tagged<MaybeObject> Relaxed_Load() const;
  inline Tagged<MaybeObject> Relaxed_Load(PtrComprCageBase cage_base) const;
  inline Tagged_t Relaxed_Load_Raw() const;
  static inline Tagged<Object> RawToTagged(PtrComprCageBase cage_base,
                                           Tagged_t raw);
  inline void Relaxed_Store(Tagged<MaybeObject> value) const;
  inline void Release_CompareAndSwap(Tagged<MaybeObject> old,
                                     Tagged<MaybeObject> target) const;
};

// A CompressedHeapObjectSlot instance describes a kTaggedSize-sized field
// ("slot") holding a weak or strong compressed pointer to a heap object (think:
// Tagged<HeapObjectReference>).
// Its address() is the address of the slot.
// The slot's contents can be read and written using operator* and store().
// In case it is known that that slot contains a strong heap object pointer,
// ToHeapObject() can be used to retrieve that heap object.
class CompressedHeapObjectSlot
    : public SlotBase<CompressedHeapObjectSlot, Tagged_t> {
 public:
  using TCompressionScheme = V8HeapCompressionScheme;

  CompressedHeapObjectSlot() : SlotBase(kNullAddress) {}
  explicit CompressedHeapObjectSlot(Address ptr) : SlotBase(ptr) {}
  explicit CompressedHeapObjectSlot(TaggedBase* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  template <typename T>
  explicit CompressedHeapObjectSlot(SlotBase<T, TData, kSlotDataAlignment> slot)
      : SlotBase(slot.address()) {}

  inline Tagged<HeapObjectReference> operator*() const;
  inline Tagged<HeapObjectReference> load(PtrComprCageBase cage_base) const;
  inline void store(Tagged<HeapObjectReference> value) const;

  inline Tagged<HeapObject> ToHeapObject() const;

  inline void StoreHeapObject(Tagged<HeapObject> value) const;
};

// An OffHeapCompressedObjectSlot instance describes a kTaggedSize-sized field
// ("slot") holding a compressed tagged pointer (smi or heap object).
// Unlike CompressedObjectSlot, it does not assume that the slot is on the heap,
// and so does not provide an operator* with implicit Isolate* calculation.
// Its address() is the address of the slot.
// The slot's contents can be read and written using load() and store().
template <typename CompressionScheme, typename TObject, typename Subclass>
class OffHeapCompressedObjectSlotBase : public SlotBase<Subclass, Tagged_t> {
 public:
  using TSlotBase = SlotBase<Subclass, Tagged_t>;
  using TCompressionScheme = CompressionScheme;

  OffHeapCompressedObjectSlotBase() : TSlotBase(kNullAddress) {}
  explicit OffHeapCompressedObjectSlotBase(Address ptr) : TSlotBase(ptr) {}
  explicit OffHeapCompressedObjectSlotBase(const uint32_t* ptr)
      : TSlotBase(reinterpret_cast<Address>(ptr)) {}

  inline TObject load() const;
  inline TObject load(PtrComprCageBase cage_base) const;
  inline void store(TObject value) const;

  inline TObject Relaxed_Load() const;
  // TODO(saelo): same as in CompressedObjectSlot, consider removing the load
  // variant with a PtrComprCageBase but instead adding one with an isolate
  // parameter that simply forwards the the parameterless variant.
  inline TObject Relaxed_Load(PtrComprCageBase cage_base) const;
  inline Tagged_t Relaxed_Load_Raw() const;
  static inline Tagged<Object> RawToTagged(PtrComprCageBase cage_base,
                                           Tagged_t raw);
  inline TObject Acquire_Load() const;
  inline TObject Acquire_Load(PtrComprCageBase cage_base) const;
  inline void Relaxed_Store(TObject value) const;
  inline void Release_Store(TObject value) const;
  inline void Release_CompareAndSwap(TObject old, TObject target) const;
};

template <typename CompressionScheme>
class OffHeapCompressedObjectSlot
    : public OffHeapCompressedObjectSlotBase<
          CompressionScheme, Tagged<Object>,
          OffHeapCompressedObjectSlot<CompressionScheme>> {
 public:
  using TSlotBase = OffHeapCompressedObjectSlotBase<
      CompressionScheme, Tagged<Object>,
      OffHeapCompressedObjectSlot<CompressionScheme>>;
  using TObject = Tagged<Object>;
  using THeapObjectSlot = OffHeapCompressedObjectSlot<CompressionScheme>;

  static constexpr bool kCanBeWeak = false;

  OffHeapCompressedObjectSlot() : TSlotBase(kNullAddress) {}
  explicit OffHeapCompressedObjectSlot(Address ptr) : TSlotBase(ptr) {}
  explicit OffHeapCompressedObjectSlot(const uint32_t* ptr)
      : TSlotBase(reinterpret_cast<Address>(ptr)) {}
  // TODO(jkummerow): Not sure this is useful?
  template <typename T>
  explicit OffHeapCompressedObjectSlot(SlotBase<T, Tagged_t> slot)
      : TSlotBase(slot.address()) {}
};

template <typename CompressionScheme>
class OffHeapCompressedMaybeObjectSlot
    : public OffHeapCompressedObjectSlotBase<
          CompressionScheme, Tagged<MaybeObject>,
          OffHeapCompressedMaybeObjectSlot<CompressionScheme>> {
 public:
  using TSlotBase = OffHeapCompressedObjectSlotBase<
      CompressionScheme, Tagged<MaybeObject>,
      OffHeapCompressedMaybeObjectSlot<CompressionScheme>>;
  using TObject = Tagged<MaybeObject>;
  using THeapObjectSlot = OffHeapCompressedMaybeObjectSlot<CompressionScheme>;

  static constexpr bool kCanBeWeak = true;

  OffHeapCompressedMaybeObjectSlot() : TSlotBase(kNullAddress) {}
  explicit OffHeapCompressedMaybeObjectSlot(Address ptr) : TSlotBase(ptr) {}
  explicit OffHeapCompressedMaybeObjectSlot(const uint32_t* ptr)
      : TSlotBase(reinterpret_cast<Address>(ptr)) {}
};

#endif  // V8_COMPRESS_POINTERS

}  // namespace v8::internal

#endif  // V8_OBJECTS_COMPRESSED_SLOTS_H_
