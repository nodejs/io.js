// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HEAP_OBJECT_H_
#define V8_OBJECTS_HEAP_OBJECT_H_

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/objects/casting.h"
#include "src/objects/instance-type.h"
#include "src/objects/slots.h"
#include "src/objects/tagged-field.h"
#include "src/sandbox/indirect-pointer-tag.h"
#include "src/sandbox/isolate.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Heap;
class PrimitiveHeapObject;
class ExternalPointerSlot;
class IndirectPointerSlot;
class ExposedTrustedObject;
class ObjectVisitor;
class WritableFreeSpace;

V8_OBJECT class HeapObjectLayout {
 public:
  HeapObjectLayout() = delete;

  // [map]: Contains a map which contains the object's reflective
  // information.
  inline Tagged<Map> map() const;
  inline Tagged<Map> map(AcquireLoadTag) const;

  inline void set_map(Tagged<Map> value);
  inline void set_map(Tagged<Map> value, ReleaseStoreTag);

  // This method behaves the same as `set_map` but marks the map transition as
  // safe for the concurrent marker (object layout doesn't change) during
  // verification.
  inline void set_map_safe_transition(Tagged<Map> value, ReleaseStoreTag);

  inline void set_map_safe_transition_no_write_barrier(
      Tagged<Map> value, RelaxedStoreTag = kRelaxedStore);

  // Initialize the map immediately after the object is allocated.
  // Do not use this outside Heap.
  inline void set_map_after_allocation(
      Tagged<Map> value, WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // The no-write-barrier version.  This is OK if the object is white and in
  // new space, or if the value is an immortal immutable object, like the maps
  // of primitive (non-JS) objects like strings, heap numbers etc.
  inline void set_map_no_write_barrier(Tagged<Map> value,
                                       RelaxedStoreTag = kRelaxedStore);

  // Access the map word using acquire load and release store.
  inline void set_map_word_forwarded(Tagged<HeapObject> target_object,
                                     ReleaseStoreTag);

  // Returns the tagged pointer to this HeapObject.
  // TODO(leszeks): Consider bottlenecking this through Tagged<>.
  inline Address ptr() const { return address() + kHeapObjectTag; }

  // Returns the address of this HeapObject.
  inline Address address() const { return reinterpret_cast<Address>(this); }

  // This method exists to help remove GetIsolate/GetHeap from HeapObject, in a
  // way that doesn't require passing Isolate/Heap down huge call chains or to
  // places where it might not be safe to access it.
  inline ReadOnlyRoots GetReadOnlyRoots() const;
  // This is slower, but safe to call during bootstrapping.
  inline ReadOnlyRoots EarlyGetReadOnlyRoots() const;

  // Returns the heap object's size in bytes
  inline int Size() const;

  // Given a heap object's map pointer, returns the heap size in bytes
  // Useful when the map pointer field is used for other purposes.
  // GC internal.
  V8_EXPORT_PRIVATE int SizeFromMap(Tagged<Map> map) const;

  // Return the write barrier mode for this. Callers of this function
  // must be able to present a reference to an DisallowGarbageCollection
  // object as a sign that they are not going to use this function
  // from code that allocates and thus invalidates the returned write
  // barrier mode.
  inline WriteBarrierMode GetWriteBarrierMode(
      const DisallowGarbageCollection& promise);

#ifdef OBJECT_PRINT
  void PrintHeader(std::ostream& os, const char* id);
#endif

 private:
  friend class HeapObject;

  TaggedMember<Map> map_;
} V8_OBJECT_END;

static_assert(sizeof(HeapObjectLayout) == kTaggedSize);

inline bool operator==(const HeapObjectLayout* obj, StrongTaggedBase ptr) {
  return Tagged<HeapObject>(obj) == ptr;
}
inline bool operator==(StrongTaggedBase ptr, const HeapObjectLayout* obj) {
  return ptr == Tagged<HeapObject>(obj);
}
inline bool operator!=(const HeapObjectLayout* obj, StrongTaggedBase ptr) {
  return Tagged<HeapObject>(obj) != ptr;
}
inline bool operator!=(StrongTaggedBase ptr, const HeapObjectLayout* obj) {
  return ptr != Tagged<HeapObject>(obj);
}

template <typename T>
struct ObjectTraits {
  using BodyDescriptor = typename T::BodyDescriptor;
};

// HeapObject is the superclass for all classes describing heap allocated
// objects.
class HeapObject : public TaggedImpl<HeapObjectReferenceType::STRONG, Address> {
 public:
  constexpr HeapObject() = default;

  // [map]: Contains a map which contains the object's reflective
  // information.
  DECL_GETTER(map, Tagged<Map>)
  inline void set_map(Tagged<Map> value);

  // This method behaves the same as `set_map` but marks the map transition as
  // safe for the concurrent marker (object layout doesn't change) during
  // verification.
  inline void set_map_safe_transition(Tagged<Map> value);

  inline ObjectSlot map_slot() const;

  // The no-write-barrier version.  This is OK if the object is white and in
  // new space, or if the value is an immortal immutable object, like the maps
  // of primitive (non-JS) objects like strings, heap numbers etc.
  inline void set_map_no_write_barrier(Tagged<Map> value,
                                       RelaxedStoreTag = kRelaxedStore);
  inline void set_map_no_write_barrier(Tagged<Map> value, ReleaseStoreTag);
  inline void set_map_safe_transition_no_write_barrier(
      Tagged<Map> value, RelaxedStoreTag = kRelaxedStore);
  inline void set_map_safe_transition_no_write_barrier(Tagged<Map> value,
                                                       ReleaseStoreTag);

  // Access the map using acquire load and release store.
  DECL_ACQUIRE_GETTER(map, Tagged<Map>)
  inline void set_map(Tagged<Map> value, ReleaseStoreTag);
  inline void set_map_safe_transition(Tagged<Map> value, ReleaseStoreTag);

  // Compare-and-swaps map word using release store, returns true if the map
  // word was actually swapped.
  inline bool release_compare_and_swap_map_word_forwarded(
      MapWord old_map_word, Tagged<HeapObject> new_target_object);

  // Initialize the map immediately after the object is allocated.
  // Do not use this outside Heap.
  inline void set_map_after_allocation(
      Tagged<Map> value, WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  static inline void SetFillerMap(const WritableFreeSpace& writable_page,
                                  Tagged<Map> value);

  // During garbage collection, the map word of a heap object does not
  // necessarily contain a map pointer.
  DECL_RELAXED_GETTER(map_word, MapWord)
  inline void set_map_word(Tagged<Map> map, RelaxedStoreTag);
  inline void set_map_word_forwarded(Tagged<HeapObject> target_object,
                                     RelaxedStoreTag);

  // Access the map word using acquire load and release store.
  DECL_ACQUIRE_GETTER(map_word, MapWord)
  inline void set_map_word(Tagged<Map> map, ReleaseStoreTag);
  inline void set_map_word_forwarded(Tagged<HeapObject> target_object,
                                     ReleaseStoreTag);

  // This method exists to help remove GetIsolate/GetHeap from HeapObject, in a
  // way that doesn't require passing Isolate/Heap down huge call chains or to
  // places where it might not be safe to access it.
  inline ReadOnlyRoots GetReadOnlyRoots() const;
  // This version is intended to be used for the isolate values produced by
  // i::GetPtrComprCageBase(HeapObject) function which may return nullptr.
  inline ReadOnlyRoots GetReadOnlyRoots(PtrComprCageBase cage_base) const;
  // This is slower, but safe to call during bootstrapping.
  inline ReadOnlyRoots EarlyGetReadOnlyRoots() const;

  // Converts an address to a HeapObject pointer.
  static inline Tagged<HeapObject> FromAddress(Address address) {
    DCHECK_TAG_ALIGNED(address);
    return Tagged<HeapObject>(address + kHeapObjectTag);
  }

  // Returns the address of this HeapObject.
  inline Address address() const { return ptr() - kHeapObjectTag; }

  // Iterates over pointers contained in the object (including the Map).
  // If it's not performance critical iteration use the non-templatized
  // version.
  void Iterate(PtrComprCageBase cage_base, ObjectVisitor* v);

  template <typename ObjectVisitor>
  inline void IterateFast(PtrComprCageBase cage_base, ObjectVisitor* v);

  template <typename ObjectVisitor>
  inline void IterateFast(Tagged<Map> map, ObjectVisitor* v);

  template <typename ObjectVisitor>
  inline void IterateFast(Tagged<Map> map, int object_size, ObjectVisitor* v);

  // Iterates over all pointers contained in the object except the
  // first map pointer.  The object type is given in the first
  // parameter. This function does not access the map pointer in the
  // object, and so is safe to call while the map pointer is modified.
  // If it's not performance critical iteration use the non-templatized
  // version.
  void IterateBody(PtrComprCageBase cage_base, ObjectVisitor* v);
  void IterateBody(Tagged<Map> map, int object_size, ObjectVisitor* v);

  template <typename ObjectVisitor>
  inline void IterateBodyFast(PtrComprCageBase cage_base, ObjectVisitor* v);

  template <typename ObjectVisitor>
  inline void IterateBodyFast(Tagged<Map> map, int object_size,
                              ObjectVisitor* v);

  // Returns the heap object's size in bytes
  DECL_GETTER(Size, int)

  // Given a heap object's map pointer, returns the heap size in bytes
  // Useful when the map pointer field is used for other purposes.
  // GC internal.
  V8_EXPORT_PRIVATE int SizeFromMap(Tagged<Map> map) const;

  template <class T, typename std::enable_if<std::is_arithmetic<T>::value ||
                                                 std::is_enum<T>::value ||
                                                 std::is_pointer<T>::value,
                                             int>::type = 0>
  inline T ReadField(size_t offset) const {
    return ReadMaybeUnalignedValue<T>(field_address(offset));
  }

  template <class T, typename std::enable_if<std::is_arithmetic<T>::value ||
                                                 std::is_enum<T>::value ||
                                                 std::is_pointer<T>::value,
                                             int>::type = 0>
  inline void WriteField(size_t offset, T value) const {
    return WriteMaybeUnalignedValue<T>(field_address(offset), value);
  }

  // Atomically reads a field using relaxed memory ordering. Can only be used
  // with integral types whose size is <= kTaggedSize (to guarantee alignment).
  template <class T,
            typename std::enable_if<(std::is_arithmetic<T>::value ||
                                     std::is_enum<T>::value) &&
                                        !std::is_floating_point<T>::value,
                                    int>::type = 0>
  inline T Relaxed_ReadField(size_t offset) const;

  // Atomically writes a field using relaxed memory ordering. Can only be used
  // with integral types whose size is <= kTaggedSize (to guarantee alignment).
  template <class T,
            typename std::enable_if<(std::is_arithmetic<T>::value ||
                                     std::is_enum<T>::value) &&
                                        !std::is_floating_point<T>::value,
                                    int>::type = 0>
  inline void Relaxed_WriteField(size_t offset, T value);

  // Atomically compares and swaps a field using seq cst memory ordering.
  // Contains the required logic to properly handle number comparison.
  template <typename CompareAndSwapImpl>
  static Tagged<Object> SeqCst_CompareAndSwapField(
      Tagged<Object> expected_value, Tagged<Object> new_value,
      CompareAndSwapImpl compare_and_swap_impl);

  //
  // SandboxedPointer_t field accessors.
  //
  inline Address ReadSandboxedPointerField(size_t offset,
                                           PtrComprCageBase cage_base) const;
  inline void WriteSandboxedPointerField(size_t offset,
                                         PtrComprCageBase cage_base,
                                         Address value);
  inline void WriteSandboxedPointerField(size_t offset, Isolate* isolate,
                                         Address value);

  //
  // BoundedSize field accessors.
  //
  inline size_t ReadBoundedSizeField(size_t offset) const;
  inline void WriteBoundedSizeField(size_t offset, size_t value);

  //
  // ExternalPointer_t field accessors.
  //
  template <ExternalPointerTag tag>
  inline void InitExternalPointerField(size_t offset, IsolateForSandbox isolate,
                                       Address value);
  template <ExternalPointerTag tag>
  inline Address ReadExternalPointerField(size_t offset,
                                          IsolateForSandbox isolate) const;
  // Similar to `ReadExternalPointerField()` but uses the CppHeapPointerTable.
  template <CppHeapPointerTag lower_bound, CppHeapPointerTag upper_bound>
  inline Address ReadCppHeapPointerField(
      size_t offset, IsolateForPointerCompression isolate) const;
  inline Address ReadCppHeapPointerField(
      size_t offset, IsolateForPointerCompression isolate,
      CppHeapPointerTagRange tag_range) const;
  template <ExternalPointerTag tag>
  inline void WriteExternalPointerField(size_t offset,
                                        IsolateForSandbox isolate,
                                        Address value);

  template <ExternalPointerTag tag>
  inline void WriteLazilyInitializedExternalPointerField(
      size_t offset, IsolateForSandbox isolate, Address value);

  inline void SetupLazilyInitializedExternalPointerField(size_t offset);
  inline void SetupLazilyInitializedCppHeapPointerField(size_t offset);

  template <CppHeapPointerTag tag>
  inline void WriteLazilyInitializedCppHeapPointerField(
      size_t offset, IsolateForPointerCompression isolate, Address value);
  inline void WriteLazilyInitializedCppHeapPointerField(
      size_t offset, IsolateForPointerCompression isolate, Address value,
      CppHeapPointerTag tag);

  //
  // Indirect pointers.
  //
  // These are only available when the sandbox is enabled, in which case they
  // are the under-the-hood implementation of trusted pointers.
  inline void InitSelfIndirectPointerField(size_t offset,
                                           IsolateForSandbox isolate);

  // Trusted pointers.
  //
  // A pointer to a trusted object. When the sandbox is enabled, these are
  // indirect pointers using the the TrustedPointerTable (TPT). When the sandbox
  // is disabled, they are regular tagged pointers. They must always point to an
  // ExposedTrustedObject as (only) these objects can be referenced through the
  // trusted pointer table.
  template <IndirectPointerTag tag>
  inline Tagged<ExposedTrustedObject> ReadTrustedPointerField(
      size_t offset, IsolateForSandbox isolate) const;
  template <IndirectPointerTag tag>
  inline Tagged<ExposedTrustedObject> ReadTrustedPointerField(
      size_t offset, IsolateForSandbox isolate, AcquireLoadTag) const;
  // Like ReadTrustedPointerField, but if the field is cleared, this will
  // return Smi::zero().
  template <IndirectPointerTag tag>
  inline Tagged<Object> ReadMaybeEmptyTrustedPointerField(
      size_t offset, IsolateForSandbox isolate, AcquireLoadTag) const;

  template <IndirectPointerTag tag>
  inline void WriteTrustedPointerField(size_t offset,
                                       Tagged<ExposedTrustedObject> value);

  // Trusted pointer fields can be cleared/empty, in which case they no longer
  // point to any object. When the sandbox is enabled, this will set the fields
  // indirect pointer handle to the null handle (referencing the zeroth entry
  // in the TrustedPointerTable which just contains nullptr). When the sandbox
  // is disabled, this will set the field to Smi::zero().
  inline bool IsTrustedPointerFieldEmpty(size_t offset) const;
  inline void ClearTrustedPointerField(size_t offest);
  inline void ClearTrustedPointerField(size_t offest, ReleaseStoreTag);

  // Code pointers.
  //
  // These are special versions of trusted pointers that always point to Code
  // objects. When the sandbox is enabled, they are indirect pointers using the
  // code pointer table (CPT) instead of the TrustedPointerTable. When the
  // sandbox is disabled, they are regular tagged pointers.
  inline Tagged<Code> ReadCodePointerField(size_t offset,
                                           IsolateForSandbox isolate) const;
  inline void WriteCodePointerField(size_t offset, Tagged<Code> value);

  inline bool IsCodePointerFieldEmpty(size_t offset) const;
  inline void ClearCodePointerField(size_t offest);

  inline Address ReadCodeEntrypointViaCodePointerField(
      size_t offset, CodeEntrypointTag tag) const;
  inline void WriteCodeEntrypointViaCodePointerField(size_t offset,
                                                     Address value,
                                                     CodeEntrypointTag tag);

  // JSDispatchHandles.
  //
  // These are references to entries in the JSDispatchTable, which contain the
  // current code for a JSFunction.
  inline void InitJSDispatchHandleField(size_t offset,
                                        IsolateForSandbox isolate,
                                        uint16_t parameter_count);

  // Returns the field at offset in obj, as a read/write Object reference.
  // Does no checking, and is safe to use during GC, while maps are invalid.
  // Does not invoke write barrier, so should only be assigned to
  // during marking GC.
  inline ObjectSlot RawField(int byte_offset) const;
  inline MaybeObjectSlot RawMaybeWeakField(int byte_offset) const;
  inline InstructionStreamSlot RawInstructionStreamField(int byte_offset) const;
  inline ExternalPointerSlot RawExternalPointerField(
      int byte_offset, ExternalPointerTag tag) const;
  inline CppHeapPointerSlot RawCppHeapPointerField(int byte_offset) const;
  inline IndirectPointerSlot RawIndirectPointerField(
      int byte_offset, IndirectPointerTag tag) const;

  // Return the write barrier mode for this. Callers of this function
  // must be able to present a reference to an DisallowGarbageCollection
  // object as a sign that they are not going to use this function
  // from code that allocates and thus invalidates the returned write
  // barrier mode.
  inline WriteBarrierMode GetWriteBarrierMode(
      const DisallowGarbageCollection& promise);

  // Dispatched behavior.
  void HeapObjectShortPrint(std::ostream& os);
  void Print();
  static void Print(Tagged<Object> obj);
  static void Print(Tagged<Object> obj, std::ostream& os);
#ifdef OBJECT_PRINT
  void PrintHeader(std::ostream& os, const char* id);
#endif
  DECL_PRINTER(HeapObject)
  EXPORT_DECL_VERIFIER(HeapObject)
#ifdef VERIFY_HEAP
  inline void VerifyObjectField(Isolate* isolate, int offset);
  inline void VerifySmiField(int offset);
  inline void VerifyMaybeObjectField(Isolate* isolate, int offset);

  // Verify a pointer is a valid HeapObject pointer that points to object
  // areas in the heap.
  static void VerifyHeapPointer(Isolate* isolate, Tagged<Object> p);
  static void VerifyCodePointer(Isolate* isolate, Tagged<Object> p);
#endif

  static inline AllocationAlignment RequiredAlignment(Tagged<Map> map);
  bool inline CheckRequiredAlignment(PtrComprCageBase cage_base) const;

  // Whether the object needs rehashing. That is the case if the object's
  // content depends on v8_flags.hash_seed. When the object is deserialized into
  // a heap with a different hash seed, these objects need to adapt.
  bool NeedsRehashing(InstanceType instance_type) const;
  bool NeedsRehashing(PtrComprCageBase cage_base) const;

  // Rehashing support is not implemented for all objects that need rehashing.
  // With objects that need rehashing but cannot be rehashed, rehashing has to
  // be disabled.
  bool CanBeRehashed(PtrComprCageBase cage_base) const;

  // Rehash the object based on the layout inferred from its map.
  template <typename IsolateT>
  void RehashBasedOnMap(IsolateT* isolate);

  // Layout description.
  static constexpr int kMapOffset = offsetof(HeapObjectLayout, map_);
  static constexpr int kHeaderSize = sizeof(HeapObjectLayout);

  static_assert(kMapOffset == Internals::kHeapObjectMapOffset);

  using MapField = TaggedField<MapWord, HeapObject::kMapOffset>;

  inline Address GetFieldAddress(int field_offset) const;

  HeapObject* operator->() { return this; }
  const HeapObject* operator->() const { return this; }

 protected:
  struct SkipTypeCheckTag {};
  friend class Tagged<HeapObject>;
  explicit V8_INLINE constexpr HeapObject(Address ptr,
                                          HeapObject::SkipTypeCheckTag)
      : TaggedImpl(ptr) {}
  explicit inline HeapObject(Address ptr);

  // Static overwrites of TaggedImpl's IsSmi/IsHeapObject, to avoid conflicts
  // with IsSmi(Tagged<HeapObject>) inside HeapObject subclasses' methods.
  template <typename T>
  static bool IsSmi(T obj);
  template <typename T>
  static bool IsHeapObject(T obj);

  inline Address field_address(size_t offset) const {
    return ptr() + offset - kHeapObjectTag;
  }

 private:
  enum class VerificationMode {
    kSafeMapTransition,
    kPotentialLayoutChange,
  };

  enum class EmitWriteBarrier {
    kYes,
    kNo,
  };

  template <EmitWriteBarrier emit_write_barrier, typename MemoryOrder>
  V8_INLINE void set_map(Tagged<Map> value, MemoryOrder order,
                         VerificationMode mode);
};

inline HeapObject::HeapObject(Address ptr) : TaggedImpl(ptr) {
  IsHeapObject(*this);
}

template <typename T>
// static
bool HeapObject::IsSmi(T obj) {
  return i::IsSmi(obj);
}
template <typename T>
// static
bool HeapObject::IsHeapObject(T obj) {
  return i::IsHeapObject(obj);
}

// Define Tagged<HeapObject> now that HeapObject exists.
constexpr HeapObject Tagged<HeapObject>::operator*() const {
  return ToRawPtr();
}
constexpr detail::TaggedOperatorArrowRef<HeapObject>
Tagged<HeapObject>::operator->() const {
  return detail::TaggedOperatorArrowRef<HeapObject>{ToRawPtr()};
}
constexpr HeapObject Tagged<HeapObject>::ToRawPtr() const {
  return HeapObject(this->ptr(), HeapObject::SkipTypeCheckTag{});
}

// Overload Is* predicates for HeapObject.
#define IS_TYPE_FUNCTION_DECL(Type)                                            \
  V8_INLINE bool Is##Type(Tagged<HeapObject> obj);                             \
  V8_INLINE bool Is##Type(Tagged<HeapObject> obj, PtrComprCageBase cage_base); \
  V8_INLINE bool Is##Type(HeapObject obj);                                     \
  V8_INLINE bool Is##Type(HeapObject obj, PtrComprCageBase cage_base);         \
  V8_INLINE bool Is##Type(const HeapObjectLayout* obj);                        \
  V8_INLINE bool Is##Type(const HeapObjectLayout* obj,                         \
                          PtrComprCageBase cage_base);
HEAP_OBJECT_TYPE_LIST(IS_TYPE_FUNCTION_DECL)
IS_TYPE_FUNCTION_DECL(HashTableBase)
IS_TYPE_FUNCTION_DECL(SmallOrderedHashTable)
IS_TYPE_FUNCTION_DECL(PropertyDictionary)
#undef IS_TYPE_FUNCTION_DECL

// Most calls to Is<Oddball> should go via the Tagged<Object> overloads, withst
// an Isolate/LocalIsolate/ReadOnlyRoots parameter.
#define IS_TYPE_FUNCTION_DECL(Type, Value, _)                             \
  V8_INLINE bool Is##Type(Tagged<HeapObject> obj);                        \
  V8_INLINE bool Is##Type(HeapObject obj);                                \
  V8_INLINE bool Is##Type(const HeapObjectLayout* obj, Isolate* isolate); \
  V8_INLINE bool Is##Type(const HeapObjectLayout* obj);
ODDBALL_LIST(IS_TYPE_FUNCTION_DECL)
HOLE_LIST(IS_TYPE_FUNCTION_DECL)
IS_TYPE_FUNCTION_DECL(NullOrUndefined, , /* unused */)
#undef IS_TYPE_FUNCTION_DECL

#define DECL_STRUCT_PREDICATE(NAME, Name, name)                                \
  V8_INLINE bool Is##Name(Tagged<HeapObject> obj);                             \
  V8_INLINE bool Is##Name(Tagged<HeapObject> obj, PtrComprCageBase cage_base); \
  V8_INLINE bool Is##Name(HeapObject obj);                                     \
  V8_INLINE bool Is##Name(HeapObject obj, PtrComprCageBase cage_base);         \
  V8_INLINE bool Is##Name(const HeapObjectLayout* obj);                        \
  V8_INLINE bool Is##Name(const HeapObjectLayout* obj,                         \
                          PtrComprCageBase cage_base);
STRUCT_LIST(DECL_STRUCT_PREDICATE)
#undef DECL_STRUCT_PREDICATE

// Whether the object is in the RO heap and the RO heap is shared, or in the
// writable shared heap.
V8_INLINE bool InAnySharedSpace(Tagged<HeapObject> obj);
V8_INLINE bool InWritableSharedSpace(Tagged<HeapObject> obj);
V8_INLINE bool InReadOnlySpace(Tagged<HeapObject> obj);
// Whether the object is located outside of the sandbox or in read-only
// space. Currently only needed due to Code objects. Once they are fully
// migrated into trusted space, this can be replaced by !InsideSandbox().
static_assert(!kAllCodeObjectsLiveInTrustedSpace);
V8_INLINE bool OutsideSandboxOrInReadonlySpace(Tagged<HeapObject> obj);

// Returns true if obj is guaranteed to be a read-only object or a specific
// (small) Smi. If the method returns false, we need more checks for RO space
// objects or Smis. This can be used for a fast RO space/Smi check which are
// objects for e.g. GC than can be exlucded for processing.
V8_INLINE constexpr bool FastInReadOnlySpaceOrSmallSmi(Tagged_t obj);

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HEAP_OBJECT_H_
