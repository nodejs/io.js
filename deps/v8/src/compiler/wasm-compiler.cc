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
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/graph.h"
#include "src/compiler/int64-lowering.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/pipeline.h"
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
#include "src/wasm/graph-builder-interface.h"
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

namespace v8 {
namespace internal {
namespace compiler {

namespace {

constexpr MachineType kMaybeSandboxedPointer =
    V8_ENABLE_SANDBOX_BOOL ? MachineType::SandboxedPointer()
                           : MachineType::Pointer();

#define FATAL_UNSUPPORTED_OPCODE(opcode)        \
  FATAL("Unsupported opcode 0x%x:%s", (opcode), \
        wasm::WasmOpcodes::OpcodeName(opcode));

MachineType assert_size(int expected_size, MachineType type) {
  DCHECK_EQ(expected_size, ElementSizeInBytes(type.representation()));
  return type;
}

#define WASM_INSTANCE_OBJECT_SIZE(name)          \
  (WasmTrustedInstanceData::k##name##OffsetEnd - \
   WasmTrustedInstanceData::k##name##Offset + 1)  // NOLINT(whitespace/indent)

#define LOAD_MUTABLE_INSTANCE_FIELD(name, type)                              \
  gasm_->LoadFromObject(                                                     \
      assert_size(WASM_INSTANCE_OBJECT_SIZE(name), type), GetInstanceData(), \
      wasm::ObjectAccess::ToTagged(WasmTrustedInstanceData::k##name##Offset))

#define LOAD_INSTANCE_FIELD(name, type)                                      \
  gasm_->LoadImmutable(                                                      \
      assert_size(WASM_INSTANCE_OBJECT_SIZE(name), type), GetInstanceData(), \
      wasm::ObjectAccess::ToTagged(WasmTrustedInstanceData::k##name##Offset))

#define LOAD_PROTECTED_INSTANCE_FIELD(name) \
  gasm_->LoadProtectedPointerFromObject(    \
      GetInstanceData(),                    \
      wasm::ObjectAccess::ToTagged(         \
          WasmTrustedInstanceData::kProtected##name##Offset));

#define LOAD_IMMUTABLE_PROTECTED_INSTANCE_FIELD(name) \
  gasm_->LoadImmutableProtectedPointerFromObject(     \
      GetInstanceData(),                              \
      wasm::ObjectAccess::ToTagged(                   \
          WasmTrustedInstanceData::kProtected##name##Offset));

#define LOAD_INSTANCE_FIELD_NO_ELIMINATION(name, type)                       \
  gasm_->Load(                                                               \
      assert_size(WASM_INSTANCE_OBJECT_SIZE(name), type), GetInstanceData(), \
      wasm::ObjectAccess::ToTagged(WasmTrustedInstanceData::k##name##Offset))

// Use MachineType::Pointer() over Tagged() to load root pointers because they
// do not get compressed.
#define LOAD_ROOT(RootName, factory_name)                         \
  (isolate_ ? graph()->NewNode(mcgraph()->common()->HeapConstant( \
                  isolate_->factory()->factory_name()))           \
            : gasm_->LoadImmutable(                               \
                  MachineType::Pointer(), BuildLoadIsolateRoot(), \
                  IsolateData::root_slot_offset(RootIndex::k##RootName)))

#define LOAD_MUTABLE_ROOT(RootName, factory_name)                 \
  (isolate_ ? graph()->NewNode(mcgraph()->common()->HeapConstant( \
                  isolate_->factory()->factory_name()))           \
            : gasm_->BitcastWordToTagged(gasm_->Load(             \
                  MachineType::Pointer(), BuildLoadIsolateRoot(), \
                  IsolateData::root_slot_offset(RootIndex::k##RootName))))

bool ContainsSimd(const wasm::FunctionSig* sig) {
  for (auto type : sig->all()) {
    if (type == wasm::kWasmS128) return true;
  }
  return false;
}

bool ContainsInt64(const wasm::FunctionSig* sig) {
  for (auto type : sig->all()) {
    if (type == wasm::kWasmI64) return true;
  }
  return false;
}

}  // namespace

WasmGraphBuilder::WasmGraphBuilder(
    wasm::CompilationEnv* env, Zone* zone, MachineGraph* mcgraph,
    const wasm::FunctionSig* sig,
    compiler::SourcePositionTable* source_position_table,
    ParameterMode parameter_mode, Isolate* isolate,
    wasm::WasmEnabledFeatures enabled_features)
    : gasm_(std::make_unique<WasmGraphAssembler>(mcgraph, zone)),
      zone_(zone),
      mcgraph_(mcgraph),
      env_(env),
      enabled_features_(enabled_features),
      has_simd_(ContainsSimd(sig)),
      sig_(sig),
      source_position_table_(source_position_table),
      parameter_mode_(parameter_mode),
      isolate_(isolate),
      null_check_strategy_(trap_handler::IsTrapHandlerEnabled() &&
                                   V8_STATIC_ROOTS_BOOL
                               ? NullCheckStrategy::kTrapHandler
                               : NullCheckStrategy::kExplicit) {
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
  bool is_shared = module->types[inlinee.sig_index].is_shared;
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
      Node* param = Param(wasm::kWasmInstanceParameterIndex);
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

Node* WasmGraphBuilder::Loop(Node* entry) {
  return graph()->NewNode(mcgraph()->common()->Loop(1), entry);
}

void WasmGraphBuilder::TerminateLoop(Node* effect, Node* control) {
  Node* terminate =
      graph()->NewNode(mcgraph()->common()->Terminate(), effect, control);
  gasm_->MergeControlToEnd(terminate);
}

Node* WasmGraphBuilder::LoopExit(Node* loop_node) {
  DCHECK(loop_node->opcode() == IrOpcode::kLoop);
  Node* loop_exit =
      graph()->NewNode(mcgraph()->common()->LoopExit(), control(), loop_node);
  Node* loop_exit_effect = graph()->NewNode(
      mcgraph()->common()->LoopExitEffect(), effect(), loop_exit);
  SetEffectControl(loop_exit_effect, loop_exit);
  return loop_exit;
}

Node* WasmGraphBuilder::LoopExitValue(Node* value,
                                      MachineRepresentation representation) {
  DCHECK_EQ(control()->opcode(), IrOpcode::kLoopExit);
  return graph()->NewNode(mcgraph()->common()->LoopExitValue(representation),
                          value, control());
}

void WasmGraphBuilder::TerminateThrow(Node* effect, Node* control) {
  Node* terminate =
      graph()->NewNode(mcgraph()->common()->Throw(), effect, control);
  gasm_->MergeControlToEnd(terminate);
  gasm_->InitializeEffectControl(nullptr, nullptr);
}

bool WasmGraphBuilder::IsPhiWithMerge(Node* phi, Node* merge) {
  return phi && IrOpcode::IsPhiOpcode(phi->opcode()) &&
         NodeProperties::GetControlInput(phi) == merge;
}

bool WasmGraphBuilder::ThrowsException(Node* node, Node** if_success,
                                       Node** if_exception) {
  if (node->op()->HasProperty(compiler::Operator::kNoThrow)) {
    return false;
  }

  *if_success = graph()->NewNode(mcgraph()->common()->IfSuccess(), node);
  *if_exception =
      graph()->NewNode(mcgraph()->common()->IfException(), node, node);

  return true;
}

void WasmGraphBuilder::AppendToMerge(Node* merge, Node* from) {
  DCHECK(IrOpcode::IsMergeOpcode(merge->opcode()));
  merge->AppendInput(mcgraph()->zone(), from);
  int new_size = merge->InputCount();
  NodeProperties::ChangeOp(
      merge, mcgraph()->common()->ResizeMergeOrPhi(merge->op(), new_size));
}

void WasmGraphBuilder::AppendToPhi(Node* phi, Node* from) {
  DCHECK(IrOpcode::IsPhiOpcode(phi->opcode()));
  int new_size = phi->InputCount();
  phi->InsertInput(mcgraph()->zone(), phi->InputCount() - 1, from);
  NodeProperties::ChangeOp(
      phi, mcgraph()->common()->ResizeMergeOrPhi(phi->op(), new_size));
}

template <typename... Nodes>
Node* WasmGraphBuilder::Merge(Node* fst, Nodes*... args) {
  return graph()->NewNode(this->mcgraph()->common()->Merge(1 + sizeof...(args)),
                          fst, args...);
}

Node* WasmGraphBuilder::Merge(unsigned count, Node** controls) {
  return graph()->NewNode(mcgraph()->common()->Merge(count), count, controls);
}

Node* WasmGraphBuilder::Phi(wasm::ValueType type, unsigned count,
                            Node** vals_and_control) {
  DCHECK(IrOpcode::IsMergeOpcode(vals_and_control[count]->opcode()));
  DCHECK_EQ(vals_and_control[count]->op()->ControlInputCount(), count);
  return graph()->NewNode(
      mcgraph()->common()->Phi(type.machine_representation(), count), count + 1,
      vals_and_control);
}

Node* WasmGraphBuilder::EffectPhi(unsigned count, Node** effects_and_control) {
  DCHECK(IrOpcode::IsMergeOpcode(effects_and_control[count]->opcode()));
  return graph()->NewNode(mcgraph()->common()->EffectPhi(count), count + 1,
                          effects_and_control);
}

Node* WasmGraphBuilder::RefNull(wasm::ValueType type) {
  // We immediately lower null in wrappers, as they do not go through a lowering
  // phase.
  // TODO(thibaudm): Can we use wasm null for exnref?
  return parameter_mode_ == kInstanceParameterMode ? gasm_->Null(type)
         : (type == wasm::kWasmExternRef || type == wasm::kWasmNullExternRef ||
            type == wasm::kWasmExnRef || type == wasm::kWasmNullExnRef)
             ? LOAD_ROOT(NullValue, null_value)
             : LOAD_ROOT(WasmNull, wasm_null);
}

Node* WasmGraphBuilder::RefFunc(uint32_t function_index) {
  Node* func_refs = LOAD_INSTANCE_FIELD(FuncRefs, MachineType::TaggedPointer());
  Node* maybe_function =
      gasm_->LoadFixedArrayElementPtr(func_refs, function_index);
  auto done = gasm_->MakeLabel(MachineRepresentation::kTaggedPointer);
  auto create_funcref = gasm_->MakeDeferredLabel();
  // We only care to distinguish between zero and funcref, "IsI31" is close
  // enough.
  gasm_->GotoIf(gasm_->IsSmi(maybe_function), &create_funcref);
  gasm_->Goto(&done, maybe_function);

  gasm_->Bind(&create_funcref);
  Node* function_from_builtin = gasm_->CallBuiltinThroughJumptable(
      Builtin::kWasmRefFunc, Operator::kNoThrow,
      gasm_->Uint32Constant(function_index), gasm_->Int32Constant(0));
  gasm_->Goto(&done, function_from_builtin);

  gasm_->Bind(&done);
  return done.PhiAt(0);
}

Node* WasmGraphBuilder::NoContextConstant() {
  return mcgraph()->IntPtrConstant(0);
}

Node* WasmGraphBuilder::GetInstanceData() { return instance_data_node_.get(); }

Node* WasmGraphBuilder::BuildLoadIsolateRoot() {
  return isolate_ ? mcgraph()->IntPtrConstant(isolate_->isolate_root())
                  : gasm_->LoadRootRegister();
}

Node* WasmGraphBuilder::TraceInstruction(uint32_t mark_id) {
  const Operator* op = mcgraph()->machine()->TraceInstruction(mark_id);
  Node* node = SetEffect(graph()->NewNode(op, effect(), control()));
  return node;
}

Node* WasmGraphBuilder::Int32Constant(int32_t value) {
  return mcgraph()->Int32Constant(value);
}

Node* WasmGraphBuilder::Int64Constant(int64_t value) {
  return mcgraph()->Int64Constant(value);
}

Node* WasmGraphBuilder::UndefinedValue() {
  return LOAD_ROOT(UndefinedValue, undefined_value);
}

void WasmGraphBuilder::StackCheck(
    WasmInstanceCacheNodes* shared_memory_instance_cache,
    wasm::WasmCodePosition position) {
  DCHECK_NOT_NULL(env_);  // Wrappers don't get stack checks.
  if (!v8_flags.wasm_stack_checks) return;

  Node* limit =
      gasm_->Load(MachineType::Pointer(), gasm_->LoadRootRegister(),
                  mcgraph()->IntPtrConstant(IsolateData::jslimit_offset()));

  Node* check = SetEffect(graph()->NewNode(
      mcgraph()->machine()->StackPointerGreaterThan(StackCheckKind::kWasm),
      limit, effect()));

  auto [if_true, if_false] = BranchExpectTrue(check);

  if (stack_check_call_operator_ == nullptr) {
    // Build and cache the stack check call operator and the constant
    // representing the stack check code.

    // A direct call to a wasm runtime stub defined in this module.
    // Just encode the stub index. This will be patched at relocation.
    stack_check_code_node_.set(
        mcgraph()->RelocatableWasmBuiltinCallTarget(Builtin::kWasmStackGuard));

    constexpr Operator::Properties properties =
        Operator::kNoThrow | Operator::kNoWrite;
    // If we ever want to mark this call as kNoDeopt, we'll have to make it
    // non-eliminatable some other way.
    static_assert((properties & Operator::kEliminatable) !=
                  Operator::kEliminatable);
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        mcgraph()->zone(),                    // zone
        NoContextDescriptor{},                // descriptor
        0,                                    // stack parameter count
        CallDescriptor::kNoFlags,             // flags
        properties,                           // properties
        StubCallMode::kCallWasmRuntimeStub);  // stub call mode
    stack_check_call_operator_ = mcgraph()->common()->Call(call_descriptor);
  }

  Node* call =
      graph()->NewNode(stack_check_call_operator_.get(),
                       stack_check_code_node_.get(), effect(), if_false);
  SetSourcePosition(call, position);

  DCHECK_GT(call->op()->EffectOutputCount(), 0);
  DCHECK_EQ(call->op()->ControlOutputCount(), 0);

  SetEffectControl(call, if_false);

  // We only need to refresh the size of a shared memory, as its start can never
  // change.
  // We handle caching of the instance cache nodes manually, and we may reload
  // them in contexts where load elimination would eliminate the reload.
  // Therefore, we use plain Load nodes which are not subject to load
  // elimination.
  DCHECK_IMPLIES(shared_memory_instance_cache, has_cached_memory());
  Node* new_memory_size = shared_memory_instance_cache == nullptr
                              ? nullptr
                              : LoadMemSize(cached_memory_index_);

  Node* merge = Merge(if_true, control());
  Node* ephi_inputs[] = {check, effect(), merge};
  Node* ephi = EffectPhi(2, ephi_inputs);

  if (shared_memory_instance_cache != nullptr) {
    shared_memory_instance_cache->mem_size = CreateOrMergeIntoPhi(
        MachineType::PointerRepresentation(), merge,
        shared_memory_instance_cache->mem_size, new_memory_size);
  }

  SetEffectControl(ephi, merge);
}

void WasmGraphBuilder::PatchInStackCheckIfNeeded() {
  if (!needs_stack_check_) return;

  Node* start = graph()->start();
  // Place a stack check which uses a dummy node as control and effect.
  Node* dummy = graph()->NewNode(mcgraph()->common()->Dead());
  SetEffectControl(dummy);
  // The function-prologue stack check is associated with position 0, which
  // is never a position of any instruction in the function.
  // We pass the null instance cache, as we are at the beginning of the function
  // and do not need to update it.
  StackCheck(nullptr, 0);

  // In testing, no stack checks were emitted. Nothing to rewire then.
  if (effect() == dummy) return;

  // Now patch all control uses of {start} to use {control} and all effect uses
  // to use {effect} instead. We exclude Projection nodes: Projections pointing
  // to start are floating control, and we want it to point directly to start
  // because of restrictions later in the pipeline (specifically, loop
  // unrolling).
  // Then rewire the dummy node to use start instead.
  NodeProperties::ReplaceUses(start, start, effect(), control());
  {
    // We need an intermediate vector because we are not allowed to modify a use
    // while traversing uses().
    std::vector<Node*> projections;
    for (Node* use : control()->uses()) {
      if (use->opcode() == IrOpcode::kProjection) projections.emplace_back(use);
    }
    for (Node* use : projections) {
      use->ReplaceInput(NodeProperties::FirstControlIndex(use), start);
    }
  }
  NodeProperties::ReplaceUses(dummy, nullptr, start, start);
}

Node* WasmGraphBuilder::Binop(wasm::WasmOpcode opcode, Node* left, Node* right,
                              wasm::WasmCodePosition position) {
  const Operator* op;
  MachineOperatorBuilder* m = mcgraph()->machine();
  switch (opcode) {
    case wasm::kExprI32Add:
      op = m->Int32Add();
      break;
    case wasm::kExprI32Sub:
      op = m->Int32Sub();
      break;
    case wasm::kExprI32Mul:
      op = m->Int32Mul();
      break;
    case wasm::kExprI32DivS:
      return BuildI32DivS(left, right, position);
    case wasm::kExprI32DivU:
      return BuildI32DivU(left, right, position);
    case wasm::kExprI32RemS:
      return BuildI32RemS(left, right, position);
    case wasm::kExprI32RemU:
      return BuildI32RemU(left, right, position);
    case wasm::kExprI32And:
      op = m->Word32And();
      break;
    case wasm::kExprI32Ior:
      op = m->Word32Or();
      break;
    case wasm::kExprI32Xor:
      op = m->Word32Xor();
      break;
    case wasm::kExprI32Shl:
      op = m->Word32Shl();
      right = MaskShiftCount32(right);
      break;
    case wasm::kExprI32ShrU:
      op = m->Word32Shr();
      right = MaskShiftCount32(right);
      break;
    case wasm::kExprI32ShrS:
      op = m->Word32Sar();
      right = MaskShiftCount32(right);
      break;
    case wasm::kExprI32Ror:
      op = m->Word32Ror();
      right = MaskShiftCount32(right);
      break;
    case wasm::kExprI32Rol:
      if (m->Word32Rol().IsSupported()) {
        op = m->Word32Rol().op();
        right = MaskShiftCount32(right);
        break;
      }
      return BuildI32Rol(left, right);
    case wasm::kExprI32Eq:
      op = m->Word32Equal();
      break;
    case wasm::kExprI32Ne:
      return Invert(Binop(wasm::kExprI32Eq, left, right));
    case wasm::kExprI32LtS:
      op = m->Int32LessThan();
      break;
    case wasm::kExprI32LeS:
      op = m->Int32LessThanOrEqual();
      break;
    case wasm::kExprI32LtU:
      op = m->Uint32LessThan();
      break;
    case wasm::kExprI32LeU:
      op = m->Uint32LessThanOrEqual();
      break;
    case wasm::kExprI32GtS:
      op = m->Int32LessThan();
      std::swap(left, right);
      break;
    case wasm::kExprI32GeS:
      op = m->Int32LessThanOrEqual();
      std::swap(left, right);
      break;
    case wasm::kExprI32GtU:
      op = m->Uint32LessThan();
      std::swap(left, right);
      break;
    case wasm::kExprI32GeU:
      op = m->Uint32LessThanOrEqual();
      std::swap(left, right);
      break;
    case wasm::kExprI64And:
      op = m->Word64And();
      break;
    case wasm::kExprI64Add:
      op = m->Int64Add();
      break;
    case wasm::kExprI64Sub:
      op = m->Int64Sub();
      break;
    case wasm::kExprI64Mul:
      op = m->Int64Mul();
      break;
    case wasm::kExprI64DivS:
      return BuildI64DivS(left, right, position);
    case wasm::kExprI64DivU:
      return BuildI64DivU(left, right, position);
    case wasm::kExprI64RemS:
      return BuildI64RemS(left, right, position);
    case wasm::kExprI64RemU:
      return BuildI64RemU(left, right, position);
    case wasm::kExprI64Ior:
      op = m->Word64Or();
      break;
    case wasm::kExprI64Xor:
      op = m->Word64Xor();
      break;
    case wasm::kExprI64Shl:
      op = m->Word64Shl();
      right = MaskShiftCount64(right);
      break;
    case wasm::kExprI64ShrU:
      op = m->Word64Shr();
      right = MaskShiftCount64(right);
      break;
    case wasm::kExprI64ShrS:
      op = m->Word64Sar();
      right = MaskShiftCount64(right);
      break;
    case wasm::kExprI64Eq:
      op = m->Word64Equal();
      break;
    case wasm::kExprI64Ne:
      return Invert(Binop(wasm::kExprI64Eq, left, right));
    case wasm::kExprI64LtS:
      op = m->Int64LessThan();
      break;
    case wasm::kExprI64LeS:
      op = m->Int64LessThanOrEqual();
      break;
    case wasm::kExprI64LtU:
      op = m->Uint64LessThan();
      break;
    case wasm::kExprI64LeU:
      op = m->Uint64LessThanOrEqual();
      break;
    case wasm::kExprI64GtS:
      op = m->Int64LessThan();
      std::swap(left, right);
      break;
    case wasm::kExprI64GeS:
      op = m->Int64LessThanOrEqual();
      std::swap(left, right);
      break;
    case wasm::kExprI64GtU:
      op = m->Uint64LessThan();
      std::swap(left, right);
      break;
    case wasm::kExprI64GeU:
      op = m->Uint64LessThanOrEqual();
      std::swap(left, right);
      break;
    case wasm::kExprI64Ror:
      right = MaskShiftCount64(right);
      return m->Is64() ? graph()->NewNode(m->Word64Ror(), left, right)
                       : graph()->NewNode(m->Word64RorLowerable(), left, right,
                                          control());
    case wasm::kExprI64Rol:
      if (m->Word64Rol().IsSupported()) {
        return m->Is64() ? graph()->NewNode(m->Word64Rol().op(), left,
                                            MaskShiftCount64(right))
                         : graph()->NewNode(m->Word64RolLowerable().op(), left,
                                            MaskShiftCount64(right), control());
      } else if (m->Word32Rol().IsSupported()) {
        return graph()->NewNode(m->Word64RolLowerable().placeholder(), left,
                                right, control());
      }
      return BuildI64Rol(left, right);
    case wasm::kExprF32CopySign:
      return BuildF32CopySign(left, right);
    case wasm::kExprF64CopySign:
      return BuildF64CopySign(left, right);
    case wasm::kExprF32Add:
      op = m->Float32Add();
      break;
    case wasm::kExprF32Sub:
      op = m->Float32Sub();
      break;
    case wasm::kExprF32Mul:
      op = m->Float32Mul();
      break;
    case wasm::kExprF32Div:
      op = m->Float32Div();
      break;
    case wasm::kExprF32Eq:
      op = m->Float32Equal();
      break;
    case wasm::kExprF32Ne:
      return Invert(Binop(wasm::kExprF32Eq, left, right));
    case wasm::kExprF32Lt:
      op = m->Float32LessThan();
      break;
    case wasm::kExprF32Ge:
      op = m->Float32LessThanOrEqual();
      std::swap(left, right);
      break;
    case wasm::kExprF32Gt:
      op = m->Float32LessThan();
      std::swap(left, right);
      break;
    case wasm::kExprF32Le:
      op = m->Float32LessThanOrEqual();
      break;
    case wasm::kExprF64Add:
      op = m->Float64Add();
      break;
    case wasm::kExprF64Sub:
      op = m->Float64Sub();
      break;
    case wasm::kExprF64Mul:
      op = m->Float64Mul();
      break;
    case wasm::kExprF64Div:
      op = m->Float64Div();
      break;
    case wasm::kExprF64Eq:
      op = m->Float64Equal();
      break;
    case wasm::kExprF64Ne:
      return Invert(Binop(wasm::kExprF64Eq, left, right));
    case wasm::kExprF64Lt:
      op = m->Float64LessThan();
      break;
    case wasm::kExprF64Le:
      op = m->Float64LessThanOrEqual();
      break;
    case wasm::kExprF64Gt:
      op = m->Float64LessThan();
      std::swap(left, right);
      break;
    case wasm::kExprF64Ge:
      op = m->Float64LessThanOrEqual();
      std::swap(left, right);
      break;
    case wasm::kExprF32Min:
      op = m->Float32Min();
      break;
    case wasm::kExprF64Min:
      op = m->Float64Min();
      break;
    case wasm::kExprF32Max:
      op = m->Float32Max();
      break;
    case wasm::kExprF64Max:
      op = m->Float64Max();
      break;
    case wasm::kExprF64Pow:
      return BuildF64Pow(left, right);
    case wasm::kExprF64Atan2:
      op = m->Float64Atan2();
      break;
    case wasm::kExprF64Mod:
      return BuildF64Mod(left, right);
    case wasm::kExprRefEq:
      return gasm_->TaggedEqual(left, right);
    case wasm::kExprI32AsmjsDivS:
      return BuildI32AsmjsDivS(left, right);
    case wasm::kExprI32AsmjsDivU:
      return BuildI32AsmjsDivU(left, right);
    case wasm::kExprI32AsmjsRemS:
      return BuildI32AsmjsRemS(left, right);
    case wasm::kExprI32AsmjsRemU:
      return BuildI32AsmjsRemU(left, right);
    case wasm::kExprI32AsmjsStoreMem8:
      return BuildAsmjsStoreMem(MachineType::Int8(), left, right);
    case wasm::kExprI32AsmjsStoreMem16:
      return BuildAsmjsStoreMem(MachineType::Int16(), left, right);
    case wasm::kExprI32AsmjsStoreMem:
      return BuildAsmjsStoreMem(MachineType::Int32(), left, right);
    case wasm::kExprF32AsmjsStoreMem:
      return BuildAsmjsStoreMem(MachineType::Float32(), left, right);
    case wasm::kExprF64AsmjsStoreMem:
      return BuildAsmjsStoreMem(MachineType::Float64(), left, right);
    default:
      FATAL_UNSUPPORTED_OPCODE(opcode);
  }
  return graph()->NewNode(op, left, right);
}

Node* WasmGraphBuilder::Unop(wasm::WasmOpcode opcode, Node* input,
                             wasm::ValueType type,
                             wasm::WasmCodePosition position) {
  const Operator* op;
  MachineOperatorBuilder* m = mcgraph()->machine();
  switch (opcode) {
    case wasm::kExprI32Eqz:
      return gasm_->Word32Equal(input, Int32Constant(0));
    case wasm::kExprF32Abs:
      op = m->Float32Abs();
      break;
    case wasm::kExprF32Neg: {
      op = m->Float32Neg();
      break;
    }
    case wasm::kExprF32Sqrt:
      op = m->Float32Sqrt();
      break;
    case wasm::kExprF64Abs:
      op = m->Float64Abs();
      break;
    case wasm::kExprF64Neg: {
      op = m->Float64Neg();
      break;
    }
    case wasm::kExprF64Sqrt:
      op = m->Float64Sqrt();
      break;
    case wasm::kExprI32SConvertF32:
    case wasm::kExprI32UConvertF32:
    case wasm::kExprI32SConvertF64:
    case wasm::kExprI32UConvertF64:
    case wasm::kExprI32SConvertSatF64:
    case wasm::kExprI32UConvertSatF64:
    case wasm::kExprI32SConvertSatF32:
    case wasm::kExprI32UConvertSatF32:
      return BuildIntConvertFloat(input, position, opcode);
    case wasm::kExprI32AsmjsSConvertF64:
      return BuildI32AsmjsSConvertF64(input);
    case wasm::kExprI32AsmjsUConvertF64:
      return BuildI32AsmjsUConvertF64(input);
    case wasm::kExprF32ConvertF64:
      op = m->TruncateFloat64ToFloat32();
      break;
    case wasm::kExprF64SConvertI32:
      op = m->ChangeInt32ToFloat64();
      break;
    case wasm::kExprF64UConvertI32:
      op = m->ChangeUint32ToFloat64();
      break;
    case wasm::kExprF32SConvertI32:
      op = m->RoundInt32ToFloat32();
      break;
    case wasm::kExprF32UConvertI32:
      op = m->RoundUint32ToFloat32();
      break;
    case wasm::kExprI32AsmjsSConvertF32:
      return BuildI32AsmjsSConvertF32(input);
    case wasm::kExprI32AsmjsUConvertF32:
      return BuildI32AsmjsUConvertF32(input);
    case wasm::kExprF64ConvertF32:
      op = m->ChangeFloat32ToFloat64();
      break;
    case wasm::kExprF32ReinterpretI32:
      op = m->BitcastInt32ToFloat32();
      break;
    case wasm::kExprI32ReinterpretF32:
      op = m->BitcastFloat32ToInt32();
      break;
    case wasm::kExprI32Clz:
      op = m->Word32Clz();
      break;
    case wasm::kExprI32Ctz: {
      if (m->Word32Ctz().IsSupported()) {
        op = m->Word32Ctz().op();
        break;
      } else if (m->Word32ReverseBits().IsSupported()) {
        Node* reversed = graph()->NewNode(m->Word32ReverseBits().op(), input);
        Node* result = graph()->NewNode(m->Word32Clz(), reversed);
        return result;
      } else {
        return BuildI32Ctz(input);
      }
    }
    case wasm::kExprI32Popcnt: {
      if (m->Word32Popcnt().IsSupported()) {
        op = m->Word32Popcnt().op();
        break;
      } else {
        return BuildI32Popcnt(input);
      }
    }
    case wasm::kExprF32Floor: {
      if (!m->Float32RoundDown().IsSupported()) return BuildF32Floor(input);
      op = m->Float32RoundDown().op();
      break;
    }
    case wasm::kExprF32Ceil: {
      if (!m->Float32RoundUp().IsSupported()) return BuildF32Ceil(input);
      op = m->Float32RoundUp().op();
      break;
    }
    case wasm::kExprF32Trunc: {
      if (!m->Float32RoundTruncate().IsSupported()) return BuildF32Trunc(input);
      op = m->Float32RoundTruncate().op();
      break;
    }
    case wasm::kExprF32NearestInt: {
      if (!m->Float32RoundTiesEven().IsSupported())
        return BuildF32NearestInt(input);
      op = m->Float32RoundTiesEven().op();
      break;
    }
    case wasm::kExprF64Floor: {
      if (!m->Float64RoundDown().IsSupported()) return BuildF64Floor(input);
      op = m->Float64RoundDown().op();
      break;
    }
    case wasm::kExprF64Ceil: {
      if (!m->Float64RoundUp().IsSupported()) return BuildF64Ceil(input);
      op = m->Float64RoundUp().op();
      break;
    }
    case wasm::kExprF64Trunc: {
      if (!m->Float64RoundTruncate().IsSupported()) return BuildF64Trunc(input);
      op = m->Float64RoundTruncate().op();
      break;
    }
    case wasm::kExprF64NearestInt: {
      if (!m->Float64RoundTiesEven().IsSupported())
        return BuildF64NearestInt(input);
      op = m->Float64RoundTiesEven().op();
      break;
    }
    case wasm::kExprF64Acos: {
      return BuildF64Acos(input);
    }
    case wasm::kExprF64Asin: {
      return BuildF64Asin(input);
    }
    case wasm::kExprF64Atan:
      op = m->Float64Atan();
      break;
    case wasm::kExprF64Cos: {
      op = m->Float64Cos();
      break;
    }
    case wasm::kExprF64Sin: {
      op = m->Float64Sin();
      break;
    }
    case wasm::kExprF64Tan: {
      op = m->Float64Tan();
      break;
    }
    case wasm::kExprF64Exp: {
      op = m->Float64Exp();
      break;
    }
    case wasm::kExprF64Log:
      op = m->Float64Log();
      break;
    case wasm::kExprI32ConvertI64:
      op = m->TruncateInt64ToInt32();
      break;
    case wasm::kExprI64SConvertI32:
      op = m->ChangeInt32ToInt64();
      break;
    case wasm::kExprI64UConvertI32:
      op = m->ChangeUint32ToUint64();
      break;
    case wasm::kExprF64ReinterpretI64:
      op = m->BitcastInt64ToFloat64();
      break;
    case wasm::kExprI64ReinterpretF64:
      op = m->BitcastFloat64ToInt64();
      break;
    case wasm::kExprI64Clz:
      return m->Is64()
                 ? graph()->NewNode(m->Word64Clz(), input)
                 : graph()->NewNode(m->Word64ClzLowerable(), input, control());
    case wasm::kExprI64Ctz: {
      if (m->Word64Ctz().IsSupported()) {
        return m->Is64() ? graph()->NewNode(m->Word64Ctz().op(), input)
                         : graph()->NewNode(m->Word64CtzLowerable().op(), input,
                                            control());
      } else if (m->Is32() && m->Word32Ctz().IsSupported()) {
        return graph()->NewNode(m->Word64CtzLowerable().placeholder(), input,
                                control());
      } else if (m->Word64ReverseBits().IsSupported()) {
        Node* reversed = graph()->NewNode(m->Word64ReverseBits().op(), input);
        Node* result = m->Is64() ? graph()->NewNode(m->Word64Clz(), reversed)
                                 : graph()->NewNode(m->Word64ClzLowerable(),
                                                    reversed, control());
        return result;
      } else {
        return BuildI64Ctz(input);
      }
    }
    case wasm::kExprI64Popcnt: {
      OptionalOperator popcnt64 = m->Word64Popcnt();
      if (popcnt64.IsSupported()) {
        op = popcnt64.op();
      } else if (m->Is32() && m->Word32Popcnt().IsSupported()) {
        op = popcnt64.placeholder();
      } else {
        return BuildI64Popcnt(input);
      }
      break;
    }
    case wasm::kExprI64Eqz:
      return gasm_->Word64Equal(input, Int64Constant(0));
    case wasm::kExprF32SConvertI64:
      if (m->Is32()) {
        return BuildF32SConvertI64(input);
      }
      op = m->RoundInt64ToFloat32();
      break;
    case wasm::kExprF32UConvertI64:
      if (m->Is32()) {
        return BuildF32UConvertI64(input);
      }
      op = m->RoundUint64ToFloat32();
      break;
    case wasm::kExprF64SConvertI64:
      if (m->Is32()) {
        return BuildF64SConvertI64(input);
      }
      op = m->RoundInt64ToFloat64();
      break;
    case wasm::kExprF64UConvertI64:
      if (m->Is32()) {
        return BuildF64UConvertI64(input);
      }
      op = m->RoundUint64ToFloat64();
      break;
    case wasm::kExprI32SExtendI8:
      op = m->SignExtendWord8ToInt32();
      break;
    case wasm::kExprI32SExtendI16:
      op = m->SignExtendWord16ToInt32();
      break;
    case wasm::kExprI64SExtendI8:
      op = m->SignExtendWord8ToInt64();
      break;
    case wasm::kExprI64SExtendI16:
      op = m->SignExtendWord16ToInt64();
      break;
    case wasm::kExprI64SExtendI32:
      op = m->SignExtendWord32ToInt64();
      break;
    case wasm::kExprI64SConvertF32:
    case wasm::kExprI64UConvertF32:
    case wasm::kExprI64SConvertF64:
    case wasm::kExprI64UConvertF64:
    case wasm::kExprI64SConvertSatF32:
    case wasm::kExprI64UConvertSatF32:
    case wasm::kExprI64SConvertSatF64:
    case wasm::kExprI64UConvertSatF64:
      return mcgraph()->machine()->Is32()
                 ? BuildCcallConvertFloat(input, position, opcode)
                 : BuildIntConvertFloat(input, position, opcode);
    case wasm::kExprRefIsNull:
      return IsNull(input, type);
    // We abuse ref.as_non_null, which isn't otherwise used in this switch, as
    // a sentinel for the negation of ref.is_null.
    case wasm::kExprRefAsNonNull:
      return gasm_->Word32Equal(gasm_->Int32Constant(0), IsNull(input, type));
    case wasm::kExprI32AsmjsLoadMem8S:
      return BuildAsmjsLoadMem(MachineType::Int8(), input);
    case wasm::kExprI32AsmjsLoadMem8U:
      return BuildAsmjsLoadMem(MachineType::Uint8(), input);
    case wasm::kExprI32AsmjsLoadMem16S:
      return BuildAsmjsLoadMem(MachineType::Int16(), input);
    case wasm::kExprI32AsmjsLoadMem16U:
      return BuildAsmjsLoadMem(MachineType::Uint16(), input);
    case wasm::kExprI32AsmjsLoadMem:
      return BuildAsmjsLoadMem(MachineType::Int32(), input);
    case wasm::kExprF32AsmjsLoadMem:
      return BuildAsmjsLoadMem(MachineType::Float32(), input);
    case wasm::kExprF64AsmjsLoadMem:
      return BuildAsmjsLoadMem(MachineType::Float64(), input);
    case wasm::kExprAnyConvertExtern: {
      return gasm_->WasmAnyConvertExtern(input);
    }
    case wasm::kExprExternConvertAny:
      return gasm_->WasmExternConvertAny(input);
    default:
      FATAL_UNSUPPORTED_OPCODE(opcode);
  }
  return graph()->NewNode(op, input);
}

Node* WasmGraphBuilder::Float32Constant(float value) {
  return mcgraph()->Float32Constant(value);
}

Node* WasmGraphBuilder::Float64Constant(double value) {
  return mcgraph()->Float64Constant(value);
}

Node* WasmGraphBuilder::Simd128Constant(const uint8_t value[16]) {
  has_simd_ = true;
  return graph()->NewNode(mcgraph()->machine()->S128Const(value));
}

std::tuple<Node*, Node*> WasmGraphBuilder::BranchNoHint(Node* cond) {
  Node* true_node;
  Node* false_node;
  gasm_->Branch(cond, &true_node, &false_node, BranchHint::kNone);
  return {true_node, false_node};
}

std::tuple<Node*, Node*> WasmGraphBuilder::BranchExpectFalse(Node* cond) {
  Node* true_node;
  Node* false_node;
  gasm_->Branch(cond, &true_node, &false_node, BranchHint::kFalse);
  return {true_node, false_node};
}

std::tuple<Node*, Node*> WasmGraphBuilder::BranchExpectTrue(Node* cond) {
  Node* true_node;
  Node* false_node;
  gasm_->Branch(cond, &true_node, &false_node, BranchHint::kTrue);
  return {true_node, false_node};
}

Node* WasmGraphBuilder::Select(Node *cond, Node* true_node,
                               Node* false_node, wasm::ValueType type) {
  MachineOperatorBuilder* m = mcgraph()->machine();
  wasm::ValueKind kind = type.kind();
  // Lower to select if supported.
  if (kind == wasm::kF32 && m->Float32Select().IsSupported()) {
    return mcgraph()->graph()->NewNode(m->Float32Select().op(), cond,
                                       true_node, false_node);
  }
  if (kind == wasm::kF64 && m->Float64Select().IsSupported()) {
    return mcgraph()->graph()->NewNode(m->Float64Select().op(), cond,
                                       true_node, false_node);
  }
  if (kind == wasm::kI32 && m->Word32Select().IsSupported()) {
    return mcgraph()->graph()->NewNode(m->Word32Select().op(), cond, true_node,
                                       false_node);
  }
  if (kind == wasm::kI64 && m->Word64Select().IsSupported()) {
    return mcgraph()->graph()->NewNode(m->Word64Select().op(), cond, true_node,
                                       false_node);
  }
  // Default to control-flow.

  auto [if_true, if_false] = BranchNoHint(cond);
  Node* merge = Merge(if_true, if_false);
  SetControl(merge);
  Node* inputs[] = {true_node, false_node, merge};
  return Phi(type, 2, inputs);
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

void WasmGraphBuilder::TrapIfTrue(wasm::TrapReason reason, Node* cond,
                                  wasm::WasmCodePosition position) {
  TrapId trap_id = GetTrapIdForTrap(reason);
  gasm_->TrapIf(cond, trap_id);
  SetSourcePosition(effect(), position);
}

void WasmGraphBuilder::TrapIfFalse(wasm::TrapReason reason, Node* cond,
                                   wasm::WasmCodePosition position) {
  TrapId trap_id = GetTrapIdForTrap(reason);
  gasm_->TrapUnless(cond, trap_id);
  SetSourcePosition(effect(), position);
}

Node* WasmGraphBuilder::AssertNotNull(Node* object, wasm::ValueType type,
                                      wasm::WasmCodePosition position,
                                      wasm::TrapReason reason) {
  TrapId trap_id = GetTrapIdForTrap(reason);
  Node* result = gasm_->AssertNotNull(object, type, trap_id);
  SetSourcePosition(result, position);
  return result;
}

// Add a check that traps if {node} is equal to {val}.
void WasmGraphBuilder::TrapIfEq32(wasm::TrapReason reason, Node* node,
                                  int32_t val,
                                  wasm::WasmCodePosition position) {
  if (val == 0) {
    TrapIfFalse(reason, node, position);
  } else {
    TrapIfTrue(reason, gasm_->Word32Equal(node, Int32Constant(val)), position);
  }
}

// Add a check that traps if {node} is zero.
void WasmGraphBuilder::ZeroCheck32(wasm::TrapReason reason, Node* node,
                                   wasm::WasmCodePosition position) {
  TrapIfEq32(reason, node, 0, position);
}

// Add a check that traps if {node} is equal to {val}.
void WasmGraphBuilder::TrapIfEq64(wasm::TrapReason reason, Node* node,
                                  int64_t val,
                                  wasm::WasmCodePosition position) {
  TrapIfTrue(reason, gasm_->Word64Equal(node, Int64Constant(val)), position);
}

// Add a check that traps if {node} is zero.
void WasmGraphBuilder::ZeroCheck64(wasm::TrapReason reason, Node* node,
                                   wasm::WasmCodePosition position) {
  TrapIfEq64(reason, node, 0, position);
}

Node* WasmGraphBuilder::Switch(unsigned count, Node* key) {
  // The instruction selector will use {kArchTableSwitch} for large switches,
  // which has limited input count, see {InstructionSelector::EmitTableSwitch}.
  DCHECK_LE(count, Instruction::kMaxInputCount - 2);          // value_range + 2
  DCHECK_LE(count, wasm::kV8MaxWasmFunctionBrTableSize + 1);  // plus IfDefault
  return graph()->NewNode(mcgraph()->common()->Switch(count), key, control());
}

Node* WasmGraphBuilder::IfValue(int32_t value, Node* sw) {
  DCHECK_EQ(IrOpcode::kSwitch, sw->opcode());
  return graph()->NewNode(mcgraph()->common()->IfValue(value), sw);
}

Node* WasmGraphBuilder::IfDefault(Node* sw) {
  DCHECK_EQ(IrOpcode::kSwitch, sw->opcode());
  return graph()->NewNode(mcgraph()->common()->IfDefault(), sw);
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

void WasmGraphBuilder::Trap(wasm::TrapReason reason,
                            wasm::WasmCodePosition position) {
  TrapIfFalse(reason, Int32Constant(0), position);
  // Connect control to end via a Throw() node.
  TerminateThrow(effect(), control());
}

Node* WasmGraphBuilder::MaskShiftCount32(Node* node) {
  static const int32_t kMask32 = 0x1F;
  if (!mcgraph()->machine()->Word32ShiftIsSafe()) {
    // Shifts by constants are so common we pattern-match them here.
    Int32Matcher match(node);
    if (match.HasResolvedValue()) {
      int32_t masked = (match.ResolvedValue() & kMask32);
      if (match.ResolvedValue() != masked) node = Int32Constant(masked);
    } else {
      node = gasm_->Word32And(node, Int32Constant(kMask32));
    }
  }
  return node;
}

Node* WasmGraphBuilder::MaskShiftCount64(Node* node) {
  static const int64_t kMask64 = 0x3F;
  if (!mcgraph()->machine()->Word32ShiftIsSafe()) {
    // Shifts by constants are so common we pattern-match them here.
    Int64Matcher match(node);
    if (match.HasResolvedValue()) {
      int64_t masked = (match.ResolvedValue() & kMask64);
      if (match.ResolvedValue() != masked) node = Int64Constant(masked);
    } else {
      node = gasm_->Word64And(node, Int64Constant(kMask64));
    }
  }
  return node;
}

namespace {

bool ReverseBytesSupported(MachineOperatorBuilder* m, size_t size_in_bytes) {
  switch (size_in_bytes) {
    case 4:
    case 16:
      return true;
    case 8:
      return m->Is64();
    default:
      break;
  }
  return false;
}

}  // namespace

Node* WasmGraphBuilder::BuildChangeEndiannessStore(
    Node* node, MachineRepresentation mem_rep, wasm::ValueType wasmtype) {
  Node* result;
  Node* value = node;
  MachineOperatorBuilder* m = mcgraph()->machine();
  int valueSizeInBytes = wasmtype.value_kind_size();
  int valueSizeInBits = 8 * valueSizeInBytes;
  bool isFloat = false;

  switch (wasmtype.kind()) {
    case wasm::kF64:
      value = gasm_->BitcastFloat64ToInt64(node);
      isFloat = true;
      [[fallthrough]];
    case wasm::kI64:
      result = Int64Constant(0);
      break;
    case wasm::kF32:
      value = gasm_->BitcastFloat32ToInt32(node);
      isFloat = true;
      [[fallthrough]];
    case wasm::kI32:
      result = Int32Constant(0);
      break;
    case wasm::kS128:
      DCHECK(ReverseBytesSupported(m, valueSizeInBytes));
      break;
    default:
      UNREACHABLE();
  }

  if (mem_rep == MachineRepresentation::kWord8) {
    // No need to change endianness for byte size, return original node
    return node;
  }
  if (wasmtype == wasm::kWasmI64 && mem_rep < MachineRepresentation::kWord64) {
    // In case we store lower part of WasmI64 expression, we can truncate
    // upper 32bits
    value = gasm_->TruncateInt64ToInt32(value);
    valueSizeInBytes = wasm::kWasmI32.value_kind_size();
    valueSizeInBits = 8 * valueSizeInBytes;
    if (mem_rep == MachineRepresentation::kWord16) {
      value = gasm_->Word32Shl(value, Int32Constant(16));
    }
  } else if (wasmtype == wasm::kWasmI32 &&
             mem_rep == MachineRepresentation::kWord16) {
    value = gasm_->Word32Shl(value, Int32Constant(16));
  }

  int i;
  uint32_t shiftCount;

  if (ReverseBytesSupported(m, valueSizeInBytes)) {
    switch (valueSizeInBytes) {
      case 4:
        result = gasm_->Word32ReverseBytes(value);
        break;
      case 8:
        result = gasm_->Word64ReverseBytes(value);
        break;
      case 16:
        result = graph()->NewNode(m->Simd128ReverseBytes(), value);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    for (i = 0, shiftCount = valueSizeInBits - 8; i < valueSizeInBits / 2;
         i += 8, shiftCount -= 16) {
      Node* shiftLower;
      Node* shiftHigher;
      Node* lowerByte;
      Node* higherByte;

      DCHECK_LT(0, shiftCount);
      DCHECK_EQ(0, (shiftCount + 8) % 16);

      if (valueSizeInBits > 32) {
        shiftLower = gasm_->Word64Shl(value, Int64Constant(shiftCount));
        shiftHigher = gasm_->Word64Shr(value, Int64Constant(shiftCount));
        lowerByte = gasm_->Word64And(
            shiftLower, Int64Constant(static_cast<uint64_t>(0xFF)
                                      << (valueSizeInBits - 8 - i)));
        higherByte = gasm_->Word64And(
            shiftHigher, Int64Constant(static_cast<uint64_t>(0xFF) << i));
        result = gasm_->Word64Or(result, lowerByte);
        result = gasm_->Word64Or(result, higherByte);
      } else {
        shiftLower = gasm_->Word32Shl(value, Int32Constant(shiftCount));
        shiftHigher = gasm_->Word32Shr(value, Int32Constant(shiftCount));
        lowerByte = gasm_->Word32And(
            shiftLower, Int32Constant(static_cast<uint32_t>(0xFF)
                                      << (valueSizeInBits - 8 - i)));
        higherByte = gasm_->Word32And(
            shiftHigher, Int32Constant(static_cast<uint32_t>(0xFF) << i));
        result = gasm_->Word32Or(result, lowerByte);
        result = gasm_->Word32Or(result, higherByte);
      }
    }
  }

  if (isFloat) {
    switch (wasmtype.kind()) {
      case wasm::kF64:
        result = gasm_->BitcastInt64ToFloat64(result);
        break;
      case wasm::kF32:
        result = gasm_->BitcastInt32ToFloat32(result);
        break;
      default:
        UNREACHABLE();
    }
  }

  return result;
}

Node* WasmGraphBuilder::BuildChangeEndiannessLoad(Node* node,
                                                  MachineType memtype,
                                                  wasm::ValueType wasmtype) {
  Node* result;
  Node* value = node;
  MachineOperatorBuilder* m = mcgraph()->machine();
  int valueSizeInBytes = ElementSizeInBytes(memtype.representation());
  int valueSizeInBits = 8 * valueSizeInBytes;
  bool isFloat = false;

  switch (memtype.representation()) {
    case MachineRepresentation::kFloat64:
      value = gasm_->BitcastFloat64ToInt64(node);
      isFloat = true;
      [[fallthrough]];
    case MachineRepresentation::kWord64:
      result = Int64Constant(0);
      break;
    case MachineRepresentation::kFloat32:
      value = gasm_->BitcastFloat32ToInt32(node);
      isFloat = true;
      [[fallthrough]];
    case MachineRepresentation::kWord32:
    case MachineRepresentation::kWord16:
      result = Int32Constant(0);
      break;
    case MachineRepresentation::kWord8:
      // No need to change endianness for byte size, return original node
      return node;
    case MachineRepresentation::kSimd128:
      DCHECK(ReverseBytesSupported(m, valueSizeInBytes));
      break;
    default:
      UNREACHABLE();
  }

  int i;
  uint32_t shiftCount;

  if (ReverseBytesSupported(m, valueSizeInBytes < 4 ? 4 : valueSizeInBytes)) {
    switch (valueSizeInBytes) {
      case 2:
        result = gasm_->Word32ReverseBytes(
            gasm_->Word32Shl(value, Int32Constant(16)));
        break;
      case 4:
        result = gasm_->Word32ReverseBytes(value);
        break;
      case 8:
        result = gasm_->Word64ReverseBytes(value);
        break;
      case 16:
        result = graph()->NewNode(m->Simd128ReverseBytes(), value);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    for (i = 0, shiftCount = valueSizeInBits - 8; i < valueSizeInBits / 2;
         i += 8, shiftCount -= 16) {
      Node* shiftLower;
      Node* shiftHigher;
      Node* lowerByte;
      Node* higherByte;

      DCHECK_LT(0, shiftCount);
      DCHECK_EQ(0, (shiftCount + 8) % 16);

      if (valueSizeInBits > 32) {
        shiftLower = gasm_->Word64Shl(value, Int64Constant(shiftCount));
        shiftHigher = gasm_->Word64Shr(value, Int64Constant(shiftCount));
        lowerByte = gasm_->Word64And(
            shiftLower, Int64Constant(static_cast<uint64_t>(0xFF)
                                      << (valueSizeInBits - 8 - i)));
        higherByte = gasm_->Word64And(
            shiftHigher, Int64Constant(static_cast<uint64_t>(0xFF) << i));
        result = gasm_->Word64Or(result, lowerByte);
        result = gasm_->Word64Or(result, higherByte);
      } else {
        shiftLower = gasm_->Word32Shl(value, Int32Constant(shiftCount));
        shiftHigher = gasm_->Word32Shr(value, Int32Constant(shiftCount));
        lowerByte = gasm_->Word32And(
            shiftLower, Int32Constant(static_cast<uint32_t>(0xFF)
                                      << (valueSizeInBits - 8 - i)));
        higherByte = gasm_->Word32And(
            shiftHigher, Int32Constant(static_cast<uint32_t>(0xFF) << i));
        result = gasm_->Word32Or(result, lowerByte);
        result = gasm_->Word32Or(result, higherByte);
      }
    }
  }

  if (isFloat) {
    switch (memtype.representation()) {
      case MachineRepresentation::kFloat64:
        result = gasm_->BitcastInt64ToFloat64(result);
        break;
      case MachineRepresentation::kFloat32:
        result = gasm_->BitcastInt32ToFloat32(result);
        break;
      default:
        UNREACHABLE();
    }
  }

  // We need to sign or zero extend the value
  if (memtype.IsSigned()) {
    DCHECK(!isFloat);
    if (valueSizeInBits < 32) {
      Node* shiftBitCount;
      // Perform sign extension using following trick
      // result = (x << machine_width - type_width) >> (machine_width -
      // type_width)
      if (wasmtype == wasm::kWasmI64) {
        shiftBitCount = Int32Constant(64 - valueSizeInBits);
        result = gasm_->Word64Sar(
            gasm_->Word64Shl(gasm_->ChangeInt32ToInt64(result), shiftBitCount),
            shiftBitCount);
      } else if (wasmtype == wasm::kWasmI32) {
        shiftBitCount = Int32Constant(32 - valueSizeInBits);
        result = gasm_->Word32Sar(gasm_->Word32Shl(result, shiftBitCount),
                                  shiftBitCount);
      }
    }
  } else if (wasmtype == wasm::kWasmI64 && valueSizeInBits < 64) {
    result = gasm_->ChangeUint32ToUint64(result);
  }

  return result;
}

Node* WasmGraphBuilder::BuildF32CopySign(Node* left, Node* right) {
  Node* result = Unop(
      wasm::kExprF32ReinterpretI32,
      Binop(wasm::kExprI32Ior,
            Binop(wasm::kExprI32And, Unop(wasm::kExprI32ReinterpretF32, left),
                  Int32Constant(0x7FFFFFFF)),
            Binop(wasm::kExprI32And, Unop(wasm::kExprI32ReinterpretF32, right),
                  Int32Constant(0x80000000))));

  return result;
}

Node* WasmGraphBuilder::BuildF64CopySign(Node* left, Node* right) {
  if (mcgraph()->machine()->Is64()) {
    return gasm_->BitcastInt64ToFloat64(
        gasm_->Word64Or(gasm_->Word64And(gasm_->BitcastFloat64ToInt64(left),
                                         Int64Constant(0x7FFFFFFFFFFFFFFF)),
                        gasm_->Word64And(gasm_->BitcastFloat64ToInt64(right),
                                         Int64Constant(0x8000000000000000))));
  }

  DCHECK(mcgraph()->machine()->Is32());

  Node* high_word_left = gasm_->Float64ExtractHighWord32(left);
  Node* high_word_right = gasm_->Float64ExtractHighWord32(right);

  Node* new_high_word = gasm_->Word32Or(
      gasm_->Word32And(high_word_left, Int32Constant(0x7FFFFFFF)),
      gasm_->Word32And(high_word_right, Int32Constant(0x80000000)));

  return gasm_->Float64InsertHighWord32(left, new_high_word);
}

namespace {

MachineType IntConvertType(wasm::WasmOpcode opcode) {
  switch (opcode) {
    case wasm::kExprI32SConvertF32:
    case wasm::kExprI32SConvertF64:
    case wasm::kExprI32SConvertSatF32:
    case wasm::kExprI32SConvertSatF64:
      return MachineType::Int32();
    case wasm::kExprI32UConvertF32:
    case wasm::kExprI32UConvertF64:
    case wasm::kExprI32UConvertSatF32:
    case wasm::kExprI32UConvertSatF64:
      return MachineType::Uint32();
    case wasm::kExprI64SConvertF32:
    case wasm::kExprI64SConvertF64:
    case wasm::kExprI64SConvertSatF32:
    case wasm::kExprI64SConvertSatF64:
      return MachineType::Int64();
    case wasm::kExprI64UConvertF32:
    case wasm::kExprI64UConvertF64:
    case wasm::kExprI64UConvertSatF32:
    case wasm::kExprI64UConvertSatF64:
      return MachineType::Uint64();
    default:
      UNREACHABLE();
  }
}

MachineType FloatConvertType(wasm::WasmOpcode opcode) {
  switch (opcode) {
    case wasm::kExprI32SConvertF32:
    case wasm::kExprI32UConvertF32:
    case wasm::kExprI32SConvertSatF32:
    case wasm::kExprI64SConvertF32:
    case wasm::kExprI64UConvertF32:
    case wasm::kExprI32UConvertSatF32:
    case wasm::kExprI64SConvertSatF32:
    case wasm::kExprI64UConvertSatF32:
      return MachineType::Float32();
    case wasm::kExprI32SConvertF64:
    case wasm::kExprI32UConvertF64:
    case wasm::kExprI64SConvertF64:
    case wasm::kExprI64UConvertF64:
    case wasm::kExprI32SConvertSatF64:
    case wasm::kExprI32UConvertSatF64:
    case wasm::kExprI64SConvertSatF64:
    case wasm::kExprI64UConvertSatF64:
      return MachineType::Float64();
    default:
      UNREACHABLE();
  }
}

const Operator* ConvertOp(WasmGraphBuilder* builder, wasm::WasmOpcode opcode) {
  switch (opcode) {
    case wasm::kExprI32SConvertF32:
      return builder->mcgraph()->machine()->TruncateFloat32ToInt32(
          TruncateKind::kSetOverflowToMin);
    case wasm::kExprI32SConvertSatF32:
      return builder->mcgraph()->machine()->TruncateFloat32ToInt32(
          TruncateKind::kArchitectureDefault);
    case wasm::kExprI32UConvertF32:
      return builder->mcgraph()->machine()->TruncateFloat32ToUint32(
          TruncateKind::kSetOverflowToMin);
    case wasm::kExprI32UConvertSatF32:
      return builder->mcgraph()->machine()->TruncateFloat32ToUint32(
          TruncateKind::kArchitectureDefault);
    case wasm::kExprI32SConvertF64:
    case wasm::kExprI32SConvertSatF64:
      return builder->mcgraph()->machine()->ChangeFloat64ToInt32();
    case wasm::kExprI32UConvertF64:
    case wasm::kExprI32UConvertSatF64:
      return builder->mcgraph()->machine()->TruncateFloat64ToUint32();
    case wasm::kExprI64SConvertF32:
    case wasm::kExprI64SConvertSatF32:
      return builder->mcgraph()->machine()->TryTruncateFloat32ToInt64();
    case wasm::kExprI64UConvertF32:
    case wasm::kExprI64UConvertSatF32:
      return builder->mcgraph()->machine()->TryTruncateFloat32ToUint64();
    case wasm::kExprI64SConvertF64:
    case wasm::kExprI64SConvertSatF64:
      return builder->mcgraph()->machine()->TryTruncateFloat64ToInt64();
    case wasm::kExprI64UConvertF64:
    case wasm::kExprI64UConvertSatF64:
      return builder->mcgraph()->machine()->TryTruncateFloat64ToUint64();
    default:
      UNREACHABLE();
  }
}

wasm::WasmOpcode ConvertBackOp(wasm::WasmOpcode opcode) {
  switch (opcode) {
    case wasm::kExprI32SConvertF32:
    case wasm::kExprI32SConvertSatF32:
      return wasm::kExprF32SConvertI32;
    case wasm::kExprI32UConvertF32:
    case wasm::kExprI32UConvertSatF32:
      return wasm::kExprF32UConvertI32;
    case wasm::kExprI32SConvertF64:
    case wasm::kExprI32SConvertSatF64:
      return wasm::kExprF64SConvertI32;
    case wasm::kExprI32UConvertF64:
    case wasm::kExprI32UConvertSatF64:
      return wasm::kExprF64UConvertI32;
    default:
      UNREACHABLE();
  }
}

bool IsTrappingConvertOp(wasm::WasmOpcode opcode) {
  switch (opcode) {
    case wasm::kExprI32SConvertF32:
    case wasm::kExprI32UConvertF32:
    case wasm::kExprI32SConvertF64:
    case wasm::kExprI32UConvertF64:
    case wasm::kExprI64SConvertF32:
    case wasm::kExprI64UConvertF32:
    case wasm::kExprI64SConvertF64:
    case wasm::kExprI64UConvertF64:
      return true;
    case wasm::kExprI32SConvertSatF64:
    case wasm::kExprI32UConvertSatF64:
    case wasm::kExprI32SConvertSatF32:
    case wasm::kExprI32UConvertSatF32:
    case wasm::kExprI64SConvertSatF32:
    case wasm::kExprI64UConvertSatF32:
    case wasm::kExprI64SConvertSatF64:
    case wasm::kExprI64UConvertSatF64:
      return false;
    default:
      UNREACHABLE();
  }
}

Node* Zero(WasmGraphBuilder* builder, const MachineType& ty) {
  switch (ty.representation()) {
    case MachineRepresentation::kWord32:
      return builder->Int32Constant(0);
    case MachineRepresentation::kWord64:
      return builder->Int64Constant(0);
    case MachineRepresentation::kFloat32:
      return builder->Float32Constant(0.0);
    case MachineRepresentation::kFloat64:
      return builder->Float64Constant(0.0);
    default:
      UNREACHABLE();
  }
}

Node* Min(WasmGraphBuilder* builder, const MachineType& ty) {
  switch (ty.semantic()) {
    case MachineSemantic::kInt32:
      return builder->Int32Constant(std::numeric_limits<int32_t>::min());
    case MachineSemantic::kUint32:
      return builder->Int32Constant(std::numeric_limits<uint32_t>::min());
    case MachineSemantic::kInt64:
      return builder->Int64Constant(std::numeric_limits<int64_t>::min());
    case MachineSemantic::kUint64:
      return builder->Int64Constant(std::numeric_limits<uint64_t>::min());
    default:
      UNREACHABLE();
  }
}

Node* Max(WasmGraphBuilder* builder, const MachineType& ty) {
  switch (ty.semantic()) {
    case MachineSemantic::kInt32:
      return builder->Int32Constant(std::numeric_limits<int32_t>::max());
    case MachineSemantic::kUint32:
      return builder->Int32Constant(std::numeric_limits<uint32_t>::max());
    case MachineSemantic::kInt64:
      return builder->Int64Constant(std::numeric_limits<int64_t>::max());
    case MachineSemantic::kUint64:
      return builder->Int64Constant(std::numeric_limits<uint64_t>::max());
    default:
      UNREACHABLE();
  }
}

wasm::WasmOpcode TruncOp(const MachineType& ty) {
  switch (ty.representation()) {
    case MachineRepresentation::kFloat32:
      return wasm::kExprF32Trunc;
    case MachineRepresentation::kFloat64:
      return wasm::kExprF64Trunc;
    default:
      UNREACHABLE();
  }
}

wasm::WasmOpcode NeOp(const MachineType& ty) {
  switch (ty.representation()) {
    case MachineRepresentation::kFloat32:
      return wasm::kExprF32Ne;
    case MachineRepresentation::kFloat64:
      return wasm::kExprF64Ne;
    default:
      UNREACHABLE();
  }
}

wasm::WasmOpcode LtOp(const MachineType& ty) {
  switch (ty.representation()) {
    case MachineRepresentation::kFloat32:
      return wasm::kExprF32Lt;
    case MachineRepresentation::kFloat64:
      return wasm::kExprF64Lt;
    default:
      UNREACHABLE();
  }
}

Node* ConvertTrapTest(WasmGraphBuilder* builder, wasm::WasmOpcode opcode,
                      const MachineType& int_ty, const MachineType& float_ty,
                      Node* trunc, Node* converted_value) {
  if (int_ty.representation() == MachineRepresentation::kWord32) {
    Node* check = builder->Unop(ConvertBackOp(opcode), converted_value);
    return builder->Binop(NeOp(float_ty), trunc, check);
  }
  return builder->graph()->NewNode(builder->mcgraph()->common()->Projection(1),
                                   trunc, builder->graph()->start());
}

Node* ConvertSaturateTest(WasmGraphBuilder* builder, wasm::WasmOpcode opcode,
                          const MachineType& int_ty,
                          const MachineType& float_ty, Node* trunc,
                          Node* converted_value) {
  Node* test = ConvertTrapTest(builder, opcode, int_ty, float_ty, trunc,
                               converted_value);
  if (int_ty.representation() == MachineRepresentation::kWord64) {
    test = builder->Binop(wasm::kExprI64Eq, test, builder->Int64Constant(0));
  }
  return test;
}

}  // namespace

Node* WasmGraphBuilder::BuildIntConvertFloat(Node* input,
                                             wasm::WasmCodePosition position,
                                             wasm::WasmOpcode opcode) {
  const MachineType int_ty = IntConvertType(opcode);
  const MachineType float_ty = FloatConvertType(opcode);
  const Operator* conv_op = ConvertOp(this, opcode);
  Node* trunc = nullptr;
  Node* converted_value = nullptr;
  const bool is_int32 =
      int_ty.representation() == MachineRepresentation::kWord32;
  if (is_int32) {
    trunc = Unop(TruncOp(float_ty), input);
    converted_value = graph()->NewNode(conv_op, trunc);
  } else {
    trunc = graph()->NewNode(conv_op, input);
    converted_value = graph()->NewNode(mcgraph()->common()->Projection(0),
                                       trunc, graph()->start());
  }
  if (IsTrappingConvertOp(opcode)) {
    Node* test =
        ConvertTrapTest(this, opcode, int_ty, float_ty, trunc, converted_value);
    if (is_int32) {
      TrapIfTrue(wasm::kTrapFloatUnrepresentable, test, position);
    } else {
      ZeroCheck64(wasm::kTrapFloatUnrepresentable, test, position);
    }
    return converted_value;
  }
  if (mcgraph()->machine()->SatConversionIsSafe()) {
    return converted_value;
  }
  Node* test = ConvertSaturateTest(this, opcode, int_ty, float_ty, trunc,
                                   converted_value);
  Diamond tl_d(graph(), mcgraph()->common(), test, BranchHint::kFalse);
  tl_d.Chain(control());
  Node* nan_test = Binop(NeOp(float_ty), input, input);
  Diamond nan_d(graph(), mcgraph()->common(), nan_test, BranchHint::kFalse);
  nan_d.Nest(tl_d, true);
  Node* neg_test = Binop(LtOp(float_ty), input, Zero(this, float_ty));
  Diamond sat_d(graph(), mcgraph()->common(), neg_test, BranchHint::kNone);
  sat_d.Nest(nan_d, false);
  Node* sat_val =
      sat_d.Phi(int_ty.representation(), Min(this, int_ty), Max(this, int_ty));
  Node* nan_val =
      nan_d.Phi(int_ty.representation(), Zero(this, int_ty), sat_val);
  SetControl(tl_d.merge);
  return tl_d.Phi(int_ty.representation(), nan_val, converted_value);
}

Node* WasmGraphBuilder::BuildI32AsmjsSConvertF32(Node* input) {
  // asm.js must use the wacky JS semantics.
  return gasm_->TruncateFloat64ToWord32(gasm_->ChangeFloat32ToFloat64(input));
}

Node* WasmGraphBuilder::BuildI32AsmjsSConvertF64(Node* input) {
  // asm.js must use the wacky JS semantics.
  return gasm_->TruncateFloat64ToWord32(input);
}

Node* WasmGraphBuilder::BuildI32AsmjsUConvertF32(Node* input) {
  // asm.js must use the wacky JS semantics.
  return gasm_->TruncateFloat64ToWord32(gasm_->ChangeFloat32ToFloat64(input));
}

Node* WasmGraphBuilder::BuildI32AsmjsUConvertF64(Node* input) {
  // asm.js must use the wacky JS semantics.
  return gasm_->TruncateFloat64ToWord32(input);
}

Node* WasmGraphBuilder::BuildBitCountingCall(Node* input, ExternalReference ref,
                                             MachineRepresentation input_type) {
  auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                 .Params(MachineType::TypeForRepresentation(input_type, false));
  return BuildCCall(&sig, gasm_->ExternalConstant(ref), input);
}

Node* WasmGraphBuilder::BuildI32Ctz(Node* input) {
  return BuildBitCountingCall(input, ExternalReference::wasm_word32_ctz(),
                              MachineRepresentation::kWord32);
}

Node* WasmGraphBuilder::BuildI64Ctz(Node* input) {
  if (mcgraph()->machine()->Is32()) {
    Node* upper_word = gasm_->TruncateInt64ToInt32(
        Binop(wasm::kExprI64ShrU, input, Int64Constant(32)));
    Node* lower_word = gasm_->TruncateInt64ToInt32(input);
    // return lower_word == 0 ? 32 + CTZ32(upper_word) : CTZ32(lower_word);
    // Build control flow because Word32Select is not always available.
    Diamond d{graph(), mcgraph()->common(),
              gasm_->Word32Equal(lower_word, gasm_->Uint32Constant(0))};
    d.Chain(control());
    Node* original_effect = gasm_->effect();
    // Build the path that uses the upper word.
    SetControl(d.if_true);
    Node* result_from_upper = gasm_->Int32Add(
        BuildBitCountingCall(upper_word, ExternalReference::wasm_word32_ctz(),
                             MachineRepresentation::kWord32),
        gasm_->Int32Constant(32));
    Node* effect_after_upper = gasm_->effect();
    // Build the path that uses the lower word.
    SetEffectControl(original_effect, d.if_false);
    Node* result_from_lower =
        BuildBitCountingCall(lower_word, ExternalReference::wasm_word32_ctz(),
                             MachineRepresentation::kWord32);
    Node* effect_after_lower = gasm_->effect();
    // Merge the two paths.
    Node* ephi = d.EffectPhi(effect_after_upper, effect_after_lower);
    SetEffectControl(ephi, d.merge);
    Node* result_32 = d.Phi(MachineRepresentation::kWord32, result_from_upper,
                            result_from_lower);
    return gasm_->ChangeUint32ToUint64(result_32);
  }
  return gasm_->ChangeUint32ToUint64(
      BuildBitCountingCall(input, ExternalReference::wasm_word64_ctz(),
                           MachineRepresentation::kWord64));
}

Node* WasmGraphBuilder::BuildI32Popcnt(Node* input) {
  return BuildBitCountingCall(input, ExternalReference::wasm_word32_popcnt(),
                              MachineRepresentation::kWord32);
}

Node* WasmGraphBuilder::BuildI64Popcnt(Node* input) {
  if (mcgraph()->machine()->Is32()) {
    // Emit two calls to wasm_word32_popcnt.
    Node* upper_word = gasm_->TruncateInt64ToInt32(
        Binop(wasm::kExprI64ShrU, input, Int64Constant(32)));
    Node* lower_word = gasm_->TruncateInt64ToInt32(input);
    return gasm_->ChangeUint32ToUint64(gasm_->Int32Add(
        BuildBitCountingCall(lower_word,
                             ExternalReference::wasm_word32_popcnt(),
                             MachineRepresentation::kWord32),
        BuildBitCountingCall(upper_word,
                             ExternalReference::wasm_word32_popcnt(),
                             MachineRepresentation::kWord32)));
  }
  return gasm_->ChangeUint32ToUint64(
      BuildBitCountingCall(input, ExternalReference::wasm_word64_popcnt(),
                           MachineRepresentation::kWord64));
}

Node* WasmGraphBuilder::BuildF32Trunc(Node* input) {
  MachineType type = MachineType::Float32();
  ExternalReference ref = ExternalReference::wasm_f32_trunc();

  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF32Floor(Node* input) {
  MachineType type = MachineType::Float32();
  ExternalReference ref = ExternalReference::wasm_f32_floor();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF32Ceil(Node* input) {
  MachineType type = MachineType::Float32();
  ExternalReference ref = ExternalReference::wasm_f32_ceil();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF32NearestInt(Node* input) {
  MachineType type = MachineType::Float32();
  ExternalReference ref = ExternalReference::wasm_f32_nearest_int();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64Trunc(Node* input) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::wasm_f64_trunc();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64Floor(Node* input) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::wasm_f64_floor();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64Ceil(Node* input) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::wasm_f64_ceil();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64NearestInt(Node* input) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::wasm_f64_nearest_int();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64Acos(Node* input) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::f64_acos_wrapper_function();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64Asin(Node* input) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::f64_asin_wrapper_function();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64Pow(Node* left, Node* right) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::wasm_float64_pow();
  return BuildCFuncInstruction(ref, type, left, right);
}

Node* WasmGraphBuilder::BuildF64Mod(Node* left, Node* right) {
  MachineType type = MachineType::Float64();
  ExternalReference ref = ExternalReference::f64_mod_wrapper_function();
  return BuildCFuncInstruction(ref, type, left, right);
}

Node* WasmGraphBuilder::BuildCFuncInstruction(ExternalReference ref,
                                              MachineType type, Node* input0,
                                              Node* input1) {
  // We do truncation by calling a C function which calculates the result.
  // The input is passed to the C function as a byte buffer holding the two
  // input doubles. We reserve this byte buffer as a stack slot, store the
  // parameters in this buffer slots, pass a pointer to the buffer to the C
  // function, and after calling the C function we collect the return value from
  // the buffer.
  Node* stack_slot;
  if (input1) {
    stack_slot = StoreArgsInStackSlot(
        {{type.representation(), input0}, {type.representation(), input1}});
  } else {
    stack_slot = StoreArgsInStackSlot({{type.representation(), input0}});
  }

  MachineType sig_types[] = {MachineType::Pointer()};
  MachineSignature sig(0, 1, sig_types);
  Node* function = gasm_->ExternalConstant(ref);
  BuildCCall(&sig, function, stack_slot);

  return gasm_->LoadFromObject(type, stack_slot, 0);
}

Node* WasmGraphBuilder::BuildF32SConvertI64(Node* input) {
  // TODO(titzer/bradnelson): Check handlng of asm.js case.
  return BuildIntToFloatConversionInstruction(
      input, ExternalReference::wasm_int64_to_float32(),
      MachineRepresentation::kWord64, MachineType::Float32());
}
Node* WasmGraphBuilder::BuildF32UConvertI64(Node* input) {
  // TODO(titzer/bradnelson): Check handlng of asm.js case.
  return BuildIntToFloatConversionInstruction(
      input, ExternalReference::wasm_uint64_to_float32(),
      MachineRepresentation::kWord64, MachineType::Float32());
}
Node* WasmGraphBuilder::BuildF64SConvertI64(Node* input) {
  return BuildIntToFloatConversionInstruction(
      input, ExternalReference::wasm_int64_to_float64(),
      MachineRepresentation::kWord64, MachineType::Float64());
}
Node* WasmGraphBuilder::BuildF64UConvertI64(Node* input) {
  return BuildIntToFloatConversionInstruction(
      input, ExternalReference::wasm_uint64_to_float64(),
      MachineRepresentation::kWord64, MachineType::Float64());
}

Node* WasmGraphBuilder::BuildIntToFloatConversionInstruction(
    Node* input, ExternalReference ref,
    MachineRepresentation parameter_representation,
    const MachineType result_type) {
  int stack_slot_size =
      std::max(ElementSizeInBytes(parameter_representation),
               ElementSizeInBytes(result_type.representation()));
  Node* stack_slot =
      graph()->NewNode(mcgraph()->machine()->StackSlot(stack_slot_size));
  auto store_rep =
      StoreRepresentation(parameter_representation, kNoWriteBarrier);
  gasm_->Store(store_rep, stack_slot, 0, input);
  MachineType sig_types[] = {MachineType::Pointer()};
  MachineSignature sig(0, 1, sig_types);
  Node* function = gasm_->ExternalConstant(ref);
  BuildCCall(&sig, function, stack_slot);
  return gasm_->LoadFromObject(result_type, stack_slot, 0);
}

namespace {

ExternalReference convert_ccall_ref(wasm::WasmOpcode opcode) {
  switch (opcode) {
    case wasm::kExprI64SConvertF32:
    case wasm::kExprI64SConvertSatF32:
      return ExternalReference::wasm_float32_to_int64();
    case wasm::kExprI64UConvertF32:
    case wasm::kExprI64UConvertSatF32:
      return ExternalReference::wasm_float32_to_uint64();
    case wasm::kExprI64SConvertF64:
    case wasm::kExprI64SConvertSatF64:
      return ExternalReference::wasm_float64_to_int64();
    case wasm::kExprI64UConvertF64:
    case wasm::kExprI64UConvertSatF64:
      return ExternalReference::wasm_float64_to_uint64();
    default:
      UNREACHABLE();
  }
}

}  // namespace

Node* WasmGraphBuilder::BuildCcallConvertFloat(Node* input,
                                               wasm::WasmCodePosition position,
                                               wasm::WasmOpcode opcode) {
  const MachineType int_ty = IntConvertType(opcode);
  const MachineType float_ty = FloatConvertType(opcode);
  ExternalReference call_ref = convert_ccall_ref(opcode);
  int stack_slot_size = std::max(ElementSizeInBytes(int_ty.representation()),
                                 ElementSizeInBytes(float_ty.representation()));
  Node* stack_slot =
      graph()->NewNode(mcgraph()->machine()->StackSlot(stack_slot_size));
  auto store_rep =
      StoreRepresentation(float_ty.representation(), kNoWriteBarrier);
  gasm_->Store(store_rep, stack_slot, 0, input);
  MachineType sig_types[] = {MachineType::Int32(), MachineType::Pointer()};
  MachineSignature sig(1, 1, sig_types);
  Node* function = gasm_->ExternalConstant(call_ref);
  Node* overflow = BuildCCall(&sig, function, stack_slot);
  if (IsTrappingConvertOp(opcode)) {
    ZeroCheck32(wasm::kTrapFloatUnrepresentable, overflow, position);
    return gasm_->LoadFromObject(int_ty, stack_slot, 0);
  }
  Node* test = Binop(wasm::kExprI32Eq, overflow, Int32Constant(0), position);
  Diamond tl_d(graph(), mcgraph()->common(), test, BranchHint::kFalse);
  tl_d.Chain(control());
  Node* nan_test = Binop(NeOp(float_ty), input, input);
  Diamond nan_d(graph(), mcgraph()->common(), nan_test, BranchHint::kFalse);
  nan_d.Nest(tl_d, true);
  Node* neg_test = Binop(LtOp(float_ty), input, Zero(this, float_ty));
  Diamond sat_d(graph(), mcgraph()->common(), neg_test, BranchHint::kNone);
  sat_d.Nest(nan_d, false);
  Node* sat_val =
      sat_d.Phi(int_ty.representation(), Min(this, int_ty), Max(this, int_ty));
  Node* load = gasm_->LoadFromObject(int_ty, stack_slot, 0);
  Node* nan_val =
      nan_d.Phi(int_ty.representation(), Zero(this, int_ty), sat_val);
  return tl_d.Phi(int_ty.representation(), nan_val, load);
}

Node* WasmGraphBuilder::MemoryGrow(const wasm::WasmMemory* memory,
                                   Node* input) {
  needs_stack_check_ = true;
  if (!memory->is_memory64) {
    // For 32-bit memories, just call the builtin.
    return gasm_->CallBuiltinThroughJumptable(
        Builtin::kWasmMemoryGrow, Operator::kNoThrow,
        gasm_->Int32Constant(memory->index), input);
  }

  // If the input is not a positive int32, growing will always fail
  // (growing negative or requesting >= 256 TB).
  Node* old_effect = effect();
  Diamond is_32_bit(graph(), mcgraph()->common(),
                    gasm_->Uint64LessThanOrEqual(input, Int64Constant(kMaxInt)),
                    BranchHint::kTrue);
  is_32_bit.Chain(control());

  SetControl(is_32_bit.if_true);

  Node* grow_result =
      gasm_->ChangeInt32ToInt64(gasm_->CallBuiltinThroughJumptable(
          Builtin::kWasmMemoryGrow, Operator::kNoThrow,
          gasm_->Int32Constant(memory->index),
          gasm_->TruncateInt64ToInt32(input)));

  Node* diamond_result = is_32_bit.Phi(MachineRepresentation::kWord64,
                                       grow_result, gasm_->Int64Constant(-1));
  SetEffectControl(is_32_bit.EffectPhi(effect(), old_effect), is_32_bit.merge);
  return diamond_result;
}

Node* WasmGraphBuilder::Throw(uint32_t tag_index, const wasm::WasmTag* tag,
                              const base::Vector<Node*> values,
                              wasm::WasmCodePosition position) {
  needs_stack_check_ = true;
  uint32_t encoded_size = WasmExceptionPackage::GetEncodedSize(tag);

  Node* values_array = gasm_->CallBuiltinThroughJumptable(
      Builtin::kWasmAllocateFixedArray, Operator::kNoThrow,
      gasm_->IntPtrConstant(encoded_size));
  SetSourcePosition(values_array, position);

  uint32_t index = 0;
  const wasm::WasmTagSig* sig = tag->sig;
  MachineOperatorBuilder* m = mcgraph()->machine();
  for (size_t i = 0; i < sig->parameter_count(); ++i) {
    Node* value = values[i];
    switch (sig->GetParam(i).kind()) {
      case wasm::kF32:
        value = gasm_->BitcastFloat32ToInt32(value);
        [[fallthrough]];
      case wasm::kI32:
        BuildEncodeException32BitValue(values_array, &index, value);
        break;
      case wasm::kF64:
        value = gasm_->BitcastFloat64ToInt64(value);
        [[fallthrough]];
      case wasm::kI64: {
        Node* upper32 = gasm_->TruncateInt64ToInt32(
            Binop(wasm::kExprI64ShrU, value, Int64Constant(32)));
        BuildEncodeException32BitValue(values_array, &index, upper32);
        Node* lower32 = gasm_->TruncateInt64ToInt32(value);
        BuildEncodeException32BitValue(values_array, &index, lower32);
        break;
      }
      case wasm::kS128:
        BuildEncodeException32BitValue(
            values_array, &index,
            graph()->NewNode(m->I32x4ExtractLane(0), value));
        BuildEncodeException32BitValue(
            values_array, &index,
            graph()->NewNode(m->I32x4ExtractLane(1), value));
        BuildEncodeException32BitValue(
            values_array, &index,
            graph()->NewNode(m->I32x4ExtractLane(2), value));
        BuildEncodeException32BitValue(
            values_array, &index,
            graph()->NewNode(m->I32x4ExtractLane(3), value));
        break;
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kRtt:
        gasm_->StoreFixedArrayElementAny(values_array, index, value);
        ++index;
        break;
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }
  }
  DCHECK_EQ(encoded_size, index);

  Node* exception_tag = LoadTagFromTable(tag_index);

  Node* throw_call = gasm_->CallBuiltinThroughJumptable(
      Builtin::kWasmThrow, Operator::kNoProperties, exception_tag,
      values_array);
  SetSourcePosition(throw_call, position);
  return throw_call;
}

void WasmGraphBuilder::BuildEncodeException32BitValue(Node* values_array,
                                                      uint32_t* index,
                                                      Node* value) {
  Node* upper_halfword_as_smi =
      gasm_->BuildChangeUint31ToSmi(gasm_->Word32Shr(value, Int32Constant(16)));
  gasm_->StoreFixedArrayElementSmi(values_array, *index, upper_halfword_as_smi);
  ++(*index);
  Node* lower_halfword_as_smi = gasm_->BuildChangeUint31ToSmi(
      gasm_->Word32And(value, Int32Constant(0xFFFFu)));
  gasm_->StoreFixedArrayElementSmi(values_array, *index, lower_halfword_as_smi);
  ++(*index);
}

Node* WasmGraphBuilder::BuildDecodeException32BitValue(Node* values_array,
                                                       uint32_t* index) {
  Node* upper = gasm_->BuildChangeSmiToInt32(
      gasm_->LoadFixedArrayElementSmi(values_array, *index));
  (*index)++;
  upper = gasm_->Word32Shl(upper, Int32Constant(16));
  Node* lower = gasm_->BuildChangeSmiToInt32(
      gasm_->LoadFixedArrayElementSmi(values_array, *index));
  (*index)++;
  Node* value = gasm_->Word32Or(upper, lower);
  return value;
}

Node* WasmGraphBuilder::BuildDecodeException64BitValue(Node* values_array,
                                                       uint32_t* index) {
  Node* upper = Binop(wasm::kExprI64Shl,
                      Unop(wasm::kExprI64UConvertI32,
                           BuildDecodeException32BitValue(values_array, index)),
                      Int64Constant(32));
  Node* lower = Unop(wasm::kExprI64UConvertI32,
                     BuildDecodeException32BitValue(values_array, index));
  return Binop(wasm::kExprI64Ior, upper, lower);
}

Node* WasmGraphBuilder::Rethrow(Node* except_obj) {
  // TODO(v8:8091): Currently the message of the original exception is not being
  // preserved when rethrown to the console. The pending message will need to be
  // saved when caught and restored here while being rethrown.
  return gasm_->CallBuiltinThroughJumptable(
      Builtin::kWasmRethrow, Operator::kNoProperties, except_obj);
}

Node* WasmGraphBuilder::IsExceptionTagUndefined(Node* tag) {
  return gasm_->TaggedEqual(tag, UndefinedValue());
}

Node* WasmGraphBuilder::LoadJSTag() {
  Node* context =
      LOAD_INSTANCE_FIELD(NativeContext, MachineType::TaggedPointer());
  Node* tag_obj =
      gasm_->Load(MachineType::TaggedPointer(), context,
                  NativeContext::SlotOffset(Context::WASM_JS_TAG_INDEX));
  return gasm_->Load(MachineType::TaggedPointer(), tag_obj,
                     wasm::ObjectAccess::ToTagged(WasmTagObject::kTagOffset));
}

Node* WasmGraphBuilder::ExceptionTagEqual(Node* caught_tag,
                                          Node* expected_tag) {
  return gasm_->WordEqual(caught_tag, expected_tag);
}

Node* WasmGraphBuilder::LoadTagFromTable(uint32_t tag_index) {
  Node* tags_table =
      LOAD_INSTANCE_FIELD(TagsTable, MachineType::TaggedPointer());
  Node* tag = gasm_->LoadFixedArrayElementPtr(tags_table, tag_index);
  return tag;
}

Node* WasmGraphBuilder::GetExceptionTag(Node* except_obj) {
  return gasm_->CallBuiltin(
      Builtin::kWasmGetOwnProperty, Operator::kEliminatable, except_obj,
      LOAD_ROOT(wasm_exception_tag_symbol, wasm_exception_tag_symbol),
      LOAD_INSTANCE_FIELD(NativeContext, MachineType::TaggedPointer()));
}

Node* WasmGraphBuilder::GetExceptionValues(Node* except_obj,
                                           const wasm::WasmTag* tag,
                                           base::Vector<Node*> values) {
  Node* values_array = gasm_->CallBuiltin(
      Builtin::kWasmGetOwnProperty, Operator::kEliminatable, except_obj,
      LOAD_ROOT(wasm_exception_values_symbol, wasm_exception_values_symbol),
      LOAD_INSTANCE_FIELD(NativeContext, MachineType::TaggedPointer()));
  uint32_t index = 0;
  const wasm::WasmTagSig* sig = tag->sig;
  DCHECK_EQ(sig->parameter_count(), values.size());
  for (size_t i = 0; i < sig->parameter_count(); ++i) {
    Node* value;
    switch (sig->GetParam(i).kind()) {
      case wasm::kI32:
        value = BuildDecodeException32BitValue(values_array, &index);
        break;
      case wasm::kI64:
        value = BuildDecodeException64BitValue(values_array, &index);
        break;
      case wasm::kF32: {
        value = Unop(wasm::kExprF32ReinterpretI32,
                     BuildDecodeException32BitValue(values_array, &index));
        break;
      }
      case wasm::kF64: {
        value = Unop(wasm::kExprF64ReinterpretI64,
                     BuildDecodeException64BitValue(values_array, &index));
        break;
      }
      case wasm::kS128:
        value = graph()->NewNode(
            mcgraph()->machine()->I32x4Splat(),
            BuildDecodeException32BitValue(values_array, &index));
        value = graph()->NewNode(
            mcgraph()->machine()->I32x4ReplaceLane(1), value,
            BuildDecodeException32BitValue(values_array, &index));
        value = graph()->NewNode(
            mcgraph()->machine()->I32x4ReplaceLane(2), value,
            BuildDecodeException32BitValue(values_array, &index));
        value = graph()->NewNode(
            mcgraph()->machine()->I32x4ReplaceLane(3), value,
            BuildDecodeException32BitValue(values_array, &index));
        break;
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kRtt:
        value = gasm_->LoadFixedArrayElementAny(values_array, index);
        ++index;
        break;
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }
    values[i] = value;
  }
  DCHECK_EQ(index, WasmExceptionPackage::GetEncodedSize(tag));
  return values_array;
}

Node* WasmGraphBuilder::BuildI32DivS(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  ZeroCheck32(wasm::kTrapDivByZero, right, position);
  Node* previous_effect = effect();
  auto [denom_is_m1, denom_is_not_m1] =
      BranchExpectFalse(gasm_->Word32Equal(right, Int32Constant(-1)));
  SetControl(denom_is_m1);
  TrapIfEq32(wasm::kTrapDivUnrepresentable, left, kMinInt, position);
  Node* merge = Merge(control(), denom_is_not_m1);
  SetEffectControl(graph()->NewNode(mcgraph()->common()->EffectPhi(2), effect(),
                                    previous_effect, merge),
                   merge);
  return gasm_->Int32Div(left, right);
}

Node* WasmGraphBuilder::BuildI32RemS(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  MachineOperatorBuilder* m = mcgraph()->machine();

  ZeroCheck32(wasm::kTrapRemByZero, right, position);

  Diamond d(graph(), mcgraph()->common(),
            gasm_->Word32Equal(right, Int32Constant(-1)), BranchHint::kFalse);
  d.Chain(control());

  return d.Phi(MachineRepresentation::kWord32, Int32Constant(0),
               graph()->NewNode(m->Int32Mod(), left, right, d.if_false));
}

Node* WasmGraphBuilder::BuildI32DivU(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  ZeroCheck32(wasm::kTrapDivByZero, right, position);
  return gasm_->Uint32Div(left, right);
}

Node* WasmGraphBuilder::BuildI32RemU(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  ZeroCheck32(wasm::kTrapRemByZero, right, position);
  return gasm_->Uint32Mod(left, right);
}

Node* WasmGraphBuilder::BuildI32AsmjsDivS(Node* left, Node* right) {
  MachineOperatorBuilder* m = mcgraph()->machine();

  Int32Matcher mr(right);
  if (mr.HasResolvedValue()) {
    if (mr.ResolvedValue() == 0) {
      return Int32Constant(0);
    } else if (mr.ResolvedValue() == -1) {
      // The result is the negation of the left input.
      return gasm_->Int32Sub(Int32Constant(0), left);
    }
    return gasm_->Int32Div(left, right);
  }

  // asm.js semantics return 0 on divide or mod by zero.
  if (m->Int32DivIsSafe()) {
    // The hardware instruction does the right thing (e.g. arm).
    return gasm_->Int32Div(left, right);
  }

  // Check denominator for zero.
  Diamond z(graph(), mcgraph()->common(),
            gasm_->Word32Equal(right, Int32Constant(0)), BranchHint::kFalse);
  z.Chain(control());

  // Check denominator for -1. (avoid minint / -1 case).
  Diamond n(graph(), mcgraph()->common(),
            gasm_->Word32Equal(right, Int32Constant(-1)), BranchHint::kFalse);
  n.Chain(z.if_false);

  Node* div = graph()->NewNode(m->Int32Div(), left, right, n.if_false);

  Node* neg = gasm_->Int32Sub(Int32Constant(0), left);

  return z.Phi(MachineRepresentation::kWord32, Int32Constant(0),
               n.Phi(MachineRepresentation::kWord32, neg, div));
}

Node* WasmGraphBuilder::BuildI32AsmjsRemS(Node* left, Node* right) {
  CommonOperatorBuilder* c = mcgraph()->common();
  MachineOperatorBuilder* m = mcgraph()->machine();
  Node* const zero = Int32Constant(0);

  Int32Matcher mr(right);
  if (mr.HasResolvedValue()) {
    if (mr.ResolvedValue() == 0 || mr.ResolvedValue() == -1) {
      return zero;
    }
    return gasm_->Int32Mod(left, right);
  }

  // General case for signed integer modulus, with optimization for (unknown)
  // power of 2 right hand side.
  //
  //   if 0 < right then
  //     msk = right - 1
  //     if right & msk != 0 then
  //       left % right
  //     else
  //       if left < 0 then
  //         -(-left & msk)
  //       else
  //         left & msk
  //   else
  //     if right < -1 then
  //       left % right
  //     else
  //       zero
  //
  // Note: We do not use the Diamond helper class here, because it really hurts
  // readability with nested diamonds.
  Node* const minus_one = Int32Constant(-1);

  const Operator* const merge_op = c->Merge(2);
  const Operator* const phi_op = c->Phi(MachineRepresentation::kWord32, 2);

  Node* check0 = gasm_->Int32LessThan(zero, right);
  Node* branch0 =
      graph()->NewNode(c->Branch(BranchHint::kTrue), check0, control());

  Node* if_true0 = graph()->NewNode(c->IfTrue(), branch0);
  Node* true0;
  {
    Node* msk = graph()->NewNode(m->Int32Add(), right, minus_one);

    Node* check1 = graph()->NewNode(m->Word32And(), right, msk);
    Node* branch1 = graph()->NewNode(c->Branch(), check1, if_true0);

    Node* if_true1 = graph()->NewNode(c->IfTrue(), branch1);
    Node* true1 = graph()->NewNode(m->Int32Mod(), left, right, if_true1);

    Node* if_false1 = graph()->NewNode(c->IfFalse(), branch1);
    Node* false1;
    {
      Node* check2 = graph()->NewNode(m->Int32LessThan(), left, zero);
      Node* branch2 =
          graph()->NewNode(c->Branch(BranchHint::kFalse), check2, if_false1);

      Node* if_true2 = graph()->NewNode(c->IfTrue(), branch2);
      Node* true2 = graph()->NewNode(
          m->Int32Sub(), zero,
          graph()->NewNode(m->Word32And(),
                           graph()->NewNode(m->Int32Sub(), zero, left), msk));

      Node* if_false2 = graph()->NewNode(c->IfFalse(), branch2);
      Node* false2 = graph()->NewNode(m->Word32And(), left, msk);

      if_false1 = graph()->NewNode(merge_op, if_true2, if_false2);
      false1 = graph()->NewNode(phi_op, true2, false2, if_false1);
    }

    if_true0 = graph()->NewNode(merge_op, if_true1, if_false1);
    true0 = graph()->NewNode(phi_op, true1, false1, if_true0);
  }

  Node* if_false0 = graph()->NewNode(c->IfFalse(), branch0);
  Node* false0;
  {
    Node* check1 = graph()->NewNode(m->Int32LessThan(), right, minus_one);
    Node* branch1 =
        graph()->NewNode(c->Branch(BranchHint::kTrue), check1, if_false0);

    Node* if_true1 = graph()->NewNode(c->IfTrue(), branch1);
    Node* true1 = graph()->NewNode(m->Int32Mod(), left, right, if_true1);

    Node* if_false1 = graph()->NewNode(c->IfFalse(), branch1);
    Node* false1 = zero;

    if_false0 = graph()->NewNode(merge_op, if_true1, if_false1);
    false0 = graph()->NewNode(phi_op, true1, false1, if_false0);
  }

  Node* merge0 = graph()->NewNode(merge_op, if_true0, if_false0);
  return graph()->NewNode(phi_op, true0, false0, merge0);
}

Node* WasmGraphBuilder::BuildI32AsmjsDivU(Node* left, Node* right) {
  MachineOperatorBuilder* m = mcgraph()->machine();
  // asm.js semantics return 0 on divide or mod by zero.
  if (m->Uint32DivIsSafe()) {
    // The hardware instruction does the right thing (e.g. arm).
    return gasm_->Uint32Div(left, right);
  }

  // Explicit check for x / 0.
  Diamond z(graph(), mcgraph()->common(),
            gasm_->Word32Equal(right, Int32Constant(0)), BranchHint::kFalse);
  z.Chain(control());

  return z.Phi(MachineRepresentation::kWord32, Int32Constant(0),
               graph()->NewNode(mcgraph()->machine()->Uint32Div(), left, right,
                                z.if_false));
}

Node* WasmGraphBuilder::BuildI32AsmjsRemU(Node* left, Node* right) {
  // asm.js semantics return 0 on divide or mod by zero.
  // Explicit check for x % 0.
  Diamond z(graph(), mcgraph()->common(),
            gasm_->Word32Equal(right, Int32Constant(0)), BranchHint::kFalse);
  z.Chain(control());

  Node* rem = graph()->NewNode(mcgraph()->machine()->Uint32Mod(), left, right,
                               z.if_false);
  return z.Phi(MachineRepresentation::kWord32, Int32Constant(0), rem);
}

Node* WasmGraphBuilder::BuildI64DivS(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  if (mcgraph()->machine()->Is32()) {
    return BuildDiv64Call(left, right, ExternalReference::wasm_int64_div(),
                          MachineType::Int64(), wasm::kTrapDivByZero, position);
  }
  ZeroCheck64(wasm::kTrapDivByZero, right, position);
  Node* previous_effect = effect();
  auto [denom_is_m1, denom_is_not_m1] =
      BranchExpectFalse(gasm_->Word64Equal(right, Int64Constant(-1)));
  SetControl(denom_is_m1);
  TrapIfEq64(wasm::kTrapDivUnrepresentable, left,
             std::numeric_limits<int64_t>::min(), position);
  Node* merge = Merge(control(), denom_is_not_m1);
  SetEffectControl(graph()->NewNode(mcgraph()->common()->EffectPhi(2), effect(),
                                    previous_effect, merge),
                   merge);
  return gasm_->Int64Div(left, right);
}

Node* WasmGraphBuilder::BuildI64RemS(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  if (mcgraph()->machine()->Is32()) {
    return BuildDiv64Call(left, right, ExternalReference::wasm_int64_mod(),
                          MachineType::Int64(), wasm::kTrapRemByZero, position);
  }
  ZeroCheck64(wasm::kTrapRemByZero, right, position);
  Diamond d(mcgraph()->graph(), mcgraph()->common(),
            gasm_->Word64Equal(right, Int64Constant(-1)));

  d.Chain(control());

  Node* rem = graph()->NewNode(mcgraph()->machine()->Int64Mod(), left, right,
                               d.if_false);

  return d.Phi(MachineRepresentation::kWord64, Int64Constant(0), rem);
}

Node* WasmGraphBuilder::BuildI64DivU(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  if (mcgraph()->machine()->Is32()) {
    return BuildDiv64Call(left, right, ExternalReference::wasm_uint64_div(),
                          MachineType::Int64(), wasm::kTrapDivByZero, position);
  }
  ZeroCheck64(wasm::kTrapDivByZero, right, position);
  return gasm_->Uint64Div(left, right);
}
Node* WasmGraphBuilder::BuildI64RemU(Node* left, Node* right,
                                     wasm::WasmCodePosition position) {
  if (mcgraph()->machine()->Is32()) {
    return BuildDiv64Call(left, right, ExternalReference::wasm_uint64_mod(),
                          MachineType::Int64(), wasm::kTrapRemByZero, position);
  }
  ZeroCheck64(wasm::kTrapRemByZero, right, position);
  return gasm_->Uint64Mod(left, right);
}

Node* WasmGraphBuilder::BuildDiv64Call(Node* left, Node* right,
                                       ExternalReference ref,
                                       MachineType result_type,
                                       wasm::TrapReason trap_zero,
                                       wasm::WasmCodePosition position) {
  Node* stack_slot =
      StoreArgsInStackSlot({{MachineRepresentation::kWord64, left},
                            {MachineRepresentation::kWord64, right}});

  MachineType sig_types[] = {MachineType::Int32(), MachineType::Pointer()};
  MachineSignature sig(1, 1, sig_types);

  Node* function = gasm_->ExternalConstant(ref);
  Node* call = BuildCCall(&sig, function, stack_slot);

  ZeroCheck32(trap_zero, call, position);
  TrapIfEq32(wasm::kTrapDivUnrepresentable, call, -1, position);
  return gasm_->Load(result_type, stack_slot, 0);
}

Node* WasmGraphBuilder::IsNull(Node* object, wasm::ValueType type) {
  // We immediately lower null in wrappers, as they do not go through a lowering
  // phase.
  return parameter_mode_ == kInstanceParameterMode
             ? gasm_->IsNull(object, type)
             : gasm_->TaggedEqual(object, RefNull(type));
}

template <typename... Args>
Node* WasmGraphBuilder::BuildCCall(MachineSignature* sig, Node* function,
                                   Args... args) {
  DCHECK_LE(sig->return_count(), 1);
  DCHECK_EQ(sizeof...(args), sig->parameter_count());
  Node* call_args[] = {function, args..., effect(), control()};

  auto call_descriptor =
      Linkage::GetSimplifiedCDescriptor(mcgraph()->zone(), sig);

  return gasm_->Call(call_descriptor, arraysize(call_args), call_args);
}

Node* WasmGraphBuilder::BuildCallNode(const wasm::FunctionSig* sig,
                                      base::Vector<Node*> args,
                                      wasm::WasmCodePosition position,
                                      Node* implicit_first_arg,
                                      const Operator* op, Node* frame_state) {
  needs_stack_check_ = true;
  const size_t params = sig->parameter_count();
  const size_t has_frame_state = frame_state != nullptr ? 1 : 0;
  const size_t extra = 3;  // instance_node, effect, and control.
  const size_t count = 1 + params + extra + has_frame_state;

  // Reallocate the buffer to make space for extra inputs.
  base::SmallVector<Node*, 16 + extra> inputs(count);
  DCHECK_EQ(1 + params, args.size());

  // Make room for the first argument at index 1, just after code.
  inputs[0] = args[0];  // code
  inputs[1] = implicit_first_arg;
  if (params > 0) memcpy(&inputs[2], &args[1], params * sizeof(Node*));

  // Add effect and control inputs.
  if (has_frame_state != 0) inputs[params + 2] = frame_state;
  inputs[params + has_frame_state + 2] = effect();
  inputs[params + has_frame_state + 3] = control();

  Node* call = graph()->NewNode(op, static_cast<int>(count), inputs.begin());
  // Return calls have no effect output. Other calls are the new effect node.
  if (op->EffectOutputCount() > 0) SetEffect(call);
  DCHECK(position == wasm::kNoCodePosition || position > 0);
  if (position > 0) SetSourcePosition(call, position);

  return call;
}

Node* WasmGraphBuilder::BuildWasmCall(const wasm::FunctionSig* sig,
                                      base::Vector<Node*> args,
                                      base::Vector<Node*> rets,
                                      wasm::WasmCodePosition position,
                                      Node* implicit_first_arg,
                                      Node* frame_state) {
  CallDescriptor* call_descriptor = GetWasmCallDescriptor(
      mcgraph()->zone(), sig, kWasmFunction, frame_state != nullptr);
  const Operator* op = mcgraph()->common()->Call(call_descriptor);
  Node* call =
      BuildCallNode(sig, args, position, implicit_first_arg, op, frame_state);
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

Node* WasmGraphBuilder::BuildWasmReturnCall(const wasm::FunctionSig* sig,
                                            base::Vector<Node*> args,
                                            wasm::WasmCodePosition position,
                                            Node* implicit_first_arg) {
  CallDescriptor* call_descriptor =
      GetWasmCallDescriptor(mcgraph()->zone(), sig);
  const Operator* op = mcgraph()->common()->TailCall(call_descriptor);
  Node* call = BuildCallNode(sig, args, position, implicit_first_arg, op);

  // TODO(manoskouk): If we have kNoThrow calls, do not merge them to end.
  DCHECK_GT(call->op()->ControlOutputCount(), 0);
  gasm_->MergeControlToEnd(call);

  return call;
}

Node* WasmGraphBuilder::BuildImportCall(const wasm::FunctionSig* sig,
                                        base::Vector<Node*> args,
                                        base::Vector<Node*> rets,
                                        wasm::WasmCodePosition position,
                                        int func_index,
                                        IsReturnCall continuation) {
  return BuildImportCall(sig, args, rets, position,
                         gasm_->Uint32Constant(func_index), continuation);
}

Node* WasmGraphBuilder::BuildImportCall(
    const wasm::FunctionSig* sig, base::Vector<Node*> args,
    base::Vector<Node*> rets, wasm::WasmCodePosition position, Node* func_index,
    IsReturnCall continuation, Node* frame_state) {
  // Load the imported function refs array from the instance.
  Node* dispatch_table =
      LOAD_IMMUTABLE_PROTECTED_INSTANCE_FIELD(DispatchTableForImports);
  // Access fixed array at {header_size - tag + func_index * kTaggedSize}.
  Node* func_index_intptr = gasm_->BuildChangeUint32ToUintPtr(func_index);
  Node* dispatch_table_entry_offset = gasm_->IntMul(
      func_index_intptr, gasm_->IntPtrConstant(WasmDispatchTable::kEntrySize));
  Node* implicit_arg = gasm_->LoadProtectedPointerFromObject(
      dispatch_table,
      gasm_->IntAdd(dispatch_table_entry_offset,
                    gasm_->IntPtrConstant(wasm::ObjectAccess::ToTagged(
                        WasmDispatchTable::kEntriesOffset +
                        WasmDispatchTable::kImplicitArgBias))));

  Node* target = gasm_->LoadFromObject(
      MachineType::Pointer(), dispatch_table,
      gasm_->IntAdd(dispatch_table_entry_offset,
                    gasm_->IntPtrConstant(wasm::ObjectAccess::ToTagged(
                        WasmDispatchTable::kEntriesOffset +
                        WasmDispatchTable::kTargetBias))));

  args[0] = target;

  switch (continuation) {
    case kCallContinues:
      return BuildWasmCall(sig, args, rets, position, implicit_arg,
                           frame_state);
    case kReturnCall:
      DCHECK(rets.empty());
      return BuildWasmReturnCall(sig, args, position, implicit_arg);
  }
}

Node* WasmGraphBuilder::CallDirect(uint32_t index, base::Vector<Node*> args,
                                   base::Vector<Node*> rets,
                                   wasm::WasmCodePosition position) {
  DCHECK_NULL(args[0]);
  const wasm::FunctionSig* sig = env_->module->functions[index].sig;

  if (env_ && index < env_->module->num_imported_functions) {
    // Call to an imported function.
    return BuildImportCall(sig, args, rets, position, index, kCallContinues);
  }

  // A direct call to a wasm function defined in this module.
  // Just encode the function index. This will be patched at instantiation.
  Address code = static_cast<Address>(index);
  args[0] = mcgraph()->RelocatableIntPtrConstant(code, RelocInfo::WASM_CALL);

  return BuildWasmCall(sig, args, rets, position, GetInstanceData());
}

Node* WasmGraphBuilder::CallIndirect(uint32_t table_index, uint32_t sig_index,
                                     base::Vector<Node*> args,
                                     base::Vector<Node*> rets,
                                     wasm::WasmCodePosition position) {
  return BuildIndirectCall(table_index, sig_index, args, rets, position,
                           kCallContinues);
}

Node* WasmGraphBuilder::BuildIndirectCall(uint32_t table_index,
                                          uint32_t sig_index,
                                          base::Vector<Node*> args,
                                          base::Vector<Node*> rets,
                                          wasm::WasmCodePosition position,
                                          IsReturnCall continuation) {
  DCHECK_NOT_NULL(args[0]);
  DCHECK_NOT_NULL(env_);

  // Load the dispatch table.
  Node* dispatch_table;
  if (table_index == 0) {
    dispatch_table = LOAD_PROTECTED_INSTANCE_FIELD(DispatchTable0);
  } else {
    Node* dispatch_tables =
        LOAD_IMMUTABLE_PROTECTED_INSTANCE_FIELD(DispatchTables);
    dispatch_table = gasm_->LoadProtectedPointerFromObject(
        dispatch_tables,
        wasm::ObjectAccess::ToTagged(
            ProtectedFixedArray::OffsetOfElementAt(table_index)));
  }

  // Bounds check the index.
  Node* index = args[0];
  const wasm::WasmTable& table = env_->module->tables[table_index];
  TableTypeToUintPtrOrOOBTrap(table.is_table64, {&index}, position);
  const bool needs_dynamic_size =
      !table.has_maximum_size || table.maximum_size != table.initial_size;
  Node* table_size =
      needs_dynamic_size
          ? gasm_->LoadFromObject(
                MachineType::Int32(), dispatch_table,
                wasm::ObjectAccess::ToTagged(WasmDispatchTable::kLengthOffset))
          : Int32Constant(table.initial_size);
  Node* in_bounds = Is64() ? gasm_->Uint64LessThan(
                                 index, gasm_->ChangeUint32ToUint64(table_size))
                           : gasm_->Uint32LessThan(index, table_size);
  TrapIfFalse(wasm::kTrapTableOutOfBounds, in_bounds, position);

  wasm::ValueType table_type = env_->module->tables[table_index].type;
  bool needs_type_check = !wasm::EquivalentTypes(
      table_type.AsNonNull(), wasm::ValueType::Ref(sig_index), env_->module,
      env_->module);
  bool needs_null_check = table_type.is_nullable();

  Node* dispatch_table_entry_offset = gasm_->IntAdd(
      gasm_->IntPtrConstant(
          wasm::ObjectAccess::ToTagged(WasmDispatchTable::kEntriesOffset)),
      gasm_->IntMul(index,
                    gasm_->IntPtrConstant(WasmDispatchTable::kEntrySize)));

  // Skip check if table type matches declared signature.
  if (needs_type_check) {
    // Embed the expected signature ID as a relocatable constant.
    uint32_t canonical_sig_id =
        env_->module->isorecursive_canonical_type_ids[sig_index];
    Node* expected_sig_id = mcgraph()->RelocatableInt32Constant(
        canonical_sig_id, RelocInfo::WASM_CANONICAL_SIG_ID);

    Node* loaded_sig = gasm_->LoadFromObject(
        MachineType::Int32(), dispatch_table,
        gasm_->IntAdd(dispatch_table_entry_offset,
                      gasm_->IntPtrConstant(WasmDispatchTable::kSigBias)));
    Node* sig_match = gasm_->Word32Equal(loaded_sig, expected_sig_id);

    if (!env_->module->types[sig_index].is_final) {
      // Do a full subtyping check.
      auto end_label = gasm_->MakeLabel();
      gasm_->GotoIf(sig_match, &end_label);

      // Trap on null element.
      if (needs_null_check) {
        TrapIfTrue(wasm::kTrapFuncSigMismatch,
                   gasm_->Word32Equal(loaded_sig, Int32Constant(-1)), position);
      }

      Node* formal_rtt = RttCanon(sig_index);
      int rtt_depth = wasm::GetSubtypingDepth(env_->module, sig_index);
      DCHECK_GE(rtt_depth, 0);

      // Since we have the canonical index of the real rtt, we have to load it
      // from the isolate rtt-array (which is canonically indexed). Since this
      // reference is weak, we have to promote it to a strong reference.
      // Note: The reference cannot have been cleared: Since the loaded_sig
      // corresponds to a function of the same canonical type, that function
      // will have kept the type alive.
      Node* rtts = LOAD_MUTABLE_ROOT(WasmCanonicalRtts, wasm_canonical_rtts);
      Node* real_rtt =
          gasm_->WordAnd(gasm_->LoadWeakArrayListElement(rtts, loaded_sig),
                         gasm_->IntPtrConstant(~kWeakHeapObjectMask));
      Node* type_info = gasm_->LoadWasmTypeInfo(real_rtt);

      // If the depth of the rtt is known to be less than the minimum supertype
      // array length, we can access the supertype without bounds-checking the
      // supertype array.
      if (static_cast<uint32_t>(rtt_depth) >=
          wasm::kMinimumSupertypeArraySize) {
        Node* supertypes_length =
            gasm_->BuildChangeSmiToIntPtr(gasm_->LoadImmutableFromObject(
                MachineType::TaggedSigned(), type_info,
                wasm::ObjectAccess::ToTagged(
                    WasmTypeInfo::kSupertypesLengthOffset)));
        TrapIfFalse(wasm::kTrapFuncSigMismatch,
                    gasm_->UintLessThan(gasm_->IntPtrConstant(rtt_depth),
                                        supertypes_length),
                    position);
      }

      Node* maybe_match = gasm_->LoadImmutableFromObject(
          MachineType::TaggedPointer(), type_info,
          wasm::ObjectAccess::ToTagged(WasmTypeInfo::kSupertypesOffset +
                                       kTaggedSize * rtt_depth));
      TrapIfFalse(wasm::kTrapFuncSigMismatch,
                  gasm_->TaggedEqual(maybe_match, formal_rtt), position);
      gasm_->Goto(&end_label);

      gasm_->Bind(&end_label);
    } else {
      // In absence of subtyping, we just need to check for type equality.
      TrapIfFalse(wasm::kTrapFuncSigMismatch, sig_match, position);
    }
  } else if (needs_null_check) {
    Node* loaded_sig = gasm_->LoadFromObject(
        MachineType::Int32(), dispatch_table,
        gasm_->IntAdd(dispatch_table_entry_offset,
                      gasm_->IntPtrConstant(WasmDispatchTable::kSigBias)));
    TrapIfTrue(wasm::kTrapFuncSigMismatch,
               gasm_->Word32Equal(loaded_sig, Int32Constant(-1)), position);
  }

  Node* implicit_arg = gasm_->LoadProtectedPointerFromObject(
      dispatch_table, gasm_->IntAdd(dispatch_table_entry_offset,
                                    gasm_->IntPtrConstant(
                                        WasmDispatchTable::kImplicitArgBias)));

  Node* target = gasm_->LoadFromObject(
      MachineType::Pointer(), dispatch_table,
      gasm_->IntAdd(dispatch_table_entry_offset,
                    gasm_->IntPtrConstant(WasmDispatchTable::kTargetBias)));
  args[0] = target;

  const wasm::FunctionSig* sig = env_->module->signature(sig_index);

  switch (continuation) {
    case kCallContinues:
      return BuildWasmCall(sig, args, rets, position, implicit_arg);
    case kReturnCall:
      return BuildWasmReturnCall(sig, args, position, implicit_arg);
  }
}

Node* WasmGraphBuilder::BuildLoadCallTargetFromExportedFunctionData(
    Node* function_data) {
  Node* internal = gasm_->LoadProtectedPointerFromObject(
      function_data, wasm::ObjectAccess::ToTagged(
                         WasmExportedFunctionData::kProtectedInternalOffset));
  return gasm_->LoadFromObject(
      MachineType::Pointer(), internal,
      wasm::ObjectAccess::ToTagged(WasmInternalFunction::kCallTargetOffset));
}

// TODO(9495): Support CAPI function refs.
Node* WasmGraphBuilder::BuildCallRef(const wasm::FunctionSig* sig,
                                     base::Vector<Node*> args,
                                     base::Vector<Node*> rets,
                                     CheckForNull null_check,
                                     IsReturnCall continuation,
                                     wasm::WasmCodePosition position) {
  Node* func_ref = args[0];
  if (null_check == kWithNullCheck &&
      null_check_strategy_ == NullCheckStrategy::kExplicit) {
    func_ref =
        AssertNotNull(func_ref, wasm::kWasmFuncRef /* good enough */, position);
  }

  Node* internal_function;
  if (null_check == kWithNullCheck &&
      null_check_strategy_ == NullCheckStrategy::kTrapHandler) {
    // TODO(14564): Move WasmInternalFunction to trusted space and make
    // this a load of a trusted (immutable) pointer.
    Node* load;
    std::tie(load, internal_function) =
        gasm_->LoadTrustedPointerFromObjectTrapOnNull(
            func_ref,
            wasm::ObjectAccess::ToTagged(WasmFuncRef::kTrustedInternalOffset),
            kWasmInternalFunctionIndirectPointerTag);
    SetSourcePosition(load, position);
  } else {
    internal_function = gasm_->LoadTrustedPointerFromObject(
        func_ref,
        wasm::ObjectAccess::ToTagged(WasmFuncRef::kTrustedInternalOffset),
        kWasmInternalFunctionIndirectPointerTag);
  }

  Node* implicit_arg = gasm_->LoadProtectedPointerFromObject(
      internal_function,
      wasm::ObjectAccess::ToTagged(
          WasmInternalFunction::kProtectedImplicitArgOffset));
  Node* target = gasm_->LoadFromObject(
      MachineType::Pointer(), internal_function,
      wasm::ObjectAccess::ToTagged(WasmInternalFunction::kCallTargetOffset));

  args[0] = target;

  Node* call = continuation == kCallContinues
                   ? BuildWasmCall(sig, args, rets, position, implicit_arg)
                   : BuildWasmReturnCall(sig, args, position, implicit_arg);
  return call;
}

void WasmGraphBuilder::CompareToFuncRefAtIndex(Node* func_ref,
                                               uint32_t function_index,
                                               Node** success_control,
                                               Node** failure_control,
                                               bool is_last_case) {
  // Since we are comparing to a function reference, it is guaranteed that
  // instance->wasm_internal_functions() has been initialized.
  Node* func_refs = gasm_->LoadImmutable(
      MachineType::TaggedPointer(), GetInstanceData(),
      wasm::ObjectAccess::ToTagged(WasmTrustedInstanceData::kFuncRefsOffset));
  // We cannot use an immutable load here, since function references are
  // initialized lazily: Calling {RefFunc()} between two invocations of this
  // function may initialize the function, i.e. mutate the object we are
  // loading.
  Node* function_ref_at_index = gasm_->LoadFixedArrayElement(
      func_refs, gasm_->IntPtrConstant(function_index),
      MachineType::AnyTagged());
  BranchHint hint = is_last_case ? BranchHint::kTrue : BranchHint::kNone;
  gasm_->Branch(gasm_->TaggedEqual(function_ref_at_index, func_ref),
                success_control, failure_control, hint);
}

Node* WasmGraphBuilder::CallRef(const wasm::FunctionSig* sig,
                                base::Vector<Node*> args,
                                base::Vector<Node*> rets,
                                CheckForNull null_check,
                                wasm::WasmCodePosition position) {
  return BuildCallRef(sig, args, rets, null_check, IsReturnCall::kCallContinues,
                      position);
}

Node* WasmGraphBuilder::ReturnCallRef(const wasm::FunctionSig* sig,
                                      base::Vector<Node*> args,
                                      CheckForNull null_check,
                                      wasm::WasmCodePosition position) {
  return BuildCallRef(sig, args, {}, null_check, IsReturnCall::kReturnCall,
                      position);
}

Node* WasmGraphBuilder::ReturnCall(uint32_t index, base::Vector<Node*> args,
                                   wasm::WasmCodePosition position) {
  DCHECK_NULL(args[0]);
  const wasm::FunctionSig* sig = env_->module->functions[index].sig;

  if (env_ && index < env_->module->num_imported_functions) {
    // Return Call to an imported function.
    return BuildImportCall(sig, args, {}, position, index, kReturnCall);
  }

  // A direct tail call to a wasm function defined in this module.
  // Just encode the function index. This will be patched during code
  // generation.
  Address code = static_cast<Address>(index);
  args[0] = mcgraph()->RelocatableIntPtrConstant(code, RelocInfo::WASM_CALL);

  return BuildWasmReturnCall(sig, args, position, GetInstanceData());
}

Node* WasmGraphBuilder::ReturnCallIndirect(uint32_t table_index,
                                           uint32_t sig_index,
                                           base::Vector<Node*> args,
                                           wasm::WasmCodePosition position) {
  return BuildIndirectCall(table_index, sig_index, args, {}, position,
                           kReturnCall);
}

std::tuple<Node*, Node*> WasmGraphBuilder::BrOnNull(Node* ref_object,
                                                    wasm::ValueType type) {
  return BranchExpectFalse(IsNull(ref_object, type));
}

Node* WasmGraphBuilder::BuildI32Rol(Node* left, Node* right) {
  // Implement Rol by Ror since TurboFan does not have Rol opcode.
  // TODO(weiliang): support Word32Rol opcode in TurboFan.
  Int32Matcher m(right);
  if (m.HasResolvedValue()) {
    return Binop(wasm::kExprI32Ror, left,
                 Int32Constant(32 - (m.ResolvedValue() & 0x1F)));
  } else {
    return Binop(wasm::kExprI32Ror, left,
                 Binop(wasm::kExprI32Sub, Int32Constant(32), right));
  }
}

Node* WasmGraphBuilder::BuildI64Rol(Node* left, Node* right) {
  // Implement Rol by Ror since TurboFan does not have Rol opcode.
  // TODO(weiliang): support Word64Rol opcode in TurboFan.
  Int64Matcher m(right);
  Node* inv_right = m.HasResolvedValue()
                        ? Int64Constant(64 - (m.ResolvedValue() & 0x3F))
                        : Binop(wasm::kExprI64Sub, Int64Constant(64), right);
  return Binop(wasm::kExprI64Ror, left, inv_right);
}

Node* WasmGraphBuilder::Invert(Node* node) {
  return Unop(wasm::kExprI32Eqz, node);
}

void WasmGraphBuilder::InitInstanceCache(
    WasmInstanceCacheNodes* instance_cache) {
  // We handle caching of the instance cache nodes manually, and we may reload
  // them in contexts where load elimination would eliminate the reload.
  // Therefore, we use plain Load nodes which are not subject to load
  // elimination.

  // Only cache memory start and size if there is a memory (the nodes would be
  // dead otherwise, but we can avoid creating them in the first place).
  if (!has_cached_memory()) return;

  instance_cache->mem_start = LoadMemStart(cached_memory_index_);

  // TODO(13957): Clamp the loaded memory size to a safe value.
  instance_cache->mem_size = LoadMemSize(cached_memory_index_);
}

void WasmGraphBuilder::PrepareInstanceCacheForLoop(
    WasmInstanceCacheNodes* instance_cache, Node* control) {
  if (!has_cached_memory()) return;
  for (auto field : WasmInstanceCacheNodes::kFields) {
    instance_cache->*field = graph()->NewNode(
        mcgraph()->common()->Phi(MachineType::PointerRepresentation(), 1),
        instance_cache->*field, control);
  }
}

void WasmGraphBuilder::NewInstanceCacheMerge(WasmInstanceCacheNodes* to,
                                             WasmInstanceCacheNodes* from,
                                             Node* merge) {
  for (auto field : WasmInstanceCacheNodes::kFields) {
    if (to->*field == from->*field) continue;
    Node* vals[] = {to->*field, from->*field, merge};
    to->*field = graph()->NewNode(
        mcgraph()->common()->Phi(MachineType::PointerRepresentation(), 2), 3,
        vals);
  }
}

void WasmGraphBuilder::MergeInstanceCacheInto(WasmInstanceCacheNodes* to,
                                              WasmInstanceCacheNodes* from,
                                              Node* merge) {
  if (!has_cached_memory()) {
    // Instance cache nodes should be nullptr then.
    DCHECK(to->mem_start == nullptr && to->mem_size == nullptr &&
           from->mem_start == nullptr && from->mem_size == nullptr);
    return;
  }

  for (auto field : WasmInstanceCacheNodes::kFields) {
    to->*field = CreateOrMergeIntoPhi(MachineType::PointerRepresentation(),
                                      merge, to->*field, from->*field);
  }
}

Node* WasmGraphBuilder::CreateOrMergeIntoPhi(MachineRepresentation rep,
                                             Node* merge, Node* tnode,
                                             Node* fnode) {
  if (IsPhiWithMerge(tnode, merge)) {
    AppendToPhi(tnode, fnode);
  } else if (tnode != fnode) {
    // Note that it is not safe to use {Buffer} here since this method is used
    // via {CheckForException} while the {Buffer} is in use by another method.
    uint32_t count = merge->InputCount();
    // + 1 for the merge node.
    base::SmallVector<Node*, 9> inputs(count + 1);
    for (uint32_t j = 0; j < count - 1; j++) inputs[j] = tnode;
    inputs[count - 1] = fnode;
    inputs[count] = merge;
    tnode = graph()->NewNode(mcgraph()->common()->Phi(rep, count), count + 1,
                             inputs.begin());
  }
  return tnode;
}

Node* WasmGraphBuilder::CreateOrMergeIntoEffectPhi(Node* merge, Node* tnode,
                                                   Node* fnode) {
  if (IsPhiWithMerge(tnode, merge)) {
    AppendToPhi(tnode, fnode);
  } else if (tnode != fnode) {
    // Note that it is not safe to use {Buffer} here since this method is used
    // via {CheckForException} while the {Buffer} is in use by another method.
    uint32_t count = merge->InputCount();
    // + 1 for the merge node.
    base::SmallVector<Node*, 9> inputs(count + 1);
    for (uint32_t j = 0; j < count - 1; j++) {
      inputs[j] = tnode;
    }
    inputs[count - 1] = fnode;
    inputs[count] = merge;
    tnode = graph()->NewNode(mcgraph()->common()->EffectPhi(count), count + 1,
                             inputs.begin());
  }
  return tnode;
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

Node* WasmGraphBuilder::MemStart(uint32_t mem_index) {
  DCHECK_NOT_NULL(instance_cache_);
  V8_ASSUME(cached_memory_index_ == kNoCachedMemoryIndex ||
            cached_memory_index_ >= 0);
  if (mem_index == static_cast<uint32_t>(cached_memory_index_)) {
    return instance_cache_->mem_start;
  }
  return LoadMemStart(mem_index);
}

Node* WasmGraphBuilder::MemSize(uint32_t mem_index) {
  DCHECK_NOT_NULL(instance_cache_);
  V8_ASSUME(cached_memory_index_ == kNoCachedMemoryIndex ||
            cached_memory_index_ >= 0);
  if (mem_index == static_cast<uint32_t>(cached_memory_index_)) {
    return instance_cache_->mem_size;
  }

  return LoadMemSize(mem_index);
}

Node* WasmGraphBuilder::LoadMemStart(uint32_t mem_index) {
  if (mem_index == 0) {
    return LOAD_INSTANCE_FIELD_NO_ELIMINATION(Memory0Start,
                                              MachineType::Pointer());
  }
  Node* memory_bases_and_sizes =
      LOAD_IMMUTABLE_PROTECTED_INSTANCE_FIELD(MemoryBasesAndSizes);
  // Use {LoadByteArrayElement} even though it's a trusted array; their layout
  // is the same.
  static_assert(FixedAddressArray::OffsetOfElementAt(0) ==
                TrustedFixedAddressArray::OffsetOfElementAt(0));
  return gasm_->LoadByteArrayElement(memory_bases_and_sizes,
                                     gasm_->IntPtrConstant(2 * mem_index),
                                     MachineType::Pointer());
}

Node* WasmGraphBuilder::LoadMemSize(uint32_t mem_index) {
  wasm::ValueType mem_type = env_->module->memories[mem_index].is_memory64
                                 ? wasm::kWasmI64
                                 : wasm::kWasmI32;
  if (mem_index == 0) {
    return SetType(
        LOAD_INSTANCE_FIELD_NO_ELIMINATION(Memory0Size, MachineType::UintPtr()),
        mem_type);
  }
  Node* memory_bases_and_sizes =
      LOAD_IMMUTABLE_PROTECTED_INSTANCE_FIELD(MemoryBasesAndSizes);
  // Use {LoadByteArrayElement} even though it's a trusted array; their layout
  // is the same.
  static_assert(FixedAddressArray::OffsetOfElementAt(0) ==
                TrustedFixedAddressArray::OffsetOfElementAt(0));
  return SetType(
      gasm_->LoadByteArrayElement(memory_bases_and_sizes,
                                  gasm_->IntPtrConstant(2 * mem_index + 1),
                                  MachineType::UintPtr()),
      mem_type);
}

Node* WasmGraphBuilder::MemBuffer(uint32_t mem_index, uintptr_t offset) {
  Node* mem_start = MemStart(mem_index);
  if (offset == 0) return mem_start;
  return gasm_->IntAdd(mem_start, gasm_->UintPtrConstant(offset));
}

Node* WasmGraphBuilder::CurrentMemoryPages(const wasm::WasmMemory* memory) {
  // CurrentMemoryPages can not be called from asm.js.
  DCHECK_EQ(wasm::kWasmOrigin, env_->module->origin);

  Node* mem_size = MemSize(memory->index);
  Node* result =
      gasm_->WordShr(mem_size, gasm_->IntPtrConstant(wasm::kWasmPageSizeLog2));
  result = memory->is_memory64
               ? gasm_->BuildChangeIntPtrToInt64(result)
               : gasm_->BuildTruncateIntPtrToInt32(result);
  return result;
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

void WasmGraphBuilder::GetGlobalBaseAndOffset(const wasm::WasmGlobal& global,
                                              Node** base, Node** offset) {
  if (global.mutability && global.imported) {
    Node* imported_mutable_globals = LOAD_INSTANCE_FIELD(
        ImportedMutableGlobals, MachineType::TaggedPointer());
    Node* field_offset = Int32Constant(
        wasm::ObjectAccess::ElementOffsetInTaggedFixedAddressArray(
            global.index));
    if (global.type.is_reference()) {
      // Load the base from the ImportedMutableGlobalsBuffer of the instance.
      Node* buffers = LOAD_INSTANCE_FIELD(ImportedMutableGlobalsBuffers,
                                          MachineType::TaggedPointer());
      *base = gasm_->LoadFixedArrayElementAny(buffers, global.index);

      Node* index = gasm_->LoadFromObject(
          MachineType::Int32(), imported_mutable_globals, field_offset);
      // For this case, {index} gives the index of the global in the buffer.
      // From the index, calculate the actual offset in the FixedArray. This is
      // kHeaderSize + (index * kTaggedSize).
      *offset = gasm_->IntAdd(
          gasm_->IntMul(index, gasm_->IntPtrConstant(kTaggedSize)),
          gasm_->IntPtrConstant(
              wasm::ObjectAccess::ToTagged(FixedArray::OffsetOfElementAt(0))));
    } else {
      *base = gasm_->LoadFromObject(kMaybeSandboxedPointer,
                                    imported_mutable_globals, field_offset);
      *offset = gasm_->IntPtrConstant(0);
    }
  } else if (global.type.is_reference()) {
    *base =
        LOAD_INSTANCE_FIELD(TaggedGlobalsBuffer, MachineType::TaggedPointer());
    *offset = gasm_->IntPtrConstant(
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(global.offset));
  } else {
    *base = LOAD_INSTANCE_FIELD(GlobalsStart, MachineType::Pointer());
    *offset = gasm_->IntPtrConstant(global.offset);
  }
}

Node* WasmGraphBuilder::GlobalGet(uint32_t index) {
  const wasm::WasmGlobal& global = env_->module->globals[index];
  if (global.type == wasm::kWasmS128) has_simd_ = true;
  Node* base = nullptr;
  Node* offset = nullptr;
  GetGlobalBaseAndOffset(global, &base, &offset);
  MachineType mem_type = global.type.machine_type();
  return global.mutability ? gasm_->LoadFromObject(mem_type, base, offset)
                           : gasm_->LoadImmutable(mem_type, base, offset);
}

void WasmGraphBuilder::GlobalSet(uint32_t index, Node* val) {
  const wasm::WasmGlobal& global = env_->module->globals[index];
  if (global.type == wasm::kWasmS128) has_simd_ = true;
  Node* base = nullptr;
  Node* offset = nullptr;
  GetGlobalBaseAndOffset(global, &base, &offset);
  ObjectAccess access(global.type.machine_type(), global.type.is_reference()
                                                      ? kFullWriteBarrier
                                                      : kNoWriteBarrier);
  gasm_->StoreToObject(access, base, offset, val);
}

Node* WasmGraphBuilder::TableGet(uint32_t table_index, Node* index,
                                 wasm::WasmCodePosition position) {
  const wasm::WasmTable& table = env_->module->tables[table_index];
  bool is_funcref = IsSubtypeOf(table.type, wasm::kWasmFuncRef, env_->module);
  auto stub =
      is_funcref ? Builtin::kWasmTableGetFuncRef : Builtin::kWasmTableGet;

  TableTypeToUintPtrOrOOBTrap(table.is_table64, {&index}, position);
  return gasm_->CallBuiltinThroughJumptable(
      stub, Operator::kNoThrow, gasm_->IntPtrConstant(table_index), index);
}

void WasmGraphBuilder::TableSet(uint32_t table_index, Node* index, Node* val,
                                wasm::WasmCodePosition position) {
  const wasm::WasmTable& table = env_->module->tables[table_index];
  bool is_funcref = IsSubtypeOf(table.type, wasm::kWasmFuncRef, env_->module);
  auto stub =
      is_funcref ? Builtin::kWasmTableSetFuncRef : Builtin::kWasmTableSet;
  TableTypeToUintPtrOrOOBTrap(table.is_table64, {&index}, position);
  gasm_->CallBuiltinThroughJumptable(stub, Operator::kNoThrow,
                                     gasm_->IntPtrConstant(table_index),
                                     gasm_->Int32Constant(0), index, val);
}

// Insert code to bounds check a memory access if necessary. Return the
// bounds-checked index, which is guaranteed to have (the equivalent of)
// {uintptr_t} representation.
std::pair<Node*, BoundsCheckResult> WasmGraphBuilder::BoundsCheckMem(
    const wasm::WasmMemory* memory, uint8_t access_size, Node* index,
    uintptr_t offset, wasm::WasmCodePosition position,
    EnforceBoundsCheck enforce_check, AlignmentCheck alignment_check) {
  DCHECK_LE(1, access_size);

  wasm::BoundsCheckStrategy bounds_checks = memory->bounds_checks;

  // The function body decoder already validated that the access is not
  // statically OOB.
  DCHECK(base::IsInBounds<uintptr_t>(offset, access_size,
                                     memory->max_memory_size));

  // Convert the index to uintptr.
  Node* converted_index = index;
  if (!memory->is_memory64) {
    converted_index = gasm_->BuildChangeUint32ToUintPtr(index);
  } else if (kSystemPointerSize == kInt32Size) {
    // Only use the low word for the following bounds check.
    converted_index = gasm_->TruncateInt64ToInt32(index);
  }

  UintPtrMatcher constant_index(converted_index);
  // Do alignment checks only for > 1 byte accesses (otherwise they trivially
  // pass).
  const uintptr_t align_mask = access_size - 1;
  if (static_cast<bool>(alignment_check) && align_mask != 0) {
    // Don't emit an alignment check if the index is a constant.
    // TODO(wasm): a constant match is also done above in {BoundsCheckMem}.
    if (constant_index.HasResolvedValue()) {
      uintptr_t effective_offset = constant_index.ResolvedValue() + offset;
      if ((effective_offset & align_mask) != 0) {
        // statically known to be unaligned; trap.
        TrapIfEq32(wasm::kTrapUnalignedAccess, Int32Constant(0), 0, position);
      }
    } else {
      // Unlike regular memory accesses, atomic memory accesses should trap if
      // the effective offset is misaligned.
      // TODO(wasm): this addition is redundant with one inserted by
      // {MemBuffer}.
      Node* effective_offset =
          gasm_->IntAdd(MemBuffer(memory->index, offset), converted_index);

      Node* cond =
          gasm_->WordAnd(effective_offset, gasm_->IntPtrConstant(align_mask));
      TrapIfFalse(wasm::kTrapUnalignedAccess,
                  gasm_->Word32Equal(cond, Int32Constant(0)), position);
    }
  }

  // If no bounds checks should be performed (for testing), just return the
  // converted index and assume it to be in-bounds.
  if (bounds_checks == wasm::kNoBoundsChecks) {
    return {converted_index, BoundsCheckResult::kInBounds};
  }

  if (memory->is_memory64 && kSystemPointerSize == kInt32Size) {
    // In memory64 mode on 32-bit systems, the upper 32 bits need to be zero to
    // succeed the bounds check.
    DCHECK_EQ(wasm::kExplicitBoundsChecks, bounds_checks);
    Node* high_word =
        gasm_->TruncateInt64ToInt32(gasm_->Word64Shr(index, Int32Constant(32)));
    TrapIfTrue(wasm::kTrapMemOutOfBounds, high_word, position);
  }

  // We already checked that offset is below the max memory size.
  DCHECK_LT(offset, memory->max_memory_size);

  // The accessed memory is [index + offset, index + end_offset].
  // Check that the last read byte (at {index + end_offset}) is in bounds.
  // 1) Check that {end_offset < mem_size}. This also ensures that we can safely
  //    compute {effective_size} as {mem_size - end_offset)}.
  //    {effective_size} is >= 1 if condition 1) holds.
  // 2) Check that {index + end_offset < mem_size} by
  //    - computing {effective_size} as {mem_size - end_offset} and
  //    - checking that {index < effective_size}.

  uintptr_t end_offset = offset + access_size - 1u;

  if (constant_index.HasResolvedValue() &&
      end_offset <= memory->min_memory_size &&
      constant_index.ResolvedValue() < memory->min_memory_size - end_offset) {
    // The input index is a constant and everything is statically within
    // bounds of the smallest possible memory.
    return {converted_index, BoundsCheckResult::kInBounds};
  }

  if (bounds_checks == wasm::kTrapHandler &&
      enforce_check == EnforceBoundsCheck::kCanOmitBoundsCheck) {
    if (memory->is_memory64) {
      Node* cond = gasm_->Uint64LessThan(
          converted_index, Int64Constant(memory->GetMemory64GuardsSize()));
      TrapIfFalse(wasm::kTrapMemOutOfBounds, cond, position);
    }
    return {converted_index, BoundsCheckResult::kTrapHandler};
  }

  Node* mem_size = MemSize(memory->index);

  Node* end_offset_node = mcgraph_->UintPtrConstant(end_offset);
  if (end_offset > memory->min_memory_size) {
    // The end offset is larger than the smallest memory.
    // Dynamically check the end offset against the dynamic memory size.
    Node* cond = gasm_->UintLessThan(end_offset_node, mem_size);
    TrapIfFalse(wasm::kTrapMemOutOfBounds, cond, position);
  }

  // This produces a positive number since {end_offset <= min_size <= mem_size}.
  Node* effective_size = gasm_->IntSub(mem_size, end_offset_node);

  // Introduce the actual bounds check.
  Node* cond = gasm_->UintLessThan(converted_index, effective_size);
  TrapIfFalse(wasm::kTrapMemOutOfBounds, cond, position);
  return {converted_index, BoundsCheckResult::kDynamicallyChecked};
}

const Operator* WasmGraphBuilder::GetSafeLoadOperator(int offset,
                                                      wasm::ValueType type) {
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

const Operator* WasmGraphBuilder::GetSafeStoreOperator(int offset,
                                                       wasm::ValueType type) {
  int alignment = offset % type.value_kind_size();
  MachineRepresentation rep = type.machine_representation();
  if (COMPRESS_POINTERS_BOOL && IsAnyTagged(rep)) {
    // We are storing tagged value to off-heap location, so we need to store
    // it as a full word otherwise we will not be able to decompress it.
    rep = MachineType::PointerRepresentation();
  }
  if (alignment == 0 || mcgraph()->machine()->UnalignedStoreSupported(rep)) {
    StoreRepresentation store_rep(rep, WriteBarrierKind::kNoWriteBarrier);
    return mcgraph()->machine()->Store(store_rep);
  }
  UnalignedStoreRepresentation store_rep(rep);
  return mcgraph()->machine()->UnalignedStore(store_rep);
}

void WasmGraphBuilder::TraceFunctionEntry(wasm::WasmCodePosition position) {
  Node* call = BuildCallToRuntime(Runtime::kWasmTraceEnter, nullptr, 0);
  SetSourcePosition(call, position);
}

void WasmGraphBuilder::TraceFunctionExit(base::Vector<Node*> vals,
                                         wasm::WasmCodePosition position) {
  Node* info = gasm_->IntPtrConstant(0);
  size_t num_returns = vals.size();
  if (num_returns == 1) {
    wasm::ValueType return_type = sig_->GetReturn(0);
    MachineRepresentation rep = return_type.machine_representation();
    int size = ElementSizeInBytes(rep);
    info = gasm_->StackSlot(size, size);

    gasm_->Store(StoreRepresentation(rep, kNoWriteBarrier), info,
                 Int32Constant(0), vals[0]);
  }

  Node* call = BuildCallToRuntime(Runtime::kWasmTraceExit, &info, 1);
  SetSourcePosition(call, position);
}

void WasmGraphBuilder::TraceMemoryOperation(bool is_store,
                                            MachineRepresentation rep,
                                            Node* index, uintptr_t offset,
                                            wasm::WasmCodePosition position) {
  int kAlign = 4;  // Ensure that the LSB is 0, such that this looks like a Smi.
  TNode<RawPtrT> info =
      gasm_->StackSlot(sizeof(wasm::MemoryTracingInfo), kAlign);

  Node* effective_offset = gasm_->IntAdd(gasm_->UintPtrConstant(offset), index);
  auto store = [&](int field_offset, MachineRepresentation rep, Node* data) {
    gasm_->Store(StoreRepresentation(rep, kNoWriteBarrier), info,
                 Int32Constant(field_offset), data);
  };
  // Store effective_offset, is_store, and mem_rep.
  store(offsetof(wasm::MemoryTracingInfo, offset),
        MachineType::PointerRepresentation(), effective_offset);
  store(offsetof(wasm::MemoryTracingInfo, is_store),
        MachineRepresentation::kWord8, Int32Constant(is_store ? 1 : 0));
  store(offsetof(wasm::MemoryTracingInfo, mem_rep),
        MachineRepresentation::kWord8, Int32Constant(static_cast<int>(rep)));

  Node* args[] = {info};
  Node* call =
      BuildCallToRuntime(Runtime::kWasmTraceMemory, args, arraysize(args));
  SetSourcePosition(call, position);
}

namespace {
LoadTransformation GetLoadTransformation(
    MachineType memtype, wasm::LoadTransformationKind transform) {
  switch (transform) {
    case wasm::LoadTransformationKind::kSplat: {
      if (memtype == MachineType::Int8()) {
        return LoadTransformation::kS128Load8Splat;
      } else if (memtype == MachineType::Int16()) {
        return LoadTransformation::kS128Load16Splat;
      } else if (memtype == MachineType::Int32()) {
        return LoadTransformation::kS128Load32Splat;
      } else if (memtype == MachineType::Int64()) {
        return LoadTransformation::kS128Load64Splat;
      }
      break;
    }
    case wasm::LoadTransformationKind::kExtend: {
      if (memtype == MachineType::Int8()) {
        return LoadTransformation::kS128Load8x8S;
      } else if (memtype == MachineType::Uint8()) {
        return LoadTransformation::kS128Load8x8U;
      } else if (memtype == MachineType::Int16()) {
        return LoadTransformation::kS128Load16x4S;
      } else if (memtype == MachineType::Uint16()) {
        return LoadTransformation::kS128Load16x4U;
      } else if (memtype == MachineType::Int32()) {
        return LoadTransformation::kS128Load32x2S;
      } else if (memtype == MachineType::Uint32()) {
        return LoadTransformation::kS128Load32x2U;
      }
      break;
    }
    case wasm::LoadTransformationKind::kZeroExtend: {
      if (memtype == MachineType::Int32()) {
        return LoadTransformation::kS128Load32Zero;
      } else if (memtype == MachineType::Int64()) {
        return LoadTransformation::kS128Load64Zero;
      }
      break;
    }
  }
  UNREACHABLE();
}

MemoryAccessKind GetMemoryAccessKind(MachineGraph* mcgraph,
                                     MachineRepresentation memrep,
                                     BoundsCheckResult bounds_check_result) {
  if (bounds_check_result == BoundsCheckResult::kTrapHandler) {
    // Protected instructions do not come in an 'unaligned' flavor, so the trap
    // handler can currently only be used on systems where all memory accesses
    // are allowed to be unaligned.
    DCHECK(memrep == MachineRepresentation::kWord8 ||
           mcgraph->machine()->UnalignedLoadSupported(memrep));
    return MemoryAccessKind::kProtected;
  }
  if (memrep != MachineRepresentation::kWord8 &&
      !mcgraph->machine()->UnalignedLoadSupported(memrep)) {
    return MemoryAccessKind::kUnaligned;
  }
  return MemoryAccessKind::kNormal;
}
}  // namespace

Node* WasmGraphBuilder::LoadLane(const wasm::WasmMemory* memory,
                                 wasm::ValueType type, MachineType memtype,
                                 Node* value, Node* index, uintptr_t offset,
                                 uint32_t alignment, uint8_t laneidx,
                                 wasm::WasmCodePosition position) {
  has_simd_ = true;
  Node* load;
  uint8_t access_size = memtype.MemSize();
  BoundsCheckResult bounds_check_result;
  std::tie(index, bounds_check_result) = BoundsCheckMem(
      memory, access_size, index, offset, position,
      EnforceBoundsCheck::kCanOmitBoundsCheck, AlignmentCheck::kNo);

  MemoryAccessKind load_kind = GetMemoryAccessKind(
      mcgraph_, memtype.representation(), bounds_check_result);

  load = SetEffect(graph()->NewNode(
      mcgraph()->machine()->LoadLane(load_kind, memtype, laneidx),
      MemBuffer(memory->index, offset), index, value, effect(), control()));

  if (load_kind == MemoryAccessKind::kProtected) {
    SetSourcePosition(load, position);
  }
  if (v8_flags.trace_wasm_memory) {
    // TODO(14259): Implement memory tracing for multiple memories.
    CHECK_EQ(0, memory->index);
    TraceMemoryOperation(false, memtype.representation(), index, offset,
                         position);
  }

  return load;
}

Node* WasmGraphBuilder::LoadTransform(const wasm::WasmMemory* memory,
                                      wasm::ValueType type, MachineType memtype,
                                      wasm::LoadTransformationKind transform,
                                      Node* index, uintptr_t offset,
                                      uint32_t alignment,
                                      wasm::WasmCodePosition position) {
  has_simd_ = true;

  // Wasm semantics throw on OOB. Introduce explicit bounds check and
  // conditioning when not using the trap handler.

  // Load extends always load 8 bytes.
  uint8_t access_size = transform == wasm::LoadTransformationKind::kExtend
                            ? 8
                            : memtype.MemSize();
  BoundsCheckResult bounds_check_result;
  std::tie(index, bounds_check_result) = BoundsCheckMem(
      memory, access_size, index, offset, position,
      EnforceBoundsCheck::kCanOmitBoundsCheck, AlignmentCheck::kNo);

  LoadTransformation transformation = GetLoadTransformation(memtype, transform);
  MemoryAccessKind load_kind = GetMemoryAccessKind(
      mcgraph_, memtype.representation(), bounds_check_result);

  Node* load = SetEffect(graph()->NewNode(
      mcgraph()->machine()->LoadTransform(load_kind, transformation),
      MemBuffer(memory->index, offset), index, effect(), control()));

  if (load_kind == MemoryAccessKind::kProtected) {
    SetSourcePosition(load, position);
  }

  if (v8_flags.trace_wasm_memory) {
    // TODO(14259): Implement memory tracing for multiple memories.
    CHECK_EQ(0, memory->index);
    TraceMemoryOperation(false, memtype.representation(), index, offset,
                         position);
  }
  return load;
}

Node* WasmGraphBuilder::LoadMem(const wasm::WasmMemory* memory,
                                wasm::ValueType type, MachineType memtype,
                                Node* index, uintptr_t offset,
                                uint32_t alignment,
                                wasm::WasmCodePosition position) {
  if (memtype.representation() == MachineRepresentation::kSimd128) {
    has_simd_ = true;
  }

  // Wasm semantics throw on OOB. Introduce explicit bounds check and
  // conditioning when not using the trap handler.
  BoundsCheckResult bounds_check_result;
  std::tie(index, bounds_check_result) = BoundsCheckMem(
      memory, memtype.MemSize(), index, offset, position,
      EnforceBoundsCheck::kCanOmitBoundsCheck, AlignmentCheck::kNo);

  Node* mem_start = MemBuffer(memory->index, offset);
  Node* load;
  switch (GetMemoryAccessKind(mcgraph_, memtype.representation(),
                              bounds_check_result)) {
    case MemoryAccessKind::kUnaligned:
      load = gasm_->LoadUnaligned(memtype, mem_start, index);
      break;
    case MemoryAccessKind::kProtected:
      load = gasm_->ProtectedLoad(memtype, mem_start, index);
      SetSourcePosition(load, position);
      break;
    case MemoryAccessKind::kNormal:
      load = gasm_->Load(memtype, mem_start, index);
      break;
  }

#if defined(V8_TARGET_BIG_ENDIAN)
  load = BuildChangeEndiannessLoad(load, memtype, type);
#endif

  if (type == wasm::kWasmI64 &&
      ElementSizeInBytes(memtype.representation()) < 8) {
    // TODO(titzer): TF zeroes the upper bits of 64-bit loads for subword sizes.
    load = memtype.IsSigned()
               ? gasm_->ChangeInt32ToInt64(load)     // sign extend
               : gasm_->ChangeUint32ToUint64(load);  // zero extend
  }

  if (v8_flags.trace_wasm_memory) {
    // TODO(14259): Implement memory tracing for multiple memories.
    CHECK_EQ(0, memory->index);
    TraceMemoryOperation(false, memtype.representation(), index, offset,
                         position);
  }

  return load;
}

void WasmGraphBuilder::StoreLane(const wasm::WasmMemory* memory,
                                 MachineRepresentation mem_rep, Node* index,
                                 uintptr_t offset, uint32_t alignment,
                                 Node* val, uint8_t laneidx,
                                 wasm::WasmCodePosition position,
                                 wasm::ValueType type) {
  has_simd_ = true;
  BoundsCheckResult bounds_check_result;
  std::tie(index, bounds_check_result) = BoundsCheckMem(
      memory, i::ElementSizeInBytes(mem_rep), index, offset, position,
      wasm::kPartialOOBWritesAreNoops ? EnforceBoundsCheck::kCanOmitBoundsCheck
                                      : EnforceBoundsCheck::kNeedsBoundsCheck,
      AlignmentCheck::kNo);
  MemoryAccessKind load_kind =
      GetMemoryAccessKind(mcgraph_, mem_rep, bounds_check_result);

  Node* store = SetEffect(graph()->NewNode(
      mcgraph()->machine()->StoreLane(load_kind, mem_rep, laneidx),
      MemBuffer(memory->index, offset), index, val, effect(), control()));

  if (load_kind == MemoryAccessKind::kProtected) {
    SetSourcePosition(store, position);
  }
  if (v8_flags.trace_wasm_memory) {
    // TODO(14259): Implement memory tracing for multiple memories.
    CHECK_EQ(0, memory->index);
    TraceMemoryOperation(true, mem_rep, index, offset, position);
  }
}

void WasmGraphBuilder::StoreMem(const wasm::WasmMemory* memory,
                                MachineRepresentation mem_rep, Node* index,
                                uintptr_t offset, uint32_t alignment, Node* val,
                                wasm::WasmCodePosition position,
                                wasm::ValueType type) {
  if (mem_rep == MachineRepresentation::kSimd128) {
    has_simd_ = true;
  }

  BoundsCheckResult bounds_check_result;
  std::tie(index, bounds_check_result) = BoundsCheckMem(
      memory, i::ElementSizeInBytes(mem_rep), index, offset, position,
      wasm::kPartialOOBWritesAreNoops ? EnforceBoundsCheck::kCanOmitBoundsCheck
                                      : EnforceBoundsCheck::kNeedsBoundsCheck,
      AlignmentCheck::kNo);

#if defined(V8_TARGET_BIG_ENDIAN)
  val = BuildChangeEndiannessStore(val, mem_rep, type);
#endif

  Node* mem_start = MemBuffer(memory->index, offset);
  switch (GetMemoryAccessKind(mcgraph_, mem_rep, bounds_check_result)) {
    case MemoryAccessKind::kUnaligned:
      gasm_->StoreUnaligned(UnalignedStoreRepresentation{mem_rep}, mem_start,
                            index, val);
      break;
    case MemoryAccessKind::kProtected: {
      Node* store = gasm_->ProtectedStore(mem_rep, mem_start, index, val);
      SetSourcePosition(store, position);
      if (mem_rep == MachineRepresentation::kSimd128) {
        graph()->RecordSimdStore(store);
      }
      break;
    }
    case MemoryAccessKind::kNormal: {
      Node* store = gasm_->Store(StoreRepresentation{mem_rep, kNoWriteBarrier},
                                 mem_start, index, val);
      if (mem_rep == MachineRepresentation::kSimd128) {
        graph()->RecordSimdStore(store);
      }
      break;
    }
  }

  if (v8_flags.trace_wasm_memory) {
    // TODO(14259): Implement memory tracing for multiple memories.
    CHECK_EQ(0, memory->index);
    TraceMemoryOperation(true, mem_rep, index, offset, position);
  }
}

Node* WasmGraphBuilder::BuildAsmjsLoadMem(MachineType type, Node* index) {
  DCHECK_NOT_NULL(instance_cache_);
  DCHECK_EQ(1, env_->module->memories.size());
  Node* mem_start = MemStart(0);
  Node* mem_size = MemSize(0);

  // Asm.js semantics are defined in terms of typed arrays, hence OOB
  // reads return {undefined} coerced to the result type (0 for integers, NaN
  // for float and double).
  // Note that we check against the memory size ignoring the size of the
  // stored value, which is conservative if misaligned. Technically, asm.js
  // should never have misaligned accesses.
  // Technically, we should do a signed 32-to-ptr extension here. However,
  // that is an explicit instruction, whereas unsigned extension is implicit.
  // Since the difference is only observable for memories larger than 2 GiB,
  // and since we disallow such memories, we can use unsigned extension.
  index = gasm_->BuildChangeUint32ToUintPtr(index);
  Diamond bounds_check(graph(), mcgraph()->common(),
                       gasm_->UintLessThan(index, mem_size), BranchHint::kTrue);
  bounds_check.Chain(control());

  Node* load = graph()->NewNode(mcgraph()->machine()->Load(type), mem_start,
                                index, effect(), bounds_check.if_true);
  SetEffectControl(bounds_check.EffectPhi(load, effect()), bounds_check.merge);

  Node* oob_value;
  switch (type.representation()) {
    case MachineRepresentation::kWord8:
    case MachineRepresentation::kWord16:
    case MachineRepresentation::kWord32:
      oob_value = Int32Constant(0);
      break;
    case MachineRepresentation::kWord64:
      oob_value = Int64Constant(0);
      break;
    case MachineRepresentation::kFloat32:
      oob_value = Float32Constant(std::numeric_limits<float>::quiet_NaN());
      break;
    case MachineRepresentation::kFloat64:
      oob_value = Float64Constant(std::numeric_limits<double>::quiet_NaN());
      break;
    default:
      UNREACHABLE();
  }

  return bounds_check.Phi(type.representation(), load, oob_value);
}

Node* WasmGraphBuilder::BuildAsmjsStoreMem(MachineType type, Node* index,
                                           Node* val) {
  DCHECK_NOT_NULL(instance_cache_);
  DCHECK_EQ(1, env_->module->memories.size());
  Node* mem_start = MemStart(0);
  Node* mem_size = MemSize(0);

  // Asm.js semantics are to ignore OOB writes.
  // Note that we check against the memory size ignoring the size of the
  // stored value, which is conservative if misaligned. Technically, asm.js
  // should never have misaligned accesses.
  // See {BuildAsmJsLoadMem} for background on using an unsigned extension
  // here.
  index = gasm_->BuildChangeUint32ToUintPtr(index);
  Diamond bounds_check(graph(), mcgraph()->common(),
                       gasm_->UintLessThan(index, mem_size), BranchHint::kTrue);
  bounds_check.Chain(control());

  const Operator* store_op = mcgraph()->machine()->Store(StoreRepresentation(
      type.representation(), WriteBarrierKind::kNoWriteBarrier));
  Node* store = graph()->NewNode(store_op, mem_start, index, val, effect(),
                                 bounds_check.if_true);
  SetEffectControl(bounds_check.EffectPhi(store, effect()), bounds_check.merge);
  return val;
}

Node* WasmGraphBuilder::BuildF64x2Ceil(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f64x2_ceil();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64x2Floor(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f64x2_floor();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64x2Trunc(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f64x2_trunc();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF64x2NearestInt(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f64x2_nearest_int();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF32x4Ceil(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f32x4_ceil();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF32x4Floor(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f32x4_floor();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF32x4Trunc(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f32x4_trunc();
  return BuildCFuncInstruction(ref, type, input);
}

Node* WasmGraphBuilder::BuildF32x4NearestInt(Node* input) {
  MachineType type = MachineType::Simd128();
  ExternalReference ref = ExternalReference::wasm_f32x4_nearest_int();
  return BuildCFuncInstruction(ref, type, input);
}

void WasmGraphBuilder::PrintDebugName(Node* node) {
  PrintF("#%d:%s", node->id(), node->op()->mnemonic());
}

Graph* WasmGraphBuilder::graph() { return mcgraph()->graph(); }

Zone* WasmGraphBuilder::graph_zone() { return graph()->zone(); }

Signature<MachineRepresentation>* CreateMachineSignature(
    Zone* zone, const wasm::FunctionSig* sig, wasm::CallOrigin origin) {
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

void WasmGraphBuilder::LowerInt64(Signature<MachineRepresentation>* sig) {
  if (mcgraph()->machine()->Is64()) return;
  Int64Lowering r(mcgraph()->graph(), mcgraph()->machine(), mcgraph()->common(),
                  gasm_->simplified(), mcgraph()->zone(), sig);
  r.LowerGraph();
}

void WasmGraphBuilder::LowerInt64(wasm::CallOrigin origin) {
  LowerInt64(CreateMachineSignature(mcgraph()->zone(), sig_, origin));
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

Node* WasmGraphBuilder::S128Zero() {
  has_simd_ = true;
  return graph()->NewNode(mcgraph()->machine()->S128Zero());
}

Node* WasmGraphBuilder::SimdOp(wasm::WasmOpcode opcode, Node* const* inputs) {
  has_simd_ = true;
  switch (opcode) {
    case wasm::kExprF64x2Splat:
      return graph()->NewNode(mcgraph()->machine()->F64x2Splat(), inputs[0]);
    case wasm::kExprF64x2Abs:
      return graph()->NewNode(mcgraph()->machine()->F64x2Abs(), inputs[0]);
    case wasm::kExprF64x2Neg:
      return graph()->NewNode(mcgraph()->machine()->F64x2Neg(), inputs[0]);
    case wasm::kExprF64x2Sqrt:
      return graph()->NewNode(mcgraph()->machine()->F64x2Sqrt(), inputs[0]);
    case wasm::kExprF64x2Add:
      return graph()->NewNode(mcgraph()->machine()->F64x2Add(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Sub:
      return graph()->NewNode(mcgraph()->machine()->F64x2Sub(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Mul:
      return graph()->NewNode(mcgraph()->machine()->F64x2Mul(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Div:
      return graph()->NewNode(mcgraph()->machine()->F64x2Div(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Min:
      return graph()->NewNode(mcgraph()->machine()->F64x2Min(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Max:
      return graph()->NewNode(mcgraph()->machine()->F64x2Max(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Eq:
      return graph()->NewNode(mcgraph()->machine()->F64x2Eq(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Ne:
      return graph()->NewNode(mcgraph()->machine()->F64x2Ne(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Lt:
      return graph()->NewNode(mcgraph()->machine()->F64x2Lt(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Le:
      return graph()->NewNode(mcgraph()->machine()->F64x2Le(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Gt:
      return graph()->NewNode(mcgraph()->machine()->F64x2Lt(), inputs[1],
                              inputs[0]);
    case wasm::kExprF64x2Ge:
      return graph()->NewNode(mcgraph()->machine()->F64x2Le(), inputs[1],
                              inputs[0]);
    case wasm::kExprF64x2Qfma:
      return graph()->NewNode(mcgraph()->machine()->F64x2Qfma(), inputs[0],
                              inputs[1], inputs[2]);
    case wasm::kExprF64x2Qfms:
      return graph()->NewNode(mcgraph()->machine()->F64x2Qfms(), inputs[0],
                              inputs[1], inputs[2]);
    case wasm::kExprF64x2Pmin:
      return graph()->NewNode(mcgraph()->machine()->F64x2Pmin(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Pmax:
      return graph()->NewNode(mcgraph()->machine()->F64x2Pmax(), inputs[0],
                              inputs[1]);
    case wasm::kExprF64x2Ceil:
      // Architecture support for F64x2Ceil and Float64RoundUp is the same.
      if (!mcgraph()->machine()->Float64RoundUp().IsSupported())
        return BuildF64x2Ceil(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F64x2Ceil(), inputs[0]);
    case wasm::kExprF64x2Floor:
      // Architecture support for F64x2Floor and Float64RoundDown is the same.
      if (!mcgraph()->machine()->Float64RoundDown().IsSupported())
        return BuildF64x2Floor(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F64x2Floor(), inputs[0]);
    case wasm::kExprF64x2Trunc:
      // Architecture support for F64x2Trunc and Float64RoundTruncate is the
      // same.
      if (!mcgraph()->machine()->Float64RoundTruncate().IsSupported())
        return BuildF64x2Trunc(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F64x2Trunc(), inputs[0]);
    case wasm::kExprF64x2NearestInt:
      // Architecture support for F64x2NearestInt and Float64RoundTiesEven is
      // the same.
      if (!mcgraph()->machine()->Float64RoundTiesEven().IsSupported())
        return BuildF64x2NearestInt(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F64x2NearestInt(),
                              inputs[0]);
    case wasm::kExprF64x2ConvertLowI32x4S:
      return graph()->NewNode(mcgraph()->machine()->F64x2ConvertLowI32x4S(),
                              inputs[0]);
    case wasm::kExprF64x2ConvertLowI32x4U:
      return graph()->NewNode(mcgraph()->machine()->F64x2ConvertLowI32x4U(),
                              inputs[0]);
    case wasm::kExprF64x2PromoteLowF32x4:
      return graph()->NewNode(mcgraph()->machine()->F64x2PromoteLowF32x4(),
                              inputs[0]);
    case wasm::kExprF32x4Splat:
      return graph()->NewNode(mcgraph()->machine()->F32x4Splat(), inputs[0]);
    case wasm::kExprF32x4SConvertI32x4:
      return graph()->NewNode(mcgraph()->machine()->F32x4SConvertI32x4(),
                              inputs[0]);
    case wasm::kExprF32x4UConvertI32x4:
      return graph()->NewNode(mcgraph()->machine()->F32x4UConvertI32x4(),
                              inputs[0]);
    case wasm::kExprF32x4Abs:
      return graph()->NewNode(mcgraph()->machine()->F32x4Abs(), inputs[0]);
    case wasm::kExprF32x4Neg:
      return graph()->NewNode(mcgraph()->machine()->F32x4Neg(), inputs[0]);
    case wasm::kExprF32x4Sqrt:
      return graph()->NewNode(mcgraph()->machine()->F32x4Sqrt(), inputs[0]);
    case wasm::kExprF32x4Add:
      return graph()->NewNode(mcgraph()->machine()->F32x4Add(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Sub:
      return graph()->NewNode(mcgraph()->machine()->F32x4Sub(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Mul:
      return graph()->NewNode(mcgraph()->machine()->F32x4Mul(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Div:
      return graph()->NewNode(mcgraph()->machine()->F32x4Div(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Min:
      return graph()->NewNode(mcgraph()->machine()->F32x4Min(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Max:
      return graph()->NewNode(mcgraph()->machine()->F32x4Max(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Eq:
      return graph()->NewNode(mcgraph()->machine()->F32x4Eq(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Ne:
      return graph()->NewNode(mcgraph()->machine()->F32x4Ne(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Lt:
      return graph()->NewNode(mcgraph()->machine()->F32x4Lt(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Le:
      return graph()->NewNode(mcgraph()->machine()->F32x4Le(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Gt:
      return graph()->NewNode(mcgraph()->machine()->F32x4Lt(), inputs[1],
                              inputs[0]);
    case wasm::kExprF32x4Ge:
      return graph()->NewNode(mcgraph()->machine()->F32x4Le(), inputs[1],
                              inputs[0]);
    case wasm::kExprF32x4Qfma:
      return graph()->NewNode(mcgraph()->machine()->F32x4Qfma(), inputs[0],
                              inputs[1], inputs[2]);
    case wasm::kExprF32x4Qfms:
      return graph()->NewNode(mcgraph()->machine()->F32x4Qfms(), inputs[0],
                              inputs[1], inputs[2]);
    case wasm::kExprF32x4Pmin:
      return graph()->NewNode(mcgraph()->machine()->F32x4Pmin(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Pmax:
      return graph()->NewNode(mcgraph()->machine()->F32x4Pmax(), inputs[0],
                              inputs[1]);
    case wasm::kExprF32x4Ceil:
      // Architecture support for F32x4Ceil and Float32RoundUp is the same.
      if (!mcgraph()->machine()->Float32RoundUp().IsSupported())
        return BuildF32x4Ceil(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F32x4Ceil(), inputs[0]);
    case wasm::kExprF32x4Floor:
      // Architecture support for F32x4Floor and Float32RoundDown is the same.
      if (!mcgraph()->machine()->Float32RoundDown().IsSupported())
        return BuildF32x4Floor(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F32x4Floor(), inputs[0]);
    case wasm::kExprF32x4Trunc:
      // Architecture support for F32x4Trunc and Float32RoundTruncate is the
      // same.
      if (!mcgraph()->machine()->Float32RoundTruncate().IsSupported())
        return BuildF32x4Trunc(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F32x4Trunc(), inputs[0]);
    case wasm::kExprF32x4NearestInt:
      // Architecture support for F32x4NearestInt and Float32RoundTiesEven is
      // the same.
      if (!mcgraph()->machine()->Float32RoundTiesEven().IsSupported())
        return BuildF32x4NearestInt(inputs[0]);
      return graph()->NewNode(mcgraph()->machine()->F32x4NearestInt(),
                              inputs[0]);
    case wasm::kExprF32x4DemoteF64x2Zero:
      return graph()->NewNode(mcgraph()->machine()->F32x4DemoteF64x2Zero(),
                              inputs[0]);
    case wasm::kExprI64x2Splat:
      return graph()->NewNode(mcgraph()->machine()->I64x2Splat(), inputs[0]);
    case wasm::kExprI64x2Abs:
      return graph()->NewNode(mcgraph()->machine()->I64x2Abs(), inputs[0]);
    case wasm::kExprI64x2Neg:
      return graph()->NewNode(mcgraph()->machine()->I64x2Neg(), inputs[0]);
    case wasm::kExprI64x2SConvertI32x4Low:
      return graph()->NewNode(mcgraph()->machine()->I64x2SConvertI32x4Low(),
                              inputs[0]);
    case wasm::kExprI64x2SConvertI32x4High:
      return graph()->NewNode(mcgraph()->machine()->I64x2SConvertI32x4High(),
                              inputs[0]);
    case wasm::kExprI64x2UConvertI32x4Low:
      return graph()->NewNode(mcgraph()->machine()->I64x2UConvertI32x4Low(),
                              inputs[0]);
    case wasm::kExprI64x2UConvertI32x4High:
      return graph()->NewNode(mcgraph()->machine()->I64x2UConvertI32x4High(),
                              inputs[0]);
    case wasm::kExprI64x2BitMask:
      return graph()->NewNode(mcgraph()->machine()->I64x2BitMask(), inputs[0]);
    case wasm::kExprI64x2Shl:
      return graph()->NewNode(mcgraph()->machine()->I64x2Shl(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2ShrS:
      return graph()->NewNode(mcgraph()->machine()->I64x2ShrS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2Add:
      return graph()->NewNode(mcgraph()->machine()->I64x2Add(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2Sub:
      return graph()->NewNode(mcgraph()->machine()->I64x2Sub(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2Mul:
      return graph()->NewNode(mcgraph()->machine()->I64x2Mul(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2Eq:
      return graph()->NewNode(mcgraph()->machine()->I64x2Eq(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2Ne:
      return graph()->NewNode(mcgraph()->machine()->I64x2Ne(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2LtS:
      return graph()->NewNode(mcgraph()->machine()->I64x2GtS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI64x2LeS:
      return graph()->NewNode(mcgraph()->machine()->I64x2GeS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI64x2GtS:
      return graph()->NewNode(mcgraph()->machine()->I64x2GtS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2GeS:
      return graph()->NewNode(mcgraph()->machine()->I64x2GeS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2ShrU:
      return graph()->NewNode(mcgraph()->machine()->I64x2ShrU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2ExtMulLowI32x4S:
      return graph()->NewNode(mcgraph()->machine()->I64x2ExtMulLowI32x4S(),
                              inputs[0], inputs[1]);
    case wasm::kExprI64x2ExtMulHighI32x4S:
      return graph()->NewNode(mcgraph()->machine()->I64x2ExtMulHighI32x4S(),
                              inputs[0], inputs[1]);
    case wasm::kExprI64x2ExtMulLowI32x4U:
      return graph()->NewNode(mcgraph()->machine()->I64x2ExtMulLowI32x4U(),
                              inputs[0], inputs[1]);
    case wasm::kExprI64x2ExtMulHighI32x4U:
      return graph()->NewNode(mcgraph()->machine()->I64x2ExtMulHighI32x4U(),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4Splat:
      return graph()->NewNode(mcgraph()->machine()->I32x4Splat(), inputs[0]);
    case wasm::kExprI32x4SConvertF32x4:
      return graph()->NewNode(mcgraph()->machine()->I32x4SConvertF32x4(),
                              inputs[0]);
    case wasm::kExprI32x4UConvertF32x4:
      return graph()->NewNode(mcgraph()->machine()->I32x4UConvertF32x4(),
                              inputs[0]);
    case wasm::kExprI32x4SConvertI16x8Low:
      return graph()->NewNode(mcgraph()->machine()->I32x4SConvertI16x8Low(),
                              inputs[0]);
    case wasm::kExprI32x4SConvertI16x8High:
      return graph()->NewNode(mcgraph()->machine()->I32x4SConvertI16x8High(),
                              inputs[0]);
    case wasm::kExprI32x4Neg:
      return graph()->NewNode(mcgraph()->machine()->I32x4Neg(), inputs[0]);
    case wasm::kExprI32x4Shl:
      return graph()->NewNode(mcgraph()->machine()->I32x4Shl(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4ShrS:
      return graph()->NewNode(mcgraph()->machine()->I32x4ShrS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4Add:
      return graph()->NewNode(mcgraph()->machine()->I32x4Add(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4Sub:
      return graph()->NewNode(mcgraph()->machine()->I32x4Sub(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4Mul:
      return graph()->NewNode(mcgraph()->machine()->I32x4Mul(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4MinS:
      return graph()->NewNode(mcgraph()->machine()->I32x4MinS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4MaxS:
      return graph()->NewNode(mcgraph()->machine()->I32x4MaxS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4Eq:
      return graph()->NewNode(mcgraph()->machine()->I32x4Eq(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4Ne:
      return graph()->NewNode(mcgraph()->machine()->I32x4Ne(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4LtS:
      return graph()->NewNode(mcgraph()->machine()->I32x4GtS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI32x4LeS:
      return graph()->NewNode(mcgraph()->machine()->I32x4GeS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI32x4GtS:
      return graph()->NewNode(mcgraph()->machine()->I32x4GtS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4GeS:
      return graph()->NewNode(mcgraph()->machine()->I32x4GeS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4UConvertI16x8Low:
      return graph()->NewNode(mcgraph()->machine()->I32x4UConvertI16x8Low(),
                              inputs[0]);
    case wasm::kExprI32x4UConvertI16x8High:
      return graph()->NewNode(mcgraph()->machine()->I32x4UConvertI16x8High(),
                              inputs[0]);
    case wasm::kExprI32x4ShrU:
      return graph()->NewNode(mcgraph()->machine()->I32x4ShrU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4MinU:
      return graph()->NewNode(mcgraph()->machine()->I32x4MinU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4MaxU:
      return graph()->NewNode(mcgraph()->machine()->I32x4MaxU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4LtU:
      return graph()->NewNode(mcgraph()->machine()->I32x4GtU(), inputs[1],
                              inputs[0]);
    case wasm::kExprI32x4LeU:
      return graph()->NewNode(mcgraph()->machine()->I32x4GeU(), inputs[1],
                              inputs[0]);
    case wasm::kExprI32x4GtU:
      return graph()->NewNode(mcgraph()->machine()->I32x4GtU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4GeU:
      return graph()->NewNode(mcgraph()->machine()->I32x4GeU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4Abs:
      return graph()->NewNode(mcgraph()->machine()->I32x4Abs(), inputs[0]);
    case wasm::kExprI32x4BitMask:
      return graph()->NewNode(mcgraph()->machine()->I32x4BitMask(), inputs[0]);
    case wasm::kExprI32x4DotI16x8S:
      return graph()->NewNode(mcgraph()->machine()->I32x4DotI16x8S(), inputs[0],
                              inputs[1]);
    case wasm::kExprI32x4ExtMulLowI16x8S:
      return graph()->NewNode(mcgraph()->machine()->I32x4ExtMulLowI16x8S(),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4ExtMulHighI16x8S:
      return graph()->NewNode(mcgraph()->machine()->I32x4ExtMulHighI16x8S(),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4ExtMulLowI16x8U:
      return graph()->NewNode(mcgraph()->machine()->I32x4ExtMulLowI16x8U(),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4ExtMulHighI16x8U:
      return graph()->NewNode(mcgraph()->machine()->I32x4ExtMulHighI16x8U(),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4ExtAddPairwiseI16x8S:
      return graph()->NewNode(mcgraph()->machine()->I32x4ExtAddPairwiseI16x8S(),
                              inputs[0]);
    case wasm::kExprI32x4ExtAddPairwiseI16x8U:
      return graph()->NewNode(mcgraph()->machine()->I32x4ExtAddPairwiseI16x8U(),
                              inputs[0]);
    case wasm::kExprI32x4TruncSatF64x2SZero:
      return graph()->NewNode(mcgraph()->machine()->I32x4TruncSatF64x2SZero(),
                              inputs[0]);
    case wasm::kExprI32x4TruncSatF64x2UZero:
      return graph()->NewNode(mcgraph()->machine()->I32x4TruncSatF64x2UZero(),
                              inputs[0]);
    case wasm::kExprI16x8Splat:
      return graph()->NewNode(mcgraph()->machine()->I16x8Splat(), inputs[0]);
    case wasm::kExprI16x8SConvertI8x16Low:
      return graph()->NewNode(mcgraph()->machine()->I16x8SConvertI8x16Low(),
                              inputs[0]);
    case wasm::kExprI16x8SConvertI8x16High:
      return graph()->NewNode(mcgraph()->machine()->I16x8SConvertI8x16High(),
                              inputs[0]);
    case wasm::kExprI16x8Shl:
      return graph()->NewNode(mcgraph()->machine()->I16x8Shl(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8ShrS:
      return graph()->NewNode(mcgraph()->machine()->I16x8ShrS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8Neg:
      return graph()->NewNode(mcgraph()->machine()->I16x8Neg(), inputs[0]);
    case wasm::kExprI16x8SConvertI32x4:
      return graph()->NewNode(mcgraph()->machine()->I16x8SConvertI32x4(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8Add:
      return graph()->NewNode(mcgraph()->machine()->I16x8Add(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8AddSatS:
      return graph()->NewNode(mcgraph()->machine()->I16x8AddSatS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8Sub:
      return graph()->NewNode(mcgraph()->machine()->I16x8Sub(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8SubSatS:
      return graph()->NewNode(mcgraph()->machine()->I16x8SubSatS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8Mul:
      return graph()->NewNode(mcgraph()->machine()->I16x8Mul(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8MinS:
      return graph()->NewNode(mcgraph()->machine()->I16x8MinS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8MaxS:
      return graph()->NewNode(mcgraph()->machine()->I16x8MaxS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8Eq:
      return graph()->NewNode(mcgraph()->machine()->I16x8Eq(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8Ne:
      return graph()->NewNode(mcgraph()->machine()->I16x8Ne(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8LtS:
      return graph()->NewNode(mcgraph()->machine()->I16x8GtS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI16x8LeS:
      return graph()->NewNode(mcgraph()->machine()->I16x8GeS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI16x8GtS:
      return graph()->NewNode(mcgraph()->machine()->I16x8GtS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8GeS:
      return graph()->NewNode(mcgraph()->machine()->I16x8GeS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8UConvertI8x16Low:
      return graph()->NewNode(mcgraph()->machine()->I16x8UConvertI8x16Low(),
                              inputs[0]);
    case wasm::kExprI16x8UConvertI8x16High:
      return graph()->NewNode(mcgraph()->machine()->I16x8UConvertI8x16High(),
                              inputs[0]);
    case wasm::kExprI16x8UConvertI32x4:
      return graph()->NewNode(mcgraph()->machine()->I16x8UConvertI32x4(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8ShrU:
      return graph()->NewNode(mcgraph()->machine()->I16x8ShrU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8AddSatU:
      return graph()->NewNode(mcgraph()->machine()->I16x8AddSatU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8SubSatU:
      return graph()->NewNode(mcgraph()->machine()->I16x8SubSatU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8MinU:
      return graph()->NewNode(mcgraph()->machine()->I16x8MinU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8MaxU:
      return graph()->NewNode(mcgraph()->machine()->I16x8MaxU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8LtU:
      return graph()->NewNode(mcgraph()->machine()->I16x8GtU(), inputs[1],
                              inputs[0]);
    case wasm::kExprI16x8LeU:
      return graph()->NewNode(mcgraph()->machine()->I16x8GeU(), inputs[1],
                              inputs[0]);
    case wasm::kExprI16x8GtU:
      return graph()->NewNode(mcgraph()->machine()->I16x8GtU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8GeU:
      return graph()->NewNode(mcgraph()->machine()->I16x8GeU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI16x8RoundingAverageU:
      return graph()->NewNode(mcgraph()->machine()->I16x8RoundingAverageU(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8Q15MulRSatS:
      return graph()->NewNode(mcgraph()->machine()->I16x8Q15MulRSatS(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8RelaxedQ15MulRS:
      return graph()->NewNode(mcgraph()->machine()->I16x8RelaxedQ15MulRS(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8DotI8x16I7x16S:
      return graph()->NewNode(mcgraph()->machine()->I16x8DotI8x16I7x16S(),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4DotI8x16I7x16AddS:
      return graph()->NewNode(mcgraph()->machine()->I32x4DotI8x16I7x16AddS(),
                              inputs[0], inputs[1], inputs[2]);
    case wasm::kExprI16x8Abs:
      return graph()->NewNode(mcgraph()->machine()->I16x8Abs(), inputs[0]);
    case wasm::kExprI16x8BitMask:
      return graph()->NewNode(mcgraph()->machine()->I16x8BitMask(), inputs[0]);
    case wasm::kExprI16x8ExtMulLowI8x16S:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtMulLowI8x16S(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8ExtMulHighI8x16S:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtMulHighI8x16S(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8ExtMulLowI8x16U:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtMulLowI8x16U(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8ExtMulHighI8x16U:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtMulHighI8x16U(),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8ExtAddPairwiseI8x16S:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtAddPairwiseI8x16S(),
                              inputs[0]);
    case wasm::kExprI16x8ExtAddPairwiseI8x16U:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtAddPairwiseI8x16U(),
                              inputs[0]);
    case wasm::kExprI8x16Splat:
      return graph()->NewNode(mcgraph()->machine()->I8x16Splat(), inputs[0]);
    case wasm::kExprI8x16Neg:
      return graph()->NewNode(mcgraph()->machine()->I8x16Neg(), inputs[0]);
    case wasm::kExprI8x16Shl:
      return graph()->NewNode(mcgraph()->machine()->I8x16Shl(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16ShrS:
      return graph()->NewNode(mcgraph()->machine()->I8x16ShrS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16SConvertI16x8:
      return graph()->NewNode(mcgraph()->machine()->I8x16SConvertI16x8(),
                              inputs[0], inputs[1]);
    case wasm::kExprI8x16Add:
      return graph()->NewNode(mcgraph()->machine()->I8x16Add(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16AddSatS:
      return graph()->NewNode(mcgraph()->machine()->I8x16AddSatS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16Sub:
      return graph()->NewNode(mcgraph()->machine()->I8x16Sub(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16SubSatS:
      return graph()->NewNode(mcgraph()->machine()->I8x16SubSatS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16MinS:
      return graph()->NewNode(mcgraph()->machine()->I8x16MinS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16MaxS:
      return graph()->NewNode(mcgraph()->machine()->I8x16MaxS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16Eq:
      return graph()->NewNode(mcgraph()->machine()->I8x16Eq(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16Ne:
      return graph()->NewNode(mcgraph()->machine()->I8x16Ne(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16LtS:
      return graph()->NewNode(mcgraph()->machine()->I8x16GtS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI8x16LeS:
      return graph()->NewNode(mcgraph()->machine()->I8x16GeS(), inputs[1],
                              inputs[0]);
    case wasm::kExprI8x16GtS:
      return graph()->NewNode(mcgraph()->machine()->I8x16GtS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16GeS:
      return graph()->NewNode(mcgraph()->machine()->I8x16GeS(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16ShrU:
      return graph()->NewNode(mcgraph()->machine()->I8x16ShrU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16UConvertI16x8:
      return graph()->NewNode(mcgraph()->machine()->I8x16UConvertI16x8(),
                              inputs[0], inputs[1]);
    case wasm::kExprI8x16AddSatU:
      return graph()->NewNode(mcgraph()->machine()->I8x16AddSatU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16SubSatU:
      return graph()->NewNode(mcgraph()->machine()->I8x16SubSatU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16MinU:
      return graph()->NewNode(mcgraph()->machine()->I8x16MinU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16MaxU:
      return graph()->NewNode(mcgraph()->machine()->I8x16MaxU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16LtU:
      return graph()->NewNode(mcgraph()->machine()->I8x16GtU(), inputs[1],
                              inputs[0]);
    case wasm::kExprI8x16LeU:
      return graph()->NewNode(mcgraph()->machine()->I8x16GeU(), inputs[1],
                              inputs[0]);
    case wasm::kExprI8x16GtU:
      return graph()->NewNode(mcgraph()->machine()->I8x16GtU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16GeU:
      return graph()->NewNode(mcgraph()->machine()->I8x16GeU(), inputs[0],
                              inputs[1]);
    case wasm::kExprI8x16RoundingAverageU:
      return graph()->NewNode(mcgraph()->machine()->I8x16RoundingAverageU(),
                              inputs[0], inputs[1]);
    case wasm::kExprI8x16Popcnt:
      return graph()->NewNode(mcgraph()->machine()->I8x16Popcnt(), inputs[0]);
    case wasm::kExprI8x16Abs:
      return graph()->NewNode(mcgraph()->machine()->I8x16Abs(), inputs[0]);
    case wasm::kExprI8x16BitMask:
      return graph()->NewNode(mcgraph()->machine()->I8x16BitMask(), inputs[0]);
    case wasm::kExprS128And:
      return graph()->NewNode(mcgraph()->machine()->S128And(), inputs[0],
                              inputs[1]);
    case wasm::kExprS128Or:
      return graph()->NewNode(mcgraph()->machine()->S128Or(), inputs[0],
                              inputs[1]);
    case wasm::kExprS128Xor:
      return graph()->NewNode(mcgraph()->machine()->S128Xor(), inputs[0],
                              inputs[1]);
    case wasm::kExprS128Not:
      return graph()->NewNode(mcgraph()->machine()->S128Not(), inputs[0]);
    case wasm::kExprS128Select:
      return graph()->NewNode(mcgraph()->machine()->S128Select(), inputs[2],
                              inputs[0], inputs[1]);
    case wasm::kExprS128AndNot:
      return graph()->NewNode(mcgraph()->machine()->S128AndNot(), inputs[0],
                              inputs[1]);
    case wasm::kExprI64x2AllTrue:
      return graph()->NewNode(mcgraph()->machine()->I64x2AllTrue(), inputs[0]);
    case wasm::kExprI32x4AllTrue:
      return graph()->NewNode(mcgraph()->machine()->I32x4AllTrue(), inputs[0]);
    case wasm::kExprI16x8AllTrue:
      return graph()->NewNode(mcgraph()->machine()->I16x8AllTrue(), inputs[0]);
    case wasm::kExprV128AnyTrue:
      return graph()->NewNode(mcgraph()->machine()->V128AnyTrue(), inputs[0]);
    case wasm::kExprI8x16AllTrue:
      return graph()->NewNode(mcgraph()->machine()->I8x16AllTrue(), inputs[0]);
    case wasm::kExprI8x16Swizzle:
      return graph()->NewNode(mcgraph()->machine()->I8x16Swizzle(false),
                              inputs[0], inputs[1]);
    case wasm::kExprI8x16RelaxedSwizzle:
      return graph()->NewNode(mcgraph()->machine()->I8x16Swizzle(true),
                              inputs[0], inputs[1]);
    case wasm::kExprI8x16RelaxedLaneSelect:
      // Relaxed lane select puts the mask as first input (same as S128Select).
      return graph()->NewNode(mcgraph()->machine()->I8x16RelaxedLaneSelect(),
                              inputs[2], inputs[0], inputs[1]);
    case wasm::kExprI16x8RelaxedLaneSelect:
      return graph()->NewNode(mcgraph()->machine()->I16x8RelaxedLaneSelect(),
                              inputs[2], inputs[0], inputs[1]);
    case wasm::kExprI32x4RelaxedLaneSelect:
      return graph()->NewNode(mcgraph()->machine()->I32x4RelaxedLaneSelect(),
                              inputs[2], inputs[0], inputs[1]);
    case wasm::kExprI64x2RelaxedLaneSelect:
      return graph()->NewNode(mcgraph()->machine()->I64x2RelaxedLaneSelect(),
                              inputs[2], inputs[0], inputs[1]);
    case wasm::kExprF32x4RelaxedMin:
      return graph()->NewNode(mcgraph()->machine()->F32x4RelaxedMin(),
                              inputs[0], inputs[1]);
    case wasm::kExprF32x4RelaxedMax:
      return graph()->NewNode(mcgraph()->machine()->F32x4RelaxedMax(),
                              inputs[0], inputs[1]);
    case wasm::kExprF64x2RelaxedMin:
      return graph()->NewNode(mcgraph()->machine()->F64x2RelaxedMin(),
                              inputs[0], inputs[1]);
    case wasm::kExprF64x2RelaxedMax:
      return graph()->NewNode(mcgraph()->machine()->F64x2RelaxedMax(),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4RelaxedTruncF64x2SZero:
      return graph()->NewNode(
          mcgraph()->machine()->I32x4RelaxedTruncF64x2SZero(), inputs[0]);
    case wasm::kExprI32x4RelaxedTruncF64x2UZero:
      return graph()->NewNode(
          mcgraph()->machine()->I32x4RelaxedTruncF64x2UZero(), inputs[0]);
    case wasm::kExprI32x4RelaxedTruncF32x4S:
      return graph()->NewNode(mcgraph()->machine()->I32x4RelaxedTruncF32x4S(),
                              inputs[0]);
    case wasm::kExprI32x4RelaxedTruncF32x4U:
      return graph()->NewNode(mcgraph()->machine()->I32x4RelaxedTruncF32x4U(),
                              inputs[0]);
    default:
      FATAL_UNSUPPORTED_OPCODE(opcode);
  }
}

Node* WasmGraphBuilder::SimdLaneOp(wasm::WasmOpcode opcode, uint8_t lane,
                                   Node* const* inputs) {
  has_simd_ = true;
  switch (opcode) {
    case wasm::kExprF64x2ExtractLane:
      return graph()->NewNode(mcgraph()->machine()->F64x2ExtractLane(lane),
                              inputs[0]);
    case wasm::kExprF64x2ReplaceLane:
      return graph()->NewNode(mcgraph()->machine()->F64x2ReplaceLane(lane),
                              inputs[0], inputs[1]);
    case wasm::kExprF32x4ExtractLane:
      return graph()->NewNode(mcgraph()->machine()->F32x4ExtractLane(lane),
                              inputs[0]);
    case wasm::kExprF32x4ReplaceLane:
      return graph()->NewNode(mcgraph()->machine()->F32x4ReplaceLane(lane),
                              inputs[0], inputs[1]);
    case wasm::kExprI64x2ExtractLane:
      return graph()->NewNode(mcgraph()->machine()->I64x2ExtractLane(lane),
                              inputs[0]);
    case wasm::kExprI64x2ReplaceLane:
      return graph()->NewNode(mcgraph()->machine()->I64x2ReplaceLane(lane),
                              inputs[0], inputs[1]);
    case wasm::kExprI32x4ExtractLane:
      return graph()->NewNode(mcgraph()->machine()->I32x4ExtractLane(lane),
                              inputs[0]);
    case wasm::kExprI32x4ReplaceLane:
      return graph()->NewNode(mcgraph()->machine()->I32x4ReplaceLane(lane),
                              inputs[0], inputs[1]);
    case wasm::kExprI16x8ExtractLaneS:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtractLaneS(lane),
                              inputs[0]);
    case wasm::kExprI16x8ExtractLaneU:
      return graph()->NewNode(mcgraph()->machine()->I16x8ExtractLaneU(lane),
                              inputs[0]);
    case wasm::kExprI16x8ReplaceLane:
      return graph()->NewNode(mcgraph()->machine()->I16x8ReplaceLane(lane),
                              inputs[0], inputs[1]);
    case wasm::kExprI8x16ExtractLaneS:
      return graph()->NewNode(mcgraph()->machine()->I8x16ExtractLaneS(lane),
                              inputs[0]);
    case wasm::kExprI8x16ExtractLaneU:
      return graph()->NewNode(mcgraph()->machine()->I8x16ExtractLaneU(lane),
                              inputs[0]);
    case wasm::kExprI8x16ReplaceLane:
      return graph()->NewNode(mcgraph()->machine()->I8x16ReplaceLane(lane),
                              inputs[0], inputs[1]);
    default:
      FATAL_UNSUPPORTED_OPCODE(opcode);
  }
}

Node* WasmGraphBuilder::Simd8x16ShuffleOp(const uint8_t shuffle[16],
                                          Node* const* inputs) {
  has_simd_ = true;
  return graph()->NewNode(mcgraph()->machine()->I8x16Shuffle(shuffle),
                          inputs[0], inputs[1]);
}

Node* WasmGraphBuilder::AtomicOp(const wasm::WasmMemory* memory,
                                 wasm::WasmOpcode opcode, Node* const* inputs,
                                 uint32_t alignment, uintptr_t offset,
                                 wasm::WasmCodePosition position) {
  struct AtomicOpInfo {
    enum Type : int8_t {
      kNoInput = 0,
      kOneInput = 1,
      kTwoInputs = 2,
      kSpecial
    };

    using OperatorByAtomicOpParams =
        const Operator* (MachineOperatorBuilder::*)(AtomicOpParameters);
    using OperatorByAtomicLoadRep =
        const Operator* (MachineOperatorBuilder::*)(AtomicLoadParameters);
    using OperatorByAtomicStoreRep =
        const Operator* (MachineOperatorBuilder::*)(AtomicStoreParameters);

    const Type type;
    const MachineType machine_type;
    const OperatorByAtomicOpParams operator_by_type = nullptr;
    const OperatorByAtomicLoadRep operator_by_atomic_load_params = nullptr;
    const OperatorByAtomicStoreRep operator_by_atomic_store_rep = nullptr;
    const wasm::ValueType wasm_type;

    constexpr AtomicOpInfo(Type t, MachineType m, OperatorByAtomicOpParams o)
        : type(t), machine_type(m), operator_by_type(o) {}
    constexpr AtomicOpInfo(Type t, MachineType m, OperatorByAtomicLoadRep o,
                           wasm::ValueType v)
        : type(t),
          machine_type(m),
          operator_by_atomic_load_params(o),
          wasm_type(v) {}
    constexpr AtomicOpInfo(Type t, MachineType m, OperatorByAtomicStoreRep o,
                           wasm::ValueType v)
        : type(t),
          machine_type(m),
          operator_by_atomic_store_rep(o),
          wasm_type(v) {}

    // Constexpr, hence just a table lookup in most compilers.
    static constexpr AtomicOpInfo Get(wasm::WasmOpcode opcode) {
      switch (opcode) {
#define CASE(Name, Type, MachType, Op) \
  case wasm::kExpr##Name:              \
    return {Type, MachineType::MachType(), &MachineOperatorBuilder::Op};
#define CASE_LOAD_STORE(Name, Type, MachType, Op, WasmType)             \
  case wasm::kExpr##Name:                                               \
    return {Type, MachineType::MachType(), &MachineOperatorBuilder::Op, \
            WasmType};

        // Binops.
        CASE(I32AtomicAdd, kOneInput, Uint32, Word32AtomicAdd)
        CASE(I64AtomicAdd, kOneInput, Uint64, Word64AtomicAdd)
        CASE(I32AtomicAdd8U, kOneInput, Uint8, Word32AtomicAdd)
        CASE(I32AtomicAdd16U, kOneInput, Uint16, Word32AtomicAdd)
        CASE(I64AtomicAdd8U, kOneInput, Uint8, Word64AtomicAdd)
        CASE(I64AtomicAdd16U, kOneInput, Uint16, Word64AtomicAdd)
        CASE(I64AtomicAdd32U, kOneInput, Uint32, Word64AtomicAdd)
        CASE(I32AtomicSub, kOneInput, Uint32, Word32AtomicSub)
        CASE(I64AtomicSub, kOneInput, Uint64, Word64AtomicSub)
        CASE(I32AtomicSub8U, kOneInput, Uint8, Word32AtomicSub)
        CASE(I32AtomicSub16U, kOneInput, Uint16, Word32AtomicSub)
        CASE(I64AtomicSub8U, kOneInput, Uint8, Word64AtomicSub)
        CASE(I64AtomicSub16U, kOneInput, Uint16, Word64AtomicSub)
        CASE(I64AtomicSub32U, kOneInput, Uint32, Word64AtomicSub)
        CASE(I32AtomicAnd, kOneInput, Uint32, Word32AtomicAnd)
        CASE(I64AtomicAnd, kOneInput, Uint64, Word64AtomicAnd)
        CASE(I32AtomicAnd8U, kOneInput, Uint8, Word32AtomicAnd)
        CASE(I32AtomicAnd16U, kOneInput, Uint16, Word32AtomicAnd)
        CASE(I64AtomicAnd8U, kOneInput, Uint8, Word64AtomicAnd)
        CASE(I64AtomicAnd16U, kOneInput, Uint16, Word64AtomicAnd)
        CASE(I64AtomicAnd32U, kOneInput, Uint32, Word64AtomicAnd)
        CASE(I32AtomicOr, kOneInput, Uint32, Word32AtomicOr)
        CASE(I64AtomicOr, kOneInput, Uint64, Word64AtomicOr)
        CASE(I32AtomicOr8U, kOneInput, Uint8, Word32AtomicOr)
        CASE(I32AtomicOr16U, kOneInput, Uint16, Word32AtomicOr)
        CASE(I64AtomicOr8U, kOneInput, Uint8, Word64AtomicOr)
        CASE(I64AtomicOr16U, kOneInput, Uint16, Word64AtomicOr)
        CASE(I64AtomicOr32U, kOneInput, Uint32, Word64AtomicOr)
        CASE(I32AtomicXor, kOneInput, Uint32, Word32AtomicXor)
        CASE(I64AtomicXor, kOneInput, Uint64, Word64AtomicXor)
        CASE(I32AtomicXor8U, kOneInput, Uint8, Word32AtomicXor)
        CASE(I32AtomicXor16U, kOneInput, Uint16, Word32AtomicXor)
        CASE(I64AtomicXor8U, kOneInput, Uint8, Word64AtomicXor)
        CASE(I64AtomicXor16U, kOneInput, Uint16, Word64AtomicXor)
        CASE(I64AtomicXor32U, kOneInput, Uint32, Word64AtomicXor)
        CASE(I32AtomicExchange, kOneInput, Uint32, Word32AtomicExchange)
        CASE(I64AtomicExchange, kOneInput, Uint64, Word64AtomicExchange)
        CASE(I32AtomicExchange8U, kOneInput, Uint8, Word32AtomicExchange)
        CASE(I32AtomicExchange16U, kOneInput, Uint16, Word32AtomicExchange)
        CASE(I64AtomicExchange8U, kOneInput, Uint8, Word64AtomicExchange)
        CASE(I64AtomicExchange16U, kOneInput, Uint16, Word64AtomicExchange)
        CASE(I64AtomicExchange32U, kOneInput, Uint32, Word64AtomicExchange)

        // Compare-exchange.
        CASE(I32AtomicCompareExchange, kTwoInputs, Uint32,
             Word32AtomicCompareExchange)
        CASE(I64AtomicCompareExchange, kTwoInputs, Uint64,
             Word64AtomicCompareExchange)
        CASE(I32AtomicCompareExchange8U, kTwoInputs, Uint8,
             Word32AtomicCompareExchange)
        CASE(I32AtomicCompareExchange16U, kTwoInputs, Uint16,
             Word32AtomicCompareExchange)
        CASE(I64AtomicCompareExchange8U, kTwoInputs, Uint8,
             Word64AtomicCompareExchange)
        CASE(I64AtomicCompareExchange16U, kTwoInputs, Uint16,
             Word64AtomicCompareExchange)
        CASE(I64AtomicCompareExchange32U, kTwoInputs, Uint32,
             Word64AtomicCompareExchange)

        // Load.
        CASE_LOAD_STORE(I32AtomicLoad, kNoInput, Uint32, Word32AtomicLoad,
                        wasm::kWasmI32)
        CASE_LOAD_STORE(I64AtomicLoad, kNoInput, Uint64, Word64AtomicLoad,
                        wasm::kWasmI64)
        CASE_LOAD_STORE(I32AtomicLoad8U, kNoInput, Uint8, Word32AtomicLoad,
                        wasm::kWasmI32)
        CASE_LOAD_STORE(I32AtomicLoad16U, kNoInput, Uint16, Word32AtomicLoad,
                        wasm::kWasmI32)
        CASE_LOAD_STORE(I64AtomicLoad8U, kNoInput, Uint8, Word64AtomicLoad,
                        wasm::kWasmI64)
        CASE_LOAD_STORE(I64AtomicLoad16U, kNoInput, Uint16, Word64AtomicLoad,
                        wasm::kWasmI64)
        CASE_LOAD_STORE(I64AtomicLoad32U, kNoInput, Uint32, Word64AtomicLoad,
                        wasm::kWasmI64)

        // Store.
        CASE_LOAD_STORE(I32AtomicStore, kOneInput, Uint32, Word32AtomicStore,
                        wasm::kWasmI32)
        CASE_LOAD_STORE(I64AtomicStore, kOneInput, Uint64, Word64AtomicStore,
                        wasm::kWasmI64)
        CASE_LOAD_STORE(I32AtomicStore8U, kOneInput, Uint8, Word32AtomicStore,
                        wasm::kWasmI32)
        CASE_LOAD_STORE(I32AtomicStore16U, kOneInput, Uint16, Word32AtomicStore,
                        wasm::kWasmI32)
        CASE_LOAD_STORE(I64AtomicStore8U, kOneInput, Uint8, Word64AtomicStore,
                        wasm::kWasmI64)
        CASE_LOAD_STORE(I64AtomicStore16U, kOneInput, Uint16, Word64AtomicStore,
                        wasm::kWasmI64)
        CASE_LOAD_STORE(I64AtomicStore32U, kOneInput, Uint32, Word64AtomicStore,
                        wasm::kWasmI64)

#undef CASE
#undef CASE_LOAD_STORE

        case wasm::kExprAtomicNotify:
          return {kSpecial, MachineType::Int32(),
                  OperatorByAtomicOpParams{nullptr}};
        case wasm::kExprI32AtomicWait:
          return {kSpecial, MachineType::Int32(),
                  OperatorByAtomicOpParams{nullptr}};
        case wasm::kExprI64AtomicWait:
          return {kSpecial, MachineType::Int64(),
                  OperatorByAtomicOpParams{nullptr}};
        default:
          UNREACHABLE();
      }
    }
  };

  AtomicOpInfo info = AtomicOpInfo::Get(opcode);

  const auto enforce_bounds_check = info.type != AtomicOpInfo::kSpecial
    ? EnforceBoundsCheck::kCanOmitBoundsCheck
    : EnforceBoundsCheck::kNeedsBoundsCheck;
  Node* index;
  BoundsCheckResult bounds_check_result;
  // Atomic operations need bounds checks until the backend can emit protected
  // loads. Independently, an alignemnt check is needed as well.
  std::tie(index, bounds_check_result) =
      BoundsCheckMem(memory, info.machine_type.MemSize(), inputs[0], offset,
                     position, enforce_bounds_check, AlignmentCheck::kYes);
  // MemoryAccessKind::kUnaligned is impossible due to explicit aligment check.
  MemoryAccessKind access_kind =
      bounds_check_result == BoundsCheckResult::kTrapHandler
          ? MemoryAccessKind::kProtected
          : MemoryAccessKind::kNormal;

  if (info.type != AtomicOpInfo::kSpecial) {
    const Operator* op;
    if (info.operator_by_type) {
      op = (mcgraph()->machine()->*info.operator_by_type)(
          AtomicOpParameters(info.machine_type,
                             access_kind));
    } else if (info.operator_by_atomic_load_params) {
      op = (mcgraph()->machine()->*info.operator_by_atomic_load_params)(
          AtomicLoadParameters(info.machine_type, AtomicMemoryOrder::kSeqCst,
                               access_kind));
    } else {
      op = (mcgraph()->machine()->*info.operator_by_atomic_store_rep)(
          AtomicStoreParameters(info.machine_type.representation(),
                                WriteBarrierKind::kNoWriteBarrier,
                                AtomicMemoryOrder::kSeqCst,
                                access_kind));
    }

    Node* input_nodes[6] = {MemBuffer(memory->index, offset), index};
    int num_actual_inputs = info.type;
    std::copy_n(inputs + 1, num_actual_inputs, input_nodes + 2);
    input_nodes[num_actual_inputs + 2] = effect();
    input_nodes[num_actual_inputs + 3] = control();

#ifdef V8_TARGET_BIG_ENDIAN
    // Reverse the value bytes before storing.
    if (info.operator_by_atomic_store_rep) {
      input_nodes[num_actual_inputs + 1] = BuildChangeEndiannessStore(
          input_nodes[num_actual_inputs + 1],
          info.machine_type.representation(), info.wasm_type);
    }
#endif

    Node* result = gasm_->AddNode(
        graph()->NewNode(op, num_actual_inputs + 4, input_nodes));

    if (access_kind == MemoryAccessKind::kProtected) {
      SetSourcePosition(result, position);
    }

#ifdef V8_TARGET_BIG_ENDIAN
    // Reverse the value bytes after load.
    if (info.operator_by_atomic_load_params) {
      result =
          BuildChangeEndiannessLoad(result, info.machine_type, info.wasm_type);
    }
#endif

    return result;
  }

  Node* memory_index = gasm_->Int32Constant(memory->index);
  Node* effective_offset = gasm_->IntAdd(gasm_->UintPtrConstant(offset), index);

  switch (opcode) {
    case wasm::kExprAtomicNotify: {
      Node* function =
          gasm_->ExternalConstant(ExternalReference::wasm_atomic_notify());
      auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                     .Params(MachineType::Pointer(), MachineType::Uint32());

      Node* addr = gasm_->IntAdd(MemStart(memory->index), effective_offset);
      Node* num_waiters_to_wake = inputs[1];

      return BuildCCall(&sig, function, addr, num_waiters_to_wake);
    }

    case wasm::kExprI32AtomicWait: {
      constexpr StubCallMode kStubMode = StubCallMode::kCallWasmRuntimeStub;
      auto* call_descriptor = GetBuiltinCallDescriptor(
          Builtin::kWasmI32AtomicWait, zone_, kStubMode);

      Builtin target = Builtin::kWasmI32AtomicWait;
      Node* call_target = mcgraph()->RelocatableWasmBuiltinCallTarget(target);

      return gasm_->Call(call_descriptor, call_target, memory_index,
                         effective_offset, inputs[1],
                         BuildChangeInt64ToBigInt(inputs[2], kStubMode));
    }

    case wasm::kExprI64AtomicWait: {
      constexpr StubCallMode kStubMode = StubCallMode::kCallWasmRuntimeStub;
      auto* call_descriptor = GetBuiltinCallDescriptor(
          Builtin::kWasmI64AtomicWait, zone_, kStubMode);

      Builtin target = Builtin::kWasmI64AtomicWait;
      Node* call_target = mcgraph()->RelocatableWasmBuiltinCallTarget(target);

      return gasm_->Call(call_descriptor, call_target, memory_index,
                         effective_offset,
                         BuildChangeInt64ToBigInt(inputs[1], kStubMode),
                         BuildChangeInt64ToBigInt(inputs[2], kStubMode));
    }

    default:
      FATAL_UNSUPPORTED_OPCODE(opcode);
  }
}

void WasmGraphBuilder::AtomicFence() {
  SetEffect(graph()->NewNode(
      mcgraph()->machine()->MemoryBarrier(AtomicMemoryOrder::kSeqCst), effect(),
      control()));
}

void WasmGraphBuilder::MemoryInit(const wasm::WasmMemory* memory,
                                  uint32_t data_segment_index, Node* dst,
                                  Node* src, Node* size,
                                  wasm::WasmCodePosition position) {
  // The data segment index must be in bounds since it is required by
  // validation.
  DCHECK_LT(data_segment_index, env_->module->num_declared_data_segments);

  Node* function =
      gasm_->ExternalConstant(ExternalReference::wasm_memory_init());

  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&dst}, position);

  auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                 .Params(MachineType::Pointer(), MachineType::Uint32(),
                         MachineType::UintPtr(), MachineType::Uint32(),
                         MachineType::Uint32(), MachineType::Uint32());
  Node* call = BuildCCall(&sig, function, GetInstanceData(),
                          gasm_->Int32Constant(memory->index), dst, src,
                          gasm_->Uint32Constant(data_segment_index), size);

  // TODO(manoskouk): Also throw kDataSegmentOutOfBounds.
  TrapIfFalse(wasm::kTrapMemOutOfBounds, call, position);
}

void WasmGraphBuilder::DataDrop(uint32_t data_segment_index,
                                wasm::WasmCodePosition position) {
  DCHECK_LT(data_segment_index, env_->module->num_declared_data_segments);

  Node* seg_size_array =
      LOAD_INSTANCE_FIELD(DataSegmentSizes, MachineType::TaggedPointer());
  static_assert(wasm::kV8MaxWasmDataSegments <= kMaxUInt32 >> 2);
  auto access = ObjectAccess(MachineType::Int32(), kNoWriteBarrier);
  gasm_->StoreToObject(
      access, seg_size_array,
      wasm::ObjectAccess::ElementOffsetInTaggedFixedUInt32Array(
          data_segment_index),
      Int32Constant(0));
}

Node* WasmGraphBuilder::StoreArgsInStackSlot(
    std::initializer_list<std::pair<MachineRepresentation, Node*>> args) {
  int slot_size = 0;
  for (auto arg : args) {
    slot_size += ElementSizeInBytes(arg.first);
  }
  DCHECK_LT(0, slot_size);
  Node* stack_slot =
      graph()->NewNode(mcgraph()->machine()->StackSlot(slot_size));

  int offset = 0;
  for (auto arg : args) {
    MachineRepresentation type = arg.first;
    Node* value = arg.second;
    gasm_->StoreUnaligned(type, stack_slot, Int32Constant(offset), value);
    offset += ElementSizeInBytes(type);
  }
  return stack_slot;
}

void WasmGraphBuilder::MemTypeToUintPtrOrOOBTrap(
    bool is_memory64, std::initializer_list<Node**> nodes,
    wasm::WasmCodePosition position) {
  MemOrTableTypeToUintPtrOrOOBTrap(is_memory64, nodes, position,
                                   wasm::kTrapMemOutOfBounds);
}

void WasmGraphBuilder::TableTypeToUintPtrOrOOBTrap(
    bool is_table64, std::initializer_list<Node**> nodes,
    wasm::WasmCodePosition position) {
  MemOrTableTypeToUintPtrOrOOBTrap(is_table64, nodes, position,
                                   wasm::kTrapTableOutOfBounds);
}

void WasmGraphBuilder::MemOrTableTypeToUintPtrOrOOBTrap(
    bool index_type_is_64bit, std::initializer_list<Node**> nodes,
    wasm::WasmCodePosition position, wasm::TrapReason trap_reason) {
  if (!index_type_is_64bit) {
    for (Node** node : nodes) {
      *node = gasm_->BuildChangeUint32ToUintPtr(*node);
    }
    return;
  }
  if constexpr (Is64()) return;
  Node* any_high_word = nullptr;
  for (Node** node : nodes) {
    Node* high_word =
        gasm_->TruncateInt64ToInt32(gasm_->Word64Shr(*node, Int32Constant(32)));
    any_high_word =
        any_high_word ? gasm_->Word32Or(any_high_word, high_word) : high_word;
    // Only keep the low word as uintptr_t.
    *node = gasm_->TruncateInt64ToInt32(*node);
  }
  TrapIfTrue(trap_reason, any_high_word, position);
}

void WasmGraphBuilder::MemoryCopy(const wasm::WasmMemory* dst_memory,
                                  const wasm::WasmMemory* src_memory, Node* dst,
                                  Node* src, Node* size,
                                  wasm::WasmCodePosition position) {
  Node* function =
      gasm_->ExternalConstant(ExternalReference::wasm_memory_copy());

  if (dst_memory->is_memory64 == src_memory->is_memory64) {
    MemTypeToUintPtrOrOOBTrap(dst_memory->is_memory64, {&dst, &src, &size},
                              position);
  } else {
    MemTypeToUintPtrOrOOBTrap(dst_memory->is_memory64, {&dst}, position);
    MemTypeToUintPtrOrOOBTrap(src_memory->is_memory64, {&src}, position);
    MemTypeToUintPtrOrOOBTrap(
        dst_memory->is_memory64 && src_memory->is_memory64, {&size}, position);
  }

  auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                 .Params(MachineType::Pointer(), MachineType::Uint32(),
                         MachineType::Uint32(), MachineType::UintPtr(),
                         MachineType::UintPtr(), MachineType::UintPtr());

  Node* call =
      BuildCCall(&sig, function, GetInstanceData(),
                 gasm_->Int32Constant(dst_memory->index),
                 gasm_->Int32Constant(src_memory->index), dst, src, size);
  TrapIfFalse(wasm::kTrapMemOutOfBounds, call, position);
}

void WasmGraphBuilder::MemoryFill(const wasm::WasmMemory* memory, Node* dst,
                                  Node* value, Node* size,
                                  wasm::WasmCodePosition position) {
  Node* function =
      gasm_->ExternalConstant(ExternalReference::wasm_memory_fill());

  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&dst, &size}, position);

  auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                 .Params(MachineType::Pointer(), MachineType::Uint32(),
                         MachineType::UintPtr(), MachineType::Uint8(),
                         MachineType::UintPtr());
  Node* call =
      BuildCCall(&sig, function, GetInstanceData(),
                 gasm_->Int32Constant(memory->index), dst, value, size);
  TrapIfFalse(wasm::kTrapMemOutOfBounds, call, position);
}

void WasmGraphBuilder::TableInit(uint32_t table_index,
                                 uint32_t elem_segment_index, Node* dst,
                                 Node* src, Node* size,
                                 wasm::WasmCodePosition position) {
  const wasm::WasmTable& table = env_->module->tables[table_index];
  TableTypeToUintPtrOrOOBTrap(table.is_table64, {&dst}, position);
  gasm_->CallBuiltinThroughJumptable(
      Builtin::kWasmTableInit, Operator::kNoThrow, dst, src, size,
      gasm_->NumberConstant(table_index),
      gasm_->NumberConstant(elem_segment_index), gasm_->Int32Constant(0));
}

void WasmGraphBuilder::ElemDrop(uint32_t elem_segment_index,
                                wasm::WasmCodePosition position) {
  // The elem segment index must be in bounds since it is required by
  // validation.
  DCHECK_LT(elem_segment_index, env_->module->elem_segments.size());

  Node* elem_segments =
      LOAD_INSTANCE_FIELD(ElementSegments, MachineType::TaggedPointer());
  gasm_->StoreFixedArrayElement(
      elem_segments, elem_segment_index,
      LOAD_ROOT(EmptyFixedArray, empty_fixed_array),
      ObjectAccess(MachineType::TaggedPointer(), kFullWriteBarrier));
}

void WasmGraphBuilder::TableCopy(uint32_t table_dst_index,
                                 uint32_t table_src_index, Node* dst, Node* src,
                                 Node* size, wasm::WasmCodePosition position) {
  const wasm::WasmTable& table_dst = env_->module->tables[table_dst_index];
  const wasm::WasmTable& table_src = env_->module->tables[table_src_index];
  // TODO(crbug.com/338024338): Merge the `TableTypeToUintPtrOrOOBTrap` calls
  // into one. This would result in smaller graphs because we would have a
  // single `TrapIf` node that uses the combined high words of `dst`, `src`, and
  // `size`.
  TableTypeToUintPtrOrOOBTrap(table_dst.is_table64, {&dst}, position);
  TableTypeToUintPtrOrOOBTrap(table_src.is_table64, {&src}, position);
  TableTypeToUintPtrOrOOBTrap(table_src.is_table64 && table_dst.is_table64,
                              {&size}, position);
  gasm_->CallBuiltinThroughJumptable(
      Builtin::kWasmTableCopy, Operator::kNoThrow, dst, src, size,
      gasm_->NumberConstant(table_dst_index),
      gasm_->NumberConstant(table_src_index), gasm_->NumberConstant(0));
}

Node* WasmGraphBuilder::TableGrow(uint32_t table_index, Node* value,
                                  Node* delta,
                                  wasm::WasmCodePosition position) {
  const wasm::WasmTable& table = env_->module->tables[table_index];
  auto done = gasm_->MakeLabel(MachineRepresentation::kWord32);

  // If `delta` is OOB, return -1.
  if (!table.is_table64) {
    delta = gasm_->BuildChangeUint32ToUintPtr(delta);
  } else if constexpr (!Is64()) {
    Node* high_word =
        gasm_->TruncateInt64ToInt32(gasm_->Word64Shr(delta, Int32Constant(32)));
    gasm_->GotoIf(high_word, &done, Int32Constant(-1));
    delta = gasm_->TruncateInt64ToInt32(delta);
  }

  Node* result =
      gasm_->BuildChangeSmiToInt32(gasm_->CallBuiltinThroughJumptable(
          Builtin::kWasmTableGrow, Operator::kNoThrow,
          gasm_->NumberConstant(table_index), delta, gasm_->Int32Constant(0),
          value));
  gasm_->Goto(&done, result);

  gasm_->Bind(&done);
  result = done.PhiAt(0);

  return table.is_table64 ? gasm_->ChangeInt32ToInt64(result) : result;
}

Node* WasmGraphBuilder::TableSize(uint32_t table_index) {
  Node* tables = LOAD_INSTANCE_FIELD(Tables, MachineType::TaggedPointer());
  Node* table = gasm_->LoadFixedArrayElementAny(tables, table_index);

  int length_field_size = WasmTableObject::kCurrentLengthOffsetEnd -
                          WasmTableObject::kCurrentLengthOffset + 1;
  Node* length_smi = gasm_->LoadFromObject(
      assert_size(length_field_size, MachineType::TaggedSigned()), table,
      wasm::ObjectAccess::ToTagged(WasmTableObject::kCurrentLengthOffset));
  Node* length32 = gasm_->BuildChangeSmiToInt32(length_smi);
  return env_->module->tables[table_index].is_table64
             ? gasm_->ChangeInt32ToInt64(length32)
             : length32;
}

void WasmGraphBuilder::TableFill(uint32_t table_index, Node* start, Node* value,
                                 Node* count, wasm::WasmCodePosition position) {
  const wasm::WasmTable& table = env_->module->tables[table_index];
  TableTypeToUintPtrOrOOBTrap(table.is_table64, {&start, &count}, position);
  gasm_->CallBuiltinThroughJumptable(
      Builtin::kWasmTableFill, Operator::kNoThrow, start, count,
      gasm_->Int32Constant(false), gasm_->NumberConstant(table_index), value);
}

Node* WasmGraphBuilder::DefaultValue(wasm::ValueType type) {
  DCHECK(type.is_defaultable());
  switch (type.kind()) {
    case wasm::kI8:
    case wasm::kI16:
    case wasm::kI32:
      return Int32Constant(0);
    case wasm::kI64:
      return Int64Constant(0);
    case wasm::kF16:
    case wasm::kF32:
      return Float32Constant(0);
    case wasm::kF64:
      return Float64Constant(0);
    case wasm::kS128:
      return S128Zero();
    case wasm::kRefNull:
      return RefNull(type);
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kBottom:
    case wasm::kRef:
      UNREACHABLE();
  }
}

Node* WasmGraphBuilder::StructNew(uint32_t struct_index,
                                  const wasm::StructType* type, Node* rtt,
                                  base::Vector<Node*> fields) {
  int size = WasmStruct::Size(type);
  Node* s = gasm_->Allocate(size);
  gasm_->StoreMap(s, rtt);
  gasm_->InitializeImmutableInObject(
      ObjectAccess(MachineType::TaggedPointer(), kNoWriteBarrier), s,
      wasm::ObjectAccess::ToTagged(JSReceiver::kPropertiesOrHashOffset),
      LOAD_ROOT(EmptyFixedArray, empty_fixed_array));
  for (uint32_t i = 0; i < type->field_count(); i++) {
    gasm_->StructSet(s, fields[i], type, i, kWithoutNullCheck);
  }
  // If this assert fails then initialization of padding field might be
  // necessary.
  static_assert(Heap::kMinObjectSizeInTaggedWords == 2 &&
                    WasmStruct::kHeaderSize == 2 * kTaggedSize,
                "empty struct might require initialization of padding field");
  return s;
}

Node* WasmGraphBuilder::ArrayNew(uint32_t array_index,
                                 const wasm::ArrayType* type, Node* length,
                                 Node* initial_value, Node* rtt,
                                 wasm::WasmCodePosition position) {
  TrapIfFalse(wasm::kTrapArrayTooLarge,
              gasm_->Uint32LessThanOrEqual(
                  length, gasm_->Uint32Constant(WasmArray::MaxLength(type))),
              position);
  wasm::ValueType element_type = type->element_type();

  // RoundUp(length * value_size, kObjectAlignment) =
  //   RoundDown(length * value_size + kObjectAlignment - 1,
  //             kObjectAlignment);
  Node* padded_length = gasm_->Word32And(
      gasm_->Int32Add(
          gasm_->Int32Mul(length,
                          Int32Constant(element_type.value_kind_size())),
          Int32Constant(kObjectAlignment - 1)),
      Int32Constant(-kObjectAlignment));
  Node* a = gasm_->Allocate(
      gasm_->Int32Add(padded_length, Int32Constant(WasmArray::kHeaderSize)));

  // Initialize the array header.
  gasm_->StoreMap(a, rtt);
  gasm_->InitializeImmutableInObject(
      ObjectAccess(MachineType::TaggedPointer(), kNoWriteBarrier), a,
      wasm::ObjectAccess::ToTagged(JSReceiver::kPropertiesOrHashOffset),
      LOAD_ROOT(EmptyFixedArray, empty_fixed_array));
  gasm_->ArrayInitializeLength(a, length);

  ArrayFillImpl(a, gasm_->Int32Constant(0),
                initial_value != nullptr
                    ? initial_value
                    : SetType(DefaultValue(element_type),
                              type->element_type().Unpacked()),
                length, type, false);

  return a;
}

Node* WasmGraphBuilder::ArrayNewFixed(const wasm::ArrayType* type, Node* rtt,
                                      base::Vector<Node*> elements) {
  wasm::ValueType element_type = type->element_type();
  Node* array = gasm_->Allocate(RoundUp(element_type.value_kind_size() *
                                            static_cast<int>(elements.size()),
                                        kObjectAlignment) +
                                WasmArray::kHeaderSize);
  gasm_->StoreMap(array, rtt);
  gasm_->InitializeImmutableInObject(
      ObjectAccess(MachineType::TaggedPointer(), kNoWriteBarrier), array,
      wasm::ObjectAccess::ToTagged(JSReceiver::kPropertiesOrHashOffset),
      LOAD_ROOT(EmptyFixedArray, empty_fixed_array));
  gasm_->ArrayInitializeLength(
      array, SetType(Int32Constant(static_cast<int>(elements.size())),
                     wasm::kWasmI32));
  for (int i = 0; i < static_cast<int>(elements.size()); i++) {
    gasm_->ArraySet(array, gasm_->Int32Constant(i), elements[i], type);
  }
  return array;
}

Node* WasmGraphBuilder::ArrayNewSegment(uint32_t segment_index, Node* offset,
                                        Node* length, Node* rtt,
                                        bool is_element,
                                        wasm::WasmCodePosition position) {
  // This call cannot be marked as eliminatable because it performs an array
  // maximum size check.
  Node* array = gasm_->CallBuiltin(
      Builtin::kWasmArrayNewSegment, Operator::kNoProperties,
      gasm_->Uint32Constant(segment_index), offset, length,
      gasm_->SmiConstant(is_element ? 1 : 0), gasm_->SmiConstant(0), rtt);
  SetSourcePosition(array, position);
  return array;
}

// TODO(jkummerow): This check would be more elegant if we made
// {ArrayNewSegment} a high-level node that's lowered later.
bool IsArrayNewSegment(Node* node) {
  if (node->opcode() != IrOpcode::kCall) return false;
  Node* callee = NodeProperties::GetValueInput(node, 0);
  if (callee->opcode() != IrOpcode::kNumberConstant) return false;
  double target = OpParameter<double>(callee->op());
  return target == static_cast<double>(Builtin::kWasmArrayNewSegment);
}

void WasmGraphBuilder::ArrayInitSegment(uint32_t segment_index, Node* array,
                                        Node* array_index, Node* segment_offset,
                                        Node* length, bool is_element,
                                        wasm::WasmCodePosition position) {
  gasm_->CallBuiltin(
      Builtin::kWasmArrayInitSegment, Operator::kNoProperties, array_index,
      segment_offset, length, gasm_->SmiConstant(segment_index),
      gasm_->SmiConstant(is_element ? 1 : 0), gasm_->SmiConstant(0), array);
  SetSourcePosition(control(), position);
}

Node* WasmGraphBuilder::RttCanon(uint32_t type_index) {
  Node* rtt = graph()->NewNode(gasm_->simplified()->RttCanon(type_index),
                               GetInstanceData());
  return SetType(rtt, wasm::ValueType::Rtt(type_index));
}

WasmGraphBuilder::Callbacks WasmGraphBuilder::TestCallbacks(
    GraphAssemblerLabel<1>* label) {
  return {// succeed_if
          [this, label](Node* condition, BranchHint hint) -> void {
            gasm_->GotoIf(condition, label, hint, Int32Constant(1));
          },
          // fail_if
          [this, label](Node* condition, BranchHint hint) -> void {
            gasm_->GotoIf(condition, label, hint, Int32Constant(0));
          },
          // fail_if_not
          [this, label](Node* condition, BranchHint hint) -> void {
            gasm_->GotoIfNot(condition, label, hint, Int32Constant(0));
          }};
}

WasmGraphBuilder::Callbacks WasmGraphBuilder::CastCallbacks(
    GraphAssemblerLabel<0>* label, wasm::WasmCodePosition position) {
  return {// succeed_if
          [this, label](Node* condition, BranchHint hint) -> void {
            gasm_->GotoIf(condition, label, hint);
          },
          // fail_if
          [this, position](Node* condition, BranchHint hint) -> void {
            TrapIfTrue(wasm::kTrapIllegalCast, condition, position);
          },
          // fail_if_not
          [this, position](Node* condition, BranchHint hint) -> void {
            TrapIfFalse(wasm::kTrapIllegalCast, condition, position);
          }};
}

WasmGraphBuilder::Callbacks WasmGraphBuilder::BranchCallbacks(
    SmallNodeVector& no_match_controls, SmallNodeVector& no_match_effects,
    SmallNodeVector& match_controls, SmallNodeVector& match_effects) {
  return {
      // succeed_if
      [&](Node* condition, BranchHint hint) -> void {
        Node* branch = graph()->NewNode(mcgraph()->common()->Branch(hint),
                                        condition, control());
        match_controls.emplace_back(
            graph()->NewNode(mcgraph()->common()->IfTrue(), branch));
        match_effects.emplace_back(effect());
        SetControl(graph()->NewNode(mcgraph()->common()->IfFalse(), branch));
      },
      // fail_if
      [&](Node* condition, BranchHint hint) -> void {
        Node* branch = graph()->NewNode(mcgraph()->common()->Branch(hint),
                                        condition, control());
        no_match_controls.emplace_back(
            graph()->NewNode(mcgraph()->common()->IfTrue(), branch));
        no_match_effects.emplace_back(effect());
        SetControl(graph()->NewNode(mcgraph()->common()->IfFalse(), branch));
      },
      // fail_if_not
      [&](Node* condition, BranchHint hint) -> void {
        Node* branch = graph()->NewNode(mcgraph()->common()->Branch(hint),
                                        condition, control());
        no_match_controls.emplace_back(
            graph()->NewNode(mcgraph()->common()->IfFalse(), branch));
        no_match_effects.emplace_back(effect());
        SetControl(graph()->NewNode(mcgraph()->common()->IfTrue(), branch));
      }};
}

void WasmGraphBuilder::EqCheck(Node* object, bool object_can_be_null,
                               Callbacks callbacks, bool null_succeeds) {
  if (object_can_be_null) {
    if (null_succeeds) {
      callbacks.succeed_if(IsNull(object, wasm::kWasmAnyRef),
                           BranchHint::kFalse);
    } else {
      // The {IsDataRefMap} check below will fail for {null} anyway.
    }
  }
  callbacks.succeed_if(gasm_->IsSmi(object), BranchHint::kFalse);
  Node* map = gasm_->LoadMap(object);
  callbacks.fail_if_not(gasm_->IsDataRefMap(map), BranchHint::kTrue);
}

void WasmGraphBuilder::ManagedObjectInstanceCheck(Node* object,
                                                  bool object_can_be_null,
                                                  InstanceType instance_type,
                                                  Callbacks callbacks,
                                                  bool null_succeeds) {
  if (object_can_be_null) {
    if (null_succeeds) {
      callbacks.succeed_if(IsNull(object, wasm::kWasmAnyRef),
                           BranchHint::kFalse);
    } else {
      // The {IsDataRefMap} check below will fail for {null} anyway.
    }
  }
  callbacks.fail_if(gasm_->IsSmi(object), BranchHint::kFalse);
  callbacks.fail_if_not(gasm_->HasInstanceType(object, instance_type),
                        BranchHint::kTrue);
}

void WasmGraphBuilder::StringCheck(Node* object, bool object_can_be_null,
                                   Callbacks callbacks, bool null_succeeds) {
  if (object_can_be_null) {
    if (null_succeeds) {
      callbacks.succeed_if(IsNull(object, wasm::kWasmAnyRef),
                           BranchHint::kFalse);
    } else {
      // The {IsDataRefMap} check below will fail for {null} anyway.
    }
  }
  callbacks.fail_if(gasm_->IsSmi(object), BranchHint::kFalse);
  Node* map = gasm_->LoadMap(object);
  Node* instance_type = gasm_->LoadInstanceType(map);
  Node* check = gasm_->Uint32LessThan(
      instance_type, gasm_->Uint32Constant(FIRST_NONSTRING_TYPE));
  callbacks.fail_if_not(check, BranchHint::kTrue);
}

WasmGraphBuilder::ResultNodesOfBr WasmGraphBuilder::BrOnCastAbs(
    std::function<void(Callbacks)> type_checker) {
  SmallNodeVector no_match_controls, no_match_effects, match_controls,
      match_effects;
  Node *match_control, *match_effect, *no_match_control, *no_match_effect;

  type_checker(BranchCallbacks(no_match_controls, no_match_effects,
                               match_controls, match_effects));

  match_controls.emplace_back(control());
  match_effects.emplace_back(effect());

  // Wire up the control/effect nodes.
  DCHECK_EQ(match_controls.size(), match_effects.size());
  unsigned match_count = static_cast<unsigned>(match_controls.size());
  if (match_count == 1) {
    match_control = match_controls[0];
    match_effect = match_effects[0];
  } else {
    match_control = Merge(match_count, match_controls.data());
    // EffectPhis need their control dependency as an additional input.
    match_effects.emplace_back(match_control);
    match_effect = EffectPhi(match_count, match_effects.data());
  }

  DCHECK_EQ(no_match_controls.size(), no_match_effects.size());
  unsigned no_match_count = static_cast<unsigned>(no_match_controls.size());
  if (no_match_count == 1) {
    no_match_control = no_match_controls[0];
    no_match_effect = no_match_effects[0];
  } else {
    // Range is 2..4, so casting to unsigned is safe.
    no_match_control = Merge(no_match_count, no_match_controls.data());
    // EffectPhis need their control dependency as an additional input.
    no_match_effects.emplace_back(no_match_control);
    no_match_effect = EffectPhi(no_match_count, no_match_effects.data());
  }

  return {match_control, match_effect, no_match_control, no_match_effect};
}

Node* WasmGraphBuilder::RefTest(Node* object, Node* rtt,
                                WasmTypeCheckConfig config) {
  return gasm_->WasmTypeCheck(object, rtt, config);
}

Node* WasmGraphBuilder::RefTestAbstract(Node* object,
                                        WasmTypeCheckConfig config) {
  DCHECK(!config.to.has_index());
  return gasm_->WasmTypeCheckAbstract(object, config);
}

Node* WasmGraphBuilder::RefCast(Node* object, Node* rtt,
                                WasmTypeCheckConfig config,
                                wasm::WasmCodePosition position) {
  Node* cast = gasm_->WasmTypeCast(object, rtt, config);
  SetSourcePosition(cast, position);
  return cast;
}

Node* WasmGraphBuilder::RefCastAbstract(Node* object,
                                        WasmTypeCheckConfig config,
                                        wasm::WasmCodePosition position) {
  DCHECK(!config.to.has_index());
  Node* cast = gasm_->WasmTypeCastAbstract(object, config);
  SetSourcePosition(cast, position);
  return cast;
}

WasmGraphBuilder::ResultNodesOfBr WasmGraphBuilder::BrOnCast(
    Node* object, Node* rtt, WasmTypeCheckConfig config) {
  auto [true_node, false_node] =
      BranchNoHint(gasm_->WasmTypeCheck(object, rtt, config));

  return {true_node,   // control on match
          effect(),    // effect on match
          false_node,  // control on no match
          effect()};   // effect on no match
}

WasmGraphBuilder::ResultNodesOfBr WasmGraphBuilder::BrOnEq(
    Node* object, Node* /*rtt*/, WasmTypeCheckConfig config) {
  return BrOnCastAbs([this, config, object](Callbacks callbacks) -> void {
    if (config.from.is_nullable()) {
      if (config.to.is_nullable()) {
        callbacks.succeed_if(gasm_->IsNull(object, config.from),
                             BranchHint::kFalse);
      } else {
        // The {IsDataRefMap} check below will fail for {null}.
      }
    }
    callbacks.succeed_if(gasm_->IsSmi(object), BranchHint::kFalse);
    Node* map = gasm_->LoadMap(object);
    callbacks.fail_if_not(gasm_->IsDataRefMap(map), BranchHint::kTrue);
  });
}

WasmGraphBuilder::ResultNodesOfBr WasmGraphBuilder::BrOnStruct(
    Node* object, Node* /*rtt*/, WasmTypeCheckConfig config) {
  bool null_succeeds = config.to.is_nullable();
  return BrOnCastAbs(
      [this, object, config, null_succeeds](Callbacks callbacks) -> void {
        return ManagedObjectInstanceCheck(object, config.from.is_nullable(),
                                          WASM_STRUCT_TYPE, callbacks,
                                          null_succeeds);
      });
}

WasmGraphBuilder::ResultNodesOfBr WasmGraphBuilder::BrOnArray(
    Node* object, Node* /*rtt*/, WasmTypeCheckConfig config) {
  bool null_succeeds = config.to.is_nullable();
  return BrOnCastAbs(
      [this, config, object, null_succeeds](Callbacks callbacks) -> void {
        return ManagedObjectInstanceCheck(object, config.from.is_nullable(),
                                          WASM_ARRAY_TYPE, callbacks,
                                          null_succeeds);
      });
}

WasmGraphBuilder::ResultNodesOfBr WasmGraphBuilder::BrOnI31(
    Node* object, Node* /* rtt */, WasmTypeCheckConfig config) {
  return BrOnCastAbs([this, object, config](Callbacks callbacks) -> void {
    if (config.from.is_nullable()) {
      if (config.to.is_nullable()) {
        callbacks.succeed_if(gasm_->IsNull(object, config.from),
                             BranchHint::kFalse);
      } else {
        // Covered by the {IsSmi} check below.
      }
    }
    callbacks.fail_if_not(gasm_->IsSmi(object), BranchHint::kTrue);
  });
}

WasmGraphBuilder::ResultNodesOfBr WasmGraphBuilder::BrOnString(
    Node* object, Node* /*rtt*/, WasmTypeCheckConfig config) {
  bool null_succeeds = config.to.is_nullable();
  return BrOnCastAbs(
      [this, config, object, null_succeeds](Callbacks callbacks) -> void {
        return StringCheck(object, config.from.is_nullable(), callbacks,
                           null_succeeds);
      });
}

Node* WasmGraphBuilder::TypeGuard(Node* value, wasm::ValueType type) {
  DCHECK_NOT_NULL(env_);
  return SetEffect(graph()->NewNode(mcgraph()->common()->TypeGuard(Type::Wasm(
                                        type, env_->module, graph()->zone())),
                                    value, effect(), control()));
}

Node* WasmGraphBuilder::StructGet(Node* struct_object,
                                  const wasm::StructType* struct_type,
                                  uint32_t field_index, CheckForNull null_check,
                                  bool is_signed,
                                  wasm::WasmCodePosition position) {
  Node* result = gasm_->StructGet(struct_object, struct_type, field_index,
                                  is_signed, null_check);
  SetSourcePosition(result, position);
  return result;
}

void WasmGraphBuilder::StructSet(Node* struct_object,
                                 const wasm::StructType* struct_type,
                                 uint32_t field_index, Node* field_value,
                                 CheckForNull null_check,
                                 wasm::WasmCodePosition position) {
  gasm_->StructSet(struct_object, field_value, struct_type, field_index,
                   null_check);
  SetSourcePosition(effect(), position);
}

void WasmGraphBuilder::BoundsCheckArray(Node* array, Node* index,
                                        CheckForNull null_check,
                                        wasm::WasmCodePosition position) {
  if (V8_UNLIKELY(v8_flags.experimental_wasm_skip_bounds_checks)) {
    if (null_check == kWithNullCheck) {
      AssertNotNull(array, wasm::kWasmArrayRef, position);
    }
  } else {
    Node* length = gasm_->ArrayLength(array, null_check);
    SetSourcePosition(length, position);
    TrapIfFalse(wasm::kTrapArrayOutOfBounds,
                gasm_->Uint32LessThan(index, length), position);
  }
}

void WasmGraphBuilder::BoundsCheckArrayWithLength(
    Node* array, Node* index, Node* length, CheckForNull null_check,
    wasm::WasmCodePosition position) {
  if (V8_UNLIKELY(v8_flags.experimental_wasm_skip_bounds_checks)) return;
  Node* array_length = gasm_->ArrayLength(array, null_check);
  SetSourcePosition(array_length, position);
  Node* range_end = gasm_->Int32Add(index, length);
  Node* range_valid = gasm_->Word32And(
      // OOB if (index + length > array.len).
      gasm_->Uint32LessThanOrEqual(range_end, array_length),
      // OOB if (index + length) overflows.
      gasm_->Uint32LessThanOrEqual(index, range_end));
  TrapIfFalse(wasm::kTrapArrayOutOfBounds, range_valid, position);
}

Node* WasmGraphBuilder::ArrayGet(Node* array_object,
                                 const wasm::ArrayType* type, Node* index,
                                 CheckForNull null_check, bool is_signed,
                                 wasm::WasmCodePosition position) {
  BoundsCheckArray(array_object, index, null_check, position);
  return gasm_->ArrayGet(array_object, index, type, is_signed);
}

void WasmGraphBuilder::ArraySet(Node* array_object, const wasm::ArrayType* type,
                                Node* index, Node* value,
                                CheckForNull null_check,
                                wasm::WasmCodePosition position) {
  BoundsCheckArray(array_object, index, null_check, position);
  gasm_->ArraySet(array_object, index, value, type);
}

Node* WasmGraphBuilder::ArrayLen(Node* array_object, CheckForNull null_check,
                                 wasm::WasmCodePosition position) {
  Node* result = gasm_->ArrayLength(array_object, null_check);
  SetSourcePosition(result, position);
  return result;
}

void WasmGraphBuilder::ArrayCopy(Node* dst_array, Node* dst_index,
                                 CheckForNull dst_null_check, Node* src_array,
                                 Node* src_index, CheckForNull src_null_check,
                                 Node* length,
                                 const wasm::ArrayType* array_type,
                                 wasm::WasmCodePosition position) {
  BoundsCheckArrayWithLength(dst_array, dst_index, length, dst_null_check,
                             position);
  BoundsCheckArrayWithLength(src_array, src_index, length, src_null_check,
                             position);

  auto end = gasm_->MakeLabel();

  gasm_->GotoIf(gasm_->Word32Equal(length, Int32Constant(0)), &end);

  auto builtin = gasm_->MakeLabel();

  // Values determined by test/mjsunit/wasm/array-copy-benchmark.js on x64.
  int array_copy_max_loop_length;
  switch (array_type->element_type().kind()) {
    case wasm::kI32:
    case wasm::kI64:
    case wasm::kI8:
    case wasm::kI16:
      array_copy_max_loop_length = 20;
      break;
    case wasm::kF16:
    case wasm::kF32:
    case wasm::kF64:
      array_copy_max_loop_length = 35;
      break;
    case wasm::kS128:
      array_copy_max_loop_length = 100;
      break;
    case wasm::kRtt:
    case wasm::kRef:
    case wasm::kRefNull:
      array_copy_max_loop_length = 15;
      break;
    case wasm::kVoid:
    case wasm::kBottom:
      UNREACHABLE();
  }

  gasm_->GotoIf(
      gasm_->Uint32LessThan(Int32Constant(array_copy_max_loop_length), length),
      &builtin);

  auto reverse = gasm_->MakeLabel();

  gasm_->GotoIf(gasm_->Uint32LessThan(src_index, dst_index), &reverse);

  Node* src_end_index = gasm_->Int32Sub(gasm_->Int32Add(src_index, length),
                                        gasm_->Int32Constant(1));
  Node* dst_end_index = gasm_->Int32Sub(gasm_->Int32Add(dst_index, length),
                                        gasm_->Int32Constant(1));

  {
    auto loop = gasm_->MakeLoopLabel(MachineRepresentation::kWord32,
                                     MachineRepresentation::kWord32);

    gasm_->Goto(&loop, src_index, dst_index);
    gasm_->Bind(&loop);

    Node* value = gasm_->ArrayGet(src_array, loop.PhiAt(0), array_type, false);
    gasm_->ArraySet(dst_array, loop.PhiAt(1), value, array_type);

    Node* condition = gasm_->Uint32LessThan(loop.PhiAt(0), src_end_index);
    gasm_->GotoIfNot(condition, &end);
    gasm_->Goto(&loop, gasm_->Int32Add(loop.PhiAt(0), Int32Constant(1)),
                gasm_->Int32Add(loop.PhiAt(1), Int32Constant(1)));
  }

  {
    gasm_->Bind(&reverse);
    auto loop = gasm_->MakeLoopLabel(MachineRepresentation::kWord32,
                                     MachineRepresentation::kWord32);

    gasm_->Goto(&loop, src_end_index, dst_end_index);
    gasm_->Bind(&loop);

    Node* value = gasm_->ArrayGet(src_array, loop.PhiAt(0), array_type, false);
    gasm_->ArraySet(dst_array, loop.PhiAt(1), value, array_type);

    Node* condition = gasm_->Uint32LessThan(src_index, loop.PhiAt(0));
    gasm_->GotoIfNot(condition, &end);
    gasm_->Goto(&loop, gasm_->Int32Sub(loop.PhiAt(0), Int32Constant(1)),
                gasm_->Int32Sub(loop.PhiAt(1), Int32Constant(1)));
  }

  {
    gasm_->Bind(&builtin);
    Node* function =
        gasm_->ExternalConstant(ExternalReference::wasm_array_copy());
    MachineType arg_types[]{MachineType::TaggedPointer(), MachineType::Uint32(),
                            MachineType::TaggedPointer(), MachineType::Uint32(),
                            MachineType::Uint32()};
    MachineSignature sig(0, 5, arg_types);
    BuildCCall(&sig, function, dst_array, dst_index, src_array, src_index,
               length);
    gasm_->Goto(&end);
  }

  gasm_->Bind(&end);
}

Node* WasmGraphBuilder::StoreInInt64StackSlot(Node* value,
                                              wasm::ValueType type) {
  Node* value_int64;
  switch (type.kind()) {
    case wasm::kI32:
    case wasm::kI8:
    case wasm::kI16:
      value_int64 =
          graph()->NewNode(mcgraph()->machine()->ChangeInt32ToInt64(), value);
      break;
    case wasm::kI64:
      value_int64 = value;
      break;
    case wasm::kS128:
      // We can only get here if {value} is the constant 0.
      DCHECK_EQ(value->opcode(), IrOpcode::kS128Zero);
      value_int64 = Int64Constant(0);
      break;
    case wasm::kF32:
      value_int64 = graph()->NewNode(
          mcgraph()->machine()->ChangeInt32ToInt64(),
          graph()->NewNode(mcgraph()->machine()->BitcastFloat32ToInt32(),
                           value));
      break;
    case wasm::kF64:
      value_int64 = graph()->NewNode(
          mcgraph()->machine()->BitcastFloat64ToInt64(), value);
      break;
    case wasm::kRefNull:
    case wasm::kRef:
      value_int64 = kSystemPointerSize == 4
                        ? graph()->NewNode(
                              mcgraph()->machine()->ChangeInt32ToInt64(), value)
                        : value;
      break;
    case wasm::kF16:
      UNIMPLEMENTED();
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kBottom:
      UNREACHABLE();
  }

  return StoreArgsInStackSlot({{MachineRepresentation::kWord64, value_int64}});
}

void WasmGraphBuilder::ArrayFill(Node* array, Node* index, Node* value,
                                 Node* length, const wasm::ArrayType* type,
                                 CheckForNull null_check,
                                 wasm::WasmCodePosition position) {
  BoundsCheckArrayWithLength(array, index, length, null_check, position);
  ArrayFillImpl(array, index, value, length, type,
                type->element_type().is_reference());
}

void WasmGraphBuilder::ArrayFillImpl(Node* array, Node* index, Node* value,
                                     Node* length, const wasm::ArrayType* type,
                                     bool emit_write_barrier) {
  DCHECK_NOT_NULL(value);
  wasm::ValueType element_type = type->element_type();

  // Initialize the array. Use an external function for large arrays with
  // null/number initializer. Use a loop for small arrays and reference arrays
  // with a non-null initial value.
  auto done = gasm_->MakeLabel();
  // TODO(manoskouk): If the loop is ever removed here, we have to update
  // ArrayNew(), ArrayNewDefault(), and ArrayFill() in
  // graph-builder-interface.cc to not mark the current loop as non-innermost.
  auto loop = gasm_->MakeLoopLabel(MachineRepresentation::kWord32);

  // The builtin cannot handle s128 values other than 0.
  if (!(element_type == wasm::kWasmS128 &&
        value->opcode() != IrOpcode::kS128Zero)) {
    constexpr uint32_t kArrayNewMinimumSizeForMemSet = 16;
    gasm_->GotoIf(gasm_->Uint32LessThan(
                      length, Int32Constant(kArrayNewMinimumSizeForMemSet)),
                  &loop, BranchHint::kNone, index);
    Node* function =
        gasm_->ExternalConstant(ExternalReference::wasm_array_fill());

    Node* stack_slot = StoreInInt64StackSlot(value, element_type);

    MachineType arg_types[]{
        MachineType::TaggedPointer(), MachineType::Uint32(),
        MachineType::Uint32(),        MachineType::Uint32(),
        MachineType::Uint32(),        MachineType::Pointer()};
    MachineSignature sig(0, 6, arg_types);
    BuildCCall(&sig, function, array, index, length,
               Int32Constant(emit_write_barrier ? 1 : 0),
               Int32Constant(element_type.raw_bit_field()), stack_slot);
    gasm_->Goto(&done);
  } else {
    gasm_->Goto(&loop, index);
  }
  gasm_->Bind(&loop);
  {
    Node* current_index = loop.PhiAt(0);
    Node* check =
        gasm_->UintLessThan(current_index, gasm_->Int32Add(index, length));
    gasm_->GotoIfNot(check, &done);
    gasm_->ArraySet(array, current_index, value, type);
    current_index = gasm_->Int32Add(current_index, Int32Constant(1));
    gasm_->Goto(&loop, current_index);
  }
  gasm_->Bind(&done);
}

// General rules for operator properties for builtin calls:
// - Use kEliminatable if it can neither throw a catchable exception nor trap.
// - Use kNoDeopt | kNoThrow if it can trap (because in that case, eliminating
//   it would avoid the trap and thereby observably change the code's behavior
//   compared to its unoptimized version).
// - If you don't use kNoThrow (nor kEliminatable which implies it), then you
//   must also set up control nodes for the throwing case, e.g. by using
//   WasmGraphBuildingInterface::CheckForException().

Node* WasmGraphBuilder::StringNewWtf8(const wasm::WasmMemory* memory,
                                      unibrow::Utf8Variant variant,
                                      Node* offset, Node* size,
                                      wasm::WasmCodePosition position) {
  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&offset}, position);
  return gasm_->CallBuiltin(Builtin::kWasmStringNewWtf8,
                            Operator::kNoDeopt | Operator::kNoThrow, offset,
                            size, gasm_->Int32Constant(memory->index),
                            gasm_->SmiConstant(static_cast<int32_t>(variant)));
}

Node* WasmGraphBuilder::StringNewWtf8Array(unibrow::Utf8Variant variant,
                                           Node* array, CheckForNull null_check,
                                           Node* start, Node* end,
                                           wasm::WasmCodePosition position) {
  // Special case: shortcut a sequence "array from data segment" + "string from
  // wtf8 array" to directly create a string from the segment.
  if (IsArrayNewSegment(array)) {
    // We can only pass 3 untagged parameters to the builtin (on 32-bit
    // platforms). The segment index is easy to tag: if it validated, it must
    // be in Smi range.
    Node* segment_index = NodeProperties::GetValueInput(array, 1);
    Uint32Matcher index_matcher(segment_index);
    DCHECK(index_matcher.HasResolvedValue());
    Node* segment_index_smi = gasm_->SmiConstant(index_matcher.ResolvedValue());
    // Arbitrary choice for the second tagged parameter: the segment offset.
    Node* segment_offset = NodeProperties::GetValueInput(array, 2);
    TrapIfFalse(wasm::kTrapDataSegmentOutOfBounds,
                gasm_->Uint32LessThan(segment_offset,
                                      gasm_->Uint32Constant(Smi::kMaxValue)),
                position);
    Node* segment_offset_smi = gasm_->BuildChangeInt32ToSmi(segment_offset);
    Node* segment_length = NodeProperties::GetValueInput(array, 3);
    Node* variant_smi = gasm_->SmiConstant(static_cast<int32_t>(variant));
    return gasm_->CallBuiltin(Builtin::kWasmStringFromDataSegment,
                              Operator::Operator::kNoDeopt | Operator::kNoThrow,
                              segment_length, start, end, segment_index_smi,
                              segment_offset_smi, variant_smi);
  }

  // Regular path if the shortcut wasn't taken.
  if (null_check == kWithNullCheck) {
    array = AssertNotNull(array, wasm::kWasmArrayRef, position);
  }
  return gasm_->CallBuiltin(
      Builtin::kWasmStringNewWtf8Array, Operator::kNoDeopt | Operator::kNoThrow,
      start, end, array, gasm_->SmiConstant(static_cast<int32_t>(variant)));
}

Node* WasmGraphBuilder::StringNewWtf16(const wasm::WasmMemory* memory,
                                       Node* offset, Node* size,
                                       wasm::WasmCodePosition position) {
  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&offset}, position);
  return gasm_->CallBuiltin(Builtin::kWasmStringNewWtf16,
                            Operator::kNoDeopt | Operator::kNoThrow,
                            gasm_->Uint32Constant(memory->index), offset, size);
}

Node* WasmGraphBuilder::StringNewWtf16Array(Node* array,
                                            CheckForNull null_check,
                                            Node* start, Node* end,
                                            wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    array = AssertNotNull(array, wasm::kWasmArrayRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringNewWtf16Array,
                            Operator::kNoDeopt | Operator::kNoThrow, array,
                            start, end);
}

Node* WasmGraphBuilder::StringConst(uint32_t index) {
  return gasm_->CallBuiltin(Builtin::kWasmStringConst,
                            Operator::kNoDeopt | Operator::kNoThrow,
                            gasm_->Uint32Constant(index));
}

Node* WasmGraphBuilder::StringMeasureUtf8(Node* string, CheckForNull null_check,
                                          wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringMeasureUtf8,
                            Operator::kEliminatable, string);
}

Node* WasmGraphBuilder::StringMeasureWtf8(Node* string, CheckForNull null_check,
                                          wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringMeasureWtf8,
                            Operator::kEliminatable, string);
}

Node* WasmGraphBuilder::StringMeasureWtf16(Node* string,
                                           CheckForNull null_check,
                                           wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  return gasm_->LoadStringLength(string);
}

Node* WasmGraphBuilder::StringEncodeWtf8(const wasm::WasmMemory* memory,
                                         unibrow::Utf8Variant variant,
                                         Node* string, CheckForNull null_check,
                                         Node* offset,
                                         wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&offset}, position);
  return gasm_->CallBuiltin(
      Builtin::kWasmStringEncodeWtf8, Operator::kNoDeopt | Operator::kNoThrow,
      offset, gasm_->Int32Constant(memory->index),
      gasm_->Int32Constant(static_cast<int32_t>(variant)), string);
}

Node* WasmGraphBuilder::StringEncodeWtf8Array(
    unibrow::Utf8Variant variant, Node* string, CheckForNull string_null_check,
    Node* array, CheckForNull array_null_check, Node* start,
    wasm::WasmCodePosition position) {
  if (string_null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  if (array_null_check == kWithNullCheck) {
    array = AssertNotNull(array, wasm::kWasmArrayRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringEncodeWtf8Array,
                            Operator::kNoDeopt | Operator::kNoThrow, string,
                            array, start,
                            gasm_->SmiConstant(static_cast<int32_t>(variant)));
}

Node* WasmGraphBuilder::StringToUtf8Array(Node* string, CheckForNull null_check,
                                          wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringToUtf8Array,
                            Operator::kNoDeopt | Operator::kNoThrow, string);
}

Node* WasmGraphBuilder::StringEncodeWtf16(const wasm::WasmMemory* memory,
                                          Node* string, CheckForNull null_check,
                                          Node* offset,
                                          wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&offset}, position);
  return gasm_->CallBuiltin(Builtin::kWasmStringEncodeWtf16,
                            Operator::kNoDeopt | Operator::kNoThrow, string,
                            offset, gasm_->Int32Constant(memory->index));
}

Node* WasmGraphBuilder::StringAsWtf16(Node* string, CheckForNull null_check,
                                      wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  return gasm_->StringAsWtf16(string);
}

Node* WasmGraphBuilder::StringEncodeWtf16Array(
    Node* string, CheckForNull string_null_check, Node* array,
    CheckForNull array_null_check, Node* start,
    wasm::WasmCodePosition position) {
  if (string_null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  if (array_null_check == kWithNullCheck) {
    array = AssertNotNull(array, wasm::kWasmArrayRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringEncodeWtf16Array,
                            Operator::kNoDeopt | Operator::kNoThrow, string,
                            array, start);
}

Node* WasmGraphBuilder::StringConcat(Node* head, CheckForNull head_null_check,
                                     Node* tail, CheckForNull tail_null_check,
                                     wasm::WasmCodePosition position) {
  if (head_null_check == kWithNullCheck) {
    head = AssertNotNull(head, wasm::kWasmStringRef, position);
  }
  if (tail_null_check == kWithNullCheck) {
    tail = AssertNotNull(tail, wasm::kWasmStringRef, position);
  }
  return gasm_->CallBuiltin(
      Builtin::kStringAdd_CheckNone, Operator::kNoDeopt | Operator::kNoThrow,
      head, tail,
      LOAD_INSTANCE_FIELD(NativeContext, MachineType::TaggedPointer()));
}

Node* WasmGraphBuilder::StringEqual(Node* a, wasm::ValueType a_type, Node* b,
                                    wasm::ValueType b_type,
                                    wasm::WasmCodePosition position) {
  auto done = gasm_->MakeLabel(MachineRepresentation::kWord32);
  // Covers "identical string pointer" and "both are null" cases.
  gasm_->GotoIf(gasm_->TaggedEqual(a, b), &done, Int32Constant(1));
  if (a_type.is_nullable()) {
    gasm_->GotoIf(gasm_->IsNull(a, a_type), &done, Int32Constant(0));
  }
  if (b_type.is_nullable()) {
    gasm_->GotoIf(gasm_->IsNull(b, b_type), &done, Int32Constant(0));
  }
  // TODO(jkummerow): Call Builtin::kStringEqual directly.
  gasm_->Goto(&done, gasm_->CallBuiltin(Builtin::kWasmStringEqual,
                                        Operator::kEliminatable, a, b));
  gasm_->Bind(&done);
  return done.PhiAt(0);
}

Node* WasmGraphBuilder::StringIsUSVSequence(Node* str, CheckForNull null_check,
                                            wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    str = AssertNotNull(str, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringIsUSVSequence,
                            Operator::kEliminatable, str);
}

Node* WasmGraphBuilder::StringAsWtf8(Node* str, CheckForNull null_check,
                                     wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    str = AssertNotNull(str, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringAsWtf8, Operator::kEliminatable,
                            str);
}

Node* WasmGraphBuilder::StringViewWtf8Advance(Node* view,
                                              CheckForNull null_check,
                                              Node* pos, Node* bytes,
                                              wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    view = AssertNotNull(view, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringViewWtf8Advance,
                            Operator::kEliminatable, view, pos, bytes);
}

void WasmGraphBuilder::StringViewWtf8Encode(
    const wasm::WasmMemory* memory, unibrow::Utf8Variant variant, Node* view,
    CheckForNull null_check, Node* addr, Node* pos, Node* bytes,
    Node** next_pos, Node** bytes_written, wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    view = AssertNotNull(view, wasm::kWasmStringRef, position);
  }
  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&addr}, position);
  Node* pair =
      gasm_->CallBuiltin(Builtin::kWasmStringViewWtf8Encode,
                         Operator::kNoDeopt | Operator::kNoThrow, addr, pos,
                         bytes, view, gasm_->SmiConstant(memory->index),
                         gasm_->SmiConstant(static_cast<int32_t>(variant)));
  *next_pos = gasm_->Projection(0, pair);
  *bytes_written = gasm_->Projection(1, pair);
}

Node* WasmGraphBuilder::StringViewWtf8Slice(Node* view, CheckForNull null_check,
                                            Node* pos, Node* bytes,
                                            wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    view = AssertNotNull(view, wasm::kWasmStringRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringViewWtf8Slice,
                            Operator::kEliminatable, view, pos, bytes);
}

Node* WasmGraphBuilder::StringViewWtf16GetCodeUnit(
    Node* string, CheckForNull null_check, Node* offset,
    wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  Node* prepare = gasm_->StringPrepareForGetCodeunit(string);
  Node* base = gasm_->Projection(0, prepare);
  Node* base_offset = gasm_->Projection(1, prepare);
  Node* charwidth_shift = gasm_->Projection(2, prepare);

  // Bounds check.
  Node* length = gasm_->LoadStringLength(string);
  TrapIfFalse(wasm::kTrapStringOffsetOutOfBounds,
              gasm_->Uint32LessThan(offset, length), position);

  auto onebyte = gasm_->MakeLabel();
  auto bailout = gasm_->MakeDeferredLabel();
  auto done = gasm_->MakeLabel(MachineRepresentation::kWord32);
  gasm_->GotoIf(
      gasm_->Word32Equal(charwidth_shift,
                         gasm_->Int32Constant(kCharWidthBailoutSentinel)),
      &bailout);
  gasm_->GotoIf(gasm_->Word32Equal(charwidth_shift, gasm_->Int32Constant(0)),
                &onebyte);

  // Two-byte.
  Node* object_offset =
      gasm_->IntAdd(gasm_->IntMul(gasm_->BuildChangeInt32ToIntPtr(offset),
                                  gasm_->IntPtrConstant(2)),
                    base_offset);
  Node* result = gasm_->LoadImmutableFromObject(MachineType::Uint16(), base,
                                                object_offset);
  gasm_->Goto(&done, result);

  // One-byte.
  gasm_->Bind(&onebyte);
  object_offset =
      gasm_->IntAdd(gasm_->BuildChangeInt32ToIntPtr(offset), base_offset);
  result =
      gasm_->LoadImmutableFromObject(MachineType::Uint8(), base, object_offset);
  gasm_->Goto(&done, result);

  gasm_->Bind(&bailout);
  gasm_->Goto(&done, gasm_->CallBuiltinThroughJumptable(
                         Builtin::kWasmStringViewWtf16GetCodeUnit,
                         Operator::kEliminatable, string, offset));

  gasm_->Bind(&done);
  // Make sure the original string is kept alive as long as we're operating
  // on pointers extracted from it (otherwise e.g. external strings' resources
  // might get freed prematurely).
  gasm_->Retain(string);
  return done.PhiAt(0);
}

Node* WasmGraphBuilder::StringCodePointAt(Node* string, CheckForNull null_check,
                                          Node* offset,
                                          wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  Node* prepare = gasm_->StringPrepareForGetCodeunit(string);
  Node* base = gasm_->Projection(0, prepare);
  Node* base_offset = gasm_->Projection(1, prepare);
  Node* charwidth_shift = gasm_->Projection(2, prepare);

  // Bounds check.
  Node* length = gasm_->LoadStringLength(string);
  TrapIfFalse(wasm::kTrapStringOffsetOutOfBounds,
              gasm_->Uint32LessThan(offset, length), position);

  auto onebyte = gasm_->MakeLabel();
  auto bailout = gasm_->MakeDeferredLabel();
  auto done = gasm_->MakeLabel(MachineRepresentation::kWord32);
  gasm_->GotoIf(
      gasm_->Word32Equal(charwidth_shift,
                         gasm_->Int32Constant(kCharWidthBailoutSentinel)),
      &bailout);
  gasm_->GotoIf(gasm_->Word32Equal(charwidth_shift, gasm_->Int32Constant(0)),
                &onebyte);

  // Two-byte.
  Node* object_offset =
      gasm_->IntAdd(gasm_->IntMul(gasm_->BuildChangeInt32ToIntPtr(offset),
                                  gasm_->IntPtrConstant(2)),
                    base_offset);
  Node* lead = gasm_->LoadImmutableFromObject(MachineType::Uint16(), base,
                                              object_offset);
  Node* is_lead_surrogate =
      gasm_->Word32Equal(gasm_->Word32And(lead, gasm_->Int32Constant(0xFC00)),
                         gasm_->Int32Constant(0xD800));
  gasm_->GotoIfNot(is_lead_surrogate, &done, lead);
  Node* trail_offset = gasm_->Int32Add(offset, gasm_->Int32Constant(1));
  gasm_->GotoIfNot(gasm_->Uint32LessThan(trail_offset, length), &done, lead);
  Node* trail = gasm_->LoadImmutableFromObject(
      MachineType::Uint16(), base,
      gasm_->IntAdd(object_offset, gasm_->IntPtrConstant(2)));
  Node* is_trail_surrogate =
      gasm_->WordEqual(gasm_->Word32And(trail, gasm_->Int32Constant(0xFC00)),
                       gasm_->Int32Constant(0xDC00));
  gasm_->GotoIfNot(is_trail_surrogate, &done, lead);
  Node* surrogate_bias =
      gasm_->Int32Constant(0x10000 - (0xD800 << 10) - 0xDC00);
  Node* result =
      gasm_->Int32Add(gasm_->Word32Shl(lead, gasm_->Int32Constant(10)),
                      gasm_->Int32Add(trail, surrogate_bias));
  gasm_->Goto(&done, result);

  // One-byte.
  gasm_->Bind(&onebyte);
  object_offset =
      gasm_->IntAdd(gasm_->BuildChangeInt32ToIntPtr(offset), base_offset);
  result =
      gasm_->LoadImmutableFromObject(MachineType::Uint8(), base, object_offset);
  gasm_->Goto(&done, result);

  gasm_->Bind(&bailout);
  gasm_->Goto(&done, gasm_->CallBuiltinThroughJumptable(
                         Builtin::kWasmStringCodePointAt,
                         Operator::kEliminatable, string, offset));

  gasm_->Bind(&done);
  // Make sure the original string is kept alive as long as we're operating
  // on pointers extracted from it (otherwise e.g. external strings' resources
  // might get freed prematurely).
  gasm_->Retain(string);
  return done.PhiAt(0);
}

Node* WasmGraphBuilder::StringViewWtf16Encode(const wasm::WasmMemory* memory,
                                              Node* string,
                                              CheckForNull null_check,
                                              Node* offset, Node* start,
                                              Node* codeunits,
                                              wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  MemTypeToUintPtrOrOOBTrap(memory->is_memory64, {&offset}, position);
  return gasm_->CallBuiltin(Builtin::kWasmStringViewWtf16Encode,
                            Operator::kNoDeopt | Operator::kNoThrow, offset,
                            start, codeunits, string,
                            gasm_->SmiConstant(memory->index));
}

Node* WasmGraphBuilder::StringViewWtf16Slice(Node* string,
                                             CheckForNull null_check,
                                             Node* start, Node* end,
                                             wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }
  return gasm_->CallBuiltin(Builtin::kWasmStringViewWtf16Slice,
                            Operator::kEliminatable, string, start, end);
}

Node* WasmGraphBuilder::StringAsIter(Node* str, CheckForNull null_check,
                                     wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    str = AssertNotNull(str, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringAsIter, Operator::kEliminatable,
                            str);
}

Node* WasmGraphBuilder::StringViewIterNext(Node* view, CheckForNull null_check,
                                           wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    view = AssertNotNull(view, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringViewIterNext,
                            Operator::kEliminatable, view);
}

Node* WasmGraphBuilder::StringViewIterAdvance(Node* view,
                                              CheckForNull null_check,
                                              Node* codepoints,
                                              wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    view = AssertNotNull(view, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringViewIterAdvance,
                            Operator::kEliminatable, view, codepoints);
}

Node* WasmGraphBuilder::StringViewIterRewind(Node* view,
                                             CheckForNull null_check,
                                             Node* codepoints,
                                             wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    view = AssertNotNull(view, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringViewIterRewind,
                            Operator::kEliminatable, view, codepoints);
}

Node* WasmGraphBuilder::StringViewIterSlice(Node* view, CheckForNull null_check,
                                            Node* codepoints,
                                            wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    view = AssertNotNull(view, wasm::kWasmStringRef, position);
  }

  return gasm_->CallBuiltin(Builtin::kWasmStringViewIterSlice,
                            Operator::kEliminatable, view, codepoints);
}

Node* WasmGraphBuilder::StringCompare(Node* lhs, CheckForNull null_check_lhs,
                                      Node* rhs, CheckForNull null_check_rhs,
                                      wasm::WasmCodePosition position) {
  if (null_check_lhs == kWithNullCheck) {
    lhs = AssertNotNull(lhs, wasm::kWasmStringRef, position);
  }
  if (null_check_rhs == kWithNullCheck) {
    rhs = AssertNotNull(rhs, wasm::kWasmStringRef, position);
  }
  return gasm_->BuildChangeSmiToInt32(gasm_->CallBuiltin(
      Builtin::kStringCompare, Operator::kEliminatable, lhs, rhs));
}

Node* WasmGraphBuilder::StringFromCharCode(Node* char_code) {
  Node* capped = gasm_->Word32And(char_code, gasm_->Uint32Constant(0xFFFF));
  return gasm_->CallBuiltin(Builtin::kWasmStringFromCodePoint,
                            Operator::kEliminatable, capped);
}

Node* WasmGraphBuilder::StringFromCodePoint(Node* code_point) {
  // TODO(jkummerow): Refactor the code in
  // EffectControlLinearizer::LowerStringFromSingleCodePoint to make it
  // accessible for Wasm.
  // This call traps when the {code_point} is invalid.
  return gasm_->CallBuiltin(Builtin::kWasmStringFromCodePoint,
                            Operator::kNoDeopt | Operator::kNoWrite,
                            code_point);
}

Node* WasmGraphBuilder::StringHash(Node* string, CheckForNull null_check,
                                   wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    string = AssertNotNull(string, wasm::kWasmStringRef, position);
  }

  auto runtime_label = gasm_->MakeLabel();
  auto end_label = gasm_->MakeLabel(MachineRepresentation::kWord32);

  Node* raw_hash = gasm_->LoadFromObject(
      MachineType::Int32(), string,
      wasm::ObjectAccess::ToTagged(offsetof(Name, raw_hash_field_)));
  Node* hash_not_computed_mask =
      gasm_->Int32Constant(static_cast<int32_t>(Name::kHashNotComputedMask));
  static_assert(Name::HashFieldTypeBits::kShift == 0);
  Node* hash_not_computed = gasm_->Word32And(raw_hash, hash_not_computed_mask);
  gasm_->GotoIf(hash_not_computed, &runtime_label);

  // Fast path if hash is already computed: Decode raw hash value.
  static_assert(Name::HashBits::kLastUsedBit == kBitsPerInt - 1);
  Node* hash = gasm_->Word32Shr(
      raw_hash,
      gasm_->Int32Constant(static_cast<int32_t>(Name::HashBits::kShift)));
  gasm_->Goto(&end_label, hash);

  gasm_->Bind(&runtime_label);
  Node* hash_runtime = gasm_->CallBuiltin(Builtin::kWasmStringHash,
                                          Operator::kEliminatable, string);
  gasm_->Goto(&end_label, hash_runtime);

  gasm_->Bind(&end_label);
  return end_label.PhiAt(0);
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

Node* WasmGraphBuilder::WellKnown_StringIndexOf(
    Node* string, Node* search, Node* start, CheckForNull string_null_check,
    CheckForNull search_null_check) {
  if (string_null_check == kWithNullCheck) {
    // If string is null, throw.
    auto if_not_null = gasm_->MakeLabel();
    auto if_null = gasm_->MakeDeferredLabel();
    gasm_->GotoIf(IsNull(string, wasm::kWasmStringRef), &if_null);
    gasm_->Goto(&if_not_null);
    gasm_->Bind(&if_null);
    gasm_->CallBuiltin(Builtin::kThrowIndexOfCalledOnNull, Operator::kNoWrite);
    gasm_->Unreachable();
    gasm_->Bind(&if_not_null);
  }
  if (search_null_check == kWithNullCheck) {
    // If search is null, replace it with "null".
    auto search_not_null =
        gasm_->MakeLabel(MachineRepresentation::kTaggedPointer);
    gasm_->GotoIfNot(IsNull(search, wasm::kWasmStringRef), &search_not_null,
                     search);
    Node* null_string = LOAD_ROOT(null_string, null_string);
    gasm_->Goto(&search_not_null, null_string);
    gasm_->Bind(&search_not_null);
    search = search_not_null.PhiAt(0);
  }
  {
    // Clamp the start index.
    auto clamped_start = gasm_->MakeLabel(MachineRepresentation::kWord32);
    gasm_->GotoIf(gasm_->Int32LessThan(start, Int32Constant(0)), &clamped_start,
                  Int32Constant(0));
    Node* length = gasm_->LoadStringLength(string);
    gasm_->GotoIf(gasm_->Int32LessThan(start, length), &clamped_start, start);
    gasm_->Goto(&clamped_start, length);
    gasm_->Bind(&clamped_start);
    start = clamped_start.PhiAt(0);
  }

  BuildModifyThreadInWasmFlag(false);
  // This can't overflow because we've clamped {start} above.
  Node* start_smi = gasm_->BuildChangeInt32ToSmi(start);
  Node* result =
      gasm_->CallBuiltin(Builtin::kStringIndexOf, Operator::kEliminatable,
                         string, search, start_smi);
  BuildModifyThreadInWasmFlag(true);
  return gasm_->BuildChangeSmiToInt32(result);
}

Node* WasmGraphBuilder::WellKnown_StringToLocaleLowerCaseStringref(
    int func_index, Node* string, Node* locale,
    CheckForNull string_null_check) {
#if V8_INTL_SUPPORT
  if (string_null_check == kWithNullCheck) {
    // We can let the builtin throw the exception, but it only checks for
    // JS null, so we must externalize any Wasm null here.
    // Externalizing the {locale} is not required, because
    // {Object::ConvertToString} has been taught how to deal with WasmNull.
    string = gasm_->WasmExternConvertAny(string);
  }
  int param_count = 2;  // String, locale.
  CallDescriptor* call_descriptor = Linkage::GetJSCallDescriptor(
      zone_, false, param_count, CallDescriptor::kCanUseRoots,
      Operator::kNoDeopt | Operator::kNoWrite);
  Node* callees_array =
      LOAD_INSTANCE_FIELD(WellKnownImports, MachineType::TaggedPointer());
  Node* callee = gasm_->LoadFixedArrayElementPtr(callees_array, func_index);
  Node* context = gasm_->LoadContextFromJSFunction(callee);
  BuildModifyThreadInWasmFlag(false);
  Node* result = gasm_->Call(call_descriptor, callee, string, locale,
                             UndefinedValue(),                   // new.target
                             gasm_->Int32Constant(param_count),  // argc
                             context);                           // context
  BuildModifyThreadInWasmFlag(true);
  return result;
#else
  UNREACHABLE();
#endif
}

Node* WasmGraphBuilder::WellKnown_StringToLowerCaseStringref(
    Node* string, CheckForNull null_check) {
#if V8_INTL_SUPPORT
  BuildModifyThreadInWasmFlag(false);
  if (null_check == kWithNullCheck) {
    auto if_not_null = gasm_->MakeLabel();
    auto if_null = gasm_->MakeDeferredLabel();
    gasm_->GotoIf(IsNull(string, wasm::kWasmStringRef), &if_null);
    gasm_->Goto(&if_not_null);
    gasm_->Bind(&if_null);
    gasm_->CallBuiltin(Builtin::kThrowToLowerCaseCalledOnNull,
                       Operator::kNoWrite);
    gasm_->Unreachable();
    gasm_->Bind(&if_not_null);
  }
  Node* result =
      gasm_->CallBuiltin(Builtin::kStringToLowerCaseIntl,
                         Operator::kEliminatable, string, NoContextConstant());
  BuildModifyThreadInWasmFlag(true);
  return result;
#else
  UNREACHABLE();
#endif
}

Node* WasmGraphBuilder::WellKnown_ParseFloat(Node* string,
                                             CheckForNull null_check) {
  if (null_check == kWithNullCheck) {
    auto done = gasm_->MakeLabel(MachineRepresentation::kFloat64);
    auto if_null = gasm_->MakeDeferredLabel();
    gasm_->GotoIf(IsNull(string, wasm::kWasmStringRef), &if_null);
    BuildModifyThreadInWasmFlag(false);
    Node* result = gasm_->CallBuiltin(Builtin::kWasmStringToDouble,
                                      Operator::kEliminatable, string);
    BuildModifyThreadInWasmFlag(true);
    gasm_->Goto(&done, result);
    gasm_->Bind(&if_null);
    gasm_->Goto(&done,
                Float64Constant(std::numeric_limits<double>::quiet_NaN()));
    gasm_->Bind(&done);
    return done.PhiAt(0);
  } else {
    BuildModifyThreadInWasmFlag(false);
    Node* result = gasm_->CallBuiltin(Builtin::kWasmStringToDouble,
                                      Operator::kEliminatable, string);
    BuildModifyThreadInWasmFlag(true);
    return result;
  }
}

Node* WasmGraphBuilder::WellKnown_DoubleToString(Node* n) {
  BuildModifyThreadInWasmFlag(false);
  Node* result = gasm_->CallBuiltin(Builtin::kWasmFloat64ToString,
                                    Operator::kEliminatable, n);
  BuildModifyThreadInWasmFlag(true);
  return result;
}

Node* WasmGraphBuilder::WellKnown_IntToString(Node* n, Node* radix) {
  BuildModifyThreadInWasmFlag(false);
  Node* result = gasm_->CallBuiltin(Builtin::kWasmIntToString,
                                    Operator::kNoDeopt, n, radix);
  BuildModifyThreadInWasmFlag(true);
  return result;
}

Node* WasmGraphBuilder::RefI31(Node* input) {
  if constexpr (SmiValuesAre31Bits()) {
    return gasm_->Word32Shl(input, gasm_->BuildSmiShiftBitsConstant32());
  } else {
    DCHECK(SmiValuesAre32Bits());
    // Set the topmost bit to sign-extend the second bit. This way,
    // interpretation in JS (if this value escapes there) will be the same as
    // i31.get_s.
    input = gasm_->BuildChangeInt32ToIntPtr(input);
    return gasm_->WordSar(
        gasm_->WordShl(input,
                       gasm_->IntPtrConstant(kSmiShiftSize + kSmiTagSize + 1)),
        gasm_->IntPtrConstant(1));
  }
}

Node* WasmGraphBuilder::I31GetS(Node* input, CheckForNull null_check,
                                wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    input = AssertNotNull(input, wasm::kWasmI31Ref, position);
  }
  if constexpr (SmiValuesAre31Bits()) {
    input = gasm_->BuildTruncateIntPtrToInt32(input);
    return gasm_->Word32SarShiftOutZeros(input,
                                         gasm_->BuildSmiShiftBitsConstant32());
  } else {
    DCHECK(SmiValuesAre32Bits());
    // Topmost bit is already sign-extended.
    return gasm_->BuildTruncateIntPtrToInt32(gasm_->WordSar(
        input, gasm_->IntPtrConstant(kSmiShiftSize + kSmiTagSize)));
  }
}

Node* WasmGraphBuilder::I31GetU(Node* input, CheckForNull null_check,
                                wasm::WasmCodePosition position) {
  if (null_check == kWithNullCheck) {
    input = AssertNotNull(input, wasm::kWasmI31Ref, position);
  }
  if constexpr (SmiValuesAre31Bits()) {
    input = gasm_->BuildTruncateIntPtrToInt32(input);
    return gasm_->Word32Shr(input, gasm_->BuildSmiShiftBitsConstant32());
  } else {
    DCHECK(SmiValuesAre32Bits());
    // We need to remove the topmost bit of the 32-bit Smi.
    return gasm_->BuildTruncateIntPtrToInt32(
        gasm_->WordShr(gasm_->WordShl(input, gasm_->IntPtrConstant(1)),
                       gasm_->IntPtrConstant(kSmiShiftSize + kSmiTagSize + 1)));
  }
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

void WasmGraphBuilder::AddBytecodePositionDecorator(
    NodeOriginTable* node_origins, wasm::Decoder* decoder) {
  DCHECK_NULL(decorator_);
  decorator_ = graph()->zone()->New<WasmDecorator>(node_origins, decoder);
  graph()->AddDecorator(decorator_);
}

void WasmGraphBuilder::RemoveBytecodePositionDecorator() {
  DCHECK_NOT_NULL(decorator_);
  graph()->RemoveDecorator(decorator_);
  decorator_ = nullptr;
}

namespace {

// A non-null {isolate} signifies that the generated code is treated as being in
// a JS frame for functions like BuildLoadIsolateRoot().
class WasmWrapperGraphBuilder : public WasmGraphBuilder {
 public:
  WasmWrapperGraphBuilder(Zone* zone, MachineGraph* mcgraph,
                          const wasm::FunctionSig* sig,
                          const wasm::WasmModule* module,
                          ParameterMode parameter_mode, Isolate* isolate,
                          compiler::SourcePositionTable* spt,
                          wasm::WasmEnabledFeatures features)
      : WasmGraphBuilder(nullptr, zone, mcgraph, sig, spt, parameter_mode,
                         isolate, features),
        module_(module) {}

  CallDescriptor* GetBigIntToI64CallDescriptor(bool needs_frame_state) {
    return wasm::GetWasmEngine()->call_descriptors()->GetBigIntToI64Descriptor(
        needs_frame_state);
  }

  Node* GetTargetForBuiltinCall(Builtin builtin) {
    // Per-process shared wrappers don't have access to a jump table, so they
    // can't use kCallWasmRuntimeStub mode.
    return gasm_->GetBuiltinPointerTarget(builtin);
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
                       const wasm::FunctionSig* sig, Node* context) {
    // Convert wasm numbers to JS values and drop the instance node.
    for (int i = 0; i < param_count; ++i) {
      Node* param = Param(i + 1);
      args[pos++] = ToJS(param, sig->GetParam(i), context);
    }
    return pos;
  }

  Node* ToJS(Node* node, wasm::ValueType type, Node* context) {
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
          case wasm::HeapType::kStringViewWtf8:
          case wasm::HeapType::kStringViewWtf16:
          case wasm::HeapType::kStringViewIter:
            UNREACHABLE();
          case wasm::HeapType::kFunc:
          default:
            if (type.heap_representation_non_shared() ==
                    wasm::HeapType::kFunc ||
                module_->has_signature(type.ref_index())) {
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
                module_->has_signature(type.ref_index())) {
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

  Node* BuildCheckString(Node* input, Node* js_context, wasm::ValueType type) {
    auto done = gasm_->MakeLabel(MachineRepresentation::kTagged);
    auto type_error = gasm_->MakeDeferredLabel();
    gasm_->GotoIf(IsSmi(input), &type_error, BranchHint::kFalse);
    if (type.is_nullable()) {
      auto not_null = gasm_->MakeLabel();
      gasm_->GotoIfNot(IsNull(input, wasm::kWasmExternRef), &not_null);
      gasm_->Goto(&done, LOAD_ROOT(WasmNull, wasm_null));
      gasm_->Bind(&not_null);
    }
    Node* map = gasm_->LoadMap(input);
    Node* instance_type = gasm_->LoadInstanceType(map);
    Node* check = gasm_->Uint32LessThan(
        instance_type, gasm_->Uint32Constant(FIRST_NONSTRING_TYPE));
    gasm_->GotoIf(check, &done, BranchHint::kTrue, input);
    gasm_->Goto(&type_error);
    gasm_->Bind(&type_error);
    BuildCallToRuntimeWithContext(Runtime::kWasmThrowJSTypeError, js_context,
                                  nullptr, 0);
    TerminateThrow(effect(), control());
    gasm_->Bind(&done);
    return done.PhiAt(0);
  }

  Node* FromJS(Node* input, Node* js_context, wasm::ValueType type,
               const wasm::WasmModule* module, Node* frame_state = nullptr) {
    switch (type.kind()) {
      case wasm::kRef:
      case wasm::kRefNull: {
        switch (type.heap_representation_non_shared()) {
          // TODO(14034): Add more fast paths?
          case wasm::HeapType::kExtern:
          case wasm::HeapType::kNoExtern:
          case wasm::HeapType::kExn:
          case wasm::HeapType::kNoExn:
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
            return BuildCheckString(input, js_context, type);
          case wasm::HeapType::kNone:
          case wasm::HeapType::kNoFunc:
          case wasm::HeapType::kI31:
          case wasm::HeapType::kAny:
          case wasm::HeapType::kFunc:
          case wasm::HeapType::kStruct:
          case wasm::HeapType::kArray:
          case wasm::HeapType::kEq:
          default: {
            // Make sure ValueType fits in a Smi.
            static_assert(wasm::ValueType::kLastUsedBit + 1 <= kSmiValueSize);

            uint32_t canonical_index = wasm::kInvalidCanonicalIndex;
            if (type.has_index()) {
              DCHECK_NOT_NULL(module);
              canonical_index =
                  module->isorecursive_canonical_type_ids[type.ref_index()];
              DCHECK_LE(canonical_index, kSmiMaxValue);
            }

            Node* inputs[] = {input,
                              mcgraph()->IntPtrConstant(IntToSmi(
                                  static_cast<int>(type.raw_bit_field()))),
                              mcgraph()->IntPtrConstant(
                                  IntToSmi(static_cast<int>(canonical_index)))};

            return BuildCallToRuntimeWithContext(Runtime::kWasmJSToWasmObject,
                                                 js_context, inputs, 3);
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

  Node* FromJSFast(Node* input, wasm::ValueType type) {
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

  Node* BuildMultiReturnFixedArrayFromIterable(const wasm::FunctionSig* sig,
                                               Node* iterable, Node* context) {
    Node* length = gasm_->BuildChangeUint31ToSmi(
        mcgraph()->Uint32Constant(static_cast<uint32_t>(sig->return_count())));
    return gasm_->CallBuiltin(Builtin::kIterableToFixedArrayForWasm,
                              Operator::kEliminatable, iterable, length,
                              context);
  }

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
    const int rets_count = static_cast<int>(sig_->return_count());
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
      Node* internal = gasm_->LoadProtectedPointerFromObject(
          function_data, wasm::ObjectAccess::ToTagged(
                             WasmFunctionData::kProtectedInternalOffset));
      args[0] =
          gasm_->LoadFromObject(MachineType::Pointer(), internal,
                                wasm::ObjectAccess::ToTagged(
                                    WasmInternalFunction::kCallTargetOffset));
      Node* implicit_arg = gasm_->LoadProtectedPointerFromObject(
          internal, wasm::ObjectAccess::ToTagged(
                        WasmInternalFunction::kProtectedImplicitArgOffset));
      BuildWasmCall(sig_, base::VectorOf(args), base::VectorOf(rets),
                    wasm::kNoCodePosition, implicit_arg, frame_state);
    }

    Node* jsval;
    if (sig_->return_count() == 0) {
      jsval = UndefinedValue();
    } else if (sig_->return_count() == 1) {
      jsval = !do_conversion ? rets[0]
                             : ToJS(rets[0], sig_->GetReturn(), js_context);
    } else {
      int32_t return_count = static_cast<int32_t>(sig_->return_count());
      Node* size = gasm_->NumberConstant(return_count);

      jsval = BuildCallAllocateJSArray(size, js_context);

      Node* fixed_array = gasm_->LoadJSArrayElements(jsval);

      for (int i = 0; i < return_count; ++i) {
        Node* value = ToJS(rets[i], sig_->GetReturn(i), js_context);
        gasm_->StoreFixedArrayElementAny(fixed_array, i, value);
      }
    }
    return jsval;
  }

  bool QualifiesForFastTransform(const wasm::FunctionSig*) {
    const int wasm_count = static_cast<int>(sig_->parameter_count());
    for (int i = 0; i < wasm_count; ++i) {
      wasm::ValueType type = sig_->GetParam(i);
      switch (type.kind()) {
        case wasm::kRef:
        case wasm::kRefNull:
        case wasm::kI64:
        case wasm::kRtt:
        case wasm::kS128:
        case wasm::kI8:
        case wasm::kI16:
        case wasm::kF16:
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
      Node* input, wasm::ValueType type,
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
      case wasm::kBottom:
      case wasm::kVoid:
        UNREACHABLE();
    }
  }

  void BuildJSToWasmWrapper(bool do_conversion = true,
                            Node* frame_state = nullptr,
                            bool set_in_wasm_flag = true) {
    const int wasm_param_count = static_cast<int>(sig_->parameter_count());

    // Build the start and the JS parameter nodes.
    Start(wasm_param_count + 5);

    // Create the js_closure and js_context parameters.
    Node* js_closure = Param(Linkage::kJSCallClosureParamIndex, "%closure");
    Node* js_context = Param(
        Linkage::GetJSCallContextParamIndex(wasm_param_count + 1), "%context");
    Node* function_data = gasm_->LoadFunctionDataFromJSFunction(js_closure);

    if (!wasm::IsJSCompatibleSignature(sig_)) {
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
                             QualifiesForFastTransform(sig_);

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
        CanTransformFast(params[i + 1], sig_->GetParam(i), &slow_path);
      }
      // Convert JS parameters to wasm numbers using the fast transformation
      // and build the call.
      base::SmallVector<Node*, 16> args(args_count);
      for (int i = 0; i < wasm_param_count; ++i) {
        Node* wasm_param = FromJSFast(params[i + 1], sig_->GetParam(i));
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
        args[i + 1] = FromJS(params[i + 1], js_context, sig_->GetParam(i),
                             module_, frame_state);
      } else {
        Node* wasm_param = params[i + 1];

        // For Float32 parameters
        // we set UseInfo::CheckedNumberOrOddballAsFloat64 in
        // simplified-lowering and we need to add here a conversion from Float64
        // to Float32.
        if (sig_->GetParam(i).kind() == wasm::kF32) {
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
    if (ContainsInt64(sig_)) LowerInt64(wasm::kCalledFromJS);
  }

  Node* BuildReceiverNode(Node* callable_node, Node* native_context,
                          Node* undefined_node) {
    // Check function strict bit.
    Node* shared_function_info = gasm_->LoadSharedFunctionInfo(callable_node);
    Node* flags = gasm_->LoadFromObject(
        MachineType::Int32(), shared_function_info,
        wasm::ObjectAccess::FlagsOffsetInSharedFunctionInfo());
    Node* strict_check =
        Binop(wasm::kExprI32And, flags,
              Int32Constant(SharedFunctionInfo::IsNativeBit::kMask |
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

  Node* BuildSuspend(Node* value, Node* suspender, Node* import_data,
                     Node** old_sp) {
    Node* native_context = gasm_->Load(
        MachineType::TaggedPointer(), import_data,
        wasm::ObjectAccess::ToTagged(WasmImportData::kNativeContextOffset));
    Node* active_suspender =
        LOAD_MUTABLE_ROOT(ActiveSuspender, active_suspender);

    // If value is a promise, suspend to the js-to-wasm prompt, and resume later
    // with the promise's resolved value.
    auto resume = gasm_->MakeLabel(MachineRepresentation::kTagged,
                                   MachineType::UintPtr().representation());
    gasm_->GotoIf(IsSmi(value), &resume, value, *old_sp);
    gasm_->GotoIfNot(gasm_->HasInstanceType(value, JS_PROMISE_TYPE), &resume,
                     BranchHint::kTrue, value, *old_sp);

    // Trap if the suspender argument is not the active suspender or if there is
    // no active suspender.
    auto bad_suspender = gasm_->MakeDeferredLabel();
    gasm_->GotoIf(gasm_->TaggedEqual(active_suspender, UndefinedValue()),
                  &bad_suspender, BranchHint::kFalse);
    gasm_->GotoIfNot(gasm_->TaggedEqual(suspender, active_suspender),
                     &bad_suspender, BranchHint::kFalse);

    auto* call_descriptor = GetBuiltinCallDescriptor(
        Builtin::kWasmSuspend, zone_, StubCallMode::kCallBuiltinPointer);
    Node* call_target = GetTargetForBuiltinCall(Builtin::kWasmSuspend);
    // If {old_sp} is null, it must be that we were on the central stack before
    // entering the wasm-to-js wrapper, which means that there are JS frames in
    // the current suspender. JS frames cannot be suspended, so trap.
    Node* has_js_frames = gasm_->IntPtrEqual(gasm_->IntPtrConstant(0), *old_sp);
    auto suspend = gasm_->MakeLabel();
    gasm_->GotoIfNot(has_js_frames, &suspend);
    // {ThrowWasmError} expects to be called from wasm code, so set the
    // thread-in-wasm flag now.
    // Usually we set this flag later so that it stays off while we convert the
    // return values. This is a special case, it is safe to set it now because
    // the error will unwind this frame.
    BuildModifyThreadInWasmFlag(true);
    Node* error = gasm_->SmiConstant(
        Smi::FromInt(
            static_cast<int32_t>(MessageTemplate::kWasmTrapSuspendJSFrames))
            .value());
    BuildCallToRuntimeWithContext(Runtime::kThrowWasmError, native_context,
                                  &error, 1);
    TerminateThrow(effect(), control());

    gasm_->Bind(&suspend);
    Node* on_fulfilled = gasm_->Load(
        MachineType::TaggedPointer(), suspender,
        wasm::ObjectAccess::ToTagged(WasmSuspenderObject::kResumeOffset));
    Node* on_rejected = gasm_->Load(
        MachineType::TaggedPointer(), suspender,
        wasm::ObjectAccess::ToTagged(WasmSuspenderObject::kRejectOffset));

    auto* then_call_desc = GetBuiltinCallDescriptor(
        Builtin::kPerformPromiseThen, zone_, StubCallMode::kCallBuiltinPointer);
    Node* then_target =
        gasm_->GetBuiltinPointerTarget(Builtin::kPerformPromiseThen);
    gasm_->Call(then_call_desc, then_target, value, on_fulfilled, on_rejected,
                UndefinedValue(), native_context);

    BuildSwitchBackFromCentralStack(*old_sp);
    Node* resolved = gasm_->Call(call_descriptor, call_target, suspender);
    BuildSwitchToTheCentralStack();
    gasm_->Goto(&resume, resolved, *old_sp);

    gasm_->Bind(&bad_suspender);
    BuildCallToRuntimeWithContext(Runtime::kThrowBadSuspenderError,
                                  native_context, nullptr, 0);
    TerminateThrow(effect(), control());
    gasm_->Bind(&resume);
    *old_sp = resume.PhiAt(1);
    return resume.PhiAt(0);
  }

  Node* BuildSwitchToTheCentralStack() {
    Node* stack_limit_slot = gasm_->IntPtrAdd(
        gasm_->LoadFramePointer(),
        gasm_->IntPtrConstant(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset));

    Node* do_switch = gasm_->ExternalConstant(
        ExternalReference::wasm_switch_to_the_central_stack_for_js());
    MachineType reps[] = {MachineType::Pointer(), MachineType::Pointer(),
                          MachineType::Pointer()};
    MachineSignature sig(1, 2, reps);

    Node* central_stack_sp = BuildCCall(
        &sig, do_switch,
        gasm_->ExternalConstant(ExternalReference::isolate_address()),
        stack_limit_slot);
    Node* old_sp = gasm_->LoadStackPointer();
    // Temporarily disallow sp-relative offsets.
    gasm_->SetStackPointer(central_stack_sp);
    gasm_->Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                     kNoWriteBarrier),
                 gasm_->LoadFramePointer(),
                 WasmImportWrapperFrameConstants::kCentralStackSPOffset,
                 central_stack_sp);
    return old_sp;
  }

  void BuildSwitchBackFromCentralStack(Node* old_sp) {
    Node* stack_limit = gasm_->Load(
        MachineType::Pointer(), gasm_->LoadFramePointer(),
        WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset);

    Node* do_switch = gasm_->ExternalConstant(
        ExternalReference::wasm_switch_from_the_central_stack_for_js());
    MachineType reps[] = {MachineType::Pointer(), MachineType::Pointer()};
    MachineSignature sig(0, 2, reps);
    BuildCCall(&sig, do_switch,
               gasm_->ExternalConstant(ExternalReference::isolate_address()),
               stack_limit);
    gasm_->Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                     kNoWriteBarrier),
                 gasm_->LoadFramePointer(),
                 WasmImportWrapperFrameConstants::kCentralStackSPOffset,
                 gasm_->IntPtrConstant(0));
    gasm_->SetStackPointer(old_sp);
  }

  Node* BuildSwitchToTheCentralStackIfNeeded() {
    // If the current stack is a secondary stack, switch to the central stack.
    auto end = gasm_->MakeLabel(MachineRepresentation::kTaggedPointer);
    Node* isolate_root = BuildLoadIsolateRoot();
    Node* is_on_central_stack_flag =
        gasm_->Load(MachineType::Uint8(), isolate_root,
                    IsolateData::is_on_central_stack_flag_offset());
    gasm_->GotoIf(is_on_central_stack_flag, &end, BranchHint::kTrue,
                  gasm_->IntPtrConstant(0));

    Node* old_sp = BuildSwitchToTheCentralStack();
    gasm_->Goto(&end, old_sp);

    gasm_->Bind(&end);
    return end.PhiAt(0);
  }

  // For wasm-to-js wrappers, parameter 0 is a WasmImportData.
  void BuildWasmToJSWrapper(wasm::ImportCallKind kind, int expected_arity,
                            wasm::Suspend suspend,
                            const wasm::WasmModule* module) {
    int wasm_count = static_cast<int>(sig_->parameter_count());

    // Build the start and the parameter nodes.
    Start(wasm_count + 3);

    Node* native_context = gasm_->Load(
        MachineType::TaggedPointer(), Param(0),
        wasm::ObjectAccess::ToTagged(WasmImportData::kNativeContextOffset));

    if (kind == wasm::ImportCallKind::kRuntimeTypeError) {
      // =======================================================================
      // === Runtime TypeError =================================================
      // =======================================================================
      BuildCallToRuntimeWithContext(Runtime::kWasmThrowJSTypeError,
                                    native_context, nullptr, 0);
      TerminateThrow(effect(), control());
      return;
    }

#if V8_ENABLE_DRUMBRAKE
    if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms &&
        !v8_flags.wasm_jitless) {
      Node* runtime_call = BuildCallToRuntimeWithContext(
          Runtime::kWasmTraceEndExecution, native_context, nullptr, 0);
      SetControl(runtime_call);
    }
#endif  // V8_ENABLE_DRUMBRAKE

    Node* callable_node = gasm_->Load(
        MachineType::TaggedPointer(), Param(0),
        wasm::ObjectAccess::ToTagged(WasmImportData::kCallableOffset));
    Node* old_sp = BuildSwitchToTheCentralStackIfNeeded();

    Node* undefined_node = UndefinedValue();
    Node* call = nullptr;
    // Clear the ThreadInWasm flag.
    BuildModifyThreadInWasmFlag(false);
    switch (kind) {
      // =======================================================================
      // === JS Functions with matching arity ==================================
      // =======================================================================
      case wasm::ImportCallKind::kJSFunctionArityMatch:
        DCHECK_EQ(expected_arity, wasm_count);
        [[fallthrough]];
      case wasm::ImportCallKind::kJSFunctionArityMismatch: {
        int pushed_count = std::max(expected_arity, wasm_count);
        base::SmallVector<Node*, 16> args(pushed_count + 7);
        int pos = 0;

        args[pos++] = callable_node;  // target callable.
        // Determine receiver at runtime.
        args[pos++] =
            BuildReceiverNode(callable_node, native_context, undefined_node);

        // Convert wasm numbers to JS values.
        pos = AddArgumentNodes(base::VectorOf(args), pos, wasm_count, sig_,
                               native_context);
        for (int i = wasm_count; i < expected_arity; ++i) {
          args[pos++] = undefined_node;
        }
        args[pos++] = undefined_node;  // new target
        args[pos++] =
            Int32Constant(JSParameterCount(wasm_count));  // argument count

        Node* function_context =
            gasm_->LoadContextFromJSFunction(callable_node);
        args[pos++] = function_context;
        args[pos++] = effect();
        args[pos++] = control();

        auto call_descriptor = Linkage::GetJSCallDescriptor(
            graph()->zone(), false, pushed_count + 1, CallDescriptor::kNoFlags);
        call = gasm_->Call(call_descriptor, pos, args.begin());
        break;
      }
      // =======================================================================
      // === General case of unknown callable ==================================
      // =======================================================================
      case wasm::ImportCallKind::kUseCallBuiltin: {
        base::SmallVector<Node*, 16> args(wasm_count + 7);
        int pos = 0;
        args[pos++] =
            gasm_->GetBuiltinPointerTarget(Builtin::kCall_ReceiverIsAny);
        args[pos++] = callable_node;
        args[pos++] =
            Int32Constant(JSParameterCount(wasm_count));     // argument count
        args[pos++] = undefined_node;                        // receiver

        auto call_descriptor = Linkage::GetStubCallDescriptor(
            graph()->zone(), CallTrampolineDescriptor{}, wasm_count + 1,
            CallDescriptor::kNoFlags, Operator::kNoProperties,
            StubCallMode::kCallBuiltinPointer);

        // Convert wasm numbers to JS values.
        pos = AddArgumentNodes(base::VectorOf(args), pos, wasm_count, sig_,
                               native_context);

        // The native_context is sufficient here, because all kind of callables
        // which depend on the context provide their own context. The context
        // here is only needed if the target is a constructor to throw a
        // TypeError, if the target is a native function, or if the target is a
        // callable JSObject, which can only be constructed by the runtime.
        args[pos++] = native_context;
        args[pos++] = effect();
        args[pos++] = control();
        call = gasm_->Call(call_descriptor, pos, args.begin());
        break;
      }
      default:
        UNREACHABLE();
    }
    // For asm.js the error location can differ depending on whether an
    // exception was thrown in imported JS code or an exception was thrown in
    // the ToNumber builtin that converts the result of the JS code a
    // WebAssembly value. The source position allows asm.js to determine the
    // correct error location. Source position 1 encodes the call to ToNumber,
    // source position 0 encodes the call to the imported JS code.
    SetSourcePosition(call, 0);
    DCHECK_NOT_NULL(call);

#if V8_ENABLE_DRUMBRAKE
    if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms &&
        !v8_flags.wasm_jitless) {
      Node* runtime_call = BuildCallToRuntimeWithContext(
          Runtime::kWasmTraceBeginExecution, native_context, nullptr, 0);
      SetControl(runtime_call);
    }
#endif  // V8_ENABLE_DRUMBRAKE

    if (suspend == wasm::kSuspend) {
      Node* active_suspender =
          LOAD_MUTABLE_ROOT(ActiveSuspender, active_suspender);
      call = BuildSuspend(call, active_suspender, Param(0), &old_sp);
    }

    // Convert the return value(s) back.
    Node* val;
    base::SmallVector<Node*, 8> wasm_values;
    if (sig_->return_count() <= 1) {
      val = sig_->return_count() == 0
                ? Int32Constant(0)
                : FromJS(call, native_context, sig_->GetReturn(), module);
    } else {
      Node* fixed_array =
          BuildMultiReturnFixedArrayFromIterable(sig_, call, native_context);
      wasm_values.resize_no_init(sig_->return_count());
      for (unsigned i = 0; i < sig_->return_count(); ++i) {
        wasm_values[i] = FromJS(gasm_->LoadFixedArrayElementAny(fixed_array, i),
                                native_context, sig_->GetReturn(i), module);
      }
    }
    BuildModifyThreadInWasmFlag(true);

    auto done = gasm_->MakeLabel();
    gasm_->GotoIf(gasm_->IntPtrEqual(old_sp, gasm_->IntPtrConstant(0)), &done,
                  BranchHint::kTrue);
    BuildSwitchBackFromCentralStack(old_sp);
    gasm_->Goto(&done);
    gasm_->Bind(&done);

    if (sig_->return_count() <= 1) {
      Return(val);
    } else {
      Return(base::VectorOf(wasm_values));
    }

    if (ContainsInt64(sig_)) LowerInt64(wasm::kCalledFromWasm);
  }

  void BuildCapiCallWrapper() {
    // Set up the graph start.
    Start(static_cast<int>(sig_->parameter_count()) +
          1 /* offset for first parameter index being -1 */ +
          1 /* WasmImportData */);
    // Store arguments on our stack, then align the stack for calling to C.
    int param_bytes = 0;
    for (wasm::ValueType type : sig_->parameters()) {
      param_bytes += type.value_kind_size();
    }
    int return_bytes = 0;
    for (wasm::ValueType type : sig_->returns()) {
      return_bytes += type.value_kind_size();
    }

    int stack_slot_bytes = std::max(param_bytes, return_bytes);
    Node* values = stack_slot_bytes == 0
                       ? mcgraph()->IntPtrConstant(0)
                       : graph()->NewNode(mcgraph()->machine()->StackSlot(
                             stack_slot_bytes, kDoubleAlignment));

    int offset = 0;
    int param_count = static_cast<int>(sig_->parameter_count());
    for (int i = 0; i < param_count; ++i) {
      wasm::ValueType type = sig_->GetParam(i);
      // Start from the parameter with index 1 to drop the instance_node.
      // TODO(jkummerow): When a values is a reference type, we should pass it
      // in a GC-safe way, not just as a raw pointer.
      SetEffect(graph()->NewNode(GetSafeStoreOperator(offset, type), values,
                                 Int32Constant(offset), Param(i + 1), effect(),
                                 control()));
      offset += type.value_kind_size();
    }

    Node* function_node = gasm_->Load(
        MachineType::TaggedPointer(), Param(0),
        wasm::ObjectAccess::ToTagged(WasmImportData::kCallableOffset));
    Node* sfi_data = gasm_->LoadFunctionDataFromJSFunction(function_node);
    Node* host_data_foreign =
        gasm_->Load(MachineType::AnyTagged(), sfi_data,
                    wasm::ObjectAccess::ToTagged(
                        WasmCapiFunctionData::kEmbedderDataOffset));

    BuildModifyThreadInWasmFlag(false);
    Node* isolate_root = BuildLoadIsolateRoot();
    Node* fp_value = graph()->NewNode(mcgraph()->machine()->LoadFramePointer());
    gasm_->Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                     kNoWriteBarrier),
                 isolate_root, Isolate::c_entry_fp_offset(), fp_value);

    Node* function = BuildLoadCallTargetFromExportedFunctionData(sfi_data);

    // Parameters: Address host_data_foreign, Address arguments.
    MachineType host_sig_types[] = {
        MachineType::Pointer(), MachineType::Pointer(), MachineType::Pointer()};
    MachineSignature host_sig(1, 2, host_sig_types);
    Node* return_value =
        BuildCCall(&host_sig, function, host_data_foreign, values);

    BuildModifyThreadInWasmFlag(true);

    Node* old_effect = effect();
    Node* exception_branch = graph()->NewNode(
        mcgraph()->common()->Branch(BranchHint::kTrue),
        gasm_->WordEqual(return_value, mcgraph()->IntPtrConstant(0)),
        control());
    SetControl(
        graph()->NewNode(mcgraph()->common()->IfFalse(), exception_branch));
    WasmRethrowExplicitContextDescriptor interface_descriptor;
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        mcgraph()->zone(), interface_descriptor,
        interface_descriptor.GetStackParameterCount(), CallDescriptor::kNoFlags,
        Operator::kNoProperties, StubCallMode::kCallBuiltinPointer);
    Node* call_target =
        GetTargetForBuiltinCall(Builtin::kWasmRethrowExplicitContext);
    Node* context = gasm_->Load(
        MachineType::TaggedPointer(), Param(0),
        wasm::ObjectAccess::ToTagged(WasmImportData::kNativeContextOffset));
    gasm_->Call(call_descriptor, call_target, return_value, context);
    TerminateThrow(effect(), control());

    SetEffectControl(old_effect, graph()->NewNode(mcgraph()->common()->IfTrue(),
                                                  exception_branch));
    DCHECK_LT(sig_->return_count(), wasm::kV8MaxWasmFunctionReturns);
    size_t return_count = sig_->return_count();
    if (return_count == 0) {
      Return(Int32Constant(0));
    } else {
      base::SmallVector<Node*, 8> returns(return_count);
      offset = 0;
      for (size_t i = 0; i < return_count; ++i) {
        wasm::ValueType type = sig_->GetReturn(i);
        Node* val = SetEffect(
            graph()->NewNode(GetSafeLoadOperator(offset, type), values,
                             Int32Constant(offset), effect(), control()));
        returns[i] = val;
        offset += type.value_kind_size();
      }
      Return(base::VectorOf(returns));
    }

    if (ContainsInt64(sig_)) LowerInt64(wasm::kCalledFromWasm);
  }

  void BuildJSFastApiCallWrapper(Handle<JSReceiver> callable) {
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

    FastApiCallFunctionVector fast_api_call_function_vector(mcgraph()->zone());
    fast_api_call_function_vector.push_back({c_address, c_signature});
    Node* call = fast_api_call::BuildFastApiCall(
        target->GetIsolate(), graph(), gasm_.get(),
        fast_api_call_function_vector, c_signature, api_data_argument,
        // Load and convert parameters passed to C function
        [this, c_signature, receiver_node](
            int param_index,
            fast_api_call::OverloadsResolutionResult& overloads,
            GraphAssemblerLabel<0>*) {
          // Wasm does not currently support overloads
          CHECK(!overloads.is_valid());

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
          int wasm_count = static_cast<int>(sig_->parameter_count());
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
          pos = AddArgumentNodes(base::VectorOf(args), pos, wasm_count, sig_,
                                 native_context);

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
          return sig_->return_count() == 0
                     ? Int32Constant(0)
                     : FromJS(call, native_context, sig_->GetReturn(), nullptr);
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

    int wasm_arg_count = static_cast<int>(sig_->parameter_count());
    base::SmallVector<Node*, 16> args(wasm_arg_count + 4);

    int pos = 0;
    args[pos++] = code_entry;
    args[pos++] = gasm_->LoadTrustedDataFromInstanceObject(object_ref);

    int offset = 0;
    for (wasm::ValueType type : sig_->parameters()) {
      Node* arg_load = SetEffect(
          graph()->NewNode(GetSafeLoadOperator(offset, type), arg_buffer,
                           Int32Constant(offset), effect(), control()));
      args[pos++] = arg_load;
      offset += type.value_kind_size();
    }

    args[pos++] = effect();
    args[pos++] = control();

    // Call the wasm code.
    auto call_descriptor = GetWasmCallDescriptor(mcgraph()->zone(), sig_);

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
    for (wasm::ValueType type : sig_->returns()) {
      Node* value = sig_->return_count() == 1
                        ? call
                        : graph()->NewNode(mcgraph()->common()->Projection(pos),
                                           call, control());
      SetEffect(graph()->NewNode(GetSafeStoreOperator(offset, type), arg_buffer,
                                 Int32Constant(offset), value, effect(),
                                 control()));
      offset += type.value_kind_size();
      pos++;
    }

    Return(mcgraph()->IntPtrConstant(0));

    if (mcgraph()->machine()->Is32() && ContainsInt64(sig_)) {
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
  const wasm::WasmModule* module_;
  SetOncePointer<const Operator> int32_to_heapnumber_operator_;
  SetOncePointer<const Operator> tagged_non_smi_to_int32_operator_;
  SetOncePointer<const Operator> float32_to_number_operator_;
  SetOncePointer<const Operator> float64_to_number_operator_;
  SetOncePointer<const Operator> tagged_to_float64_operator_;
};

}  // namespace

void BuildInlinedJSToWasmWrapper(Zone* zone, MachineGraph* mcgraph,
                                 const wasm::FunctionSig* signature,
                                 const wasm::WasmModule* module,
                                 Isolate* isolate,
                                 compiler::SourcePositionTable* spt,
                                 wasm::WasmEnabledFeatures features,
                                 Node* frame_state, bool set_in_wasm_flag) {
  WasmWrapperGraphBuilder builder(zone, mcgraph, signature, module,
                                  WasmGraphBuilder::kJSFunctionAbiMode, isolate,
                                  spt, features);
  builder.BuildJSToWasmWrapper(false, frame_state, set_in_wasm_flag);
}

std::unique_ptr<OptimizedCompilationJob> NewJSToWasmCompilationJob(
    Isolate* isolate, const wasm::FunctionSig* sig,
    const wasm::WasmModule* module,
    wasm::WasmEnabledFeatures enabled_features) {
  std::unique_ptr<char[]> debug_name = WasmExportedFunction::GetDebugName(sig);
  if (v8_flags.turboshaft_wasm_wrappers) {
    return Pipeline::NewWasmTurboshaftWrapperCompilationJob(
        isolate, sig,
        wasm::WrapperCompilationInfo{CodeKind::JS_TO_WASM_FUNCTION,
                                     StubCallMode::kCallBuiltinPointer},
        module, std::move(debug_name), WasmAssemblerOptions());
  } else {
    std::unique_ptr<Zone> zone = std::make_unique<Zone>(
        wasm::GetWasmEngine()->allocator(), ZONE_NAME, kCompressGraphZone);
    int params = static_cast<int>(sig->parameter_count());
    CallDescriptor* incoming = Linkage::GetJSCallDescriptor(
        zone.get(), false, params + 1, CallDescriptor::kNoFlags);

    //----------------------------------------------------------------------------
    // Create the Graph.
    //----------------------------------------------------------------------------
    Graph* graph = zone->New<Graph>(zone.get());
    CommonOperatorBuilder* common =
        zone->New<CommonOperatorBuilder>(zone.get());
    MachineOperatorBuilder* machine = zone->New<MachineOperatorBuilder>(
        zone.get(), MachineType::PointerRepresentation(),
        InstructionSelector::SupportedMachineOperatorFlags(),
        InstructionSelector::AlignmentRequirements());
    MachineGraph* mcgraph = zone->New<MachineGraph>(graph, common, machine);

    WasmWrapperGraphBuilder builder(zone.get(), mcgraph, sig, module,
                                    WasmGraphBuilder::kJSFunctionAbiMode,
                                    isolate, nullptr, enabled_features);
    builder.BuildJSToWasmWrapper();

    //----------------------------------------------------------------------------
    // Create the compilation job.
    //----------------------------------------------------------------------------
    return Pipeline::NewWasmHeapStubCompilationJob(
        isolate, incoming, std::move(zone), graph,
        CodeKind::JS_TO_WASM_FUNCTION, std::move(debug_name),
        WasmAssemblerOptions());
  }
}

namespace {

wasm::WasmOpcode GetMathIntrinsicOpcode(wasm::ImportCallKind kind,
                                        const char** name_ptr) {
#define CASE(name)                          \
  case wasm::ImportCallKind::k##name:       \
    *name_ptr = "WasmMathIntrinsic:" #name; \
    return wasm::kExpr##name
  switch (kind) {
    CASE(F64Acos);
    CASE(F64Asin);
    CASE(F64Atan);
    CASE(F64Cos);
    CASE(F64Sin);
    CASE(F64Tan);
    CASE(F64Exp);
    CASE(F64Log);
    CASE(F64Atan2);
    CASE(F64Pow);
    CASE(F64Ceil);
    CASE(F64Floor);
    CASE(F64Sqrt);
    CASE(F64Min);
    CASE(F64Max);
    CASE(F64Abs);
    CASE(F32Min);
    CASE(F32Max);
    CASE(F32Abs);
    CASE(F32Ceil);
    CASE(F32Floor);
    CASE(F32Sqrt);
    CASE(F32ConvertF64);
    default:
      UNREACHABLE();
  }
#undef CASE
}

MachineGraph* CreateCommonMachineGraph(Zone* zone) {
  return zone->New<MachineGraph>(
      zone->New<Graph>(zone), zone->New<CommonOperatorBuilder>(zone),
      zone->New<MachineOperatorBuilder>(
          zone, MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements()));
}

wasm::WasmCompilationResult CompileWasmMathIntrinsic(
    wasm::ImportCallKind kind, const wasm::FunctionSig* sig) {
  DCHECK_EQ(1, sig->return_count());

  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileWasmMathIntrinsic");

  Zone zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME, kCompressGraphZone);

  // Compile a Wasm function with a single bytecode and let TurboFan
  // generate either inlined machine code or a call to a helper.
  SourcePositionTable* source_positions = nullptr;
  MachineGraph* mcgraph = CreateCommonMachineGraph(&zone);

  wasm::CompilationEnv env = wasm::CompilationEnv::NoModuleAllFeatures();

  WasmGraphBuilder builder(&env, mcgraph->zone(), mcgraph, sig,
                           source_positions,
                           WasmGraphBuilder::kWasmImportDataMode,
                           nullptr /* isolate */, env.enabled_features);

  // Set up the graph start.
  builder.Start(static_cast<int>(sig->parameter_count() + 1 + 1));

  // Generate either a unop or a binop.
  Node* node = nullptr;
  const char* debug_name = "WasmMathIntrinsic";
  auto opcode = GetMathIntrinsicOpcode(kind, &debug_name);
  switch (sig->parameter_count()) {
    case 1:
      node = builder.Unop(opcode, builder.Param(1));
      break;
    case 2:
      node = builder.Binop(opcode, builder.Param(1), builder.Param(2));
      break;
    default:
      UNREACHABLE();
  }

  builder.Return(node);

  // Run the compiler pipeline to generate machine code.
  auto call_descriptor = GetWasmCallDescriptor(&zone, sig);
  if (mcgraph->machine()->Is32()) {
    call_descriptor = GetI32WasmCallDescriptor(&zone, call_descriptor);
  }

  // The code does not call to JS, but conceptually it is an import wrapper,
  // hence use {WASM_TO_JS_FUNCTION} here.
  // TODO(wasm): Rename this to {WASM_IMPORT_CALL}?
  return Pipeline::GenerateCodeForWasmNativeStub(
      call_descriptor, mcgraph, CodeKind::WASM_TO_JS_FUNCTION, debug_name,
      WasmStubAssemblerOptions(), source_positions);
}

}  // namespace

wasm::WasmCompilationResult CompileWasmImportCallWrapper(
    wasm::CompilationEnv* env, wasm::ImportCallKind kind,
    const wasm::FunctionSig* sig, bool source_positions, int expected_arity,
    wasm::Suspend suspend) {
  DCHECK_NE(wasm::ImportCallKind::kLinkError, kind);
  DCHECK_NE(wasm::ImportCallKind::kWasmToWasm, kind);
  DCHECK_NE(wasm::ImportCallKind::kWasmToJSFastApi, kind);

  // Check for math intrinsics first.
  if (v8_flags.wasm_math_intrinsics &&
      kind >= wasm::ImportCallKind::kFirstMathIntrinsic &&
      kind <= wasm::ImportCallKind::kLastMathIntrinsic) {
    // TODO(thibaudm): Port to Turboshaft.
    return CompileWasmMathIntrinsic(kind, sig);
  }

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

  auto compile_with_turboshaft = [&]() {
    return Pipeline::GenerateCodeForWasmNativeStubFromTurboshaft(
        env->module, sig,
        wasm::WrapperCompilationInfo{CodeKind::WASM_TO_JS_FUNCTION,
                                     StubCallMode::kCallBuiltinPointer, kind,
                                     expected_arity, suspend},
        func_name, WasmStubAssemblerOptions(), nullptr);
  };
  auto compile_with_turbofan = [&]() {
    //--------------------------------------------------------------------------
    // Create the Graph
    //--------------------------------------------------------------------------
    Zone zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME,
              kCompressGraphZone);
    Graph* graph = zone.New<Graph>(&zone);
    CommonOperatorBuilder* common = zone.New<CommonOperatorBuilder>(&zone);
    MachineOperatorBuilder* machine = zone.New<MachineOperatorBuilder>(
        &zone, MachineType::PointerRepresentation(),
        InstructionSelector::SupportedMachineOperatorFlags(),
        InstructionSelector::AlignmentRequirements());
    MachineGraph* mcgraph = zone.New<MachineGraph>(graph, common, machine);

    SourcePositionTable* source_position_table =
        source_positions ? zone.New<SourcePositionTable>(graph) : nullptr;

    WasmWrapperGraphBuilder builder(
        &zone, mcgraph, sig, env->module, WasmGraphBuilder::kWasmImportDataMode,
        nullptr, source_position_table, env->enabled_features);
    builder.BuildWasmToJSWrapper(kind, expected_arity, suspend, env->module);

    // Schedule and compile to machine code.
    CallDescriptor* incoming =
        GetWasmCallDescriptor(&zone, sig, WasmCallKind::kWasmImportWrapper);
    if (machine->Is32()) {
      incoming = GetI32WasmCallDescriptor(&zone, incoming);
    }
    return Pipeline::GenerateCodeForWasmNativeStub(
        incoming, mcgraph, CodeKind::WASM_TO_JS_FUNCTION, func_name,
        WasmStubAssemblerOptions(), source_position_table);
  };

  auto result = v8_flags.turboshaft_wasm_wrappers ? compile_with_turboshaft()
                                                  : compile_with_turbofan();
  if (V8_UNLIKELY(v8_flags.trace_wasm_compilation_times)) {
    base::TimeDelta time = base::TimeTicks::Now() - start_time;
    int codesize = result.code_desc.body_size();
    StdoutStream{} << "Compiled WasmToJS wrapper " << func_name << ", took "
                   << time.InMilliseconds() << " ms; codesize " << codesize
                   << std::endl;
  }

  return result;
}

wasm::WasmCode* CompileWasmCapiCallWrapper(wasm::NativeModule* native_module,
                                           const wasm::FunctionSig* sig) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileWasmCapiFunction");
  const char* debug_name = "WasmCapiCall";

  auto compile_with_turboshaft = [&]() {
    return Pipeline::GenerateCodeForWasmNativeStubFromTurboshaft(
        native_module->module(), sig,
        wasm::WrapperCompilationInfo{CodeKind::WASM_TO_CAPI_FUNCTION,
                                     StubCallMode::kCallBuiltinPointer},
        debug_name, WasmStubAssemblerOptions(), nullptr);
  };

  auto compile_with_turbofan = [&]() {
    Zone zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME,
              kCompressGraphZone);

    SourcePositionTable* source_positions = nullptr;
    MachineGraph* mcgraph = CreateCommonMachineGraph(&zone);

    WasmWrapperGraphBuilder builder(
        &zone, mcgraph, sig, native_module->module(),
        WasmGraphBuilder::kWasmImportDataMode, nullptr, source_positions,
        native_module->enabled_features());

    builder.BuildCapiCallWrapper();

    // Run the compiler pipeline to generate machine code.
    CallDescriptor* call_descriptor =
        GetWasmCallDescriptor(&zone, sig, WasmCallKind::kWasmCapiFunction);
    if (mcgraph->machine()->Is32()) {
      call_descriptor = GetI32WasmCallDescriptor(&zone, call_descriptor);
    }

    return Pipeline::GenerateCodeForWasmNativeStub(
        call_descriptor, mcgraph, CodeKind::WASM_TO_CAPI_FUNCTION, debug_name,
        WasmStubAssemblerOptions(), source_positions);
  };
  wasm::WasmCompilationResult result = v8_flags.turboshaft_wasm_wrappers
                                           ? compile_with_turboshaft()
                                           : compile_with_turbofan();
  wasm::WasmCode* published_code;
  {
    std::unique_ptr<wasm::WasmCode> wasm_code = native_module->AddCode(
        wasm::kAnonymousFuncIndex, result.code_desc, result.frame_slot_count,
        result.ool_spill_count, result.tagged_parameter_slots,
        result.protected_instructions_data.as_vector(),
        result.source_positions.as_vector(),
        result.inlining_positions.as_vector(), result.deopt_data.as_vector(),
        wasm::WasmCode::kWasmToCapiWrapper, wasm::ExecutionTier::kNone,
        wasm::kNotForDebugging);
    published_code = native_module->PublishCode(std::move(wasm_code));
  }
  return published_code;
}

bool IsFastCallSupportedSignature(const v8::CFunctionInfo* sig) {
  return fast_api_call::CanOptimizeFastSignature(sig);
}

wasm::WasmCode* CompileWasmJSFastCallWrapper(wasm::NativeModule* native_module,
                                             const wasm::FunctionSig* sig,
                                             Handle<JSReceiver> callable) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileWasmJSFastCallWrapper");

  Zone zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME, kCompressGraphZone);
  SourcePositionTable* source_positions = nullptr;
  MachineGraph* mcgraph = CreateCommonMachineGraph(&zone);

  WasmWrapperGraphBuilder builder(&zone, mcgraph, sig, native_module->module(),
                                  WasmGraphBuilder::kWasmImportDataMode,
                                  nullptr, source_positions,
                                  native_module->enabled_features());

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
  {
    std::unique_ptr<wasm::WasmCode> wasm_code = native_module->AddCode(
        wasm::kAnonymousFuncIndex, result.code_desc, result.frame_slot_count,
        result.ool_spill_count, result.tagged_parameter_slots,
        result.protected_instructions_data.as_vector(),
        result.source_positions.as_vector(),
        result.inlining_positions.as_vector(), result.deopt_data.as_vector(),
        wasm::WasmCode::kWasmToJsWrapper, wasm::ExecutionTier::kNone,
        wasm::kNotForDebugging);
    return native_module->PublishCode(std::move(wasm_code));
  }
}

MaybeHandle<Code> CompileWasmToJSWrapper(Isolate* isolate,
                                         const wasm::WasmModule* module,
                                         const wasm::FunctionSig* sig,
                                         wasm::ImportCallKind kind,
                                         int expected_arity,
                                         wasm::Suspend suspend) {
  DCHECK(!v8_flags.wasm_jitless);

  // Build a name in the form "wasm-to-js-<kind>-<signature>".
  constexpr size_t kMaxNameLen = 128;
  constexpr size_t kNamePrefixLen = 11;
  auto name_buffer = std::unique_ptr<char[]>(new char[kMaxNameLen]);
  memcpy(name_buffer.get(), "wasm-to-js:", kNamePrefixLen);
  PrintSignature(
      base::VectorOf(name_buffer.get(), kMaxNameLen) + kNamePrefixLen, sig);

  auto compile_with_turboshaft = [&]() {
    std::unique_ptr<turboshaft::TurboshaftCompilationJob> job =
        Pipeline::NewWasmTurboshaftWrapperCompilationJob(
            isolate, sig,
            wasm::WrapperCompilationInfo{CodeKind::WASM_TO_JS_FUNCTION,
                                         StubCallMode::kCallBuiltinPointer,
                                         kind, expected_arity, suspend},
            module, std::move(name_buffer), WasmAssemblerOptions());

    // Compile the wrapper
    if (job->ExecuteJob(isolate->counters()->runtime_call_stats()) ==
            CompilationJob::FAILED ||
        job->FinalizeJob(isolate) == CompilationJob::FAILED) {
      return Handle<Code>();
    }
    return job->compilation_info()->code();
  };
  auto compile_with_turbofan = [&]() {
    std::unique_ptr<Zone> zone = std::make_unique<Zone>(
        isolate->allocator(), ZONE_NAME, kCompressGraphZone);

    // Create the Graph
    Graph* graph = zone->New<Graph>(zone.get());
    CommonOperatorBuilder* common =
        zone->New<CommonOperatorBuilder>(zone.get());
    MachineOperatorBuilder* machine = zone->New<MachineOperatorBuilder>(
        zone.get(), MachineType::PointerRepresentation(),
        InstructionSelector::SupportedMachineOperatorFlags(),
        InstructionSelector::AlignmentRequirements());
    MachineGraph* mcgraph = zone->New<MachineGraph>(graph, common, machine);

    WasmWrapperGraphBuilder builder(
        zone.get(), mcgraph, sig, module, WasmGraphBuilder::kWasmImportDataMode,
        nullptr, nullptr, wasm::WasmEnabledFeatures::FromIsolate(isolate));
    builder.BuildWasmToJSWrapper(kind, expected_arity, suspend, nullptr);

    // Generate the call descriptor.
    CallDescriptor* incoming = GetWasmCallDescriptor(
        zone.get(), sig, WasmCallKind::kWasmImportWrapper);
    if (machine->Is32()) {
      incoming = GetI32WasmCallDescriptor(zone.get(), incoming);
    }

    // Run the compilation job synchronously.
    std::unique_ptr<TurbofanCompilationJob> job(
        Pipeline::NewWasmHeapStubCompilationJob(
            isolate, incoming, std::move(zone), graph,
            CodeKind::WASM_TO_JS_FUNCTION, std::move(name_buffer),
            AssemblerOptions::Default(isolate)));

    // Compile the wrapper
    if (job->ExecuteJob(isolate->counters()->runtime_call_stats()) ==
            CompilationJob::FAILED ||
        job->FinalizeJob(isolate) == CompilationJob::FAILED) {
      return Handle<Code>();
    }
    return job->compilation_info()->code();
  };
  return v8_flags.turboshaft_wasm_wrappers ? compile_with_turboshaft()
                                           : compile_with_turbofan();
}

Handle<Code> CompileCWasmEntry(Isolate* isolate, const wasm::FunctionSig* sig,
                               const wasm::WasmModule* module) {
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

  WasmWrapperGraphBuilder builder(
      zone.get(), mcgraph, sig, module,
      WasmGraphBuilder::kNoSpecialParameterMode, nullptr, nullptr,
      wasm::WasmEnabledFeatures::FromIsolate(isolate));
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

namespace {

void BuildGraphForWasmFunction(wasm::CompilationEnv* env,
                               WasmCompilationData& data,
                               wasm::WasmDetectedFeatures* detected,
                               MachineGraph* mcgraph) {
  // Create a TF graph during decoding.
  WasmGraphBuilder builder(env, mcgraph->zone(), mcgraph, data.func_body.sig,
                           data.source_positions,
                           WasmGraphBuilder::kInstanceParameterMode,
                           nullptr /* isolate */, env->enabled_features);
  auto* allocator = wasm::GetWasmEngine()->allocator();
  wasm::BuildTFGraph(allocator, env->enabled_features, env->module, &builder,
                     detected, data.func_body, data.loop_infos, nullptr,
                     data.node_origins, data.func_index, data.assumptions,
                     wasm::kRegularFunction);

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
  if (v8_flags.experimental_wasm_revectorize && builder.has_simd()) {
    mcgraph->graph()->SetSimd(true);
  }
#endif
}

}  // namespace

wasm::WasmCompilationResult ExecuteTurbofanWasmCompilation(
    wasm::CompilationEnv* env, WasmCompilationData& data, Counters* counters,
    wasm::WasmDetectedFeatures* detected) {
  // Check that we do not accidentally compile a Wasm function to TurboFan if
  // --liftoff-only is set.
  DCHECK(!v8_flags.liftoff_only);

  TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileTopTier", "func_index", data.func_index,
               "body_size", data.body_size());
  Zone zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME, kCompressGraphZone);
  MachineGraph* mcgraph = CreateCommonMachineGraph(&zone);

  OptimizedCompilationInfo info(
      GetDebugName(&zone, env->module, data.wire_bytes_storage,
                   data.func_index),
      &zone, CodeKind::WASM_FUNCTION);
  info.set_allocation_folding();

  if (info.trace_turbo_json()) {
    TurboCfgFile tcf;
    tcf << AsC1VCompilation(&info);
  }

  if (info.trace_turbo_json()) {
    data.node_origins = zone.New<NodeOriginTable>(mcgraph->graph());
  }

  data.source_positions =
      mcgraph->zone()->New<SourcePositionTable>(mcgraph->graph());
  ZoneVector<WasmInliningPosition> inlining_positions(&zone);

  std::vector<WasmLoopInfo> loop_infos;
  data.loop_infos = &loop_infos;
  data.assumptions = new wasm::AssumptionsJournal();

  DCHECK_NOT_NULL(detected);
  BuildGraphForWasmFunction(env, data, detected, mcgraph);

  if (data.node_origins) {
    data.node_origins->AddDecorator();
  }

  // Run the compiler pipeline to generate machine code.
  auto call_descriptor = GetWasmCallDescriptor(&zone, data.func_body.sig);
  if (mcgraph->machine()->Is32()) {
    call_descriptor = GetI32WasmCallDescriptor(&zone, call_descriptor);
  }

  if (ContainsSimd(data.func_body.sig) && !CpuFeatures::SupportsWasmSimd128()) {
    // Fail compilation if hardware does not support SIMD.
    return wasm::WasmCompilationResult{};
  }

  Pipeline::GenerateCodeForWasmFunction(&info, env, data, mcgraph,
                                        call_descriptor, &inlining_positions,
                                        detected);

  if (counters && data.body_size() >= 100 * KB) {
    size_t zone_bytes = mcgraph->graph()->zone()->allocation_size();
    counters->wasm_compile_huge_function_peak_memory_bytes()->AddSample(
        static_cast<int>(zone_bytes));
  }

  // If we tiered up only one function for debugging, dump statistics
  // immediately.
  if (V8_UNLIKELY(v8_flags.turbo_stats_wasm &&
                  v8_flags.wasm_tier_up_filter >= 0)) {
    wasm::GetWasmEngine()->DumpTurboStatistics();
  }
  auto result = info.ReleaseWasmCompilationResult();
  CHECK_NOT_NULL(result);  // Compilation expected to succeed.
  DCHECK_EQ(wasm::ExecutionTier::kTurbofan, result->result_tier);
  result->assumptions.reset(data.assumptions);
  return std::move(*result);
}

void WasmGraphBuilder::StoreCallCount(Node* call, int count) {
  mcgraph()->StoreCallCount(call->id(), count);
}

void WasmGraphBuilder::ReserveCallCounts(size_t num_call_instructions) {
  mcgraph()->ReserveCallCounts(num_call_instructions);
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
      .is_wasm = true,
  };
}

#undef FATAL_UNSUPPORTED_OPCODE
#undef WASM_INSTANCE_OBJECT_SIZE
#undef LOAD_INSTANCE_FIELD
#undef LOAD_MUTABLE_INSTANCE_FIELD
#undef LOAD_ROOT
#undef LOAD_MUTABLE_ROOT

}  // namespace compiler
}  // namespace internal
}  // namespace v8
