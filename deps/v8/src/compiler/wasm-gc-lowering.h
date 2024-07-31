// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_GC_LOWERING_H_
#define V8_COMPILER_WASM_GC_LOWERING_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/compiler/wasm-graph-assembler.h"

namespace v8 {
namespace internal {
namespace compiler {

class MachineGraph;
class SourcePositionTable;
class WasmGraphAssembler;

class WasmGCLowering final : public AdvancedReducer {
 public:
  WasmGCLowering(Editor* editor, MachineGraph* mcgraph,
                 const wasm::WasmModule* module, bool disable_trap_handler,
                 SourcePositionTable* source_position_table);

  const char* reducer_name() const override { return "WasmGCLowering"; }

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceWasmTypeCheck(Node* node);
  Reduction ReduceWasmTypeCheckAbstract(Node* node);
  Reduction ReduceWasmTypeCast(Node* node);
  Reduction ReduceWasmTypeCastAbstract(Node* node);
  Reduction ReduceAssertNotNull(Node* node);
  Reduction ReduceNull(Node* node);
  Reduction ReduceIsNull(Node* node);
  Reduction ReduceIsNotNull(Node* node);
  Reduction ReduceRttCanon(Node* node);
  Reduction ReduceTypeGuard(Node* node);
  Reduction ReduceWasmAnyConvertExtern(Node* node);
  Reduction ReduceWasmExternConvertAny(Node* node);
  Reduction ReduceWasmStructGet(Node* node);
  Reduction ReduceWasmStructSet(Node* node);
  Reduction ReduceWasmArrayGet(Node* node);
  Reduction ReduceWasmArraySet(Node* node);
  Reduction ReduceWasmArrayLength(Node* node);
  Reduction ReduceWasmArrayInitializeLength(Node* node);
  Reduction ReduceStringAsWtf16(Node* node);
  Reduction ReduceStringPrepareForGetCodeunit(Node* node);
  Node* Null(wasm::ValueType type);
  Node* IsNull(Node* object, wasm::ValueType type);
  Node* BuildLoadExternalPointerFromObject(Node* object, int offset,
                                           ExternalPointerTag tag);
  void UpdateSourcePosition(Node* new_node, Node* old_node);
  NullCheckStrategy null_check_strategy_;
  WasmGraphAssembler gasm_;
  const wasm::WasmModule* module_;
  Node* dead_;
  const MachineGraph* mcgraph_;
  SourcePositionTable* source_position_table_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_GC_LOWERING_H_
