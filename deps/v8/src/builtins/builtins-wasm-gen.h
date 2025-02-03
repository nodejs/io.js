// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_WASM_GEN_H_
#define V8_BUILTINS_BUILTINS_WASM_GEN_H_

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

class WasmBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit WasmBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  TNode<WasmTrustedInstanceData> LoadInstanceDataFromFrame();

  TNode<WasmTrustedInstanceData> LoadTrustedDataFromInstance(
      TNode<WasmInstanceObject>);

  TNode<NativeContext> LoadContextFromWasmOrJsFrame();

  TNode<NativeContext> LoadContextFromInstanceData(
      TNode<WasmTrustedInstanceData>);

  TNode<WasmTrustedInstanceData> LoadSharedPartFromInstanceData(
      TNode<WasmTrustedInstanceData>);

  TNode<FixedArray> LoadTablesFromInstanceData(TNode<WasmTrustedInstanceData>);

  TNode<FixedArray> LoadFuncRefsFromInstanceData(
      TNode<WasmTrustedInstanceData>);

  TNode<FixedArray> LoadManagedObjectMapsFromInstanceData(
      TNode<WasmTrustedInstanceData>);

  TNode<Float64T> StringToFloat64(TNode<String>);

  TNode<Smi> SignatureCheckFail(TNode<WasmInternalFunction>, TNode<UintPtrT>);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_WASM_GEN_H_
