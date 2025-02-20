// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-compiler.h"

#include <memory>
#include <optional>

#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/codegen/assembler.h"
#include "src/codegen/compiler.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/diamond.h"
#include "src/compiler/fast-api-calls.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/int64-lowering.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/compiler/turbofan-graph.h"
#include "src/compiler/turboshaft/wasm-turboshaft-compiler.h"
#include "src/compiler/wasm-call-descriptors.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/compiler/wasm-inlining-into-js.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/execution/simulator-base.h"
#include "src/heap/factory.h"
#include "src/logging/counters.h"
#include "src/objects/code-kind.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type.h"
#include "src/objects/name.h"
#include "src/objects/string.h"
#include "src/roots/roots.h"
#include "src/tracing/trace-event.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/jump-table-assembler.h"
#include "src/wasm/memory-tracing.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8::internal::compiler {

namespace {

// Use MachineType::Pointer() over Tagged() to load root pointers because they
// do not get compressed.
#define LOAD_ROOT(RootName, factory_name)                         \
  (isolate_ ? graph()->NewNode(mcgraph()->common()->HeapConstant( \
                  isolate_->factory()->factory_name()))           \
            : gasm_->LoadImmutable(                               \
                  MachineType::Pointer(), BuildLoadIsolateRoot(), \
                  IsolateData::root_slot_offset(RootIndex::k##RootName)))

template <typename T>
bool ContainsSimd(const Signature<T>* sig) {
  for (auto type : sig->all()) {
    if (type == T::Primitive(wasm::kS128)) return true;
  }
  return false;
}

bool ContainsInt64(const wasm::CanonicalSig* sig) {
  for (auto type : sig->all()) {
    if (type == wasm::kCanonicalI64) return true;
  }
  return false;
}

}  // namespace

WasmGraphBuilder::WasmGraphBuilder(
    wasm::CompilationEnv* env, Zone* zone, MachineGraph* mcgraph,
    const wasm::FunctionSig* sig,
    compiler::SourcePositionTable* source_position_table,
    ParameterMode parameter_mode, Isolate* isolate,
    wasm::WasmEnabledFeatures enabled_features,
    const wasm::CanonicalSig* wrapper_sig)
    : gasm_(std::make_unique<WasmGraphAssembler>(mcgraph, zone)),
      zone_(zone),
      mcgraph_(mcgraph),
      env_(env),
      enabled_features_(enabled_features),
      has_simd_(sig ? ContainsSimd(sig) : ContainsSimd(wrapper_sig)),
      function_sig_(sig),
      wrapper_sig_(wrapper_sig),
      source_position_table_(source_position_table),
      parameter_mode_(parameter_mode),
      isolate_(isolate),
      null_check_strategy_(trap_handler::IsTrapHandlerEnabled() &&
                                   V8_STATIC_ROOTS_BOOL
                               ? NullCheckStrategy::kTrapHandler
                               : NullCheckStrategy::kExplicit) {
  // This code is only used
  // - for compiling certain wrappers (wasm-to-fast API, C-wasm-entry), and
  // - for inlining js-to-wasm wrappers into Turbofan-compile JS functions.
  CHECK(parameter_mode != ParameterMode::kInstanceParameterMode);

  // There are two kinds of isolate-specific code: JS-to-JS wrappers (passing
  // kNoSpecialParameterMode) and JS-to-Wasm wrappers (passing
  // kJSFunctionAbiMode).
  DCHECK_IMPLIES(isolate != nullptr,
                 parameter_mode_ == kJSFunctionAbiMode ||
                     parameter_mode_ == kNoSpecialParameterMode);
  DCHECK_IMPLIES(env && env->module &&
                     std::any_of(env->module->memories.begin(),
                                 env->module->memories.end(),
                                 [](auto& memory) {
                                   return memory.bounds_checks ==
                                          wasm::kTrapHandler;
                                 }),
                 trap_handler::IsTrapHandlerEnabled());
  DCHECK_NOT_NULL(mcgraph_);
}

// Destructor define here where the definition of {WasmGraphAssembler} is
// available.
WasmGraphBuilder::~WasmGraphBuilder() = default;

bool WasmGraphBuilder::TryWasmInlining(int fct_index,
                                       wasm::NativeModule* native_module,
                                       int inlining_id) {
#define TRACE(x)                         \
  do {                                   \
    if (v8_flags.trace_turbo_inlining) { \
      StdoutStream() << x << "\n";       \
    }                                    \
  } while (false)

  DCHECK(native_module->HasWireBytes());
  const wasm::WasmModule* module = native_module->module();
  const wasm::WasmFunction& inlinee = module->functions[fct_index];
  // TODO(mliedtke): What would be a proper maximum size?
  const uint32_t kMaxWasmInlineeSize = 30;
  if (inlinee.code.length() > kMaxWasmInlineeSize) {
    TRACE("- not inlining: function body is larger than max inlinee size ("
          << inlinee.code.length() << " > " << kMaxWasmInlineeSize << ")");
    return false;
  }
  if (inlinee.imported) {
    TRACE("- not inlining: function is imported");
    return false;
  }
  base::Vector<const uint8_t> bytes(native_module->wire_bytes().SubVector(
      inlinee.code.offset(), inlinee.code.end_offset()));
  bool is_shared = module->type(inlinee.sig_index).is_shared;
  const wasm::FunctionBody inlinee_body(inlinee.sig, inlinee.code.offset(),
                                        bytes.begin(), bytes.end(), is_shared);
  // If the inlinee was not validated before, do that now.
  if (V8_UNLIKELY(!module->function_was_validated(fct_index))) {
    wasm::WasmDetectedFeatures unused_detected_features;
    if (ValidateFunctionBody(graph()->zone(), enabled_features_, module,
                             &unused_detected_features, inlinee_body)
            .failed()) {
      // At this point we cannot easily raise a compilation error any more.
      // Since this situation is highly unlikely though, we just ignore this
      // inlinee and move on. The same validation error will be triggered
      // again when actually compiling the invalid function.
      TRACE("- not inlining: function body is invalid");
      return false;
    }
    module->set_function_validated(fct_index);
  }
  bool result = WasmIntoJSInliner::TryInlining(
      graph()->zone(), module, mcgraph_, inlinee_body, bytes,
      source_position_table_, inlining_id);
  TRACE((
      result
          ? "- inlining"
          : "- not inlining: function body contains unsupported instructions"));
  return result;
#undef TRACE
}

void WasmGraphBuilder::Start(unsigned params) {
  Node* start = graph()->NewNode(mcgraph()->common()->Start(params));
  graph()->SetStart(start);
  SetEffectControl(start);
  // Initialize parameter nodes.
  parameters_ = zone_->AllocateArray<Node*>(params);
  for (unsigned i = 0; i < params; i++) {
    parameters_[i] = nullptr;
  }
  // Initialize instance node.
  switch (parameter_mode_) {
    case kInstanceParameterMode: {
      Node* param = Param(wasm::kWasmInstanceDataParameterIndex);
      if (v8_flags.debug_code) {
        Assert(gasm_->HasInstanceType(param, WASM_TRUSTED_INSTANCE_DATA_TYPE),
               AbortReason::kUnexpectedInstanceType);
      }
      instance_data_node_ = param;
      break;
    }
    case kWasmImportDataMode: {
      Node* param = Param(0);
      if (v8_flags.debug_code) {
        Assert(gasm_->HasInstanceType(param, WASM_IMPORT_DATA_TYPE),
               AbortReason::kUnexpectedInstanceType);
      }
      instance_data_node_ = gasm_->LoadProtectedPointerFromObject(
          param, wasm::ObjectAccess::ToTagged(
                     WasmImportData::kProtectedInstanceDataOffset));
      break;
    }
    case kJSFunctionAbiMode: {
      Node* param = Param(Linkage::kJSCallClosureParamIndex, "%closure");
      if (v8_flags.debug_code) {
        Assert(gasm_->HasInstanceType(param, JS_FUNCTION_TYPE),
               AbortReason::kUnexpectedInstanceType);
      }
      instance_data_node_ = gasm_->LoadExportedFunctionInstanceData(
          gasm_->LoadFunctionDataFromJSFunction(param));
      break;
    }
    case kNoSpecialParameterMode:
      break;
  }
  graph()->SetEnd(graph()->NewNode(mcgraph()->common()->End(0)));
}

Node* WasmGraphBuilder::Param(int index, const char* debug_name) {
  DCHECK_NOT_NULL(graph()->start());
  // Turbofan allows negative parameter indices.
  DCHECK_GE(index, kMinParameterIndex);
  int array_index = index - kMinParameterIndex;
  if (parameters_[array_index] == nullptr) {
    parameters_[array_index] = graph()->NewNode(
        mcgraph()->common()->Parameter(index, debug_name), graph()->start());
  }
  return parameters_[array_index];
}

void WasmGraphBuilder::TerminateThrow(Node* effect, Node* control) {
  Node* terminate =
      graph()->NewNode(mcgraph()->common()->Throw(), effect, control);
  gasm_->MergeControlToEnd(terminate);
  gasm_->InitializeEffectControl(nullptr, nullptr);
}

Node* WasmGraphBuilder::NoContextConstant() {
  return mcgraph()->IntPtrConstant(0);
}

Node* WasmGraphBuilder::BuildLoadIsolateRoot() {
  return isolate_ ? mcgraph()->IntPtrConstant(isolate_->isolate_root())
                  : gasm_->LoadRootRegister();
}

Node* WasmGraphBuilder::Int32Constant(int32_t value) {
  return mcgraph()->Int32Constant(value);
}

Node* WasmGraphBuilder::UndefinedValue() {
  return LOAD_ROOT(UndefinedValue, undefined_value);
}

// TODO(ahaas): Merge TrapId with TrapReason.
TrapId WasmGraphBuilder::GetTrapIdForTrap(wasm::TrapReason reason) {
  switch (reason) {
#define TRAPREASON_TO_TRAPID(name)                                 \
  case wasm::k##name:                                              \
    static_assert(static_cast<int>(TrapId::k##name) ==             \
                      static_cast<int>(Builtin::kThrowWasm##name), \
                  "trap id mismatch");                             \
    return TrapId::k##name;
    FOREACH_WASM_TRAPREASON(TRAPREASON_TO_TRAPID)
#undef TRAPREASON_TO_TRAPID
    default:
      UNREACHABLE();
  }
}

Node* WasmGraphBuilder::Return(base::Vector<Node*> vals) {
  unsigned count = static_cast<unsigned>(vals.size());
  base::SmallVector<Node*, 8> buf(count + 3);

  // TODOC: What is the meaning of the 0-constant?
  buf[0] = Int32Constant(0);
  if (count > 0) {
    memcpy(buf.data() + 1, vals.begin(), sizeof(void*) * count);
  }
  buf[count + 1] = effect();
  buf[count + 2] = control();
  Node* ret = graph()->NewNode(mcgraph()->common()->Return(count), count + 3,
                               buf.data());

  gasm_->MergeControlToEnd(ret);
  return ret;
}

Node* WasmGraphBuilder::IsNull(Node* object, wasm::ValueType type) {
  // This version is for Wasm functions (i.e. not wrappers):
  // - they use module-specific types
  // - they go through a lowering phase later
  // Both points are different in wrappers, see
  // WasmWrapperGraphBuilder::IsNull().
  DCHECK_EQ(parameter_mode_, kInstanceParameterMode);
  return gasm_->IsNull(object, type);
}

Node* WasmGraphBuilder::effect() { return gasm_->effect(); }

Node* WasmGraphBuilder::control() { return gasm_->control(); }

Node* WasmGraphBuilder::SetEffect(Node* node) {
  SetEffectControl(node, control());
  return node;
}

Node* WasmGraphBuilder::SetControl(Node* node) {
  SetEffectControl(effect(), node);
  return node;
}

void WasmGraphBuilder::SetEffectControl(Node* effect, Node* control) {
  gasm_->InitializeEffectControl(effect, control);
}

Node* WasmGraphBuilder::BuildCallNode(size_t param_count,
                                      base::Vector<Node*> args,
                                      wasm::WasmCodePosition position,
                                      Node* implicit_first_arg,
                                      const Operator* op, Node* frame_state) {
  needs_stack_check_ = true;
  const size_t has_frame_state = frame_state != nullptr ? 1 : 0;
  const size_t extra = 3;  // instance_node, effect, and control.
  const size_t count = 1 + param_count + extra + has_frame_state;

  // Reallocate the buffer to make space for extra inputs.
  base::SmallVector<Node*, 16 + extra> inputs(count);
  DCHECK_EQ(1 + param_count, args.size());

  // Make room for the first argument at index 1, just after code.
  inputs[0] = args[0];  // code
  inputs[1] = implicit_first_arg;
  if (param_count > 0) {
    memcpy(&inputs[2], &args[1], param_count * sizeof(Node*));
  }

  // Add effect and control inputs.
  if (has_frame_state != 0) inputs[param_count + 2] = frame_state;
  inputs[param_count + has_frame_state + 2] = effect();
  inputs[param_count + has_frame_state + 3] = control();

  Node* call = graph()->NewNode(op, static_cast<int>(count), inputs.begin());
  // Return calls have no effect output. Other calls are the new effect node.
  if (op->EffectOutputCount() > 0) SetEffect(call);
  DCHECK(position == wasm::kNoCodePosition || position > 0);
  if (position > 0) SetSourcePosition(call, position);

  return call;
}

template <typename T>
Node* WasmGraphBuilder::BuildWasmCall(const Signature<T>* sig,
                                      base::Vector<Node*> args,
                                      base::Vector<Node*> rets,
                                      wasm::WasmCodePosition position,
                                      Node* implicit_first_arg, bool indirect,
                                      Node* frame_state) {
  WasmCallKind call_kind = indirect ? kWasmIndirectFunction : kWasmFunction;
  CallDescriptor* call_descriptor = GetWasmCallDescriptor(
      mcgraph()->zone(), sig, call_kind, frame_state != nullptr);
  const Operator* op = mcgraph()->common()->Call(call_descriptor);
  Node* call = BuildCallNode(sig->parameter_count(), args, position,
                             implicit_first_arg, op, frame_state);
  // TODO(manoskouk): These assume the call has control and effect outputs.
  DCHECK_GT(op->ControlOutputCount(), 0);
  DCHECK_GT(op->EffectOutputCount(), 0);
  SetEffectControl(call);

  size_t ret_count = sig->return_count();
  if (ret_count == 0) return call;  // No return value.

  DCHECK_EQ(ret_count, rets.size());
  if (ret_count == 1) {
    // Only a single return value.
    rets[0] = call;
  } else {
    // Create projections for all return values.
    for (size_t i = 0; i < ret_count; i++) {
      rets[i] = graph()->NewNode(mcgraph()->common()->Projection(i), call,
                                 graph()->start());
    }
  }
  return call;
}

// Only call this function for code which is not reused across instantiations,
// as we do not patch the embedded js_context.
Node* WasmGraphBuilder::BuildCallToRuntimeWithContext(Runtime::FunctionId f,
                                                      Node* js_context,
                                                      Node** parameters,
                                                      int parameter_count) {
  const Runtime::Function* fun = Runtime::FunctionForId(f);
  auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
      mcgraph()->zone(), f, fun->nargs, Operator::kNoProperties,
      CallDescriptor::kNoFlags);
  // The CEntryStub is loaded from the IsolateRoot so that generated code is
  // Isolate independent. At the moment this is only done for CEntryStub(1).
  Node* isolate_root = BuildLoadIsolateRoot();
  DCHECK_EQ(1, fun->result_size);
  auto centry_id = Builtin::kWasmCEntry;
  int builtin_slot_offset = IsolateData::BuiltinSlotOffset(centry_id);
  Node* centry_stub =
      gasm_->Load(MachineType::Pointer(), isolate_root, builtin_slot_offset);
  // TODO(titzer): allow arbitrary number of runtime arguments
  // At the moment we only allow 5 parameters. If more parameters are needed,
  // increase this constant accordingly.
  static const int kMaxParams = 5;
  DCHECK_GE(kMaxParams, parameter_count);
  Node* inputs[kMaxParams + 6];
  int count = 0;
  inputs[count++] = centry_stub;
  for (int i = 0; i < parameter_count; i++) {
    inputs[count++] = parameters[i];
  }
  inputs[count++] =
      mcgraph()->ExternalConstant(ExternalReference::Create(f));  // ref
  inputs[count++] = Int32Constant(fun->nargs);                    // arity
  inputs[count++] = js_context;                                   // js_context
  inputs[count++] = effect();
  inputs[count++] = control();

  return gasm_->Call(call_descriptor, count, inputs);
}

Node* WasmGraphBuilder::BuildCallToRuntime(Runtime::FunctionId f,
                                           Node** parameters,
                                           int parameter_count) {
  return BuildCallToRuntimeWithContext(f, NoContextConstant(), parameters,
                                       parameter_count);
}

const Operator* WasmGraphBuilder::GetSafeLoadOperator(
    int offset, wasm::ValueTypeBase type) {
  int alignment = offset % type.value_kind_size();
  MachineType mach_type = type.machine_type();
  if (COMPRESS_POINTERS_BOOL && mach_type.IsTagged()) {
    // We are loading tagged value from off-heap location, so we need to load
    // it as a full word otherwise we will not be able to decompress it.
    mach_type = MachineType::Pointer();
  }
  if (alignment == 0 || mcgraph()->machine()->UnalignedLoadSupported(
                            type.machine_representation())) {
    return mcgraph()->machine()->Load(mach_type);
  }
  return mcgraph()->machine()->UnalignedLoad(mach_type);
}

Node* WasmGraphBuilder::BuildSafeStore(int offset, wasm::ValueTypeBase type,
                                       Node* arg_buffer, Node* value,
                                       Node* effect, Node* control) {
  int alignment = offset % type.value_kind_size();
  MachineRepresentation rep = type.machine_representation();
  if (COMPRESS_POINTERS_BOOL && IsAnyTagged(rep)) {
    // We are storing tagged value to off-heap location, so we need to store
    // it as a full word otherwise we will not be able to decompress it.
    rep = MachineType::PointerRepresentation();
    value = effect = graph()->NewNode(
        mcgraph()->machine()->BitcastTaggedToWord(), value, effect, control);
  }
  if (alignment == 0 || mcgraph()->machine()->UnalignedStoreSupported(rep)) {
    StoreRepresentation store_rep(rep, WriteBarrierKind::kNoWriteBarrier);
    return graph()->NewNode(mcgraph()->machine()->Store(store_rep), arg_buffer,
                            Int32Constant(offset), value, effect, control);
  }
  UnalignedStoreRepresentation store_rep(rep);
  return graph()->NewNode(mcgraph()->machine()->UnalignedStore(store_rep),
                          arg_buffer, Int32Constant(offset), value, effect,
                          control);
}

Graph* WasmGraphBuilder::graph() { return mcgraph()->graph(); }

Zone* WasmGraphBuilder::graph_zone() { return graph()->zone(); }

template <typename T>
Signature<MachineRepresentation>* CreateMachineSignature(
    Zone* zone, const Signature<T>* sig, wasm::CallOrigin origin) {
  Signature<MachineRepresentation>::Builder builder(zone, sig->return_count(),
                                                    sig->parameter_count());
  for (auto ret : sig->returns()) {
    if (origin == wasm::kCalledFromJS) {
      builder.AddReturn(MachineRepresentation::kTagged);
    } else {
      builder.AddReturn(ret.machine_representation());
    }
  }

  for (auto param : sig->parameters()) {
    if (origin == wasm::kCalledFromJS) {
      // Parameters coming from JavaScript are always tagged values. Especially
      // when the signature says that it's an I64 value, then a BigInt object is
      // provided by JavaScript, and not two 32-bit parameters.
      builder.AddParam(MachineRepresentation::kTagged);
    } else {
      builder.AddParam(param.machine_representation());
    }
  }
  return builder.Get();
}

template Signature<MachineRepresentation>* CreateMachineSignature(
    Zone*, const Signature<wasm::ValueType>*, wasm::CallOrigin);
template Signature<MachineRepresentation>* CreateMachineSignature(
    Zone*, const Signature<wasm::CanonicalValueType>*, wasm::CallOrigin);

void WasmGraphBuilder::LowerInt64(Signature<MachineRepresentation>* sig) {
  if (mcgraph()->machine()->Is64()) return;
  Int64Lowering r(mcgraph()->graph(), mcgraph()->machine(), mcgraph()->common(),
                  gasm_->simplified(), mcgraph()->zone(), sig);
  r.LowerGraph();
}

void WasmGraphBuilder::LowerInt64(wasm::CallOrigin origin) {
  Signature<MachineRepresentation>* machine_sig =
      function_sig_ != nullptr
          ? CreateMachineSignature(mcgraph()->zone(), function_sig_, origin)
          : CreateMachineSignature(mcgraph()->zone(), wrapper_sig_, origin);
  LowerInt64(machine_sig);
}

Node* WasmGraphBuilder::BuildChangeInt64ToBigInt(Node* input,
                                                 StubCallMode stub_mode) {
  if (mcgraph()->machine()->Is64()) {
    return gasm_->CallBuiltin(Builtin::kI64ToBigInt, Operator::kEliminatable,
                              input);
  } else {
    Node* low_word = gasm_->TruncateInt64ToInt32(input);
    Node* high_word = gasm_->TruncateInt64ToInt32(
        gasm_->Word64Shr(input, gasm_->Int32Constant(32)));
    return gasm_->CallBuiltin(Builtin::kI32PairToBigInt,
                              Operator::kEliminatable, low_word, high_word);
  }
}

void WasmGraphBuilder::SetSourcePosition(Node* node,
                                         wasm::WasmCodePosition position) {
  DCHECK_NE(position, wasm::kNoCodePosition);
  if (source_position_table_) {
    source_position_table_->SetSourcePosition(
        node, SourcePosition(position, inlining_id_));
  }
}

Node* WasmGraphBuilder::TypeGuard(Node* value, wasm::ValueType type) {
  DCHECK_NOT_NULL(env_);
  return SetEffect(graph()->NewNode(mcgraph()->common()->TypeGuard(Type::Wasm(
                                        type, env_->module, graph()->zone())),
                                    value, effect(), control()));
}

void WasmGraphBuilder::BuildModifyThreadInWasmFlagHelper(
    Node* thread_in_wasm_flag_address, bool new_value) {
  if (v8_flags.debug_code) {
    Node* flag_value =
        gasm_->Load(MachineType::Int32(), thread_in_wasm_flag_address, 0);
    Node* check =
        gasm_->Word32Equal(flag_value, Int32Constant(new_value ? 0 : 1));
    Assert(check, new_value ? AbortReason::kUnexpectedThreadInWasmSet
                            : AbortReason::kUnexpectedThreadInWasmUnset);
  }

  gasm_->Store({MachineRepresentation::kWord32, kNoWriteBarrier},
               thread_in_wasm_flag_address, 0,
               Int32Constant(new_value ? 1 : 0));
}

void WasmGraphBuilder::BuildModifyThreadInWasmFlag(bool new_value) {
  if (!trap_handler::IsTrapHandlerEnabled()) return;
  Node* isolate_root = BuildLoadIsolateRoot();

  Node* thread_in_wasm_flag_address =
      gasm_->Load(MachineType::Pointer(), isolate_root,
                  Isolate::thread_in_wasm_flag_address_offset());

  BuildModifyThreadInWasmFlagHelper(thread_in_wasm_flag_address, new_value);
}

void WasmGraphBuilder::Assert(Node* condition, AbortReason abort_reason) {
  if (!v8_flags.debug_code) return;

  Diamond check(graph(), mcgraph()->common(), condition, BranchHint::kTrue);
  check.Chain(control());
  SetControl(check.if_false);
  Node* message_id = gasm_->NumberConstant(static_cast<int32_t>(abort_reason));
  Node* old_effect = effect();
  Node* call = BuildCallToRuntimeWithContext(
      Runtime::kAbort, NoContextConstant(), &message_id, 1);
  check.merge->ReplaceInput(1, call);
  SetEffectControl(check.EffectPhi(old_effect, effect()), check.merge);
}

Node* WasmGraphBuilder::SetType(Node* node, wasm::ValueType type) {
  DCHECK_NOT_NULL(env_);
  if (!compiler::NodeProperties::IsTyped(node)) {
    compiler::NodeProperties::SetType(
        node, compiler::Type::Wasm(type, env_->module, graph_zone()));
  } else {
    // We might try to set the type twice since some nodes are cached in the
    // graph assembler, but we should never change the type.
    // The exception is imported strings support, which may special-case
    // values that are officially externref-typed as being known to be strings.
#if DEBUG
    static constexpr wasm::ValueType kRefExtern =
        wasm::ValueType::Ref(wasm::HeapType::kExtern);
    DCHECK((compiler::NodeProperties::GetType(node).AsWasm().type == type) ||
           (enabled_features_.has_imported_strings() &&
            compiler::NodeProperties::GetType(node).AsWasm().type ==
                wasm::kWasmRefExternString &&
            (type == wasm::kWasmExternRef || type == kRefExtern)));
#endif
  }
  return node;
}

class WasmDecorator final : public GraphDecorator {
 public:
  explicit WasmDecorator(NodeOriginTable* origins, wasm::Decoder* decoder)
      : origins_(origins), decoder_(decoder) {}

  void Decorate(Node* node) final {
    origins_->SetNodeOrigin(
        node, NodeOrigin("wasm graph creation", "n/a",
                         NodeOrigin::kWasmBytecode, decoder_->position()));
  }

 private:
  compiler::NodeOriginTable* origins_;
  wasm::Decoder* decoder_;
};

namespace {

// A non-null {isolate} signifies that the generated code is treated as being in
// a JS frame for functions like BuildLoadIsolateRoot().
class WasmWrapperGraphBuilder : public WasmGraphBuilder {
 public:
  WasmWrapperGraphBuilder(Zone* zone, MachineGraph* mcgraph,
                          const wasm::CanonicalSig* sig,
                          ParameterMode parameter_mode, Isolate* isolate,
                          compiler::SourcePositionTable* spt)
      : WasmGraphBuilder(nullptr, zone, mcgraph, nullptr, spt, parameter_mode,
                         isolate, wasm::WasmEnabledFeatures::All(), sig) {}

  CallDescriptor* GetBigIntToI64CallDescriptor(bool needs_frame_state) {
    return wasm::GetWasmEngine()->call_descriptors()->GetBigIntToI64Descriptor(
        needs_frame_state);
  }

  Node* GetTargetForBuiltinCall(Builtin builtin) {
    // Per-process shared wrappers don't have access to a jump table, so they
    // can't use kCallWasmRuntimeStub mode.
    return gasm_->GetBuiltinPointerTarget(builtin);
  }

  Node* IsNull(Node* object, wasm::CanonicalValueType type) {
    // We immediately lower null in wrappers, as they do not go through a
    // lowering phase.
    Node* null = type.use_wasm_null() ? LOAD_ROOT(WasmNull, wasm_null)
                                      : LOAD_ROOT(NullValue, null_value);
    return gasm_->TaggedEqual(object, null);
  }

  Node* BuildChangeInt32ToNumber(Node* value) {
    // We expect most integers at runtime to be Smis, so it is important for
    // wrapper performance that Smi conversion be inlined.
    if (SmiValuesAre32Bits()) {
      return gasm_->BuildChangeInt32ToSmi(value);
    }
    DCHECK(SmiValuesAre31Bits());

    auto builtin = gasm_->MakeDeferredLabel();
    auto done = gasm_->MakeLabel(MachineRepresentation::kTagged);

    // Double value to test if value can be a Smi, and if so, to convert it.
    Node* add = gasm_->Int32AddWithOverflow(value, value);
    Node* ovf = gasm_->Projection(1, add);
    gasm_->GotoIf(ovf, &builtin);

    // If it didn't overflow, the result is {2 * value} as pointer-sized value.
    Node* smi_tagged =
        gasm_->BuildChangeInt32ToIntPtr(gasm_->Projection(0, add));
    gasm_->Goto(&done, smi_tagged);

    // Otherwise, call builtin, to convert to a HeapNumber.
    gasm_->Bind(&builtin);
    CommonOperatorBuilder* common = mcgraph()->common();
    Node* target = GetTargetForBuiltinCall(Builtin::kWasmInt32ToHeapNumber);
    if (!int32_to_heapnumber_operator_.is_set()) {
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          mcgraph()->zone(), WasmInt32ToHeapNumberDescriptor(), 0,
          CallDescriptor::kNoFlags, Operator::kNoProperties,
          StubCallMode::kCallBuiltinPointer);
      int32_to_heapnumber_operator_.set(common->Call(call_descriptor));
    }
    Node* call =
        gasm_->Call(int32_to_heapnumber_operator_.get(), target, value);
    gasm_->Goto(&done, call);
    gasm_->Bind(&done);
    return done.PhiAt(0);
  }

  Node* BuildChangeTaggedToInt32(Node* value, Node* context,
                                 Node* frame_state) {
    // We expect most integers at runtime to be Smis, so it is important for
    // wrapper performance that Smi conversion be inlined.
    auto builtin = gasm_->MakeDeferredLabel();
    auto done = gasm_->MakeLabel(MachineRepresentation::kWord32);

    gasm_->GotoIfNot(IsSmi(value), &builtin);

    // If Smi, convert to int32.
    Node* smi = gasm_->BuildChangeSmiToInt32(value);
    gasm_->Goto(&done, smi);

    // Otherwise, call builtin which changes non-Smi to Int32.
    gasm_->Bind(&builtin);
    CommonOperatorBuilder* common = mcgraph()->common();
    Node* target = GetTargetForBuiltinCall(Builtin::kWasmTaggedNonSmiToInt32);
    if (!tagged_non_smi_to_int32_operator_.is_set()) {
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          mcgraph()->zone(), WasmTaggedNonSmiToInt32Descriptor(), 0,
          frame_state ? CallDescriptor::kNeedsFrameState
                      : CallDescriptor::kNoFlags,
          Operator::kNoProperties, StubCallMode::kCallBuiltinPointer);
      tagged_non_smi_to_int32_operator_.set(common->Call(call_descriptor));
    }
    Node* call = frame_state
                     ? gasm_->Call(tagged_non_smi_to_int32_operator_.get(),
                                   target, value, context, frame_state)
                     : gasm_->Call(tagged_non_smi_to_int32_operator_.get(),
                                   target, value, context);
    // The source position here is needed for asm.js, see the comment on the
    // source position of the call to JavaScript in the wasm-to-js wrapper.
    SetSourcePosition(call, 1);
    gasm_->Goto(&done, call);
    gasm_->Bind(&done);
    return done.PhiAt(0);
  }

  Node* BuildChangeFloat32ToNumber(Node* value) {
    CommonOperatorBuilder* common = mcgraph()->common();
    Node* target = GetTargetForBuiltinCall(Builtin::kWasmFloat32ToNumber);
    if (!float32_to_number_operator_.is_set()) {
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          mcgraph()->zone(), WasmFloat32ToNumberDescriptor(), 0,
          CallDescriptor::kNoFlags, Operator::kNoProperties,
          StubCallMode::kCallBuiltinPointer);
      float32_to_number_operator_.set(common->Call(call_descriptor));
    }
    return gasm_->Call(float32_to_number_operator_.get(), target, value);
  }

  Node* BuildChangeFloat64ToNumber(Node* value) {
    CommonOperatorBuilder* common = mcgraph()->common();
    Node* target = GetTargetForBuiltinCall(Builtin::kWasmFloat64ToNumber);
    if (!float64_to_number_operator_.is_set()) {
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          mcgraph()->zone(), WasmFloat64ToTaggedDescriptor(), 0,
          CallDescriptor::kNoFlags, Operator::kNoProperties,
          StubCallMode::kCallBuiltinPointer);
      float64_to_number_operator_.set(common->Call(call_descriptor));
    }
    return gasm_->Call(float64_to_number_operator_.get(), target, value);
  }

  Node* BuildChangeTaggedToFloat64(Node* value, Node* context,
                                   Node* frame_state) {
    CommonOperatorBuilder* common = mcgraph()->common();
    Node* target = GetTargetForBuiltinCall(Builtin::kWasmTaggedToFloat64);
    bool needs_frame_state = frame_state != nullptr;
    if (!tagged_to_float64_operator_.is_set()) {
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          mcgraph()->zone(), WasmTaggedToFloat64Descriptor(), 0,
          frame_state ? CallDescriptor::kNeedsFrameState
                      : CallDescriptor::kNoFlags,
          Operator::kNoProperties, StubCallMode::kCallBuiltinPointer);
      tagged_to_float64_operator_.set(common->Call(call_descriptor));
    }
    Node* call = needs_frame_state
                     ? gasm_->Call(tagged_to_float64_operator_.get(), target,
                                   value, context, frame_state)
                     : gasm_->Call(tagged_to_float64_operator_.get(), target,
                                   value, context);
    // The source position here is needed for asm.js, see the comment on the
    // source position of the call to JavaScript in the wasm-to-js wrapper.
    SetSourcePosition(call, 1);
    return call;
  }

