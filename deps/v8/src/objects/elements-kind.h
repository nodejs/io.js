// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ELEMENTS_KIND_H_
#define V8_OBJECTS_ELEMENTS_KIND_H_

#include "src/base/bits.h"
#include "src/base/bounds.h"
#include "src/base/macros.h"
#include "src/common/checks.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

// V has parameters (Type, type, TYPE, C type)
#define TYPED_ARRAYS_BASE(V)                             \
  V(Uint8, uint8, UINT8, uint8_t)                        \
  V(Int8, int8, INT8, int8_t)                            \
  V(Uint16, uint16, UINT16, uint16_t)                    \
  V(Int16, int16, INT16, int16_t)                        \
  V(Uint32, uint32, UINT32, uint32_t)                    \
  V(Int32, int32, INT32, int32_t)                        \
  V(Float32, float32, FLOAT32, float)                    \
  V(Float64, float64, FLOAT64, double)                   \
  V(Uint8Clamped, uint8_clamped, UINT8_CLAMPED, uint8_t) \
  V(BigUint64, biguint64, BIGUINT64, uint64_t)           \
  V(BigInt64, bigint64, BIGINT64, int64_t)

#define TYPED_ARRAYS_FLOAT16(V) V(Float16, float16, FLOAT16, uint16_t)

#define TYPED_ARRAYS(V) \
  TYPED_ARRAYS_BASE(V)  \
  TYPED_ARRAYS_FLOAT16(V)

#define RAB_GSAB_TYPED_ARRAYS_BASE(V)                                    \
  V(RabGsabUint8, rab_gsab_uint8, RAB_GSAB_UINT8, uint8_t)               \
  V(RabGsabInt8, rab_gsab_int8, RAB_GSAB_INT8, int8_t)                   \
  V(RabGsabUint16, rab_gsab_uint16, RAB_GSAB_UINT16, uint16_t)           \
  V(RabGsabInt16, rab_gsab_int16, RAB_GSAB_INT16, int16_t)               \
  V(RabGsabUint32, rab_gsab_uint32, RAB_GSAB_UINT32, uint32_t)           \
  V(RabGsabInt32, rab_gsab_int32, RAB_GSAB_INT32, int32_t)               \
  V(RabGsabFloat32, rab_gsab_float32, RAB_GSAB_FLOAT32, float)           \
  V(RabGsabFloat64, rab_gsab_float64, RAB_GSAB_FLOAT64, double)          \
  V(RabGsabUint8Clamped, rab_gsab_uint8_clamped, RAB_GSAB_UINT8_CLAMPED, \
    uint8_t)                                                             \
  V(RabGsabBigUint64, rab_gsab_biguint64, RAB_GSAB_BIGUINT64, uint64_t)  \
  V(RabGsabBigInt64, rab_gsab_bigint64, RAB_GSAB_BIGINT64, int64_t)

#define RAB_GSAB_TYPED_ARRAYS_FLOAT16(V) \
  V(RabGsabFloat16, rab_gsab_float16, RAB_GSAB_FLOAT16, uint16_t)

#define RAB_GSAB_TYPED_ARRAYS(V) \
  RAB_GSAB_TYPED_ARRAYS_BASE(V)  \
  RAB_GSAB_TYPED_ARRAYS_FLOAT16(V)

// The TypedArrays backed by RAB / GSAB are called Uint8Array, Uint16Array etc,
// and not RabGsabUint8Array, RabGsabUint16Array etc. This macro is used for
// generating code which refers to the TypedArray type.
#define RAB_GSAB_TYPED_ARRAYS_WITH_TYPED_ARRAY_TYPE_BASE(V)                \
  V(Uint8, rab_gsab_uint8, RAB_GSAB_UINT8, uint8_t)                        \
  V(Int8, rab_gsab_int8, RAB_GSAB_INT8, int8_t)                            \
  V(Uint16, rab_gsab_uint16, RAB_GSAB_UINT16, uint16_t)                    \
  V(Int16, rab_gsab_int16, RAB_GSAB_INT16, int16_t)                        \
  V(Uint32, rab_gsab_uint32, RAB_GSAB_UINT32, uint32_t)                    \
  V(Int32, rab_gsab_int32, RAB_GSAB_INT32, int32_t)                        \
  V(Float32, rab_gsab_float32, RAB_GSAB_FLOAT32, float)                    \
  V(Float64, rab_gsab_float64, RAB_GSAB_FLOAT64, double)                   \
  V(Uint8Clamped, rab_gsab_uint8_clamped, RAB_GSAB_UINT8_CLAMPED, uint8_t) \
  V(BigUint64, rab_gsab_biguint64, RAB_GSAB_BIGUINT64, uint64_t)           \
  V(BigInt64, rab_gsab_bigint64, RAB_GSAB_BIGINT64, int64_t)

