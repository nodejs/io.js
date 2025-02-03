// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_INLINING_H_
#define V8_COMPILER_JS_INLINING_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-origin-table.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder.h"
#include "src/wasm/wasm-code-manager.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

class BytecodeOffset;
class OptimizedCompilationInfo;

namespace compiler {

class SourcePositionTable;
class JSWasmCallParameters;
using JsWasmCallsSidetable = ZoneMap<NodeId, const JSWasmCallParameters*>;

// The JSInliner provides the core graph inlining machinery. Note that this
// class only deals with the mechanics of how to inline one graph into another,
// heuristics that decide what and how much to inline are beyond its scope.
class JSInliner final : public AdvancedReducer {
 public:
  JSInliner(Editor* editor, Zone* local_zone, OptimizedCompilationInfo* info,
            JSGraph* jsgraph, JSHeapBroker* broker,
            SourcePositionTable* source_positions,
            NodeOriginTable* node_origins, const wasm::WasmModule* wasm_module,
            JsWasmCallsSidetable* js_wasm_calls_sidetable,
            bool inline_wasm_fct_if_supported)
      : AdvancedReducer(editor),
        local_zone_(local_zone),
        info_(info),
        jsgraph_(jsgraph),
        broker_(broker),
        source_positions_(source_positions),
        node_origins_(node_origins),
        wasm_module_(wasm_module),
        js_wasm_calls_sidetable_(js_wasm_calls_sidetable),
        inline_wasm_fct_if_supported_(inline_wasm_fct_if_supported) {
    // In case WebAssembly is disabled.
    USE(wasm_module_);
    USE(inline_wasm_fct_if_supported_);
    USE(js_wasm_calls_sidetable_);
    DCHECK_IMPLIES(inline_wasm_fct_if_supported_, wasm_module_ != nullptr);
  }

  const char* reducer_name() const override { return "JSInliner"; }

  Reduction Reduce(Node* node) final { UNREACHABLE(); }

  // Can be used by inlining heuristics or by testing code directly, without
  // using the above generic reducer interface of the inlining machinery.
  Reduction ReduceJSCall(Node* node);

#if V8_ENABLE_WEBASSEMBLY
  Reduction ReduceJSWasmCall(Node* node);
  void InlineWasmFunction(Node* call, Node* inlinee_start, Node* inlinee_end,
                          Node* frame_state,
                          SharedFunctionInfoRef shared_fct_info,
                          int argument_count, Node* context);
  static std::string WasmFunctionNameForTrace(wasm::NativeModule* native_module,
                                              int fct_index) {
    wasm::StringBuilder builder;
    native_module->GetNamesProvider()->PrintFunctionName(builder, fct_index);
    if (builder.length() == 0) return "<no name>";
    return {builder.start(), builder.length()};
  }
#endif  // V8_ENABLE_WEBASSEMBLY

 private:
  Zone* zone() const { return local_zone_; }
  CommonOperatorBuilder* common() const;
  JSOperatorBuilder* javascript() const;
  SimplifiedOperatorBuilder* simplified() const;
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  // TODO(neis): Make heap broker a component of JSGraph?
  JSHeapBroker* broker() const { return broker_; }
  Isolate* isolate() const { return jsgraph_->isolate(); }

  Zone* const local_zone_;
  OptimizedCompilationInfo* info_;
  JSGraph* const jsgraph_;
  JSHeapBroker* const broker_;
  SourcePositionTable* const source_positions_;
  NodeOriginTable* const node_origins_;
  const wasm::WasmModule* wasm_module_;
  JsWasmCallsSidetable* js_wasm_calls_sidetable_;

  // Inline not only the wasm wrapper but also the wasm function itself if
  // inlining into JavaScript is supported and the function is small enough.
  bool inline_wasm_fct_if_supported_;

  OptionalSharedFunctionInfoRef DetermineCallTarget(Node* node);
  FeedbackCellRef DetermineCallContext(Node* node, Node** context_out);

  // TODO(victorgomes): This function is used to create 3 *quite* different
  // artificial frame states, we should perhaps split it into three different
  // functions.
  FrameState CreateArtificialFrameState(
      Node* node, FrameState outer_frame_state, int parameter_count,
      FrameStateType frame_state_type, SharedFunctionInfoRef shared,
      Node* context = nullptr, Node* callee = nullptr);

  Reduction InlineCall(Node* call, Node* new_target, Node* context,
                       Node* frame_state, StartNode start, Node* end,
                       Node* exception_target,
                       const NodeVector& uncaught_subcalls, int argument_count);

#if V8_ENABLE_WEBASSEMBLY
  struct WasmInlineResult {
    bool can_inline_body = false;
    Node* body_start = nullptr;
    Node* body_end = nullptr;
  };
  WasmInlineResult TryWasmInlining(const JSWasmCallNode& call_node);
  Reduction InlineJSWasmCall(Node* call, Node* new_target, Node* context,
                             Node* frame_state, StartNode start, Node* end,
                             Node* exception_target,
                             const NodeVector& uncaught_subcalls);
#endif  // V8_ENABLE_WEBASSEMBLY
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_INLINING_H_