  int AddArgumentNodes(base::Vector<Node*> args, int pos, int param_count,
                       const wasm::CanonicalSig* sig, Node* context) {
    // Convert wasm numbers to JS values and drop the instance node.
    for (int i = 0; i < param_count; ++i) {
      Node* param = Param(i + 1);
      args[pos++] = ToJS(param, sig->GetParam(i), context);
    }
    return pos;
  }

  Node* ToJS(Node* node, wasm::CanonicalValueType type, Node* context) {
    switch (type.kind()) {
      case wasm::kI32:
        return BuildChangeInt32ToNumber(node);
      case wasm::kI64:
        return BuildChangeInt64ToBigInt(node,
                                        StubCallMode::kCallBuiltinPointer);
      case wasm::kF32:
        return BuildChangeFloat32ToNumber(node);
      case wasm::kF64:
        return BuildChangeFloat64ToNumber(node);
      case wasm::kRef:
        switch (type.heap_representation_non_shared()) {
          case wasm::HeapType::kEq:
          case wasm::HeapType::kI31:
          case wasm::HeapType::kStruct:
          case wasm::HeapType::kArray:
          case wasm::HeapType::kAny:
          case wasm::HeapType::kExtern:
          case wasm::HeapType::kString:
          case wasm::HeapType::kNone:
          case wasm::HeapType::kNoFunc:
          case wasm::HeapType::kNoExtern:
          case wasm::HeapType::kExn:
          case wasm::HeapType::kNoExn:
            return node;
          case wasm::HeapType::kBottom:
          case wasm::HeapType::kTop:
          case wasm::HeapType::kStringViewWtf8:
          case wasm::HeapType::kStringViewWtf16:
          case wasm::HeapType::kStringViewIter:
            UNREACHABLE();
          case wasm::HeapType::kFunc:
          default:
            if (type.heap_representation_non_shared() ==
                    wasm::HeapType::kFunc ||
                wasm::GetTypeCanonicalizer()->IsFunctionSignature(
                    type.ref_index())) {
              // Function reference. Extract the external function.
              auto done =
                  gasm_->MakeLabel(MachineRepresentation::kTaggedPointer);
              Node* internal = gasm_->LoadTrustedPointerFromObject(
                  node,
                  wasm::ObjectAccess::ToTagged(
                      WasmFuncRef::kTrustedInternalOffset),
                  kWasmInternalFunctionIndirectPointerTag);
              Node* maybe_external = gasm_->LoadFromObject(
                  MachineType::TaggedPointer(), internal,
                  wasm::ObjectAccess::ToTagged(
                      WasmInternalFunction::kExternalOffset));
              gasm_->GotoIfNot(
                  gasm_->TaggedEqual(maybe_external, UndefinedValue()), &done,
                  maybe_external);
              Node* from_builtin = gasm_->CallBuiltin(
                  Builtin::kWasmInternalFunctionCreateExternal,
                  Operator::kNoProperties, internal, context);
              gasm_->Goto(&done, from_builtin);
              gasm_->Bind(&done);
              return done.PhiAt(0);
            } else {
              return node;
            }
        }
      case wasm::kRefNull:
        switch (type.heap_representation_non_shared()) {
          case wasm::HeapType::kExtern:
          case wasm::HeapType::kNoExtern:
          case wasm::HeapType::kExn:
          case wasm::HeapType::kNoExn:
            return node;
          case wasm::HeapType::kNone:
          case wasm::HeapType::kNoFunc:
            return LOAD_ROOT(NullValue, null_value);
          case wasm::HeapType::kEq:
          case wasm::HeapType::kStruct:
          case wasm::HeapType::kArray:
          case wasm::HeapType::kString:
          case wasm::HeapType::kI31:
          case wasm::HeapType::kAny: {
            auto done = gasm_->MakeLabel(MachineRepresentation::kTaggedPointer);
            gasm_->GotoIfNot(IsNull(node, type), &done, node);
            gasm_->Goto(&done, LOAD_ROOT(NullValue, null_value));
            gasm_->Bind(&done);
            return done.PhiAt(0);
          }
          case wasm::HeapType::kFunc:
          default: {
            if (type.heap_representation_non_shared() ==
                    wasm::HeapType::kFunc ||
                wasm::GetTypeCanonicalizer()->IsFunctionSignature(
                    type.ref_index())) {
              // Function reference. Extract the external function.
              auto done =
                  gasm_->MakeLabel(MachineRepresentation::kTaggedPointer);
              auto null_label = gasm_->MakeLabel();
              gasm_->GotoIf(IsNull(node, type), &null_label);
              Node* internal = gasm_->LoadTrustedPointerFromObject(
                  node,
                  wasm::ObjectAccess::ToTagged(
                      WasmFuncRef::kTrustedInternalOffset),
                  kWasmInternalFunctionIndirectPointerTag);
              Node* maybe_external = gasm_->LoadFromObject(
                  MachineType::TaggedPointer(), internal,
                  wasm::ObjectAccess::ToTagged(
                      WasmInternalFunction::kExternalOffset));
              gasm_->GotoIfNot(
                  gasm_->TaggedEqual(maybe_external, UndefinedValue()), &done,
                  maybe_external);
              Node* from_builtin = gasm_->CallBuiltin(
                  Builtin::kWasmInternalFunctionCreateExternal,
                  Operator::kNoProperties, internal, context);
              gasm_->Goto(&done, from_builtin);
              gasm_->Bind(&null_label);
              gasm_->Goto(&done, LOAD_ROOT(NullValue, null_value));
              gasm_->Bind(&done);
              return done.PhiAt(0);
            } else {
              auto done =
                  gasm_->MakeLabel(MachineRepresentation::kTaggedPointer);
              gasm_->GotoIfNot(IsNull(node, type), &done, node);
              gasm_->Goto(&done, LOAD_ROOT(NullValue, null_value));
              gasm_->Bind(&done);
              return done.PhiAt(0);
            }
          }
        }
      case wasm::kRtt:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kS128:
      case wasm::kVoid:
      case wasm::kTop:
      case wasm::kBottom:
        // If this is reached, then IsJSCompatibleSignature() is too permissive.
        UNREACHABLE();
    }
  }

