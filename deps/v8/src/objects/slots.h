// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SLOTS_H_
#define V8_OBJECTS_SLOTS_H_

#include "src/base/memory.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/external-pointer.h"
#include "src/sandbox/indirect-pointer-tag.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

class Object;
class ExposedTrustedObject;
using TaggedBase = TaggedImpl<HeapObjectReferenceType::STRONG, Address>;

template <typename Subclass, typename Data,
          size_t SlotDataAlignment = sizeof(Data)>
class SlotBase {
 public:
  using TData = Data;

  static constexpr size_t kSlotDataSize = sizeof(Data);
  static constexpr size_t kSlotDataAlignment = SlotDataAlignment;

  Subclass& operator++() {  // Prefix increment.
    ptr_ += kSlotDataSize;
    return *static_cast<Subclass*>(this);
  }
  Subclass operator++(int) {  // Postfix increment.
    Subclass result = *static_cast<Subclass*>(this);
    ptr_ += kSlotDataSize;
    return result;
  }
  Subclass& operator--() {  // Prefix decrement.
    ptr_ -= kSlotDataSize;
    return *static_cast<Subclass*>(this);
  }
  Subclass operator--(int) {  // Postfix decrement.
    Subclass result = *static_cast<Subclass*>(this);
    ptr_ -= kSlotDataSize;
    return result;
  }

  bool operator<(const SlotBase& other) const { return ptr_ < other.ptr_; }
  bool operator<=(const SlotBase& other) const { return ptr_ <= other.ptr_; }
  bool operator>(const SlotBase& other) const { return ptr_ > other.ptr_; }
  bool operator>=(const SlotBase& other) const { return ptr_ >= other.ptr_; }
  bool operator==(const SlotBase& other) const { return ptr_ == other.ptr_; }
  bool operator!=(const SlotBase& other) const { return ptr_ != other.ptr_; }
  size_t operator-(const SlotBase& other) const {
    DCHECK_GE(ptr_, other.ptr_);
    return static_cast<size_t>((ptr_ - other.ptr_) / kSlotDataSize);
  }
  Subclass operator-(int i) const { return Subclass(ptr_ - i * kSlotDataSize); }
  Subclass operator+(int i) const { return Subclass(ptr_ + i * kSlotDataSize); }
  friend Subclass operator+(int i, const Subclass& slot) {
    return Subclass(slot.ptr_ + i * kSlotDataSize);
  }
  Subclass& operator+=(int i) {
    ptr_ += i * kSlotDataSize;
    return *static_cast<Subclass*>(this);
  }
  Subclass operator-(int i) { return Subclass(ptr_ - i * kSlotDataSize); }
  Subclass& operator-=(int i) {
    ptr_ -= i * kSlotDataSize;
    return *static_cast<Subclass*>(this);
  }

  void* ToVoidPtr() const { return reinterpret_cast<void*>(address()); }

  Address address() const { return ptr_; }
  // For symmetry with Handle.
  TData* location() const { return reinterpret_cast<TData*>(ptr_); }

 protected:
  explicit SlotBase(Address ptr) : ptr_(ptr) {
    DCHECK(IsAligned(ptr, kSlotDataAlignment));
  }

 private:
  // This field usually describes an on-heap address (a slot within an object),
  // so its type should not be a pointer to another C++ wrapper class.
  // Type safety is provided by well-defined conversion operations.
  Address ptr_;
};

// An FullObjectSlot instance describes a kSystemPointerSize-sized field
// ("slot") holding a tagged pointer (smi or strong heap object).
// Its address() is the address of the slot.
// The slot's contents can be read and written using operator* and store().
class FullObjectSlot : public SlotBase<FullObjectSlot, Address> {
 public:
  using TObject = Tagged<Object>;
  using THeapObjectSlot = FullHeapObjectSlot;

  // Tagged value stored in this slot is guaranteed to never be a weak pointer.
  static constexpr bool kCanBeWeak = false;

