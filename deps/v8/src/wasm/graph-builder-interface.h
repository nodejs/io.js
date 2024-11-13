// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_GRAPH_BUILDER_INTERFACE_H_
#define V8_WASM_GRAPH_BUILDER_INTERFACE_H_

#include "src/wasm/decoder.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {

class AccountingAllocator;

namespace compiler {  // external declarations from compiler.
class Node;
class NodeOriginTable;
class WasmGraphBuilder;
struct WasmLoopInfo;
}  // namespace compiler

namespace wasm {

class AssumptionsJournal;
struct FunctionBody;
class WasmDetectedFeatures;
class WasmEnabledFeatures;
struct WasmModule;

enum InlinedStatus {
  // Inlined function whose call node has IfSuccess/IfException outputs.
  kInlinedHandledCall,
  // Inlined function whose call node does not have IfSuccess/IfException
  // outputs.
  kInlinedNonHandledCall,
  // Not an inlined call.
  kRegularFunction
};

struct DanglingExceptions {
  std::vector<compiler::Node*> exception_values;
  std::vector<compiler::Node*> effects;
  std::vector<compiler::Node*> controls;

  void Add(compiler::Node* exception_value, compiler::Node* effect,
           compiler::Node* control) {
    exception_values.emplace_back(exception_value);
    effects.emplace_back(effect);
    controls.emplace_back(control);
  }

  size_t Size() const { return exception_values.size(); }
};

V8_EXPORT_PRIVATE void BuildTFGraph(
    AccountingAllocator* allocator, WasmEnabledFeatures enabled,
    const WasmModule* module, compiler::WasmGraphBuilder* builder,
    WasmDetectedFeatures* detected, const FunctionBody& body,
    std::vector<compiler::WasmLoopInfo>* loop_infos,
    DanglingExceptions* dangling_exceptions,
    compiler::NodeOriginTable* node_origins, int func_index,
    AssumptionsJournal* assumptions, InlinedStatus inlined_status);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_GRAPH_BUILDER_INTERFACE_H_
