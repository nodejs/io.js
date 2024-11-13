// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_EVALUATE_H_
#define V8_DEBUG_DEBUG_EVALUATE_H_

#include <vector>

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/debug/debug-frames.h"
#include "src/debug/debug-interface.h"
#include "src/debug/debug-scopes.h"
#include "src/execution/frames.h"
#include "src/objects/objects.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string-set.h"

namespace v8 {
namespace internal {

class FrameInspector;

class DebugEvaluate : public AllStatic {
 public:
  static V8_EXPORT_PRIVATE MaybeHandle<Object> Global(
      Isolate* isolate, Handle<String> source, debug::EvaluateGlobalMode mode,
      REPLMode repl_mode = REPLMode::kNo);

  // Evaluate a piece of JavaScript in the context of a stack frame for
  // debugging.  Things that need special attention are:
  // - Parameters and stack-allocated locals need to be materialized.  Altered
  //   values need to be written back to the stack afterwards.
  // - The arguments object needs to materialized.
  // The stack frame can be either a JavaScript stack frame or a Wasm
  // stack frame. In the latter case, a special Debug Proxy API is
  // provided to peek into the Wasm state.
  static V8_EXPORT_PRIVATE MaybeHandle<Object> Local(Isolate* isolate,
                                                     StackFrameId frame_id,
                                                     int inlined_jsframe_index,
                                                     Handle<String> source,
                                                     bool throw_on_side_effect);

  // This is used for break-at-entry for builtins and API functions.
  // Evaluate a piece of JavaScript in the native context, but with the
  // materialized arguments object and receiver of the current call.
  static MaybeHandle<Object> WithTopmostArguments(Isolate* isolate,
                                                  Handle<String> source);

  static DebugInfo::SideEffectState FunctionGetSideEffectState(
      Isolate* isolate, DirectHandle<SharedFunctionInfo> info);
  static void ApplySideEffectChecks(Handle<BytecodeArray> bytecode_array);
  static bool IsSideEffectFreeIntrinsic(Runtime::FunctionId id);

#ifdef DEBUG
  static void VerifyTransitiveBuiltins(Isolate* isolate);
#endif  // DEBUG

 private:
  // This class builds a context chain for evaluation of expressions
  // in debugger.
  // The scope chain leading up to a breakpoint where evaluation occurs
  // looks like:
  // - [a mix of with, catch and block scopes]
  //    - [function stack + context]
  //      - [outer context]
  // The builder materializes all stack variables into properties of objects;
  // the expression is then evaluated as if it is inside a series of 'with'
  // statements using those objects. To this end, the builder builds a new
  // context chain, based on a scope chain:
  //   - every With and Catch scope begets a cloned context
  //   - Block scope begets one or two contexts:
  //       - if a block has context-allocated varaibles, its context is cloned
  //       - stack locals are materizalized as a With context
  //   - Local scope begets a With context for materizalized locals, chained to
  //     original function context. Original function context is the end of
  //     the chain.
  class ContextBuilder {
   public:
    ContextBuilder(Isolate* isolate, JavaScriptFrame* frame,
                   int inlined_jsframe_index);

    void UpdateValues();

    Handle<Context> evaluation_context() const { return evaluation_context_; }
    Handle<SharedFunctionInfo> outer_info() const;

   private:
    struct ContextChainElement {
      Handle<Context> wrapped_context;
      Handle<JSObject> materialized_object;
      Handle<StringSet> blocklist;
    };

    Handle<Context> evaluation_context_;
    std::vector<ContextChainElement> context_chain_;
    Isolate* isolate_;
    FrameInspector frame_inspector_;
    ScopeIterator scope_iterator_;
  };

  static MaybeHandle<Object> Evaluate(Isolate* isolate,
                                      Handle<SharedFunctionInfo> outer_info,
                                      Handle<Context> context,
                                      Handle<Object> receiver,
                                      Handle<String> source,
                                      bool throw_on_side_effect);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_EVALUATE_H_
