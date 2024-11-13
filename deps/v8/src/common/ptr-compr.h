// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_PTR_COMPR_H_
#define V8_COMMON_PTR_COMPR_H_

#include "src/base/memory.h"
#include "src/common/globals.h"

namespace v8::internal {

class IsolateGroup;

// This is just a collection of common compression scheme related functions.
// Each pointer compression cage then has its own compression scheme, which
// mainly differes in the cage base address they use.
template <typename Cage>
class V8HeapCompressionSchemeImpl {
 public:
  V8_INLINE static Address GetPtrComprCageBaseAddress(Address on_heap_addr);

  V8_INLINE static Address GetPtrComprCageBaseAddress(
      PtrComprCageBase cage_base);

  // Compresses full-pointer representation of a tagged value to on-heap
  // representation.
  // Must only be used for compressing object pointers since this function
  // assumes that we deal with a valid address inside the pointer compression
  // cage.
  V8_INLINE static Tagged_t CompressObject(Address tagged);
  // Compress a potentially invalid pointer.
  V8_INLINE static Tagged_t CompressAny(Address tagged);

  // Decompresses smi value.
  V8_INLINE static Address DecompressTaggedSigned(Tagged_t raw_value);

  // Decompresses any tagged value, preserving both weak- and smi- tags.
  template <typename TOnHeapAddress>
  V8_INLINE static Address DecompressTagged(TOnHeapAddress on_heap_addr,
                                            Tagged_t raw_value);

  // Given a 64bit raw value, found on the stack, calls the callback function
  // with all possible pointers that may be "contained" in compressed form in
  // this value, either as complete compressed pointers or as intermediate
  // (half-computed) results.
  template <typename ProcessPointerCallback>
  V8_INLINE static void ProcessIntermediatePointers(
      PtrComprCageBase cage_base, Address raw_value,
      ProcessPointerCallback callback);

  // Process-wide cage base value used for decompression.
  V8_INLINE static void InitBase(Address base);
  V8_CONST V8_INLINE static Address base();
};

// The main pointer compression cage, used for most objects.
class MainCage : public AllStatic {
  friend class V8HeapCompressionSchemeImpl<MainCage>;

  // These non-inlined accessors to base_ field are used in component builds
  // where cross-component access to thread local variables is not allowed.
  static V8_EXPORT_PRIVATE Address base_non_inlined();
  static V8_EXPORT_PRIVATE void set_base_non_inlined(Address base);

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
  static V8_EXPORT_PRIVATE uintptr_t base_ V8_CONSTINIT;
#else
  static thread_local uintptr_t base_ V8_CONSTINIT;
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE
};
using V8HeapCompressionScheme = V8HeapCompressionSchemeImpl<MainCage>;

#ifdef V8_ENABLE_SANDBOX
// Compression scheme used for compressed pointers between trusted objects in
// the trusted heap space, outside of the sandbox.
class TrustedCage : public AllStatic {
  friend class V8HeapCompressionSchemeImpl<TrustedCage>;

  // The TrustedCage is only used in the shared cage build configuration, so
  // there is no need for a thread_local version.
  static V8_EXPORT_PRIVATE uintptr_t base_ V8_CONSTINIT;
};
using TrustedSpaceCompressionScheme = V8HeapCompressionSchemeImpl<TrustedCage>;
#else
// The trusted cage does not exist in this case.
using TrustedSpaceCompressionScheme = V8HeapCompressionScheme;
#endif  // V8_ENABLE_SANDBOX

// A compression scheme which can be passed if the only objects we ever expect
// to see are Smis (e.g. for {TaggedField<Smi, 0, SmiCompressionScheme>}).
class SmiCompressionScheme : public AllStatic {
 public:
  static Address DecompressTaggedSigned(Tagged_t raw_value) {
    // For runtime code the upper 32-bits of the Smi value do not matter.
    return static_cast<Address>(raw_value);
  }

  static Tagged_t CompressObject(Address tagged) {
    V8_ASSUME(HAS_SMI_TAG(tagged));
    return static_cast<Tagged_t>(tagged);
  }
};

#ifdef V8_EXTERNAL_CODE_SPACE
// Compression scheme used for fields containing InstructionStream objects
// (namely for the Code::code field). Same as
// V8HeapCompressionScheme but with a different base value.
// TODO(ishell): consider also using V8HeapCompressionSchemeImpl here unless
// this becomes a different compression scheme that allows crossing the 4GB
// boundary.
class ExternalCodeCompressionScheme {
 public:
  V8_INLINE static Address PrepareCageBaseAddress(Address on_heap_addr);

