// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/elements.h"

#include "src/base/atomicops.h"
#include "src/base/safe_conversions.h"
#include "src/common/message-template.h"
#include "src/execution/arguments.h"
#include "src/execution/frames.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/protectors-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"  // For MaxNumberToStringCacheSize.
#include "src/heap/heap-write-barrier-inl.h"
#include "src/numbers/conversions.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-shared-array-inl.h"
#include "src/objects/keys.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots-atomic-inl.h"
#include "src/objects/slots.h"
#include "src/utils/utils.h"
#include "third_party/fp16/src/include/fp16.h"

// Each concrete ElementsAccessor can handle exactly one ElementsKind,
// several abstract ElementsAccessor classes are used to allow sharing
// common code.
//
// Inheritance hierarchy:
// - ElementsAccessorBase                        (abstract)
//   - FastElementsAccessor                      (abstract)
//     - FastSmiOrObjectElementsAccessor
//       - FastPackedSmiElementsAccessor
//       - FastHoleySmiElementsAccessor
//       - FastPackedObjectElementsAccessor
//       - FastNonextensibleObjectElementsAccessor: template
//         - FastPackedNonextensibleObjectElementsAccessor
//         - FastHoleyNonextensibleObjectElementsAccessor
//       - FastSealedObjectElementsAccessor: template
//         - FastPackedSealedObjectElementsAccessor
//         - FastHoleySealedObjectElementsAccessor
//       - FastFrozenObjectElementsAccessor: template
//         - FastPackedFrozenObjectElementsAccessor
//         - FastHoleyFrozenObjectElementsAccessor
//       - FastHoleyObjectElementsAccessor
//     - FastDoubleElementsAccessor
//       - FastPackedDoubleElementsAccessor
//       - FastHoleyDoubleElementsAccessor
//   - TypedElementsAccessor: template, with instantiations:
//     - Uint8ElementsAccessor
//     - Int8ElementsAccessor
//     - Uint16ElementsAccessor
//     - Int16ElementsAccessor
//     - Uint32ElementsAccessor
//     - Int32ElementsAccessor
//     - Float32ElementsAccessor
//     - Float64ElementsAccessor
//     - Uint8ClampedElementsAccessor
//     - BigUint64ElementsAccessor
//     - BigInt64ElementsAccessor
//     - RabGsabUint8ElementsAccessor
//     - RabGsabInt8ElementsAccessor
//     - RabGsabUint16ElementsAccessor
//     - RabGsabInt16ElementsAccessor
//     - RabGsabUint32ElementsAccessor
//     - RabGsabInt32ElementsAccessor
//     - RabGsabFloat32ElementsAccessor
//     - RabGsabFloat64ElementsAccessor
//     - RabGsabUint8ClampedElementsAccessor
//     - RabGsabBigUint64ElementsAccessor
//     - RabGsabBigInt64ElementsAccessor
//   - DictionaryElementsAccessor
//   - SloppyArgumentsElementsAccessor
//     - FastSloppyArgumentsElementsAccessor
//     - SlowSloppyArgumentsElementsAccessor
//   - StringWrapperElementsAccessor
//     - FastStringWrapperElementsAccessor
//     - SlowStringWrapperElementsAccessor

namespace v8 {
namespace internal {

namespace {

#define RETURN_NOTHING_IF_NOT_SUCCESSFUL(call) \
  do {                                         \
    if (!(call)) return Nothing<bool>();       \
  } while (false)

#define RETURN_FAILURE_IF_NOT_SUCCESSFUL(call)          \
  do {                                                  \
    ExceptionStatus status_enum_result = (call);        \
    if (!status_enum_result) return status_enum_result; \
  } while (false)

static const int kPackedSizeNotKnown = -1;

enum Where { AT_START, AT_END };

// First argument in list is the accessor class, the second argument is the
// accessor ElementsKind, and the third is the backing store class.  Use the
// fast element handler for smi-only arrays.  The implementation is currently
// identical.  Note that the order must match that of the ElementsKind enum for
// the |accessor_array[]| below to work.
#define ELEMENTS_LIST(V)                                                      \
  V(FastPackedSmiElementsAccessor, PACKED_SMI_ELEMENTS, FixedArray)           \
  V(FastHoleySmiElementsAccessor, HOLEY_SMI_ELEMENTS, FixedArray)             \
  V(FastPackedObjectElementsAccessor, PACKED_ELEMENTS, FixedArray)            \
  V(FastHoleyObjectElementsAccessor, HOLEY_ELEMENTS, FixedArray)              \
  V(FastPackedDoubleElementsAccessor, PACKED_DOUBLE_ELEMENTS,                 \
    FixedDoubleArray)                                                         \
  V(FastHoleyDoubleElementsAccessor, HOLEY_DOUBLE_ELEMENTS, FixedDoubleArray) \
  V(FastPackedNonextensibleObjectElementsAccessor,                            \
    PACKED_NONEXTENSIBLE_ELEMENTS, FixedArray)                                \
  V(FastHoleyNonextensibleObjectElementsAccessor,                             \
    HOLEY_NONEXTENSIBLE_ELEMENTS, FixedArray)                                 \
  V(FastPackedSealedObjectElementsAccessor, PACKED_SEALED_ELEMENTS,           \
    FixedArray)                                                               \
  V(FastHoleySealedObjectElementsAccessor, HOLEY_SEALED_ELEMENTS, FixedArray) \
  V(FastPackedFrozenObjectElementsAccessor, PACKED_FROZEN_ELEMENTS,           \
    FixedArray)                                                               \
  V(FastHoleyFrozenObjectElementsAccessor, HOLEY_FROZEN_ELEMENTS, FixedArray) \
  V(SharedArrayElementsAccessor, SHARED_ARRAY_ELEMENTS, FixedArray)           \
  V(DictionaryElementsAccessor, DICTIONARY_ELEMENTS, NumberDictionary)        \
  V(FastSloppyArgumentsElementsAccessor, FAST_SLOPPY_ARGUMENTS_ELEMENTS,      \
    FixedArray)                                                               \
  V(SlowSloppyArgumentsElementsAccessor, SLOW_SLOPPY_ARGUMENTS_ELEMENTS,      \
    FixedArray)                                                               \
  V(FastStringWrapperElementsAccessor, FAST_STRING_WRAPPER_ELEMENTS,          \
    FixedArray)                                                               \
  V(SlowStringWrapperElementsAccessor, SLOW_STRING_WRAPPER_ELEMENTS,          \
    FixedArray)                                                               \
  V(Uint8ElementsAccessor, UINT8_ELEMENTS, ByteArray)                         \
  V(Int8ElementsAccessor, INT8_ELEMENTS, ByteArray)                           \
  V(Uint16ElementsAccessor, UINT16_ELEMENTS, ByteArray)                       \
  V(Int16ElementsAccessor, INT16_ELEMENTS, ByteArray)                         \
  V(Uint32ElementsAccessor, UINT32_ELEMENTS, ByteArray)                       \
  V(Int32ElementsAccessor, INT32_ELEMENTS, ByteArray)                         \
  V(Float32ElementsAccessor, FLOAT32_ELEMENTS, ByteArray)                     \
  V(Float64ElementsAccessor, FLOAT64_ELEMENTS, ByteArray)                     \
  V(Uint8ClampedElementsAccessor, UINT8_CLAMPED_ELEMENTS, ByteArray)          \
  V(BigUint64ElementsAccessor, BIGUINT64_ELEMENTS, ByteArray)                 \
  V(BigInt64ElementsAccessor, BIGINT64_ELEMENTS, ByteArray)                   \
  V(Float16ElementsAccessor, FLOAT16_ELEMENTS, ByteArray)                     \
  V(RabGsabUint8ElementsAccessor, RAB_GSAB_UINT8_ELEMENTS, ByteArray)         \
  V(RabGsabInt8ElementsAccessor, RAB_GSAB_INT8_ELEMENTS, ByteArray)           \
  V(RabGsabUint16ElementsAccessor, RAB_GSAB_UINT16_ELEMENTS, ByteArray)       \
  V(RabGsabInt16ElementsAccessor, RAB_GSAB_INT16_ELEMENTS, ByteArray)         \
  V(RabGsabUint32ElementsAccessor, RAB_GSAB_UINT32_ELEMENTS, ByteArray)       \
  V(RabGsabInt32ElementsAccessor, RAB_GSAB_INT32_ELEMENTS, ByteArray)         \
  V(RabGsabFloat32ElementsAccessor, RAB_GSAB_FLOAT32_ELEMENTS, ByteArray)     \
  V(RabGsabFloat64ElementsAccessor, RAB_GSAB_FLOAT64_ELEMENTS, ByteArray)     \
  V(RabGsabUint8ClampedElementsAccessor, RAB_GSAB_UINT8_CLAMPED_ELEMENTS,     \
    ByteArray)                                                                \
  V(RabGsabBigUint64ElementsAccessor, RAB_GSAB_BIGUINT64_ELEMENTS, ByteArray) \
  V(RabGsabBigInt64ElementsAccessor, RAB_GSAB_BIGINT64_ELEMENTS, ByteArray)   \
  V(RabGsabFloat16ElementsAccessor, RAB_GSAB_FLOAT16_ELEMENTS, ByteArray)

template <ElementsKind Kind>
class ElementsKindTraits {
 public:
  using BackingStore = FixedArrayBase;
};

#define ELEMENTS_TRAITS(Class, KindParam, Store)    \
  template <>                                       \
  class ElementsKindTraits<KindParam> {             \
   public: /* NOLINT */                             \
    static constexpr ElementsKind Kind = KindParam; \
    using BackingStore = Store;                     \
  };                                                \
  constexpr ElementsKind ElementsKindTraits<KindParam>::Kind;
ELEMENTS_LIST(ELEMENTS_TRAITS)
#undef ELEMENTS_TRAITS

V8_WARN_UNUSED_RESULT
MaybeHandle<Object> ThrowArrayLengthRangeError(Isolate* isolate) {
  THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidArrayLength));
}

WriteBarrierMode GetWriteBarrierMode(Tagged<FixedArrayBase> elements,
                                     ElementsKind kind,
                                     const DisallowGarbageCollection& promise) {
  if (IsSmiElementsKind(kind)) return SKIP_WRITE_BARRIER;
  if (IsDoubleElementsKind(kind)) return SKIP_WRITE_BARRIER;
  return elements->GetWriteBarrierMode(promise);
}

// If kCopyToEndAndInitializeToHole is specified as the copy_size to
// CopyElements, it copies all of elements from source after source_start to
// destination array, padding any remaining uninitialized elements in the
// destination array with the hole.
constexpr int kCopyToEndAndInitializeToHole = -1;

void CopyObjectToObjectElements(Isolate* isolate,
                                Tagged<FixedArrayBase> from_base,
                                ElementsKind from_kind, uint32_t from_start,
                                Tagged<FixedArrayBase> to_base,
                                ElementsKind to_kind, uint32_t to_start,
                                int raw_copy_size) {
  ReadOnlyRoots roots(isolate);
  DCHECK(to_base->map() != roots.fixed_cow_array_map());
  DisallowGarbageCollection no_gc;
  int copy_size = raw_copy_size;
  if (raw_copy_size < 0) {
    DCHECK_EQ(kCopyToEndAndInitializeToHole, raw_copy_size);
    copy_size = std::min(from_base->length() - from_start,
                         to_base->length() - to_start);
    int start = to_start + copy_size;
    int length = to_base->length() - start;
    if (length > 0) {
      MemsetTagged(Cast<FixedArray>(to_base)->RawFieldOfElementAt(start),
                   roots.the_hole_value(), length);
    }
  }
  DCHECK((copy_size + static_cast<int>(to_start)) <= to_base->length() &&
         (copy_size + static_cast<int>(from_start)) <= from_base->length());
  if (copy_size == 0) return;
  Tagged<FixedArray> from = Cast<FixedArray>(from_base);
  Tagged<FixedArray> to = Cast<FixedArray>(to_base);
  DCHECK(IsSmiOrObjectElementsKind(from_kind));
  DCHECK(IsSmiOrObjectElementsKind(to_kind));

  WriteBarrierMode write_barrier_mode =
      (IsObjectElementsKind(from_kind) && IsObjectElementsKind(to_kind))
          ? UPDATE_WRITE_BARRIER
          : SKIP_WRITE_BARRIER;
  to->CopyElements(isolate, to_start, from, from_start, copy_size,
                   write_barrier_mode);
}

void CopyDictionaryToObjectElements(Isolate* isolate,
                                    Tagged<FixedArrayBase> from_base,
                                    uint32_t from_start,
                                    Tagged<FixedArrayBase> to_base,
                                    ElementsKind to_kind, uint32_t to_start,
                                    int raw_copy_size) {
  DisallowGarbageCollection no_gc;
  Tagged<NumberDictionary> from = Cast<NumberDictionary>(from_base);
  int copy_size = raw_copy_size;
  if (raw_copy_size < 0) {
    DCHECK_EQ(kCopyToEndAndInitializeToHole, raw_copy_size);
    copy_size = from->max_number_key() + 1 - from_start;
    int start = to_start + copy_size;
    int length = to_base->length() - start;
    if (length > 0) {
      MemsetTagged(Cast<FixedArray>(to_base)->RawFieldOfElementAt(start),
                   ReadOnlyRoots(isolate).the_hole_value(), length);
    }
  }
  DCHECK(to_base != from_base);
  DCHECK(IsSmiOrObjectElementsKind(to_kind));
  if (copy_size == 0) return;
  Tagged<FixedArray> to = Cast<FixedArray>(to_base);
  uint32_t to_length = to->length();
  if (to_start + copy_size > to_length) {
    copy_size = to_length - to_start;
  }
  WriteBarrierMode write_barrier_mode = GetWriteBarrierMode(to, to_kind, no_gc);
  for (int i = 0; i < copy_size; i++) {
    InternalIndex entry = from->FindEntry(isolate, i + from_start);
    if (entry.is_found()) {
      Tagged<Object> value = from->ValueAt(entry);
      DCHECK(!IsTheHole(value, isolate));
      to->set(i + to_start, value, write_barrier_mode);
    } else {
      to->set_the_hole(isolate, i + to_start);
    }
  }
}

// NOTE: this method violates the handlified function signature convention:
// raw pointer parameters in the function that allocates.
// See ElementsAccessorBase::CopyElements() for details.
void CopyDoubleToObjectElements(Isolate* isolate,
                                Tagged<FixedArrayBase> from_base,
                                uint32_t from_start,
                                Tagged<FixedArrayBase> to_base,
                                uint32_t to_start, int raw_copy_size) {
  int copy_size = raw_copy_size;
  if (raw_copy_size < 0) {
    DisallowGarbageCollection no_gc;
    DCHECK_EQ(kCopyToEndAndInitializeToHole, raw_copy_size);
    copy_size = std::min(from_base->length() - from_start,
                         to_base->length() - to_start);
    // Also initialize the area that will be copied over since HeapNumber
    // allocation below can cause an incremental marking step, requiring all
    // existing heap objects to be propertly initialized.
    int start = to_start;
    int length = to_base->length() - start;
    if (length > 0) {
      MemsetTagged(Cast<FixedArray>(to_base)->RawFieldOfElementAt(start),
                   ReadOnlyRoots(isolate).the_hole_value(), length);
    }
  }

  DCHECK((copy_size + static_cast<int>(to_start)) <= to_base->length() &&
         (copy_size + static_cast<int>(from_start)) <= from_base->length());
  if (copy_size == 0) return;

  // From here on, the code below could actually allocate. Therefore the raw
  // values are wrapped into handles.
  DirectHandle<FixedDoubleArray> from(Cast<FixedDoubleArray>(from_base),
                                      isolate);
  DirectHandle<FixedArray> to(Cast<FixedArray>(to_base), isolate);

  // Use an outer loop to not waste too much time on creating HandleScopes.
  // On the other hand we might overflow a single handle scope depending on
  // the copy_size.
  int offset = 0;
  while (offset < copy_size) {
    HandleScope scope(isolate);
    offset += 100;
    for (int i = offset - 100; i < offset && i < copy_size; ++i) {
      DirectHandle<Object> value =
          FixedDoubleArray::get(*from, i + from_start, isolate);
      to->set(i + to_start, *value, UPDATE_WRITE_BARRIER);
    }
  }
}

void CopyDoubleToDoubleElements(Tagged<FixedArrayBase> from_base,
                                uint32_t from_start,
                                Tagged<FixedArrayBase> to_base,
                                uint32_t to_start, int raw_copy_size) {
  DisallowGarbageCollection no_gc;
  int copy_size = raw_copy_size;
  if (raw_copy_size < 0) {
    DCHECK_EQ(kCopyToEndAndInitializeToHole, raw_copy_size);
    copy_size = std::min(from_base->length() - from_start,
                         to_base->length() - to_start);
    for (int i = to_start + copy_size; i < to_base->length(); ++i) {
      Cast<FixedDoubleArray>(to_base)->set_the_hole(i);
    }
  }
  DCHECK((copy_size + static_cast<int>(to_start)) <= to_base->length() &&
         (copy_size + static_cast<int>(from_start)) <= from_base->length());
  if (copy_size == 0) return;
  Tagged<FixedDoubleArray> from = Cast<FixedDoubleArray>(from_base);
  Tagged<FixedDoubleArray> to = Cast<FixedDoubleArray>(to_base);
  Address to_address = to.address() + FixedDoubleArray::kHeaderSize;
  Address from_address = from.address() + FixedDoubleArray::kHeaderSize;
  to_address += kDoubleSize * to_start;
  from_address += kDoubleSize * from_start;
#ifdef V8_COMPRESS_POINTERS
  // TODO(ishell, v8:8875): we use CopyTagged() in order to avoid unaligned
  // access to double values in the arrays. This will no longed be necessary
  // once the allocations alignment issue is fixed.
  int words_per_double = (kDoubleSize / kTaggedSize);
  CopyTagged(to_address, from_address,
             static_cast<size_t>(words_per_double * copy_size));
#else
  int words_per_double = (kDoubleSize / kSystemPointerSize);
  CopyWords(to_address, from_address,
            static_cast<size_t>(words_per_double * copy_size));
#endif
}

void CopySmiToDoubleElements(Tagged<FixedArrayBase> from_base,
                             uint32_t from_start,
                             Tagged<FixedArrayBase> to_base, uint32_t to_start,
                             int raw_copy_size) {
  DisallowGarbageCollection no_gc;
  int copy_size = raw_copy_size;
  if (raw_copy_size < 0) {
    DCHECK_EQ(kCopyToEndAndInitializeToHole, raw_copy_size);
    copy_size = from_base->length() - from_start;
    for (int i = to_start + copy_size; i < to_base->length(); ++i) {
      Cast<FixedDoubleArray>(to_base)->set_the_hole(i);
    }
  }
  DCHECK((copy_size + static_cast<int>(to_start)) <= to_base->length() &&
         (copy_size + static_cast<int>(from_start)) <= from_base->length());
  if (copy_size == 0) return;
  Tagged<FixedArray> from = Cast<FixedArray>(from_base);
  Tagged<FixedDoubleArray> to = Cast<FixedDoubleArray>(to_base);
  Tagged<Object> the_hole = from->GetReadOnlyRoots().the_hole_value();
  for (uint32_t from_end = from_start + static_cast<uint32_t>(copy_size);
       from_start < from_end; from_start++, to_start++) {
    Tagged<Object> hole_or_smi = from->get(from_start);
    if (hole_or_smi == the_hole) {
      to->set_the_hole(to_start);
    } else {
      to->set(to_start, Smi::ToInt(hole_or_smi));
    }
  }
}

void CopyPackedSmiToDoubleElements(Tagged<FixedArrayBase> from_base,
                                   uint32_t from_start,
                                   Tagged<FixedArrayBase> to_base,
                                   uint32_t to_start, int packed_size,
                                   int raw_copy_size) {
  DisallowGarbageCollection no_gc;
  int copy_size = raw_copy_size;
  uint32_t to_end;
  if (raw_copy_size < 0) {
    DCHECK_EQ(kCopyToEndAndInitializeToHole, raw_copy_size);
    copy_size = packed_size - from_start;
    to_end = to_base->length();
    for (uint32_t i = to_start + copy_size; i < to_end; ++i) {
      Cast<FixedDoubleArray>(to_base)->set_the_hole(i);
    }
  } else {
    to_end = to_start + static_cast<uint32_t>(copy_size);
  }
  DCHECK(static_cast<int>(to_end) <= to_base->length());
  DCHECK(packed_size >= 0 && packed_size <= copy_size);
  DCHECK((copy_size + static_cast<int>(to_start)) <= to_base->length() &&
         (copy_size + static_cast<int>(from_start)) <= from_base->length());
  if (copy_size == 0) return;
  Tagged<FixedArray> from = Cast<FixedArray>(from_base);
  Tagged<FixedDoubleArray> to = Cast<FixedDoubleArray>(to_base);
  for (uint32_t from_end = from_start + static_cast<uint32_t>(packed_size);
       from_start < from_end; from_start++, to_start++) {
    Tagged<Object> smi = from->get(from_start);
    DCHECK(!IsTheHole(smi));
    to->set(to_start, Smi::ToInt(smi));
  }
}

void CopyObjectToDoubleElements(Tagged<FixedArrayBase> from_base,
                                uint32_t from_start,
                                Tagged<FixedArrayBase> to_base,
                                uint32_t to_start, int raw_copy_size) {
  DisallowGarbageCollection no_gc;
  int copy_size = raw_copy_size;
  if (raw_copy_size < 0) {
    DCHECK_EQ(kCopyToEndAndInitializeToHole, raw_copy_size);
    copy_size = from_base->length() - from_start;
    for (int i = to_start + copy_size; i < to_base->length(); ++i) {
      Cast<FixedDoubleArray>(to_base)->set_the_hole(i);
    }
  }
  DCHECK((copy_size + static_cast<int>(to_start)) <= to_base->length() &&
         (copy_size + static_cast<int>(from_start)) <= from_base->length());
  if (copy_size == 0) return;
  Tagged<FixedArray> from = Cast<FixedArray>(from_base);
  Tagged<FixedDoubleArray> to = Cast<FixedDoubleArray>(to_base);
  Tagged<Hole> the_hole = from->GetReadOnlyRoots().the_hole_value();
  for (uint32_t from_end = from_start + copy_size; from_start < from_end;
       from_start++, to_start++) {
    Tagged<Object> hole_or_object = from->get(from_start);
    if (hole_or_object == the_hole) {
      to->set_the_hole(to_start);
    } else {
      to->set(to_start, Object::NumberValue(Cast<Number>(hole_or_object)));
    }
  }
}

void CopyDictionaryToDoubleElements(Isolate* isolate,
                                    Tagged<FixedArrayBase> from_base,
                                    uint32_t from_start,
                                    Tagged<FixedArrayBase> to_base,
                                    uint32_t to_start, int raw_copy_size) {
  DisallowGarbageCollection no_gc;
  Tagged<NumberDictionary> from = Cast<NumberDictionary>(from_base);
  int copy_size = raw_copy_size;
  if (copy_size < 0) {
    DCHECK_EQ(kCopyToEndAndInitializeToHole, copy_size);
    copy_size = from->max_number_key() + 1 - from_start;
    for (int i = to_start + copy_size; i < to_base->length(); ++i) {
      Cast<FixedDoubleArray>(to_base)->set_the_hole(i);
    }
  }
  if (copy_size == 0) return;
  Tagged<FixedDoubleArray> to = Cast<FixedDoubleArray>(to_base);
  uint32_t to_length = to->length();
  if (to_start + copy_size > to_length) {
    copy_size = to_length - to_start;
  }
  for (int i = 0; i < copy_size; i++) {
    InternalIndex entry = from->FindEntry(isolate, i + from_start);
    if (entry.is_found()) {
      to->set(i + to_start,
              Object::NumberValue(Cast<Number>(from->ValueAt(entry))));
    } else {
      to->set_the_hole(i + to_start);
    }
  }
}

void SortIndices(Isolate* isolate, DirectHandle<FixedArray> indices,
                 uint32_t sort_size) {
  if (sort_size == 0) return;

  // Use AtomicSlot wrapper to ensure that std::sort uses atomic load and
  // store operations that are safe for concurrent marking.
  AtomicSlot start(indices->RawFieldOfFirstElement());
  AtomicSlot end(start + sort_size);
  std::sort(start, end, [isolate](Tagged_t elementA, Tagged_t elementB) {
#ifdef V8_COMPRESS_POINTERS
    Tagged<Object> a(
        V8HeapCompressionScheme::DecompressTagged(isolate, elementA));
    Tagged<Object> b(
        V8HeapCompressionScheme::DecompressTagged(isolate, elementB));
#else
    Tagged<Object> a(elementA);
    Tagged<Object> b(elementB);
#endif
    if (IsSmi(a) || !IsUndefined(a, isolate)) {
      if (!IsSmi(b) && IsUndefined(b, isolate)) {
        return true;
      }
      return Object::NumberValue(Cast<Number>(a)) <
             Object::NumberValue(Cast<Number>(b));
    }
    return !IsSmi(b) && IsUndefined(b, isolate);
  });
  isolate->heap()->WriteBarrierForRange(*indices, ObjectSlot(start),
                                        ObjectSlot(end));
}

Maybe<bool> IncludesValueSlowPath(Isolate* isolate, Handle<JSObject> receiver,
                                  DirectHandle<Object> value, size_t start_from,
                                  size_t length) {
  bool search_for_hole = IsUndefined(*value, isolate);
  for (size_t k = start_from; k < length; ++k) {
    LookupIterator it(isolate, receiver, k);
    if (!it.IsFound()) {
      if (search_for_hole) return Just(true);
      continue;
    }
    DirectHandle<Object> element_k;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, element_k,
                                     Object::GetProperty(&it), Nothing<bool>());

    if (Object::SameValueZero(*value, *element_k)) return Just(true);
  }

  return Just(false);
}

Maybe<int64_t> IndexOfValueSlowPath(Isolate* isolate, Handle<JSObject> receiver,
                                    DirectHandle<Object> value,
                                    size_t start_from, size_t length) {
  for (size_t k = start_from; k < length; ++k) {
    LookupIterator it(isolate, receiver, k);
    if (!it.IsFound()) {
      continue;
    }
    DirectHandle<Object> element_k;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, element_k, Object::GetProperty(&it), Nothing<int64_t>());

    if (Object::StrictEquals(*value, *element_k)) return Just<int64_t>(k);
  }

  return Just<int64_t>(-1);
}

// The InternalElementsAccessor is a helper class to expose otherwise protected
// methods to its subclasses. Namely, we don't want to publicly expose methods
// that take an entry (instead of an index) as an argument.
class InternalElementsAccessor : public ElementsAccessor {
 public:
  InternalIndex GetEntryForIndex(Isolate* isolate, Tagged<JSObject> holder,
                                 Tagged<FixedArrayBase> backing_store,
                                 size_t index) override = 0;

  PropertyDetails GetDetails(Tagged<JSObject> holder,
                             InternalIndex entry) override = 0;
};

// Base class for element handler implementations. Contains the
// the common logic for objects with different ElementsKinds.
// Subclasses must specialize method for which the element
// implementation differs from the base class implementation.
//
// This class is intended to be used in the following way:
//
//   class SomeElementsAccessor :
//       public ElementsAccessorBase<SomeElementsAccessor,
//                                   BackingStoreClass> {
//     ...
//   }
//
// This is an example of the Curiously Recurring Template Pattern (see
// http://en.wikipedia.org/wiki/Curiously_recurring_template_pattern).  We use
// CRTP to guarantee aggressive compile time optimizations (i.e.  inlining and
// specialization of SomeElementsAccessor methods).
template <typename Subclass, typename ElementsTraitsParam>
class ElementsAccessorBase : public InternalElementsAccessor {
 public:
  ElementsAccessorBase() = default;
  ElementsAccessorBase(const ElementsAccessorBase&) = delete;
  ElementsAccessorBase& operator=(const ElementsAccessorBase&) = delete;

  using ElementsTraits = ElementsTraitsParam;
  using BackingStore = typename ElementsTraitsParam::BackingStore;

  static ElementsKind kind() { return ElementsTraits::Kind; }

  static void ValidateContents(Tagged<JSObject> holder, size_t length) {}

  static void ValidateImpl(Tagged<JSObject> holder) {
    Tagged<FixedArrayBase> fixed_array_base = holder->elements();
    if (!IsHeapObject(fixed_array_base)) return;
    // Arrays that have been shifted in place can't be verified.
    if (IsFreeSpaceOrFiller(fixed_array_base)) return;
    size_t length = 0;
    if (IsJSArray(holder)) {
      Tagged<Object> length_obj = Cast<JSArray>(holder)->length();
      if (IsSmi(length_obj)) {
        length = Smi::ToInt(length_obj);
      }
    } else if (IsJSTypedArray(holder)) {
      length = Cast<JSTypedArray>(holder)->length();
    } else {
      length = fixed_array_base->length();
    }
    Subclass::ValidateContents(holder, length);
  }

  void Validate(Tagged<JSObject> holder) final {
    DisallowGarbageCollection no_gc;
    Subclass::ValidateImpl(holder);
  }

