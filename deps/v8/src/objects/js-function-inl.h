// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_FUNCTION_INL_H_
#define V8_OBJECTS_JS_FUNCTION_INL_H_

#include "src/objects/js-function.h"

// Include other inline headers *after* including js-function.h, such that e.g.
// the definition of JSFunction is available (and this comment prevents
// clang-format from merging that include into the following ones).
#include "src/diagnostics/code-tracer.h"
#include "src/ic/ic.h"
#include "src/init/bootstrapper.h"
#include "src/objects/abstract-code-inl.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/map-updater.h"
#include "src/objects/shared-function-info-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-function-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSFunctionOrBoundFunctionOrWrappedFunction)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSBoundFunction)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSWrappedFunction)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSFunction)

ACCESSORS(JSFunction, raw_feedback_cell, Tagged<FeedbackCell>,
          kFeedbackCellOffset)
RELEASE_ACQUIRE_ACCESSORS(JSFunction, raw_feedback_cell, Tagged<FeedbackCell>,
                          kFeedbackCellOffset)

DEF_GETTER(JSFunction, feedback_vector, Tagged<FeedbackVector>) {
  DCHECK(has_feedback_vector(cage_base));
  return FeedbackVector::cast(raw_feedback_cell(cage_base)->value(cage_base));
}

Tagged<ClosureFeedbackCellArray> JSFunction::closure_feedback_cell_array()
    const {
  DCHECK(has_closure_feedback_cell_array());
  return ClosureFeedbackCellArray::cast(raw_feedback_cell()->value());
}

void JSFunction::reset_tiering_state() {
  DCHECK(has_feedback_vector());
  feedback_vector()->reset_tiering_state();
}

bool JSFunction::ChecksTieringState(IsolateForSandbox isolate) {
  return code(isolate)->checks_tiering_state();
}

void JSFunction::CompleteInobjectSlackTrackingIfActive() {
  if (!has_prototype_slot()) return;
  if (has_initial_map() && initial_map()->IsInobjectSlackTrackingInProgress()) {
    MapUpdater::CompleteInobjectSlackTracking(GetIsolate(), initial_map());
  }
}

template <typename IsolateT>
Tagged<AbstractCode> JSFunction::abstract_code(IsolateT* isolate) {
  if (ActiveTierIsIgnition(isolate)) {
    return AbstractCode::cast(shared()->GetBytecodeArray(isolate));
  } else {
    return AbstractCode::cast(code(isolate, kAcquireLoad));
  }
}

int JSFunction::length() { return shared()->length(); }

Tagged<Code> JSFunction::code(IsolateForSandbox isolate) const {
  return ReadCodePointerField(kCodeOffset, isolate);
}

void JSFunction::set_code(Tagged<Code> value, WriteBarrierMode mode) {
  WriteCodePointerField(kCodeOffset, value);
  CONDITIONAL_CODE_POINTER_WRITE_BARRIER(*this, kCodeOffset, value, mode);
}

Tagged<Code> JSFunction::code(IsolateForSandbox isolate,
                              AcquireLoadTag tag) const {
  return ReadCodePointerField(kCodeOffset, isolate);
}

void JSFunction::set_code(Tagged<Code> value, ReleaseStoreTag,
                          WriteBarrierMode mode) {
  WriteCodePointerField(kCodeOffset, value);
  CONDITIONAL_CODE_POINTER_WRITE_BARRIER(*this, kCodeOffset, value, mode);

  if (V8_UNLIKELY(v8_flags.log_function_events && has_feedback_vector())) {
    feedback_vector()->set_log_next_execution(true);
  }
}

Tagged<Object> JSFunction::raw_code(IsolateForSandbox isolate) const {
#ifdef V8_ENABLE_SANDBOX
  return RawIndirectPointerField(kCodeOffset, kCodeIndirectPointerTag)
      .Relaxed_Load(isolate);
#else
  return RELAXED_READ_FIELD(*this, JSFunction::kCodeOffset);
#endif  // V8_ENABLE_SANDBOX
}

Tagged<Object> JSFunction::raw_code(IsolateForSandbox isolate,
                                    AcquireLoadTag tag) const {
#ifdef V8_ENABLE_SANDBOX
  return RawIndirectPointerField(kCodeOffset, kCodeIndirectPointerTag)
      .Acquire_Load(isolate);
#else
  return ACQUIRE_READ_FIELD(*this, JSFunction::kCodeOffset);
#endif  // V8_ENABLE_SANDBOX
}

