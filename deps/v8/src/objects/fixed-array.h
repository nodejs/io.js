// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FIXED_ARRAY_H_
#define V8_OBJECTS_FIXED_ARRAY_H_

#include <optional>

#include "src/common/globals.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type.h"
#include "src/objects/maybe-object.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"
#include "src/objects/tagged.h"
#include "src/objects/trusted-object.h"
#include "src/roots/roots.h"
#include "src/utils/memcopy.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/objects/fixed-array-tq.inc"

// Derived: must have a Smi slot at kCapacityOffset.
template <class Derived, class ShapeT, class Super = HeapObject>
class TaggedArrayBase : public Super {
  static_assert(std::is_base_of<HeapObject, Super>::value);
  OBJECT_CONSTRUCTORS(TaggedArrayBase, Super);

  using ElementT = typename ShapeT::ElementT;
  static_assert(ShapeT::kElementSize == kTaggedSize);
  static_assert(is_subtype_v<ElementT, MaybeObject>);

  using ElementFieldT =
      TaggedField<ElementT, 0, typename ShapeT::CompressionScheme>;

  template <typename ElementT>
  static constexpr bool kSupportsSmiElements =
      std::is_convertible_v<Smi, ElementT>;

  static constexpr WriteBarrierMode kDefaultMode =
      std::is_same_v<ElementT, Smi> ? SKIP_WRITE_BARRIER : UPDATE_WRITE_BARRIER;

 public:
  static constexpr bool kElementsAreMaybeObject = is_maybe_weak_v<ElementT>;

 private:
  using SlotType =
      std::conditional_t<kElementsAreMaybeObject, MaybeObjectSlot, ObjectSlot>;

 public:
  using Shape = ShapeT;

  inline int capacity() const;
  inline int capacity(AcquireLoadTag) const;
  inline void set_capacity(int value);
  inline void set_capacity(int value, ReleaseStoreTag);

  // For most arraylike objects, length equals capacity. Provide these
  // convenience accessors:
  template <typename T = Shape,
            typename = std::enable_if<T::kLengthEqualsCapacity>>
  inline int length() const;
  template <typename T = Shape,
            typename = std::enable_if<T::kLengthEqualsCapacity>>
  inline int length(AcquireLoadTag tag) const;
  template <typename T = Shape,
            typename = std::enable_if<T::kLengthEqualsCapacity>>
  inline void set_length(int value);
  template <typename T = Shape,
            typename = std::enable_if<T::kLengthEqualsCapacity>>
  inline void set_length(int value, ReleaseStoreTag tag);

  inline Tagged<ElementT> get(int index) const;
  inline Tagged<ElementT> get(int index, RelaxedLoadTag) const;
  inline Tagged<ElementT> get(int index, AcquireLoadTag) const;
  inline Tagged<ElementT> get(int index, SeqCstAccessTag) const;

  inline void set(int index, Tagged<ElementT> value,
                  WriteBarrierMode mode = kDefaultMode);
  template <typename T = ElementT,
            typename = std::enable_if<kSupportsSmiElements<T>>>
  inline void set(int index, Tagged<Smi> value);
  inline void set(int index, Tagged<ElementT> value, RelaxedStoreTag,
                  WriteBarrierMode mode = kDefaultMode);
  template <typename T = ElementT,
            typename = std::enable_if<kSupportsSmiElements<T>>>
  inline void set(int index, Tagged<Smi> value, RelaxedStoreTag);
  inline void set(int index, Tagged<ElementT> value, ReleaseStoreTag,
                  WriteBarrierMode mode = kDefaultMode);
  template <typename T = ElementT,
            typename = std::enable_if<kSupportsSmiElements<T>>>
  inline void set(int index, Tagged<Smi> value, ReleaseStoreTag);
  inline void set(int index, Tagged<ElementT> value, SeqCstAccessTag,
                  WriteBarrierMode mode = kDefaultMode);
  template <typename T = ElementT,
            typename = std::enable_if<kSupportsSmiElements<T>>>
  inline void set(int index, Tagged<Smi> value, SeqCstAccessTag);

  inline Tagged<ElementT> swap(int index, Tagged<ElementT> value,
                               SeqCstAccessTag,
                               WriteBarrierMode mode = kDefaultMode);
  inline Tagged<ElementT> compare_and_swap(
      int index, Tagged<ElementT> expected, Tagged<ElementT> value,
      SeqCstAccessTag, WriteBarrierMode mode = kDefaultMode);

  // Move vs. Copy behaves like memmove vs. memcpy: for Move, the memory
  // regions may overlap, for Copy they must not overlap.
  inline static void MoveElements(Isolate* isolate, Tagged<Derived> dst,
                                  int dst_index, Tagged<Derived> src,
                                  int src_index, int len,
                                  WriteBarrierMode mode = kDefaultMode);
  inline static void CopyElements(Isolate* isolate, Tagged<Derived> dst,
                                  int dst_index, Tagged<Derived> src,
                                  int src_index, int len,
                                  WriteBarrierMode mode = kDefaultMode);