  bool HasElement(Tagged<JSObject> holder, uint32_t index,
                  Tagged<FixedArrayBase> backing_store,
                  PropertyFilter filter) final {
    return Subclass::HasElementImpl(holder->GetIsolate(), holder, index,
                                    backing_store, filter);
  }

  static bool HasElementImpl(Isolate* isolate, Tagged<JSObject> holder,
                             size_t index, Tagged<FixedArrayBase> backing_store,
                             PropertyFilter filter = ALL_PROPERTIES) {
    return Subclass::GetEntryForIndexImpl(isolate, holder, backing_store, index,
                                          filter)
        .is_found();
  }

  bool HasEntry(Tagged<JSObject> holder, InternalIndex entry) final {
    return Subclass::HasEntryImpl(holder->GetIsolate(), holder->elements(),
                                  entry);
  }

  static bool HasEntryImpl(Isolate* isolate,
                           Tagged<FixedArrayBase> backing_store,
                           InternalIndex entry) {
    UNIMPLEMENTED();
  }

  bool HasAccessors(Tagged<JSObject> holder) final {
    return Subclass::HasAccessorsImpl(holder, holder->elements());
  }

  static bool HasAccessorsImpl(Tagged<JSObject> holder,
                               Tagged<FixedArrayBase> backing_store) {
    return false;
  }

  Handle<Object> Get(Isolate* isolate, Handle<JSObject> holder,
                     InternalIndex entry) final {
    return Subclass::GetInternalImpl(isolate, holder, entry);
  }

  static Handle<Object> GetInternalImpl(Isolate* isolate,
                                        DirectHandle<JSObject> holder,
                                        InternalIndex entry) {
    return Subclass::GetImpl(isolate, holder->elements(), entry);
  }

  static Handle<Object> GetImpl(Isolate* isolate,
                                Tagged<FixedArrayBase> backing_store,
                                InternalIndex entry) {
    return handle(Cast<BackingStore>(backing_store)->get(entry.as_int()),
                  isolate);
  }

  Handle<Object> GetAtomic(Isolate* isolate, Handle<JSObject> holder,
                           InternalIndex entry, SeqCstAccessTag tag) final {
    return Subclass::GetAtomicInternalImpl(isolate, holder->elements(), entry,
                                           tag);
  }

  static Handle<Object> GetAtomicInternalImpl(
      Isolate* isolate, Tagged<FixedArrayBase> backing_store,
      InternalIndex entry, SeqCstAccessTag tag) {
    UNREACHABLE();
  }

  void SetAtomic(Handle<JSObject> holder, InternalIndex entry,
                 Tagged<Object> value, SeqCstAccessTag tag) final {
    Subclass::SetAtomicInternalImpl(holder->elements(), entry, value, tag);
  }

  static void SetAtomicInternalImpl(Tagged<FixedArrayBase> backing_store,
                                    InternalIndex entry, Tagged<Object> value,
                                    SeqCstAccessTag tag) {
    UNREACHABLE();
  }

  Handle<Object> SwapAtomic(Isolate* isolate, Handle<JSObject> holder,
                            InternalIndex entry, Tagged<Object> value,
                            SeqCstAccessTag tag) final {
    return Subclass::SwapAtomicInternalImpl(isolate, holder->elements(), entry,
                                            value, tag);
  }

  static Handle<Object> SwapAtomicInternalImpl(
      Isolate* isolate, Tagged<FixedArrayBase> backing_store,
      InternalIndex entry, Tagged<Object> value, SeqCstAccessTag tag) {
    UNREACHABLE();
  }

  Handle<Object> CompareAndSwapAtomic(Isolate* isolate, Handle<JSObject> holder,
                                      InternalIndex entry,
                                      Tagged<Object> expected,
                                      Tagged<Object> value,
                                      SeqCstAccessTag tag) final {
    return handle(HeapObject::SeqCst_CompareAndSwapField(
                      expected, value,
                      [=](Tagged<Object> expected_value,
                          Tagged<Object> new_value) {
                        return Subclass::CompareAndSwapAtomicInternalImpl(
                            holder->elements(), entry, expected_value,
                            new_value, tag);
                      }),
                  isolate);
  }

  static Tagged<Object> CompareAndSwapAtomicInternalImpl(
      Tagged<FixedArrayBase> backing_store, InternalIndex entry,
      Tagged<Object> expected, Tagged<Object> value, SeqCstAccessTag tag) {
    UNREACHABLE();
  }

  void Set(Handle<JSObject> holder, InternalIndex entry,
           Tagged<Object> value) final {
    Subclass::SetImpl(holder, entry, value);
  }

  void Reconfigure(Handle<JSObject> object, Handle<FixedArrayBase> store,
                   InternalIndex entry, Handle<Object> value,
                   PropertyAttributes attributes) final {
    Subclass::ReconfigureImpl(object, store, entry, value, attributes);
  }

  static void ReconfigureImpl(DirectHandle<JSObject> object,
                              DirectHandle<FixedArrayBase> store,
                              InternalIndex entry, DirectHandle<Object> value,
                              PropertyAttributes attributes) {
    UNREACHABLE();
  }

  Maybe<bool> Add(Handle<JSObject> object, uint32_t index,
                  DirectHandle<Object> value, PropertyAttributes attributes,
                  uint32_t new_capacity) final {
    return Subclass::AddImpl(object, index, value, attributes, new_capacity);
  }

  static Maybe<bool> AddImpl(DirectHandle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    UNREACHABLE();
  }

  Maybe<uint32_t> Push(Handle<JSArray> receiver, BuiltinArguments* args,
                       uint32_t push_size) final {
    return Subclass::PushImpl(receiver, args, push_size);
  }

  static Maybe<uint32_t> PushImpl(DirectHandle<JSArray> receiver,
                                  BuiltinArguments* args, uint32_t push_sized) {
    UNREACHABLE();
  }

  Maybe<uint32_t> Unshift(Handle<JSArray> receiver, BuiltinArguments* args,
                          uint32_t unshift_size) final {
    return Subclass::UnshiftImpl(receiver, args, unshift_size);
  }

  static Maybe<uint32_t> UnshiftImpl(DirectHandle<JSArray> receiver,
                                     BuiltinArguments* args,
                                     uint32_t unshift_size) {
    UNREACHABLE();
  }

  MaybeHandle<Object> Pop(Handle<JSArray> receiver) final {
    return Subclass::PopImpl(receiver);
  }

  static MaybeHandle<Object> PopImpl(DirectHandle<JSArray> receiver) {
    UNREACHABLE();
  }

  MaybeHandle<Object> Shift(Handle<JSArray> receiver) final {
    return Subclass::ShiftImpl(receiver);
  }

  static MaybeHandle<Object> ShiftImpl(DirectHandle<JSArray> receiver) {
    UNREACHABLE();
  }

  Maybe<bool> SetLength(Handle<JSArray> array, uint32_t length) final {
    return Subclass::SetLengthImpl(
        array->GetIsolate(), array, length,
        handle(array->elements(), array->GetIsolate()));
  }

  static Maybe<bool> SetLengthImpl(Isolate* isolate, Handle<JSArray> array,
                                   uint32_t length,
                                   DirectHandle<FixedArrayBase> backing_store) {
    DCHECK(!array->SetLengthWouldNormalize(length));
    DCHECK(IsFastElementsKind(array->GetElementsKind()));
    uint32_t old_length = 0;
    CHECK(Object::ToArrayIndex(array->length(), &old_length));

    if (old_length < length) {
      ElementsKind kind = array->GetElementsKind();
      if (!IsHoleyElementsKind(kind)) {
        kind = GetHoleyElementsKind(kind);
        JSObject::TransitionElementsKind(array, kind);
      }
    }

    // Check whether the backing store should be shrunk.
    uint32_t capacity = backing_store->length();
    old_length = std::min(old_length, capacity);
    if (length == 0) {
      array->initialize_elements();
    } else if (length <= capacity) {
      if (IsSmiOrObjectElementsKind(kind())) {
        JSObject::EnsureWritableFastElements(array);
        if (array->elements() != *backing_store) {
          backing_store = handle(array->elements(), isolate);
        }
      }
      if (2 * length + JSObject::kMinAddedElementsCapacity <= capacity) {
        // If more than half the elements won't be used, trim the array.
        // Do not trim from short arrays to prevent frequent trimming on
        // repeated pop operations.
        // Leave some space to allow for subsequent push operations.
        uint32_t new_capacity =
            length + 1 == old_length ? (capacity + length) / 2 : length;
        DCHECK_LT(new_capacity, capacity);
        isolate->heap()->RightTrimArray(Cast<BackingStore>(*backing_store),
                                        new_capacity, capacity);
        // Fill the non-trimmed elements with holes.
        Cast<BackingStore>(*backing_store)
            ->FillWithHoles(length, std::min(old_length, new_capacity));
      } else {
        // Otherwise, fill the unused tail with holes.
        Cast<BackingStore>(*backing_store)->FillWithHoles(length, old_length);
      }
    } else {
      // Check whether the backing store should be expanded.
      capacity = std::max(length, JSObject::NewElementsCapacity(capacity));
      MAYBE_RETURN(Subclass::GrowCapacityAndConvertImpl(array, capacity),
                   Nothing<bool>());
    }

    array->set_length(Smi::FromInt(length));
    JSObject::ValidateElements(*array);
    return Just(true);
  }

  size_t NumberOfElements(Tagged<JSObject> receiver) final {
    return Subclass::NumberOfElementsImpl(receiver, receiver->elements());
  }

  static uint32_t NumberOfElementsImpl(Tagged<JSObject> receiver,
                                       Tagged<FixedArrayBase> backing_store) {
    UNREACHABLE();
  }

  static size_t GetMaxIndex(Tagged<JSObject> receiver,
                            Tagged<FixedArrayBase> elements) {
    if (IsJSArray(receiver)) {
      DCHECK(IsSmi(Cast<JSArray>(receiver)->length()));
      return static_cast<uint32_t>(
          Smi::ToInt(Cast<JSArray>(receiver)->length()));
    }
    return Subclass::GetCapacityImpl(receiver, elements);
  }

  static size_t GetMaxNumberOfEntries(Tagged<JSObject> receiver,
                                      Tagged<FixedArrayBase> elements) {
    return Subclass::GetMaxIndex(receiver, elements);
  }

  static MaybeHandle<FixedArrayBase> ConvertElementsWithCapacity(
      Handle<JSObject> object, Handle<FixedArrayBase> old_elements,
      ElementsKind from_kind, uint32_t capacity) {
    return ConvertElementsWithCapacity(object, old_elements, from_kind,
                                       capacity, 0, 0);
  }

  static MaybeHandle<FixedArrayBase> ConvertElementsWithCapacity(
      DirectHandle<JSObject> object, DirectHandle<FixedArrayBase> old_elements,
      ElementsKind from_kind, uint32_t capacity, uint32_t src_index,
      uint32_t dst_index) {
    Isolate* isolate = object->GetIsolate();
    Handle<FixedArrayBase> new_elements;
    // TODO(victorgomes): Retrieve native context in optimized code
    // and remove the check isolate->context().is_null().
    if (IsDoubleElementsKind(kind())) {
      if (!isolate->context().is_null() &&
          !base::IsInRange(capacity, 0, FixedDoubleArray::kMaxLength)) {
        THROW_NEW_ERROR(isolate,
                        NewRangeError(MessageTemplate::kInvalidArrayLength));
      }
      new_elements = isolate->factory()->NewFixedDoubleArray(capacity);
    } else {
      if (!isolate->context().is_null() &&
          !base::IsInRange(capacity, 0, FixedArray::kMaxLength)) {
        THROW_NEW_ERROR(isolate,
                        NewRangeError(MessageTemplate::kInvalidArrayLength));
      }
      new_elements = isolate->factory()->NewFixedArray(capacity);
    }

    int packed_size = kPackedSizeNotKnown;
    if (IsFastPackedElementsKind(from_kind) && IsJSArray(*object)) {
      packed_size = Smi::ToInt(Cast<JSArray>(*object)->length());
    }

    Subclass::CopyElementsImpl(isolate, *old_elements, src_index, *new_elements,
                               from_kind, dst_index, packed_size,
                               kCopyToEndAndInitializeToHole);

    return MaybeHandle<FixedArrayBase>(new_elements);
  }

  static Maybe<bool> TransitionElementsKindImpl(Handle<JSObject> object,
                                                DirectHandle<Map> to_map) {
    Isolate* isolate = object->GetIsolate();
    DirectHandle<Map> from_map(object->map(), isolate);
    ElementsKind from_kind = from_map->elements_kind();
    ElementsKind to_kind = to_map->elements_kind();
    if (IsHoleyElementsKind(from_kind)) {
      to_kind = GetHoleyElementsKind(to_kind);
    }
    if (from_kind != to_kind) {
      // This method should never be called for any other case.
      DCHECK(IsFastElementsKind(from_kind));
      DCHECK(IsFastElementsKind(to_kind));
      DCHECK_NE(TERMINAL_FAST_ELEMENTS_KIND, from_kind);

      Handle<FixedArrayBase> from_elements(object->elements(), isolate);
      if (object->elements() == ReadOnlyRoots(isolate).empty_fixed_array() ||
          IsDoubleElementsKind(from_kind) == IsDoubleElementsKind(to_kind)) {
        // No change is needed to the elements() buffer, the transition
        // only requires a map change.
        JSObject::MigrateToMap(isolate, object, to_map);
      } else {
        DCHECK(
            (IsSmiElementsKind(from_kind) && IsDoubleElementsKind(to_kind)) ||
            (IsDoubleElementsKind(from_kind) && IsObjectElementsKind(to_kind)));
        uint32_t capacity = static_cast<uint32_t>(object->elements()->length());
        Handle<FixedArrayBase> elements;
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            object->GetIsolate(), elements,
            ConvertElementsWithCapacity(object, from_elements, from_kind,
                                        capacity),
            Nothing<bool>());
        JSObject::SetMapAndElements(object, to_map, elements);
      }
      if (v8_flags.trace_elements_transitions) {
        JSObject::PrintElementsTransition(stdout, object, from_kind,
                                          from_elements, to_kind,
                                          handle(object->elements(), isolate));
      }
    }
    return Just(true);
  }

  static Maybe<bool> GrowCapacityAndConvertImpl(Handle<JSObject> object,
                                                uint32_t capacity) {
    ElementsKind from_kind = object->GetElementsKind();
    if (IsSmiOrObjectElementsKind(from_kind)) {
      // Array optimizations rely on the prototype lookups of Array objects
      // always returning undefined. If there is a store to the initial
      // prototype object, make sure all of these optimizations are invalidated.
      object->GetIsolate()->UpdateNoElementsProtectorOnSetLength(object);
    }
    Handle<FixedArrayBase> old_elements(object->elements(),
                                        object->GetIsolate());
    // This method should only be called if there's a reason to update the
    // elements.
    DCHECK(IsDoubleElementsKind(from_kind) != IsDoubleElementsKind(kind()) ||
           IsDictionaryElementsKind(from_kind) ||
           static_cast<uint32_t>(old_elements->length()) < capacity);
    return Subclass::BasicGrowCapacityAndConvertImpl(
        object, old_elements, from_kind, kind(), capacity);
  }

  static Maybe<bool> BasicGrowCapacityAndConvertImpl(
      Handle<JSObject> object, Handle<FixedArrayBase> old_elements,
      ElementsKind from_kind, ElementsKind to_kind, uint32_t capacity) {
    Handle<FixedArrayBase> elements;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        object->GetIsolate(), elements,
        ConvertElementsWithCapacity(object, old_elements, from_kind, capacity),
        Nothing<bool>());

    if (IsHoleyElementsKind(from_kind)) {
      to_kind = GetHoleyElementsKind(to_kind);
    }
    DirectHandle<Map> new_map =
        JSObject::GetElementsTransitionMap(object, to_kind);
    JSObject::SetMapAndElements(object, new_map, elements);

    // Transition through the allocation site as well if present.
    JSObject::UpdateAllocationSite(object, to_kind);

    if (v8_flags.trace_elements_transitions) {
      JSObject::PrintElementsTransition(stdout, object, from_kind, old_elements,
                                        to_kind, elements);
    }
    return Just(true);
  }

  Maybe<bool> TransitionElementsKind(Handle<JSObject> object,
                                     Handle<Map> map) final {
    return Subclass::TransitionElementsKindImpl(object, map);
  }

  Maybe<bool> GrowCapacityAndConvert(Handle<JSObject> object,
                                     uint32_t capacity) final {
    return Subclass::GrowCapacityAndConvertImpl(object, capacity);
  }

  Maybe<bool> GrowCapacity(Handle<JSObject> object, uint32_t index) final {
    // This function is intended to be called from optimized code. We don't
    // want to trigger lazy deopts there, so refuse to handle cases that would.
    if (object->map()->is_prototype_map() ||
        object->WouldConvertToSlowElements(index)) {
      return Just(false);
    }
    Handle<FixedArrayBase> old_elements(object->elements(),
                                        object->GetIsolate());
    uint32_t new_capacity = JSObject::NewElementsCapacity(index + 1);
    DCHECK(static_cast<uint32_t>(old_elements->length()) < new_capacity);
    const uint32_t kMaxLength = IsDoubleElementsKind(kind())
                                    ? FixedDoubleArray::kMaxLength
                                    : FixedArray::kMaxLength;
    if (new_capacity > kMaxLength) {
      return Just(false);
    }
    Handle<FixedArrayBase> elements;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        object->GetIsolate(), elements,
        ConvertElementsWithCapacity(object, old_elements, kind(), new_capacity),
        Nothing<bool>());

    DCHECK_EQ(object->GetElementsKind(), kind());
    // Transition through the allocation site as well if present.
    if (JSObject::UpdateAllocationSite<AllocationSiteUpdateMode::kCheckOnly>(
            object, kind())) {
      return Just(false);
    }

    object->set_elements(*elements);
    return Just(true);
  }

  void Delete(Handle<JSObject> obj, InternalIndex entry) final {
    Subclass::DeleteImpl(obj, entry);
  }

  static void CopyElementsImpl(Isolate* isolate, Tagged<FixedArrayBase> from,
                               uint32_t from_start, Tagged<FixedArrayBase> to,
                               ElementsKind from_kind, uint32_t to_start,
                               int packed_size, int copy_size) {
    UNREACHABLE();
  }

  void CopyElements(Isolate* isolate, Tagged<JSObject> from_holder,
                    uint32_t from_start, ElementsKind from_kind,
                    Handle<FixedArrayBase> to, uint32_t to_start,
                    int copy_size) final {
    int packed_size = kPackedSizeNotKnown;
    bool is_packed =
        IsFastPackedElementsKind(from_kind) && IsJSArray(from_holder);
    if (is_packed) {
      packed_size = Smi::ToInt(Cast<JSArray>(from_holder)->length());
      if (copy_size >= 0 && packed_size > copy_size) {
        packed_size = copy_size;
      }
    }
    Tagged<FixedArrayBase> from = from_holder->elements();
    // NOTE: the Subclass::CopyElementsImpl() methods
    // violate the handlified function signature convention:
    // raw pointer parameters in the function that allocates. This is done
    // intentionally to avoid ArrayConcat() builtin performance degradation.
    //
    // Details: The idea is that allocations actually happen only in case of
    // copying from object with fast double elements to object with object
    // elements. In all the other cases there are no allocations performed and
    // handle creation causes noticeable performance degradation of the builtin.
    Subclass::CopyElementsImpl(isolate, from, from_start, *to, from_kind,
                               to_start, packed_size, copy_size);
  }

  void CopyElements(Isolate* isolate, Handle<FixedArrayBase> source,
                    ElementsKind source_kind,
                    Handle<FixedArrayBase> destination, int size) override {
    Subclass::CopyElementsImpl(isolate, *source, 0, *destination, source_kind,
                               0, kPackedSizeNotKnown, size);
  }

  void CopyTypedArrayElementsSlice(Tagged<JSTypedArray> source,
                                   Tagged<JSTypedArray> destination,
                                   size_t start, size_t end) override {
    Subclass::CopyTypedArrayElementsSliceImpl(source, destination, start, end);
  }

  static void CopyTypedArrayElementsSliceImpl(Tagged<JSTypedArray> source,
                                              Tagged<JSTypedArray> destination,
                                              size_t start, size_t end) {
    UNREACHABLE();
  }

  Tagged<Object> CopyElements(Handle<Object> source,
                              Handle<JSObject> destination, size_t length,
                              size_t offset) final {
    return Subclass::CopyElementsHandleImpl(source, destination, length,
                                            offset);
  }

  static Tagged<Object> CopyElementsHandleImpl(
      DirectHandle<Object> source, DirectHandle<JSObject> destination,
      size_t length, size_t offset) {
    UNREACHABLE();
  }

  Handle<NumberDictionary> Normalize(Handle<JSObject> object) final {
    return Subclass::NormalizeImpl(
        object, handle(object->elements(), object->GetIsolate()));
  }

  static Handle<NumberDictionary> NormalizeImpl(
      DirectHandle<JSObject> object, DirectHandle<FixedArrayBase> elements) {
    UNREACHABLE();
  }

  Maybe<bool> CollectValuesOrEntries(Isolate* isolate, Handle<JSObject> object,
                                     Handle<FixedArray> values_or_entries,
                                     bool get_entries, int* nof_items,
                                     PropertyFilter filter) override {
    return Subclass::CollectValuesOrEntriesImpl(
        isolate, object, values_or_entries, get_entries, nof_items, filter);
  }

  static Maybe<bool> CollectValuesOrEntriesImpl(
      Isolate* isolate, Handle<JSObject> object,
      DirectHandle<FixedArray> values_or_entries, bool get_entries,
      int* nof_items, PropertyFilter filter) {
    DCHECK_EQ(*nof_items, 0);
    KeyAccumulator accumulator(isolate, KeyCollectionMode::kOwnOnly,
                               ALL_PROPERTIES);
    RETURN_NOTHING_IF_NOT_SUCCESSFUL(Subclass::CollectElementIndicesImpl(
        object, handle(object->elements(), isolate), &accumulator));
    DirectHandle<FixedArray> keys = accumulator.GetKeys();

    int count = 0;
    int i = 0;
    ElementsKind original_elements_kind = object->GetElementsKind();

    for (; i < keys->length(); ++i) {
      DirectHandle<Object> key(keys->get(i), isolate);
      uint32_t index;
      if (!Object::ToUint32(*key, &index)) continue;

      DCHECK_EQ(object->GetElementsKind(), original_elements_kind);
      InternalIndex entry = Subclass::GetEntryForIndexImpl(
          isolate, *object, object->elements(), index, filter);
      if (entry.is_not_found()) continue;
      PropertyDetails details = Subclass::GetDetailsImpl(*object, entry);

      DirectHandle<Object> value;
      if (details.kind() == PropertyKind::kData) {
        value = Subclass::GetInternalImpl(isolate, object, entry);
      } else {
        // This might modify the elements and/or change the elements kind.
        LookupIterator it(isolate, object, index, LookupIterator::OWN);
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, value, Object::GetProperty(&it), Nothing<bool>());
      }
      if (get_entries) value = MakeEntryPair(isolate, index, value);
      values_or_entries->set(count++, *value);
      if (object->GetElementsKind() != original_elements_kind) break;
    }

    // Slow path caused by changes in elements kind during iteration.
    for (; i < keys->length(); i++) {
      DirectHandle<Object> key(keys->get(i), isolate);
      uint32_t index;
      if (!Object::ToUint32(*key, &index)) continue;

      if (filter & ONLY_ENUMERABLE) {
        InternalElementsAccessor* accessor =
            reinterpret_cast<InternalElementsAccessor*>(
                object->GetElementsAccessor());
        InternalIndex entry = accessor->GetEntryForIndex(
            isolate, *object, object->elements(), index);
        if (entry.is_not_found()) continue;
        PropertyDetails details = accessor->GetDetails(*object, entry);
        if (!details.IsEnumerable()) continue;
      }

      Handle<Object> value;
      LookupIterator it(isolate, object, index, LookupIterator::OWN);
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, value, Object::GetProperty(&it),
                                       Nothing<bool>());

      if (get_entries) value = MakeEntryPair(isolate, index, value);
      values_or_entries->set(count++, *value);
    }

    *nof_items = count;
    return Just(true);
  }

  V8_WARN_UNUSED_RESULT ExceptionStatus CollectElementIndices(
      Handle<JSObject> object, Handle<FixedArrayBase> backing_store,
      KeyAccumulator* keys) final {
    return Subclass::CollectElementIndicesImpl(object, backing_store, keys);
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus CollectElementIndicesImpl(
      DirectHandle<JSObject> object, DirectHandle<FixedArrayBase> backing_store,
      KeyAccumulator* keys) {
    DCHECK_NE(DICTIONARY_ELEMENTS, kind());
    // Non-dictionary elements can't have all-can-read accessors.
    size_t length = Subclass::GetMaxIndex(*object, *backing_store);
    PropertyFilter filter = keys->filter();
    Isolate* isolate = keys->isolate();
    Factory* factory = isolate->factory();
    for (size_t i = 0; i < length; i++) {
      if (Subclass::HasElementImpl(isolate, *object, i, *backing_store,
                                   filter)) {
        RETURN_FAILURE_IF_NOT_SUCCESSFUL(
            keys->AddKey(factory->NewNumberFromSize(i)));
      }
    }
    return ExceptionStatus::kSuccess;
  }

  static Handle<FixedArray> DirectCollectElementIndicesImpl(
      Isolate* isolate, DirectHandle<JSObject> object,
      DirectHandle<FixedArrayBase> backing_store, GetKeysConversion convert,
      PropertyFilter filter, Handle<FixedArray> list, uint32_t* nof_indices,
      uint32_t insertion_index = 0) {
    size_t length = Subclass::GetMaxIndex(*object, *backing_store);
    uint32_t const kMaxStringTableEntries =
        isolate->heap()->MaxNumberToStringCacheSize();
    for (size_t i = 0; i < length; i++) {
      if (Subclass::HasElementImpl(isolate, *object, i, *backing_store,
                                   filter)) {
        if (convert == GetKeysConversion::kConvertToString) {
          bool use_cache = i < kMaxStringTableEntries;
          DirectHandle<String> index_string =
              isolate->factory()->SizeToString(i, use_cache);
          list->set(insertion_index, *index_string);
        } else {
          DirectHandle<Object> number =
              isolate->factory()->NewNumberFromSize(i);
          list->set(insertion_index, *number);
        }
        insertion_index++;
      }
    }
    *nof_indices = insertion_index;
    return list;
  }

  MaybeHandle<FixedArray> PrependElementIndices(
      Isolate* isolate, Handle<JSObject> object,
      Handle<FixedArrayBase> backing_store, Handle<FixedArray> keys,
      GetKeysConversion convert, PropertyFilter filter) final {
    return Subclass::PrependElementIndicesImpl(isolate, object, backing_store,
                                               keys, convert, filter);
  }

  static MaybeHandle<FixedArray> PrependElementIndicesImpl(
      Isolate* isolate, Handle<JSObject> object,
      Handle<FixedArrayBase> backing_store, DirectHandle<FixedArray> keys,
      GetKeysConversion convert, PropertyFilter filter) {
    uint32_t nof_property_keys = keys->length();
    size_t initial_list_length =
        Subclass::GetMaxNumberOfEntries(*object, *backing_store);

    if (initial_list_length > FixedArray::kMaxLength - nof_property_keys) {
      THROW_NEW_ERROR(isolate,
                      NewRangeError(MessageTemplate::kInvalidArrayLength));
    }
    initial_list_length += nof_property_keys;

    // Collect the element indices into a new list.
    DCHECK_LE(initial_list_length, std::numeric_limits<int>::max());
    MaybeHandle<FixedArray> raw_array = isolate->factory()->TryNewFixedArray(
        static_cast<int>(initial_list_length));
    Handle<FixedArray> combined_keys;

    // If we have a holey backing store try to precisely estimate the backing
    // store size as a last emergency measure if we cannot allocate the big
    // array.
    if (!raw_array.ToHandle(&combined_keys)) {
      if (IsHoleyOrDictionaryElementsKind(kind())) {
        // If we overestimate the result list size we might end up in the
        // large-object space which doesn't free memory on shrinking the list.
        // Hence we try to estimate the final size for holey backing stores more
        // precisely here.
        initial_list_length =
            Subclass::NumberOfElementsImpl(*object, *backing_store);
        initial_list_length += nof_property_keys;
      }
      DCHECK_LE(initial_list_length, std::numeric_limits<int>::max());
      combined_keys = isolate->factory()->NewFixedArray(
          static_cast<int>(initial_list_length));
    }

    uint32_t nof_indices = 0;
    bool needs_sorting = IsDictionaryElementsKind(kind()) ||
                         IsSloppyArgumentsElementsKind(kind());
    combined_keys = Subclass::DirectCollectElementIndicesImpl(
        isolate, object, backing_store,
        needs_sorting ? GetKeysConversion::kKeepNumbers : convert, filter,
        combined_keys, &nof_indices);

    if (needs_sorting) {
      SortIndices(isolate, combined_keys, nof_indices);
      // Indices from dictionary elements should only be converted after
      // sorting.
      if (convert == GetKeysConversion::kConvertToString) {
        for (uint32_t i = 0; i < nof_indices; i++) {
          DirectHandle<Object> index_string =
              isolate->factory()->Uint32ToString(
                  Object::NumberValue(combined_keys->get(i)));
          combined_keys->set(i, *index_string);
        }
      }
    }

    // Copy over the passed-in property keys.
    CopyObjectToObjectElements(isolate, *keys, PACKED_ELEMENTS, 0,
                               *combined_keys, PACKED_ELEMENTS, nof_indices,
                               nof_property_keys);

    // For holey elements and arguments we might have to shrink the collected
    // keys since the estimates might be off.
    if (IsHoleyOrDictionaryElementsKind(kind()) ||
        IsSloppyArgumentsElementsKind(kind())) {
      // Shrink combined_keys to the final size.
      int final_size = nof_indices + nof_property_keys;
      DCHECK_LE(final_size, combined_keys->length());
      return FixedArray::RightTrimOrEmpty(isolate, combined_keys, final_size);
    }

    return combined_keys;
  }

  V8_WARN_UNUSED_RESULT ExceptionStatus AddElementsToKeyAccumulator(
      Handle<JSObject> receiver, KeyAccumulator* accumulator,
      AddKeyConversion convert) final {
    return Subclass::AddElementsToKeyAccumulatorImpl(receiver, accumulator,
                                                     convert);
  }

  static uint32_t GetCapacityImpl(Tagged<JSObject> holder,
                                  Tagged<FixedArrayBase> backing_store) {
    return backing_store->length();
  }

  size_t GetCapacity(Tagged<JSObject> holder,
                     Tagged<FixedArrayBase> backing_store) final {
    return Subclass::GetCapacityImpl(holder, backing_store);
  }

  static MaybeHandle<Object> FillImpl(DirectHandle<JSObject> receiver,
                                      DirectHandle<Object> obj_value,
                                      size_t start, size_t end) {
    UNREACHABLE();
  }

  MaybeHandle<Object> Fill(Handle<JSObject> receiver, Handle<Object> obj_value,
                           size_t start, size_t end) override {
    return Subclass::FillImpl(receiver, obj_value, start, end);
  }

  static Maybe<bool> IncludesValueImpl(Isolate* isolate,
                                       Handle<JSObject> receiver,
                                       DirectHandle<Object> value,
                                       size_t start_from, size_t length) {
    return IncludesValueSlowPath(isolate, receiver, value, start_from, length);
  }

  Maybe<bool> IncludesValue(Isolate* isolate, Handle<JSObject> receiver,
                            Handle<Object> value, size_t start_from,
                            size_t length) final {
    return Subclass::IncludesValueImpl(isolate, receiver, value, start_from,
                                       length);
  }

  static Maybe<int64_t> IndexOfValueImpl(Isolate* isolate,
                                         Handle<JSObject> receiver,
                                         DirectHandle<Object> value,
                                         size_t start_from, size_t length) {
    return IndexOfValueSlowPath(isolate, receiver, value, start_from, length);
  }

  Maybe<int64_t> IndexOfValue(Isolate* isolate, Handle<JSObject> receiver,
                              Handle<Object> value, size_t start_from,
                              size_t length) final {
    return Subclass::IndexOfValueImpl(isolate, receiver, value, start_from,
                                      length);
  }

  static Maybe<int64_t> LastIndexOfValueImpl(DirectHandle<JSObject> receiver,
                                             DirectHandle<Object> value,
                                             size_t start_from) {
    UNREACHABLE();
  }

  Maybe<int64_t> LastIndexOfValue(Handle<JSObject> receiver,
                                  Handle<Object> value,
                                  size_t start_from) final {
    return Subclass::LastIndexOfValueImpl(receiver, value, start_from);
  }

  static void ReverseImpl(Tagged<JSObject> receiver) { UNREACHABLE(); }

  void Reverse(Tagged<JSObject> receiver) final {
    Subclass::ReverseImpl(receiver);
  }

  static InternalIndex GetEntryForIndexImpl(
      Isolate* isolate, Tagged<JSObject> holder,
      Tagged<FixedArrayBase> backing_store, size_t index,
      PropertyFilter filter) {
    DCHECK(IsFastElementsKind(kind()) ||
           IsAnyNonextensibleElementsKind(kind()));
    size_t length = Subclass::GetMaxIndex(holder, backing_store);
    if (IsHoleyElementsKindForRead(kind())) {
      DCHECK_IMPLIES(
          index < length,
          index <= static_cast<size_t>(std::numeric_limits<int>::max()));
      return index < length &&
                     !Cast<BackingStore>(backing_store)
                          ->is_the_hole(isolate, static_cast<int>(index))
                 ? InternalIndex(index)
                 : InternalIndex::NotFound();
    } else {
      return index < length ? InternalIndex(index) : InternalIndex::NotFound();
    }
  }

  InternalIndex GetEntryForIndex(Isolate* isolate, Tagged<JSObject> holder,
                                 Tagged<FixedArrayBase> backing_store,
                                 size_t index) final {
    return Subclass::GetEntryForIndexImpl(isolate, holder, backing_store, index,
                                          ALL_PROPERTIES);
  }

  static PropertyDetails GetDetailsImpl(Tagged<FixedArrayBase> backing_store,
                                        InternalIndex entry) {
    return PropertyDetails(PropertyKind::kData, NONE,
                           PropertyCellType::kNoCell);
  }

  static PropertyDetails GetDetailsImpl(Tagged<JSObject> holder,
                                        InternalIndex entry) {
    return PropertyDetails(PropertyKind::kData, NONE,
                           PropertyCellType::kNoCell);
  }

  PropertyDetails GetDetails(Tagged<JSObject> holder,
                             InternalIndex entry) final {
    return Subclass::GetDetailsImpl(holder, entry);
  }

  Handle<FixedArray> CreateListFromArrayLike(Isolate* isolate,
                                             Handle<JSObject> object,
                                             uint32_t length) final {
    return Subclass::CreateListFromArrayLikeImpl(isolate, object, length);
  }

  static Handle<FixedArray> CreateListFromArrayLikeImpl(
      Isolate* isolate, DirectHandle<JSObject> object, uint32_t length) {
    UNREACHABLE();
  }
};

