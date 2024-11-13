// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEOPTIMIZATION_DATA_INL_H_
#define V8_OBJECTS_DEOPTIMIZATION_DATA_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/objects/deoptimization-data.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/js-regexp-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(DeoptimizationData, ProtectedFixedArray)

DEFINE_DEOPT_ELEMENT_ACCESSORS(FrameTranslation, DeoptimizationFrameTranslation)
DEFINE_DEOPT_ELEMENT_ACCESSORS(InlinedFunctionCount, Smi)
DEFINE_DEOPT_ELEMENT_ACCESSORS(LiteralArray, DeoptimizationLiteralArray)
DEFINE_DEOPT_ELEMENT_ACCESSORS(OsrBytecodeOffset, Smi)
DEFINE_DEOPT_ELEMENT_ACCESSORS(OsrPcOffset, Smi)
DEFINE_DEOPT_ELEMENT_ACCESSORS(OptimizationId, Smi)
DEFINE_DEOPT_ELEMENT_ACCESSORS(SharedFunctionInfoWrapper, Object)
DEFINE_DEOPT_ELEMENT_ACCESSORS(InliningPositions,
                               TrustedPodArray<InliningPosition>)
DEFINE_DEOPT_ELEMENT_ACCESSORS(DeoptExitStart, Smi)
DEFINE_DEOPT_ELEMENT_ACCESSORS(EagerDeoptCount, Smi)
DEFINE_DEOPT_ELEMENT_ACCESSORS(LazyDeoptCount, Smi)

DEFINE_DEOPT_ENTRY_ACCESSORS(BytecodeOffsetRaw, Smi)
DEFINE_DEOPT_ENTRY_ACCESSORS(TranslationIndex, Smi)
DEFINE_DEOPT_ENTRY_ACCESSORS(Pc, Smi)
#ifdef DEBUG
DEFINE_DEOPT_ENTRY_ACCESSORS(NodeId, Smi)
#endif  // DEBUG

Tagged<Object> DeoptimizationData::SharedFunctionInfo() const {
  return Cast<i::SharedFunctionInfoWrapper>(SharedFunctionInfoWrapper())
      ->shared_info();
}

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
    : TrustedWeakFixedArray(ptr) {
  // No type check is possible beyond that for WeakFixedArray.
}

inline Tagged<Object> DeoptimizationLiteralArray::get(int index) const {
  return get(GetPtrComprCageBase(*this), index);
}

inline Tagged<Object> DeoptimizationLiteralArray::get(
    PtrComprCageBase cage_base, int index) const {
  Tagged<MaybeObject> maybe = TrustedWeakFixedArray::get(index);

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
  return TrustedWeakFixedArray::get(index);
}

inline void DeoptimizationLiteralArray::set(int index, Tagged<Object> value) {
  Tagged<MaybeObject> maybe = value;
  if (IsBytecodeArray(value)) {
    // The BytecodeArray lives in trusted space, so we cannot reference it from
    // a fixed array. However, we can use the BytecodeArray's wrapper object,
    // which exists for exactly this purpose.
    maybe = Cast<BytecodeArray>(value)->wrapper();
#ifdef V8_ENABLE_SANDBOX
  } else if (IsRegExpData(value)) {
    // Store the RegExpData wrapper if the sandbox is enabled, as data lives in
    // trusted space. We can't store a tagged value to a trusted space object
    // inside the sandbox, we'd need to go through the trusted pointer table.
    // Otherwise we can store the RegExpData object directly.
    maybe = Cast<RegExpData>(value)->wrapper();
#endif
  } else if (Code::IsWeakObjectInDeoptimizationLiteralArray(value)) {
    maybe = MakeWeak(maybe);
  }
  TrustedWeakFixedArray::set(index, maybe);
}

inline DeoptimizationFrameTranslation::DeoptimizationFrameTranslation(
    Address ptr)
    : TrustedByteArray(ptr) {}

uint32_t DeoptimizationFrameTranslation::get_int(int offset) const {
  DCHECK_LE(offset + sizeof(uint32_t), length());
  return ReadField<uint32_t>(OffsetOfElementAt(offset));
}

void DeoptimizationFrameTranslation::set_int(int offset, uint32_t value) {
  DCHECK_LE(offset + sizeof(uint32_t), length());
  WriteField<uint32_t>(OffsetOfElementAt(offset), value);
}
}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEOPTIMIZATION_DATA_INL_H_
