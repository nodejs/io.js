// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-stack-trace-iterator.h"

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/debug/debug-evaluate.h"
#include "src/debug/debug-scope-iterator.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {

std::unique_ptr<debug::StackTraceIterator> debug::StackTraceIterator::Create(
    v8::Isolate* isolate, int index) {
  return std::unique_ptr<debug::StackTraceIterator>(
      new internal::DebugStackTraceIterator(
          reinterpret_cast<internal::Isolate*>(isolate), index));
}

namespace internal {

DebugStackTraceIterator::DebugStackTraceIterator(Isolate* isolate, int index)
    : isolate_(isolate),
      iterator_(isolate, isolate->debug()->break_frame_id()),
      is_top_frame_(true),
      resumable_fn_on_stack_(false) {
  if (iterator_.done()) return;
  UpdateInlineFrameIndexAndResumableFnOnStack();
  Advance();
  for (; !Done() && index > 0; --index) Advance();
}

DebugStackTraceIterator::~DebugStackTraceIterator() = default;

bool DebugStackTraceIterator::Done() const { return iterator_.done(); }

void DebugStackTraceIterator::Advance() {
  while (true) {
    --inlined_frame_index_;
    for (; inlined_frame_index_ >= 0; --inlined_frame_index_) {
      // Omit functions from native and extension scripts.
      if (FrameSummary::Get(iterator_.frame(), inlined_frame_index_)
              .is_subject_to_debugging()) {
        break;
      }
      is_top_frame_ = false;
    }
    if (inlined_frame_index_ >= 0) {
      frame_inspector_.reset(new FrameInspector(
          iterator_.frame(), inlined_frame_index_, isolate_));
      break;
    }
    is_top_frame_ = false;
    frame_inspector_.reset();
    iterator_.Advance();
    if (iterator_.done()) break;
    UpdateInlineFrameIndexAndResumableFnOnStack();
  }
}

int DebugStackTraceIterator::GetContextId() const {
  DCHECK(!Done());
  Handle<Object> context = frame_inspector_->GetContext();
  if (IsContext(*context)) {
    Tagged<Object> value =
        Context::cast(*context)->native_context()->debug_context_id();
    if (IsSmi(value)) return Smi::ToInt(value);
  }
  return 0;
}

v8::MaybeLocal<v8::Value> DebugStackTraceIterator::GetReceiver() const {
  DCHECK(!Done());
  if (frame_inspector_->IsJavaScript() &&
      frame_inspector_->GetFunction()->shared()->kind() ==
          FunctionKind::kArrowFunction) {
    // FrameInspector is not able to get receiver for arrow function.
    // So let's try to fetch it using same logic as is used to retrieve 'this'
    // during DebugEvaluate::Local.
    Handle<JSFunction> function = frame_inspector_->GetFunction();
    Handle<Context> context(function->context(), isolate_);
    // Arrow function defined in top level function without references to
    // variables may have NativeContext as context.
    if (!context->IsFunctionContext()) return v8::MaybeLocal<v8::Value>();
    ScopeIterator scope_iterator(
        isolate_, frame_inspector_.get(),
        ScopeIterator::ReparseStrategy::kFunctionLiteral);
    // We lookup this variable in function context only when it is used in arrow
    // function otherwise V8 can optimize it out.
    if (!scope_iterator.ClosureScopeHasThisReference()) {
      return v8::MaybeLocal<v8::Value>();
    }
    DisallowGarbageCollection no_gc;
    int slot_index = context->scope_info()->ContextSlotIndex(
        ReadOnlyRoots(isolate_).this_string_handle());
    if (slot_index < 0) return v8::MaybeLocal<v8::Value>();
    Handle<Object> value = handle(context->get(slot_index), isolate_);
    if (IsTheHole(*value, isolate_)) return v8::MaybeLocal<v8::Value>();
    return Utils::ToLocal(value);
  }

  Handle<Object> value = frame_inspector_->GetReceiver();
  if (value.is_null() || (IsSmi(*value) || !IsTheHole(*value, isolate_))) {
    return Utils::ToLocal(value);
  }
  return v8::MaybeLocal<v8::Value>();
}

v8::Local<v8::Value> DebugStackTraceIterator::GetReturnValue() const {
  CHECK(!Done());
#if V8_ENABLE_WEBASSEMBLY
  if (frame_inspector_ && frame_inspector_->IsWasm()) {
    return v8::Local<v8::Value>();
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  CHECK_NOT_NULL(iterator_.frame());
  bool is_optimized = iterator_.frame()->is_optimized();
  if (is_optimized || !is_top_frame_ ||
      !isolate_->debug()->IsBreakAtReturn(iterator_.javascript_frame())) {
    return v8::Local<v8::Value>();
  }
  return Utils::ToLocal(isolate_->debug()->return_value_handle());
}

v8::Local<v8::String> DebugStackTraceIterator::GetFunctionDebugName() const {
  DCHECK(!Done());
  return Utils::ToLocal(frame_inspector_->GetFunctionName());
}

v8::Local<v8::debug::Script> DebugStackTraceIterator::GetScript() const {
  DCHECK(!Done());
  Handle<Object> value = frame_inspector_->GetScript();
  if (!IsScript(*value)) return v8::Local<v8::debug::Script>();
  return ToApiHandle<debug::Script>(Handle<Script>::cast(value));
}

debug::Location DebugStackTraceIterator::GetSourceLocation() const {
  DCHECK(!Done());
  v8::Local<v8::debug::Script> script = GetScript();
  if (script.IsEmpty()) return v8::debug::Location();
  return script->GetSourceLocation(frame_inspector_->GetSourcePosition());
}

debug::Location DebugStackTraceIterator::GetFunctionLocation() const {
  DCHECK(!Done());

  v8::Local<v8::Function> func = this->GetFunction();
  if (!func.IsEmpty()) {
    return v8::debug::Location(func->GetScriptLineNumber(),
                               func->GetScriptColumnNumber());
  }
#if V8_ENABLE_WEBASSEMBLY
  if (iterator_.frame()->is_wasm()) {
    auto frame = WasmFrame::cast(iterator_.frame());
    Handle<WasmInstanceObject> instance(frame->wasm_instance(), isolate_);
    auto offset = instance->module_object()
                      ->module()
                      ->functions[frame->function_index()]
                      .code.offset();
    return v8::debug::Location(0, offset);
  }
#endif
  return v8::debug::Location();
}

v8::Local<v8::Function> DebugStackTraceIterator::GetFunction() const {
  DCHECK(!Done());
  if (!frame_inspector_->IsJavaScript()) return v8::Local<v8::Function>();
  return Utils::ToLocal(frame_inspector_->GetFunction());
}

Handle<SharedFunctionInfo> DebugStackTraceIterator::GetSharedFunctionInfo()
    const {
  DCHECK(!Done());
  if (!frame_inspector_->IsJavaScript()) return Handle<SharedFunctionInfo>();
  return handle(frame_inspector_->GetFunction()->shared(), isolate_);
}

std::unique_ptr<v8::debug::ScopeIterator>
DebugStackTraceIterator::GetScopeIterator() const {
  DCHECK(!Done());
#if V8_ENABLE_WEBASSEMBLY
  if (iterator_.frame()->is_wasm()) {
    return GetWasmScopeIterator(WasmFrame::cast(iterator_.frame()));
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return std::make_unique<DebugScopeIterator>(isolate_, frame_inspector_.get());
}

bool DebugStackTraceIterator::CanBeRestarted() const {
  DCHECK(!Done());

  if (resumable_fn_on_stack_) return false;

  StackFrame* frame = iterator_.frame();
  // We do not support restarting WASM frames.
#if V8_ENABLE_WEBASSEMBLY
  if (frame->is_wasm()) return false;
#endif  // V8_ENABLE_WEBASSEMBLY

  // Check that no embedder API calls are between the top-most frame, and the
  // current frame. While we *could* determine whether embedder
  // frames are safe to terminate (via the CallDepthScope chain), we don't know
  // if embedder frames would cancel the termination effectively breaking
  // restart frame.
  if (isolate_->thread_local_top()->last_api_entry_ < frame->fp()) {
    return false;
  }

  return true;
}

void DebugStackTraceIterator::UpdateInlineFrameIndexAndResumableFnOnStack() {
  CHECK(!iterator_.done());

  std::vector<FrameSummary> frames;
  iterator_.frame()->Summarize(&frames);
  inlined_frame_index_ = static_cast<int>(frames.size());

  if (resumable_fn_on_stack_) return;

  StackFrame* frame = iterator_.frame();
  if (!frame->is_java_script()) return;

  std::vector<Handle<SharedFunctionInfo>> shareds;
  JavaScriptFrame::cast(frame)->GetFunctions(&shareds);
  for (auto& shared : shareds) {
    if (IsResumableFunction(shared->kind())) {
      resumable_fn_on_stack_ = true;
      return;
    }
  }
}

v8::MaybeLocal<v8::Value> DebugStackTraceIterator::Evaluate(
    v8::Local<v8::String> source, bool throw_on_side_effect) {
  DCHECK(!Done());
  Handle<Object> value;

  i::SafeForInterruptsScope safe_for_interrupt_scope(isolate_);
  if (!DebugEvaluate::Local(isolate_, iterator_.frame()->id(),
                            inlined_frame_index_, Utils::OpenHandle(*source),
                            throw_on_side_effect)
           .ToHandle(&value)) {
    return v8::MaybeLocal<v8::Value>();
  }
  return Utils::ToLocal(value);
}

void DebugStackTraceIterator::PrepareRestart() {
  CHECK(!Done());
  CHECK(CanBeRestarted());

  isolate_->debug()->PrepareRestartFrame(iterator_.javascript_frame(),
                                         inlined_frame_index_);
}

}  // namespace internal
}  // namespace v8