class DictionaryElementsAccessor
    : public ElementsAccessorBase<DictionaryElementsAccessor,
                                  ElementsKindTraits<DICTIONARY_ELEMENTS>> {
 public:
  static uint32_t GetMaxIndex(Tagged<JSObject> receiver,
                              Tagged<FixedArrayBase> elements) {
    // We cannot properly estimate this for dictionaries.
    UNREACHABLE();
  }

  static uint32_t GetMaxNumberOfEntries(Tagged<JSObject> receiver,
                                        Tagged<FixedArrayBase> backing_store) {
    return NumberOfElementsImpl(receiver, backing_store);
  }

  static uint32_t NumberOfElementsImpl(Tagged<JSObject> receiver,
                                       Tagged<FixedArrayBase> backing_store) {
    Tagged<NumberDictionary> dict = Cast<NumberDictionary>(backing_store);
    return dict->NumberOfElements();
  }

  static Maybe<bool> SetLengthImpl(Isolate* isolate,
                                   DirectHandle<JSArray> array, uint32_t length,
                                   Handle<FixedArrayBase> backing_store) {
    auto dict = Cast<NumberDictionary>(backing_store);
    uint32_t old_length = 0;
    CHECK(Object::ToArrayLength(array->length(), &old_length));
    {
      DisallowGarbageCollection no_gc;
      ReadOnlyRoots roots(isolate);
      if (length < old_length) {
        if (dict->requires_slow_elements()) {
          // Find last non-deletable element in range of elements to be
          // deleted and adjust range accordingly.
          for (InternalIndex entry : dict->IterateEntries()) {
            Tagged<Object> index = dict->KeyAt(isolate, entry);
            if (dict->IsKey(roots, index)) {
              uint32_t number =
                  static_cast<uint32_t>(Object::NumberValue(index));
              if (length <= number && number < old_length) {
                PropertyDetails details = dict->DetailsAt(entry);
                if (!details.IsConfigurable()) length = number + 1;
              }
            }
          }
        }

        if (length == 0) {
          // Flush the backing store.
          array->initialize_elements();
        } else {
          // Remove elements that should be deleted.
          int removed_entries = 0;
          for (InternalIndex entry : dict->IterateEntries()) {
            Tagged<Object> index = dict->KeyAt(isolate, entry);
            if (dict->IsKey(roots, index)) {
              uint32_t number =
                  static_cast<uint32_t>(Object::NumberValue(index));
              if (length <= number && number < old_length) {
                dict->ClearEntry(entry);
                removed_entries++;
              }
            }
          }

          if (removed_entries > 0) {
            // Update the number of elements.
            dict->ElementsRemoved(removed_entries);
          }
        }
      }
    }

    DirectHandle<Number> length_obj =
        isolate->factory()->NewNumberFromUint(length);
    array->set_length(*length_obj);
    return Just(true);
  }

  static void CopyElementsImpl(Isolate* isolate, Tagged<FixedArrayBase> from,
                               uint32_t from_start, Tagged<FixedArrayBase> to,
                               ElementsKind from_kind, uint32_t to_start,
                               int packed_size, int copy_size) {
    UNREACHABLE();
  }

  static void DeleteImpl(DirectHandle<JSObject> obj, InternalIndex entry) {
    Handle<NumberDictionary> dict(Cast<NumberDictionary>(obj->elements()),
                                  obj->GetIsolate());
    dict = NumberDictionary::DeleteEntry(obj->GetIsolate(), dict, entry);
    obj->set_elements(*dict);
  }

  static bool HasAccessorsImpl(Tagged<JSObject> holder,
                               Tagged<FixedArrayBase> backing_store) {
    DisallowGarbageCollection no_gc;
    Tagged<NumberDictionary> dict = Cast<NumberDictionary>(backing_store);
    if (!dict->requires_slow_elements()) return false;
    PtrComprCageBase cage_base = GetPtrComprCageBase(holder);
    ReadOnlyRoots roots = holder->GetReadOnlyRoots(cage_base);
    for (InternalIndex i : dict->IterateEntries()) {
      Tagged<Object> key = dict->KeyAt(cage_base, i);
      if (!dict->IsKey(roots, key)) continue;
      PropertyDetails details = dict->DetailsAt(i);
      if (details.kind() == PropertyKind::kAccessor) return true;
    }
    return false;
  }

  static Tagged<Object> GetRaw(Tagged<FixedArrayBase> store,
                               InternalIndex entry) {
    Tagged<NumberDictionary> backing_store = Cast<NumberDictionary>(store);
    return backing_store->ValueAt(entry);
  }

  static Handle<Object> GetImpl(Isolate* isolate,
                                Tagged<FixedArrayBase> backing_store,
                                InternalIndex entry) {
    return handle(GetRaw(backing_store, entry), isolate);
  }

  static Handle<Object> GetAtomicInternalImpl(
      Isolate* isolate, Tagged<FixedArrayBase> backing_store,
      InternalIndex entry, SeqCstAccessTag tag) {
    return handle(Cast<NumberDictionary>(backing_store)->ValueAt(entry, tag),
                  isolate);
  }

  static inline void SetImpl(DirectHandle<JSObject> holder, InternalIndex entry,
                             Tagged<Object> value) {
    SetImpl(holder->elements(), entry, value);
  }

  static inline void SetImpl(Tagged<FixedArrayBase> backing_store,
                             InternalIndex entry, Tagged<Object> value) {
    Cast<NumberDictionary>(backing_store)->ValueAtPut(entry, value);
  }

  static void SetAtomicInternalImpl(Tagged<FixedArrayBase> backing_store,
                                    InternalIndex entry, Tagged<Object> value,
                                    SeqCstAccessTag tag) {
    Cast<NumberDictionary>(backing_store)->ValueAtPut(entry, value, tag);
  }

  static Handle<Object> SwapAtomicInternalImpl(
      Isolate* isolate, Tagged<FixedArrayBase> backing_store,
      InternalIndex entry, Tagged<Object> value, SeqCstAccessTag tag) {
    return handle(
        Cast<NumberDictionary>(backing_store)->ValueAtSwap(entry, value, tag),
        isolate);
  }

  static Tagged<Object> CompareAndSwapAtomicInternalImpl(
      Tagged<FixedArrayBase> backing_store, InternalIndex entry,
      Tagged<Object> expected, Tagged<Object> value, SeqCstAccessTag tag) {
    return Cast<NumberDictionary>(backing_store)
        ->ValueAtCompareAndSwap(entry, expected, value, tag);
  }

  static void ReconfigureImpl(DirectHandle<JSObject> object,
                              DirectHandle<FixedArrayBase> store,
                              InternalIndex entry, DirectHandle<Object> value,
                              PropertyAttributes attributes) {
    Tagged<NumberDictionary> dictionary = Cast<NumberDictionary>(*store);
    if (attributes != NONE) object->RequireSlowElements(dictionary);
    dictionary->ValueAtPut(entry, *value);
    PropertyDetails details = dictionary->DetailsAt(entry);
    details =
        PropertyDetails(PropertyKind::kData, attributes,
                        PropertyCellType::kNoCell, details.dictionary_index());

    dictionary->DetailsAtPut(entry, details);
  }

  static Maybe<bool> AddImpl(Handle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    PropertyDetails details(PropertyKind::kData, attributes,
                            PropertyCellType::kNoCell);
    Handle<NumberDictionary> dictionary =
        object->HasFastElements() || object->HasFastStringWrapperElements()
            ? JSObject::NormalizeElements(object)
            : handle(Cast<NumberDictionary>(object->elements()),
                     object->GetIsolate());
    Handle<NumberDictionary> new_dictionary = NumberDictionary::Add(
        object->GetIsolate(), dictionary, index, value, details);
    new_dictionary->UpdateMaxNumberKey(index, object);
    if (attributes != NONE) object->RequireSlowElements(*new_dictionary);
    if (dictionary.is_identical_to(new_dictionary)) return Just(true);
    object->set_elements(*new_dictionary);
    return Just(true);
  }

  static bool HasEntryImpl(Isolate* isolate, Tagged<FixedArrayBase> store,
                           InternalIndex entry) {
    DisallowGarbageCollection no_gc;
    Tagged<NumberDictionary> dict = Cast<NumberDictionary>(store);
    Tagged<Object> index = dict->KeyAt(isolate, entry);
    return !IsTheHole(index, isolate);
  }

  static InternalIndex GetEntryForIndexImpl(Isolate* isolate,
                                            Tagged<JSObject> holder,
                                            Tagged<FixedArrayBase> store,
                                            size_t index,
                                            PropertyFilter filter) {
    DisallowGarbageCollection no_gc;
    Tagged<NumberDictionary> dictionary = Cast<NumberDictionary>(store);
    DCHECK_LE(index, std::numeric_limits<uint32_t>::max());
    InternalIndex entry =
        dictionary->FindEntry(isolate, static_cast<uint32_t>(index));
    if (entry.is_not_found()) return entry;

    if (filter != ALL_PROPERTIES) {
      PropertyDetails details = dictionary->DetailsAt(entry);
      PropertyAttributes attr = details.attributes();
      if ((int{attr} & filter) != 0) return InternalIndex::NotFound();
    }
    return entry;
  }

  static PropertyDetails GetDetailsImpl(Tagged<JSObject> holder,
                                        InternalIndex entry) {
    return GetDetailsImpl(holder->elements(), entry);
  }

  static PropertyDetails GetDetailsImpl(Tagged<FixedArrayBase> backing_store,
                                        InternalIndex entry) {
    return Cast<NumberDictionary>(backing_store)->DetailsAt(entry);
  }

  static uint32_t FilterKey(DirectHandle<NumberDictionary> dictionary,
                            InternalIndex entry, Tagged<Object> raw_key,
                            PropertyFilter filter) {
    DCHECK(IsNumber(raw_key));
    DCHECK_LE(Object::NumberValue(raw_key), kMaxUInt32);
    PropertyDetails details = dictionary->DetailsAt(entry);
    PropertyAttributes attr = details.attributes();
    if ((int{attr} & filter) != 0) return kMaxUInt32;
    return static_cast<uint32_t>(Object::NumberValue(raw_key));
  }

  static uint32_t GetKeyForEntryImpl(Isolate* isolate,
                                     DirectHandle<NumberDictionary> dictionary,
                                     InternalIndex entry,
                                     PropertyFilter filter) {
    DisallowGarbageCollection no_gc;
    Tagged<Object> raw_key = dictionary->KeyAt(isolate, entry);
    if (!dictionary->IsKey(ReadOnlyRoots(isolate), raw_key)) return kMaxUInt32;
    return FilterKey(dictionary, entry, raw_key, filter);
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus CollectElementIndicesImpl(
      DirectHandle<JSObject> object, Handle<FixedArrayBase> backing_store,
      KeyAccumulator* keys) {
    if (keys->filter() & SKIP_STRINGS) return ExceptionStatus::kSuccess;
    Isolate* isolate = keys->isolate();
    auto dictionary = Cast<NumberDictionary>(backing_store);
    DirectHandle<FixedArray> elements = isolate->factory()->NewFixedArray(
        GetMaxNumberOfEntries(*object, *backing_store));
    int insertion_index = 0;
    PropertyFilter filter = keys->filter();
    ReadOnlyRoots roots(isolate);
    for (InternalIndex i : dictionary->IterateEntries()) {
      AllowGarbageCollection allow_gc;
      Tagged<Object> raw_key = dictionary->KeyAt(isolate, i);
      if (!dictionary->IsKey(roots, raw_key)) continue;
      uint32_t key = FilterKey(dictionary, i, raw_key, filter);
      if (key == kMaxUInt32) {
        // This might allocate, but {raw_key} is not used afterwards.
        keys->AddShadowingKey(raw_key, &allow_gc);
        continue;
      }
      elements->set(insertion_index, raw_key);
      insertion_index++;
    }
    SortIndices(isolate, elements, insertion_index);
    for (int i = 0; i < insertion_index; i++) {
      RETURN_FAILURE_IF_NOT_SUCCESSFUL(keys->AddKey(elements->get(i)));
    }
    return ExceptionStatus::kSuccess;
  }

  static Handle<FixedArray> DirectCollectElementIndicesImpl(
      Isolate* isolate, DirectHandle<JSObject> object,
      Handle<FixedArrayBase> backing_store, GetKeysConversion convert,
      PropertyFilter filter, Handle<FixedArray> list, uint32_t* nof_indices,
      uint32_t insertion_index = 0) {
    if (filter & SKIP_STRINGS) return list;

    auto dictionary = Cast<NumberDictionary>(backing_store);
    for (InternalIndex i : dictionary->IterateEntries()) {
      uint32_t key = GetKeyForEntryImpl(isolate, dictionary, i, filter);
      if (key == kMaxUInt32) continue;
      DirectHandle<Object> index = isolate->factory()->NewNumberFromUint(key);
      list->set(insertion_index, *index);
      insertion_index++;
    }
    *nof_indices = insertion_index;
    return list;
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus AddElementsToKeyAccumulatorImpl(
      DirectHandle<JSObject> receiver, KeyAccumulator* accumulator,
      AddKeyConversion convert) {
    Isolate* isolate = accumulator->isolate();
    DirectHandle<NumberDictionary> dictionary(
        Cast<NumberDictionary>(receiver->elements()), isolate);
    ReadOnlyRoots roots(isolate);
    for (InternalIndex i : dictionary->IterateEntries()) {
      Tagged<Object> k = dictionary->KeyAt(isolate, i);
      if (!dictionary->IsKey(roots, k)) continue;
      Tagged<Object> value = dictionary->ValueAt(isolate, i);
      DCHECK(!IsTheHole(value, isolate));
      DCHECK(!IsAccessorPair(value));
      DCHECK(!IsAccessorInfo(value));
      RETURN_FAILURE_IF_NOT_SUCCESSFUL(accumulator->AddKey(value, convert));
    }
    return ExceptionStatus::kSuccess;
  }

  static bool IncludesValueFastPath(Isolate* isolate,
                                    DirectHandle<JSObject> receiver,
                                    DirectHandle<Object> value,
                                    size_t start_from, size_t length,
                                    Maybe<bool>* result) {
    DisallowGarbageCollection no_gc;
    Tagged<NumberDictionary> dictionary =
        Cast<NumberDictionary>(receiver->elements());
    Tagged<Object> the_hole = ReadOnlyRoots(isolate).the_hole_value();
    Tagged<Object> undefined = ReadOnlyRoots(isolate).undefined_value();

    // Scan for accessor properties. If accessors are present, then elements
    // must be accessed in order via the slow path.
    bool found = false;
    for (InternalIndex i : dictionary->IterateEntries()) {
      Tagged<Object> k = dictionary->KeyAt(isolate, i);
      if (k == the_hole) continue;
      if (k == undefined) continue;

      uint32_t index;
      if (!Object::ToArrayIndex(k, &index) || index < start_from ||
          index >= length) {
        continue;
      }

      if (dictionary->DetailsAt(i).kind() == PropertyKind::kAccessor) {
        // Restart from beginning in slow path, otherwise we may observably
        // access getters out of order
        return false;
      } else if (!found) {
        Tagged<Object> element_k = dictionary->ValueAt(isolate, i);
        if (Object::SameValueZero(*value, element_k)) found = true;
      }
    }

    *result = Just(found);
    return true;
  }

  static Maybe<bool> IncludesValueImpl(Isolate* isolate,
                                       Handle<JSObject> receiver,
                                       Handle<Object> value, size_t start_from,
                                       size_t length) {
    DCHECK(JSObject::PrototypeHasNoElements(isolate, *receiver));
    bool search_for_hole = IsUndefined(*value, isolate);

    if (!search_for_hole) {
      Maybe<bool> result = Nothing<bool>();
      if (DictionaryElementsAccessor::IncludesValueFastPath(
              isolate, receiver, value, start_from, length, &result)) {
        return result;
      }
    }
    ElementsKind original_elements_kind = receiver->GetElementsKind();
    USE(original_elements_kind);
    DirectHandle<NumberDictionary> dictionary(
        Cast<NumberDictionary>(receiver->elements()), isolate);
    // Iterate through the entire range, as accessing elements out of order is
    // observable.
    for (size_t k = start_from; k < length; ++k) {
      DCHECK_EQ(receiver->GetElementsKind(), original_elements_kind);
      InternalIndex entry =
          dictionary->FindEntry(isolate, static_cast<uint32_t>(k));
      if (entry.is_not_found()) {
        if (search_for_hole) return Just(true);
        continue;
      }

      PropertyDetails details = GetDetailsImpl(*dictionary, entry);
      switch (details.kind()) {
        case PropertyKind::kData: {
          Tagged<Object> element_k = dictionary->ValueAt(entry);
          if (Object::SameValueZero(*value, element_k)) return Just(true);
          break;
        }
        case PropertyKind::kAccessor: {
          LookupIterator it(isolate, receiver, k,
                            LookupIterator::OWN_SKIP_INTERCEPTOR);
          DCHECK(it.IsFound());
          DCHECK_EQ(it.state(), LookupIterator::ACCESSOR);
          Handle<Object> element_k;

          ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, element_k,
                                           Object::GetPropertyWithAccessor(&it),
                                           Nothing<bool>());

          if (Object::SameValueZero(*value, *element_k)) return Just(true);

          // Bailout to slow path if elements on prototype changed
          if (!JSObject::PrototypeHasNoElements(isolate, *receiver)) {
            return IncludesValueSlowPath(isolate, receiver, value, k + 1,
                                         length);
          }

          // Continue if elements unchanged
          if (*dictionary == receiver->elements()) continue;

          // Otherwise, bailout or update elements

          // If switched to initial elements, return true if searching for
          // undefined, and false otherwise.
          if (receiver->map()->GetInitialElements() == receiver->elements()) {
            return Just(search_for_hole);
          }

          // If switched to fast elements, continue with the correct accessor.
          if (receiver->GetElementsKind() != DICTIONARY_ELEMENTS) {
            ElementsAccessor* accessor = receiver->GetElementsAccessor();
            return accessor->IncludesValue(isolate, receiver, value, k + 1,
                                           length);
          }
          dictionary =
              handle(Cast<NumberDictionary>(receiver->elements()), isolate);
          break;
        }
      }
    }
    return Just(false);
  }

  static Maybe<int64_t> IndexOfValueImpl(Isolate* isolate,
                                         Handle<JSObject> receiver,
                                         DirectHandle<Object> value,
                                         size_t start_from, size_t length) {
    DCHECK(JSObject::PrototypeHasNoElements(isolate, *receiver));

    ElementsKind original_elements_kind = receiver->GetElementsKind();
    USE(original_elements_kind);
    DirectHandle<NumberDictionary> dictionary(
        Cast<NumberDictionary>(receiver->elements()), isolate);
    // Iterate through entire range, as accessing elements out of order is
    // observable.
    for (size_t k = start_from; k < length; ++k) {
      DCHECK_EQ(receiver->GetElementsKind(), original_elements_kind);
      DCHECK_LE(k, std::numeric_limits<uint32_t>::max());
      InternalIndex entry =
          dictionary->FindEntry(isolate, static_cast<uint32_t>(k));
      if (entry.is_not_found()) continue;

      PropertyDetails details =
          GetDetailsImpl(*dictionary, InternalIndex(entry));
      switch (details.kind()) {
        case PropertyKind::kData: {
          Tagged<Object> element_k = dictionary->ValueAt(entry);
          if (Object::StrictEquals(*value, element_k)) {
            return Just<int64_t>(k);
          }
          break;
        }
        case PropertyKind::kAccessor: {
          LookupIterator it(isolate, receiver, k,
                            LookupIterator::OWN_SKIP_INTERCEPTOR);
          DCHECK(it.IsFound());
          DCHECK_EQ(it.state(), LookupIterator::ACCESSOR);
          Handle<Object> element_k;

          ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, element_k,
                                           Object::GetPropertyWithAccessor(&it),
                                           Nothing<int64_t>());

          if (Object::StrictEquals(*value, *element_k)) return Just<int64_t>(k);

          // Bailout to slow path if elements on prototype changed.
          if (!JSObject::PrototypeHasNoElements(isolate, *receiver)) {
            return IndexOfValueSlowPath(isolate, receiver, value, k + 1,
                                        length);
          }

          // Continue if elements unchanged.
          if (*dictionary == receiver->elements()) continue;

          // Otherwise, bailout or update elements.
          if (receiver->GetElementsKind() != DICTIONARY_ELEMENTS) {
            // Otherwise, switch to slow path.
            return IndexOfValueSlowPath(isolate, receiver, value, k + 1,
                                        length);
          }
          dictionary = direct_handle(
              Cast<NumberDictionary>(receiver->elements()), isolate);
          break;
        }
      }
    }
    return Just<int64_t>(-1);
  }

  static void ValidateContents(Tagged<JSObject> holder, size_t length) {
    DisallowGarbageCollection no_gc;
#if DEBUG
    DCHECK_EQ(holder->map()->elements_kind(), DICTIONARY_ELEMENTS);
    if (!v8_flags.enable_slow_asserts) return;
    ReadOnlyRoots roots = holder->GetReadOnlyRoots();
    Tagged<NumberDictionary> dictionary =
        Cast<NumberDictionary>(holder->elements());
    // Validate the requires_slow_elements and max_number_key values.
    bool requires_slow_elements = false;
    int max_key = 0;
    for (InternalIndex i : dictionary->IterateEntries()) {
      Tagged<Object> k;
      if (!dictionary->ToKey(roots, i, &k)) continue;
      DCHECK_LE(0.0, Object::NumberValue(k));
      if (Object::NumberValue(k) >
          NumberDictionary::kRequiresSlowElementsLimit) {
        requires_slow_elements = true;
      } else {
        max_key = std::max(max_key, Smi::ToInt(k));
      }
    }
    if (requires_slow_elements) {
      DCHECK(dictionary->requires_slow_elements());
    } else if (!dictionary->requires_slow_elements()) {
      DCHECK_LE(max_key, dictionary->max_number_key());
    }
#endif
  }
};

// Super class for all fast element arrays.
template <typename Subclass, typename KindTraits>
class FastElementsAccessor : public ElementsAccessorBase<Subclass, KindTraits> {
 public:
  using BackingStore = typename KindTraits::BackingStore;

  static Handle<NumberDictionary> NormalizeImpl(
      Handle<JSObject> object, DirectHandle<FixedArrayBase> store) {
    Isolate* isolate = object->GetIsolate();
    ElementsKind kind = Subclass::kind();

    // Ensure that notifications fire if the array or object prototypes are
    // normalizing.
    if (IsSmiOrObjectElementsKind(kind) ||
        kind == FAST_STRING_WRAPPER_ELEMENTS) {
      isolate->UpdateNoElementsProtectorOnNormalizeElements(object);
    }

    int capacity = object->GetFastElementsUsage();
    Handle<NumberDictionary> dictionary =
        NumberDictionary::New(isolate, capacity);

    PropertyDetails details = PropertyDetails::Empty();
    int j = 0;
    int max_number_key = -1;
    for (int i = 0; j < capacity; i++) {
      if (IsHoleyElementsKindForRead(kind)) {
        if (Cast<BackingStore>(*store)->is_the_hole(isolate, i)) continue;
      }
      max_number_key = i;
      DirectHandle<Object> value =
          Subclass::GetImpl(isolate, *store, InternalIndex(i));
      dictionary =
          NumberDictionary::Add(isolate, dictionary, i, value, details);
      j++;
    }

    if (max_number_key > 0) {
      dictionary->UpdateMaxNumberKey(static_cast<uint32_t>(max_number_key),
                                     object);
    }
    return dictionary;
  }