  // Note that this compression scheme doesn't allow reconstruction of the cage
  // base value from any arbitrary value, thus the cage base has to be passed
  // explicitly to the decompression functions.
  static Address GetPtrComprCageBaseAddress(Address on_heap_addr) = delete;

  V8_INLINE static Address GetPtrComprCageBaseAddress(
      PtrComprCageBase cage_base);

  // Compresses full-pointer representation of a tagged value to on-heap
  // representation.
  // Must only be used for compressing object pointers (incl. SMI) since this
  // function assumes pointers to be inside the pointer compression cage.
  V8_INLINE static Tagged_t CompressObject(Address tagged);
  // Compress anything that does not follow the above requirements (e.g. a maybe
  // object, or a marker bit pattern).
  V8_INLINE static Tagged_t CompressAny(Address tagged);

  // Decompresses smi value.
  V8_INLINE static Address DecompressTaggedSigned(Tagged_t raw_value);

  // Decompresses any tagged value, preserving both weak- and smi- tags.
  template <typename TOnHeapAddress>
  V8_INLINE static Address DecompressTagged(TOnHeapAddress on_heap_addr,
                                            Tagged_t raw_value);

  // Process-wide cage base value used for decompression.
  V8_INLINE static void InitBase(Address base);
  V8_INLINE static Address base();

  // Given a 64bit raw value, found on the stack, calls the callback function
  // with all possible pointers that may be "contained" in compressed form in
  // this value, either as complete compressed pointers or as intermediate
  // (half-computed) results.
  template <typename ProcessPointerCallback>
  V8_INLINE static void ProcessIntermediatePointers(
      PtrComprCageBase cage_base, Address raw_value,
      ProcessPointerCallback callback);

 private:
  // These non-inlined accessors to base_ field are used in component builds
  // where cross-component access to thread local variables is not allowed.
  static Address base_non_inlined();
  static void set_base_non_inlined(Address base);

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
  static V8_EXPORT_PRIVATE uintptr_t base_ V8_CONSTINIT;
#else
  static thread_local uintptr_t base_ V8_CONSTINIT;
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE
};

#endif  // V8_EXTERNAL_CODE_SPACE

// Accessors for fields that may be unaligned due to pointer compression.

template <typename V>
static inline V ReadMaybeUnalignedValue(Address p) {
  // Pointer compression causes types larger than kTaggedSize to be unaligned.
#ifdef V8_COMPRESS_POINTERS
  constexpr bool v8_pointer_compression_unaligned = sizeof(V) > kTaggedSize;
#else
  constexpr bool v8_pointer_compression_unaligned = false;
#endif
  // Bug(v8:8875) Double fields may be unaligned.
  constexpr bool unaligned_double_field =
      std::is_same<V, double>::value && kDoubleSize > kTaggedSize;
  if (unaligned_double_field || v8_pointer_compression_unaligned) {
    return base::ReadUnalignedValue<V>(p);
  } else {
    return base::Memory<V>(p);
  }
}

template <typename V>
static inline void WriteMaybeUnalignedValue(Address p, V value) {
  // Pointer compression causes types larger than kTaggedSize to be unaligned.
#ifdef V8_COMPRESS_POINTERS
  constexpr bool v8_pointer_compression_unaligned = sizeof(V) > kTaggedSize;
#else
  constexpr bool v8_pointer_compression_unaligned = false;
#endif
  // Bug(v8:8875) Double fields may be unaligned.
  constexpr bool unaligned_double_field =
      std::is_same<V, double>::value && kDoubleSize > kTaggedSize;
  if (unaligned_double_field || v8_pointer_compression_unaligned) {
    base::WriteUnalignedValue<V>(p, value);
  } else {
    base::Memory<V>(p) = value;
  }
}

// When multi-cage pointer compression mode is enabled this scope object
// saves current cage's base values and sets them according to given Isolate.
// For all other configurations this scope object is a no-op.
class PtrComprCageAccessScope final {
 public:
#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  V8_INLINE explicit PtrComprCageAccessScope(Isolate* isolate);
  V8_INLINE ~PtrComprCageAccessScope();
#else
  V8_INLINE explicit PtrComprCageAccessScope(Isolate* isolate) {}
  V8_INLINE ~PtrComprCageAccessScope() {}
#endif

 private:
#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  const Address cage_base_;
#ifdef V8_EXTERNAL_CODE_SPACE
  const Address code_cage_base_;
#endif  // V8_EXTERNAL_CODE_SPACE
  IsolateGroup* saved_current_isolate_group_;
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
};

}  // namespace v8::internal

#endif  // V8_COMMON_PTR_COMPR_H_
