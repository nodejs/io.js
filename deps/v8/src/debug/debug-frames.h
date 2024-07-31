// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_FRAMES_H_
#define V8_DEBUG_DEBUG_FRAMES_H_

#include <memory>

#include "src/deoptimizer/deoptimized-frame-info.h"
#include "src/execution/isolate.h"
#include "src/execution/v8threads.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class JavaScriptFrame;
class CommonFrame;
class WasmFrame;

class V8_EXPORT_PRIVATE FrameInspector {
 public:
  FrameInspector(CommonFrame* frame, int inlined_frame_index, Isolate* isolate);
  FrameInspector(const FrameInspector&) = delete;
  FrameInspector& operator=(const FrameInspector&) = delete;

  ~FrameInspector();

  Handle<JSFunction> GetFunction() const { return function_; }
  Handle<Script> GetScript() { return script_; }
  Handle<Object> GetParameter(int index);
  Handle<Object> GetExpression(int index);
  int GetSourcePosition() { return source_position_; }
  bool IsConstructor() { return is_constructor_; }
  Handle<Object> GetContext();
  Handle<Object> GetReceiver() { return receiver_; }

  Handle<String> GetFunctionName();

#if V8_ENABLE_WEBASSEMBLY
  bool IsWasm();
#endif  // V8_ENABLE_WEBASSEMBLY
  bool IsJavaScript();

  JavaScriptFrame* javascript_frame();

  int inlined_frame_index() const { return inlined_frame_index_; }

 private:
  bool ParameterIsShadowedByContextLocal(Handle<ScopeInfo> info,
                                         Handle<String> parameter_name);

  CommonFrame* frame_;
  int inlined_frame_index_;
  std::unique_ptr<DeoptimizedFrameInfo> deoptimized_frame_;
  Isolate* isolate_;
  Handle<Script> script_;
  Handle<Object> receiver_;
  Handle<JSFunction> function_;
  int source_position_ = -1;
  bool is_optimized_ = false;
  bool is_constructor_ = false;
};

class RedirectActiveFunctions : public ThreadVisitor {
 public:
  enum class Mode {
    kUseOriginalBytecode,
    kUseDebugBytecode,
  };

  RedirectActiveFunctions(Isolate* isolate, Tagged<SharedFunctionInfo> shared,
                          Mode mode);

  void VisitThread(Isolate* isolate, ThreadLocalTop* top) override;

 private:
  Tagged<SharedFunctionInfo> shared_;
  Mode mode_;
  DISALLOW_GARBAGE_COLLECTION(no_gc_)
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_FRAMES_H_