  static void DeleteAtEnd(DirectHandle<JSObject> obj,
                          Handle<BackingStore> backing_store, uint32_t entry) {
    uint32_t length = static_cast<uint32_t>(backing_store->length());
    DCHECK_LT(entry, length);
    Isolate* isolate = obj->GetIsolate();
    for (; entry > 0; entry--) {
      if (!backing_store->is_the_hole(isolate, entry - 1)) break;
    }
    if (entry == 0) {
      Tagged<FixedArray> empty = ReadOnlyRoots(isolate).empty_fixed_array();
      // Dynamically ask for the elements kind here since we manually redirect
      // the operations for argument backing stores.
      if (obj->GetElementsKind() == FAST_SLOPPY_ARGUMENTS_ELEMENTS) {
        Cast<SloppyArgumentsElements>(obj->elements())->set_arguments(empty);
      } else {
        obj->set_elements(empty);
      }
      return;
    }

    isolate->heap()->RightTrimArray(*backing_store, entry, length);
  }

  static void DeleteCommon(Handle<JSObject> obj, uint32_t entry,
                           Handle<FixedArrayBase> store) {
    DCHECK(obj->HasSmiOrObjectElements() || obj->HasDoubleElements() ||
           obj->HasNonextensibleElements() || obj->HasFastArgumentsElements() ||
           obj->HasFastStringWrapperElements());
    Handle<BackingStore> backing_store = Cast<BackingStore>(store);
    if (!IsJSArray(*obj) &&
        entry == static_cast<uint32_t>(store->length()) - 1) {
      DeleteAtEnd(obj, backing_store, entry);
      return;
    }

    Isolate* isolate = obj->GetIsolate();
    backing_store->set_the_hole(isolate, entry);

    // TODO(verwaest): Move this out of elements.cc.
    // If the backing store is larger than a certain size and
    // has too few used values, normalize it.
    const int kMinLengthForSparsenessCheck = 64;
    if (backing_store->length() < kMinLengthForSparsenessCheck) return;
    uint32_t length = 0;
    if (IsJSArray(*obj)) {
      Object::ToArrayLength(Cast<JSArray>(*obj)->length(), &length);
    } else {
      length = static_cast<uint32_t>(store->length());
    }

    // To avoid doing the check on every delete, use a counter-based heuristic.
    const int kLengthFraction = 16;
    // The above constant must be large enough to ensure that we check for
    // normalization frequently enough. At a minimum, it should be large
    // enough to reliably hit the "window" of remaining elements count where
    // normalization would be beneficial.
    static_assert(kLengthFraction >=
                  NumberDictionary::kEntrySize *
                      NumberDictionary::kPreferFastElementsSizeFactor);
    size_t current_counter = isolate->elements_deletion_counter();
    if (current_counter < length / kLengthFraction) {
      isolate->set_elements_deletion_counter(current_counter + 1);
      return;
    }
    // Reset the counter whenever the full check is performed.
    isolate->set_elements_deletion_counter(0);

    if (!IsJSArray(*obj)) {
      uint32_t i;
      for (i = entry + 1; i < length; i++) {
        if (!backing_store->is_the_hole(isolate, i)) break;
      }
      if (i == length) {
        DeleteAtEnd(obj, backing_store, entry);
        return;
      }
    }
    int num_used = 0;
    for (int i = 0; i < backing_store->length(); ++i) {
      if (!backing_store->is_the_hole(isolate, i)) {
        ++num_used;
        // Bail out if a number dictionary wouldn't be able to save much space.
        if (NumberDictionary::kPreferFastElementsSizeFactor *
                NumberDictionary::ComputeCapacity(num_used) *
                NumberDictionary::kEntrySize >
            static_cast<uint32_t>(backing_store->length())) {
          return;
        }
      }
    }
    JSObject::NormalizeElements(obj);
  }

  static void ReconfigureImpl(Handle<JSObject> object,
                              DirectHandle<FixedArrayBase> store,
                              InternalIndex entry, DirectHandle<Object> value,
                              PropertyAttributes attributes) {
    Handle<NumberDictionary> dictionary = JSObject::NormalizeElements(object);
    entry = InternalIndex(
        dictionary->FindEntry(object->GetIsolate(), entry.as_uint32()));
    DictionaryElementsAccessor::ReconfigureImpl(
        object, Cast<FixedArrayBase>(dictionary), entry, value, attributes);
  }

  static Maybe<bool> AddImpl(Handle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    DCHECK_EQ(NONE, attributes);
    ElementsKind from_kind = object->GetElementsKind();
    ElementsKind to_kind = Subclass::kind();
    if (IsDictionaryElementsKind(from_kind) ||
        IsDoubleElementsKind(from_kind) != IsDoubleElementsKind(to_kind) ||
        Subclass::GetCapacityImpl(*object, object->elements()) !=
            new_capacity) {
      MAYBE_RETURN(Subclass::GrowCapacityAndConvertImpl(object, new_capacity),
                   Nothing<bool>());
    } else {
      if (IsFastElementsKind(from_kind) && from_kind != to_kind) {
        JSObject::TransitionElementsKind(object, to_kind);
      }
      if (IsSmiOrObjectElementsKind(from_kind)) {
        DCHECK(IsSmiOrObjectElementsKind(to_kind));
        JSObject::EnsureWritableFastElements(object);
      }
    }
    Subclass::SetImpl(object, InternalIndex(index), *value);
    return Just(true);
  }

  static void DeleteImpl(Handle<JSObject> obj, InternalIndex entry) {
    ElementsKind kind = KindTraits::Kind;
    if (IsFastPackedElementsKind(kind) ||
        kind == PACKED_NONEXTENSIBLE_ELEMENTS) {
      JSObject::TransitionElementsKind(obj, GetHoleyElementsKind(kind));
    }
    if (IsSmiOrObjectElementsKind(KindTraits::Kind) ||
        IsNonextensibleElementsKind(kind)) {
      JSObject::EnsureWritableFastElements(obj);
    }
    DeleteCommon(obj, entry.as_uint32(),
                 handle(obj->elements(), obj->GetIsolate()));
  }

  static bool HasEntryImpl(Isolate* isolate,
                           Tagged<FixedArrayBase> backing_store,
                           InternalIndex entry) {
    return !Cast<BackingStore>(backing_store)
                ->is_the_hole(isolate, entry.as_int());
  }

  static uint32_t NumberOfElementsImpl(Tagged<JSObject> receiver,
                                       Tagged<FixedArrayBase> backing_store) {
    size_t max_index = Subclass::GetMaxIndex(receiver, backing_store);
    DCHECK_LE(max_index, std::numeric_limits<uint32_t>::max());
    if (IsFastPackedElementsKind(Subclass::kind())) {
      return static_cast<uint32_t>(max_index);
    }
    Isolate* isolate = receiver->GetIsolate();
    uint32_t count = 0;
    for (size_t i = 0; i < max_index; i++) {
      if (Subclass::HasEntryImpl(isolate, backing_store, InternalIndex(i))) {
        count++;
      }
    }
    return count;
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus AddElementsToKeyAccumulatorImpl(
      DirectHandle<JSObject> receiver, KeyAccumulator* accumulator,
      AddKeyConversion convert) {
    Isolate* isolate = accumulator->isolate();
    DirectHandle<FixedArrayBase> elements(receiver->elements(), isolate);
    size_t length = Subclass::GetMaxNumberOfEntries(*receiver, *elements);
    for (size_t i = 0; i < length; i++) {
      if (IsFastPackedElementsKind(KindTraits::Kind) ||
          HasEntryImpl(isolate, *elements, InternalIndex(i))) {
        RETURN_FAILURE_IF_NOT_SUCCESSFUL(accumulator->AddKey(
            Subclass::GetImpl(isolate, *elements, InternalIndex(i)), convert));
      }
    }
    return ExceptionStatus::kSuccess;
  }

  static void ValidateContents(Tagged<JSObject> holder, size_t length) {
#if DEBUG
    Isolate* isolate = holder->GetIsolate();
    Heap* heap = isolate->heap();
    Tagged<FixedArrayBase> elements = holder->elements();
    Tagged<Map> map = elements->map();
    if (IsSmiOrObjectElementsKind(KindTraits::Kind)) {
      DCHECK_NE(map, ReadOnlyRoots(heap).fixed_double_array_map());
    } else if (IsDoubleElementsKind(KindTraits::Kind)) {
      DCHECK_NE(map, ReadOnlyRoots(heap).fixed_cow_array_map());
      if (map == ReadOnlyRoots(heap).fixed_array_map()) DCHECK_EQ(0u, length);
    } else {
      UNREACHABLE();
    }
    if (length == 0u) return;  // nothing to do!
#if ENABLE_SLOW_DCHECKS
    DisallowGarbageCollection no_gc;
    Tagged<BackingStore> backing_store = Cast<BackingStore>(elements);
    DCHECK(length <= std::numeric_limits<int>::max());
    int length_int = static_cast<int>(length);
    if (IsSmiElementsKind(KindTraits::Kind)) {
      HandleScope scope(isolate);
      for (int i = 0; i < length_int; i++) {
        Tagged<Object> element = Cast<FixedArray>(backing_store)->get(i);
        DCHECK(IsSmi(element) || (IsHoleyElementsKind(KindTraits::Kind) &&
                                  IsTheHole(element, isolate)));
      }
    } else if (KindTraits::Kind == PACKED_ELEMENTS ||
               KindTraits::Kind == PACKED_DOUBLE_ELEMENTS) {
      for (int i = 0; i < length_int; i++) {
        DCHECK(!backing_store->is_the_hole(isolate, i));
      }
    } else {
      DCHECK(IsHoleyElementsKind(KindTraits::Kind));
    }
#endif
#endif
  }

  static MaybeHandle<Object> PopImpl(Handle<JSArray> receiver) {
    return Subclass::RemoveElement(receiver, AT_END);
  }

  static MaybeHandle<Object> ShiftImpl(Handle<JSArray> receiver) {
    return Subclass::RemoveElement(receiver, AT_START);
  }

  static Maybe<uint32_t> PushImpl(Handle<JSArray> receiver,
                                  BuiltinArguments* args, uint32_t push_size) {
    Handle<FixedArrayBase> backing_store(receiver->elements(),
                                         receiver->GetIsolate());
    return Subclass::AddArguments(receiver, backing_store, args, push_size,
                                  AT_END);
  }

  static Maybe<uint32_t> UnshiftImpl(Handle<JSArray> receiver,
                                     BuiltinArguments* args,
                                     uint32_t unshift_size) {
    Handle<FixedArrayBase> backing_store(receiver->elements(),
                                         receiver->GetIsolate());
    return Subclass::AddArguments(receiver, backing_store, args, unshift_size,
                                  AT_START);
  }

  static void MoveElements(Isolate* isolate, DirectHandle<JSArray> receiver,
                           Handle<FixedArrayBase> backing_store, int dst_index,
                           int src_index, int len, int hole_start,
                           int hole_end) {
    DisallowGarbageCollection no_gc;
    Tagged<BackingStore> dst_elms = Cast<BackingStore>(*backing_store);
    if (len > JSArray::kMaxCopyElements && dst_index == 0 &&
        isolate->heap()->CanMoveObjectStart(dst_elms)) {
      dst_elms = Cast<BackingStore>(
          isolate->heap()->LeftTrimFixedArray(dst_elms, src_index));
      // Update all the copies of this backing_store handle.
      backing_store.PatchValue(dst_elms);
      receiver->set_elements(dst_elms);
      // Adjust the hole offset as the array has been shrunk.
      hole_end -= src_index;
      DCHECK_LE(hole_start, backing_store->length());
      DCHECK_LE(hole_end, backing_store->length());
    } else if (len != 0) {
      WriteBarrierMode mode =
          GetWriteBarrierMode(dst_elms, KindTraits::Kind, no_gc);
      dst_elms->MoveElements(isolate, dst_index, src_index, len, mode);
    }
    if (hole_start != hole_end) {
      dst_elms->FillWithHoles(hole_start, hole_end);
    }
  }

  static MaybeHandle<Object> FillImpl(Handle<JSObject> receiver,
                                      DirectHandle<Object> obj_value,
                                      size_t start, size_t end) {
    // Ensure indexes are within array bounds
    DCHECK_LE(0, start);
    DCHECK_LE(start, end);

    // Make sure COW arrays are copied.
    if (IsSmiOrObjectElementsKind(Subclass::kind())) {
      JSObject::EnsureWritableFastElements(receiver);
    }

    // Make sure we have enough space.
    DCHECK_LE(end, std::numeric_limits<uint32_t>::max());
    if (end > Subclass::GetCapacityImpl(*receiver, receiver->elements())) {
      MAYBE_RETURN_NULL(Subclass::GrowCapacityAndConvertImpl(
          receiver, static_cast<uint32_t>(end)));
      CHECK_EQ(Subclass::kind(), receiver->GetElementsKind());
    }
    DCHECK_LE(end, Subclass::GetCapacityImpl(*receiver, receiver->elements()));

    for (size_t index = start; index < end; ++index) {
      Subclass::SetImpl(receiver, InternalIndex(index), *obj_value);
    }
    return MaybeHandle<Object>(receiver);
  }

  static Maybe<bool> IncludesValueImpl(Isolate* isolate,
                                       DirectHandle<JSObject> receiver,
                                       DirectHandle<Object> search_value,
                                       size_t start_from, size_t length) {
    DCHECK(JSObject::PrototypeHasNoElements(isolate, *receiver));
    DisallowGarbageCollection no_gc;
    Tagged<FixedArrayBase> elements_base = receiver->elements();
    Tagged<Object> the_hole = ReadOnlyRoots(isolate).the_hole_value();
    Tagged<Object> undefined = ReadOnlyRoots(isolate).undefined_value();
    Tagged<Object> value = *search_value;

    if (start_from >= length) return Just(false);

    // Elements beyond the capacity of the backing store treated as undefined.
    size_t elements_length = static_cast<size_t>(elements_base->length());
    if (value == undefined && elements_length < length) return Just(true);
    if (elements_length == 0) {
      DCHECK_NE(value, undefined);
      return Just(false);
    }

    length = std::min(elements_length, length);
    DCHECK_LE(length, std::numeric_limits<int>::max());

    if (!IsNumber(value)) {
      if (value == undefined) {
        // Search for `undefined` or The Hole. Even in the case of
        // PACKED_DOUBLE_ELEMENTS or PACKED_SMI_ELEMENTS, we might encounter The
        // Hole here, since the {length} used here can be larger than
        // JSArray::length.
        if (IsSmiOrObjectElementsKind(Subclass::kind()) ||
            IsAnyNonextensibleElementsKind(Subclass::kind())) {
          Tagged<FixedArray> elements = Cast<FixedArray>(receiver->elements());

          for (size_t k = start_from; k < length; ++k) {
            Tagged<Object> element_k = elements->get(static_cast<int>(k));

            if (element_k == the_hole || element_k == undefined) {
              return Just(true);
            }
          }
          return Just(false);
        } else {
          // Search for The Hole in HOLEY_DOUBLE_ELEMENTS or
          // PACKED_DOUBLE_ELEMENTS.
          DCHECK(IsDoubleElementsKind(Subclass::kind()));
          Tagged<FixedDoubleArray> elements =
              Cast<FixedDoubleArray>(receiver->elements());

          for (size_t k = start_from; k < length; ++k) {
            if (elements->is_the_hole(static_cast<int>(k))) return Just(true);
          }
          return Just(false);
        }
      } else if (!IsObjectElementsKind(Subclass::kind()) &&
                 !IsAnyNonextensibleElementsKind(Subclass::kind())) {
        // Search for non-number, non-Undefined value, with either
        // PACKED_SMI_ELEMENTS, PACKED_DOUBLE_ELEMENTS, HOLEY_SMI_ELEMENTS or
        // HOLEY_DOUBLE_ELEMENTS. Guaranteed to return false, since these
        // elements kinds can only contain Number values or undefined.
        return Just(false);
      } else {
        // Search for non-number, non-Undefined value with either
        // PACKED_ELEMENTS or HOLEY_ELEMENTS.
        DCHECK(IsObjectElementsKind(Subclass::kind()) ||
               IsAnyNonextensibleElementsKind(Subclass::kind()));
        Tagged<FixedArray> elements = Cast<FixedArray>(receiver->elements());

        for (size_t k = start_from; k < length; ++k) {
          Tagged<Object> element_k = elements->get(static_cast<int>(k));
          if (element_k == the_hole) continue;
          if (Object::SameValueZero(value, element_k)) return Just(true);
        }
        return Just(false);
      }
    } else {
      if (!IsNaN(value)) {
        double search_number = Object::NumberValue(value);
        if (IsDoubleElementsKind(Subclass::kind())) {
          // Search for non-NaN Number in PACKED_DOUBLE_ELEMENTS or
          // HOLEY_DOUBLE_ELEMENTS --- Skip TheHole, and trust UCOMISD or
          // similar operation for result.
          Tagged<FixedDoubleArray> elements =
              Cast<FixedDoubleArray>(receiver->elements());

          for (size_t k = start_from; k < length; ++k) {
            if (elements->is_the_hole(static_cast<int>(k))) continue;
            if (elements->get_scalar(static_cast<int>(k)) == search_number) {
              return Just(true);
            }
          }
          return Just(false);
        } else {
          // Search for non-NaN Number in PACKED_ELEMENTS, HOLEY_ELEMENTS,
          // PACKED_SMI_ELEMENTS or HOLEY_SMI_ELEMENTS --- Skip non-Numbers,
          // and trust UCOMISD or similar operation for result
          Tagged<FixedArray> elements = Cast<FixedArray>(receiver->elements());

          for (size_t k = start_from; k < length; ++k) {
            Tagged<Object> element_k = elements->get(static_cast<int>(k));
            if (IsNumber(element_k) &&
                Object::NumberValue(element_k) == search_number) {
              return Just(true);
            }
          }
          return Just(false);
        }
      } else {
        // Search for NaN --- NaN cannot be represented with Smi elements, so
        // abort if ElementsKind is PACKED_SMI_ELEMENTS or HOLEY_SMI_ELEMENTS
        if (IsSmiElementsKind(Subclass::kind())) return Just(false);

        if (IsDoubleElementsKind(Subclass::kind())) {
          // Search for NaN in PACKED_DOUBLE_ELEMENTS or
          // HOLEY_DOUBLE_ELEMENTS --- Skip The Hole and trust
          // std::isnan(elementK) for result
          Tagged<FixedDoubleArray> elements =
              Cast<FixedDoubleArray>(receiver->elements());

          for (size_t k = start_from; k < length; ++k) {
            if (elements->is_the_hole(static_cast<int>(k))) continue;
            if (std::isnan(elements->get_scalar(static_cast<int>(k)))) {
              return Just(true);
            }
          }
          return Just(false);
        } else {
          // Search for NaN in PACKED_ELEMENTS or HOLEY_ELEMENTS. Return true
          // if elementK->IsHeapNumber() && std::isnan(elementK->Number())
          DCHECK(IsObjectElementsKind(Subclass::kind()) ||
                 IsAnyNonextensibleElementsKind(Subclass::kind()));
          Tagged<FixedArray> elements = Cast<FixedArray>(receiver->elements());

          for (size_t k = start_from; k < length; ++k) {
            if (IsNaN(elements->get(static_cast<int>(k)))) return Just(true);
          }
          return Just(false);
        }
      }
    }
  }

  static Handle<FixedArray> CreateListFromArrayLikeImpl(
      Isolate* isolate, DirectHandle<JSObject> object, uint32_t length) {
    Handle<FixedArray> result = isolate->factory()->NewFixedArray(length);
    DirectHandle<FixedArrayBase> elements(object->elements(), isolate);
    for (uint32_t i = 0; i < length; i++) {
      InternalIndex entry(i);
      if (!Subclass::HasEntryImpl(isolate, *elements, entry)) continue;
      Handle<Object> value;
      value = Subclass::GetImpl(isolate, *elements, entry);
      if (IsName(*value)) {
        value = isolate->factory()->InternalizeName(Cast<Name>(value));
      }
      result->set(i, *value);
    }
    return result;
  }

  static MaybeHandle<Object> RemoveElement(Handle<JSArray> receiver,
                                           Where remove_position) {
    Isolate* isolate = receiver->GetIsolate();
    ElementsKind kind = KindTraits::Kind;
    if (IsSmiOrObjectElementsKind(kind)) {
      HandleScope scope(isolate);
      JSObject::EnsureWritableFastElements(receiver);
    }
    Handle<FixedArrayBase> backing_store(receiver->elements(), isolate);
    uint32_t length = static_cast<uint32_t>(Smi::ToInt(receiver->length()));
    DCHECK_GT(length, 0);
    int new_length = length - 1;
    int remove_index = remove_position == AT_START ? 0 : new_length;
    Handle<Object> result =
        Subclass::GetImpl(isolate, *backing_store, InternalIndex(remove_index));
    if (remove_position == AT_START) {
      Subclass::MoveElements(isolate, receiver, backing_store, 0, 1, new_length,
                             0, 0);
    }
    MAYBE_RETURN_NULL(
        Subclass::SetLengthImpl(isolate, receiver, new_length, backing_store));

    if (IsHoleyElementsKind(kind) && IsTheHole(*result, isolate)) {
      return isolate->factory()->undefined_value();
    }
    return MaybeHandle<Object>(result);
  }

  static Maybe<uint32_t> AddArguments(Handle<JSArray> receiver,
                                      Handle<FixedArrayBase> backing_store,
                                      BuiltinArguments* args, uint32_t add_size,
                                      Where add_position) {
    uint32_t length = Smi::ToInt(receiver->length());
    DCHECK_LT(0, add_size);
    uint32_t elms_len = backing_store->length();
    // Check we do not overflow the new_length.
    DCHECK(add_size <= static_cast<uint32_t>(Smi::kMaxValue - length));
    uint32_t new_length = length + add_size;
    Isolate* isolate = receiver->GetIsolate();

    if (new_length > elms_len) {
      // New backing storage is needed.
      uint32_t capacity = JSObject::NewElementsCapacity(new_length);
      // If we add arguments to the start we have to shift the existing objects.
      int copy_dst_index = add_position == AT_START ? add_size : 0;
      // Copy over all objects to a new backing_store.
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, backing_store,
          Subclass::ConvertElementsWithCapacity(receiver, backing_store,
                                                KindTraits::Kind, capacity, 0,
                                                copy_dst_index),
          Nothing<uint32_t>());
      receiver->set_elements(*backing_store);
    } else if (add_position == AT_START) {
      // If the backing store has enough capacity and we add elements to the
      // start we have to shift the existing objects.
      Subclass::MoveElements(isolate, receiver, backing_store, add_size, 0,
                             length, 0, 0);
    }

    int insertion_index = add_position == AT_START ? 0 : length;
    // Copy the arguments to the start.
    Subclass::CopyArguments(args, backing_store, add_size, 1, insertion_index);
    // Set the length.
    receiver->set_length(Smi::FromInt(new_length));
    return Just(new_length);
  }

  static void CopyArguments(BuiltinArguments* args,
                            DirectHandle<FixedArrayBase> dst_store,
                            uint32_t copy_size, uint32_t src_index,
                            uint32_t dst_index) {
    // Add the provided values.
    DisallowGarbageCollection no_gc;
    Tagged<FixedArrayBase> raw_backing_store = *dst_store;
    WriteBarrierMode mode = raw_backing_store->GetWriteBarrierMode(no_gc);
    for (uint32_t i = 0; i < copy_size; i++) {
      Tagged<Object> argument = (*args)[src_index + i];
      DCHECK(!IsTheHole(argument));
      Subclass::SetImpl(raw_backing_store, InternalIndex(dst_index + i),
                        argument, mode);
    }
  }
};

template <typename Subclass, typename KindTraits>
class FastSmiOrObjectElementsAccessor
    : public FastElementsAccessor<Subclass, KindTraits> {
 public:
  static inline void SetImpl(DirectHandle<JSObject> holder, InternalIndex entry,
                             Tagged<Object> value) {
    SetImpl(holder->elements(), entry, value);
  }

  static inline void SetImpl(Tagged<FixedArrayBase> backing_store,
                             InternalIndex entry, Tagged<Object> value) {
    Cast<FixedArray>(backing_store)->set(entry.as_int(), value);
  }

  static inline void SetImpl(Tagged<FixedArrayBase> backing_store,
                             InternalIndex entry, Tagged<Object> value,
                             WriteBarrierMode mode) {
    Cast<FixedArray>(backing_store)->set(entry.as_int(), value, mode);
  }

  static Tagged<Object> GetRaw(Tagged<FixedArray> backing_store,
                               InternalIndex entry) {
    return backing_store->get(entry.as_int());
  }

  // NOTE: this method violates the handlified function signature convention:
  // raw pointer parameters in the function that allocates.
  // See ElementsAccessor::CopyElements() for details.
  // This method could actually allocate if copying from double elements to
  // object elements.
  static void CopyElementsImpl(Isolate* isolate, Tagged<FixedArrayBase> from,
                               uint32_t from_start, Tagged<FixedArrayBase> to,
                               ElementsKind from_kind, uint32_t to_start,
                               int packed_size, int copy_size) {
    DisallowGarbageCollection no_gc;
    ElementsKind to_kind = KindTraits::Kind;
    switch (from_kind) {
      case PACKED_SMI_ELEMENTS:
      case HOLEY_SMI_ELEMENTS:
      case PACKED_ELEMENTS:
      case PACKED_FROZEN_ELEMENTS:
      case PACKED_SEALED_ELEMENTS:
      case PACKED_NONEXTENSIBLE_ELEMENTS:
      case HOLEY_ELEMENTS:
      case HOLEY_FROZEN_ELEMENTS:
      case HOLEY_SEALED_ELEMENTS:
      case HOLEY_NONEXTENSIBLE_ELEMENTS:
      case SHARED_ARRAY_ELEMENTS:
        CopyObjectToObjectElements(isolate, from, from_kind, from_start, to,
                                   to_kind, to_start, copy_size);
        break;
      case PACKED_DOUBLE_ELEMENTS:
      case HOLEY_DOUBLE_ELEMENTS: {
        AllowGarbageCollection allow_allocation;
        DCHECK(IsObjectElementsKind(to_kind));
        CopyDoubleToObjectElements(isolate, from, from_start, to, to_start,
                                   copy_size);
        break;
      }
      case DICTIONARY_ELEMENTS:
        CopyDictionaryToObjectElements(isolate, from, from_start, to, to_kind,
                                       to_start, copy_size);
        break;
      case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
      case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
      case FAST_STRING_WRAPPER_ELEMENTS:
      case SLOW_STRING_WRAPPER_ELEMENTS:
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:
        TYPED_ARRAYS(TYPED_ARRAY_CASE)
        RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
      case WASM_ARRAY_ELEMENTS:
        // This function is currently only used for JSArrays with non-zero
        // length.
        UNREACHABLE();
      case NO_ELEMENTS:
        break;  // Nothing to do.
    }
  }

  static Maybe<bool> CollectValuesOrEntriesImpl(
      Isolate* isolate, DirectHandle<JSObject> object,
      DirectHandle<FixedArray> values_or_entries, bool get_entries,
      int* nof_items, PropertyFilter filter) {
    int count = 0;
    if (get_entries) {
      // Collecting entries needs to allocate, so this code must be handlified.
      DirectHandle<FixedArray> elements(Cast<FixedArray>(object->elements()),
                                        isolate);
      uint32_t length = elements->length();
      for (uint32_t index = 0; index < length; ++index) {
        InternalIndex entry(index);
        if (!Subclass::HasEntryImpl(isolate, *elements, entry)) continue;
        DirectHandle<Object> value =
            Subclass::GetImpl(isolate, *elements, entry);
        value = MakeEntryPair(isolate, index, value);
        values_or_entries->set(count++, *value);
      }
    } else {
      // No allocations here, so we can avoid handlification overhead.
      DisallowGarbageCollection no_gc;
      Tagged<FixedArray> elements = Cast<FixedArray>(object->elements());
      uint32_t length = elements->length();
      for (uint32_t index = 0; index < length; ++index) {
        InternalIndex entry(index);
        if (!Subclass::HasEntryImpl(isolate, elements, entry)) continue;
        Tagged<Object> value = GetRaw(elements, entry);
        values_or_entries->set(count++, value);
      }
    }
    *nof_items = count;
    return Just(true);
  }

  static Maybe<int64_t> IndexOfValueImpl(Isolate* isolate,
                                         DirectHandle<JSObject> receiver,
                                         DirectHandle<Object> search_value,
                                         size_t start_from, size_t length) {
    DCHECK(JSObject::PrototypeHasNoElements(isolate, *receiver));
    DisallowGarbageCollection no_gc;
    Tagged<FixedArrayBase> elements_base = receiver->elements();
    Tagged<Object> value = *search_value;

    if (start_from >= length) return Just<int64_t>(-1);

    length = std::min(static_cast<size_t>(elements_base->length()), length);

    // Only FAST_{,HOLEY_}ELEMENTS can store non-numbers.
    if (!IsNumber(value) && !IsObjectElementsKind(Subclass::kind()) &&
        !IsAnyNonextensibleElementsKind(Subclass::kind())) {
      return Just<int64_t>(-1);
    }
    // NaN can never be found by strict equality.
    if (IsNaN(value)) return Just<int64_t>(-1);

    // k can be greater than receiver->length() below, but it is bounded by
    // elements_base->length() so we never read out of bounds. This means that
    // elements->get(k) can return the hole, for which the StrictEquals will
    // always fail.
    Tagged<FixedArray> elements = Cast<FixedArray>(receiver->elements());
    static_assert(FixedArray::kMaxLength <=
                  std::numeric_limits<uint32_t>::max());
    for (size_t k = start_from; k < length; ++k) {
      if (Object::StrictEquals(value,
                               elements->get(static_cast<uint32_t>(k)))) {
        return Just<int64_t>(k);
      }
    }
    return Just<int64_t>(-1);
  }
};