RELEASE_ACQUIRE_ACCESSORS(JSFunction, context, Tagged<Context>, kContextOffset)

Address JSFunction::instruction_start(IsolateForSandbox isolate) const {
  return code(isolate)->instruction_start();
}

// TODO(ishell): Why relaxed read but release store?
DEF_GETTER(JSFunction, shared, Tagged<SharedFunctionInfo>) {
  return shared(cage_base, kRelaxedLoad);
}

DEF_RELAXED_GETTER(JSFunction, shared, Tagged<SharedFunctionInfo>) {
  return TaggedField<SharedFunctionInfo,
                     kSharedFunctionInfoOffset>::Relaxed_Load(cage_base, *this);
}

void JSFunction::set_shared(Tagged<SharedFunctionInfo> value,
                            WriteBarrierMode mode) {
  // Release semantics to support acquire read in NeedsResetDueToFlushedBytecode
  RELEASE_WRITE_FIELD(*this, kSharedFunctionInfoOffset, value);
  CONDITIONAL_WRITE_BARRIER(*this, kSharedFunctionInfoOffset, value, mode);
}

TieringState JSFunction::tiering_state() const {
  if (!has_feedback_vector()) return TieringState::kNone;
  return feedback_vector()->tiering_state();
}

void JSFunction::set_tiering_state(IsolateForSandbox isolate,
                                   TieringState state) {
  DCHECK(has_feedback_vector());
  DCHECK(IsNone(state) || ChecksTieringState(isolate));
  feedback_vector()->set_tiering_state(state);
}

TieringState JSFunction::osr_tiering_state() {
  DCHECK(has_feedback_vector());
  return feedback_vector()->osr_tiering_state();
}

void JSFunction::set_osr_tiering_state(TieringState marker) {
  DCHECK(has_feedback_vector());
  feedback_vector()->set_osr_tiering_state(marker);
}

DEF_GETTER(JSFunction, has_feedback_vector, bool) {
  return shared(cage_base)->is_compiled() &&
         IsFeedbackVector(raw_feedback_cell(cage_base)->value(cage_base),
                          cage_base);
}

bool JSFunction::has_closure_feedback_cell_array() const {
  return shared()->is_compiled() &&
         IsClosureFeedbackCellArray(raw_feedback_cell()->value());
}

Tagged<Context> JSFunction::context() {
  return TaggedField<Context, kContextOffset>::load(*this);
}

DEF_RELAXED_GETTER(JSFunction, context, Tagged<Context>) {
  return TaggedField<Context, kContextOffset>::Relaxed_Load(cage_base, *this);
}

bool JSFunction::has_context() const {
  return IsContext(TaggedField<HeapObject, kContextOffset>::load(*this));
}

Tagged<JSGlobalProxy> JSFunction::global_proxy() {
  return context()->global_proxy();
}

Tagged<NativeContext> JSFunction::native_context() {
  return context()->native_context();
}

RELEASE_ACQUIRE_ACCESSORS_CHECKED(JSFunction, prototype_or_initial_map,
                                  Tagged<HeapObject>,
                                  kPrototypeOrInitialMapOffset,
                                  map()->has_prototype_slot())

DEF_GETTER(JSFunction, has_prototype_slot, bool) {
  return map(cage_base)->has_prototype_slot();
}

DEF_GETTER(JSFunction, initial_map, Tagged<Map>) {
  return Map::cast(prototype_or_initial_map(cage_base, kAcquireLoad));
}

DEF_GETTER(JSFunction, has_initial_map, bool) {
  DCHECK(has_prototype_slot(cage_base));
  return IsMap(prototype_or_initial_map(cage_base, kAcquireLoad), cage_base);
}

DEF_GETTER(JSFunction, has_instance_prototype, bool) {
  DCHECK(has_prototype_slot(cage_base));
  return has_initial_map(cage_base) ||
         !IsTheHole(prototype_or_initial_map(cage_base, kAcquireLoad),
                    GetReadOnlyRoots(cage_base));
}

DEF_GETTER(JSFunction, has_prototype, bool) {
  DCHECK(has_prototype_slot(cage_base));
  return map(cage_base)->has_non_instance_prototype() ||
         has_instance_prototype(cage_base);
}