#define RAB_GSAB_TYPED_ARRAYS_WITH_TYPED_ARRAY_TYPE_FLOAT16(V) \
  V(Float16, rab_gsab_float16, RAB_GSAB_FLOAT16, uint16_t)

#define RAB_GSAB_TYPED_ARRAYS_WITH_TYPED_ARRAY_TYPE(V) \
  RAB_GSAB_TYPED_ARRAYS_WITH_TYPED_ARRAY_TYPE_BASE(V)  \
  RAB_GSAB_TYPED_ARRAYS_WITH_TYPED_ARRAY_TYPE_FLOAT16(V)

// Like RAB_GSAB_TYPED_ARRAYS but has an additional parameter for
// for the corresponding non-RAB/GSAB ElementsKind.
#define RAB_GSAB_TYPED_ARRAYS_WITH_NON_RAB_GSAB_ELEMENTS_KIND_BASE(V)    \
  V(RabGsabUint8, rab_gsab_uint8, RAB_GSAB_UINT8, uint8_t, UINT8)        \
  V(RabGsabInt8, rab_gsab_int8, RAB_GSAB_INT8, int8_t, INT8)             \
  V(RabGsabUint16, rab_gsab_uint16, RAB_GSAB_UINT16, uint16_t, UINT16)   \
  V(RabGsabInt16, rab_gsab_int16, RAB_GSAB_INT16, int16_t, INT16)        \
  V(RabGsabUint32, rab_gsab_uint32, RAB_GSAB_UINT32, uint32_t, UINT32)   \
  V(RabGsabInt32, rab_gsab_int32, RAB_GSAB_INT32, int32_t, INT32)        \
  V(RabGsabFloat32, rab_gsab_float32, RAB_GSAB_FLOAT32, float, FLOAT32)  \
  V(RabGsabFloat64, rab_gsab_float64, RAB_GSAB_FLOAT64, double, FLOAT64) \
  V(RabGsabUint8Clamped, rab_gsab_uint8_clamped, RAB_GSAB_UINT8_CLAMPED, \
    uint8_t, UINT8_CLAMPED)                                              \
  V(RabGsabBigUint64, rab_gsab_biguint64, RAB_GSAB_BIGUINT64, uint64_t,  \
    BIGUINT64)                                                           \
  V(RabGsabBigInt64, rab_gsab_bigint64, RAB_GSAB_BIGINT64, int64_t, BIGINT64)

#define RAB_GSAB_TYPED_ARRAYS_WITH_NON_RAB_GSAB_ELEMENTS_KIND_FLOAT16(V) \
  V(RabGsabFloat16, rab_gsab_float16, RAB_GSAB_FLOAT16, uint16_t, FLOAT16)

#define RAB_GSAB_TYPED_ARRAYS_WITH_NON_RAB_GSAB_ELEMENTS_KIND(V) \
  RAB_GSAB_TYPED_ARRAYS_WITH_NON_RAB_GSAB_ELEMENTS_KIND_BASE(V)  \
  RAB_GSAB_TYPED_ARRAYS_WITH_NON_RAB_GSAB_ELEMENTS_KIND_FLOAT16(V)

enum ElementsKind : uint8_t {
  // The "fast" kind for elements that only contain SMI values. Must be first
  // to make it possible to efficiently check maps for this kind.
  PACKED_SMI_ELEMENTS,
  HOLEY_SMI_ELEMENTS,

  // The "fast" kind for tagged values. Must be second to make it possible to
  // efficiently check maps for this and the PACKED_SMI_ELEMENTS kind
  // together at once.
  PACKED_ELEMENTS,
  HOLEY_ELEMENTS,