class FastPackedSmiElementsAccessor
    : public FastSmiOrObjectElementsAccessor<
          FastPackedSmiElementsAccessor,
          ElementsKindTraits<PACKED_SMI_ELEMENTS>> {};

class FastHoleySmiElementsAccessor
    : public FastSmiOrObjectElementsAccessor<
          FastHoleySmiElementsAccessor,
          ElementsKindTraits<HOLEY_SMI_ELEMENTS>> {};

class FastPackedObjectElementsAccessor
    : public FastSmiOrObjectElementsAccessor<
          FastPackedObjectElementsAccessor,
          ElementsKindTraits<PACKED_ELEMENTS>> {};

template <typename Subclass, typename KindTraits>
class FastNonextensibleObjectElementsAccessor
    : public FastSmiOrObjectElementsAccessor<Subclass, KindTraits> {
 public:
  using BackingStore = typename KindTraits::BackingStore;

  static Maybe<uint32_t> PushImpl(DirectHandle<JSArray> receiver,
                                  BuiltinArguments* args, uint32_t push_size) {
    UNREACHABLE();
  }

  static Maybe<bool> AddImpl(DirectHandle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    UNREACHABLE();
  }

  // TODO(duongn): refactor this due to code duplication of sealed version.
  // Consider using JSObject::NormalizeElements(). Also consider follow the fast
  // element logic instead of changing to dictionary mode.
  static Maybe<bool> SetLengthImpl(Isolate* isolate, Handle<JSArray> array,
                                   uint32_t length,
                                   DirectHandle<FixedArrayBase> backing_store) {
    uint32_t old_length = 0;
    CHECK(Object::ToArrayIndex(array->length(), &old_length));
    if (length == old_length) {
      // Do nothing.
      return Just(true);
    }

    // Transition to DICTIONARY_ELEMENTS.
    // Convert to dictionary mode.
    Handle<NumberDictionary> new_element_dictionary =
        old_length == 0 ? isolate->factory()->empty_slow_element_dictionary()
                        : array->GetElementsAccessor()->Normalize(array);

    // Migrate map.
    DirectHandle<Map> new_map = Map::Copy(
        isolate, handle(array->map(), isolate), "SlowCopyForSetLengthImpl");
    new_map->set_is_extensible(false);
    new_map->set_elements_kind(DICTIONARY_ELEMENTS);
    JSObject::MigrateToMap(isolate, array, new_map);

    if (!new_element_dictionary.is_null()) {
      array->set_elements(*new_element_dictionary);
    }

    if (array->elements() !=
        ReadOnlyRoots(isolate).empty_slow_element_dictionary()) {
      Handle<NumberDictionary> dictionary(array->element_dictionary(), isolate);
      // Make sure we never go back to the fast case
      array->RequireSlowElements(*dictionary);
      JSObject::ApplyAttributesToDictionary(isolate, ReadOnlyRoots(isolate),
                                            dictionary,
                                            PropertyAttributes::NONE);
    }

    // Set length.
    Handle<FixedArrayBase> new_backing_store(array->elements(), isolate);
    return DictionaryElementsAccessor::SetLengthImpl(isolate, array, length,
                                                     new_backing_store);
  }
};

class FastPackedNonextensibleObjectElementsAccessor
    : public FastNonextensibleObjectElementsAccessor<
          FastPackedNonextensibleObjectElementsAccessor,
          ElementsKindTraits<PACKED_NONEXTENSIBLE_ELEMENTS>> {};

class FastHoleyNonextensibleObjectElementsAccessor
    : public FastNonextensibleObjectElementsAccessor<
          FastHoleyNonextensibleObjectElementsAccessor,
          ElementsKindTraits<HOLEY_NONEXTENSIBLE_ELEMENTS>> {};

template <typename Subclass, typename KindTraits>
class FastSealedObjectElementsAccessor
    : public FastSmiOrObjectElementsAccessor<Subclass, KindTraits> {
 public:
  using BackingStore = typename KindTraits::BackingStore;

  static Handle<Object> RemoveElement(DirectHandle<JSArray> receiver,
                                      Where remove_position) {
    UNREACHABLE();
  }

  static void DeleteImpl(DirectHandle<JSObject> obj, InternalIndex entry) {
    UNREACHABLE();
  }

  static void DeleteAtEnd(DirectHandle<JSObject> obj,
                          DirectHandle<BackingStore> backing_store,
                          uint32_t entry) {
    UNREACHABLE();
  }

  static void DeleteCommon(DirectHandle<JSObject> obj, uint32_t entry,
                           DirectHandle<FixedArrayBase> store) {
    UNREACHABLE();
  }

  static MaybeHandle<Object> PopImpl(DirectHandle<JSArray> receiver) {
    UNREACHABLE();
  }

  static Maybe<uint32_t> PushImpl(DirectHandle<JSArray> receiver,
                                  BuiltinArguments* args, uint32_t push_size) {
    UNREACHABLE();
  }

  static Maybe<bool> AddImpl(DirectHandle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    UNREACHABLE();
  }

  // TODO(duongn): refactor this due to code duplication of nonextensible
  // version. Consider using JSObject::NormalizeElements(). Also consider follow
  // the fast element logic instead of changing to dictionary mode.
  static Maybe<bool> SetLengthImpl(Isolate* isolate, Handle<JSArray> array,
                                   uint32_t length,
                                   DirectHandle<FixedArrayBase> backing_store) {
    uint32_t old_length = 0;
    CHECK(Object::ToArrayIndex(array->length(), &old_length));
    if (length == old_length) {
      // Do nothing.
      return Just(true);
    }

    // Transition to DICTIONARY_ELEMENTS.
    // Convert to dictionary mode
    DirectHandle<NumberDictionary> new_element_dictionary =
        old_length == 0 ? isolate->factory()->empty_slow_element_dictionary()
                        : array->GetElementsAccessor()->Normalize(array);

    // Migrate map.
    DirectHandle<Map> new_map = Map::Copy(
        isolate, handle(array->map(), isolate), "SlowCopyForSetLengthImpl");
    new_map->set_is_extensible(false);
    new_map->set_elements_kind(DICTIONARY_ELEMENTS);
    JSObject::MigrateToMap(isolate, array, new_map);

    if (!new_element_dictionary.is_null()) {
      array->set_elements(*new_element_dictionary);
    }

    if (array->elements() !=
        ReadOnlyRoots(isolate).empty_slow_element_dictionary()) {
      Handle<NumberDictionary> dictionary(array->element_dictionary(), isolate);
      // Make sure we never go back to the fast case
      array->RequireSlowElements(*dictionary);
      JSObject::ApplyAttributesToDictionary(isolate, ReadOnlyRoots(isolate),
                                            dictionary,
                                            PropertyAttributes::SEALED);
    }

    // Set length
    Handle<FixedArrayBase> new_backing_store(array->elements(), isolate);
    return DictionaryElementsAccessor::SetLengthImpl(isolate, array, length,
                                                     new_backing_store);
  }
};

class FastPackedSealedObjectElementsAccessor
    : public FastSealedObjectElementsAccessor<
          FastPackedSealedObjectElementsAccessor,
          ElementsKindTraits<PACKED_SEALED_ELEMENTS>> {};

class SharedArrayElementsAccessor
    : public FastSealedObjectElementsAccessor<
          SharedArrayElementsAccessor,
          ElementsKindTraits<SHARED_ARRAY_ELEMENTS>> {
 public:
  static Handle<Object> GetAtomicInternalImpl(
      Isolate* isolate, Tagged<FixedArrayBase> backing_store,
      InternalIndex entry, SeqCstAccessTag tag) {
    return handle(Cast<BackingStore>(backing_store)->get(entry.as_int(), tag),
                  isolate);
  }

  static void SetAtomicInternalImpl(Tagged<FixedArrayBase> backing_store,
                                    InternalIndex entry, Tagged<Object> value,
                                    SeqCstAccessTag tag) {
    Cast<BackingStore>(backing_store)->set(entry.as_int(), value, tag);
  }

  static Handle<Object> SwapAtomicInternalImpl(
      Isolate* isolate, Tagged<FixedArrayBase> backing_store,
      InternalIndex entry, Tagged<Object> value, SeqCstAccessTag tag) {
    return handle(
        Cast<BackingStore>(backing_store)->swap(entry.as_int(), value, tag),
        isolate);
  }

  static Tagged<Object> CompareAndSwapAtomicInternalImpl(
      Tagged<FixedArrayBase> backing_store, InternalIndex entry,
      Tagged<Object> expected, Tagged<Object> value, SeqCstAccessTag tag) {
    return Cast<BackingStore>(backing_store)
        ->compare_and_swap(entry.as_int(), expected, value, tag);
  }
};

class FastHoleySealedObjectElementsAccessor
    : public FastSealedObjectElementsAccessor<
          FastHoleySealedObjectElementsAccessor,
          ElementsKindTraits<HOLEY_SEALED_ELEMENTS>> {};

template <typename Subclass, typename KindTraits>
class FastFrozenObjectElementsAccessor
    : public FastSmiOrObjectElementsAccessor<Subclass, KindTraits> {
 public:
  using BackingStore = typename KindTraits::BackingStore;

  static inline void SetImpl(DirectHandle<JSObject> holder, InternalIndex entry,
                             Tagged<Object> value) {
    UNREACHABLE();
  }

  static inline void SetImpl(Tagged<FixedArrayBase> backing_store,
                             InternalIndex entry, Tagged<Object> value) {
    UNREACHABLE();
  }

  static inline void SetImpl(Tagged<FixedArrayBase> backing_store,
                             InternalIndex entry, Tagged<Object> value,
                             WriteBarrierMode mode) {
    UNREACHABLE();
  }

  static Handle<Object> RemoveElement(DirectHandle<JSArray> receiver,
                                      Where remove_position) {
    UNREACHABLE();
  }

  static void DeleteImpl(DirectHandle<JSObject> obj, InternalIndex entry) {
    UNREACHABLE();
  }

  static void DeleteAtEnd(DirectHandle<JSObject> obj,
                          DirectHandle<BackingStore> backing_store,
                          uint32_t entry) {
    UNREACHABLE();
  }

  static void DeleteCommon(DirectHandle<JSObject> obj, uint32_t entry,
                           DirectHandle<FixedArrayBase> store) {
    UNREACHABLE();
  }

  static MaybeHandle<Object> PopImpl(DirectHandle<JSArray> receiver) {
    UNREACHABLE();
  }

  static Maybe<uint32_t> PushImpl(DirectHandle<JSArray> receiver,
                                  BuiltinArguments* args, uint32_t push_size) {
    UNREACHABLE();
  }

  static Maybe<bool> AddImpl(DirectHandle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    UNREACHABLE();
  }

  static Maybe<bool> SetLengthImpl(Isolate* isolate,
                                   DirectHandle<JSArray> array, uint32_t length,
                                   DirectHandle<FixedArrayBase> backing_store) {
    UNREACHABLE();
  }

  static void ReconfigureImpl(DirectHandle<JSObject> object,
                              DirectHandle<FixedArrayBase> store,
                              InternalIndex entry, DirectHandle<Object> value,
                              PropertyAttributes attributes) {
    UNREACHABLE();
  }
};

class FastPackedFrozenObjectElementsAccessor
    : public FastFrozenObjectElementsAccessor<
          FastPackedFrozenObjectElementsAccessor,
          ElementsKindTraits<PACKED_FROZEN_ELEMENTS>> {};

class FastHoleyFrozenObjectElementsAccessor
    : public FastFrozenObjectElementsAccessor<
          FastHoleyFrozenObjectElementsAccessor,
          ElementsKindTraits<HOLEY_FROZEN_ELEMENTS>> {};

class FastHoleyObjectElementsAccessor
    : public FastSmiOrObjectElementsAccessor<
          FastHoleyObjectElementsAccessor, ElementsKindTraits<HOLEY_ELEMENTS>> {
};

// Helper templates to statically determine if our destination type can contain
// the source type.
template <ElementsKind Kind, typename ElementType, ElementsKind SourceKind,
          typename SourceElementType>
struct CopyBetweenBackingStoresImpl;

template <typename Subclass, typename KindTraits>
class FastDoubleElementsAccessor
    : public FastElementsAccessor<Subclass, KindTraits> {
 public:
  static Handle<Object> GetImpl(Isolate* isolate,
                                Tagged<FixedArrayBase> backing_store,
                                InternalIndex entry) {
    return FixedDoubleArray::get(Cast<FixedDoubleArray>(backing_store),
                                 entry.as_int(), isolate);
  }

  static inline void SetImpl(DirectHandle<JSObject> holder, InternalIndex entry,
                             Tagged<Object> value) {
    SetImpl(holder->elements(), entry, value);
  }

  static inline void SetImpl(Tagged<FixedArrayBase> backing_store,
                             InternalIndex entry, Tagged<Object> value) {
    Cast<FixedDoubleArray>(backing_store)
        ->set(entry.as_int(), Object::NumberValue(value));
  }

  static inline void SetImpl(Tagged<FixedArrayBase> backing_store,
                             InternalIndex entry, Tagged<Object> value,
                             WriteBarrierMode mode) {
    Cast<FixedDoubleArray>(backing_store)
        ->set(entry.as_int(), Object::NumberValue(value));
  }

  static void CopyElementsImpl(Isolate* isolate, Tagged<FixedArrayBase> from,
                               uint32_t from_start, Tagged<FixedArrayBase> to,
                               ElementsKind from_kind, uint32_t to_start,
                               int packed_size, int copy_size) {
    DisallowGarbageCollection no_gc;
    switch (from_kind) {
      case PACKED_SMI_ELEMENTS:
        CopyPackedSmiToDoubleElements(from, from_start, to, to_start,
                                      packed_size, copy_size);
        break;
      case HOLEY_SMI_ELEMENTS:
        CopySmiToDoubleElements(from, from_start, to, to_start, copy_size);
        break;
      case PACKED_DOUBLE_ELEMENTS:
      case HOLEY_DOUBLE_ELEMENTS:
        CopyDoubleToDoubleElements(from, from_start, to, to_start, copy_size);
        break;
      case PACKED_ELEMENTS:
      case PACKED_FROZEN_ELEMENTS:
      case PACKED_SEALED_ELEMENTS:
      case PACKED_NONEXTENSIBLE_ELEMENTS:
      case HOLEY_ELEMENTS:
      case HOLEY_FROZEN_ELEMENTS:
      case HOLEY_SEALED_ELEMENTS:
      case HOLEY_NONEXTENSIBLE_ELEMENTS:
      case SHARED_ARRAY_ELEMENTS:
        CopyObjectToDoubleElements(from, from_start, to, to_start, copy_size);
        break;
      case DICTIONARY_ELEMENTS:
        CopyDictionaryToDoubleElements(isolate, from, from_start, to, to_start,
                                       copy_size);
        break;
      case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
      case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
      case FAST_STRING_WRAPPER_ELEMENTS:
      case SLOW_STRING_WRAPPER_ELEMENTS:
      case WASM_ARRAY_ELEMENTS:
      case NO_ELEMENTS:
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:
        TYPED_ARRAYS(TYPED_ARRAY_CASE)
        RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
        // This function is currently only used for JSArrays with non-zero
        // length.
        UNREACHABLE();
    }
  }

  static Maybe<bool> CollectValuesOrEntriesImpl(
      Isolate* isolate, DirectHandle<JSObject> object,
      DirectHandle<FixedArray> values_or_entries, bool get_entries,
      int* nof_items, PropertyFilter filter) {
    DirectHandle<FixedDoubleArray> elements(
        Cast<FixedDoubleArray>(object->elements()), isolate);
    int count = 0;
    uint32_t length = elements->length();
    for (uint32_t index = 0; index < length; ++index) {
      InternalIndex entry(index);
      if (!Subclass::HasEntryImpl(isolate, *elements, entry)) continue;
      DirectHandle<Object> value = Subclass::GetImpl(isolate, *elements, entry);
      if (get_entries) {
        value = MakeEntryPair(isolate, index, value);
      }
      values_or_entries->set(count++, *value);
    }
    *nof_items = count;
    return Just(true);
  }

  static Maybe<int64_t> IndexOfValueImpl(Isolate* isolate,
                                         DirectHandle<JSObject> receiver,
                                         DirectHandle<Object> search_value,
                                         size_t start_from, size_t length) {
    DCHECK(JSObject::PrototypeHasNoElements(isolate, *receiver));
    DisallowGarbageCollection no_gc;
    Tagged<FixedArrayBase> elements_base = receiver->elements();
    Tagged<Object> value = *search_value;

    length = std::min(static_cast<size_t>(elements_base->length()), length);

    if (start_from >= length) return Just<int64_t>(-1);

    if (!IsNumber(value)) {
      return Just<int64_t>(-1);
    }
    if (IsNaN(value)) {
      return Just<int64_t>(-1);
    }
    double numeric_search_value = Object::NumberValue(value);
    Tagged<FixedDoubleArray> elements =
        Cast<FixedDoubleArray>(receiver->elements());

    static_assert(FixedDoubleArray::kMaxLength <=
                  std::numeric_limits<int>::max());
    for (size_t k = start_from; k < length; ++k) {
      int k_int = static_cast<int>(k);
      if (elements->is_the_hole(k_int)) {
        continue;
      }
      if (elements->get_scalar(k_int) == numeric_search_value) {
        return Just<int64_t>(k);
      }
    }
    return Just<int64_t>(-1);
  }
};

class FastPackedDoubleElementsAccessor
    : public FastDoubleElementsAccessor<
          FastPackedDoubleElementsAccessor,
          ElementsKindTraits<PACKED_DOUBLE_ELEMENTS>> {};

class FastHoleyDoubleElementsAccessor
    : public FastDoubleElementsAccessor<
          FastHoleyDoubleElementsAccessor,
          ElementsKindTraits<HOLEY_DOUBLE_ELEMENTS>> {};

enum IsSharedBuffer : bool { kShared = true, kUnshared = false };