  Node* BuildChangeBigIntToInt64(Node* input, Node* context,
                                 Node* frame_state) {
    Node* target;
    if (mcgraph()->machine()->Is64()) {
      target = GetTargetForBuiltinCall(Builtin::kBigIntToI64);
    } else {
      DCHECK(mcgraph()->machine()->Is32());
      // On 32-bit platforms we already set the target to the
      // BigIntToI32Pair builtin here, so that we don't have to replace the
      // target in the int64-lowering.
      target = GetTargetForBuiltinCall(Builtin::kBigIntToI32Pair);
    }

    return frame_state ? gasm_->Call(GetBigIntToI64CallDescriptor(true), target,
                                     input, context, frame_state)
                       : gasm_->Call(GetBigIntToI64CallDescriptor(false),
                                     target, input, context);
  }

  Node* FromJS(Node* input, Node* js_context, wasm::CanonicalValueType type,
               Node* frame_state = nullptr) {
    switch (type.kind()) {
      case wasm::kRef:
      case wasm::kRefNull: {
        switch (type.heap_representation_non_shared()) {
          // TODO(14034): Add more fast paths?
          case wasm::HeapType::kExtern:
          case wasm::HeapType::kExn:
            if (type.kind() == wasm::kRef) {
              Node* null_value = gasm_->LoadImmutable(
                  MachineType::Pointer(), gasm_->LoadRootRegister(),
                  IsolateData::root_slot_offset(RootIndex::kNullValue));
              auto throw_label = gasm_->MakeDeferredLabel();
              auto done = gasm_->MakeLabel();
              gasm_->GotoIf(gasm_->TaggedEqual(input, null_value),
                            &throw_label);
              gasm_->Goto(&done);

              gasm_->Bind(&throw_label);
              BuildCallToRuntimeWithContext(Runtime::kWasmThrowJSTypeError,
                                            js_context, {}, 0);
              gasm_->Unreachable();

              gasm_->Bind(&done);
            }
            return input;
          case wasm::HeapType::kString:
            UNREACHABLE();
          case wasm::HeapType::kNoExtern:
          case wasm::HeapType::kNoExn:
          case wasm::HeapType::kNone:
          case wasm::HeapType::kNoFunc:
          case wasm::HeapType::kI31:
          case wasm::HeapType::kAny:
          case wasm::HeapType::kFunc:
          case wasm::HeapType::kStruct:
          case wasm::HeapType::kArray:
          case wasm::HeapType::kEq:
          default: {
            // Make sure CanonicalValueType fits in a Smi.
            static_assert(wasm::CanonicalValueType::kLastUsedBit + 1 <=
                          kSmiValueSize);

            Node* inputs[] = {
                input, mcgraph()->IntPtrConstant(
                           IntToSmi(static_cast<int>(type.raw_bit_field())))};

            return BuildCallToRuntimeWithContext(Runtime::kWasmJSToWasmObject,
                                                 js_context, inputs, 2);
          }
        }
      }
      case wasm::kF32:
        return gasm_->TruncateFloat64ToFloat32(
            BuildChangeTaggedToFloat64(input, js_context, frame_state));

      case wasm::kF64:
        return BuildChangeTaggedToFloat64(input, js_context, frame_state);

      case wasm::kI32:
        return BuildChangeTaggedToInt32(input, js_context, frame_state);

      case wasm::kI64:
        // i64 values can only come from BigInt.
        return BuildChangeBigIntToInt64(input, js_context, frame_state);

      case wasm::kRtt:
      case wasm::kS128:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kTop:
      case wasm::kBottom:
      case wasm::kVoid:
        // If this is reached, then IsJSCompatibleSignature() is too permissive.
        UNREACHABLE();
    }
  }

