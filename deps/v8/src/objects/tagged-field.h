// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_FIELD_H_
#define V8_OBJECTS_TAGGED_FIELD_H_

#include "src/base/atomicops.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/common/ptr-compr.h"
#include "src/objects/tagged-value.h"

namespace v8::internal {

// TaggedMember<T> represents an potentially compressed V8 tagged pointer, which
// is intended to be used as a member of a V8 object class.
//
// TODO(leszeks): Merge with TaggedField.
template <typename T, typename CompressionScheme = V8HeapCompressionScheme>
class TaggedMember;

// Base class for all TaggedMember<T> classes.
// TODO(leszeks): Merge with TaggedImpl.
using TaggedMemberBase = TaggedImpl<HeapObjectReferenceType::STRONG, Tagged_t>;

template <typename T, typename CompressionScheme>
class TaggedMember : public TaggedMemberBase {
 public:
  constexpr TaggedMember() = default;

  inline Tagged<T> load() const;
  inline void store(HeapObjectLayout* host, Tagged<T> value,
                    WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline Tagged<T> Relaxed_Load() const;

 private:
  inline void store_no_write_barrier(Tagged<T> value);
  inline void Relaxed_Store_no_write_barrier(Tagged<T> value);

  static inline Address tagged_to_full(Tagged_t tagged_value);
  static inline Tagged_t full_to_tagged(Address value);
};

static_assert(alignof(TaggedMember<Object>) == alignof(Tagged_t));
static_assert(sizeof(TaggedMember<Object>) == sizeof(Tagged_t));

template <typename T>
class UnalignedValueMember {
 public:
  UnalignedValueMember() = default;

  T value() const { return base::ReadUnalignedValue<T>(storage_); }
  void set_value(T value) { base::WriteUnalignedValue(storage_, value); }

 protected:
  alignas(alignof(Tagged_t)) char storage_[sizeof(T)];
};

class UnalignedDoubleMember : public UnalignedValueMember<double> {
 public:
  UnalignedDoubleMember() = default;

  uint64_t value_as_bits() const {
    return base::ReadUnalignedValue<uint64_t>(storage_);
  }
  void set_value_as_bits(uint64_t value) {
    base::WriteUnalignedValue(storage_, value);
  }
};
static_assert(alignof(UnalignedDoubleMember) == alignof(Tagged_t));
static_assert(sizeof(UnalignedDoubleMember) == sizeof(double));

// FLEXIBLE_ARRAY_MEMBER(T, name) represents a marker for a variable-sized
// suffix of members for a type.
//
// It behaves as if it were the last member of a class, and creates an accessor
// for `T* name()`.
//
// This macro is used instead of the C99 flexible array member syntax, because
//
//   a) That syntax is only in C++ as an extension,
//   b) On all our major compilers, it doesn't allow the class to have
//      subclasses (which means it doesn't work for e.g. TaggedArrayBase or
//      BigIntBase),
//   c) The similar zero-length array extension _also_ doesn't allow subclasses
//      on some compilers (specifically, MSVC).
//
// On compilers that do support zero length arrays (i.e. not MSVC), we use one
// of these instead of `this` pointer fiddling. This gives LLVM better
// information for optimization, and gives us the warnings we'd want to have
// (e.g. only allowing one FAM in a class, ensuring that OFFSET_OF_DATA_START is
// only used on classes with a FAM) on clang -- the MSVC version then doesn't
// check the same constraints, and relies on the code being equivalent enough.
#if V8_CC_MSVC && !defined(__clang__)
// MSVC doesn't support zero length arrays in base classes. Cast the
// one-past-this value to a zero length array reference, so that the return
// values match that in GCC/clang.
#define FLEXIBLE_ARRAY_MEMBER(Type, name)                     \
  using FlexibleDataReturnType = Type[0];                     \
  FlexibleDataReturnType& name() {                            \
    using ReturnType = Type[0];                               \
    return reinterpret_cast<ReturnType&>(*(this + 1));        \
  }                                                           \
  const FlexibleDataReturnType& name() const {                \
    using ReturnType = Type[0];                               \
    return reinterpret_cast<const ReturnType&>(*(this + 1));  \
  }                                                           \
  using FlexibleDataType = Type
#else
// GCC and clang allow zero length arrays in base classes. Return the zero
// length array by reference, to avoid array-to-pointer decay which can lose
// aliasing information.
#define FLEXIBLE_ARRAY_MEMBER(Type, name)                                \
  using FlexibleDataReturnType = Type[0];                                \
  FlexibleDataReturnType& name() { return flexible_array_member_data_; } \
  const FlexibleDataReturnType& name() const {                           \
    return flexible_array_member_data_;                                  \
  }                                                                      \
  Type flexible_array_member_data_[0];                                   \
  using FlexibleDataType = Type
#endif

// OFFSET_OF_DATA_START(T) returns the offset of the FLEXIBLE_ARRAY_MEMBER of
// the class T.
#if V8_CC_MSVC && !defined(__clang__)
#define OFFSET_OF_DATA_START(Type) sizeof(Type)
#else
#define OFFSET_OF_DATA_START(Type) offsetof(Type, flexible_array_member_data_)
#endif

// This helper static class represents a tagged field of type T at offset
// kFieldOffset inside some host HeapObject.
// For full-pointer mode this type adds no overhead but when pointer
// compression is enabled such class allows us to use proper decompression
// function depending on the field type.
template <typename T, int kFieldOffset = 0,
          typename CompressionScheme = V8HeapCompressionScheme>
class TaggedField : public AllStatic {
 public:
  static_assert(is_taggable_v<T> || std::is_same<MapWord, T>::value,
                "T must be strong or weak tagged type or MapWord");

