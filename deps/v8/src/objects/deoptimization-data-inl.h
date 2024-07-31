// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEOPTIMIZATION_DATA_INL_H_
#define V8_OBJECTS_DEOPTIMIZATION_DATA_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/objects/deoptimization-data.h"
#include "src/objects/fixed-array-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(DeoptimizationData, TrustedFixedArray)

CAST_ACCESSOR(DeoptimizationData)
CAST_ACCESSOR(DeoptimizationLiteralArray)
CAST_ACCESSOR(DeoptimizationFrameTranslation)

DEFINE_DEOPT_ELEMENT_ACCESSORS(FrameTranslation,
                               Tagged<DeoptimizationFrameTranslation>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(InlinedFunctionCount, Tagged<Smi>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(LiteralArray, Tagged<DeoptimizationLiteralArray>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(OsrBytecodeOffset, Tagged<Smi>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(OsrPcOffset, Tagged<Smi>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(OptimizationId, Tagged<Smi>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(InliningPositions,
                               Tagged<PodArray<InliningPosition>>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(DeoptExitStart, Tagged<Smi>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(EagerDeoptCount, Tagged<Smi>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(LazyDeoptCount, Tagged<Smi>)

DEFINE_DEOPT_ENTRY_ACCESSORS(BytecodeOffsetRaw, Tagged<Smi>)
DEFINE_DEOPT_ENTRY_ACCESSORS(TranslationIndex, Tagged<Smi>)
DEFINE_DEOPT_ENTRY_ACCESSORS(Pc, Tagged<Smi>)
#ifdef DEBUG
DEFINE_DEOPT_ENTRY_ACCESSORS(NodeId, Tagged<Smi>)
#endif  // DEBUG

BytecodeOffset DeoptimizationData::GetBytecodeOffsetOrBuiltinContinuationId(
    int i) const {
  return BytecodeOffset(BytecodeOffsetRaw(i).value());
}

void DeoptimizationData::SetBytecodeOffset(int i, BytecodeOffset value) {
  SetBytecodeOffsetRaw(i, Smi::FromInt(value.ToInt()));
}

int DeoptimizationData::DeoptCount() const {
  return (length() - kFirstDeoptEntryIndex) / kDeoptEntrySize;
}

inline DeoptimizationLiteralArray::DeoptimizationLiteralArray(Address ptr)
    : WeakFixedArray(ptr) {
  // No type check is possible beyond that for WeakFixedArray.
}

inline Tagged<Object> DeoptimizationLiteralArray::get(int index) const {
  return get(GetPtrComprCageBase(*this), index);
}

inline Tagged<Object> DeoptimizationLiteralArray::get(
    PtrComprCageBase cage_base, int index) const {
  Tagged<MaybeObject> maybe = WeakFixedArray::get(index);

  // Slots in the DeoptimizationLiteralArray should only be cleared when there
  // is no possible code path that could need that slot. This works because the
  // weakly-held deoptimization literals are basically local variables that
  // TurboFan has decided not to keep on the stack. Thus, if the deoptimization
  // literal goes away, then whatever code needed it should be unreachable. The
  // exception is currently running InstructionStream: in that case, the
  // deoptimization literals array might be the only thing keeping the target
  // object alive. Thus, when a InstructionStream is running, we strongly mark
  // all of its deoptimization literals.
  CHECK(!maybe.IsCleared());

  return maybe.GetHeapObjectOrSmi();
}

inline Tagged<MaybeObject> DeoptimizationLiteralArray::get_raw(
    int index) const {
  return WeakFixedArray::get(index);
}

inline void DeoptimizationLiteralArray::set(int index, Tagged<Object> value) {
  Tagged<MaybeObject> maybe = value;
  if (IsBytecodeArray(value)) {
    // The BytecodeArray lives in trusted space, so we cannot reference it from
    // a fixed array. However, we can use the BytecodeArray's wrapper object,
    // which exists for exactly this purpose.
    maybe = BytecodeArray::cast(value)->wrapper();
  } else if (Code::IsWeakObjectInDeoptimizationLiteralArray(value)) {
    maybe = MakeWeak(maybe);
  }
  WeakFixedArray::set(index, maybe);
}

inline DeoptimizationFrameTranslation::DeoptimizationFrameTranslation(
    Address ptr)
    : ByteArray(ptr) {}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEOPTIMIZATION_DATA_INL_H_
