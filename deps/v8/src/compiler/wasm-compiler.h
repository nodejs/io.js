// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_COMPILER_H_
#define V8_COMPILER_WASM_COMPILER_H_

#include <memory>
#include <utility>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything else from src/compiler here!
#include "src/base/small-vector.h"
#include "src/codegen/compiler.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/runtime/runtime.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-result.h"
#include "src/zone/zone.h"

namespace v8 {

class CFunctionInfo;

namespace internal {
enum class AbortReason : uint8_t;
struct AssemblerOptions;
enum class BranchHint : uint8_t;
class TurbofanCompilationJob;

namespace compiler {
// Forward declarations for some compiler data structures.
class CallDescriptor;
class Graph;
class MachineGraph;
class Node;
class NodeOriginTable;
class Operator;
class SourcePositionTable;
struct WasmCompilationData;
class WasmDecorator;
class WasmGraphAssembler;
enum class TrapId : int32_t;
struct Int64LoweringSpecialCase;
template <size_t VarCount>
class GraphAssemblerLabel;
struct WasmTypeCheckConfig;
}  // namespace compiler

namespace wasm {
struct DecodeStruct;
class WasmCode;
class WireBytesStorage;
enum class LoadTransformationKind : uint8_t;
enum Suspend : int;
enum CallOrigin { kCalledFromWasm, kCalledFromJS };
}  // namespace wasm

namespace compiler {

// Compiles an import call wrapper, which allows Wasm to call imports.
V8_EXPORT_PRIVATE wasm::WasmCompilationResult CompileWasmImportCallWrapper(
    wasm::ImportCallKind, const wasm::CanonicalSig*, bool source_positions,
    int expected_arity, wasm::Suspend);

// Compiles a host call wrapper, which allows Wasm to call host functions.
wasm::WasmCompilationResult CompileWasmCapiCallWrapper(
    const wasm::CanonicalSig*);

bool IsFastCallSupportedSignature(const v8::CFunctionInfo*);
// Compiles a wrapper to call a Fast API function from Wasm.
wasm::WasmCompilationResult CompileWasmJSFastCallWrapper(
    const wasm::CanonicalSig*, DirectHandle<JSReceiver> callable);

// Returns a TurboshaftCompilationJob object for a JS to Wasm wrapper.
std::unique_ptr<OptimizedCompilationJob> NewJSToWasmCompilationJob(
    Isolate* isolate, const wasm::CanonicalSig* sig);

enum CWasmEntryParameters {
  kCodeEntry,
  kObjectRef,
  kArgumentsBuffer,
  kCEntryFp,
  // marker:
  kNumParameters
};

// Compiles a stub with C++ linkage, to be called from Execution::CallWasm,
// which knows how to feed it its parameters.
V8_EXPORT_PRIVATE Handle<Code> CompileCWasmEntry(Isolate*,
                                                 const wasm::CanonicalSig*);

struct WasmLoopInfo {
  Node* header;
  uint32_t nesting_depth;
  // This loop has, to our best knowledge, no other loops nested within it. A
  // loop can obtain inner loops despite this after inlining.
  bool can_be_innermost;

  WasmLoopInfo(Node* header, uint32_t nesting_depth, bool can_be_innermost)
      : header(header),
        nesting_depth(nesting_depth),
        can_be_innermost(can_be_innermost) {}
};

struct WasmCompilationData {
  explicit WasmCompilationData(const wasm::FunctionBody& func_body)
      : func_body(func_body) {}

  size_t body_size() { return func_body.end - func_body.start; }

  const wasm::FunctionBody& func_body;
  const wasm::WireBytesStorage* wire_bytes_storage;
  NodeOriginTable* node_origins{nullptr};
  std::vector<WasmLoopInfo>* loop_infos{nullptr};
  std::unique_ptr<wasm::AssumptionsJournal> assumptions{};
  SourcePositionTable* source_positions{nullptr};
  int func_index;
};

// Abstracts details of building TurboFan graph nodes for wasm to separate
// the wasm decoder from the internal details of TurboFan.
class WasmGraphBuilder {
 public:
  // ParameterMode specifies how the instance is passed.
  enum ParameterMode {
    // Normal wasm functions pass the instance as an implicit first parameter.
    kInstanceParameterMode,
    // For Wasm-to-JS and C-API wrappers, a {WasmImportData} object is
    // passed as first parameter.
    kWasmImportDataMode,
    // For JS-to-Wasm wrappers (which are JS functions), we load the Wasm
    // instance from the JS function data. The generated code objects live on
    // the JS heap, so those compilation pass an isolate.
    kJSFunctionAbiMode,
    // The JS-to-JS wrapper does not have an associated instance.
    // The C-entry stub uses a custom ABI (see {CWasmEntryParameters}).
    kNoSpecialParameterMode
  };

  V8_EXPORT_PRIVATE WasmGraphBuilder(
      wasm::CompilationEnv* env, Zone* zone, MachineGraph* mcgraph,
      const wasm::FunctionSig* sig, compiler::SourcePositionTable* spt,
      ParameterMode parameter_mode, Isolate* isolate,
      wasm::WasmEnabledFeatures enabled_features,
      const wasm::CanonicalSig* wrapper_sig = nullptr);

  V8_EXPORT_PRIVATE ~WasmGraphBuilder();

  bool TryWasmInlining(int fct_index, wasm::NativeModule* native_module,
                       int inlining_id);