  // Right-trim the array.
  // Invariant: 0 < new_length <= length()
  inline void RightTrim(Isolate* isolate, int new_capacity);

  inline int AllocatedSize() const;
  static inline constexpr int SizeFor(int capacity) {
    return Shape::kHeaderSize + capacity * Shape::kElementSize;
  }
  static inline constexpr int OffsetOfElementAt(int index) {
    return SizeFor(index);
  }

  // Gives access to raw memory which stores the array's data.
  inline SlotType RawFieldOfFirstElement() const;
  inline SlotType RawFieldOfElementAt(int index) const;

  // Maximal allowed capacity, in number of elements. Chosen s.t. the size fits
  // into a Smi which is necessary for being able to create a free space
  // filler.
  // TODO(jgruber): The kMaxCapacity could be larger (`(Smi::kMaxValue -
  // Shape::kHeaderSize) / Shape::kElementSize`), but our tests rely on a
  // smaller maximum to avoid timeouts.
  static constexpr int kMaxCapacity =
      128 * MB - Super::kHeaderSize / Shape::kElementSize;
  static_assert(Smi::IsValid(SizeFor(kMaxCapacity)));

  // Maximally allowed length for regular (non large object space) object.
  static constexpr int kMaxRegularCapacity =
      (kMaxRegularHeapObjectSize - Shape::kHeaderSize) / Shape::kElementSize;
  static_assert(kMaxRegularCapacity < kMaxCapacity);

  // Object layout.
  static constexpr int kCapacityOffset = Shape::kCapacityOffset;
  static constexpr int kHeaderSize = Shape::kHeaderSize;
  static constexpr int kObjectsOffset = kHeaderSize;

 protected:
  template <class IsolateT>
  static Handle<Derived> Allocate(
      IsolateT* isolate, int capacity,
      std::optional<DisallowGarbageCollection>* no_gc_out,
      AllocationType allocation = AllocationType::kYoung);

  static constexpr int NewCapacityForIndex(int index, int old_capacity);

  inline void ConditionalWriteBarrier(Tagged<HeapObject> object, int offset,
                                      Tagged<ElementT> value,
                                      WriteBarrierMode mode);

  inline bool IsInBounds(int index) const;
  inline bool IsCowArray() const;
};

class TaggedArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  using ElementT = Object;
  using CompressionScheme = V8HeapCompressionScheme;
  static constexpr RootIndex kMapRootIndex = RootIndex::kFixedArrayMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// FixedArray describes fixed-sized arrays with element type Object.
class FixedArray : public TaggedArrayBase<FixedArray, TaggedArrayShape> {
  using Super = TaggedArrayBase<FixedArray, TaggedArrayShape>;
  OBJECT_CONSTRUCTORS(FixedArray, Super);

 public:
  template <class IsolateT>
  static inline Handle<FixedArray> New(
      IsolateT* isolate, int capacity,
      AllocationType allocation = AllocationType::kYoung);

  using Super::CopyElements;
  using Super::MoveElements;

  // TODO(jgruber): Only needed for FixedArrays used as JSObject elements.
  inline void MoveElements(Isolate* isolate, int dst_index, int src_index,
                           int len, WriteBarrierMode mode);
  inline void CopyElements(Isolate* isolate, int dst_index,
                           Tagged<FixedArray> src, int src_index, int len,
                           WriteBarrierMode mode);

  // Return a grown copy if the index is bigger than the array's length.
  V8_EXPORT_PRIVATE static Handle<FixedArray> SetAndGrow(
      Isolate* isolate, Handle<FixedArray> array, int index,
      DirectHandle<Object> value);

  // Right-trim the array.
  // Invariant: 0 < new_length <= length()
  V8_EXPORT_PRIVATE void RightTrim(Isolate* isolate, int new_capacity);
  // Right-trims the array, and canonicalizes length 0 to empty_fixed_array.
  static Handle<FixedArray> RightTrimOrEmpty(Isolate* isolate,
                                             Handle<FixedArray> array,
                                             int new_length);

  // TODO(jgruber): Only needed for FixedArrays used as JSObject elements.
  inline void FillWithHoles(int from, int to);

  // For compatibility with FixedDoubleArray:
  // TODO(jgruber): Only needed for FixedArrays used as JSObject elements.
  inline bool is_the_hole(Isolate* isolate, int index);
  inline void set_the_hole(Isolate* isolate, int index);
  inline void set_the_hole(ReadOnlyRoots ro_roots, int index);

  static_assert(kHeaderSize == Internals::kFixedArrayHeaderSize);