  // The "fast" kind for unwrapped, non-tagged double values.
  PACKED_DOUBLE_ELEMENTS,
  HOLEY_DOUBLE_ELEMENTS,

  // The nonextensible kind for elements.
  PACKED_NONEXTENSIBLE_ELEMENTS,
  HOLEY_NONEXTENSIBLE_ELEMENTS,

  // The sealed kind for elements.
  PACKED_SEALED_ELEMENTS,
  HOLEY_SEALED_ELEMENTS,

  // The frozen kind for elements.
  PACKED_FROZEN_ELEMENTS,
  HOLEY_FROZEN_ELEMENTS,

  // SharedArray elements kind. A FAST_SEALED_ELEMENTS variation useful to
  // code specific paths for SharedArrays.
  SHARED_ARRAY_ELEMENTS,

  // The "slow" kind.
  DICTIONARY_ELEMENTS,

  // Elements kind of the "arguments" object (only in sloppy mode).
  FAST_SLOPPY_ARGUMENTS_ELEMENTS,
  SLOW_SLOPPY_ARGUMENTS_ELEMENTS,

  // For string wrapper objects ("new String('...')"), the string's characters
  // are overlaid onto a regular elements backing store.
  FAST_STRING_WRAPPER_ELEMENTS,
  SLOW_STRING_WRAPPER_ELEMENTS,

// Fixed typed arrays.
#define TYPED_ARRAY_ELEMENTS_KIND(Type, type, TYPE, ctype) TYPE##_ELEMENTS,
  TYPED_ARRAYS(TYPED_ARRAY_ELEMENTS_KIND)
      RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_ELEMENTS_KIND)
#undef TYPED_ARRAY_ELEMENTS_KIND

  // WasmObject elements kind. The actual elements type is read from the
  // respective WasmTypeInfo.
  WASM_ARRAY_ELEMENTS,

  // Sentinel ElementsKind for objects with no elements.
  NO_ELEMENTS,

  // Derived constants from ElementsKind.
  FIRST_ELEMENTS_KIND = PACKED_SMI_ELEMENTS,
  LAST_ELEMENTS_KIND = RAB_GSAB_FLOAT16_ELEMENTS,
  FIRST_FAST_ELEMENTS_KIND = PACKED_SMI_ELEMENTS,
  LAST_FAST_ELEMENTS_KIND = HOLEY_DOUBLE_ELEMENTS,
  FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND = UINT8_ELEMENTS,
  LAST_FIXED_TYPED_ARRAY_ELEMENTS_KIND = FLOAT16_ELEMENTS,
  FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND = RAB_GSAB_UINT8_ELEMENTS,
  LAST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND = RAB_GSAB_FLOAT16_ELEMENTS,
  TERMINAL_FAST_ELEMENTS_KIND = HOLEY_ELEMENTS,
  FIRST_ANY_NONEXTENSIBLE_ELEMENTS_KIND = PACKED_NONEXTENSIBLE_ELEMENTS,
  LAST_ANY_NONEXTENSIBLE_ELEMENTS_KIND = SHARED_ARRAY_ELEMENTS,

// Alias for kSystemPointerSize-sized elements
#ifdef V8_COMPRESS_POINTERS
  SYSTEM_POINTER_ELEMENTS = PACKED_DOUBLE_ELEMENTS,
#else
  SYSTEM_POINTER_ELEMENTS = PACKED_ELEMENTS,
#endif
};

constexpr int kElementsKindCount = LAST_ELEMENTS_KIND - FIRST_ELEMENTS_KIND + 1;
constexpr int kFastElementsKindCount =
    LAST_FAST_ELEMENTS_KIND - FIRST_FAST_ELEMENTS_KIND + 1;

// The number to add to a packed elements kind to reach a holey elements kind
constexpr int kFastElementsKindPackedToHoley =
    HOLEY_SMI_ELEMENTS - PACKED_SMI_ELEMENTS;

constexpr int kElementsKindBits = 6;
static_assert((1 << kElementsKindBits) > LAST_ELEMENTS_KIND);
static_assert((1 << (kElementsKindBits - 1)) <= LAST_ELEMENTS_KIND);

constexpr int kFastElementsKindBits = 3;
static_assert((1 << kFastElementsKindBits) > LAST_FAST_ELEMENTS_KIND);
static_assert((1 << (kFastElementsKindBits - 1)) <= LAST_FAST_ELEMENTS_KIND);