  //-----------------------------------------------------------------------
  // Operations independent of {control} or {effect}.
  //-----------------------------------------------------------------------
  void Start(unsigned params);
  Node* Param(int index, const char* debug_name = nullptr);
  void TerminateThrow(Node* effect, Node* control);
  Node* Int32Constant(int32_t value);

  //-----------------------------------------------------------------------
  // Operations that read and/or write {control} and {effect}.
  //-----------------------------------------------------------------------

  Node* Return(base::Vector<Node*> nodes);
  template <typename... Nodes>
  Node* Return(Node* fst, Nodes*... more) {
    Node* arr[] = {fst, more...};
    return Return(base::ArrayVector(arr));
  }

  Node* effect();
  Node* control();
  Node* SetEffect(Node* node);
  Node* SetControl(Node* node);
  void SetEffectControl(Node* effect, Node* control);
  Node* SetEffectControl(Node* effect_and_control) {
    SetEffectControl(effect_and_control, effect_and_control);
    return effect_and_control;
  }

  Node* SetType(Node* node, wasm::ValueType type);

  // Overload for when we want to provide a specific signature, rather than
  // build one using sig_, for example after scalar lowering.
  V8_EXPORT_PRIVATE void LowerInt64(Signature<MachineRepresentation>* sig);
  V8_EXPORT_PRIVATE void LowerInt64(wasm::CallOrigin origin);

  void SetSourcePosition(Node* node, wasm::WasmCodePosition position);

  Node* IsNull(Node* object, wasm::ValueType type);
  Node* TypeGuard(Node* value, wasm::ValueType type);

  bool has_simd() const { return has_simd_; }

  MachineGraph* mcgraph() { return mcgraph_; }
  Graph* graph();
  Zone* graph_zone();

 protected:
  Node* NoContextConstant();

  Node* BuildLoadIsolateRoot();
  Node* UndefinedValue();

  const Operator* GetSafeLoadOperator(int offset, wasm::ValueTypeBase type);
  Node* BuildSafeStore(int offset, wasm::ValueTypeBase type, Node* arg_buffer,
                       Node* value, Node* effect, Node* control);

  Node* BuildCallNode(size_t param_count, base::Vector<Node*> args,
                      wasm::WasmCodePosition position, Node* instance_node,
                      const Operator* op, Node* frame_state = nullptr);
  template <typename T>
  Node* BuildWasmCall(const Signature<T>* sig, base::Vector<Node*> args,
                      base::Vector<Node*> rets, wasm::WasmCodePosition position,
                      Node* implicit_first_arg, bool indirect,
                      Node* frame_state = nullptr);

  //-----------------------------------------------------------------------
  // Operations involving the CEntry, a dependency we want to remove
  // to get off the GC heap.
  //-----------------------------------------------------------------------
  Node* BuildCallToRuntime(Runtime::FunctionId f, Node** parameters,
                           int parameter_count);

  Node* BuildCallToRuntimeWithContext(Runtime::FunctionId f, Node* js_context,
                                      Node** parameters, int parameter_count);

  TrapId GetTrapIdForTrap(wasm::TrapReason reason);

  void BuildModifyThreadInWasmFlag(bool new_value);
  void BuildModifyThreadInWasmFlagHelper(Node* thread_in_wasm_flag_address,
                                         bool new_value);

  Node* BuildChangeInt64ToBigInt(Node* input, StubCallMode stub_mode);

  void Assert(Node* condition, AbortReason abort_reason);

  std::unique_ptr<WasmGraphAssembler> gasm_;
  Zone* const zone_;
  MachineGraph* const mcgraph_;
  wasm::CompilationEnv* const env_;
  // For the main WasmGraphBuilder class, this is identical to the features
  // field in {env_}, but the WasmWrapperGraphBuilder subclass doesn't have
  // that, so common code should use this field instead.
  wasm::WasmEnabledFeatures enabled_features_;

  Node** parameters_;

  SetOncePointer<Node> stack_check_code_node_;
  SetOncePointer<const Operator> stack_check_call_operator_;

  bool has_simd_ = false;
  bool needs_stack_check_ = false;

  const wasm::FunctionSig* const function_sig_;
  const wasm::CanonicalSig* const wrapper_sig_{nullptr};

  compiler::WasmDecorator* decorator_ = nullptr;

  compiler::SourcePositionTable* const source_position_table_ = nullptr;
  int inlining_id_ = -1;
  const ParameterMode parameter_mode_;
  Isolate* const isolate_;
  SetOncePointer<Node> instance_data_node_;
  NullCheckStrategy null_check_strategy_;
  static constexpr int kNoCachedMemoryIndex = -1;
  int cached_memory_index_ = kNoCachedMemoryIndex;
};

V8_EXPORT_PRIVATE void BuildInlinedJSToWasmWrapper(
    Zone* zone, MachineGraph* mcgraph, const wasm::CanonicalSig* signature,
    Isolate* isolate, compiler::SourcePositionTable* spt, Node* frame_state,
    bool set_in_wasm_flag);

AssemblerOptions WasmAssemblerOptions();
AssemblerOptions WasmStubAssemblerOptions();

template <typename T>
Signature<MachineRepresentation>* CreateMachineSignature(
    Zone* zone, const Signature<T>* sig, wasm::CallOrigin origin);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_COMPILER_H_