  Node* SmiToFloat32(Node* input) {
    return gasm_->RoundInt32ToFloat32(gasm_->BuildChangeSmiToInt32(input));
  }

  Node* SmiToFloat64(Node* input) {
    return gasm_->ChangeInt32ToFloat64(gasm_->BuildChangeSmiToInt32(input));
  }

  Node* HeapNumberToFloat64(Node* input) {
    return gasm_->LoadFromObject(
        MachineType::Float64(), input,
        wasm::ObjectAccess::ToTagged(
            AccessBuilder::ForHeapNumberValue().offset));
  }

  Node* FromJSFast(Node* input, wasm::CanonicalValueType type) {
    switch (type.kind()) {
      case wasm::kI32:
        return gasm_->BuildChangeSmiToInt32(input);
      case wasm::kF32: {
        auto done = gasm_->MakeLabel(MachineRepresentation::kFloat32);
        auto heap_number = gasm_->MakeLabel();
        gasm_->GotoIfNot(IsSmi(input), &heap_number);
        gasm_->Goto(&done, SmiToFloat32(input));
        gasm_->Bind(&heap_number);
        Node* value =
            gasm_->TruncateFloat64ToFloat32(HeapNumberToFloat64(input));
        gasm_->Goto(&done, value);
        gasm_->Bind(&done);
        return done.PhiAt(0);
      }
      case wasm::kF64: {
        auto done = gasm_->MakeLabel(MachineRepresentation::kFloat64);
        auto heap_number = gasm_->MakeLabel();
        gasm_->GotoIfNot(IsSmi(input), &heap_number);
        gasm_->Goto(&done, SmiToFloat64(input));
        gasm_->Bind(&heap_number);
        gasm_->Goto(&done, HeapNumberToFloat64(input));
        gasm_->Bind(&done);
        return done.PhiAt(0);
      }
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kI64:
      case wasm::kRtt:
      case wasm::kS128:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kTop:
      case wasm::kBottom:
      case wasm::kVoid:
        UNREACHABLE();
    }
  }