  DECL_PRINTER(FixedArray)
  DECL_VERIFIER(FixedArray)

  class BodyDescriptor;

  static constexpr int kLengthOffset = Shape::kCapacityOffset;
  static constexpr int kMaxLength = FixedArray::kMaxCapacity;
  static constexpr int kMaxRegularLength = FixedArray::kMaxRegularCapacity;

 private:
  inline static Handle<FixedArray> Resize(
      Isolate* isolate, DirectHandle<FixedArray> xs, int new_capacity,
      AllocationType allocation = AllocationType::kYoung,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
};

class TrustedArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  using ElementT = Object;
  using CompressionScheme = V8HeapCompressionScheme;
  static constexpr RootIndex kMapRootIndex = RootIndex::kTrustedFixedArrayMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(TrustedObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// A FixedArray in trusted space and with a unique instance type.
//
// Note: while the array itself is trusted, it contains tagged pointers into
// the main pointer compression heap and therefore to _untrusted_ objects.
// If you are storing references to other trusted object (i.e. protected
// pointers), use ProtectedFixedArray.
class TrustedFixedArray
    : public TaggedArrayBase<TrustedFixedArray, TrustedArrayShape,
                             TrustedObject> {
  using Super =
      TaggedArrayBase<TrustedFixedArray, TrustedArrayShape, TrustedObject>;
  OBJECT_CONSTRUCTORS(TrustedFixedArray, Super);

 public:
  template <class IsolateT>
  static inline Handle<TrustedFixedArray> New(IsolateT* isolate, int capacity);

  DECL_PRINTER(TrustedFixedArray)
  DECL_VERIFIER(TrustedFixedArray)

  class BodyDescriptor;

  static constexpr int kLengthOffset =
      TrustedFixedArray::Shape::kCapacityOffset;
  static constexpr int kMaxLength = TrustedFixedArray::kMaxCapacity;
  static constexpr int kMaxRegularLength =
      TrustedFixedArray::kMaxRegularCapacity;
};

class ProtectedArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  // Elements are of type TrustedObject or Smi, so we must declare it as Object
  // here.
  using ElementT = Object;
  using CompressionScheme = TrustedSpaceCompressionScheme;
  static constexpr RootIndex kMapRootIndex = RootIndex::kProtectedFixedArrayMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(TrustedObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// A FixedArray in trusted space, holding protected pointers (to other trusted
// objects). If you want to store JS-heap references, use TrustedFixedArray.
// ProtectedFixedArray has a unique instance type.
class ProtectedFixedArray
    : public TaggedArrayBase<ProtectedFixedArray, ProtectedArrayShape,
                             TrustedObject> {
  using Super =
      TaggedArrayBase<ProtectedFixedArray, ProtectedArrayShape, TrustedObject>;
  OBJECT_CONSTRUCTORS(ProtectedFixedArray, Super);

 public:
  // Allocate a new ProtectedFixedArray of the given capacity, initialized with
  // Smi::zero().
  template <class IsolateT>
  static inline Handle<ProtectedFixedArray> New(IsolateT* isolate,
                                                int capacity);

  DECL_PRINTER(ProtectedFixedArray)
  DECL_VERIFIER(ProtectedFixedArray)

  class BodyDescriptor;

  static constexpr int kLengthOffset =
      ProtectedFixedArray::Shape::kCapacityOffset;
  static constexpr int kMaxLength = ProtectedFixedArray::kMaxCapacity;
  static constexpr int kMaxRegularLength =
      ProtectedFixedArray::kMaxRegularCapacity;
};

// FixedArray alias added only because of IsFixedArrayExact() predicate, which
// checks for the exact instance type FIXED_ARRAY_TYPE instead of a range
// check: [FIRST_FIXED_ARRAY_TYPE, LAST_FIXED_ARRAY_TYPE].
class FixedArrayExact final : public FixedArray {};

// Common superclass for FixedArrays that allow implementations to share common
// accessors and some code paths. Note that due to single-inheritance
// restrictions, it is not part of the actual type hierarchy. Instead, we slot
// it in with manual is_subtype specializations in tagged.h.
// TODO(jgruber): This class is really specific to FixedArrays used as
// elements backing stores and should not be part of the common FixedArray
// hierarchy.
class FixedArrayBase : public HeapObject {
  OBJECT_CONSTRUCTORS(FixedArrayBase, HeapObject);

 public:
  // TODO(jgruber): Remove these `length` accessors once all subclasses have
  // been ported to use TaggedArrayBase or similar.
  inline int length() const;
  inline int length(AcquireLoadTag tag) const;
  inline void set_length(int value);
  inline void set_length(int value, ReleaseStoreTag tag);
  static constexpr int kLengthOffset = HeapObject::kHeaderSize;
  static constexpr int kHeaderSize = kLengthOffset + kTaggedSize;
  static constexpr int kMaxLength = FixedArray::kMaxCapacity;
  static constexpr int kMaxRegularLength = FixedArray::kMaxRegularCapacity;

  static int GetMaxLengthForNewSpaceAllocation(ElementsKind kind);

  V8_EXPORT_PRIVATE bool IsCowArray() const;

  // Maximal allowed size, in bytes, of a single FixedArrayBase. Prevents
  // overflowing size computations, as well as extreme memory consumption.
  static constexpr int kMaxSize = 128 * kTaggedSize * MB;
  static_assert(Smi::IsValid(kMaxSize));

  DECL_VERIFIER(FixedArrayBase)
};

// Derived: must have a Smi slot at kCapacityOffset.
template <class Derived, class ShapeT, class Super = HeapObject>
class PrimitiveArrayBase : public Super {
  static_assert(std::is_base_of<HeapObject, Super>::value);
  OBJECT_CONSTRUCTORS(PrimitiveArrayBase, Super);

  using ElementT = typename ShapeT::ElementT;
  static_assert(!is_subtype_v<ElementT, Object>);

 public:
  using Shape = ShapeT;
  static constexpr bool kElementsAreMaybeObject = false;

  inline int length() const;
  inline int length(AcquireLoadTag) const;
  inline void set_length(int value);
  inline void set_length(int value, ReleaseStoreTag);

  // For compatibility with TaggedArrayBase:
  inline int capacity() const;
  inline int capacity(AcquireLoadTag) const;
  inline void set_capacity(int value);
  inline void set_capacity(int value, ReleaseStoreTag);

  inline ElementT get(int index) const;
  inline void set(int index, ElementT value);

  inline int AllocatedSize() const;
  static inline constexpr int SizeFor(int length) {
    return OBJECT_POINTER_ALIGN(OffsetOfElementAt(length));
  }
  static inline constexpr int OffsetOfElementAt(int index) {
    return Shape::kHeaderSize + index * Shape::kElementSize;
  }

  // Gives access to raw memory which stores the array's data.
  // Note that on 32-bit archs and on 64-bit platforms with pointer compression
  // the pointers to 8-byte size elements are not guaranteed to be aligned.
  inline ElementT* AddressOfElementAt(int index) const;
  inline ElementT* begin() const;
  inline ElementT* end() const;
  inline int DataSize() const;

  static inline Tagged<Derived> FromAddressOfFirstElement(Address address);

  // Maximal allowed length, in number of elements. Chosen s.t. the size fits
  // into a Smi which is necessary for being able to create a free space
  // filler.
  // TODO(jgruber): The kMaxLength could be larger (`(Smi::kMaxValue -
  // Shape::kHeaderSize) / Shape::kElementSize`), but our tests rely on a
  // smaller maximum to avoid timeouts.
  static constexpr int kMaxLength =
      (FixedArrayBase::kMaxSize - Super::kHeaderSize) / Shape::kElementSize;
  static_assert(Smi::IsValid(SizeFor(kMaxLength)));

  // Maximally allowed length for regular (non large object space) object.
  static constexpr int kMaxRegularLength =
      (kMaxRegularHeapObjectSize - Shape::kHeaderSize) / Shape::kElementSize;
  static_assert(kMaxRegularLength < kMaxLength);

  // Object layout.
  static constexpr int kLengthOffset = Shape::kLengthOffset;
  static constexpr int kHeaderSize = Shape::kHeaderSize;

 protected:
  template <class IsolateT>
  static Handle<Derived> Allocate(
      IsolateT* isolate, int length,
      std::optional<DisallowGarbageCollection>* no_gc_out,
      AllocationType allocation = AllocationType::kYoung);

  inline bool IsInBounds(int index) const;
};

class FixedDoubleArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kDoubleSize;
  using ElementT = double;
  static constexpr RootIndex kMapRootIndex = RootIndex::kFixedDoubleArrayMap;

#define FIELD_LIST(V)           \
  V(kLengthOffset, kTaggedSize) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// FixedDoubleArray describes fixed-sized arrays with element type double.
class FixedDoubleArray
    : public PrimitiveArrayBase<FixedDoubleArray, FixedDoubleArrayShape> {
  using Super = PrimitiveArrayBase<FixedDoubleArray, FixedDoubleArrayShape>;
  OBJECT_CONSTRUCTORS(FixedDoubleArray, Super);

 public:
  // Note this returns FixedArrayBase due to canonicalization to
  // empty_fixed_array.
  template <class IsolateT>
  static inline Handle<FixedArrayBase> New(
      IsolateT* isolate, int capacity,
      AllocationType allocation = AllocationType::kYoung);

  // Setter and getter for elements.
  inline double get_scalar(int index);
  inline uint64_t get_representation(int index);
  static inline Handle<Object> get(Tagged<FixedDoubleArray> array, int index,
                                   Isolate* isolate);
  inline void set(int index, double value);

  inline void set_the_hole(Isolate* isolate, int index);
  inline void set_the_hole(int index);
  inline bool is_the_hole(Isolate* isolate, int index);
  inline bool is_the_hole(int index);

  inline void MoveElements(Isolate* isolate, int dst_index, int src_index,
                           int len, WriteBarrierMode /* unused */);

  inline void FillWithHoles(int from, int to);

  DECL_PRINTER(FixedDoubleArray)
  DECL_VERIFIER(FixedDoubleArray)

  class BodyDescriptor;

  static constexpr int kFloatsOffset = Shape::kHeaderSize;
};

class WeakFixedArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  using ElementT = MaybeObject;
  using CompressionScheme = V8HeapCompressionScheme;
  static constexpr RootIndex kMapRootIndex = RootIndex::kWeakFixedArrayMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// WeakFixedArray describes fixed-sized arrays with element type
// Tagged<MaybeObject>.
class WeakFixedArray
    : public TaggedArrayBase<WeakFixedArray, WeakFixedArrayShape> {
  using Super = TaggedArrayBase<WeakFixedArray, WeakFixedArrayShape>;
  OBJECT_CONSTRUCTORS(WeakFixedArray, Super);

 public:
  template <class IsolateT>
  static inline Handle<WeakFixedArray> New(
      IsolateT* isolate, int capacity,
      AllocationType allocation = AllocationType::kYoung,
      MaybeHandle<Object> initial_value = {});

  DECL_PRINTER(WeakFixedArray)
  DECL_VERIFIER(WeakFixedArray)

  class BodyDescriptor;

  static constexpr int kLengthOffset = Shape::kCapacityOffset;
};

class TrustedWeakFixedArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  using ElementT = MaybeObject;
  using CompressionScheme = V8HeapCompressionScheme;
  static constexpr RootIndex kMapRootIndex =
      RootIndex::kTrustedWeakFixedArrayMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// A WeakFixedArray in trusted space and with a unique instance type.
class TrustedWeakFixedArray
    : public TaggedArrayBase<TrustedWeakFixedArray,
                             TrustedWeakFixedArrayShape> {
  using Super =
      TaggedArrayBase<TrustedWeakFixedArray, TrustedWeakFixedArrayShape>;
  OBJECT_CONSTRUCTORS(TrustedWeakFixedArray, Super);

 public:
  template <class IsolateT>
  static inline Handle<TrustedWeakFixedArray> New(IsolateT* isolate,
                                                  int capacity);

  DECL_PRINTER(TrustedWeakFixedArray)
  DECL_VERIFIER(TrustedWeakFixedArray)

  class BodyDescriptor;

  static constexpr int kLengthOffset = Shape::kCapacityOffset;
};

// WeakArrayList is like a WeakFixedArray with static convenience methods for
// adding more elements. length() returns the number of elements in the list and
// capacity() returns the allocated size. The number of elements is stored at
// kLengthOffset and is updated with every insertion. The array grows
// dynamically with O(1) amortized insertion.
class WeakArrayList
    : public TorqueGeneratedWeakArrayList<WeakArrayList, HeapObject> {
 public:
  NEVER_READ_ONLY_SPACE
  DECL_PRINTER(WeakArrayList)

  V8_EXPORT_PRIVATE static Handle<WeakArrayList> AddToEnd(
      Isolate* isolate, Handle<WeakArrayList> array, MaybeObjectHandle value);

  // A version that adds to elements. This ensures that the elements are
  // inserted atomically w.r.t GC.
  V8_EXPORT_PRIVATE static Handle<WeakArrayList> AddToEnd(
      Isolate* isolate, Handle<WeakArrayList> array, MaybeObjectHandle value1,
      Tagged<Smi> value2);

  // Appends an element to the array and possibly compacts and shrinks live weak
  // references to the start of the collection. Only use this method when
  // indices to elements can change.
  static V8_WARN_UNUSED_RESULT Handle<WeakArrayList> Append(
      Isolate* isolate, Handle<WeakArrayList> array, MaybeObjectHandle value,
      AllocationType allocation = AllocationType::kYoung);

  // Compact weak references to the beginning of the array.
  V8_EXPORT_PRIVATE void Compact(Isolate* isolate);

  inline Tagged<MaybeObject> Get(int index) const;
  inline Tagged<MaybeObject> Get(PtrComprCageBase cage_base, int index) const;
  // TODO(jgruber): Remove this once it's no longer needed for compatibility
  // with WeakFixedArray.
  inline Tagged<MaybeObject> get(int index) const;

  // Set the element at index to obj. The underlying array must be large enough.
  // If you need to grow the WeakArrayList, use the static AddToEnd() method
  // instead.
  inline void Set(int index, Tagged<MaybeObject> value,
                  WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline void Set(int index, Tagged<Smi> value);

  static constexpr int SizeForCapacity(int capacity) {
    return SizeFor(capacity);
  }

  static constexpr int CapacityForLength(int length) {
    return length + std::max(length / 2, 2);
  }

  // Gives access to raw memory which stores the array's data.
  inline MaybeObjectSlot data_start();

  inline void CopyElements(Isolate* isolate, int dst_index,
                           Tagged<WeakArrayList> src, int src_index, int len,
                           WriteBarrierMode mode);

  V8_EXPORT_PRIVATE bool IsFull() const;

  inline int AllocatedSize() const;

  class BodyDescriptor;

  static const int kMaxCapacity =
      (FixedArrayBase::kMaxSize - kHeaderSize) / kTaggedSize;

  static Handle<WeakArrayList> EnsureSpace(
      Isolate* isolate, Handle<WeakArrayList> array, int length,
      AllocationType allocation = AllocationType::kYoung);

  // Returns the number of non-cleaned weak references in the array.
  int CountLiveWeakReferences() const;

  // Returns the number of non-cleaned elements in the array.
  int CountLiveElements() const;

  // Returns whether an entry was found and removed. Will move the elements
  // around in the array - this method can only be used in cases where the user
  // doesn't care about the indices! Users should make sure there are no
  // duplicates.
  V8_EXPORT_PRIVATE bool RemoveOne(MaybeObjectHandle value);

  // Searches the array (linear time) and returns whether it contains the value.
  V8_EXPORT_PRIVATE bool Contains(Tagged<MaybeObject> value);

  class Iterator;

 private:
  static int OffsetOfElementAt(int index) {
    return kHeaderSize + index * kTaggedSize;
  }

  TQ_OBJECT_CONSTRUCTORS(WeakArrayList)
};

class WeakArrayList::Iterator {
 public:
  explicit Iterator(Tagged<WeakArrayList> array) : index_(0), array_(array) {}
  Iterator(const Iterator&) = delete;
  Iterator& operator=(const Iterator&) = delete;

  inline Tagged<HeapObject> Next();

 private:
  int index_;
  Tagged<WeakArrayList> array_;
  DISALLOW_GARBAGE_COLLECTION(no_gc_)
};

class ArrayListShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  using ElementT = Object;
  using CompressionScheme = V8HeapCompressionScheme;
  static constexpr RootIndex kMapRootIndex = RootIndex::kArrayListMap;
  static constexpr bool kLengthEqualsCapacity = false;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kLengthOffset, kTaggedSize)                                         \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// A generic array that grows dynamically with O(1) amortized insertion.
class ArrayList : public TaggedArrayBase<ArrayList, ArrayListShape> {
  using Super = TaggedArrayBase<ArrayList, ArrayListShape>;
  OBJECT_CONSTRUCTORS(ArrayList, Super);

 public:
  using Shape = ArrayListShape;

  template <class IsolateT>
  static inline Handle<ArrayList> New(
      IsolateT* isolate, int capacity,
      AllocationType allocation = AllocationType::kYoung);

  inline int length() const;
  inline void set_length(int value);

  V8_EXPORT_PRIVATE static Handle<ArrayList> Add(
      Isolate* isolate, Handle<ArrayList> array, Tagged<Smi> obj,
      AllocationType allocation = AllocationType::kYoung);
  V8_EXPORT_PRIVATE static Handle<ArrayList> Add(
      Isolate* isolate, Handle<ArrayList> array, DirectHandle<Object> obj,
      AllocationType allocation = AllocationType::kYoung);
  V8_EXPORT_PRIVATE static Handle<ArrayList> Add(
      Isolate* isolate, Handle<ArrayList> array, DirectHandle<Object> obj0,
      DirectHandle<Object> obj1,
      AllocationType allocation = AllocationType::kYoung);

  V8_EXPORT_PRIVATE static Handle<FixedArray> ToFixedArray(
      Isolate* isolate, DirectHandle<ArrayList> array,
      AllocationType allocation = AllocationType::kYoung);

  // Right-trim the array.
  // Invariant: 0 < new_length <= length()
  void RightTrim(Isolate* isolate, int new_capacity);

  DECL_PRINTER(ArrayList)
  DECL_VERIFIER(ArrayList)

  class BodyDescriptor;

  static constexpr int kLengthOffset = Shape::kLengthOffset;

 private:
  static Handle<ArrayList> EnsureSpace(
      Isolate* isolate, Handle<ArrayList> array, int length,
      AllocationType allocation = AllocationType::kYoung);
};

enum SearchMode { ALL_ENTRIES, VALID_ENTRIES };

template <SearchMode search_mode, typename T>
inline int Search(T* array, Tagged<Name> name, int valid_entries = 0,
                  int* out_insertion_index = nullptr,
                  bool concurrent_search = false);

class ByteArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kUInt8Size;
  using ElementT = uint8_t;
  static constexpr RootIndex kMapRootIndex = RootIndex::kByteArrayMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kLengthOffset, kTaggedSize)                                         \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// ByteArray represents fixed sized arrays containing raw bytes that will not
// be scanned by the garbage collector.
class ByteArray : public PrimitiveArrayBase<ByteArray, ByteArrayShape> {
  using Super = PrimitiveArrayBase<ByteArray, ByteArrayShape>;
  OBJECT_CONSTRUCTORS(ByteArray, Super);

 public:
  using Shape = ByteArrayShape;

  template <class IsolateT>
  static inline Handle<ByteArray> New(
      IsolateT* isolate, int capacity,
      AllocationType allocation = AllocationType::kYoung);

  inline uint32_t get_int(int offset) const;
  inline void set_int(int offset, uint32_t value);

  // Given the full object size in bytes, return the length that should be
  // passed to New s.t. an object of the same size is created.
  static constexpr int LengthFor(int size_in_bytes) {
    DCHECK(IsAligned(size_in_bytes, kTaggedSize));
    DCHECK_GE(size_in_bytes, Shape::kHeaderSize);
    return size_in_bytes - Shape::kHeaderSize;
  }

  DECL_PRINTER(ByteArray)
  DECL_VERIFIER(ByteArray)

  class BodyDescriptor;

  static constexpr int kBytesOffset = Shape::kHeaderSize;
};

class TrustedByteArrayShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kUInt8Size;
  using ElementT = uint8_t;
  static constexpr RootIndex kMapRootIndex = RootIndex::kTrustedByteArrayMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kLengthOffset, kTaggedSize)                                         \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(TrustedObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// A ByteArray in trusted space.
class TrustedByteArray
    : public PrimitiveArrayBase<TrustedByteArray, TrustedByteArrayShape,
                                TrustedObject> {
  using Super = PrimitiveArrayBase<TrustedByteArray, TrustedByteArrayShape,
                                   TrustedObject>;
  OBJECT_CONSTRUCTORS(TrustedByteArray, Super);

 public:
  using Shape = TrustedByteArrayShape;

  template <class IsolateT>
  static inline Handle<TrustedByteArray> New(
      IsolateT* isolate, int capacity,
      AllocationType allocation_type = AllocationType::kTrusted);

  // Given the full object size in bytes, return the length that should be
  // passed to New s.t. an object of the same size is created.
  static constexpr int LengthFor(int size_in_bytes) {
    DCHECK(IsAligned(size_in_bytes, kTaggedSize));
    DCHECK_GE(size_in_bytes, Shape::kHeaderSize);
    return size_in_bytes - Shape::kHeaderSize;
  }

  DECL_PRINTER(TrustedByteArray)
  DECL_VERIFIER(TrustedByteArray)

  class BodyDescriptor;

  static constexpr int kBytesOffset = Shape::kHeaderSize;
};

// Convenience class for treating a ByteArray / TrustedByteArray as array of
// fixed-size integers.
template <typename T, typename Base>
class FixedIntegerArrayBase : public Base {
  static_assert(std::is_integral<T>::value);

 public:
  // {MoreArgs...} allows passing the `AllocationType` if `Base` is `ByteArray`.
  template <typename... MoreArgs>
  static Handle<FixedIntegerArrayBase<T, Base>> New(Isolate* isolate,
                                                    int length,
                                                    MoreArgs&&... more_args);

  // Get/set the contents of this array.
  T get(int index) const;
  void set(int index, T value);

  // Code Generation support.
  static constexpr int OffsetOfElementAt(int index) {
    return Base::kHeaderSize + index * sizeof(T);
  }

  inline int length() const;

  OBJECT_CONSTRUCTORS(FixedIntegerArrayBase, Base);
};

using FixedInt8Array = FixedIntegerArrayBase<int8_t, ByteArray>;
using FixedUInt8Array = FixedIntegerArrayBase<uint8_t, ByteArray>;
using FixedInt16Array = FixedIntegerArrayBase<int16_t, ByteArray>;
using FixedUInt16Array = FixedIntegerArrayBase<uint16_t, ByteArray>;
using FixedInt32Array = FixedIntegerArrayBase<int32_t, ByteArray>;
using FixedUInt32Array = FixedIntegerArrayBase<uint32_t, ByteArray>;
using FixedInt64Array = FixedIntegerArrayBase<int64_t, ByteArray>;
using FixedUInt64Array = FixedIntegerArrayBase<uint64_t, ByteArray>;

// Use with care! Raw addresses on the heap are not safe in combination with
// the sandbox. Use an ExternalPointerArray instead. However, this can for
// example be used to store sandboxed pointers, which is safe.
template <typename Base>
class FixedAddressArrayBase : public FixedIntegerArrayBase<Address, Base> {
  using Underlying = FixedIntegerArrayBase<Address, Base>;

 public:
  // Get/set a sandboxed pointer from this array.
  inline Address get_sandboxed_pointer(int offset) const;
  inline void set_sandboxed_pointer(int offset, Address value);

  // {MoreArgs...} allows passing the `AllocationType` if `Base` is `ByteArray`.
  template <typename... MoreArgs>
  static inline Handle<FixedAddressArrayBase> New(Isolate* isolate, int length,
                                                  MoreArgs&&... more_args);

  OBJECT_CONSTRUCTORS(FixedAddressArrayBase, Underlying);
};

using FixedAddressArray = FixedAddressArrayBase<ByteArray>;
using TrustedFixedAddressArray = FixedAddressArrayBase<TrustedByteArray>;

// An array containing external pointers.
// When the sandbox is off, this will simply contain system-pointer sized words.
// Otherwise, it contains external pointer handles, i.e. indices into the
// external pointer table.
// This class uses lazily-initialized external pointer slots. As such, its
// content can simply be zero-initialized, and the external pointer table
// entries are only allocated when an element is written to for the first time.
class ExternalPointerArray : public FixedArrayBase {
 public:
  template <ExternalPointerTag tag>
  inline Address get(int index, Isolate* isolate);
  template <ExternalPointerTag tag>
  inline void set(int index, Isolate* isolate, Address value);

  static inline Handle<ExternalPointerArray> New(
      Isolate* isolate, int length,
      AllocationType allocation = AllocationType::kYoung);

  static constexpr int SizeFor(int length) {
    return kHeaderSize + length * kExternalPointerSlotSize;
  }

  static constexpr int OffsetOfElementAt(int index) {
    return kHeaderSize + index * kExternalPointerSlotSize;
  }

  // Maximal length of a single ExternalPointerArray.
  static const int kMaxLength = FixedArrayBase::kMaxSize - kHeaderSize;
  static_assert(Internals::IsValidSmi(kMaxLength),
                "ExternalPointerArray maxLength not a Smi");

  class BodyDescriptor;

  static constexpr int kPointersOffset = kHeaderSize;

  DECL_PRINTER(ExternalPointerArray)
  DECL_VERIFIER(ExternalPointerArray)

  OBJECT_CONSTRUCTORS(ExternalPointerArray, FixedArrayBase);
};

template <class T, class Super>
class PodArrayBase : public Super {
 public:
  void copy_out(int index, T* result, int length) {
    MemCopy(result, Super::AddressOfElementAt(index * sizeof(T)),
            length * sizeof(T));
  }

  void copy_in(int index, const T* buffer, int length) {
    MemCopy(Super::AddressOfElementAt(index * sizeof(T)), buffer,
            length * sizeof(T));
  }

  bool matches(const T* buffer, int length) {
    DCHECK_LE(length, this->length());
    return memcmp(Super::begin(), buffer, length * sizeof(T)) == 0;
  }

  bool matches(int offset, const T* buffer, int length) {
    DCHECK_LE(offset, this->length());
    DCHECK_LE(offset + length, this->length());
    return memcmp(Super::begin() + sizeof(T) * offset, buffer,
                  length * sizeof(T)) == 0;
  }

  T get(int index) {
    T result;
    copy_out(index, &result, 1);
    return result;
  }

  void set(int index, const T& value) { copy_in(index, &value, 1); }

  inline int length() const;

  OBJECT_CONSTRUCTORS(PodArrayBase, Super);
};

// Wrapper class for ByteArray which can store arbitrary C++ classes, as long
// as they can be copied with memcpy.
template <class T>
class PodArray : public PodArrayBase<T, ByteArray> {
 public:
  static Handle<PodArray<T>> New(
      Isolate* isolate, int length,
      AllocationType allocation = AllocationType::kYoung);
  static Handle<PodArray<T>> New(
      LocalIsolate* isolate, int length,
      AllocationType allocation = AllocationType::kOld);

  OBJECT_CONSTRUCTORS(PodArray, PodArrayBase<T, ByteArray>);
};

template <class T>
class TrustedPodArray : public PodArrayBase<T, TrustedByteArray> {
 public:
  static Handle<TrustedPodArray<T>> New(Isolate* isolate, int length);
  static Handle<TrustedPodArray<T>> New(LocalIsolate* isolate, int length);

  OBJECT_CONSTRUCTORS(TrustedPodArray, PodArrayBase<T, TrustedByteArray>);
};

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FIXED_ARRAY_H_