// Super class for all external element arrays.
template <ElementsKind Kind, typename ElementType>
class TypedElementsAccessor
    : public ElementsAccessorBase<TypedElementsAccessor<Kind, ElementType>,
                                  ElementsKindTraits<Kind>> {
 public:
  using BackingStore = typename ElementsKindTraits<Kind>::BackingStore;
  using AccessorClass = TypedElementsAccessor<Kind, ElementType>;

  // Conversions from (other) scalar values.
  static ElementType FromScalar(int value) {
    return static_cast<ElementType>(value);
  }
  static ElementType FromScalar(uint32_t value) {
    return static_cast<ElementType>(value);
  }
  static ElementType FromScalar(double value) {
    return FromScalar(DoubleToInt32(value));
  }
  static ElementType FromScalar(int64_t value) { UNREACHABLE(); }
  static ElementType FromScalar(uint64_t value) { UNREACHABLE(); }

  // Conversions from objects / handles.
  static ElementType FromObject(Tagged<Object> value,
                                bool* lossless = nullptr) {
    if (IsSmi(value)) {
      return FromScalar(Smi::ToInt(value));
    } else if (IsHeapNumber(value)) {
      return FromScalar(Cast<HeapNumber>(value)->value());
    } else {
      // Clamp undefined here as well. All other types have been
      // converted to a number type further up in the call chain.
      DCHECK(IsUndefined(value));
      return FromScalar(Cast<Oddball>(value)->to_number_raw());
    }
  }
  static ElementType FromHandle(DirectHandle<Object> value,
                                bool* lossless = nullptr) {
    return FromObject(*value, lossless);
  }

  // Conversion of scalar value to handlified object.
  static Handle<Object> ToHandle(Isolate* isolate, ElementType value);

  static void SetImpl(Handle<JSObject> holder, InternalIndex entry,
                      Tagged<Object> value) {
    auto typed_array = Cast<JSTypedArray>(holder);
    DCHECK_LE(entry.raw_value(), typed_array->GetLength());
    auto* entry_ptr =
        static_cast<ElementType*>(typed_array->DataPtr()) + entry.raw_value();
    auto is_shared = typed_array->buffer()->is_shared() ? kShared : kUnshared;
    SetImpl(entry_ptr, FromObject(value), is_shared);
  }

  static void SetImpl(ElementType* data_ptr, ElementType value,
                      IsSharedBuffer is_shared) {
    // TODO(ishell, v8:8875): Independent of pointer compression, 8-byte size
    // fields (external pointers, doubles and BigInt data) are not always 8-byte
    // aligned. This is relying on undefined behaviour in C++, since {data_ptr}
    // is not aligned to {alignof(ElementType)}.
    if (!is_shared) {
      base::WriteUnalignedValue(reinterpret_cast<Address>(data_ptr), value);
      return;
    }

    // The JavaScript memory model allows for racy reads and writes to a
    // SharedArrayBuffer's backing store. Using relaxed atomics is not strictly
    // required for JavaScript, but will avoid undefined behaviour in C++ and is
    // unlikely to introduce noticable overhead.
    if (IsAligned(reinterpret_cast<uintptr_t>(data_ptr),
                  alignof(std::atomic<ElementType>))) {
      // Use a single relaxed atomic store.
      static_assert(sizeof(std::atomic<ElementType>) == sizeof(ElementType));
      reinterpret_cast<std::atomic<ElementType>*>(data_ptr)->store(
          value, std::memory_order_relaxed);
      return;
    }

    // Some static CHECKs (are optimized out if succeeding) to ensure that
    // {data_ptr} is at least four byte aligned, and {std::atomic<uint32_t>}
    // has size and alignment of four bytes, such that we can cast the
    // {data_ptr} to it.
    CHECK_LE(kInt32Size, alignof(ElementType));
    CHECK_EQ(kInt32Size, alignof(std::atomic<uint32_t>));
    CHECK_EQ(kInt32Size, sizeof(std::atomic<uint32_t>));
    // And dynamically check that we indeed have at least four byte alignment.
    DCHECK(IsAligned(reinterpret_cast<uintptr_t>(data_ptr), kInt32Size));
    // Store as multiple 32-bit words. Make {kNumWords} >= 1 to avoid compiler
    // warnings for the empty array or memcpy to an empty object.
    constexpr size_t kNumWords =
        std::max(size_t{1}, sizeof(ElementType) / kInt32Size);
    uint32_t words[kNumWords];
    CHECK_EQ(sizeof(words), sizeof(value));
    memcpy(words, &value, sizeof(value));
    for (size_t word = 0; word < kNumWords; ++word) {
      static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t));
      reinterpret_cast<std::atomic<uint32_t>*>(data_ptr)[word].store(
          words[word], std::memory_order_relaxed);
    }
  }

  static Handle<Object> GetInternalImpl(Isolate* isolate,
                                        Handle<JSObject> holder,
                                        InternalIndex entry) {
    auto typed_array = Cast<JSTypedArray>(holder);
    DCHECK_LT(entry.raw_value(), typed_array->GetLength());
    DCHECK(!typed_array->IsDetachedOrOutOfBounds());
    auto* element_ptr =
        static_cast<ElementType*>(typed_array->DataPtr()) + entry.raw_value();
    auto is_shared = typed_array->buffer()->is_shared() ? kShared : kUnshared;
    ElementType elem = GetImpl(element_ptr, is_shared);
    return ToHandle(isolate, elem);
  }

  static Handle<Object> GetImpl(Isolate* isolate,
                                Tagged<FixedArrayBase> backing_store,
                                InternalIndex entry) {
    UNREACHABLE();
  }

  static ElementType GetImpl(ElementType* data_ptr, IsSharedBuffer is_shared) {
    // TODO(ishell, v8:8875): Independent of pointer compression, 8-byte size
    // fields (external pointers, doubles and BigInt data) are not always
    // 8-byte aligned.
    if (!is_shared) {
      return base::ReadUnalignedValue<ElementType>(
          reinterpret_cast<Address>(data_ptr));
    }

    // The JavaScript memory model allows for racy reads and writes to a
    // SharedArrayBuffer's backing store. Using relaxed atomics is not strictly
    // required for JavaScript, but will avoid undefined behaviour in C++ and is
    // unlikely to introduce noticable overhead.
    if (IsAligned(reinterpret_cast<uintptr_t>(data_ptr),
                  alignof(std::atomic<ElementType>))) {
      // Use a single relaxed atomic load.
      static_assert(sizeof(std::atomic<ElementType>) == sizeof(ElementType));
      // Note: acquire semantics are not needed here, but clang seems to merge
      // this atomic load with the non-atomic load above if we use relaxed
      // semantics. This will result in TSan failures.
      return reinterpret_cast<std::atomic<ElementType>*>(data_ptr)->load(
          std::memory_order_acquire);
    }

    // Some static CHECKs (are optimized out if succeeding) to ensure that
    // {data_ptr} is at least four byte aligned, and {std::atomic<uint32_t>}
    // has size and alignment of four bytes, such that we can cast the
    // {data_ptr} to it.
    CHECK_LE(kInt32Size, alignof(ElementType));
    CHECK_EQ(kInt32Size, alignof(std::atomic<uint32_t>));
    CHECK_EQ(kInt32Size, sizeof(std::atomic<uint32_t>));
    // And dynamically check that we indeed have at least four byte alignment.
    DCHECK(IsAligned(reinterpret_cast<uintptr_t>(data_ptr), kInt32Size));
    // Load in multiple 32-bit words. Make {kNumWords} >= 1 to avoid compiler
    // warnings for the empty array or memcpy to an empty object.
    constexpr size_t kNumWords =
        std::max(size_t{1}, sizeof(ElementType) / kInt32Size);
    uint32_t words[kNumWords];
    for (size_t word = 0; word < kNumWords; ++word) {
      static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t));
      words[word] =
          reinterpret_cast<std::atomic<uint32_t>*>(data_ptr)[word].load(
              std::memory_order_relaxed);
    }
    ElementType result;
    CHECK_EQ(sizeof(words), sizeof(result));
    memcpy(&result, words, sizeof(result));
    return result;
  }

  static PropertyDetails GetDetailsImpl(Tagged<JSObject> holder,
                                        InternalIndex entry) {
    return PropertyDetails(PropertyKind::kData, NONE,
                           PropertyCellType::kNoCell);
  }

  static PropertyDetails GetDetailsImpl(Tagged<FixedArrayBase> backing_store,
                                        InternalIndex entry) {
    return PropertyDetails(PropertyKind::kData, NONE,
                           PropertyCellType::kNoCell);
  }

  static bool HasElementImpl(Isolate* isolate, Tagged<JSObject> holder,
                             size_t index, Tagged<FixedArrayBase> backing_store,
                             PropertyFilter filter) {
    return index < AccessorClass::GetCapacityImpl(holder, backing_store);
  }

  static bool HasAccessorsImpl(Tagged<JSObject> holder,
                               Tagged<FixedArrayBase> backing_store) {
    return false;
  }

  static Maybe<bool> SetLengthImpl(Isolate* isolate,
                                   DirectHandle<JSArray> array, uint32_t length,
                                   DirectHandle<FixedArrayBase> backing_store) {
    // External arrays do not support changing their length.
    UNREACHABLE();
  }

  static void DeleteImpl(DirectHandle<JSObject> obj, InternalIndex entry) {
    // Do nothing.
    //
    // TypedArray elements are configurable to explain detaching, but cannot be
    // deleted otherwise.
  }

  static InternalIndex GetEntryForIndexImpl(
      Isolate* isolate, Tagged<JSObject> holder,
      Tagged<FixedArrayBase> backing_store, size_t index,
      PropertyFilter filter) {
    return index < AccessorClass::GetCapacityImpl(holder, backing_store)
               ? InternalIndex(index)
               : InternalIndex::NotFound();
  }

  static size_t GetCapacityImpl(Tagged<JSObject> holder,
                                Tagged<FixedArrayBase> backing_store) {
    Tagged<JSTypedArray> typed_array = Cast<JSTypedArray>(holder);
    return typed_array->GetLength();
  }

  static size_t NumberOfElementsImpl(Tagged<JSObject> receiver,
                                     Tagged<FixedArrayBase> backing_store) {
    return AccessorClass::GetCapacityImpl(receiver, backing_store);
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus AddElementsToKeyAccumulatorImpl(
      Handle<JSObject> receiver, KeyAccumulator* accumulator,
      AddKeyConversion convert) {
    Isolate* isolate = receiver->GetIsolate();
    DirectHandle<FixedArrayBase> elements(receiver->elements(), isolate);
    size_t length = AccessorClass::GetCapacityImpl(*receiver, *elements);
    for (size_t i = 0; i < length; i++) {
      Handle<Object> value =
          AccessorClass::GetInternalImpl(isolate, receiver, InternalIndex(i));
      RETURN_FAILURE_IF_NOT_SUCCESSFUL(accumulator->AddKey(value, convert));
    }
    return ExceptionStatus::kSuccess;
  }

  static Maybe<bool> CollectValuesOrEntriesImpl(
      Isolate* isolate, Handle<JSObject> object,
      DirectHandle<FixedArray> values_or_entries, bool get_entries,
      int* nof_items, PropertyFilter filter) {
    int count = 0;
    if ((filter & ONLY_CONFIGURABLE) == 0) {
      DirectHandle<FixedArrayBase> elements(object->elements(), isolate);
      size_t length = AccessorClass::GetCapacityImpl(*object, *elements);
      for (size_t index = 0; index < length; ++index) {
        DirectHandle<Object> value = AccessorClass::GetInternalImpl(
            isolate, object, InternalIndex(index));
        if (get_entries) {
          value = MakeEntryPair(isolate, index, value);
        }
        values_or_entries->set(count++, *value);
      }
    }
    *nof_items = count;
    return Just(true);
  }

  static bool ToTypedSearchValue(double search_value,
                                 ElementType* typed_search_value) {
    if (!base::IsValueInRangeForNumericType<ElementType>(search_value) &&
        std::isfinite(search_value)) {
      // Return true if value can't be represented in this space.
      return true;
    }
    ElementType typed_value;
    if (IsFloat16TypedArrayElementsKind(Kind)) {
      typed_value = fp16_ieee_from_fp32_value(static_cast<float>(search_value));
      *typed_search_value = typed_value;
      return (static_cast<double>(fp16_ieee_to_fp32_value(typed_value)) !=
              search_value);  // Loss of precision.
    }
    typed_value = static_cast<ElementType>(search_value);
    *typed_search_value = typed_value;
    return static_cast<double>(typed_value) !=
           search_value;  // Loss of precision.
  }

  static MaybeHandle<Object> FillImpl(Handle<JSObject> receiver,
                                      Handle<Object> value, size_t start,
                                      size_t end) {
    Handle<JSTypedArray> typed_array = Cast<JSTypedArray>(receiver);
    DCHECK(!typed_array->IsDetachedOrOutOfBounds());
    DCHECK_LE(start, end);
    DCHECK_LE(end, typed_array->GetLength());
    DisallowGarbageCollection no_gc;
    ElementType scalar = FromHandle(value);
    ElementType* data = static_cast<ElementType*>(typed_array->DataPtr());
    ElementType* first = data + start;
    ElementType* last = data + end;
    if (typed_array->buffer()->is_shared()) {
      // TypedArrays backed by shared buffers need to be filled using atomic
      // operations. Since 8-byte data are not currently always 8-byte aligned,
      // manually fill using SetImpl, which abstracts over alignment and atomic
      // complexities.
      for (; first != last; ++first) {
        AccessorClass::SetImpl(first, scalar, kShared);
      }
    } else if ((scalar == 0 && !(std::is_floating_point_v<ElementType> &&
                                 IsMinusZero(scalar))) ||
               (std::is_integral_v<ElementType> &&
                scalar == static_cast<ElementType>(-1))) {
      // As of 2022-06, this is faster than {std::fill}.
      // We could extend this to any {scalar} that's a pattern of repeating
      // bytes, but patterns other than 0 and -1 are probably rare.
      size_t num_bytes = static_cast<size_t>(reinterpret_cast<int8_t*>(last) -
                                             reinterpret_cast<int8_t*>(first));
      memset(first, static_cast<int8_t>(scalar), num_bytes);
    } else if (COMPRESS_POINTERS_BOOL && alignof(ElementType) > kTaggedSize) {
      // TODO(ishell, v8:8875): See UnalignedSlot<T> for details.
      std::fill(UnalignedSlot<ElementType>(first),
                UnalignedSlot<ElementType>(last), scalar);
    } else {
      std::fill(first, last, scalar);
    }
    return MaybeHandle<Object>(typed_array);
  }

  static Maybe<bool> IncludesValueImpl(Isolate* isolate,
                                       DirectHandle<JSObject> receiver,
                                       Handle<Object> value, size_t start_from,
                                       size_t length) {
    DisallowGarbageCollection no_gc;
    Tagged<JSTypedArray> typed_array = Cast<JSTypedArray>(*receiver);

    if (typed_array->WasDetached()) {
      return Just(IsUndefined(*value, isolate) && length > start_from);
    }

    bool out_of_bounds = false;
    size_t new_length = typed_array->GetLengthOrOutOfBounds(out_of_bounds);
    if (V8_UNLIKELY(out_of_bounds)) {
      return Just(IsUndefined(*value, isolate) && length > start_from);
    }

    if (IsUndefined(*value, isolate) && length > new_length) {
      return Just(true);
    }

    // Prototype has no elements, and not searching for the hole --- limit
    // search to backing store length.
    if (new_length < length) {
      length = new_length;
    }

    ElementType typed_search_value;
    ElementType* data_ptr =
        reinterpret_cast<ElementType*>(typed_array->DataPtr());
    auto is_shared = typed_array->buffer()->is_shared() ? kShared : kUnshared;
    if (Kind == BIGINT64_ELEMENTS || Kind == BIGUINT64_ELEMENTS ||
        Kind == RAB_GSAB_BIGINT64_ELEMENTS ||
        Kind == RAB_GSAB_BIGUINT64_ELEMENTS) {
      if (!IsBigInt(*value)) return Just(false);
      bool lossless;
      typed_search_value = FromHandle(value, &lossless);
      if (!lossless) return Just(false);
    } else {
      if (!IsNumber(*value)) return Just(false);
      double search_value = Object::NumberValue(*value);
      if (!std::isfinite(search_value)) {
        // Integral types cannot represent +Inf or NaN.
        if (!IsFloatTypedArrayElementsKind(Kind)) {
          return Just(false);
        }
        if (std::isnan(search_value)) {
          for (size_t k = start_from; k < length; ++k) {
            if (IsFloat16TypedArrayElementsKind(Kind)) {
              float elem_k = fp16_ieee_to_fp32_value(
                  AccessorClass::GetImpl(data_ptr + k, is_shared));
              if (std::isnan(elem_k)) return Just(true);
            } else {
              double elem_k = static_cast<double>(
                  AccessorClass::GetImpl(data_ptr + k, is_shared));
              if (std::isnan(elem_k)) return Just(true);
            }
          }
          return Just(false);
        }
      }
      if (AccessorClass::ToTypedSearchValue(search_value,
                                            &typed_search_value)) {
        return Just(false);
      }
    }

    for (size_t k = start_from; k < length; ++k) {
      ElementType elem_k = AccessorClass::GetImpl(data_ptr + k, is_shared);
      if (elem_k == typed_search_value) return Just(true);
    }
    return Just(false);
  }

  static Maybe<int64_t> IndexOfValueImpl(Isolate* isolate,
                                         DirectHandle<JSObject> receiver,
                                         Handle<Object> value,
                                         size_t start_from, size_t length) {
    DisallowGarbageCollection no_gc;
    Tagged<JSTypedArray> typed_array = Cast<JSTypedArray>(*receiver);

    // If this is called via Array.prototype.indexOf (not
    // TypedArray.prototype.indexOf), it's possible that the TypedArray is
    // detached / out of bounds here.
    if (V8_UNLIKELY(typed_array->WasDetached())) return Just<int64_t>(-1);
    bool out_of_bounds = false;
    size_t typed_array_length =
        typed_array->GetLengthOrOutOfBounds(out_of_bounds);
    if (V8_UNLIKELY(out_of_bounds)) {
      return Just<int64_t>(-1);
    }

    // Prototype has no elements, and not searching for the hole --- limit
    // search to backing store length.
    if (typed_array_length < length) {
      length = typed_array_length;
    }

    ElementType typed_search_value;

    ElementType* data_ptr =
        reinterpret_cast<ElementType*>(typed_array->DataPtr());
    if (IsBigIntTypedArrayElementsKind(Kind)) {
      if (!IsBigInt(*value)) return Just<int64_t>(-1);
      bool lossless;
      typed_search_value = FromHandle(value, &lossless);
      if (!lossless) return Just<int64_t>(-1);
    } else {
      if (!IsNumber(*value)) return Just<int64_t>(-1);
      double search_value = Object::NumberValue(*value);
      if (!std::isfinite(search_value)) {
        // Integral types cannot represent +Inf or NaN.
        if (!IsFloatTypedArrayElementsKind(Kind)) {
          return Just<int64_t>(-1);
        }
        if (std::isnan(search_value)) {
          return Just<int64_t>(-1);
        }
      }
      if (AccessorClass::ToTypedSearchValue(search_value,
                                            &typed_search_value)) {
        return Just<int64_t>(-1);
      }
    }

    auto is_shared = typed_array->buffer()->is_shared() ? kShared : kUnshared;
    for (size_t k = start_from; k < length; ++k) {
      ElementType elem_k = AccessorClass::GetImpl(data_ptr + k, is_shared);
      if (elem_k == typed_search_value) return Just<int64_t>(k);
    }
    return Just<int64_t>(-1);
  }

  static Maybe<int64_t> LastIndexOfValueImpl(DirectHandle<JSObject> receiver,
                                             Handle<Object> value,
                                             size_t start_from) {
    DisallowGarbageCollection no_gc;
    Tagged<JSTypedArray> typed_array = Cast<JSTypedArray>(*receiver);

    DCHECK(!typed_array->IsDetachedOrOutOfBounds());

    ElementType typed_search_value;

    ElementType* data_ptr =
        reinterpret_cast<ElementType*>(typed_array->DataPtr());
    if (IsBigIntTypedArrayElementsKind(Kind)) {
      if (!IsBigInt(*value)) return Just<int64_t>(-1);
      bool lossless;
      typed_search_value = FromHandle(value, &lossless);
      if (!lossless) return Just<int64_t>(-1);
    } else {
      if (!IsNumber(*value)) return Just<int64_t>(-1);
      double search_value = Object::NumberValue(*value);
      if (!std::isfinite(search_value)) {
        if (!IsFloat16TypedArrayElementsKind(Kind) &&
            std::is_integral<ElementType>::value) {
          // Integral types cannot represent +Inf or NaN.
          return Just<int64_t>(-1);
        } else if (std::isnan(search_value)) {
          // Strict Equality Comparison of NaN is always false.
          return Just<int64_t>(-1);
        }
      }
      if (AccessorClass::ToTypedSearchValue(search_value,
                                            &typed_search_value)) {
        return Just<int64_t>(-1);
      }
    }

    size_t typed_array_length = typed_array->GetLength();
    if (V8_UNLIKELY(start_from >= typed_array_length)) {
      // This can happen if the TypedArray got resized when we did ToInteger
      // on the last parameter of lastIndexOf.
      DCHECK(typed_array->IsVariableLength());
      if (typed_array_length == 0) {
        return Just<int64_t>(-1);
      }
      start_from = typed_array_length - 1;
    }

    size_t k = start_from;
    auto is_shared = typed_array->buffer()->is_shared() ? kShared : kUnshared;
    do {
      ElementType elem_k = AccessorClass::GetImpl(data_ptr + k, is_shared);
      if (elem_k == typed_search_value) return Just<int64_t>(k);
    } while (k-- != 0);
    return Just<int64_t>(-1);
  }

  static void ReverseImpl(Tagged<JSObject> receiver) {
    DisallowGarbageCollection no_gc;
    Tagged<JSTypedArray> typed_array = Cast<JSTypedArray>(receiver);

    DCHECK(!typed_array->IsDetachedOrOutOfBounds());

    size_t len = typed_array->GetLength();
    if (len == 0) return;

    ElementType* data = static_cast<ElementType*>(typed_array->DataPtr());
    if (typed_array->buffer()->is_shared()) {
      // TypedArrays backed by shared buffers need to be reversed using atomic
      // operations. Since 8-byte data are not currently always 8-byte aligned,
      // manually reverse using GetImpl and SetImpl, which abstract over
      // alignment and atomic complexities.
      for (ElementType *first = data, *last = data + len - 1; first < last;
           ++first, --last) {
        ElementType first_value = AccessorClass::GetImpl(first, kShared);
        ElementType last_value = AccessorClass::GetImpl(last, kShared);
        AccessorClass::SetImpl(first, last_value, kShared);
        AccessorClass::SetImpl(last, first_value, kShared);
      }
    } else if (COMPRESS_POINTERS_BOOL && alignof(ElementType) > kTaggedSize) {
      // TODO(ishell, v8:8875): See UnalignedSlot<T> for details.
      std::reverse(UnalignedSlot<ElementType>(data),
                   UnalignedSlot<ElementType>(data + len));
    } else {
      std::reverse(data, data + len);
    }
  }

  static Handle<FixedArray> CreateListFromArrayLikeImpl(Isolate* isolate,
                                                        Handle<JSObject> object,
                                                        uint32_t length) {
    Handle<JSTypedArray> typed_array = Cast<JSTypedArray>(object);
    Handle<FixedArray> result = isolate->factory()->NewFixedArray(length);
    for (uint32_t i = 0; i < length; i++) {
      DirectHandle<Object> value = AccessorClass::GetInternalImpl(
          isolate, typed_array, InternalIndex(i));
      result->set(i, *value);
    }
    return result;
  }

  static void CopyTypedArrayElementsSliceImpl(Tagged<JSTypedArray> source,
                                              Tagged<JSTypedArray> destination,
                                              size_t start, size_t end) {
    DisallowGarbageCollection no_gc;
    DCHECK_EQ(destination->GetElementsKind(), AccessorClass::kind());
    CHECK(!source->IsDetachedOrOutOfBounds());
    CHECK(!destination->IsDetachedOrOutOfBounds());
    DCHECK_LE(start, end);
    DCHECK_LE(end, source->GetLength());
    size_t count = end - start;
    DCHECK_LE(count, destination->GetLength());
    ElementType* dest_data = static_cast<ElementType*>(destination->DataPtr());
    auto is_shared =
        source->buffer()->is_shared() || destination->buffer()->is_shared()
            ? kShared
            : kUnshared;
    switch (source->GetElementsKind()) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype)                             \
  case TYPE##_ELEMENTS: {                                                     \
    ctype* source_data = reinterpret_cast<ctype*>(source->DataPtr()) + start; \
    CopyBetweenBackingStores<TYPE##_ELEMENTS, ctype>(source_data, dest_data,  \
                                                     count, is_shared);       \
    break;                                                                    \
  }
      TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype, NON_RAB_GSAB_TYPE)          \
  case TYPE##_ELEMENTS: {                                                     \
    ctype* source_data = reinterpret_cast<ctype*>(source->DataPtr()) + start; \
    CopyBetweenBackingStores<NON_RAB_GSAB_TYPE##_ELEMENTS, ctype>(            \
        source_data, dest_data, count, is_shared);                            \
    break;                                                                    \
  }
      RAB_GSAB_TYPED_ARRAYS_WITH_NON_RAB_GSAB_ELEMENTS_KIND(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
      default:
        UNREACHABLE();
        break;
    }
  }

  // TODO(v8:11111): Update this once we have external RAB / GSAB array types.
  static bool HasSimpleRepresentation(ExternalArrayType type) {
    return !(type == kExternalFloat32Array || type == kExternalFloat64Array ||
             type == kExternalUint8ClampedArray ||
             type == kExternalFloat16Array);
  }

  template <ElementsKind SourceKind, typename SourceElementType>
  static void CopyBetweenBackingStores(SourceElementType* source_data_ptr,
                                       ElementType* dest_data_ptr,
                                       size_t length,
                                       IsSharedBuffer is_shared) {
    CopyBetweenBackingStoresImpl<Kind, ElementType, SourceKind,
                                 SourceElementType>::Copy(source_data_ptr,
                                                          dest_data_ptr, length,
                                                          is_shared);
  }

  static void CopyElementsFromTypedArray(Tagged<JSTypedArray> source,
                                         Tagged<JSTypedArray> destination,
                                         size_t length, size_t offset) {
    // The source is a typed array, so we know we don't need to do ToNumber
    // side-effects, as the source elements will always be a number.
    DisallowGarbageCollection no_gc;

    CHECK(!source->IsDetachedOrOutOfBounds());
    CHECK(!destination->IsDetachedOrOutOfBounds());

    DCHECK_LE(offset, destination->GetLength());
    DCHECK_LE(length, destination->GetLength() - offset);
    DCHECK_LE(length, source->GetLength());

    ExternalArrayType source_type = source->type();
    ExternalArrayType destination_type = destination->type();

    bool same_type = source_type == destination_type;
    bool same_size = source->element_size() == destination->element_size();
    bool both_are_simple = HasSimpleRepresentation(source_type) &&
                           HasSimpleRepresentation(destination_type);

    uint8_t* source_data = static_cast<uint8_t*>(source->DataPtr());
    uint8_t* dest_data = static_cast<uint8_t*>(destination->DataPtr());
    size_t source_byte_length = source->GetByteLength();
    size_t dest_byte_length = destination->GetByteLength();

    bool source_shared = source->buffer()->is_shared();
    bool destination_shared = destination->buffer()->is_shared();

    // We can simply copy the backing store if the types are the same, or if
    // we are converting e.g. Uint8 <-> Int8, as the binary representation
    // will be the same. This is not the case for floats or clamped Uint8,
    // which have special conversion operations.
    if (same_type || (same_size && both_are_simple)) {
      size_t element_size = source->element_size();
      if (source_shared || destination_shared) {
        base::Relaxed_Memcpy(
            reinterpret_cast<base::Atomic8*>(dest_data + offset * element_size),
            reinterpret_cast<base::Atomic8*>(source_data),
            length * element_size);
      } else {
        std::memmove(dest_data + offset * element_size, source_data,
                     length * element_size);
      }
    } else {
      std::unique_ptr<uint8_t[]> cloned_source_elements;

      // If the typedarrays are overlapped, clone the source.
      if (dest_data + dest_byte_length > source_data &&
          source_data + source_byte_length > dest_data) {
        cloned_source_elements.reset(new uint8_t[source_byte_length]);
        if (source_shared) {
          base::Relaxed_Memcpy(
              reinterpret_cast<base::Atomic8*>(cloned_source_elements.get()),
              reinterpret_cast<base::Atomic8*>(source_data),
              source_byte_length);
        } else {
          std::memcpy(cloned_source_elements.get(), source_data,
                      source_byte_length);
        }
        source_data = cloned_source_elements.get();
      }

      switch (source->GetElementsKind()) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype)                   \
  case TYPE##_ELEMENTS:                                             \
    CopyBetweenBackingStores<TYPE##_ELEMENTS, ctype>(               \
        reinterpret_cast<ctype*>(source_data),                      \
        reinterpret_cast<ElementType*>(dest_data) + offset, length, \
        source_shared || destination_shared ? kShared : kUnshared); \
    break;
        TYPED_ARRAYS(TYPED_ARRAY_CASE)
        RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
        default:
          UNREACHABLE();
          break;
      }
#undef TYPED_ARRAY_CASE
    }
  }

  static bool HoleyPrototypeLookupRequired(Isolate* isolate,
                                           Tagged<Context> context,
                                           Tagged<JSArray> source) {
    DisallowGarbageCollection no_gc;
    DisallowJavascriptExecution no_js(isolate);

#ifdef V8_ENABLE_FORCE_SLOW_PATH
    if (isolate->force_slow_path()) return true;
#endif

    Tagged<Object> source_proto = source->map()->prototype();

    // Null prototypes are OK - we don't need to do prototype chain lookups on
    // them.
    if (IsNull(source_proto, isolate)) return false;
    if (IsJSProxy(source_proto)) return true;
    if (IsJSObject(source_proto) &&
        !context->native_context()->is_initial_array_prototype(
            Cast<JSObject>(source_proto))) {
      return true;
    }

    return !Protectors::IsNoElementsIntact(isolate);
  }

  static bool TryCopyElementsFastNumber(Tagged<Context> context,
                                        Tagged<JSArray> source,
                                        Tagged<JSTypedArray> destination,
                                        size_t length, size_t offset) {
    if (IsBigIntTypedArrayElementsKind(Kind)) return false;
    Isolate* isolate = source->GetIsolate();
    DisallowGarbageCollection no_gc;
    DisallowJavascriptExecution no_js(isolate);

    CHECK(!destination->WasDetached());
    bool out_of_bounds = false;
    CHECK_GE(destination->GetLengthOrOutOfBounds(out_of_bounds), length);
    CHECK(!out_of_bounds);

    size_t current_length;
    DCHECK(IsNumber(source->length()) &&
           TryNumberToSize(source->length(), &current_length) &&
           length <= current_length);
    USE(current_length);

    size_t dest_length = destination->GetLength();
    DCHECK(length + offset <= dest_length);
    USE(dest_length);

    ElementsKind kind = source->GetElementsKind();

    auto destination_shared =
        destination->buffer()->is_shared() ? kShared : kUnshared;

    // When we find the hole, we normally have to look up the element on the
    // prototype chain, which is not handled here and we return false instead.
    // When the array has the original array prototype, and that prototype has
    // not been changed in a way that would affect lookups, we can just convert
    // the hole into undefined.
    if (HoleyPrototypeLookupRequired(isolate, context, source)) return false;

    Tagged<Oddball> undefined = ReadOnlyRoots(isolate).undefined_value();
    ElementType* dest_data =
        reinterpret_cast<ElementType*>(destination->DataPtr()) + offset;

    // Fast-path for packed Smi kind.
    if (kind == PACKED_SMI_ELEMENTS) {
      Tagged<FixedArray> source_store = Cast<FixedArray>(source->elements());

      for (size_t i = 0; i < length; i++) {
        Tagged<Object> elem = source_store->get(static_cast<int>(i));
        ElementType elem_k;
        if (IsFloat16TypedArrayElementsKind(Kind))
          elem_k = fp16_ieee_from_fp32_value(Smi::ToInt(elem));
        else
          elem_k = FromScalar(Smi::ToInt(elem));
        SetImpl(dest_data + i, elem_k, destination_shared);
      }
      return true;
    } else if (kind == HOLEY_SMI_ELEMENTS) {
      Tagged<FixedArray> source_store = Cast<FixedArray>(source->elements());
      for (size_t i = 0; i < length; i++) {
        if (source_store->is_the_hole(isolate, static_cast<int>(i))) {
          SetImpl(dest_data + i, FromObject(undefined), destination_shared);
        } else {
          Tagged<Object> elem = source_store->get(static_cast<int>(i));
          ElementType elem_k;
          if (IsFloat16TypedArrayElementsKind(Kind))
            elem_k = fp16_ieee_from_fp32_value(Smi::ToInt(elem));
          else
            elem_k = FromScalar(Smi::ToInt(elem));
          SetImpl(dest_data + i, elem_k, destination_shared);
        }
      }
      return true;
    } else if (kind == PACKED_DOUBLE_ELEMENTS) {
      // Fast-path for packed double kind. We avoid boxing and then immediately
      // unboxing the double here by using get_scalar.
      Tagged<FixedDoubleArray> source_store =
          Cast<FixedDoubleArray>(source->elements());

      for (size_t i = 0; i < length; i++) {
        // Use the from_double conversion for this specific TypedArray type,
        // rather than relying on C++ to convert elem.
        double elem = source_store->get_scalar(static_cast<int>(i));
        SetImpl(dest_data + i, FromScalar(elem), destination_shared);
      }
      return true;
    } else if (kind == HOLEY_DOUBLE_ELEMENTS) {
      Tagged<FixedDoubleArray> source_store =
          Cast<FixedDoubleArray>(source->elements());
      for (size_t i = 0; i < length; i++) {
        if (source_store->is_the_hole(static_cast<int>(i))) {
          SetImpl(dest_data + i, FromObject(undefined), destination_shared);
        } else {
          double elem = source_store->get_scalar(static_cast<int>(i));
          SetImpl(dest_data + i, FromScalar(elem), destination_shared);
        }
      }
      return true;
    }
    return false;
  }

  // ES#sec-settypedarrayfromarraylike
  static Tagged<Object> CopyElementsHandleSlow(Handle<Object> source,
                                               Handle<JSTypedArray> destination,
                                               size_t length, size_t offset) {
    Isolate* isolate = destination->GetIsolate();
    // 8. Let k be 0.
    // 9. Repeat, while k < srcLength,
    for (size_t i = 0; i < length; i++) {
      Handle<Object> elem;
      // a. Let Pk be ! ToString(𝔽(k)).
      // b. Let value be ? Get(src, Pk).
      LookupIterator it(isolate, source, i);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, elem,
                                         Object::GetProperty(&it));
      // c. Let targetIndex be 𝔽(targetOffset + k).
      // d. Perform ? IntegerIndexedElementSet(target, targetIndex, value).
      //
      // Rest of loop body inlines ES#IntegerIndexedElementSet
      if (IsBigIntTypedArrayElementsKind(Kind)) {
        // 1. If O.[[ContentType]] is BigInt, let numValue be ? ToBigInt(value).
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, elem,
                                           BigInt::FromObject(isolate, elem));
      } else {
        // 2. Otherwise, let numValue be ? ToNumber(value).
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, elem,
                                           Object::ToNumber(isolate, elem));
      }
      // 3. If IsValidIntegerIndex(O, index) is true, then
      //   a. Let offset be O.[[ByteOffset]].
      //   b. Let elementSize be TypedArrayElementSize(O).
      //   c. Let indexedPosition be (ℝ(index) × elementSize) + offset.
      //   d. Let elementType be TypedArrayElementType(O).
      //   e. Perform SetValueInBuffer(O.[[ViewedArrayBuffer]],
      //      indexedPosition, elementType, numValue, true, Unordered).
      bool out_of_bounds = false;
      size_t new_length = destination->GetLengthOrOutOfBounds(out_of_bounds);
      if (V8_UNLIKELY(out_of_bounds || destination->WasDetached() ||
                      new_length <= offset + i)) {
        // Proceed with the loop so that we call get getters for the source even
        // though we don't set the values in the target.
        continue;
      }
      SetImpl(destination, InternalIndex(offset + i), *elem);
      // e. Set k to k + 1.
    }
    // 10. Return unused.
    return *isolate->factory()->undefined_value();
  }

  // This doesn't guarantee that the destination array will be completely
  // filled. The caller must do this by passing a source with equal length, if
  // that is required.
  static Tagged<Object> CopyElementsHandleImpl(Handle<Object> source,
                                               Handle<JSObject> destination,
                                               size_t length, size_t offset) {
    Isolate* isolate = destination->GetIsolate();
    if (length == 0) return *isolate->factory()->undefined_value();

    Handle<JSTypedArray> destination_ta = Cast<JSTypedArray>(destination);

    // All conversions from TypedArrays can be done without allocation.
    if (IsJSTypedArray(*source)) {
      CHECK(!destination_ta->WasDetached());
      bool out_of_bounds = false;
      CHECK_LE(offset + length,
               destination_ta->GetLengthOrOutOfBounds(out_of_bounds));
      CHECK(!out_of_bounds);
      auto source_ta = Cast<JSTypedArray>(source);
      ElementsKind source_kind = source_ta->GetElementsKind();
      bool source_is_bigint = IsBigIntTypedArrayElementsKind(source_kind);
      bool target_is_bigint = IsBigIntTypedArrayElementsKind(Kind);
      // If we have to copy more elements than we have in the source, we need to
      // do special handling and conversion; that happens in the slow case.
      if (source_is_bigint == target_is_bigint && !source_ta->WasDetached() &&
          length + offset <= source_ta->GetLength()) {
        CopyElementsFromTypedArray(*source_ta, *destination_ta, length, offset);
        return *isolate->factory()->undefined_value();
      }
    } else if (IsJSArray(*source)) {
      CHECK(!destination_ta->WasDetached());
      bool out_of_bounds = false;
      CHECK_LE(offset + length,
               destination_ta->GetLengthOrOutOfBounds(out_of_bounds));
      CHECK(!out_of_bounds);
      // Fast cases for packed numbers kinds where we don't need to allocate.
      auto source_js_array = Cast<JSArray>(source);
      size_t current_length;
      DCHECK(IsNumber(source_js_array->length()));
      if (TryNumberToSize(source_js_array->length(), &current_length) &&
          length <= current_length) {
        auto source_array = Cast<JSArray>(source);
        if (TryCopyElementsFastNumber(isolate->context(), *source_array,
                                      *destination_ta, length, offset)) {
          return *isolate->factory()->undefined_value();
        }
      }
    }
    // Final generic case that handles prototype chain lookups, getters, proxies
    // and observable side effects via valueOf, etc. In this case, it's possible
    // that the length getter detached / resized the underlying buffer.
    return CopyElementsHandleSlow(source, destination_ta, length, offset);
  }
};

template <ElementsKind Kind, typename ElementType, ElementsKind SourceKind,
          typename SourceElementType>
struct CopyBetweenBackingStoresImpl {
  static void Copy(SourceElementType* source_data_ptr,
                   ElementType* dest_data_ptr, size_t length,
                   IsSharedBuffer is_shared) {
    for (; length > 0; --length, ++source_data_ptr, ++dest_data_ptr) {
      // We use scalar accessors to avoid boxing/unboxing, so there are no
      // allocations.
      SourceElementType source_elem =
          TypedElementsAccessor<SourceKind, SourceElementType>::GetImpl(
              source_data_ptr, is_shared);
      ElementType dest_elem =
          TypedElementsAccessor<Kind, ElementType>::FromScalar(source_elem);

      TypedElementsAccessor<Kind, ElementType>::SetImpl(dest_data_ptr,
                                                        dest_elem, is_shared);
    }
  }
};