  class ModifyThreadInWasmFlagScope {
   public:
    ModifyThreadInWasmFlagScope(
        WasmWrapperGraphBuilder* wasm_wrapper_graph_builder,
        WasmGraphAssembler* gasm)
        : wasm_wrapper_graph_builder_(wasm_wrapper_graph_builder) {
      if (!trap_handler::IsTrapHandlerEnabled()) return;
      Node* isolate_root = wasm_wrapper_graph_builder_->BuildLoadIsolateRoot();

      thread_in_wasm_flag_address_ =
          gasm->Load(MachineType::Pointer(), isolate_root,
                     Isolate::thread_in_wasm_flag_address_offset());

      wasm_wrapper_graph_builder_->BuildModifyThreadInWasmFlagHelper(
          thread_in_wasm_flag_address_, true);
    }

    ModifyThreadInWasmFlagScope(const ModifyThreadInWasmFlagScope&) = delete;

    ~ModifyThreadInWasmFlagScope() {
      if (!trap_handler::IsTrapHandlerEnabled()) return;

      wasm_wrapper_graph_builder_->BuildModifyThreadInWasmFlagHelper(
          thread_in_wasm_flag_address_, false);
    }

   private:
    WasmWrapperGraphBuilder* wasm_wrapper_graph_builder_;
    Node* thread_in_wasm_flag_address_;
  };

  // Generate a call to the AllocateJSArray builtin.
  Node* BuildCallAllocateJSArray(Node* array_length, Node* context) {
    // Since we don't check that args will fit in an array,
    // we make sure this is true based on statically known limits.
    static_assert(wasm::kV8MaxWasmFunctionReturns <=
                  JSArray::kInitialMaxFastElementArray);
    return gasm_->CallBuiltin(Builtin::kWasmAllocateJSArray,
                              Operator::kEliminatable, array_length, context);
  }