const uint8_t* TypedArrayAndRabGsabTypedArrayElementsKindShifts();
const uint8_t* TypedArrayAndRabGsabTypedArrayElementsKindSizes();
inline constexpr int ElementsKindToShiftSize(ElementsKind elements_kind) {
  switch (elements_kind) {
    case UINT8_ELEMENTS:
    case INT8_ELEMENTS:
    case UINT8_CLAMPED_ELEMENTS:
    case RAB_GSAB_UINT8_ELEMENTS:
    case RAB_GSAB_INT8_ELEMENTS:
    case RAB_GSAB_UINT8_CLAMPED_ELEMENTS:
      return 0;
    case UINT16_ELEMENTS:
    case INT16_ELEMENTS:
    case FLOAT16_ELEMENTS:
    case RAB_GSAB_FLOAT16_ELEMENTS:
    case RAB_GSAB_UINT16_ELEMENTS:
    case RAB_GSAB_INT16_ELEMENTS:
      return 1;
    case UINT32_ELEMENTS:
    case INT32_ELEMENTS:
    case FLOAT32_ELEMENTS:
    case RAB_GSAB_UINT32_ELEMENTS:
    case RAB_GSAB_INT32_ELEMENTS:
    case RAB_GSAB_FLOAT32_ELEMENTS:
      return 2;
    case PACKED_DOUBLE_ELEMENTS:
    case HOLEY_DOUBLE_ELEMENTS:
    case FLOAT64_ELEMENTS:
    case BIGINT64_ELEMENTS:
    case BIGUINT64_ELEMENTS:
    case RAB_GSAB_FLOAT64_ELEMENTS:
    case RAB_GSAB_BIGINT64_ELEMENTS:
    case RAB_GSAB_BIGUINT64_ELEMENTS:
      return 3;
    case PACKED_SMI_ELEMENTS:
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_SMI_ELEMENTS:
    case HOLEY_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case DICTIONARY_ELEMENTS:
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS:
    case SHARED_ARRAY_ELEMENTS:
      return kTaggedSizeLog2;
    case WASM_ARRAY_ELEMENTS:
    case NO_ELEMENTS:
      CONSTEXPR_UNREACHABLE();
  }
  CONSTEXPR_UNREACHABLE();
}
inline constexpr int ElementsKindToByteSize(ElementsKind elements_kind) {
  return 1 << ElementsKindToShiftSize(elements_kind);
}
int GetDefaultHeaderSizeForElementsKind(ElementsKind elements_kind);
const char* ElementsKindToString(ElementsKind kind);

inline ElementsKind GetInitialFastElementsKind() { return PACKED_SMI_ELEMENTS; }

ElementsKind GetFastElementsKindFromSequenceIndex(int sequence_number);
int GetSequenceIndexFromFastElementsKind(ElementsKind elements_kind);

ElementsKind GetNextTransitionElementsKind(ElementsKind elements_kind);

inline bool IsDictionaryElementsKind(ElementsKind kind) {
  return kind == DICTIONARY_ELEMENTS;
}

inline bool IsFastArgumentsElementsKind(ElementsKind kind) {
  return kind == FAST_SLOPPY_ARGUMENTS_ELEMENTS;
}

inline bool IsSlowArgumentsElementsKind(ElementsKind kind) {
  return kind == SLOW_SLOPPY_ARGUMENTS_ELEMENTS;
}

inline bool IsSloppyArgumentsElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, FAST_SLOPPY_ARGUMENTS_ELEMENTS,
                         SLOW_SLOPPY_ARGUMENTS_ELEMENTS);
}

inline bool IsStringWrapperElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, FAST_STRING_WRAPPER_ELEMENTS,
                         SLOW_STRING_WRAPPER_ELEMENTS);
}

inline bool IsTypedArrayElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND,
                         LAST_FIXED_TYPED_ARRAY_ELEMENTS_KIND);
}

inline bool IsRabGsabTypedArrayElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND,
                         LAST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND);
}

inline bool IsTypedArrayOrRabGsabTypedArrayElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND,
                         LAST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND);
}

