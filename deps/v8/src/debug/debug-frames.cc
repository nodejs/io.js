// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-frames.h"

#include "src/builtins/accessors.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frames-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

FrameInspector::FrameInspector(CommonFrame* frame, int inlined_frame_index,
                               Isolate* isolate)
    : frame_(frame),
      inlined_frame_index_(inlined_frame_index),
      isolate_(isolate) {
  // Extract the relevant information from the frame summary and discard it.
  FrameSummary summary = FrameSummary::Get(frame, inlined_frame_index);
  summary.EnsureSourcePositionsAvailable();

  is_constructor_ = summary.is_constructor();
  source_position_ = summary.SourcePosition();
  script_ = Handle<Script>::cast(summary.script());
  receiver_ = summary.receiver();

  if (summary.IsJavaScript()) {
    function_ = summary.AsJavaScript().function();
  }

#if V8_ENABLE_WEBASSEMBLY
  JavaScriptFrame* js_frame =
      frame->is_java_script() ? javascript_frame() : nullptr;
  DCHECK(js_frame || frame->is_wasm());
#else
  JavaScriptFrame* js_frame = javascript_frame();
#endif  // V8_ENABLE_WEBASSEMBLY
  is_optimized_ = frame_->is_optimized();

  // Calculate the deoptimized frame.
  if (is_optimized_) {
    DCHECK_NOT_NULL(js_frame);
    deoptimized_frame_.reset(Deoptimizer::DebuggerInspectableFrame(
        js_frame, inlined_frame_index, isolate));
  }
}

// Destructor needs to be defined in the .cc file, because it instantiates
// std::unique_ptr destructors but the types are not known in the header.
FrameInspector::~FrameInspector() = default;

JavaScriptFrame* FrameInspector::javascript_frame() {
  return JavaScriptFrame::cast(frame_);
}

Handle<Object> FrameInspector::GetParameter(int index) {
  if (is_optimized_) return deoptimized_frame_->GetParameter(index);
  DCHECK(IsJavaScript());
  return handle(javascript_frame()->GetParameter(index), isolate_);
}

Handle<Object> FrameInspector::GetExpression(int index) {
  return is_optimized_ ? deoptimized_frame_->GetExpression(index)
                       : handle(frame_->GetExpression(index), isolate_);
}

Handle<Object> FrameInspector::GetContext() {
  return deoptimized_frame_ ? deoptimized_frame_->GetContext()
                            : handle(frame_->context(), isolate_);
}

Handle<String> FrameInspector::GetFunctionName() {
#if V8_ENABLE_WEBASSEMBLY
  if (IsWasm()) {
    auto wasm_frame = WasmFrame::cast(frame_);
    auto wasm_instance = handle(wasm_frame->wasm_instance(), isolate_);
    return GetWasmFunctionDebugName(isolate_, wasm_instance,
                                    wasm_frame->function_index());
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return JSFunction::GetDebugName(function_);
}

#if V8_ENABLE_WEBASSEMBLY
bool FrameInspector::IsWasm() { return frame_->is_wasm(); }
#endif  // V8_ENABLE_WEBASSEMBLY

bool FrameInspector::IsJavaScript() { return frame_->is_java_script(); }

bool FrameInspector::ParameterIsShadowedByContextLocal(
    Handle<ScopeInfo> info, Handle<String> parameter_name) {
  return info->ContextSlotIndex(parameter_name) != -1;
}

RedirectActiveFunctions::RedirectActiveFunctions(
    Isolate* isolate, Tagged<SharedFunctionInfo> shared, Mode mode)
    : shared_(shared), mode_(mode) {
  DCHECK(shared->HasBytecodeArray());
  DCHECK_IMPLIES(mode == Mode::kUseDebugBytecode,
                 shared->HasDebugInfo(isolate));
}

void RedirectActiveFunctions::VisitThread(Isolate* isolate,
                                          ThreadLocalTop* top) {
  for (JavaScriptStackFrameIterator it(isolate, top); !it.done();
       it.Advance()) {
    JavaScriptFrame* frame = it.frame();
    Tagged<JSFunction> function = frame->function();
    if (!frame->is_interpreted()) continue;
    if (function->shared() != shared_) continue;
    InterpretedFrame* interpreted_frame =
        reinterpret_cast<InterpretedFrame*>(frame);
    Tagged<BytecodeArray> bytecode =
        mode_ == Mode::kUseDebugBytecode
            ? shared_->GetDebugInfo(isolate)->DebugBytecodeArray(isolate)
            : shared_->GetBytecodeArray(isolate);
    interpreted_frame->PatchBytecodeArray(bytecode);
  }
}

}  // namespace internal
}  // namespace v8
