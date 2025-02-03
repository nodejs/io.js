// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_DATAVIEW_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_DATAVIEW_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename Next>
class DataViewLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(DataViewLowering)

  OpIndex BuildReverseBytes(ExternalArrayType type, OpIndex value) {
    switch (type) {
      case kExternalInt8Array:
      case kExternalUint8Array:
      case kExternalUint8ClampedArray:
        return value;
      case kExternalInt16Array:
        return __ Word32ShiftRightArithmetic(__ Word32ReverseBytes(value), 16);
      case kExternalUint16Array:
        return __ Word32ShiftRightLogical(__ Word32ReverseBytes(value), 16);
      case kExternalInt32Array:
      case kExternalUint32Array:
        return __ Word32ReverseBytes(value);
      case kExternalFloat32Array: {
        V<Word32> bytes = __ BitcastFloat32ToWord32(value);
        V<Word32> reversed = __ Word32ReverseBytes(bytes);
        return __ BitcastWord32ToFloat32(reversed);
      }
      case kExternalFloat64Array: {
        if constexpr (Is64()) {
          V<Word64> bytes = __ BitcastFloat64ToWord64(value);
          V<Word64> reversed = __ Word64ReverseBytes(bytes);
          return __ BitcastWord64ToFloat64(reversed);
        } else {
          V<Word32> reversed_lo =
              __ Word32ReverseBytes(__ Float64ExtractLowWord32(value));
          V<Word32> reversed_hi =
              __ Word32ReverseBytes(__ Float64ExtractHighWord32(value));
          return __ BitcastWord32PairToFloat64(reversed_lo, reversed_hi);
        }
      }
      case kExternalBigInt64Array:
      case kExternalBigUint64Array:
        return __ Word64ReverseBytes(value);
      case kExternalFloat16Array:
        UNIMPLEMENTED();
    }
  }

  OpIndex REDUCE(LoadDataViewElement)(V<Object> object, V<WordPtr> storage,
                                      V<WordPtr> index,
                                      V<Word32> is_little_endian,
                                      ExternalArrayType element_type) {
    const MachineType machine_type =
        AccessBuilder::ForTypedArrayElement(element_type, true).machine_type;
    const MemoryRepresentation memory_rep =
        MemoryRepresentation::FromMachineType(machine_type);

    OpIndex value =
        __ Load(storage, index,
                LoadOp::Kind::MaybeUnaligned(memory_rep).NotLoadEliminable(),
                memory_rep);

    Variable result = Asm().NewLoopInvariantVariable(
        RegisterRepresentationForArrayType(element_type));
    IF (is_little_endian) {
#if V8_TARGET_LITTLE_ENDIAN
      Asm().SetVariable(result, value);
#else
      Asm().SetVariable(result, BuildReverseBytes(element_type, value));
#endif  // V8_TARGET_LITTLE_ENDIAN
    } ELSE {
#if V8_TARGET_LITTLE_ENDIAN
      Asm().SetVariable(result, BuildReverseBytes(element_type, value));
#else
      Asm().SetVariable(result, value);
#endif  // V8_TARGET_LITTLE_ENDIAN
    }

    // We need to keep the {object} (either the JSArrayBuffer or the JSDataView)
    // alive so that the GC will not release the JSArrayBuffer (if there's any)
    // as long as we are still operating on it.
    __ Retain(object);
    return Asm().GetVariable(result);
  }

  OpIndex REDUCE(StoreDataViewElement)(V<Object> object, V<WordPtr> storage,
                                       V<WordPtr> index, OpIndex value,
                                       V<Word32> is_little_endian,
                                       ExternalArrayType element_type) {
    const MachineType machine_type =
        AccessBuilder::ForTypedArrayElement(element_type, true).machine_type;

    Variable value_to_store = Asm().NewLoopInvariantVariable(
        RegisterRepresentationForArrayType(element_type));
    IF (is_little_endian) {
#if V8_TARGET_LITTLE_ENDIAN
      Asm().SetVariable(value_to_store, value);
#else
      Asm().SetVariable(value_to_store, BuildReverseBytes(element_type, value));
#endif  // V8_TARGET_LITTLE_ENDIAN
    } ELSE {
#if V8_TARGET_LITTLE_ENDIAN
      Asm().SetVariable(value_to_store, BuildReverseBytes(element_type, value));
#else
      Asm().SetVariable(value_to_store, value);
#endif  // V8_TARGET_LITTLE_ENDIAN
    }

    const MemoryRepresentation memory_rep =
        MemoryRepresentation::FromMachineType(machine_type);
    __ Store(storage, index, Asm().GetVariable(value_to_store),
             StoreOp::Kind::MaybeUnaligned(memory_rep).NotLoadEliminable(),
             memory_rep, WriteBarrierKind::kNoWriteBarrier);

    // We need to keep the {object} (either the JSArrayBuffer or the JSDataView)
    // alive so that the GC will not release the JSArrayBuffer (if there's any)
    // as long as we are still operating on it.
    __ Retain(object);
    return {};
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_DATAVIEW_LOWERING_REDUCER_H_