  Node* BuildCallAndReturn(Node* js_context, Node* function_data,
                           base::SmallVector<Node*, 16> args,
                           bool do_conversion, Node* frame_state,
                           bool set_in_wasm_flag) {
    const int rets_count = static_cast<int>(wrapper_sig_->return_count());
    base::SmallVector<Node*, 1> rets(rets_count);

    // Set the ThreadInWasm flag before we do the actual call.
    {
      std::optional<ModifyThreadInWasmFlagScope>
          modify_thread_in_wasm_flag_builder;
      if (set_in_wasm_flag) {
        modify_thread_in_wasm_flag_builder.emplace(this, gasm_.get());
      }

      // Call to an import or a wasm function defined in this module.
      // The (cached) call target is the jump table slot for that function.
      // We do not use the imports dispatch table here so that the wrapper is
      // target independent, in particular for tier-up.
      Node* internal = gasm_->LoadImmutableProtectedPointerFromObject(
          function_data, wasm::ObjectAccess::ToTagged(
                             WasmFunctionData::kProtectedInternalOffset));
      args[0] = gasm_->LoadFromObject(
          MachineType::Uint32(), internal,
          wasm::ObjectAccess::ToTagged(
              WasmInternalFunction::kRawCallTargetOffset));
      Node* implicit_arg = gasm_->LoadImmutableProtectedPointerFromObject(
          internal, wasm::ObjectAccess::ToTagged(
                        WasmInternalFunction::kProtectedImplicitArgOffset));
      BuildWasmCall(wrapper_sig_, base::VectorOf(args), base::VectorOf(rets),
                    wasm::kNoCodePosition, implicit_arg, true, frame_state);
    }

    Node* jsval;
    if (wrapper_sig_->return_count() == 0) {
      jsval = UndefinedValue();
    } else if (wrapper_sig_->return_count() == 1) {
      jsval = !do_conversion
                  ? rets[0]
                  : ToJS(rets[0], wrapper_sig_->GetReturn(), js_context);
    } else {
      int32_t return_count = static_cast<int32_t>(wrapper_sig_->return_count());
      Node* size = gasm_->NumberConstant(return_count);

      jsval = BuildCallAllocateJSArray(size, js_context);

      Node* fixed_array = gasm_->LoadJSArrayElements(jsval);

      for (int i = 0; i < return_count; ++i) {
        Node* value = ToJS(rets[i], wrapper_sig_->GetReturn(i), js_context);
        gasm_->StoreFixedArrayElementAny(fixed_array, i, value);
      }
    }
    return jsval;
  }

  bool QualifiesForFastTransform(const wasm::CanonicalSig* sig) {
    const int wasm_count = static_cast<int>(sig->parameter_count());
    for (int i = 0; i < wasm_count; ++i) {
      wasm::CanonicalValueType type = sig->GetParam(i);
      switch (type.kind()) {
        case wasm::kRef:
        case wasm::kRefNull:
        case wasm::kI64:
        case wasm::kRtt:
        case wasm::kS128:
        case wasm::kI8:
        case wasm::kI16:
        case wasm::kF16:
        case wasm::kTop:
        case wasm::kBottom:
        case wasm::kVoid:
          return false;
        case wasm::kI32:
        case wasm::kF32:
        case wasm::kF64:
          break;
      }
    }
    return true;
  }

  Node* IsSmi(Node* input) {
    return gasm_->Word32Equal(
        gasm_->Word32And(gasm_->BuildTruncateIntPtrToInt32(input),
                         Int32Constant(kSmiTagMask)),
        Int32Constant(kSmiTag));
  }

  void CanTransformFast(
      Node* input, wasm::CanonicalValueType type,
      v8::internal::compiler::GraphAssemblerLabel<0>* slow_path) {
    switch (type.kind()) {
      case wasm::kI32: {
        gasm_->GotoIfNot(IsSmi(input), slow_path);
        return;
      }
      case wasm::kF32:
      case wasm::kF64: {
        auto done = gasm_->MakeLabel();
        gasm_->GotoIf(IsSmi(input), &done);
        Node* map = gasm_->LoadMap(input);
        Node* heap_number_map = LOAD_ROOT(HeapNumberMap, heap_number_map);
#if V8_MAP_PACKING
        Node* is_heap_number = gasm_->WordEqual(heap_number_map, map);
#else
        Node* is_heap_number = gasm_->TaggedEqual(heap_number_map, map);
#endif
        gasm_->GotoIf(is_heap_number, &done);
        gasm_->Goto(slow_path);
        gasm_->Bind(&done);
        return;
      }
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kI64:
      case wasm::kRtt:
      case wasm::kS128:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kTop:
      case wasm::kBottom:
      case wasm::kVoid:
        UNREACHABLE();
    }
  }

  void BuildJSToWasmWrapper(bool do_conversion = true,
                            Node* frame_state = nullptr,
                            bool set_in_wasm_flag = true) {
    const int wasm_param_count =
        static_cast<int>(wrapper_sig_->parameter_count());

    // Build the start and the JS parameter nodes.
    // TODO(saelo): this should probably be a constant with a descriptive name.
    // As far as I understand, it's the number of additional parameters in the
    // JS calling convention. Also there should be a static_assert here that it
    // matches the number of parameters in the JSTrampolineDescriptor?
    // static_assert
    Start(wasm_param_count + 6);

    // Create the js_closure and js_context parameters.
    Node* js_closure = Param(Linkage::kJSCallClosureParamIndex, "%closure");
    Node* js_context = Param(
        Linkage::GetJSCallContextParamIndex(wasm_param_count + 1), "%context");
    Node* function_data = gasm_->LoadFunctionDataFromJSFunction(js_closure);

    if (!wasm::IsJSCompatibleSignature(wrapper_sig_)) {
      // Throw a TypeError. Use the js_context of the calling javascript
      // function (passed as a parameter), such that the generated code is
      // js_context independent.
      BuildCallToRuntimeWithContext(Runtime::kWasmThrowJSTypeError, js_context,
                                    nullptr, 0);
      TerminateThrow(effect(), control());
      return;
    }

#if V8_ENABLE_DRUMBRAKE
    if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms &&
        !v8_flags.wasm_jitless) {
      Node* runtime_call = BuildCallToRuntimeWithContext(
          Runtime::kWasmTraceBeginExecution, js_context, nullptr, 0);
      SetControl(runtime_call);
    }
#endif  // V8_ENABLE_DRUMBRAKE

    const int args_count = wasm_param_count + 1;  // +1 for wasm_code.

    // Check whether the signature of the function allows for a fast
    // transformation (if any params exist that need transformation).
    // Create a fast transformation path, only if it does.
    bool include_fast_path = do_conversion && wasm_param_count > 0 &&
                             QualifiesForFastTransform(wrapper_sig_);

    // Prepare Param() nodes. Param() nodes can only be created once,
    // so we need to use the same nodes along all possible transformation paths.
    base::SmallVector<Node*, 16> params(args_count);
    for (int i = 0; i < wasm_param_count; ++i) params[i + 1] = Param(i + 1);

    auto done = gasm_->MakeLabel(MachineRepresentation::kTagged);
    if (include_fast_path) {
      auto slow_path = gasm_->MakeDeferredLabel();
      // Check if the params received on runtime can be actually transformed
      // using the fast transformation. When a param that cannot be transformed
      // fast is encountered, skip checking the rest and fall back to the slow
      // path.
      for (int i = 0; i < wasm_param_count; ++i) {
        CanTransformFast(params[i + 1], wrapper_sig_->GetParam(i), &slow_path);
      }
      // Convert JS parameters to wasm numbers using the fast transformation
      // and build the call.
      base::SmallVector<Node*, 16> args(args_count);
      for (int i = 0; i < wasm_param_count; ++i) {
        Node* wasm_param = FromJSFast(params[i + 1], wrapper_sig_->GetParam(i));
        args[i + 1] = wasm_param;
      }
      Node* jsval =
          BuildCallAndReturn(js_context, function_data, args, do_conversion,
                             frame_state, set_in_wasm_flag);

#if V8_ENABLE_DRUMBRAKE
      if (v8_flags.wasm_enable_exec_time_histograms &&
          v8_flags.slow_histograms && !v8_flags.wasm_jitless) {
        Node* runtime_call = BuildCallToRuntimeWithContext(
            Runtime::kWasmTraceEndExecution, js_context, nullptr, 0);
        SetControl(runtime_call);
      }
#endif  // V8_ENABLE_DRUMBRAKE

      gasm_->Goto(&done, jsval);
      gasm_->Bind(&slow_path);
    }
    // Convert JS parameters to wasm numbers using the default transformation
    // and build the call.
    base::SmallVector<Node*, 16> args(args_count);
    for (int i = 0; i < wasm_param_count; ++i) {
      if (do_conversion) {
        args[i + 1] = FromJS(params[i + 1], js_context,
                             wrapper_sig_->GetParam(i), frame_state);
      } else {
        Node* wasm_param = params[i + 1];

        // For Float32 parameters
        // we set UseInfo::CheckedNumberOrOddballAsFloat64 in
        // simplified-lowering and we need to add here a conversion from Float64
        // to Float32.
        if (wrapper_sig_->GetParam(i).kind() == wasm::kF32) {
          wasm_param = gasm_->TruncateFloat64ToFloat32(wasm_param);
        }

        args[i + 1] = wasm_param;
      }
    }

    Node* jsval =
        BuildCallAndReturn(js_context, function_data, args, do_conversion,
                           frame_state, set_in_wasm_flag);

#if V8_ENABLE_DRUMBRAKE
    if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms &&
        !v8_flags.wasm_jitless) {
      Node* runtime_call = BuildCallToRuntimeWithContext(
          Runtime::kWasmTraceEndExecution, js_context, nullptr, 0);
      SetControl(runtime_call);
    }
