// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ISOLATE_INL_H_
#define V8_EXECUTION_ISOLATE_INL_H_

#include "src/execution/isolate.h"
#include "src/objects/contexts-inl.h"
#include "src/objects/js-function.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/property-cell.h"
#include "src/objects/regexp-match-info.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/source-text-module-inl.h"

#ifdef DEBUG
#include "src/common/ptr-compr-inl.h"
#include "src/runtime/runtime-utils.h"
#endif

namespace v8 {
namespace internal {

// static
V8_INLINE Isolate::PerIsolateThreadData*
Isolate::CurrentPerIsolateThreadData() {
  return g_current_per_isolate_thread_data_;
}

// static
V8_INLINE Isolate* Isolate::TryGetCurrent() { return g_current_isolate_; }

// static
V8_INLINE Isolate* Isolate::Current() {
  Isolate* isolate = TryGetCurrent();
  DCHECK_NOT_NULL(isolate);
  return isolate;
}

bool Isolate::IsCurrent() const { return this == TryGetCurrent(); }

void Isolate::set_context(Tagged<Context> context) {
  DCHECK(context.is_null() || IsContext(context));
  thread_local_top()->context_ = context;
}

Handle<NativeContext> Isolate::native_context() {
  DCHECK(!context().is_null());
  return handle(context()->native_context(), this);
}

Tagged<NativeContext> Isolate::raw_native_context() {
  DCHECK(!context().is_null());
  return context()->native_context();
}

void Isolate::set_topmost_script_having_context(Tagged<Context> context) {
  DCHECK(context.is_null() || IsContext(context));
  thread_local_top()->topmost_script_having_context_ = context;
}

void Isolate::clear_topmost_script_having_context() {
  static_assert(Context::kNoContext == 0);
  thread_local_top()->topmost_script_having_context_ = Context();
}

Handle<NativeContext> Isolate::GetIncumbentContext() {
  Tagged<Context> maybe_topmost_script_having_context =
      topmost_script_having_context();
  if (V8_LIKELY(!maybe_topmost_script_having_context.is_null())) {
    // The topmost script-having context value is guaranteed to be valid only
    // inside the Api callback however direct calls of Api callbacks from
    // builtins or optimized code do not change the current VM state, so we
    // allow JS VM state too.
    DCHECK(current_vm_state() == EXTERNAL ||  // called from C++ code
           current_vm_state() == JS);         // called from JS code directly

    Tagged<NativeContext> incumbent_context =
        maybe_topmost_script_having_context->native_context();
    DCHECK_EQ(incumbent_context, *GetIncumbentContextSlow());
    return handle(incumbent_context, this);
  }
  return GetIncumbentContextSlow();
}

void Isolate::set_pending_message(Tagged<Object> message_obj) {
  DCHECK(IsTheHole(message_obj, this) || IsJSMessageObject(message_obj));
  thread_local_top()->pending_message_ = message_obj;
}

Tagged<Object> Isolate::pending_message() {
  return thread_local_top()->pending_message_;
}

void Isolate::clear_pending_message() {
  set_pending_message(ReadOnlyRoots(this).the_hole_value());
}

bool Isolate::has_pending_message() {
  return !IsTheHole(pending_message(), this);
}

Tagged<Object> Isolate::exception() {
  CHECK(has_exception());
  DCHECK(!IsException(thread_local_top()->exception_, this));
  return thread_local_top()->exception_;
}

void Isolate::set_exception(Tagged<Object> exception_obj) {
  DCHECK(!IsException(exception_obj, this));
  thread_local_top()->exception_ = exception_obj;
}

void Isolate::clear_internal_exception() {
  DCHECK(!IsException(thread_local_top()->exception_, this));
  thread_local_top()->exception_ = ReadOnlyRoots(this).the_hole_value();
}

void Isolate::clear_exception() {
  clear_internal_exception();
  if (try_catch_handler()) try_catch_handler()->Reset();
}

bool Isolate::has_exception() {
  ThreadLocalTop* top = thread_local_top();
  DCHECK(!IsException(top->exception_, this));
  return !IsTheHole(top->exception_, this);
}

bool Isolate::is_execution_terminating() {
  return thread_local_top()->exception_ ==
         i::ReadOnlyRoots(this).termination_exception();
}

#ifdef DEBUG
Tagged<Object> Isolate::VerifyBuiltinsResult(Tagged<Object> result) {
  if (is_execution_terminating() && !v8_flags.strict_termination_checks) {
    // We may be missing places where termination checks are handled properly.
    // If that's the case, it's likely that we'll have one sitting around when
    // we return from a builtin. If we're not looking to find such bugs
    // (strict_termination_checks is false), simply return the exception marker.
    return ReadOnlyRoots(this).exception();
  }

  // Here we use full pointer comparison as the result might be an object
  // outside of the main pointer compression heap (e.g. in trusted space).
  DCHECK_EQ(has_exception(),
            result.SafeEquals(ReadOnlyRoots(this).exception()));

#ifdef V8_COMPRESS_POINTERS
  // Check that the returned pointer is actually part of the current isolate (or
  // the shared isolate), because that's the assumption in generated code (which
  // might call this builtin).
  Isolate* isolate;
  if (!IsSmi(result) &&
      GetIsolateFromHeapObject(HeapObject::cast(result), &isolate)) {
    DCHECK(isolate == this || isolate == shared_space_isolate());
  }
#endif

  return result;
}

ObjectPair Isolate::VerifyBuiltinsResult(ObjectPair pair) {
#ifdef V8_HOST_ARCH_64_BIT
  Tagged<Object> x(pair.x), y(pair.y);

  // Here we use full pointer comparison as the result might be an object
  // outside of the main pointer compression heap (e.g. in trusted space).
  DCHECK_EQ(has_exception(), x.SafeEquals(ReadOnlyRoots(this).exception()));

#ifdef V8_COMPRESS_POINTERS
  // Check that the returned pointer is actually part of the current isolate (or
  // the shared isolate), because that's the assumption in generated code (which
  // might call this builtin).
  Isolate* isolate;
  if (!IsSmi(x) && GetIsolateFromHeapObject(HeapObject::cast(x), &isolate)) {
    DCHECK(isolate == this || isolate == shared_space_isolate());
  }
  if (!IsSmi(y) && GetIsolateFromHeapObject(HeapObject::cast(y), &isolate)) {
    DCHECK(isolate == this || isolate == shared_space_isolate());
  }
#endif
#endif  // V8_HOST_ARCH_64_BIT
  return pair;
}
#endif  // DEBUG

bool Isolate::is_catchable_by_javascript(Tagged<Object> exception) {
  return exception != ReadOnlyRoots(heap()).termination_exception();
}

bool Isolate::InFastCCall() const {
  return isolate_data()->fast_c_call_caller_fp() != kNullAddress;
}

bool Isolate::is_catchable_by_wasm(Tagged<Object> exception) {
  if (!is_catchable_by_javascript(exception)) return false;
  if (!IsJSObject(exception)) return true;
  return !LookupIterator::HasInternalMarkerProperty(
      this, JSReceiver::cast(exception), factory()->wasm_uncatchable_symbol());
}

void Isolate::FireBeforeCallEnteredCallback() {
  for (auto& callback : before_call_entered_callbacks_) {
    callback(reinterpret_cast<v8::Isolate*>(this));
  }
}

Handle<JSGlobalObject> Isolate::global_object() {
  return handle(context()->global_object(), this);
}

Handle<JSGlobalProxy> Isolate::global_proxy() {
  return handle(context()->global_proxy(), this);
}

Isolate::ExceptionScope::ExceptionScope(Isolate* isolate)
    : isolate_(isolate), exception_(isolate_->exception(), isolate_) {
  isolate_->clear_internal_exception();
}

Isolate::ExceptionScope::~ExceptionScope() {
  isolate_->set_exception(*exception_);
}

bool Isolate::IsAnyInitialArrayPrototype(Tagged<JSArray> array) {
  DisallowGarbageCollection no_gc;
  return IsInAnyContext(array, Context::INITIAL_ARRAY_PROTOTYPE_INDEX);
}

void Isolate::DidFinishModuleAsyncEvaluation(unsigned ordinal) {
  // To address overflow, the ordinal is reset when the async module with the
  // largest vended ordinal finishes evaluating. Modules are evaluated in
  // ascending order of their async_evaluating_ordinal.
  //
  // While the specification imposes a global total ordering, the intention is
  // that for each async module, all its parents are totally ordered by when
  // they first had their [[AsyncEvaluating]] bit set.
  //
  // The module with largest vended ordinal finishes evaluating implies that the
  // async dependency as well as all other modules in that module's graph
  // depending on async dependencies are finished evaluating.
  //
  // If the async dependency participates in other module graphs (e.g. via
  // dynamic import, or other <script type=module> tags), those module graphs
  // must have been evaluated either before or after the async dependency is
  // settled, as the concrete Evaluate() method on cyclic module records is
  // neither reentrant nor performs microtask checkpoints during its
  // evaluation. If before, then all modules that depend on the async
  // dependencies were given an ordinal that ensure they are relatively ordered,
  // before the global ordinal was reset. If after, then the async evaluating
  // ordering does not apply, as the dependency is no longer asynchronous.
  //
  // https://tc39.es/ecma262/#sec-moduleevaluation
  if (ordinal + 1 == next_module_async_evaluating_ordinal_) {
    next_module_async_evaluating_ordinal_ =
        SourceTextModule::kFirstAsyncEvaluatingOrdinal;
  }
}

#define NATIVE_CONTEXT_FIELD_ACCESSOR(index, type, name)     \
  Handle<type> Isolate::name() {                             \
    return Handle<type>(raw_native_context()->name(), this); \
  }                                                          \
  bool Isolate::is_##name(Tagged<type> value) {              \
    return raw_native_context()->is_##name(value);           \
  }
NATIVE_CONTEXT_FIELDS(NATIVE_CONTEXT_FIELD_ACCESSOR)
#undef NATIVE_CONTEXT_FIELD_ACCESSOR

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ISOLATE_INL_H_