template <ElementsKind Kind, typename ElementType>
struct CopyBetweenBackingStoresImpl<Kind, ElementType, FLOAT16_ELEMENTS,
                                    uint16_t> {
  static void Copy(uint16_t* source_data_ptr, ElementType* dest_data_ptr,
                   size_t length, IsSharedBuffer is_shared) {
    for (; length > 0; --length, ++source_data_ptr, ++dest_data_ptr) {
      // We use scalar accessors to avoid boxing/unboxing, so there are no
      // allocations.
      uint16_t source_elem =
          TypedElementsAccessor<FLOAT16_ELEMENTS, uint16_t>::GetImpl(
              source_data_ptr, is_shared);
      ElementType dest_elem =
          TypedElementsAccessor<Kind, ElementType>::FromScalar(
              fp16_ieee_to_fp32_value(source_elem));

      TypedElementsAccessor<Kind, ElementType>::SetImpl(dest_data_ptr,
                                                        dest_elem, is_shared);
    }
  }
};

template <ElementsKind Kind, typename ElementType>
struct CopyBetweenBackingStoresImpl<Kind, ElementType,
                                    RAB_GSAB_FLOAT16_ELEMENTS, uint16_t> {
  static void Copy(uint16_t* source_data_ptr, ElementType* dest_data_ptr,
                   size_t length, IsSharedBuffer is_shared) {
    for (; length > 0; --length, ++source_data_ptr, ++dest_data_ptr) {
      // We use scalar accessors to avoid boxing/unboxing, so there are no
      // allocations.
      uint16_t source_elem =
          TypedElementsAccessor<RAB_GSAB_FLOAT16_ELEMENTS, uint16_t>::GetImpl(
              source_data_ptr, is_shared);
      ElementType dest_elem =
          TypedElementsAccessor<Kind, ElementType>::FromScalar(
              fp16_ieee_to_fp32_value(source_elem));

      TypedElementsAccessor<Kind, ElementType>::SetImpl(dest_data_ptr,
                                                        dest_elem, is_shared);
    }
  }
};

// static
template <>
Handle<Object> TypedElementsAccessor<INT8_ELEMENTS, int8_t>::ToHandle(
    Isolate* isolate, int8_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<UINT8_ELEMENTS, uint8_t>::ToHandle(
    Isolate* isolate, uint8_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<INT16_ELEMENTS, int16_t>::ToHandle(
    Isolate* isolate, int16_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<UINT16_ELEMENTS, uint16_t>::ToHandle(
    Isolate* isolate, uint16_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<INT32_ELEMENTS, int32_t>::ToHandle(
    Isolate* isolate, int32_t value) {
  return isolate->factory()->NewNumberFromInt(value);
}

// static
template <>
Handle<Object> TypedElementsAccessor<UINT32_ELEMENTS, uint32_t>::ToHandle(
    Isolate* isolate, uint32_t value) {
  return isolate->factory()->NewNumberFromUint(value);
}

// static
template <>
uint16_t TypedElementsAccessor<FLOAT16_ELEMENTS, uint16_t>::FromScalar(
    double value) {
  return DoubleToFloat16(value);
}

// static
template <>
float TypedElementsAccessor<FLOAT32_ELEMENTS, float>::FromScalar(double value) {
  return DoubleToFloat32(value);
}

// static
template <>
uint16_t TypedElementsAccessor<FLOAT16_ELEMENTS, uint16_t>::FromScalar(
    int value) {
  return fp16_ieee_from_fp32_value(value);
}

// static
template <>
uint16_t TypedElementsAccessor<FLOAT16_ELEMENTS, uint16_t>::FromScalar(
    uint32_t value) {
  return fp16_ieee_from_fp32_value(value);
}

// static
template <>
Handle<Object> TypedElementsAccessor<FLOAT16_ELEMENTS, uint16_t>::ToHandle(
    Isolate* isolate, uint16_t value) {
  return isolate->factory()->NewNumber(fp16_ieee_to_fp32_value(value));
}

// static
template <>
Handle<Object> TypedElementsAccessor<FLOAT32_ELEMENTS, float>::ToHandle(
    Isolate* isolate, float value) {
  return isolate->factory()->NewNumber(value);
}

// static
template <>
double TypedElementsAccessor<FLOAT64_ELEMENTS, double>::FromScalar(
    double value) {
  return value;
}

// static
template <>
Handle<Object> TypedElementsAccessor<FLOAT64_ELEMENTS, double>::ToHandle(
    Isolate* isolate, double value) {
  return isolate->factory()->NewNumber(value);
}

// static
template <>
uint8_t TypedElementsAccessor<UINT8_CLAMPED_ELEMENTS, uint8_t>::FromScalar(
    int value) {
  if (value < 0x00) return 0x00;
  if (value > 0xFF) return 0xFF;
  return static_cast<uint8_t>(value);
}

// static
template <>
uint8_t TypedElementsAccessor<UINT8_CLAMPED_ELEMENTS, uint8_t>::FromScalar(
    uint32_t value) {
  // We need this special case for Uint32 -> Uint8Clamped, because the highest
  // Uint32 values will be negative as an int, clamping to 0, rather than 255.
  if (value > 0xFF) return 0xFF;
  return static_cast<uint8_t>(value);
}

// static
template <>
uint8_t TypedElementsAccessor<UINT8_CLAMPED_ELEMENTS, uint8_t>::FromScalar(
    double value) {
  // Handle NaNs and less than zero values which clamp to zero.
  if (!(value > 0)) return 0;
  if (value > 0xFF) return 0xFF;
  return static_cast<uint8_t>(lrint(value));
}

// static
template <>
Handle<Object> TypedElementsAccessor<UINT8_CLAMPED_ELEMENTS, uint8_t>::ToHandle(
    Isolate* isolate, uint8_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
int64_t TypedElementsAccessor<BIGINT64_ELEMENTS, int64_t>::FromScalar(
    int value) {
  UNREACHABLE();
}

// static
template <>
int64_t TypedElementsAccessor<BIGINT64_ELEMENTS, int64_t>::FromScalar(
    uint32_t value) {
  UNREACHABLE();
}

// static
template <>
int64_t TypedElementsAccessor<BIGINT64_ELEMENTS, int64_t>::FromScalar(
    double value) {
  UNREACHABLE();
}

// static
template <>
int64_t TypedElementsAccessor<BIGINT64_ELEMENTS, int64_t>::FromScalar(
    int64_t value) {
  return value;
}

// static
template <>
int64_t TypedElementsAccessor<BIGINT64_ELEMENTS, int64_t>::FromScalar(
    uint64_t value) {
  return static_cast<int64_t>(value);
}

// static
template <>
int64_t TypedElementsAccessor<BIGINT64_ELEMENTS, int64_t>::FromObject(
    Tagged<Object> value, bool* lossless) {
  return Cast<BigInt>(value)->AsInt64(lossless);
}

// static
template <>
Handle<Object> TypedElementsAccessor<BIGINT64_ELEMENTS, int64_t>::ToHandle(
    Isolate* isolate, int64_t value) {
  return BigInt::FromInt64(isolate, value);
}

// static
template <>
uint64_t TypedElementsAccessor<BIGUINT64_ELEMENTS, uint64_t>::FromScalar(
    int value) {
  UNREACHABLE();
}

// static
template <>
uint64_t TypedElementsAccessor<BIGUINT64_ELEMENTS, uint64_t>::FromScalar(
    uint32_t value) {
  UNREACHABLE();
}

// static
template <>
uint64_t TypedElementsAccessor<BIGUINT64_ELEMENTS, uint64_t>::FromScalar(
    double value) {
  UNREACHABLE();
}

// static
template <>
uint64_t TypedElementsAccessor<BIGUINT64_ELEMENTS, uint64_t>::FromScalar(
    int64_t value) {
  return static_cast<uint64_t>(value);
}

// static
template <>
uint64_t TypedElementsAccessor<BIGUINT64_ELEMENTS, uint64_t>::FromScalar(
    uint64_t value) {
  return value;
}

// static
template <>
uint64_t TypedElementsAccessor<BIGUINT64_ELEMENTS, uint64_t>::FromObject(
    Tagged<Object> value, bool* lossless) {
  return Cast<BigInt>(value)->AsUint64(lossless);
}

// static
template <>
Handle<Object> TypedElementsAccessor<BIGUINT64_ELEMENTS, uint64_t>::ToHandle(
    Isolate* isolate, uint64_t value) {
  return BigInt::FromUint64(isolate, value);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_INT8_ELEMENTS, int8_t>::ToHandle(
    Isolate* isolate, int8_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_UINT8_ELEMENTS,
                                     uint8_t>::ToHandle(Isolate* isolate,
                                                        uint8_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_INT16_ELEMENTS,
                                     int16_t>::ToHandle(Isolate* isolate,
                                                        int16_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_UINT16_ELEMENTS,
                                     uint16_t>::ToHandle(Isolate* isolate,
                                                         uint16_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_INT32_ELEMENTS,
                                     int32_t>::ToHandle(Isolate* isolate,
                                                        int32_t value) {
  return isolate->factory()->NewNumberFromInt(value);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_UINT32_ELEMENTS,
                                     uint32_t>::ToHandle(Isolate* isolate,
                                                         uint32_t value) {
  return isolate->factory()->NewNumberFromUint(value);
}

// static
template <>
uint16_t TypedElementsAccessor<RAB_GSAB_FLOAT16_ELEMENTS, uint16_t>::FromScalar(
    double value) {
  return DoubleToFloat16(value);
}

// static
template <>
uint16_t TypedElementsAccessor<RAB_GSAB_FLOAT16_ELEMENTS, uint16_t>::FromScalar(
    int value) {
  return fp16_ieee_from_fp32_value(value);
}

// static
template <>
uint16_t TypedElementsAccessor<RAB_GSAB_FLOAT16_ELEMENTS, uint16_t>::FromScalar(
    uint32_t value) {
  return fp16_ieee_from_fp32_value(value);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_FLOAT16_ELEMENTS,
                                     uint16_t>::ToHandle(Isolate* isolate,
                                                         uint16_t value) {
  return isolate->factory()->NewHeapNumber(fp16_ieee_to_fp32_value(value));
}

// static
template <>
float TypedElementsAccessor<RAB_GSAB_FLOAT32_ELEMENTS, float>::FromScalar(
    double value) {
  return DoubleToFloat32(value);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_FLOAT32_ELEMENTS,
                                     float>::ToHandle(Isolate* isolate,
                                                      float value) {
  return isolate->factory()->NewNumber(value);
}

// static
template <>
double TypedElementsAccessor<RAB_GSAB_FLOAT64_ELEMENTS, double>::FromScalar(
    double value) {
  return value;
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_FLOAT64_ELEMENTS,
                                     double>::ToHandle(Isolate* isolate,
                                                       double value) {
  return isolate->factory()->NewNumber(value);
}

// static
template <>
uint8_t TypedElementsAccessor<RAB_GSAB_UINT8_CLAMPED_ELEMENTS,
                              uint8_t>::FromScalar(int value) {
  if (value < 0x00) return 0x00;
  if (value > 0xFF) return 0xFF;
  return static_cast<uint8_t>(value);
}

// static
template <>
uint8_t TypedElementsAccessor<RAB_GSAB_UINT8_CLAMPED_ELEMENTS,
                              uint8_t>::FromScalar(uint32_t value) {
  // We need this special case for Uint32 -> Uint8Clamped, because the highest
  // Uint32 values will be negative as an int, clamping to 0, rather than 255.
  if (value > 0xFF) return 0xFF;
  return static_cast<uint8_t>(value);
}

// static
template <>
uint8_t TypedElementsAccessor<RAB_GSAB_UINT8_CLAMPED_ELEMENTS,
                              uint8_t>::FromScalar(double value) {
  // Handle NaNs and less than zero values which clamp to zero.
  if (!(value > 0)) return 0;
  if (value > 0xFF) return 0xFF;
  return static_cast<uint8_t>(lrint(value));
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_UINT8_CLAMPED_ELEMENTS,
                                     uint8_t>::ToHandle(Isolate* isolate,
                                                        uint8_t value) {
  return handle(Smi::FromInt(value), isolate);
}

// static
template <>
int64_t TypedElementsAccessor<RAB_GSAB_BIGINT64_ELEMENTS, int64_t>::FromScalar(
    int value) {
  UNREACHABLE();
}

// static
template <>
int64_t TypedElementsAccessor<RAB_GSAB_BIGINT64_ELEMENTS, int64_t>::FromScalar(
    uint32_t value) {
  UNREACHABLE();
}

// static
template <>
int64_t TypedElementsAccessor<RAB_GSAB_BIGINT64_ELEMENTS, int64_t>::FromScalar(
    double value) {
  UNREACHABLE();
}

// static
template <>
int64_t TypedElementsAccessor<RAB_GSAB_BIGINT64_ELEMENTS, int64_t>::FromScalar(
    int64_t value) {
  return value;
}

// static
template <>
int64_t TypedElementsAccessor<RAB_GSAB_BIGINT64_ELEMENTS, int64_t>::FromScalar(
    uint64_t value) {
  return static_cast<int64_t>(value);
}

// static
template <>
int64_t TypedElementsAccessor<RAB_GSAB_BIGINT64_ELEMENTS, int64_t>::FromObject(
    Tagged<Object> value, bool* lossless) {
  return Cast<BigInt>(value)->AsInt64(lossless);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_BIGINT64_ELEMENTS,
                                     int64_t>::ToHandle(Isolate* isolate,
                                                        int64_t value) {
  return BigInt::FromInt64(isolate, value);
}

// static
template <>
uint64_t TypedElementsAccessor<RAB_GSAB_BIGUINT64_ELEMENTS,
                               uint64_t>::FromScalar(int value) {
  UNREACHABLE();
}

// static
template <>
uint64_t TypedElementsAccessor<RAB_GSAB_BIGUINT64_ELEMENTS,
                               uint64_t>::FromScalar(uint32_t value) {
  UNREACHABLE();
}

// static
template <>
uint64_t TypedElementsAccessor<RAB_GSAB_BIGUINT64_ELEMENTS,
                               uint64_t>::FromScalar(double value) {
  UNREACHABLE();
}

// static
template <>
uint64_t TypedElementsAccessor<RAB_GSAB_BIGUINT64_ELEMENTS,
                               uint64_t>::FromScalar(int64_t value) {
  return static_cast<uint64_t>(value);
}

// static
template <>
uint64_t TypedElementsAccessor<RAB_GSAB_BIGUINT64_ELEMENTS,
                               uint64_t>::FromScalar(uint64_t value) {
  return value;
}

// static
template <>
uint64_t TypedElementsAccessor<RAB_GSAB_BIGUINT64_ELEMENTS,
                               uint64_t>::FromObject(Tagged<Object> value,
                                                     bool* lossless) {
  return Cast<BigInt>(value)->AsUint64(lossless);
}

// static
template <>
Handle<Object> TypedElementsAccessor<RAB_GSAB_BIGUINT64_ELEMENTS,
                                     uint64_t>::ToHandle(Isolate* isolate,
                                                         uint64_t value) {
  return BigInt::FromUint64(isolate, value);
}

#define FIXED_ELEMENTS_ACCESSOR(Type, type, TYPE, ctype) \
  using Type##ElementsAccessor = TypedElementsAccessor<TYPE##_ELEMENTS, ctype>;
TYPED_ARRAYS(FIXED_ELEMENTS_ACCESSOR)
RAB_GSAB_TYPED_ARRAYS(FIXED_ELEMENTS_ACCESSOR)
#undef FIXED_ELEMENTS_ACCESSOR

template <typename Subclass, typename ArgumentsAccessor, typename KindTraits>
class SloppyArgumentsElementsAccessor
    : public ElementsAccessorBase<Subclass, KindTraits> {
 public:
  static void ConvertArgumentsStoreResult(
      DirectHandle<SloppyArgumentsElements> elements,
      DirectHandle<Object> result) {
    UNREACHABLE();
  }

  static Handle<Object> GetImpl(Isolate* isolate,
                                Tagged<FixedArrayBase> parameters,
                                InternalIndex entry) {
    Handle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(parameters), isolate);
    uint32_t length = elements->length();
    if (entry.as_uint32() < length) {
      // Read context mapped entry.
      DisallowGarbageCollection no_gc;
      Tagged<Object> probe =
          elements->mapped_entries(entry.as_uint32(), kRelaxedLoad);
      DCHECK(!IsTheHole(probe, isolate));
      Tagged<Context> context = elements->context();
      int context_entry = Smi::ToInt(probe);
      DCHECK(!IsTheHole(context->get(context_entry), isolate));
      return handle(context->get(context_entry), isolate);
    } else {
      // Entry is not context mapped, defer to the arguments.
      Handle<Object> result = ArgumentsAccessor::GetImpl(
          isolate, elements->arguments(), entry.adjust_down(length));
      return Subclass::ConvertArgumentsStoreResult(isolate, elements, result);
    }
  }

  static Maybe<bool> TransitionElementsKindImpl(DirectHandle<JSObject> object,
                                                DirectHandle<Map> map) {
    UNREACHABLE();
  }

  static Maybe<bool> GrowCapacityAndConvertImpl(DirectHandle<JSObject> object,
                                                uint32_t capacity) {
    UNREACHABLE();
  }

  static inline void SetImpl(DirectHandle<JSObject> holder, InternalIndex entry,
                             Tagged<Object> value) {
    SetImpl(holder->elements(), entry, value);
  }

  static inline void SetImpl(Tagged<FixedArrayBase> store, InternalIndex entry,
                             Tagged<Object> value) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(store);
    uint32_t length = elements->length();
    if (entry.as_uint32() < length) {
      // Store context mapped entry.
      DisallowGarbageCollection no_gc;
      Tagged<Object> probe =
          elements->mapped_entries(entry.as_uint32(), kRelaxedLoad);
      DCHECK(!IsTheHole(probe));
      Tagged<Context> context = Cast<Context>(elements->context());
      int context_entry = Smi::ToInt(probe);
      DCHECK(!IsTheHole(context->get(context_entry)));
      context->set(context_entry, value);
    } else {
      //  Entry is not context mapped defer to arguments.
      Tagged<FixedArray> arguments = elements->arguments();
      Tagged<Object> current =
          ArgumentsAccessor::GetRaw(arguments, entry.adjust_down(length));
      if (IsAliasedArgumentsEntry(current)) {
        Tagged<AliasedArgumentsEntry> alias =
            Cast<AliasedArgumentsEntry>(current);
        Tagged<Context> context = Cast<Context>(elements->context());
        int context_entry = alias->aliased_context_slot();
        DCHECK(!IsTheHole(context->get(context_entry)));
        context->set(context_entry, value);
      } else {
        ArgumentsAccessor::SetImpl(arguments, entry.adjust_down(length), value);
      }
    }
  }

  static Maybe<bool> SetLengthImpl(Isolate* isolate,
                                   DirectHandle<JSArray> array, uint32_t length,
                                   DirectHandle<FixedArrayBase> parameter_map) {
    // Sloppy arguments objects are not arrays.
    UNREACHABLE();
  }

  static uint32_t GetCapacityImpl(Tagged<JSObject> holder,
                                  Tagged<FixedArrayBase> store) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(store);
    Tagged<FixedArray> arguments = elements->arguments();
    return elements->length() +
           ArgumentsAccessor::GetCapacityImpl(holder, arguments);
  }

  static uint32_t GetMaxNumberOfEntries(Tagged<JSObject> holder,
                                        Tagged<FixedArrayBase> backing_store) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(backing_store);
    Tagged<FixedArrayBase> arguments = elements->arguments();
    size_t max_entries =
        ArgumentsAccessor::GetMaxNumberOfEntries(holder, arguments);
    DCHECK_LE(max_entries, std::numeric_limits<uint32_t>::max());
    return elements->length() + static_cast<uint32_t>(max_entries);
  }

  static uint32_t NumberOfElementsImpl(Tagged<JSObject> receiver,
                                       Tagged<FixedArrayBase> backing_store) {
    Isolate* isolate = receiver->GetIsolate();
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(backing_store);
    Tagged<FixedArrayBase> arguments = elements->arguments();
    uint32_t nof_elements = 0;
    uint32_t length = elements->length();
    for (uint32_t index = 0; index < length; index++) {
      if (HasParameterMapArg(isolate, elements, index)) nof_elements++;
    }
    return nof_elements +
           ArgumentsAccessor::NumberOfElementsImpl(receiver, arguments);
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus AddElementsToKeyAccumulatorImpl(
      DirectHandle<JSObject> receiver, KeyAccumulator* accumulator,
      AddKeyConversion convert) {
    Isolate* isolate = accumulator->isolate();
    DirectHandle<FixedArrayBase> elements(receiver->elements(), isolate);
    uint32_t length = GetCapacityImpl(*receiver, *elements);
    for (uint32_t index = 0; index < length; index++) {
      InternalIndex entry(index);
      if (!HasEntryImpl(isolate, *elements, entry)) continue;
      Handle<Object> value = GetImpl(isolate, *elements, entry);
      RETURN_FAILURE_IF_NOT_SUCCESSFUL(accumulator->AddKey(value, convert));
    }
    return ExceptionStatus::kSuccess;
  }

  static bool HasEntryImpl(Isolate* isolate, Tagged<FixedArrayBase> parameters,
                           InternalIndex entry) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(parameters);
    uint32_t length = elements->length();
    if (entry.raw_value() < length) {
      return HasParameterMapArg(isolate, elements, entry.raw_value());
    }
    Tagged<FixedArrayBase> arguments = elements->arguments();
    return ArgumentsAccessor::HasEntryImpl(isolate, arguments,
                                           entry.adjust_down(length));
  }

  static bool HasAccessorsImpl(Tagged<JSObject> holder,
                               Tagged<FixedArrayBase> backing_store) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(backing_store);
    Tagged<FixedArray> arguments = elements->arguments();
    return ArgumentsAccessor::HasAccessorsImpl(holder, arguments);
  }

  static InternalIndex GetEntryForIndexImpl(Isolate* isolate,
                                            Tagged<JSObject> holder,
                                            Tagged<FixedArrayBase> parameters,
                                            size_t index,
                                            PropertyFilter filter) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(parameters);
    if (HasParameterMapArg(isolate, elements, index)) {
      return InternalIndex(index);
    }
    Tagged<FixedArray> arguments = elements->arguments();
    InternalIndex entry = ArgumentsAccessor::GetEntryForIndexImpl(
        isolate, holder, arguments, index, filter);
    if (entry.is_not_found()) return entry;
    // Arguments entries could overlap with the dictionary entries, hence offset
    // them by the number of context mapped entries.
    return entry.adjust_up(elements->length());
  }

  static PropertyDetails GetDetailsImpl(Tagged<JSObject> holder,
                                        InternalIndex entry) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(holder->elements());
    uint32_t length = elements->length();
    if (entry.as_uint32() < length) {
      return PropertyDetails(PropertyKind::kData, NONE,
                             PropertyCellType::kNoCell);
    }
    Tagged<FixedArray> arguments = elements->arguments();
    return ArgumentsAccessor::GetDetailsImpl(arguments,
                                             entry.adjust_down(length));
  }

  static bool HasParameterMapArg(Isolate* isolate,
                                 Tagged<SloppyArgumentsElements> elements,
                                 size_t index) {
    uint32_t length = elements->length();
    if (index >= length) return false;
    return !IsTheHole(
        elements->mapped_entries(static_cast<uint32_t>(index), kRelaxedLoad),
        isolate);
  }

  static void DeleteImpl(Handle<JSObject> obj, InternalIndex entry) {
    Handle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(obj->elements()), obj->GetIsolate());
    uint32_t length = elements->length();
    InternalIndex delete_or_entry = entry;
    if (entry.as_uint32() < length) {
      delete_or_entry = InternalIndex::NotFound();
    }
    Subclass::SloppyDeleteImpl(obj, elements, delete_or_entry);
    // SloppyDeleteImpl allocates a new dictionary elements store. For making
    // heap verification happy we postpone clearing out the mapped entry.
    if (entry.as_uint32() < length) {
      elements->set_mapped_entries(entry.as_uint32(),
                                   obj->GetReadOnlyRoots().the_hole_value());
    }
  }

  static void SloppyDeleteImpl(DirectHandle<JSObject> obj,
                               DirectHandle<SloppyArgumentsElements> elements,
                               InternalIndex entry) {
    // Implemented in subclasses.
    UNREACHABLE();
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus CollectElementIndicesImpl(
      Handle<JSObject> object, Handle<FixedArrayBase> backing_store,
      KeyAccumulator* keys) {
    Isolate* isolate = keys->isolate();
    uint32_t nof_indices = 0;
    Handle<FixedArray> indices = isolate->factory()->NewFixedArray(
        GetCapacityImpl(*object, *backing_store));
    DirectCollectElementIndicesImpl(isolate, object, backing_store,
                                    GetKeysConversion::kKeepNumbers,
                                    ENUMERABLE_STRINGS, indices, &nof_indices);
    SortIndices(isolate, indices, nof_indices);
    for (uint32_t i = 0; i < nof_indices; i++) {
      RETURN_FAILURE_IF_NOT_SUCCESSFUL(keys->AddKey(indices->get(i)));
    }
    return ExceptionStatus::kSuccess;
  }

  static Handle<FixedArray> DirectCollectElementIndicesImpl(
      Isolate* isolate, Handle<JSObject> object,
      Handle<FixedArrayBase> backing_store, GetKeysConversion convert,
      PropertyFilter filter, Handle<FixedArray> list, uint32_t* nof_indices,
      uint32_t insertion_index = 0) {
    auto elements = Cast<SloppyArgumentsElements>(backing_store);
    uint32_t length = elements->length();

    for (uint32_t i = 0; i < length; ++i) {
      if (IsTheHole(elements->mapped_entries(i, kRelaxedLoad), isolate))
        continue;
      if (convert == GetKeysConversion::kConvertToString) {
        DirectHandle<String> index_string =
            isolate->factory()->Uint32ToString(i);
        list->set(insertion_index, *index_string);
      } else {
        list->set(insertion_index, Smi::FromInt(i));
      }
      insertion_index++;
    }

    Handle<FixedArray> store(elements->arguments(), isolate);
    return ArgumentsAccessor::DirectCollectElementIndicesImpl(
        isolate, object, store, convert, filter, list, nof_indices,
        insertion_index);
  }

  static Maybe<bool> IncludesValueImpl(Isolate* isolate,
                                       Handle<JSObject> object,
                                       DirectHandle<Object> value,
                                       size_t start_from, size_t length) {
    DCHECK(JSObject::PrototypeHasNoElements(isolate, *object));
    DirectHandle<Map> original_map(object->map(), isolate);
    DirectHandle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(object->elements()), isolate);
    bool search_for_hole = IsUndefined(*value, isolate);

    for (size_t k = start_from; k < length; ++k) {
      DCHECK_EQ(object->map(), *original_map);
      InternalIndex entry =
          GetEntryForIndexImpl(isolate, *object, *elements, k, ALL_PROPERTIES);
      if (entry.is_not_found()) {
        if (search_for_hole) return Just(true);
        continue;
      }

      DirectHandle<Object> element_k =
          Subclass::GetImpl(isolate, *elements, entry);

      if (IsAccessorPair(*element_k)) {
        LookupIterator it(isolate, object, k, LookupIterator::OWN);
        DCHECK(it.IsFound());
        DCHECK_EQ(it.state(), LookupIterator::ACCESSOR);
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, element_k,
                                         Object::GetPropertyWithAccessor(&it),
                                         Nothing<bool>());

        if (Object::SameValueZero(*value, *element_k)) return Just(true);

        if (object->map() != *original_map) {
          // Some mutation occurred in accessor. Abort "fast" path
          return IncludesValueSlowPath(isolate, object, value, k + 1, length);
        }
      } else if (Object::SameValueZero(*value, *element_k)) {
        return Just(true);
      }
    }
    return Just(false);
  }

  static Maybe<int64_t> IndexOfValueImpl(Isolate* isolate,
                                         Handle<JSObject> object,
                                         DirectHandle<Object> value,
                                         size_t start_from, size_t length) {
    DCHECK(JSObject::PrototypeHasNoElements(isolate, *object));
    DirectHandle<Map> original_map(object->map(), isolate);
    DirectHandle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(object->elements()), isolate);

    for (size_t k = start_from; k < length; ++k) {
      DCHECK_EQ(object->map(), *original_map);
      InternalIndex entry =
          GetEntryForIndexImpl(isolate, *object, *elements, k, ALL_PROPERTIES);
      if (entry.is_not_found()) {
        continue;
      }

      DirectHandle<Object> element_k =
          Subclass::GetImpl(isolate, *elements, entry);

      if (IsAccessorPair(*element_k)) {
        LookupIterator it(isolate, object, k, LookupIterator::OWN);
        DCHECK(it.IsFound());
        DCHECK_EQ(it.state(), LookupIterator::ACCESSOR);
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, element_k,
                                         Object::GetPropertyWithAccessor(&it),
                                         Nothing<int64_t>());

        if (Object::StrictEquals(*value, *element_k)) {
          return Just<int64_t>(k);
        }

        if (object->map() != *original_map) {
          // Some mutation occurred in accessor. Abort "fast" path.
          return IndexOfValueSlowPath(isolate, object, value, k + 1, length);
        }
      } else if (Object::StrictEquals(*value, *element_k)) {
        return Just<int64_t>(k);
      }
    }
    return Just<int64_t>(-1);
  }
};

class SlowSloppyArgumentsElementsAccessor
    : public SloppyArgumentsElementsAccessor<
          SlowSloppyArgumentsElementsAccessor, DictionaryElementsAccessor,
          ElementsKindTraits<SLOW_SLOPPY_ARGUMENTS_ELEMENTS>> {
 public:
  static Handle<Object> ConvertArgumentsStoreResult(
      Isolate* isolate, DirectHandle<SloppyArgumentsElements> elements,
      Handle<Object> result) {
    // Elements of the arguments object in slow mode might be slow aliases.
    if (IsAliasedArgumentsEntry(*result)) {
      DisallowGarbageCollection no_gc;
      Tagged<AliasedArgumentsEntry> alias =
          Cast<AliasedArgumentsEntry>(*result);
      Tagged<Context> context = elements->context();
      int context_entry = alias->aliased_context_slot();
      DCHECK(!IsTheHole(context->get(context_entry), isolate));
      return handle(context->get(context_entry), isolate);
    }
    return result;
  }
  static void SloppyDeleteImpl(DirectHandle<JSObject> obj,
                               DirectHandle<SloppyArgumentsElements> elements,
                               InternalIndex entry) {
    // No need to delete a context mapped entry from the arguments elements.
    if (entry.is_not_found()) return;
    Isolate* isolate = obj->GetIsolate();
    Handle<NumberDictionary> dict(Cast<NumberDictionary>(elements->arguments()),
                                  isolate);
    uint32_t length = elements->length();
    dict =
        NumberDictionary::DeleteEntry(isolate, dict, entry.adjust_down(length));
    elements->set_arguments(*dict);
  }
  static Maybe<bool> AddImpl(Handle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    Isolate* isolate = object->GetIsolate();
    DirectHandle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(object->elements()), isolate);
    Handle<FixedArrayBase> old_arguments(elements->arguments(), isolate);
    Handle<NumberDictionary> dictionary =
        IsNumberDictionary(*old_arguments)
            ? Cast<NumberDictionary>(old_arguments)
            : JSObject::NormalizeElements(object);
    PropertyDetails details(PropertyKind::kData, attributes,
                            PropertyCellType::kNoCell);
    DirectHandle<NumberDictionary> new_dictionary =
        NumberDictionary::Add(isolate, dictionary, index, value, details);
    if (attributes != NONE) object->RequireSlowElements(*new_dictionary);
    if (*dictionary != *new_dictionary) {
      elements->set_arguments(*new_dictionary);
    }
    return Just(true);
  }

  static void ReconfigureImpl(DirectHandle<JSObject> object,
                              Handle<FixedArrayBase> store, InternalIndex entry,
                              DirectHandle<Object> value,
                              PropertyAttributes attributes) {
    Isolate* isolate = object->GetIsolate();
    auto elements = Cast<SloppyArgumentsElements>(store);
    uint32_t length = elements->length();
    if (entry.as_uint32() < length) {
      Tagged<Object> probe =
          elements->mapped_entries(entry.as_uint32(), kRelaxedLoad);
      DCHECK(!IsTheHole(probe, isolate));
      Tagged<Context> context = elements->context();
      int context_entry = Smi::ToInt(probe);
      DCHECK(!IsTheHole(context->get(context_entry), isolate));
      context->set(context_entry, *value);

      // Redefining attributes of an aliased element destroys fast aliasing.
      elements->set_mapped_entries(entry.as_uint32(),
                                   ReadOnlyRoots(isolate).the_hole_value());
      // For elements that are still writable we re-establish slow aliasing.
      if ((attributes & READ_ONLY) == 0) {
        value = isolate->factory()->NewAliasedArgumentsEntry(context_entry);
      }

      PropertyDetails details(PropertyKind::kData, attributes,
                              PropertyCellType::kNoCell);
      Handle<NumberDictionary> arguments(
          Cast<NumberDictionary>(elements->arguments()), isolate);
      arguments = NumberDictionary::Add(isolate, arguments, entry.as_uint32(),
                                        value, details);
      // If the attributes were NONE, we would have called set rather than
      // reconfigure.
      DCHECK_NE(NONE, attributes);
      object->RequireSlowElements(*arguments);
      elements->set_arguments(*arguments);
    } else {
      DirectHandle<FixedArrayBase> arguments(elements->arguments(), isolate);
      DictionaryElementsAccessor::ReconfigureImpl(
          object, arguments, entry.adjust_down(length), value, attributes);
    }
  }
};

class FastSloppyArgumentsElementsAccessor
    : public SloppyArgumentsElementsAccessor<
          FastSloppyArgumentsElementsAccessor, FastHoleyObjectElementsAccessor,
          ElementsKindTraits<FAST_SLOPPY_ARGUMENTS_ELEMENTS>> {
 public:
  static Handle<Object> ConvertArgumentsStoreResult(
      Isolate* isolate, DirectHandle<SloppyArgumentsElements> parameter_map,
      Handle<Object> result) {
    DCHECK(!IsAliasedArgumentsEntry(*result));
    return result;
  }

  static Handle<FixedArray> GetArguments(Isolate* isolate,
                                         Tagged<FixedArrayBase> store) {
    Tagged<SloppyArgumentsElements> elements =
        Cast<SloppyArgumentsElements>(store);
    return Handle<FixedArray>(elements->arguments(), isolate);
  }

  static Handle<NumberDictionary> NormalizeImpl(
      Handle<JSObject> object, DirectHandle<FixedArrayBase> elements) {
    DirectHandle<FixedArray> arguments =
        GetArguments(object->GetIsolate(), *elements);
    return FastHoleyObjectElementsAccessor::NormalizeImpl(object, arguments);
  }

  static Handle<NumberDictionary> NormalizeArgumentsElements(
      Handle<JSObject> object, DirectHandle<SloppyArgumentsElements> elements,
      InternalIndex* entry) {
    Handle<NumberDictionary> dictionary = JSObject::NormalizeElements(object);
    elements->set_arguments(*dictionary);
    // kMaxUInt32 indicates that a context mapped element got deleted. In this
    // case we only normalize the elements (aka. migrate to SLOW_SLOPPY).
    if (entry->is_not_found()) return dictionary;
    uint32_t length = elements->length();
    if (entry->as_uint32() >= length) {
      *entry =
          dictionary
              ->FindEntry(object->GetIsolate(), entry->as_uint32() - length)
              .adjust_up(length);
    }
    return dictionary;
  }

  static void SloppyDeleteImpl(Handle<JSObject> obj,
                               DirectHandle<SloppyArgumentsElements> elements,
                               InternalIndex entry) {
    // Always normalize element on deleting an entry.
    NormalizeArgumentsElements(obj, elements, &entry);
    SlowSloppyArgumentsElementsAccessor::SloppyDeleteImpl(obj, elements, entry);
  }

  static Maybe<bool> AddImpl(Handle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    DCHECK_EQ(NONE, attributes);
    Isolate* isolate = object->GetIsolate();
    DirectHandle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(object->elements()), isolate);
    DirectHandle<FixedArray> old_arguments(elements->arguments(), isolate);
    if (IsNumberDictionary(*old_arguments) ||
        static_cast<uint32_t>(old_arguments->length()) < new_capacity) {
      MAYBE_RETURN(GrowCapacityAndConvertImpl(object, new_capacity),
                   Nothing<bool>());
    }
    Tagged<FixedArray> arguments = elements->arguments();
    // For fast holey objects, the entry equals the index. The code above made
    // sure that there's enough space to store the value. We cannot convert
    // index to entry explicitly since the slot still contains the hole, so the
    // current EntryForIndex would indicate that it is "absent" by returning
    // kMaxUInt32.
    FastHoleyObjectElementsAccessor::SetImpl(arguments, InternalIndex(index),
                                             *value);
    return Just(true);
  }

  static void ReconfigureImpl(Handle<JSObject> object,
                              Handle<FixedArrayBase> store, InternalIndex entry,
                              DirectHandle<Object> value,
                              PropertyAttributes attributes) {
    DCHECK_EQ(object->elements(), *store);
    DirectHandle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(*store), object->GetIsolate());
    NormalizeArgumentsElements(object, elements, &entry);
    SlowSloppyArgumentsElementsAccessor::ReconfigureImpl(object, store, entry,
                                                         value, attributes);
  }

  static void CopyElementsImpl(Isolate* isolate, Tagged<FixedArrayBase> from,
                               uint32_t from_start, Tagged<FixedArrayBase> to,
                               ElementsKind from_kind, uint32_t to_start,
                               int packed_size, int copy_size) {
    DCHECK(!IsNumberDictionary(to));
    if (from_kind == SLOW_SLOPPY_ARGUMENTS_ELEMENTS) {
      CopyDictionaryToObjectElements(isolate, from, from_start, to,
                                     HOLEY_ELEMENTS, to_start, copy_size);
    } else {
      DCHECK_EQ(FAST_SLOPPY_ARGUMENTS_ELEMENTS, from_kind);
      CopyObjectToObjectElements(isolate, from, HOLEY_ELEMENTS, from_start, to,
                                 HOLEY_ELEMENTS, to_start, copy_size);
    }
  }

  static Maybe<bool> GrowCapacityAndConvertImpl(Handle<JSObject> object,
                                                uint32_t capacity) {
    Isolate* isolate = object->GetIsolate();
    DirectHandle<SloppyArgumentsElements> elements(
        Cast<SloppyArgumentsElements>(object->elements()), isolate);
    Handle<FixedArray> old_arguments(Cast<FixedArray>(elements->arguments()),
                                     isolate);
    ElementsKind from_kind = object->GetElementsKind();
    // This method should only be called if there's a reason to update the
    // elements.
    DCHECK(from_kind == SLOW_SLOPPY_ARGUMENTS_ELEMENTS ||
           static_cast<uint32_t>(old_arguments->length()) < capacity);
    Handle<FixedArrayBase> arguments;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, arguments,
        ConvertElementsWithCapacity(object, old_arguments, from_kind, capacity),
        Nothing<bool>());
    DirectHandle<Map> new_map = JSObject::GetElementsTransitionMap(
        object, FAST_SLOPPY_ARGUMENTS_ELEMENTS);
    JSObject::MigrateToMap(isolate, object, new_map);
    elements->set_arguments(Cast<FixedArray>(*arguments));
    JSObject::ValidateElements(*object);
    return Just(true);
  }
};

template <typename Subclass, typename BackingStoreAccessor, typename KindTraits>
class StringWrapperElementsAccessor
    : public ElementsAccessorBase<Subclass, KindTraits> {
 public:
  static Handle<Object> GetInternalImpl(Isolate* isolate,
                                        Handle<JSObject> holder,
                                        InternalIndex entry) {
    return GetImpl(holder, entry);
  }

  static Handle<Object> GetImpl(DirectHandle<JSObject> holder,
                                InternalIndex entry) {
    Isolate* isolate = holder->GetIsolate();
    Handle<String> string(GetString(*holder), isolate);
    uint32_t length = static_cast<uint32_t>(string->length());
    if (entry.as_uint32() < length) {
      return isolate->factory()->LookupSingleCharacterStringFromCode(
          String::Flatten(isolate, string)->Get(entry.as_int()));
    }
    return BackingStoreAccessor::GetImpl(isolate, holder->elements(),
                                         entry.adjust_down(length));
  }

  static Handle<Object> GetImpl(Isolate* isolate,
                                Tagged<FixedArrayBase> elements,
                                InternalIndex entry) {
    UNREACHABLE();
  }

  static PropertyDetails GetDetailsImpl(Tagged<JSObject> holder,
                                        InternalIndex entry) {
    uint32_t length = static_cast<uint32_t>(GetString(holder)->length());
    if (entry.as_uint32() < length) {
      PropertyAttributes attributes =
          static_cast<PropertyAttributes>(READ_ONLY | DONT_DELETE);
      return PropertyDetails(PropertyKind::kData, attributes,
                             PropertyCellType::kNoCell);
    }
    return BackingStoreAccessor::GetDetailsImpl(holder,
                                                entry.adjust_down(length));
  }

  static InternalIndex GetEntryForIndexImpl(
      Isolate* isolate, Tagged<JSObject> holder,
      Tagged<FixedArrayBase> backing_store, size_t index,
      PropertyFilter filter) {
    uint32_t length = static_cast<uint32_t>(GetString(holder)->length());
    if (index < length) return InternalIndex(index);
    InternalIndex backing_store_entry =
        BackingStoreAccessor::GetEntryForIndexImpl(
            isolate, holder, backing_store, index, filter);
    if (backing_store_entry.is_not_found()) return backing_store_entry;
    return backing_store_entry.adjust_up(length);
  }

  static void DeleteImpl(Handle<JSObject> holder, InternalIndex entry) {
    uint32_t length = static_cast<uint32_t>(GetString(*holder)->length());
    if (entry.as_uint32() < length) {
      return;  // String contents can't be deleted.
    }
    BackingStoreAccessor::DeleteImpl(holder, entry.adjust_down(length));
  }

  static void SetImpl(DirectHandle<JSObject> holder, InternalIndex entry,
                      Tagged<Object> value) {
    uint32_t length = static_cast<uint32_t>(GetString(*holder)->length());
    if (entry.as_uint32() < length) {
      return;  // String contents are read-only.
    }
    BackingStoreAccessor::SetImpl(holder->elements(), entry.adjust_down(length),
                                  value);
  }

  static Maybe<bool> AddImpl(Handle<JSObject> object, uint32_t index,
                             DirectHandle<Object> value,
                             PropertyAttributes attributes,
                             uint32_t new_capacity) {
    DCHECK(index >= static_cast<uint32_t>(GetString(*object)->length()));
    // Explicitly grow fast backing stores if needed. Dictionaries know how to
    // extend their capacity themselves.
    if (KindTraits::Kind == FAST_STRING_WRAPPER_ELEMENTS &&
        (object->GetElementsKind() == SLOW_STRING_WRAPPER_ELEMENTS ||
         BackingStoreAccessor::GetCapacityImpl(*object, object->elements()) !=
             new_capacity)) {
      MAYBE_RETURN(GrowCapacityAndConvertImpl(object, new_capacity),
                   Nothing<bool>());
    }
    BackingStoreAccessor::AddImpl(object, index, value, attributes,
                                  new_capacity);
    return Just(true);
  }

  static void ReconfigureImpl(Handle<JSObject> object,
                              Handle<FixedArrayBase> store, InternalIndex entry,
                              Handle<Object> value,
                              PropertyAttributes attributes) {
    uint32_t length = static_cast<uint32_t>(GetString(*object)->length());
    if (entry.as_uint32() < length) {
      return;  // String contents can't be reconfigured.
    }
    BackingStoreAccessor::ReconfigureImpl(
        object, store, entry.adjust_down(length), value, attributes);
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus AddElementsToKeyAccumulatorImpl(
      Handle<JSObject> receiver, KeyAccumulator* accumulator,
      AddKeyConversion convert) {
    Isolate* isolate = receiver->GetIsolate();
    Handle<String> string(GetString(*receiver), isolate);
    string = String::Flatten(isolate, string);
    uint32_t length = static_cast<uint32_t>(string->length());
    for (uint32_t i = 0; i < length; i++) {
      Handle<String> key =
          isolate->factory()->LookupSingleCharacterStringFromCode(
              string->Get(i));
      RETURN_FAILURE_IF_NOT_SUCCESSFUL(accumulator->AddKey(key, convert));
    }
    return BackingStoreAccessor::AddElementsToKeyAccumulatorImpl(
        receiver, accumulator, convert);
  }

  V8_WARN_UNUSED_RESULT static ExceptionStatus CollectElementIndicesImpl(
      Handle<JSObject> object, Handle<FixedArrayBase> backing_store,
      KeyAccumulator* keys) {
    uint32_t length = GetString(*object)->length();
    Factory* factory = keys->isolate()->factory();
    for (uint32_t i = 0; i < length; i++) {
      RETURN_FAILURE_IF_NOT_SUCCESSFUL(
          keys->AddKey(factory->NewNumberFromUint(i)));
    }
    return BackingStoreAccessor::CollectElementIndicesImpl(object,
                                                           backing_store, keys);
  }

  static Maybe<bool> GrowCapacityAndConvertImpl(Handle<JSObject> object,
                                                uint32_t capacity) {
    Handle<FixedArrayBase> old_elements(object->elements(),
                                        object->GetIsolate());
    ElementsKind from_kind = object->GetElementsKind();
    if (from_kind == FAST_STRING_WRAPPER_ELEMENTS) {
      // The optimizing compiler relies on the prototype lookups of String
      // objects always returning undefined. If there's a store to the
      // initial String.prototype object, make sure all the optimizations
      // are invalidated.
      object->GetIsolate()->UpdateNoElementsProtectorOnSetLength(object);
    }
    // This method should only be called if there's a reason to update the
    // elements.
    DCHECK(from_kind == SLOW_STRING_WRAPPER_ELEMENTS ||
           static_cast<uint32_t>(old_elements->length()) < capacity);
    return Subclass::BasicGrowCapacityAndConvertImpl(
        object, old_elements, from_kind, FAST_STRING_WRAPPER_ELEMENTS,
        capacity);
  }

  static void CopyElementsImpl(Isolate* isolate, Tagged<FixedArrayBase> from,
                               uint32_t from_start, Tagged<FixedArrayBase> to,
                               ElementsKind from_kind, uint32_t to_start,
                               int packed_size, int copy_size) {
    DCHECK(!IsNumberDictionary(to));
    if (from_kind == SLOW_STRING_WRAPPER_ELEMENTS) {
      CopyDictionaryToObjectElements(isolate, from, from_start, to,
                                     HOLEY_ELEMENTS, to_start, copy_size);
    } else {
      DCHECK_EQ(FAST_STRING_WRAPPER_ELEMENTS, from_kind);
      CopyObjectToObjectElements(isolate, from, HOLEY_ELEMENTS, from_start, to,
                                 HOLEY_ELEMENTS, to_start, copy_size);
    }
  }

  static uint32_t NumberOfElementsImpl(Tagged<JSObject> object,
                                       Tagged<FixedArrayBase> backing_store) {
    uint32_t length = GetString(object)->length();
    return length +
           BackingStoreAccessor::NumberOfElementsImpl(object, backing_store);
  }

 private:
  static Tagged<String> GetString(Tagged<JSObject> holder) {
    DCHECK(IsJSPrimitiveWrapper(holder));
    Tagged<JSPrimitiveWrapper> js_value = Cast<JSPrimitiveWrapper>(holder);
    DCHECK(IsString(js_value->value()));
    return Cast<String>(js_value->value());
  }
};

class FastStringWrapperElementsAccessor
    : public StringWrapperElementsAccessor<
          FastStringWrapperElementsAccessor, FastHoleyObjectElementsAccessor,
          ElementsKindTraits<FAST_STRING_WRAPPER_ELEMENTS>> {
 public:
  static Handle<NumberDictionary> NormalizeImpl(
      Handle<JSObject> object, DirectHandle<FixedArrayBase> elements) {
    return FastHoleyObjectElementsAccessor::NormalizeImpl(object, elements);
  }
};

class SlowStringWrapperElementsAccessor
    : public StringWrapperElementsAccessor<
          SlowStringWrapperElementsAccessor, DictionaryElementsAccessor,
          ElementsKindTraits<SLOW_STRING_WRAPPER_ELEMENTS>> {
 public:
  static bool HasAccessorsImpl(Tagged<JSObject> holder,
                               Tagged<FixedArrayBase> backing_store) {
    return DictionaryElementsAccessor::HasAccessorsImpl(holder, backing_store);
  }
};

}  // namespace

MaybeHandle<Object> ArrayConstructInitializeElements(
    Handle<JSArray> array, JavaScriptArguments* args) {
  if (args->length() == 0) {
    // Optimize the case where there are no parameters passed.
    JSArray::Initialize(array, JSArray::kPreallocatedArrayElements);
    return array;

  } else if (args->length() == 1 && IsNumber(*args->at(0))) {
    uint32_t length;
    if (!Object::ToArrayLength(*args->at(0), &length)) {
      return ThrowArrayLengthRangeError(array->GetIsolate());
    }

    // Optimize the case where there is one argument and the argument is a small
    // smi.
    if (length > 0 && length < JSArray::kInitialMaxFastElementArray) {
      ElementsKind elements_kind = array->GetElementsKind();
      JSArray::Initialize(array, length, length);

      if (!IsHoleyElementsKind(elements_kind)) {
        elements_kind = GetHoleyElementsKind(elements_kind);
        JSObject::TransitionElementsKind(array, elements_kind);
      }
    } else if (length == 0) {
      JSArray::Initialize(array, JSArray::kPreallocatedArrayElements);
    } else {
      // Take the argument as the length.
      JSArray::Initialize(array, 0);
      MAYBE_RETURN_NULL(JSArray::SetLength(array, length));
    }
    return array;
  }

  Factory* factory = array->GetIsolate()->factory();

  // Set length and elements on the array.
  int number_of_elements = args->length();
  JSObject::EnsureCanContainElements(array, args, number_of_elements,
                                     ALLOW_CONVERTED_DOUBLE_ELEMENTS);

  // Allocate an appropriately typed elements array.
  ElementsKind elements_kind = array->GetElementsKind();
  Handle<FixedArrayBase> elms;
  if (IsDoubleElementsKind(elements_kind)) {
    elms =
        Cast<FixedArrayBase>(factory->NewFixedDoubleArray(number_of_elements));
  } else {
    elms = Cast<FixedArrayBase>(
        factory->NewFixedArrayWithHoles(number_of_elements));
  }

  // Fill in the content
  switch (elements_kind) {
    case HOLEY_SMI_ELEMENTS:
    case PACKED_SMI_ELEMENTS: {
      auto smi_elms = Cast<FixedArray>(elms);
      for (int entry = 0; entry < number_of_elements; entry++) {
        smi_elms->set(entry, (*args)[entry], SKIP_WRITE_BARRIER);
      }
      break;
    }
    case HOLEY_ELEMENTS:
    case PACKED_ELEMENTS: {
      DisallowGarbageCollection no_gc;
      WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);
      auto object_elms = Cast<FixedArray>(elms);
      for (int entry = 0; entry < number_of_elements; entry++) {
        object_elms->set(entry, (*args)[entry], mode);
      }
      break;
    }
    case HOLEY_DOUBLE_ELEMENTS:
    case PACKED_DOUBLE_ELEMENTS: {
      auto double_elms = Cast<FixedDoubleArray>(elms);
      for (int entry = 0; entry < number_of_elements; entry++) {
        double_elms->set(entry, Object::NumberValue((*args)[entry]));
      }
      break;
    }
    default:
      UNREACHABLE();
  }

  array->set_elements(*elms);
  array->set_length(Smi::FromInt(number_of_elements));
  return array;
}

