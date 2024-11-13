// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CALL_SITE_INFO_INL_H_
#define V8_OBJECTS_CALL_SITE_INFO_INL_H_

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/call-site-info.h"
#include "src/objects/objects-inl.h"
#include "src/objects/struct-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/call-site-info-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(CallSiteInfo)
NEVER_READ_ONLY_SPACE_IMPL(CallSiteInfo)

#if V8_ENABLE_WEBASSEMBLY
BOOL_GETTER(CallSiteInfo, flags, IsWasm, IsWasmBit::kShift)
BOOL_GETTER(CallSiteInfo, flags, IsAsmJsWasm, IsAsmJsWasmBit::kShift)
BOOL_GETTER(CallSiteInfo, flags, IsAsmJsAtNumberConversion,
            IsAsmJsAtNumberConversionBit::kShift)
#if V8_ENABLE_DRUMBRAKE
BOOL_GETTER(CallSiteInfo, flags, IsWasmInterpretedFrame,
            IsWasmInterpretedFrameBit::kShift)
#endif  // V8_ENABLE_DRUMBRAKE
BOOL_GETTER(CallSiteInfo, flags, IsBuiltin, IsBuiltinBit::kShift)
#endif  // V8_ENABLE_WEBASSEMBLY
BOOL_GETTER(CallSiteInfo, flags, IsStrict, IsStrictBit::kShift)
BOOL_GETTER(CallSiteInfo, flags, IsConstructor, IsConstructorBit::kShift)
BOOL_GETTER(CallSiteInfo, flags, IsAsync, IsAsyncBit::kShift)

Tagged<HeapObject> CallSiteInfo::code_object(IsolateForSandbox isolate) const {
  DCHECK(!IsTrustedPointerFieldEmpty(kCodeObjectOffset));
  // The field can contain either a Code or a BytecodeArray, so we need to use
  // the kUnknownIndirectPointerTag. Since we can then no longer rely on the
  // type-checking mechanism of trusted pointers we need to perform manual type
  // checks afterwards.
  Tagged<HeapObject> code_object =
      ReadTrustedPointerField<kUnknownIndirectPointerTag>(kCodeObjectOffset,
                                                          isolate);
  CHECK(IsCode(code_object) || IsBytecodeArray(code_object));
  return code_object;
}

void CallSiteInfo::set_code_object(Tagged<HeapObject> code,
                                   WriteBarrierMode mode) {
  DCHECK(IsCode(code) || IsBytecodeArray(code) || IsUndefined(code));
  if (IsCode(code) || IsBytecodeArray(code)) {
    WriteTrustedPointerField<kUnknownIndirectPointerTag>(
        kCodeObjectOffset, Cast<ExposedTrustedObject>(code));
    CONDITIONAL_TRUSTED_POINTER_WRITE_BARRIER(
        *this, kCodeObjectOffset, kUnknownIndirectPointerTag, code, mode);
  } else {
    DCHECK(IsUndefined(code));
    ClearTrustedPointerField(kCodeObjectOffset);
  }
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_CALL_SITE_INFO_INL_H_
