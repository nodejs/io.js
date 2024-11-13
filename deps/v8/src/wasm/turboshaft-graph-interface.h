// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_TURBOSHAFT_GRAPH_INTERFACE_H_
#define V8_WASM_TURBOSHAFT_GRAPH_INTERFACE_H_

#include "src/base/macros.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/dataview-lowering-reducer.h"
#include "src/compiler/turboshaft/select-lowering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/objects/code-kind.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/value-type.h"
#include "src/zone/zone-containers.h"

namespace v8::internal {
class AccountingAllocator;
struct WasmInliningPosition;

namespace compiler {
class NodeOriginTable;
namespace turboshaft {
class Graph;
class PipelineData;
}
}  // namespace compiler

namespace wasm {
class AssumptionsJournal;
struct FunctionBody;
class WasmDetectedFeatures;
struct WasmModule;
class WireBytesStorage;
class TurboshaftGraphBuildingInterface;
struct CompilationEnv;

V8_EXPORT_PRIVATE bool BuildTSGraph(
    compiler::turboshaft::PipelineData* data, AccountingAllocator* allocator,
    CompilationEnv* env, WasmDetectedFeatures* detected,
    compiler::turboshaft::Graph& graph, const FunctionBody& func_body,
    const WireBytesStorage* wire_bytes, AssumptionsJournal* assumptions,
    ZoneVector<WasmInliningPosition>* inlining_positions, int func_index);

void BuildWasmWrapper(compiler::turboshaft::PipelineData* data,
                      AccountingAllocator* allocator,
                      compiler::turboshaft::Graph& graph,
                      const wasm::FunctionSig* sig, WrapperCompilationInfo,
                      const WasmModule* module);

// Base class for the decoder graph builder interface and for the wrapper
// builder.
class V8_EXPORT_PRIVATE WasmGraphBuilderBase {
 public:
  using Assembler = compiler::turboshaft::TSAssembler<
      compiler::turboshaft::SelectLoweringReducer,
      compiler::turboshaft::DataViewLoweringReducer,
      compiler::turboshaft::VariableReducer>;
  template <typename T>
  using ScopedVar = compiler::turboshaft::ScopedVariable<T, Assembler>;
  template <typename T, typename A>
  friend class compiler::turboshaft::ScopedVariable;

 public:
  using OpIndex = compiler::turboshaft::OpIndex;
  void BuildModifyThreadInWasmFlagHelper(Zone* zone,
                                         OpIndex thread_in_wasm_flag_address,
                                         bool new_value);
  void BuildModifyThreadInWasmFlag(Zone* zone, bool new_value);

 protected:
  WasmGraphBuilderBase(Zone* zone, Assembler& assembler)
      : zone_(zone), asm_(assembler) {}

  using RegisterRepresentation = compiler::turboshaft::RegisterRepresentation;
  using TSCallDescriptor = compiler::turboshaft::TSCallDescriptor;
  using Word32 = compiler::turboshaft::Word32;
  using Word64 = compiler::turboshaft::Word64;
  using WordPtr = compiler::turboshaft::WordPtr;
  using Word = compiler::turboshaft::Word;
  using Any = compiler::turboshaft::Any;

  template <typename T>
  using V = compiler::turboshaft::V<T>;
  template <typename T>
  using ConstOrV = compiler::turboshaft::ConstOrV<T>;

  using ValidationTag = Decoder::FullValidationTag;
  using FullDecoder =
      WasmFullDecoder<ValidationTag, TurboshaftGraphBuildingInterface>;

  OpIndex CallRuntime(Zone* zone, Runtime::FunctionId f,
                      std::initializer_list<const OpIndex> args,
                      V<Context> context);

  OpIndex GetBuiltinPointerTarget(Builtin builtin);
  V<WordPtr> GetTargetForBuiltinCall(Builtin builtin, StubCallMode stub_mode);
  V<BigInt> BuildChangeInt64ToBigInt(V<Word64> input, StubCallMode stub_mode);

  std::pair<V<WordPtr>, V<HeapObject>>
  BuildImportedFunctionTargetAndImplicitArg(
      ConstOrV<Word32> func_index,
      V<WasmTrustedInstanceData> trusted_instance_data);

  std::pair<V<WordPtr>, V<ExposedTrustedObject>>
  BuildFunctionTargetAndImplicitArg(V<WasmInternalFunction> internal_function,
                                    uint64_t expected_sig_hash);

  RegisterRepresentation RepresentationFor(ValueType type);
  V<WasmTrustedInstanceData> LoadTrustedDataFromInstanceObject(
      V<HeapObject> instance_object);

  OpIndex CallC(const MachineSignature* sig, ExternalReference ref,
                std::initializer_list<OpIndex> args);
  OpIndex CallC(const MachineSignature* sig, OpIndex function,
                std::initializer_list<OpIndex> args);
  OpIndex CallC(const MachineSignature* sig, ExternalReference ref,
                OpIndex arg) {
    return CallC(sig, ref, {arg});
  }

  Assembler& Asm() { return asm_; }

  Zone* zone_;
  Assembler& asm_;
};

}  // namespace wasm
}  // namespace v8::internal

#endif  // V8_WASM_TURBOSHAFT_GRAPH_INTERFACE_H_