inline bool IsBigIntTypedArrayElementsKind(ElementsKind kind) {
  return kind == BIGINT64_ELEMENTS || kind == BIGUINT64_ELEMENTS ||
         kind == RAB_GSAB_BIGINT64_ELEMENTS ||
         kind == RAB_GSAB_BIGUINT64_ELEMENTS;
}

inline bool IsFloat16TypedArrayElementsKind(ElementsKind kind) {
  return kind == FLOAT16_ELEMENTS || kind == RAB_GSAB_FLOAT16_ELEMENTS;
}

inline bool IsFloatTypedArrayElementsKind(ElementsKind kind) {
  return kind == FLOAT16_ELEMENTS || kind == RAB_GSAB_FLOAT16_ELEMENTS ||
         kind == FLOAT32_ELEMENTS || kind == FLOAT64_ELEMENTS ||
         kind == RAB_GSAB_FLOAT32_ELEMENTS || kind == RAB_GSAB_FLOAT64_ELEMENTS;
}

inline bool IsSignedIntTypedArrayElementsKind(ElementsKind kind) {
  return kind == INT8_ELEMENTS || kind == RAB_GSAB_INT8_ELEMENTS ||
         kind == INT16_ELEMENTS || kind == RAB_GSAB_INT16_ELEMENTS ||
         kind == INT32_ELEMENTS || kind == RAB_GSAB_INT32_ELEMENTS;
}

inline bool IsUnsignedIntTypedArrayElementsKind(ElementsKind kind) {
  return kind == UINT8_CLAMPED_ELEMENTS ||
         kind == RAB_GSAB_UINT8_CLAMPED_ELEMENTS || kind == UINT8_ELEMENTS ||
         kind == RAB_GSAB_UINT8_ELEMENTS || kind == UINT16_ELEMENTS ||
         kind == RAB_GSAB_UINT16_ELEMENTS || kind == UINT32_ELEMENTS ||
         kind == RAB_GSAB_UINT32_ELEMENTS;
}

inline bool IsWasmArrayElementsKind(ElementsKind kind) {
  return kind == WASM_ARRAY_ELEMENTS;
}

inline bool IsSharedArrayElementsKind(ElementsKind kind) {
  return kind == SHARED_ARRAY_ELEMENTS;
}

inline bool IsTerminalElementsKind(ElementsKind kind) {
  return kind == TERMINAL_FAST_ELEMENTS_KIND ||
         IsTypedArrayOrRabGsabTypedArrayElementsKind(kind) ||
         IsRabGsabTypedArrayElementsKind(kind);
}

inline bool IsFastElementsKind(ElementsKind kind) {
  static_assert(FIRST_FAST_ELEMENTS_KIND == 0);
  return kind <= LAST_FAST_ELEMENTS_KIND;
}

inline bool IsTransitionElementsKind(ElementsKind kind) {
  return IsFastElementsKind(kind) ||
         IsTypedArrayOrRabGsabTypedArrayElementsKind(kind) ||
         kind == FAST_SLOPPY_ARGUMENTS_ELEMENTS ||
         kind == FAST_STRING_WRAPPER_ELEMENTS;
}

constexpr bool IsDoubleElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, PACKED_DOUBLE_ELEMENTS, HOLEY_DOUBLE_ELEMENTS);
}

inline bool IsFixedFloatElementsKind(ElementsKind kind) {
  return kind == FLOAT32_ELEMENTS || kind == FLOAT64_ELEMENTS;
}

inline bool IsDoubleOrFloatElementsKind(ElementsKind kind) {
  return IsDoubleElementsKind(kind) || IsFixedFloatElementsKind(kind);
}

// This predicate is used for disabling respective functionality in builtins.
inline bool IsAnyNonextensibleElementsKindUnchecked(ElementsKind kind) {
  return base::IsInRange(kind, FIRST_ANY_NONEXTENSIBLE_ELEMENTS_KIND,
                         LAST_ANY_NONEXTENSIBLE_ELEMENTS_KIND);
}

inline bool IsAnyNonextensibleElementsKind(ElementsKind kind) {
  DCHECK_IMPLIES(IsAnyNonextensibleElementsKindUnchecked(kind),
                 v8_flags.enable_sealed_frozen_elements_kind);
  return IsAnyNonextensibleElementsKindUnchecked(kind);
}