  // True for Smi fields.
  static constexpr bool kIsSmi = std::is_same<Smi, T>::value;

  // True for HeapObject and MapWord fields. The latter may look like a Smi
  // if it contains forwarding pointer but still requires tagged pointer
  // decompression.
  static constexpr bool kIsHeapObject =
      is_subtype<T, HeapObject>::value || std::is_same_v<MapWord, T>;

  // Types should be wrapped in Tagged<>, except for MapWord which is used
  // directly.
  // TODO(leszeks): Clean this up to be more uniform.
  using PtrType =
      std::conditional_t<std::is_same_v<MapWord, T>, MapWord, Tagged<T>>;

  static inline Address address(Tagged<HeapObject> host, int offset = 0);

  static inline PtrType load(Tagged<HeapObject> host, int offset = 0);
  static inline PtrType load(PtrComprCageBase cage_base,
                             Tagged<HeapObject> host, int offset = 0);

  static inline void store(Tagged<HeapObject> host, PtrType value);
  static inline void store(Tagged<HeapObject> host, int offset, PtrType value);

  static inline PtrType Relaxed_Load(Tagged<HeapObject> host, int offset = 0);
  static inline PtrType Relaxed_Load(PtrComprCageBase cage_base,
                                     Tagged<HeapObject> host, int offset = 0);

  static inline void Relaxed_Store(Tagged<HeapObject> host, PtrType value);
  static inline void Relaxed_Store(Tagged<HeapObject> host, int offset,
                                   PtrType value);

  static inline PtrType Acquire_Load(Tagged<HeapObject> host, int offset = 0);
  static inline PtrType Acquire_Load_No_Unpack(PtrComprCageBase cage_base,
                                               Tagged<HeapObject> host,
                                               int offset = 0);
  static inline PtrType Acquire_Load(PtrComprCageBase cage_base,
                                     Tagged<HeapObject> host, int offset = 0);

  static inline PtrType SeqCst_Load(Tagged<HeapObject> host, int offset = 0);
  static inline PtrType SeqCst_Load(PtrComprCageBase cage_base,
                                    Tagged<HeapObject> host, int offset = 0);

  static inline void Release_Store(Tagged<HeapObject> host, PtrType value);
  static inline void Release_Store(Tagged<HeapObject> host, int offset,
                                   PtrType value);

  static inline void SeqCst_Store(Tagged<HeapObject> host, PtrType value);
  static inline void SeqCst_Store(Tagged<HeapObject> host, int offset,
                                  PtrType value);

  static inline PtrType SeqCst_Swap(Tagged<HeapObject> host, int offset,
                                    PtrType value);
  static inline PtrType SeqCst_Swap(PtrComprCageBase cage_base,
                                    Tagged<HeapObject> host, int offset,
                                    PtrType value);

  static inline Tagged_t Release_CompareAndSwap(Tagged<HeapObject> host,
                                                PtrType old, PtrType value);
  static inline PtrType SeqCst_CompareAndSwap(Tagged<HeapObject> host,
                                              int offset, PtrType old,
                                              PtrType value);

  // Note: Use these *_Map_Word methods only when loading a MapWord from a
  // MapField.
  static inline PtrType Relaxed_Load_Map_Word(PtrComprCageBase cage_base,
                                              Tagged<HeapObject> host);
  static inline void Relaxed_Store_Map_Word(Tagged<HeapObject> host,
                                            PtrType value);
  static inline void Release_Store_Map_Word(Tagged<HeapObject> host,
                                            PtrType value);

 private:
  static inline Tagged_t* location(Tagged<HeapObject> host, int offset = 0);

  template <typename TOnHeapAddress>
  static inline Address tagged_to_full(TOnHeapAddress on_heap_addr,
                                       Tagged_t tagged_value);

  static inline Tagged_t full_to_tagged(Address value);
};

template <typename T>
class TaggedField<Tagged<T>> : public TaggedField<T> {};

template <typename T, int kFieldOffset>
class TaggedField<Tagged<T>, kFieldOffset>
    : public TaggedField<T, kFieldOffset> {};

template <typename T, int kFieldOffset, typename CompressionScheme>
class TaggedField<Tagged<T>, kFieldOffset, CompressionScheme>
    : public TaggedField<T, kFieldOffset, CompressionScheme> {};

}  // namespace v8::internal

#endif  // V8_OBJECTS_TAGGED_FIELD_H_