DEF_GETTER(JSFunction, has_prototype_property, bool) {
  return (has_prototype_slot(cage_base) && IsConstructor(*this, cage_base)) ||
         IsGeneratorFunction(shared(cage_base)->kind());
}

DEF_GETTER(JSFunction, PrototypeRequiresRuntimeLookup, bool) {
  return !has_prototype_property(cage_base) ||
         map(cage_base)->has_non_instance_prototype();
}

DEF_GETTER(JSFunction, instance_prototype, Tagged<HeapObject>) {
  DCHECK(has_instance_prototype(cage_base));
  if (has_initial_map(cage_base)) {
    return initial_map(cage_base)->prototype(cage_base);
  }
  // When there is no initial map and the prototype is a JSReceiver, the
  // initial map field is used for the prototype field.
  return HeapObject::cast(prototype_or_initial_map(cage_base, kAcquireLoad));
}

DEF_GETTER(JSFunction, prototype, Tagged<Object>) {
  DCHECK(has_prototype(cage_base));
  // If the function's prototype property has been set to a non-JSReceiver
  // value, that value is stored in the constructor field of the map.
  Tagged<Map> map = this->map(cage_base);
  if (map->has_non_instance_prototype()) {
    return map->GetNonInstancePrototype(cage_base);
  }
  return instance_prototype(cage_base);
}

bool JSFunction::is_compiled(IsolateForSandbox isolate) const {
  return code(isolate, kAcquireLoad)->builtin_id() != Builtin::kCompileLazy &&
         shared()->is_compiled();
}

bool JSFunction::NeedsResetDueToFlushedBytecode(IsolateForSandbox isolate) {
  // Do a raw read for shared and code fields here since this function may be
  // called on a concurrent thread. JSFunction itself should be fully
  // initialized here but the SharedFunctionInfo, Code objects may not be
  // initialized. We read using acquire loads to defend against that.
  // TODO(v8) the branches for !IsSharedFunctionInfo() and !IsCode() are
  // probably dead code by now. Investigate removing them or replacing them
  // with CHECKs.
  Tagged<Object> maybe_shared =
      ACQUIRE_READ_FIELD(*this, kSharedFunctionInfoOffset);
  if (!IsSharedFunctionInfo(maybe_shared)) return false;

  Tagged<Object> maybe_code = raw_code(isolate, kAcquireLoad);
  if (!IsCode(maybe_code)) return false;
  Tagged<Code> code = Code::cast(maybe_code);

  Tagged<SharedFunctionInfo> shared = SharedFunctionInfo::cast(maybe_shared);
  return !shared->is_compiled() && code->builtin_id() != Builtin::kCompileLazy;
}

bool JSFunction::NeedsResetDueToFlushedBaselineCode(IsolateForSandbox isolate) {
  return code(isolate)->kind() == CodeKind::BASELINE &&
         !shared()->HasBaselineCode();
}

void JSFunction::ResetIfCodeFlushed(
    IsolateForSandbox isolate,
    base::Optional<std::function<void(
        Tagged<HeapObject> object, ObjectSlot slot, Tagged<HeapObject> target)>>
        gc_notify_updated_slot) {
  const bool kBytecodeCanFlush =
      v8_flags.flush_bytecode || v8_flags.stress_snapshot;
  const bool kBaselineCodeCanFlush =
      v8_flags.flush_baseline_code || v8_flags.stress_snapshot;
  if (!kBytecodeCanFlush && !kBaselineCodeCanFlush) return;

  DCHECK_IMPLIES(NeedsResetDueToFlushedBytecode(isolate), kBytecodeCanFlush);
  if (kBytecodeCanFlush && NeedsResetDueToFlushedBytecode(isolate)) {
    // Bytecode was flushed and function is now uncompiled, reset JSFunction
    // by setting code to CompileLazy and clearing the feedback vector.
    set_code(*BUILTIN_CODE(GetIsolate(), CompileLazy));
    raw_feedback_cell()->reset_feedback_vector(gc_notify_updated_slot);
    return;
  }

  DCHECK_IMPLIES(NeedsResetDueToFlushedBaselineCode(isolate),
                 kBaselineCodeCanFlush);
  if (kBaselineCodeCanFlush && NeedsResetDueToFlushedBaselineCode(isolate)) {
    // Flush baseline code from the closure if required
    set_code(*BUILTIN_CODE(GetIsolate(), InterpreterEntryTrampoline));
  }
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_FUNCTION_INL_H_
