// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/elements-kind.h"

#include "src/base/lazy-instance.h"
#include "src/objects/elements.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

namespace {
constexpr size_t size_to_shift(size_t size) {
  switch (size) {
    case 1:
      return 0;
    case 2:
      return 1;
    case 4:
      return 2;
    case 8:
      return 3;
    default:
      UNREACHABLE();
  }
}
}  // namespace

constexpr uint8_t kTypedArrayAndRabGsabTypedArrayElementsKindShifts[] = {
#define SHIFT(Type, type, TYPE, ctype) size_to_shift(sizeof(ctype)),
    TYPED_ARRAYS(SHIFT) RAB_GSAB_TYPED_ARRAYS(SHIFT)
#undef SHIFT
};

constexpr uint8_t kTypedArrayAndRabGsabTypedArrayElementsKindSizes[] = {
#define SIZE(Type, type, TYPE, ctype) sizeof(ctype),
    TYPED_ARRAYS(SIZE) RAB_GSAB_TYPED_ARRAYS(SIZE)
#undef SIZE
};

#define VERIFY_SHIFT(Type, type, TYPE, ctype)                          \
  static_assert(                                                       \
      kTypedArrayAndRabGsabTypedArrayElementsKindShifts                \
              [ElementsKind::TYPE##_ELEMENTS -                         \
               ElementsKind::FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND] == \
          ElementsKindToShiftSize(ElementsKind::TYPE##_ELEMENTS),      \
      "Shift of ElementsKind::" #TYPE                                  \
      "_ELEMENTS does not match in static table");
TYPED_ARRAYS(VERIFY_SHIFT)
RAB_GSAB_TYPED_ARRAYS(VERIFY_SHIFT)
#undef VERIFY_SHIFT

#define VERIFY_SIZE(Type, type, TYPE, ctype)                           \
  static_assert(                                                       \
      kTypedArrayAndRabGsabTypedArrayElementsKindSizes                 \
              [ElementsKind::TYPE##_ELEMENTS -                         \
               ElementsKind::FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND] == \
          ElementsKindToByteSize(ElementsKind::TYPE##_ELEMENTS),       \
      "Size of ElementsKind::" #TYPE                                   \
      "_ELEMENTS does not match in static table");
TYPED_ARRAYS(VERIFY_SIZE)
RAB_GSAB_TYPED_ARRAYS(VERIFY_SIZE)
#undef VERIFY_SIZE

const uint8_t* TypedArrayAndRabGsabTypedArrayElementsKindShifts() {
  return &kTypedArrayAndRabGsabTypedArrayElementsKindShifts[0];
}

const uint8_t* TypedArrayAndRabGsabTypedArrayElementsKindSizes() {
  return &kTypedArrayAndRabGsabTypedArrayElementsKindSizes[0];
}

int GetDefaultHeaderSizeForElementsKind(ElementsKind elements_kind) {
  static_assert(FixedArray::kHeaderSize == FixedDoubleArray::kHeaderSize);

  if (IsTypedArrayOrRabGsabTypedArrayElementsKind(elements_kind)) {
    return 0;
  } else {
    return FixedArray::kHeaderSize - kHeapObjectTag;
  }
}

const char* ElementsKindToString(ElementsKind kind) {
  switch (kind) {
    case PACKED_SMI_ELEMENTS:
      return "PACKED_SMI_ELEMENTS";
    case HOLEY_SMI_ELEMENTS:
      return "HOLEY_SMI_ELEMENTS";
    case PACKED_ELEMENTS:
      return "PACKED_ELEMENTS";
    case HOLEY_ELEMENTS:
      return "HOLEY_ELEMENTS";
    case PACKED_DOUBLE_ELEMENTS:
      return "PACKED_DOUBLE_ELEMENTS";
    case HOLEY_DOUBLE_ELEMENTS:
      return "HOLEY_DOUBLE_ELEMENTS";
    case PACKED_NONEXTENSIBLE_ELEMENTS:
      return "PACKED_NONEXTENSIBLE_ELEMENTS";
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
      return "HOLEY_NONEXTENSIBLE_ELEMENTS";
    case PACKED_SEALED_ELEMENTS:
      return "PACKED_SEALED_ELEMENTS";
    case HOLEY_SEALED_ELEMENTS:
      return "HOLEY_SEALED_ELEMENTS";
    case PACKED_FROZEN_ELEMENTS:
      return "PACKED_FROZEN_ELEMENTS";
    case HOLEY_FROZEN_ELEMENTS:
      return "HOLEY_FROZEN_ELEMENTS";
    case DICTIONARY_ELEMENTS:
      return "DICTIONARY_ELEMENTS";
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
      return "FAST_SLOPPY_ARGUMENTS_ELEMENTS";
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
      return "SLOW_SLOPPY_ARGUMENTS_ELEMENTS";
    case FAST_STRING_WRAPPER_ELEMENTS:
      return "FAST_STRING_WRAPPER_ELEMENTS";
    case SLOW_STRING_WRAPPER_ELEMENTS:
      return "SLOW_STRING_WRAPPER_ELEMENTS";

#define PRINT_NAME(Type, type, TYPE, _) \
  case TYPE##_ELEMENTS:                 \
    return #TYPE "ELEMENTS";

      TYPED_ARRAYS(PRINT_NAME);
      RAB_GSAB_TYPED_ARRAYS(PRINT_NAME);
#undef PRINT_NAME
    case WASM_ARRAY_ELEMENTS:
      return "WASM_ARRAY_ELEMENTS";
    case SHARED_ARRAY_ELEMENTS:
      return "SHARED_ARRAY_ELEMENTS";
    case NO_ELEMENTS:
      return "NO_ELEMENTS";
  }
  UNREACHABLE();
}

const ElementsKind kFastElementsKindSequence[kFastElementsKindCount] = {
    PACKED_SMI_ELEMENTS,     // 0
    HOLEY_SMI_ELEMENTS,      // 1
    PACKED_DOUBLE_ELEMENTS,  // 2
    HOLEY_DOUBLE_ELEMENTS,   // 3
    PACKED_ELEMENTS,         // 4
    HOLEY_ELEMENTS           // 5
};
static_assert(PACKED_SMI_ELEMENTS == FIRST_FAST_ELEMENTS_KIND);
// Verify that kFastElementsKindPackedToHoley is correct.
static_assert(PACKED_SMI_ELEMENTS + kFastElementsKindPackedToHoley ==
              HOLEY_SMI_ELEMENTS);
static_assert(PACKED_DOUBLE_ELEMENTS + kFastElementsKindPackedToHoley ==
              HOLEY_DOUBLE_ELEMENTS);
static_assert(PACKED_ELEMENTS + kFastElementsKindPackedToHoley ==
              HOLEY_ELEMENTS);

ElementsKind GetFastElementsKindFromSequenceIndex(int sequence_number) {
  DCHECK(sequence_number >= 0 && sequence_number < kFastElementsKindCount);
  return kFastElementsKindSequence[sequence_number];
}

int GetSequenceIndexFromFastElementsKind(ElementsKind elements_kind) {
  for (int i = 0; i < kFastElementsKindCount; ++i) {
    if (kFastElementsKindSequence[i] == elements_kind) {
      return i;
    }
  }
  UNREACHABLE();
}

ElementsKind GetNextTransitionElementsKind(ElementsKind kind) {
  int index = GetSequenceIndexFromFastElementsKind(kind);
  return GetFastElementsKindFromSequenceIndex(index + 1);
}

static inline bool IsFastTransitionTarget(ElementsKind elements_kind) {
  return IsFastElementsKind(elements_kind) ||
         elements_kind == DICTIONARY_ELEMENTS;
}

bool IsMoreGeneralElementsKindTransition(ElementsKind from_kind,
                                         ElementsKind to_kind) {
  if (!IsFastElementsKind(from_kind)) return false;
  if (!IsFastTransitionTarget(to_kind)) return false;
  DCHECK(!IsTypedArrayOrRabGsabTypedArrayElementsKind(from_kind));
  DCHECK(!IsTypedArrayOrRabGsabTypedArrayElementsKind(to_kind));
  switch (from_kind) {
    case PACKED_SMI_ELEMENTS:
      return to_kind != PACKED_SMI_ELEMENTS;
    case HOLEY_SMI_ELEMENTS:
      return to_kind != PACKED_SMI_ELEMENTS && to_kind != HOLEY_SMI_ELEMENTS;
    case PACKED_DOUBLE_ELEMENTS:
      return to_kind != PACKED_SMI_ELEMENTS && to_kind != HOLEY_SMI_ELEMENTS &&
             to_kind != PACKED_DOUBLE_ELEMENTS;
    case HOLEY_DOUBLE_ELEMENTS:
      return to_kind == PACKED_ELEMENTS || to_kind == HOLEY_ELEMENTS;
    case PACKED_ELEMENTS:
      return to_kind == HOLEY_ELEMENTS;
    case HOLEY_ELEMENTS:
      return false;
    default:
      return false;
  }
}

bool UnionElementsKindUptoSize(ElementsKind* a_out, ElementsKind b) {
  // Assert that the union of two ElementKinds can be computed via std::max.
  static_assert(PACKED_SMI_ELEMENTS < HOLEY_SMI_ELEMENTS,
                "ElementsKind union not computable via std::max.");
  static_assert(HOLEY_SMI_ELEMENTS < PACKED_ELEMENTS,
                "ElementsKind union not computable via std::max.");
  static_assert(PACKED_ELEMENTS < HOLEY_ELEMENTS,
                "ElementsKind union not computable via std::max.");
  static_assert(PACKED_DOUBLE_ELEMENTS < HOLEY_DOUBLE_ELEMENTS,
                "ElementsKind union not computable via std::max.");
  ElementsKind a = *a_out;
  switch (a) {
    case PACKED_SMI_ELEMENTS:
      switch (b) {
        case PACKED_SMI_ELEMENTS:
        case HOLEY_SMI_ELEMENTS:
        case PACKED_ELEMENTS:
        case HOLEY_ELEMENTS:
          *a_out = b;
          return true;
        default:
          return false;
      }
    case HOLEY_SMI_ELEMENTS:
      switch (b) {
        case PACKED_SMI_ELEMENTS:
        case HOLEY_SMI_ELEMENTS:
          *a_out = HOLEY_SMI_ELEMENTS;
          return true;
        case PACKED_ELEMENTS:
        case HOLEY_ELEMENTS:
          *a_out = HOLEY_ELEMENTS;
          return true;
        default:
          return false;
      }
    case PACKED_ELEMENTS:
      switch (b) {
        case PACKED_SMI_ELEMENTS:
        case PACKED_ELEMENTS:
          *a_out = PACKED_ELEMENTS;
          return true;
        case HOLEY_SMI_ELEMENTS:
        case HOLEY_ELEMENTS:
          *a_out = HOLEY_ELEMENTS;
          return true;
        default:
          return false;
      }
    case HOLEY_ELEMENTS:
      switch (b) {
        case PACKED_SMI_ELEMENTS:
        case HOLEY_SMI_ELEMENTS:
        case PACKED_ELEMENTS:
        case HOLEY_ELEMENTS:
          *a_out = HOLEY_ELEMENTS;
          return true;
        default:
          return false;
      }
    case PACKED_DOUBLE_ELEMENTS:
      switch (b) {
        case PACKED_DOUBLE_ELEMENTS:
        case HOLEY_DOUBLE_ELEMENTS:
          *a_out = b;
          return true;
        default:
          return false;
      }
    case HOLEY_DOUBLE_ELEMENTS:
      switch (b) {
        case PACKED_DOUBLE_ELEMENTS:
        case HOLEY_DOUBLE_ELEMENTS:
          *a_out = HOLEY_DOUBLE_ELEMENTS;
          return true;
        default:
          return false;
      }
    default:
      break;
  }
  return false;
}

}  // namespace internal
}  // namespace v8