inline bool IsNonextensibleElementsKind(ElementsKind kind) {
  DCHECK_IMPLIES(base::IsInRange(kind, PACKED_NONEXTENSIBLE_ELEMENTS,
                                 HOLEY_NONEXTENSIBLE_ELEMENTS),
                 v8_flags.enable_sealed_frozen_elements_kind);
  return base::IsInRange(kind, PACKED_NONEXTENSIBLE_ELEMENTS,
                         HOLEY_NONEXTENSIBLE_ELEMENTS);
}

inline bool IsSealedElementsKind(ElementsKind kind) {
  DCHECK_IMPLIES(
      base::IsInRange(kind, PACKED_SEALED_ELEMENTS, HOLEY_SEALED_ELEMENTS) ||
          IsSharedArrayElementsKind(kind),
      v8_flags.enable_sealed_frozen_elements_kind);
  return IsSharedArrayElementsKind(kind) ||
         base::IsInRange(kind, PACKED_SEALED_ELEMENTS, HOLEY_SEALED_ELEMENTS);
}

inline bool IsFrozenElementsKind(ElementsKind kind) {
  DCHECK_IMPLIES(
      base::IsInRange(kind, PACKED_FROZEN_ELEMENTS, HOLEY_FROZEN_ELEMENTS),
      v8_flags.enable_sealed_frozen_elements_kind);
  return base::IsInRange(kind, PACKED_FROZEN_ELEMENTS, HOLEY_FROZEN_ELEMENTS);
}

inline bool IsFastOrNonextensibleOrSealedElementsKind(ElementsKind kind) {
  const bool result = kind <= HOLEY_SEALED_ELEMENTS;
  DCHECK_IMPLIES(result, IsFastElementsKind(kind) ||
                             IsNonextensibleElementsKind(kind) ||
                             IsSealedElementsKind(kind));
  DCHECK_IMPLIES(result, !IsFrozenElementsKind(kind));
  return result;
}

inline bool IsSmiOrObjectElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, PACKED_SMI_ELEMENTS, HOLEY_ELEMENTS);
}

constexpr bool IsSmiElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, PACKED_SMI_ELEMENTS, HOLEY_SMI_ELEMENTS);
}

inline bool IsFastNumberElementsKind(ElementsKind kind) {
  return IsSmiElementsKind(kind) || IsDoubleElementsKind(kind);
}

constexpr bool IsObjectElementsKind(ElementsKind kind) {
  return base::IsInRange(kind, PACKED_ELEMENTS, HOLEY_ELEMENTS);
}

inline bool IsAnyHoleyNonextensibleElementsKind(ElementsKind kind) {
  DCHECK_IMPLIES(kind == HOLEY_NONEXTENSIBLE_ELEMENTS ||
                     kind == HOLEY_SEALED_ELEMENTS ||
                     kind == HOLEY_FROZEN_ELEMENTS,
                 v8_flags.enable_sealed_frozen_elements_kind);
  return kind == HOLEY_NONEXTENSIBLE_ELEMENTS ||
         kind == HOLEY_SEALED_ELEMENTS || kind == HOLEY_FROZEN_ELEMENTS;
}

constexpr bool IsHoleyElementsKind(ElementsKind kind) {
  return kind % 2 == 1 && kind <= HOLEY_DOUBLE_ELEMENTS;
}

inline bool IsHoleyElementsKindForRead(ElementsKind kind) {
  return kind % 2 == 1 && kind <= HOLEY_FROZEN_ELEMENTS;
}

inline bool IsHoleyOrDictionaryElementsKind(ElementsKind kind) {
  return IsHoleyElementsKindForRead(kind) || kind == DICTIONARY_ELEMENTS;
}

inline bool IsFastPackedElementsKind(ElementsKind kind) {
  return kind % 2 == 0 && kind <= PACKED_DOUBLE_ELEMENTS;
}

inline ElementsKind GetPackedElementsKind(ElementsKind holey_kind) {
  if (holey_kind == HOLEY_SMI_ELEMENTS) {
    return PACKED_SMI_ELEMENTS;
  }
  if (holey_kind == HOLEY_DOUBLE_ELEMENTS) {
    return PACKED_DOUBLE_ELEMENTS;
  }
  if (holey_kind == HOLEY_ELEMENTS) {
    return PACKED_ELEMENTS;
  }
  return holey_kind;
}