#endif  // V8_ENABLE_DRUMBRAKE

    // If both the default and a fast transformation paths are present,
    // get the return value based on the path used.
    if (include_fast_path) {
      gasm_->Goto(&done, jsval);
      gasm_->Bind(&done);
      Return(done.PhiAt(0));
    } else {
      Return(jsval);
    }
    if (ContainsInt64(wrapper_sig_)) LowerInt64(wasm::kCalledFromJS);
  }

  Node* BuildReceiverNode(Node* callable_node, Node* native_context,
                          Node* undefined_node) {
    // Check function strict bit.
    Node* shared_function_info = gasm_->LoadSharedFunctionInfo(callable_node);
    Node* flags = gasm_->LoadFromObject(
        MachineType::Int32(), shared_function_info,
        wasm::ObjectAccess::FlagsOffsetInSharedFunctionInfo());
    Node* strict_check = gasm_->Word32And(
        flags, Int32Constant(SharedFunctionInfo::IsNativeBit::kMask |
                             SharedFunctionInfo::IsStrictBit::kMask));

    // Load global receiver if sloppy else use undefined.
    Diamond strict_d(graph(), mcgraph()->common(), strict_check,
                     BranchHint::kNone);
    Node* old_effect = effect();
    SetControl(strict_d.if_false);
    Node* global_proxy = gasm_->LoadFixedArrayElementPtr(
        native_context, Context::GLOBAL_PROXY_INDEX);
    SetEffectControl(strict_d.EffectPhi(old_effect, global_proxy),
                     strict_d.merge);
    return strict_d.Phi(MachineRepresentation::kTagged, undefined_node,
                        global_proxy);
  }

  void BuildJSFastApiCallWrapper(DirectHandle<JSReceiver> callable) {
    // Here 'callable_node' must be equal to 'callable' but we cannot pass a
    // HeapConstant(callable) because WasmCode::Validate() fails with
    // Unexpected mode: FULL_EMBEDDED_OBJECT.
    Node* callable_node = gasm_->Load(
        MachineType::TaggedPointer(), Param(0),
        wasm::ObjectAccess::ToTagged(WasmImportData::kCallableOffset));
    Node* native_context = gasm_->Load(
        MachineType::TaggedPointer(), Param(0),
        wasm::ObjectAccess::ToTagged(WasmImportData::kNativeContextOffset));

    gasm_->Store(StoreRepresentation(mcgraph_->machine()->Is64()
                                         ? MachineRepresentation::kWord64
                                         : MachineRepresentation::kWord32,
                                     WriteBarrierKind::kNoWriteBarrier),
                 gasm_->LoadRootRegister(), Isolate::context_offset(),
                 gasm_->BitcastMaybeObjectToWord(native_context));

    Node* undefined_node = UndefinedValue();

    BuildModifyThreadInWasmFlag(false);

    DirectHandle<JSFunction> target;
    Node* target_node;
    Node* receiver_node;
    Isolate* isolate = callable->GetIsolate();
    if (IsJSBoundFunction(*callable)) {
      target = direct_handle(
          Cast<JSFunction>(
              Cast<JSBoundFunction>(callable)->bound_target_function()),
          isolate);
      target_node =
          gasm_->Load(MachineType::TaggedPointer(), callable_node,
                      wasm::ObjectAccess::ToTagged(
                          JSBoundFunction::kBoundTargetFunctionOffset));
      receiver_node = gasm_->Load(
          MachineType::TaggedPointer(), callable_node,
          wasm::ObjectAccess::ToTagged(JSBoundFunction::kBoundThisOffset));
    } else {
      DCHECK(IsJSFunction(*callable));
      target = Cast<JSFunction>(callable);
      target_node = callable_node;
      receiver_node =
          BuildReceiverNode(callable_node, native_context, undefined_node);
    }

    Tagged<SharedFunctionInfo> shared = target->shared();
    Tagged<FunctionTemplateInfo> api_func_data = shared->api_func_data();
    const Address c_address = api_func_data->GetCFunction(isolate, 0);
    const v8::CFunctionInfo* c_signature =
        api_func_data->GetCSignature(target->GetIsolate(), 0);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
    Address c_functions[] = {c_address};
    const v8::CFunctionInfo* const c_signatures[] = {c_signature};
    target->GetIsolate()->simulator_data()->RegisterFunctionsAndSignatures(
        c_functions, c_signatures, 1);
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

    Node* shared_function_info = gasm_->LoadSharedFunctionInfo(target_node);
    Node* function_template_info =
        gasm_->Load(MachineType::TaggedPointer(), shared_function_info,
                    wasm::ObjectAccess::ToTagged(
                        SharedFunctionInfo::kUntrustedFunctionDataOffset));
    Node* api_data_argument =
        gasm_->Load(MachineType::TaggedPointer(), function_template_info,
                    wasm::ObjectAccess::ToTagged(
                        FunctionTemplateInfo::kCallbackDataOffset));

    FastApiCallFunction c_function{c_address, c_signature};
    Node* call = fast_api_call::BuildFastApiCall(
        target->GetIsolate(), graph(), gasm_.get(), c_function,
        api_data_argument,
        // Load and convert parameters passed to C function
        [this, c_signature, receiver_node](int param_index,
                                           GraphAssemblerLabel<0>*) {
          if (param_index == 0) {
            return gasm_->AdaptLocalArgument(receiver_node);
          }
          switch (c_signature->ArgumentInfo(param_index).GetType()) {
            case CTypeInfo::Type::kV8Value:
              return gasm_->AdaptLocalArgument(Param(param_index));
            default:
              return Param(param_index);
          }
        },
        // Convert return value (no conversion needed for wasm)
        [](const CFunctionInfo* signature, Node* c_return_value) {
          return c_return_value;
        },
        [](Node* options_stack_slot) {},
        // Generate fallback slow call if fast call fails
        [this, callable_node, native_context, receiver_node]() -> Node* {
          int wasm_count = static_cast<int>(wrapper_sig_->parameter_count());
          base::SmallVector<Node*, 16> args(wasm_count + 7);
          int pos = 0;
          args[pos++] =
              gasm_->GetBuiltinPointerTarget(Builtin::kCall_ReceiverIsAny);
          args[pos++] = callable_node;
          args[pos++] =
              Int32Constant(JSParameterCount(wasm_count));  // argument count
          args[pos++] = receiver_node;                      // receiver

          auto call_descriptor = Linkage::GetStubCallDescriptor(
              graph()->zone(), CallTrampolineDescriptor{}, wasm_count + 1,
              CallDescriptor::kNoFlags, Operator::kNoProperties,
              StubCallMode::kCallBuiltinPointer);

          // Convert wasm numbers to JS values.
          pos = AddArgumentNodes(base::VectorOf(args), pos, wasm_count,
                                 wrapper_sig_, native_context);

          // The native_context is sufficient here, because all kind of
          // callables which depend on the context provide their own context.
          // The context here is only needed if the target is a constructor to
          // throw a TypeError, if the target is a native function, or if the
          // target is a callable JSObject, which can only be constructed by the
          // runtime.
          args[pos++] = native_context;
          args[pos++] = effect();
          args[pos++] = control();

          DCHECK_EQ(pos, args.size());
          Node* call = gasm_->Call(call_descriptor, pos, args.begin());
          return wrapper_sig_->return_count() == 0
                     ? Int32Constant(0)
                     : FromJS(call, native_context, wrapper_sig_->GetReturn(),
                              nullptr);
        });

    BuildModifyThreadInWasmFlag(true);

    Return(call);
  }

  void BuildCWasmEntry() {
    // +1 offset for first parameter index being -1.
    Start(CWasmEntryParameters::kNumParameters + 1);

    Node* code_entry = Param(CWasmEntryParameters::kCodeEntry);
    Node* object_ref = Param(CWasmEntryParameters::kObjectRef);
    Node* arg_buffer = Param(CWasmEntryParameters::kArgumentsBuffer);
    Node* c_entry_fp = Param(CWasmEntryParameters::kCEntryFp);

    Node* fp_value = graph()->NewNode(mcgraph()->machine()->LoadFramePointer());
    gasm_->Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                     kNoWriteBarrier),
                 fp_value, TypedFrameConstants::kFirstPushedFrameValueOffset,
                 c_entry_fp);

    int wasm_arg_count = static_cast<int>(wrapper_sig_->parameter_count());
    base::SmallVector<Node*, 16> args(wasm_arg_count + 4);

    int pos = 0;
    args[pos++] = code_entry;
    args[pos++] = gasm_->LoadTrustedDataFromInstanceObject(object_ref);

    int offset = 0;
    for (wasm::CanonicalValueType type : wrapper_sig_->parameters()) {
      Node* arg_load = SetEffect(
          graph()->NewNode(GetSafeLoadOperator(offset, type), arg_buffer,
                           Int32Constant(offset), effect(), control()));
      args[pos++] = arg_load;
      offset += type.value_kind_size();
    }

    args[pos++] = effect();
    args[pos++] = control();

    // Call the wasm code.
    auto call_descriptor = GetWasmCallDescriptor(
        mcgraph()->zone(), wrapper_sig_, WasmCallKind::kWasmIndirectFunction);

    DCHECK_EQ(pos, args.size());
    Node* call = gasm_->Call(call_descriptor, pos, args.begin());

    Node* if_success = graph()->NewNode(mcgraph()->common()->IfSuccess(), call);
    Node* if_exception =
        graph()->NewNode(mcgraph()->common()->IfException(), call, call);

    // Handle exception: return it.
    SetEffectControl(if_exception);
    Return(if_exception);

    // Handle success: store the return value(s).
    SetEffectControl(call, if_success);
    pos = 0;
    offset = 0;
    for (wasm::CanonicalValueType type : wrapper_sig_->returns()) {
      Node* value = wrapper_sig_->return_count() == 1
                        ? call
                        : graph()->NewNode(mcgraph()->common()->Projection(pos),
                                           call, control());
      SetEffect(
          BuildSafeStore(offset, type, arg_buffer, value, effect(), control()));
      offset += type.value_kind_size();
      pos++;
    }

    Return(mcgraph()->IntPtrConstant(0));

    if (mcgraph()->machine()->Is32() && ContainsInt64(wrapper_sig_)) {
      // These correspond to {sig_types[]} in {CompileCWasmEntry}.
      MachineRepresentation sig_reps[] = {
          MachineType::PointerRepresentation(),  // return value
          MachineType::PointerRepresentation(),  // target
          MachineRepresentation::kTagged,        // object_ref
          MachineType::PointerRepresentation(),  // argv
          MachineType::PointerRepresentation()   // c_entry_fp
      };
      Signature<MachineRepresentation> c_entry_sig(1, 4, sig_reps);
      Int64Lowering r(mcgraph()->graph(), mcgraph()->machine(),
                      mcgraph()->common(), gasm_->simplified(),
                      mcgraph()->zone(), &c_entry_sig);
      r.LowerGraph();
    }
  }

 private:
  SetOncePointer<const Operator> int32_to_heapnumber_operator_;
  SetOncePointer<const Operator> tagged_non_smi_to_int32_operator_;
  SetOncePointer<const Operator> float32_to_number_operator_;
  SetOncePointer<const Operator> float64_to_number_operator_;
  SetOncePointer<const Operator> tagged_to_float64_operator_;
};

}  // namespace