  FullObjectSlot() : SlotBase(kNullAddress) {}
  explicit FullObjectSlot(Address ptr) : SlotBase(ptr) {}
  explicit FullObjectSlot(const Address* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  inline explicit FullObjectSlot(TaggedBase* object);
  template <typename T>
  explicit FullObjectSlot(SlotBase<T, TData, kSlotDataAlignment> slot)
      : SlotBase(slot.address()) {}

  // Compares memory representation of a value stored in the slot with given
  // raw value.
  inline bool contains_map_value(Address raw_value) const;
  inline bool Relaxed_ContainsMapValue(Address raw_value) const;

  inline Tagged<Object> operator*() const;
  inline Tagged<Object> load() const;
  inline Tagged<Object> load(PtrComprCageBase cage_base) const;
  inline void store(Tagged<Object> value) const;
  inline void store_map(Tagged<Map> map) const;

  inline Tagged<Map> load_map() const;

  inline Tagged<Object> Acquire_Load() const;
  inline Tagged<Object> Acquire_Load(PtrComprCageBase cage_base) const;
  inline Tagged<Object> Relaxed_Load() const;
  inline Tagged<Object> Relaxed_Load(PtrComprCageBase cage_base) const;
  inline Address Relaxed_Load_Raw() const;
  static inline Tagged<Object> RawToTagged(PtrComprCageBase cage_base,
                                           Address raw);
  inline void Relaxed_Store(Tagged<Object> value) const;
  inline void Release_Store(Tagged<Object> value) const;
  inline Tagged<Object> Relaxed_CompareAndSwap(Tagged<Object> old,
                                               Tagged<Object> target) const;
  inline Tagged<Object> Release_CompareAndSwap(Tagged<Object> old,
                                               Tagged<Object> target) const;
};

// A FullMaybeObjectSlot instance describes a kSystemPointerSize-sized field
// ("slot") holding a possibly-weak tagged pointer (think: Tagged<MaybeObject>).
// Its address() is the address of the slot.
// The slot's contents can be read and written using operator* and store().
class FullMaybeObjectSlot
    : public SlotBase<FullMaybeObjectSlot, Address, kSystemPointerSize> {
 public:
  using TObject = Tagged<MaybeObject>;
  using THeapObjectSlot = FullHeapObjectSlot;

  // Tagged value stored in this slot can be a weak pointer.
  static constexpr bool kCanBeWeak = true;

  FullMaybeObjectSlot() : SlotBase(kNullAddress) {}
  explicit FullMaybeObjectSlot(Address ptr) : SlotBase(ptr) {}
  explicit FullMaybeObjectSlot(TaggedBase* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  explicit FullMaybeObjectSlot(Tagged<MaybeObject>* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  template <typename T>
  explicit FullMaybeObjectSlot(SlotBase<T, TData, kSlotDataAlignment> slot)
      : SlotBase(slot.address()) {}

  inline Tagged<MaybeObject> operator*() const;
  inline Tagged<MaybeObject> load(PtrComprCageBase cage_base) const;
  inline void store(Tagged<MaybeObject> value) const;

  inline Tagged<MaybeObject> Relaxed_Load() const;
  inline Tagged<MaybeObject> Relaxed_Load(PtrComprCageBase cage_base) const;
  inline Address Relaxed_Load_Raw() const;
  static inline Tagged<Object> RawToTagged(PtrComprCageBase cage_base,
                                           Address raw);
  inline void Relaxed_Store(Tagged<MaybeObject> value) const;
  inline void Release_CompareAndSwap(Tagged<MaybeObject> old,
                                     Tagged<MaybeObject> target) const;
};

// A FullHeapObjectSlot instance describes a kSystemPointerSize-sized field
// ("slot") holding a weak or strong pointer to a heap object (think:
// Tagged<HeapObjectReference>).
// Its address() is the address of the slot.
// The slot's contents can be read and written using operator* and store().
// In case it is known that that slot contains a strong heap object pointer,
// ToHeapObject() can be used to retrieve that heap object.
class FullHeapObjectSlot : public SlotBase<FullHeapObjectSlot, Address> {
 public:
  FullHeapObjectSlot() : SlotBase(kNullAddress) {}
  explicit FullHeapObjectSlot(Address ptr) : SlotBase(ptr) {}
  explicit FullHeapObjectSlot(TaggedBase* ptr)
      : SlotBase(reinterpret_cast<Address>(ptr)) {}
  template <typename T>
  explicit FullHeapObjectSlot(SlotBase<T, TData, kSlotDataAlignment> slot)
      : SlotBase(slot.address()) {}

  inline Tagged<HeapObjectReference> operator*() const;
  inline Tagged<HeapObjectReference> load(PtrComprCageBase cage_base) const;
  inline void store(Tagged<HeapObjectReference> value) const;

  inline Tagged<HeapObject> ToHeapObject() const;

  inline void StoreHeapObject(Tagged<HeapObject> value) const;
};

// TODO(ishell, v8:8875): When pointer compression is enabled the [u]intptr_t
// and double fields are only kTaggedSize aligned so in order to avoid undefined
// behavior in C++ code we use this iterator adaptor when using STL algorithms
// with unaligned pointers.
// It will be removed once all v8:8875 is fixed and all the full pointer and
// double values in compressed V8 heap are properly aligned.
template <typename T>
class UnalignedSlot : public SlotBase<UnalignedSlot<T>, T, 1> {
 public:
  // This class is a stand-in for "T&" that uses custom read/write operations
  // for the actual memory accesses.
  class Reference {
   public:
    explicit Reference(Address address) : address_(address) {}
    Reference(const Reference&) V8_NOEXCEPT = default;

    Reference& operator=(const Reference& other) V8_NOEXCEPT {
      base::WriteUnalignedValue<T>(address_, other.value());
      return *this;
    }
    Reference& operator=(T value) {
      base::WriteUnalignedValue<T>(address_, value);
      return *this;
    }

    // Values of type UnalignedSlot::reference must be implicitly convertible
    // to UnalignedSlot::value_type.
    operator T() const { return value(); }

    void swap(Reference& other) {
      T tmp = value();
      base::WriteUnalignedValue<T>(address_, other.value());
      base::WriteUnalignedValue<T>(other.address_, tmp);
    }

    bool operator<(const Reference& other) const {
      return value() < other.value();
    }

    bool operator==(const Reference& other) const {
      return value() == other.value();
    }

   private:
    T value() const { return base::ReadUnalignedValue<T>(address_); }

    Address address_;
  };

  // The rest of this class follows C++'s "RandomAccessIterator" requirements.
  // Most of the heavy lifting is inherited from SlotBase.
  using difference_type = int;
  using value_type = T;
  using reference = Reference;
  using pointer = T*;
  using iterator_category = std::random_access_iterator_tag;

  UnalignedSlot() : SlotBase<UnalignedSlot<T>, T, 1>(kNullAddress) {}
  explicit UnalignedSlot(Address address)
      : SlotBase<UnalignedSlot<T>, T, 1>(address) {}
  explicit UnalignedSlot(T* address)
      : SlotBase<UnalignedSlot<T>, T, 1>(reinterpret_cast<Address>(address)) {}

  Reference operator*() const {
    return Reference(SlotBase<UnalignedSlot<T>, T, 1>::address());
  }
  Reference operator[](difference_type i) const {
    return Reference(SlotBase<UnalignedSlot<T>, T, 1>::address() +
                     i * sizeof(T));
  }

  friend void swap(Reference lhs, Reference rhs) { lhs.swap(rhs); }

  friend difference_type operator-(UnalignedSlot a, UnalignedSlot b) {
    return static_cast<int>(a.address() - b.address()) / sizeof(T);
  }
};

// An off-heap uncompressed object slot can be the same as an on-heap one, with
// a few methods deleted.
class OffHeapFullObjectSlot : public FullObjectSlot {
 public:
  OffHeapFullObjectSlot() : FullObjectSlot() {}
  explicit OffHeapFullObjectSlot(Address ptr) : FullObjectSlot(ptr) {}
  explicit OffHeapFullObjectSlot(const Address* ptr) : FullObjectSlot(ptr) {}

  inline Tagged<Object> operator*() const = delete;

  using FullObjectSlot::Relaxed_Load;
};

// An ExternalPointerSlot instance describes a kExternalPointerSlotSize-sized
// field ("slot") holding a pointer to objects located outside the V8 heap and
// V8 sandbox (think: ExternalPointer_t).
// It's basically an ExternalPointer_t* but abstracting away the fact that the
// pointer might not be kExternalPointerSlotSize-aligned in certain
// configurations. Its address() is the address of the slot.
class ExternalPointerSlot
    : public SlotBase<ExternalPointerSlot, ExternalPointer_t,
                      kTaggedSize /* slot alignment */> {
 public:
  ExternalPointerSlot()
      : SlotBase(kNullAddress)
#ifdef V8_COMPRESS_POINTERS
        ,
        tag_(kExternalPointerNullTag)
#endif
  {
  }

  explicit ExternalPointerSlot(Address ptr, ExternalPointerTag tag)
      : SlotBase(ptr)
#ifdef V8_COMPRESS_POINTERS
        ,
        tag_(tag)
#endif
  {
  }

  template <ExternalPointerTag tag>
  explicit ExternalPointerSlot(ExternalPointerMember<tag>* member)
      : SlotBase(member->storage_address())
#ifdef V8_COMPRESS_POINTERS
        ,
        tag_(tag)
#endif
  {
  }

  inline void init(IsolateForSandbox isolate, Tagged<HeapObject> host,
                   Address value);

#ifdef V8_COMPRESS_POINTERS
  // When the external pointer is sandboxed, or for array buffer extensions when
  // pointer compression is on, its slot stores a handle to an entry in an
  // ExternalPointerTable. These methods allow access to the underlying handle
  // while the load/store methods below resolve the handle to the real pointer.
  // Handles should generally be accessed atomically as they may be accessed
  // from other threads, for example GC marking threads.
  //
  // TODO(wingo): Remove if we switch to use the EPT for all external pointers
  // when pointer compression is enabled.
  bool HasExternalPointerHandle() const {
    return V8_ENABLE_SANDBOX_BOOL || tag() == kArrayBufferExtensionTag;
  }
  inline ExternalPointerHandle Relaxed_LoadHandle() const;
  inline void Relaxed_StoreHandle(ExternalPointerHandle handle) const;
  inline void Release_StoreHandle(ExternalPointerHandle handle) const;
#endif  // V8_COMPRESS_POINTERS

  inline Address load(IsolateForSandbox isolate);
  inline void store(IsolateForSandbox isolate, Address value);

  // ExternalPointerSlot serialization support.
  // These methods can be used to clear an external pointer slot prior to
  // serialization and restore it afterwards. This is useful in cases where the
  // external pointer is not contained in the snapshot but will instead be
  // reconstructed during deserialization.
  // Note that GC must be disallowed while an object's external slot is cleared
  // as otherwise the corresponding entry in the external pointer table may not
  // be marked as alive.
  using RawContent = ExternalPointer_t;
  inline RawContent GetAndClearContentForSerialization(
      const DisallowGarbageCollection& no_gc);
  inline void RestoreContentAfterSerialization(
      RawContent content, const DisallowGarbageCollection& no_gc);
  // The ReadOnlySerializer replaces the RawContent in-place.
  inline void ReplaceContentWithIndexForSerialization(
      const DisallowGarbageCollection& no_gc, uint32_t index);
  inline uint32_t GetContentAsIndexAfterDeserialization(
      const DisallowGarbageCollection& no_gc);

#ifdef V8_COMPRESS_POINTERS
  ExternalPointerTag tag() const { return tag_; }
#else
  ExternalPointerTag tag() const { return kExternalPointerNullTag; }
#endif  // V8_COMPRESS_POINTERS

 private:
#ifdef V8_COMPRESS_POINTERS
  ExternalPointerHandle* handle_location() const {
    DCHECK(HasExternalPointerHandle());
    return reinterpret_cast<ExternalPointerHandle*>(address());
  }

  // The tag associated with this slot.
  ExternalPointerTag tag_;
#endif  // V8_COMPRESS_POINTERS
};

// Similar to ExternalPointerSlot with the difference that it refers to an
// `CppHeapPointer_t` which has different sizing and alignment than
// `ExternalPointer_t`.
class CppHeapPointerSlot
    : public SlotBase<CppHeapPointerSlot, CppHeapPointer_t,
                      /*SlotDataAlignment=*/sizeof(CppHeapPointer_t)> {
 public:
  CppHeapPointerSlot() : SlotBase(kNullAddress) {}

  CppHeapPointerSlot(Address ptr) : SlotBase(ptr) {}

#ifdef V8_COMPRESS_POINTERS

  // When V8 runs with pointer compression, the slots here store a handle to an
  // entry in a dedicated ExternalPointerTable that is only used for CppHeap
  // references. These methods allow access to the underlying handle while the
  // load/store methods below resolve the handle to the real pointer. Handles
  // should generally be accessed atomically as they may be accessed from other
  // threads, for example GC marking threads.
  inline CppHeapPointerHandle Relaxed_LoadHandle() const;
  inline void Relaxed_StoreHandle(CppHeapPointerHandle handle) const;
  inline void Release_StoreHandle(CppHeapPointerHandle handle) const;

#endif  // V8_COMPRESS_POINTERS

  inline Address try_load(IsolateForPointerCompression isolate,
                          CppHeapPointerTagRange tag_range) const;
  inline void store(IsolateForPointerCompression isolate, Address value,
                    CppHeapPointerTag tag) const;
  inline void init() const;
};

// An IndirectPointerSlot instance describes a 32-bit field ("slot") containing
// an IndirectPointerHandle, i.e. an index to an entry in a pointer table which
// contains the "real" pointer to the referenced HeapObject. These slots are
// used when the sandbox is enabled to securely reference HeapObjects outside
// of the sandbox.
class IndirectPointerSlot
    : public SlotBase<IndirectPointerSlot, IndirectPointerHandle,
                      kTaggedSize /* slot alignment */> {
 public:
  IndirectPointerSlot()
      : SlotBase(kNullAddress)
#ifdef V8_ENABLE_SANDBOX
        ,
        tag_(kIndirectPointerNullTag)
#endif
  {
  }

  explicit IndirectPointerSlot(Address ptr, IndirectPointerTag tag)
      : SlotBase(ptr)
#ifdef V8_ENABLE_SANDBOX
        ,
        tag_(tag)
#endif
  {
  }

  // Even though only HeapObjects can be stored into an IndirectPointerSlot,
  // these slots can be empty (containing kNullIndirectPointerHandle), in which
  // case load() will return Smi::zero().
  inline Tagged<Object> load(IsolateForSandbox isolate) const;
  inline void store(Tagged<ExposedTrustedObject> value) const;

  // Load the value of this slot.
  // The isolate parameter is required unless using the kCodeTag tag, as these
  // object use a different pointer table.
  inline Tagged<Object> Relaxed_Load(IsolateForSandbox isolate) const;
  inline Tagged<Object> Acquire_Load(IsolateForSandbox isolate) const;

  // Store a reference to the given object into this slot. The object must be
  // indirectly refereceable.
  inline void Relaxed_Store(Tagged<ExposedTrustedObject> value) const;
  inline void Release_Store(Tagged<ExposedTrustedObject> value) const;

  inline IndirectPointerHandle Relaxed_LoadHandle() const;
  inline IndirectPointerHandle Acquire_LoadHandle() const;
  inline void Relaxed_StoreHandle(IndirectPointerHandle handle) const;
  inline void Release_StoreHandle(IndirectPointerHandle handle) const;

#ifdef V8_ENABLE_SANDBOX
  IndirectPointerTag tag() const { return tag_; }
#else
  IndirectPointerTag tag() const { return kIndirectPointerNullTag; }
#endif

  // Whether this slot is empty, i.e. contains a null handle.
  inline bool IsEmpty() const;

  // Retrieve the object referenced by the given handle by determining the
  // appropriate pointer table to use and loading the referenced entry in it.
  // This method is used internally by load() and related functions but can
  // also be used to manually implement indirect pointer accessors.
  inline Tagged<Object> ResolveHandle(IndirectPointerHandle handle,
                                      IsolateForSandbox isolate) const;

 private:
#ifdef V8_ENABLE_SANDBOX
  // Retrieve the object referenced through the given trusted pointer handle
  // from the trusted pointer table.
  inline Tagged<Object> ResolveTrustedPointerHandle(
      IndirectPointerHandle handle, IsolateForSandbox isolate) const;
  // Retrieve the Code object referenced through the given code pointer handle
  // from the code pointer table.
  inline Tagged<Object> ResolveCodePointerHandle(
      IndirectPointerHandle handle) const;

  // The tag associated with this slot.
  IndirectPointerTag tag_;
#endif  // V8_ENABLE_SANDBOX
};

class WritableJitAllocation;

template <typename SlotT>
class WriteProtectedSlot : public SlotT {
 public:
  using TObject = typename SlotT::TObject;
  using SlotT::kCanBeWeak;

  explicit WriteProtectedSlot(WritableJitAllocation& jit_allocation,
                              Address ptr)
      : SlotT(ptr), jit_allocation_(jit_allocation) {}

  inline TObject Relaxed_Load() const { return SlotT::Relaxed_Load(); }
  inline TObject Relaxed_Load(PtrComprCageBase cage_base) const {
    return SlotT::Relaxed_Load(cage_base);
  }

  inline void Relaxed_Store(TObject value) const;

 private:
  WritableJitAllocation& jit_allocation_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_SLOTS_H_
