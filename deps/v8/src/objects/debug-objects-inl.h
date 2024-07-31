// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEBUG_OBJECTS_INL_H_
#define V8_OBJECTS_DEBUG_OBJECTS_INL_H_

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/bytecode-array-inl.h"
#include "src/objects/code-inl.h"
#include "src/objects/debug-objects.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/debug-objects-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(BreakPoint)
TQ_OBJECT_CONSTRUCTORS_IMPL(BreakPointInfo)
TQ_OBJECT_CONSTRUCTORS_IMPL(CoverageInfo)
TQ_OBJECT_CONSTRUCTORS_IMPL(DebugInfo)

NEVER_READ_ONLY_SPACE_IMPL(DebugInfo)

BIT_FIELD_ACCESSORS(DebugInfo, debugger_hints, side_effect_state,
                    DebugInfo::SideEffectStateBits)
BIT_FIELD_ACCESSORS(DebugInfo, debugger_hints, debug_is_blackboxed,
                    DebugInfo::DebugIsBlackboxedBit)
BIT_FIELD_ACCESSORS(DebugInfo, debugger_hints, computed_debug_is_blackboxed,
                    DebugInfo::ComputedDebugIsBlackboxedBit)
BIT_FIELD_ACCESSORS(DebugInfo, debugger_hints, debugging_id,
                    DebugInfo::DebuggingIdBits)

bool DebugInfo::HasInstrumentedBytecodeArray() {
  return has_debug_bytecode_array();
}

Tagged<BytecodeArray> DebugInfo::OriginalBytecodeArray(Isolate* isolate) {
  DCHECK(HasInstrumentedBytecodeArray());
  return original_bytecode_array(isolate, kAcquireLoad);
}

Tagged<BytecodeArray> DebugInfo::DebugBytecodeArray(Isolate* isolate) {
  DCHECK(HasInstrumentedBytecodeArray());
  Tagged<BytecodeArray> result = debug_bytecode_array(isolate, kAcquireLoad);
  DCHECK_EQ(shared()->GetActiveBytecodeArray(isolate), result);
  return result;
}

TRUSTED_POINTER_ACCESSORS(DebugInfo, debug_bytecode_array, BytecodeArray,
                          kDebugBytecodeArrayOffset,
                          kBytecodeArrayIndirectPointerTag)
TRUSTED_POINTER_ACCESSORS(DebugInfo, original_bytecode_array, BytecodeArray,
                          kOriginalBytecodeArrayOffset,
                          kBytecodeArrayIndirectPointerTag)

TQ_OBJECT_CONSTRUCTORS_IMPL(StackFrameInfo)
NEVER_READ_ONLY_SPACE_IMPL(StackFrameInfo)

Tagged<Script> StackFrameInfo::script() const {
  Tagged<HeapObject> object = shared_or_script();
  if (IsSharedFunctionInfo(object)) {
    object = SharedFunctionInfo::cast(object)->script();
  }
  return Script::cast(object);
}

BIT_FIELD_ACCESSORS(StackFrameInfo, flags, bytecode_offset_or_source_position,
                    StackFrameInfo::BytecodeOffsetOrSourcePositionBits)
BIT_FIELD_ACCESSORS(StackFrameInfo, flags, is_constructor,
                    StackFrameInfo::IsConstructorBit)

NEVER_READ_ONLY_SPACE_IMPL(ErrorStackData)
TQ_OBJECT_CONSTRUCTORS_IMPL(ErrorStackData)

bool ErrorStackData::HasFormattedStack() const {
  return !IsFixedArray(call_site_infos_or_formatted_stack());
}

ACCESSORS_RELAXED_CHECKED(ErrorStackData, formatted_stack, Tagged<Object>,
                          kCallSiteInfosOrFormattedStackOffset,
                          !IsSmi(limit_or_stack_frame_infos()))

bool ErrorStackData::HasCallSiteInfos() const { return !HasFormattedStack(); }

ACCESSORS_RELAXED_CHECKED(ErrorStackData, call_site_infos, Tagged<FixedArray>,
                          kCallSiteInfosOrFormattedStackOffset,
                          !HasFormattedStack())

NEVER_READ_ONLY_SPACE_IMPL(PromiseOnStack)
TQ_OBJECT_CONSTRUCTORS_IMPL(PromiseOnStack)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEBUG_OBJECTS_INL_H_