void CopyFastNumberJSArrayElementsToTypedArray(Address raw_context,
                                               Address raw_source,
                                               Address raw_destination,
                                               uintptr_t length,
                                               uintptr_t offset) {
  Tagged<Context> context = Cast<Context>(Tagged<Object>(raw_context));
  Tagged<JSArray> source = Cast<JSArray>(Tagged<Object>(raw_source));
  Tagged<JSTypedArray> destination =
      Cast<JSTypedArray>(Tagged<Object>(raw_destination));

  switch (destination->GetElementsKind()) {
#define TYPED_ARRAYS_CASE(Type, type, TYPE, ctype)           \
  case TYPE##_ELEMENTS:                                      \
    CHECK(Type##ElementsAccessor::TryCopyElementsFastNumber( \
        context, source, destination, length, offset));      \
    break;
    TYPED_ARRAYS(TYPED_ARRAYS_CASE)
    RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAYS_CASE)
#undef TYPED_ARRAYS_CASE
    default:
      UNREACHABLE();
  }
}

void CopyTypedArrayElementsToTypedArray(Address raw_source,
                                        Address raw_destination,
                                        uintptr_t length, uintptr_t offset) {
  Tagged<JSTypedArray> source = Cast<JSTypedArray>(Tagged<Object>(raw_source));
  Tagged<JSTypedArray> destination =
      Cast<JSTypedArray>(Tagged<Object>(raw_destination));

  switch (destination->GetElementsKind()) {
#define TYPED_ARRAYS_CASE(Type, type, TYPE, ctype)                          \
  case TYPE##_ELEMENTS:                                                     \
    Type##ElementsAccessor::CopyElementsFromTypedArray(source, destination, \
                                                       length, offset);     \
    break;
    TYPED_ARRAYS(TYPED_ARRAYS_CASE)
    RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAYS_CASE)
#undef TYPED_ARRAYS_CASE
    default:
      UNREACHABLE();
  }
}

void CopyTypedArrayElementsSlice(Address raw_source, Address raw_destination,
                                 uintptr_t start, uintptr_t end) {
  Tagged<JSTypedArray> source = Cast<JSTypedArray>(Tagged<Object>(raw_source));
  Tagged<JSTypedArray> destination =
      Cast<JSTypedArray>(Tagged<Object>(raw_destination));

  destination->GetElementsAccessor()->CopyTypedArrayElementsSlice(
      source, destination, start, end);
}

void ElementsAccessor::InitializeOncePerProcess() {
  // Here we create an array with more entries than element kinds.
  // This is due to the sandbox: this array is indexed with an ElementsKind
  // read directly from within the sandbox, which must therefore be considered
  // attacker-controlled. An ElementsKind is a uint8_t under the hood, so we
  // can either use an array with 256 entries or have an explicit bounds-check
  // on access. The latter is probably more expensive.
  static_assert(std::is_same_v<std::underlying_type_t<ElementsKind>, uint8_t>);
  static ElementsAccessor* accessor_array[256] = {
#define ACCESSOR_ARRAY(Class, Kind, Store) new Class(),
      ELEMENTS_LIST(ACCESSOR_ARRAY)
#undef ACCESSOR_ARRAY
  };

  static_assert((sizeof(accessor_array) / sizeof(*accessor_array)) >=
                kElementsKindCount);

  elements_accessors_ = accessor_array;
}

void ElementsAccessor::TearDown() {
  if (elements_accessors_ == nullptr) return;
#define ACCESSOR_DELETE(Class, Kind, Store) delete elements_accessors_[Kind];
  ELEMENTS_LIST(ACCESSOR_DELETE)
#undef ACCESSOR_DELETE
  elements_accessors_ = nullptr;
}

Handle<JSArray> ElementsAccessor::Concat(Isolate* isolate,
                                         BuiltinArguments* args,
                                         uint32_t concat_size,
                                         uint32_t result_len) {
  ElementsKind result_elements_kind = GetInitialFastElementsKind();
  bool has_raw_doubles = false;
  {
    DisallowGarbageCollection no_gc;
    bool is_holey = false;
    for (uint32_t i = 0; i < concat_size; i++) {
      Tagged<Object> arg = (*args)[i];
      ElementsKind arg_kind = Cast<JSArray>(arg)->GetElementsKind();
      has_raw_doubles = has_raw_doubles || IsDoubleElementsKind(arg_kind);
      is_holey = is_holey || IsHoleyElementsKind(arg_kind);
      result_elements_kind =
          GetMoreGeneralElementsKind(result_elements_kind, arg_kind);
    }
    if (is_holey) {
      result_elements_kind = GetHoleyElementsKind(result_elements_kind);
    }
  }

  // If a double array is concatted into a fast elements array, the fast
  // elements array needs to be initialized to contain proper holes, since
  // boxing doubles may cause incremental marking.
  bool requires_double_boxing =
      has_raw_doubles && !IsDoubleElementsKind(result_elements_kind);
  auto mode =
      requires_double_boxing
          ? ArrayStorageAllocationMode::INITIALIZE_ARRAY_ELEMENTS_WITH_HOLE
          : ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS;
  Handle<JSArray> result_array = isolate->factory()->NewJSArray(
      result_elements_kind, result_len, result_len, mode);
  if (result_len == 0) return result_array;

  uint32_t insertion_index = 0;
  Handle<FixedArrayBase> storage(result_array->elements(), isolate);
  ElementsAccessor* accessor = ElementsAccessor::ForKind(result_elements_kind);
  for (uint32_t i = 0; i < concat_size; i++) {
    // It is crucial to keep |array| in a raw pointer form to avoid
    // performance degradation.
    Tagged<JSArray> array = Cast<JSArray>((*args)[i]);
    uint32_t len = 0;
    Object::ToArrayLength(array->length(), &len);
    if (len == 0) continue;
    ElementsKind from_kind = array->GetElementsKind();
    accessor->CopyElements(isolate, array, 0, from_kind, storage,
                           insertion_index, len);
    insertion_index += len;
  }

  DCHECK_EQ(insertion_index, result_len);
  return result_array;
}

ElementsAccessor** ElementsAccessor::elements_accessors_ = nullptr;

#undef ELEMENTS_LIST
#undef RETURN_NOTHING_IF_NOT_SUCCESSFUL
#undef RETURN_FAILURE_IF_NOT_SUCCESSFUL
}  // namespace internal
}  // namespace v8