void BuildInlinedJSToWasmWrapper(Zone* zone, MachineGraph* mcgraph,
                                 const wasm::CanonicalSig* signature,
                                 Isolate* isolate,
                                 compiler::SourcePositionTable* spt,
                                 Node* frame_state, bool set_in_wasm_flag) {
  WasmWrapperGraphBuilder builder(zone, mcgraph, signature,
                                  WasmGraphBuilder::kJSFunctionAbiMode, isolate,
                                  spt);
  builder.BuildJSToWasmWrapper(false, frame_state, set_in_wasm_flag);
}

std::unique_ptr<OptimizedCompilationJob> NewJSToWasmCompilationJob(
    Isolate* isolate, const wasm::CanonicalSig* sig) {
  return Pipeline::NewWasmTurboshaftWrapperCompilationJob(
      isolate, sig, wasm::WrapperCompilationInfo{CodeKind::JS_TO_WASM_FUNCTION},
      WasmExportedFunction::GetDebugName(sig), WasmAssemblerOptions());
}

namespace {

MachineGraph* CreateCommonMachineGraph(Zone* zone) {
  return zone->New<MachineGraph>(
      zone->New<Graph>(zone), zone->New<CommonOperatorBuilder>(zone),
      zone->New<MachineOperatorBuilder>(
          zone, MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements()));
}

}  // namespace

wasm::WasmCompilationResult CompileWasmImportCallWrapper(
    wasm::ImportCallKind kind, const wasm::CanonicalSig* sig,
    bool source_positions, int expected_arity, wasm::Suspend suspend) {
  DCHECK_NE(wasm::ImportCallKind::kLinkError, kind);
  DCHECK_NE(wasm::ImportCallKind::kWasmToWasm, kind);
  DCHECK_NE(wasm::ImportCallKind::kWasmToJSFastApi, kind);

  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileWasmImportCallWrapper");
  base::TimeTicks start_time;
  if (V8_UNLIKELY(v8_flags.trace_wasm_compilation_times)) {
    start_time = base::TimeTicks::Now();
  }

  // Build a name in the form "wasm-to-js-<kind>-<signature>".
  constexpr size_t kMaxNameLen = 128;
  char func_name[kMaxNameLen];
  int name_prefix_len = SNPrintF(base::VectorOf(func_name, kMaxNameLen),
                                 "wasm-to-js-%d-", static_cast<int>(kind));
  PrintSignature(base::VectorOf(func_name, kMaxNameLen) + name_prefix_len, sig,
                 '-');

  auto result = Pipeline::GenerateCodeForWasmNativeStubFromTurboshaft(
      sig,
      wasm::WrapperCompilationInfo{CodeKind::WASM_TO_JS_FUNCTION, kind,
                                   expected_arity, suspend},
      func_name, WasmStubAssemblerOptions(), nullptr);

  if (V8_UNLIKELY(v8_flags.trace_wasm_compilation_times)) {
    base::TimeDelta time = base::TimeTicks::Now() - start_time;
    int codesize = result.code_desc.body_size();
    StdoutStream{} << "Compiled WasmToJS wrapper " << func_name << ", took "
                   << time.InMilliseconds() << " ms; codesize " << codesize
                   << std::endl;
  }

  return result;
}

wasm::WasmCompilationResult CompileWasmCapiCallWrapper(
    const wasm::CanonicalSig* sig) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileWasmCapiFunction");

  return Pipeline::GenerateCodeForWasmNativeStubFromTurboshaft(
      sig, wasm::WrapperCompilationInfo{CodeKind::WASM_TO_CAPI_FUNCTION},
      "WasmCapiCall", WasmStubAssemblerOptions(), nullptr);
}

bool IsFastCallSupportedSignature(const v8::CFunctionInfo* sig) {
  return fast_api_call::CanOptimizeFastSignature(sig);
}

wasm::WasmCompilationResult CompileWasmJSFastCallWrapper(
    const wasm::CanonicalSig* sig, DirectHandle<JSReceiver> callable) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileWasmJSFastCallWrapper");

  Zone zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME, kCompressGraphZone);
  SourcePositionTable* source_positions = nullptr;
  MachineGraph* mcgraph = CreateCommonMachineGraph(&zone);

  WasmWrapperGraphBuilder builder(&zone, mcgraph, sig,
                                  WasmGraphBuilder::kWasmImportDataMode,
                                  nullptr, source_positions);

  // Set up the graph start.
  int param_count = static_cast<int>(sig->parameter_count()) +
                    1 /* offset for first parameter index being -1 */ +
                    1 /* Wasm instance */ + 1 /* kExtraCallableParam */;
  builder.Start(param_count);
  builder.BuildJSFastApiCallWrapper(callable);

  // Run the compiler pipeline to generate machine code.
  CallDescriptor* call_descriptor =
      GetWasmCallDescriptor(&zone, sig, WasmCallKind::kWasmImportWrapper);
  if (mcgraph->machine()->Is32()) {
    call_descriptor = GetI32WasmCallDescriptor(&zone, call_descriptor);
  }

  const char* debug_name = "WasmJSFastApiCall";
  wasm::WasmCompilationResult result = Pipeline::GenerateCodeForWasmNativeStub(
      call_descriptor, mcgraph, CodeKind::WASM_TO_JS_FUNCTION, debug_name,
      WasmStubAssemblerOptions(), source_positions);
  return result;
}

Handle<Code> CompileCWasmEntry(Isolate* isolate,
                               const wasm::CanonicalSig* sig) {
  DCHECK(!v8_flags.wasm_jitless);

  std::unique_ptr<Zone> zone = std::make_unique<Zone>(
      isolate->allocator(), ZONE_NAME, kCompressGraphZone);
  Graph* graph = zone->New<Graph>(zone.get());
  CommonOperatorBuilder* common = zone->New<CommonOperatorBuilder>(zone.get());
  MachineOperatorBuilder* machine = zone->New<MachineOperatorBuilder>(
      zone.get(), MachineType::PointerRepresentation(),
      InstructionSelector::SupportedMachineOperatorFlags(),
      InstructionSelector::AlignmentRequirements());
  MachineGraph* mcgraph = zone->New<MachineGraph>(graph, common, machine);

  WasmWrapperGraphBuilder builder(zone.get(), mcgraph, sig,
                                  WasmGraphBuilder::kNoSpecialParameterMode,
                                  nullptr, nullptr);
  builder.BuildCWasmEntry();

  // Schedule and compile to machine code.
  MachineType sig_types[] = {MachineType::Pointer(),    // return
                             MachineType::Pointer(),    // target
                             MachineType::AnyTagged(),  // object_ref
                             MachineType::Pointer(),    // argv
                             MachineType::Pointer()};   // c_entry_fp
  MachineSignature incoming_sig(1, 4, sig_types);
  // Traps need the root register, for TailCallRuntime to call
  // Runtime::kThrowWasmError.
  CallDescriptor::Flags flags = CallDescriptor::kInitializeRootRegister;
  CallDescriptor* incoming =
      Linkage::GetSimplifiedCDescriptor(zone.get(), &incoming_sig, flags);

  // Build a name in the form "c-wasm-entry:<params>:<returns>".
  constexpr size_t kMaxNameLen = 128;
  constexpr size_t kNamePrefixLen = 13;
  auto name_buffer = std::unique_ptr<char[]>(new char[kMaxNameLen]);
  memcpy(name_buffer.get(), "c-wasm-entry:", kNamePrefixLen);
  PrintSignature(
      base::VectorOf(name_buffer.get(), kMaxNameLen) + kNamePrefixLen, sig);

  // Run the compilation job synchronously.
  std::unique_ptr<TurbofanCompilationJob> job(
      Pipeline::NewWasmHeapStubCompilationJob(
          isolate, incoming, std::move(zone), graph, CodeKind::C_WASM_ENTRY,
          std::move(name_buffer), AssemblerOptions::Default(isolate)));

  CHECK_NE(job->ExecuteJob(isolate->counters()->runtime_call_stats(), nullptr),
           CompilationJob::FAILED);
  CHECK_NE(job->FinalizeJob(isolate), CompilationJob::FAILED);

  return job->compilation_info()->code();
}

AssemblerOptions WasmAssemblerOptions() {
  return AssemblerOptions{
      // Relocation info required to serialize {WasmCode} for proper functions.
      .record_reloc_info_for_serialization = true,
      .enable_root_relative_access = false,
      .is_wasm = true,
  };
}

AssemblerOptions WasmStubAssemblerOptions() {
  return AssemblerOptions{
      // Relocation info not necessary because stubs are not serialized.
      .record_reloc_info_for_serialization = false,
      .enable_root_relative_access = false,
      // TODO(jkummerow): Would it be better to have a far jump table in
      // the wrapper cache's code space, and call builtins through that?
      .builtin_call_jump_mode = BuiltinCallJumpMode::kIndirect,
      .is_wasm = true,
  };
}

#undef LOAD_ROOT

}  // namespace v8::internal::compiler