inline ElementsKind GetHoleyElementsKind(ElementsKind packed_kind) {
  if (packed_kind == PACKED_SMI_ELEMENTS) {
    return HOLEY_SMI_ELEMENTS;
  }
  if (packed_kind == PACKED_DOUBLE_ELEMENTS) {
    return HOLEY_DOUBLE_ELEMENTS;
  }
  if (packed_kind == PACKED_ELEMENTS) {
    return HOLEY_ELEMENTS;
  }
  if (packed_kind == PACKED_NONEXTENSIBLE_ELEMENTS) {
    return HOLEY_NONEXTENSIBLE_ELEMENTS;
  }
  return packed_kind;
}

inline ElementsKind GetCorrespondingRabGsabElementsKind(
    ElementsKind typed_array_kind) {
  DCHECK(IsTypedArrayElementsKind(typed_array_kind));
  return ElementsKind(typed_array_kind - FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND +
                      FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND);
}

inline ElementsKind GetCorrespondingNonRabGsabElementsKind(
    ElementsKind typed_array_kind) {
  DCHECK(IsRabGsabTypedArrayElementsKind(typed_array_kind));
  return ElementsKind(typed_array_kind -
                      FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND +
                      FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND);
}

inline bool UnionElementsKindUptoPackedness(ElementsKind* a_out,
                                            ElementsKind b) {
  // Assert that the union of two ElementKinds can be computed via std::max.
  static_assert(PACKED_SMI_ELEMENTS < HOLEY_SMI_ELEMENTS,
                "ElementsKind union not computable via std::max.");
  static_assert(PACKED_ELEMENTS < HOLEY_ELEMENTS,
                "ElementsKind union not computable via std::max.");
  static_assert(PACKED_DOUBLE_ELEMENTS < HOLEY_DOUBLE_ELEMENTS,
                "ElementsKind union not computable via std::max.");
  ElementsKind a = *a_out;
  switch (a) {
    case HOLEY_SMI_ELEMENTS:
    case PACKED_SMI_ELEMENTS:
      if (b == PACKED_SMI_ELEMENTS || b == HOLEY_SMI_ELEMENTS) {
        *a_out = std::max(a, b);
        return true;
      }
      break;
    case PACKED_ELEMENTS:
    case HOLEY_ELEMENTS:
      if (b == PACKED_ELEMENTS || b == HOLEY_ELEMENTS) {
        *a_out = std::max(a, b);
        return true;
      }
      break;
    case PACKED_DOUBLE_ELEMENTS:
    case HOLEY_DOUBLE_ELEMENTS:
      if (b == PACKED_DOUBLE_ELEMENTS || b == HOLEY_DOUBLE_ELEMENTS) {
        *a_out = std::max(a, b);
        return true;
      }
      break;
    default:
      break;
  }
  return false;
}

bool UnionElementsKindUptoSize(ElementsKind* a_out, ElementsKind b);

inline ElementsKind FastSmiToObjectElementsKind(ElementsKind from_kind) {
  DCHECK(IsSmiElementsKind(from_kind));
  return (from_kind == PACKED_SMI_ELEMENTS) ? PACKED_ELEMENTS : HOLEY_ELEMENTS;
}

inline bool IsSimpleMapChangeTransition(ElementsKind from_kind,
                                        ElementsKind to_kind) {
  return (GetHoleyElementsKind(from_kind) == to_kind) ||
         (IsSmiElementsKind(from_kind) && IsObjectElementsKind(to_kind));
}

bool IsMoreGeneralElementsKindTransition(ElementsKind from_kind,
                                         ElementsKind to_kind);

inline ElementsKind GetMoreGeneralElementsKind(ElementsKind from_kind,
                                               ElementsKind to_kind) {
  if (IsMoreGeneralElementsKindTransition(from_kind, to_kind)) {
    return to_kind;
  }
  return from_kind;
}

inline bool IsTransitionableFastElementsKind(ElementsKind from_kind) {
  return IsFastElementsKind(from_kind) &&
         from_kind != TERMINAL_FAST_ELEMENTS_KIND;
}

inline bool ElementsKindEqual(ElementsKind a, ElementsKind b) { return a == b; }

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_ELEMENTS_KIND_H_
