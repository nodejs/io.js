// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/turboshaft-graph-interface.h"

#include "include/v8-fast-api-calls.h"
#include "src/base/logging.h"
#include "src/builtins/builtins.h"
#include "src/builtins/data-view-ops.h"
#include "src/common/globals.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/builtin-call-descriptors.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/wasm-assembler-helpers.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/torque-defined-classes.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/inlining-tree.h"
#include "src/wasm/memory-tracing.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8::internal::wasm {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using compiler::AccessBuilder;
using compiler::CallDescriptor;
using compiler::MemoryAccessKind;
using compiler::Operator;
using compiler::TrapId;
using TSBlock = compiler::turboshaft::Block;
using compiler::turboshaft::BuiltinCallDescriptor;
using compiler::turboshaft::CallOp;
using compiler::turboshaft::ConditionWithHint;
using compiler::turboshaft::ConstantOp;
using compiler::turboshaft::ConstOrV;
using compiler::turboshaft::DidntThrowOp;
using compiler::turboshaft::Float32;
using compiler::turboshaft::Float64;
using compiler::turboshaft::Graph;
using compiler::turboshaft::Label;
using compiler::turboshaft::LoadOp;
using compiler::turboshaft::LoopLabel;
using compiler::turboshaft::MemoryRepresentation;
using compiler::turboshaft::OpEffects;
using compiler::turboshaft::Operation;
using compiler::turboshaft::OperationMatcher;
using compiler::turboshaft::OpIndex;
using compiler::turboshaft::OptionalOpIndex;
using compiler::turboshaft::PendingLoopPhiOp;
using compiler::turboshaft::RegisterRepresentation;
using compiler::turboshaft::Simd128ConstantOp;
using compiler::turboshaft::StackCheckOp;
using compiler::turboshaft::StoreOp;
using compiler::turboshaft::SupportedOperations;
using compiler::turboshaft::V;
using compiler::turboshaft::Variable;
using compiler::turboshaft::WasmTypeAnnotationOp;
using compiler::turboshaft::WasmTypeCastOp;
using compiler::turboshaft::Word32;
using compiler::turboshaft::WordPtr;
using compiler::turboshaft::WordRepresentation;

namespace {

ExternalArrayType GetExternalArrayType(DataViewOp op_type) {
  switch (op_type) {
#define V(Name)                \
  case DataViewOp::kGet##Name: \
  case DataViewOp::kSet##Name: \
    return kExternal##Name##Array;
    DATAVIEW_OP_LIST(V)
#undef V
    case DataViewOp::kByteLength:
      UNREACHABLE();
  }
}

size_t GetTypeSize(DataViewOp op_type) {
  ExternalArrayType array_type = GetExternalArrayType(op_type);
  switch (array_type) {
#define ELEMENTS_KIND_TO_ELEMENT_SIZE(Type, type, TYPE, ctype) \
  case kExternal##Type##Array:                                 \
    return sizeof(ctype);

    TYPED_ARRAYS(ELEMENTS_KIND_TO_ELEMENT_SIZE)
#undef ELEMENTS_KIND_TO_ELEMENT_SIZE
  }
}

bool ReverseBytesSupported(size_t size_in_bytes) {
  switch (size_in_bytes) {
    case 4:
    case 16:
      return true;
    case 8:
      return Is64();
    default:
      return false;
  }
}

}  // namespace

// TODO(14108): Annotate runtime functions as not having side effects
// where appropriate.
OpIndex WasmGraphBuilderBase::CallRuntime(
    Zone* zone, Runtime::FunctionId f,
    std::initializer_list<const OpIndex> args, V<Context> context) {
  const Runtime::Function* fun = Runtime::FunctionForId(f);
  OpIndex isolate_root = __ LoadRootRegister();
  DCHECK_EQ(1, fun->result_size);
  int builtin_slot_offset = IsolateData::BuiltinSlotOffset(
      Builtin::kCEntry_Return1_ArgvOnStack_NoBuiltinExit);
  OpIndex centry_stub =
      __ Load(isolate_root, LoadOp::Kind::RawAligned(),
              MemoryRepresentation::UintPtr(), builtin_slot_offset);
  // CallRuntime is always called with 0 or 1 argument, so a vector of size 4
  // always suffices.
  SmallZoneVector<OpIndex, 4> centry_args(zone);
  for (OpIndex arg : args) centry_args.emplace_back(arg);
  centry_args.emplace_back(__ ExternalConstant(ExternalReference::Create(f)));
  centry_args.emplace_back(__ Word32Constant(fun->nargs));
  centry_args.emplace_back(context);
  const CallDescriptor* call_descriptor =
      compiler::Linkage::GetRuntimeCallDescriptor(
          __ graph_zone(), f, fun->nargs, Operator::kNoProperties,
          CallDescriptor::kNoFlags);
  const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
      call_descriptor, compiler::CanThrow::kYes, __ graph_zone());
  return __ Call(centry_stub, OpIndex::Invalid(), base::VectorOf(centry_args),
                 ts_call_descriptor);
}

OpIndex WasmGraphBuilderBase::GetBuiltinPointerTarget(Builtin builtin) {
  static_assert(std::is_same<Smi, BuiltinPtr>(), "BuiltinPtr must be Smi");
  return __ SmiConstant(Smi::FromInt(static_cast<int>(builtin)));
}

V<WordPtr> WasmGraphBuilderBase::GetTargetForBuiltinCall(
    Builtin builtin, StubCallMode stub_mode) {
  return stub_mode == StubCallMode::kCallWasmRuntimeStub
             ? __ RelocatableWasmBuiltinCallTarget(builtin)
             : GetBuiltinPointerTarget(builtin);
}

V<BigInt> WasmGraphBuilderBase::BuildChangeInt64ToBigInt(
    V<Word64> input, StubCallMode stub_mode) {
  Builtin builtin = Is64() ? Builtin::kI64ToBigInt : Builtin::kI32PairToBigInt;
  V<WordPtr> target = GetTargetForBuiltinCall(builtin, stub_mode);
  CallInterfaceDescriptor interface_descriptor =
      Builtins::CallInterfaceDescriptorFor(builtin);
  const CallDescriptor* call_descriptor =
      compiler::Linkage::GetStubCallDescriptor(
          __ graph_zone(),  // zone
          interface_descriptor,
          0,                         // stack parameter count
          CallDescriptor::kNoFlags,  // flags
          Operator::kNoProperties,   // properties
          stub_mode);
  const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
      call_descriptor, compiler::CanThrow::kNo, __ graph_zone());
  if constexpr (Is64()) {
    return __ Call(target, {input}, ts_call_descriptor);
  }
  V<Word32> low_word = __ TruncateWord64ToWord32(input);
  V<Word32> high_word = __ TruncateWord64ToWord32(__ ShiftRightLogical(
      input, __ Word32Constant(32), WordRepresentation::Word64()));
  return __ Call(target, {low_word, high_word}, ts_call_descriptor);
}

std::pair<V<WordPtr>, V<HeapObject>>
WasmGraphBuilderBase::BuildImportedFunctionTargetAndRef(
    ConstOrV<Word32> func_index,
    V<WasmTrustedInstanceData> trusted_instance_data) {
  // Imported function.
  V<ProtectedFixedArray> imported_function_refs =
      V<ProtectedFixedArray>::Cast(LOAD_PROTECTED_INSTANCE_FIELD(
          trusted_instance_data, ImportedFunctionRefs));
  auto ref = V<HeapObject>::Cast(
      func_index.is_constant()
          ? __ LoadProtectedFixedArrayElement(imported_function_refs,
                                              func_index.constant_value())
          : __ LoadProtectedFixedArrayElement(
                imported_function_refs,
                __ ChangeUint32ToUintPtr(func_index.value())));
  V<FixedAddressArray> imported_targets = LOAD_IMMUTABLE_INSTANCE_FIELD(
      trusted_instance_data, ImportedFunctionTargets,
      MemoryRepresentation::TaggedPointer());
  V<WordPtr> target =
      func_index.is_constant()
          ? __ Load(imported_targets, LoadOp::Kind::TaggedBase(),
                    MemoryRepresentation::UintPtr(),
                    FixedAddressArray::OffsetOfElementAt(
                        func_index.constant_value()))
          : __
            Load(imported_targets, __ ChangeUint32ToUintPtr(func_index.value()),
                 LoadOp::Kind::TaggedBase(), MemoryRepresentation::UintPtr(),
                 FixedAddressArray::OffsetOfElementAt(0),
                 kSystemPointerSizeLog2);
  return {target, ref};
}

RegisterRepresentation WasmGraphBuilderBase::RepresentationFor(ValueType type) {
  switch (type.kind()) {
    case kI8:
    case kI16:
    case kI32:
      return RegisterRepresentation::Word32();
    case kI64:
      return RegisterRepresentation::Word64();
    case kF32:
      return RegisterRepresentation::Float32();
    case kF64:
      return RegisterRepresentation::Float64();
    case kRefNull:
    case kRef:
      return RegisterRepresentation::Tagged();
    case kS128:
      return RegisterRepresentation::Simd128();
    case kVoid:
    case kRtt:
    case kBottom:
      UNREACHABLE();
  }
}

// Load the trusted data from a WasmInstanceObject.
V<WasmTrustedInstanceData>
WasmGraphBuilderBase::LoadTrustedDataFromInstanceObject(
    V<HeapObject> instance_object) {
  return V<WasmTrustedInstanceData>::Cast(__ LoadTrustedPointerField(
      instance_object, LoadOp::Kind::TaggedBase().Immutable(),
      kWasmTrustedInstanceDataIndirectPointerTag,
      WasmInstanceObject::kTrustedDataOffset));
}

void WasmGraphBuilderBase::BuildModifyThreadInWasmFlagHelper(
    Zone* zone, OpIndex thread_in_wasm_flag_address, bool new_value) {
  if (v8_flags.debug_code) {
    V<Word32> flag_value =
        __ Load(thread_in_wasm_flag_address, LoadOp::Kind::RawAligned(),
                MemoryRepresentation::Int32(), 0);

    IF (UNLIKELY(__ Word32Equal(flag_value, new_value))) {
      OpIndex message_id = __ TaggedIndexConstant(static_cast<int32_t>(
          new_value ? AbortReason::kUnexpectedThreadInWasmSet
                    : AbortReason::kUnexpectedThreadInWasmUnset));
      CallRuntime(zone, Runtime::kAbort, {message_id}, __ NoContextConstant());
      __ Unreachable();
    }
  }

  __ Store(thread_in_wasm_flag_address, __ Word32Constant(new_value),
           LoadOp::Kind::RawAligned(), MemoryRepresentation::Int32(),
           compiler::kNoWriteBarrier);
}

void WasmGraphBuilderBase::BuildModifyThreadInWasmFlag(Zone* zone,
                                                       bool new_value) {
  if (!trap_handler::IsTrapHandlerEnabled()) return;

  OpIndex isolate_root = __ LoadRootRegister();
  OpIndex thread_in_wasm_flag_address =
      __ Load(isolate_root, LoadOp::Kind::RawAligned().Immutable(),
              MemoryRepresentation::UintPtr(),
              Isolate::thread_in_wasm_flag_address_offset());
  BuildModifyThreadInWasmFlagHelper(zone, thread_in_wasm_flag_address,
                                    new_value);
}

// TODO(14108): Annotate C functions as not having side effects where
// appropriate.
OpIndex WasmGraphBuilderBase::CallC(const MachineSignature* sig,
                                    ExternalReference ref,
                                    std::initializer_list<OpIndex> args) {
  DCHECK_LE(sig->return_count(), 1);
  DCHECK_EQ(sig->parameter_count(), args.size());
  const CallDescriptor* call_descriptor =
      compiler::Linkage::GetSimplifiedCDescriptor(__ graph_zone(), sig);
  const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
      call_descriptor, compiler::CanThrow::kNo, __ graph_zone());
  return __ Call(__ ExternalConstant(ref), OpIndex::Invalid(),
                 base::VectorOf(args), ts_call_descriptor);
}

class TurboshaftGraphBuildingInterface : public WasmGraphBuilderBase {
 private:
  class BlockPhis;
  class InstanceCache;

 public:
  enum Mode {
    kRegular,
    kInlinedUnhandled,
    kInlinedWithCatch,
    kInlinedTailCall
  };
  using ValidationTag = Decoder::FullValidationTag;
  using FullDecoder =
      WasmFullDecoder<ValidationTag, TurboshaftGraphBuildingInterface>;
  static constexpr bool kUsesPoppedArgs = true;
  static constexpr MemoryRepresentation kMaybeSandboxedPointer =
      V8_ENABLE_SANDBOX_BOOL ? MemoryRepresentation::SandboxedPointer()
                             : MemoryRepresentation::UintPtr();

  struct Value : public ValueBase<ValidationTag> {
    OpIndex op = OpIndex::Invalid();
    template <typename... Args>
    explicit Value(Args&&... args) V8_NOEXCEPT
        : ValueBase(std::forward<Args>(args)...) {}
  };

  struct Control : public ControlBase<Value, ValidationTag> {
    TSBlock* merge_block = nullptr;
    // for 'if', loops, and 'try'/'try-table' respectively.
    TSBlock* false_or_loop_or_catch_block = nullptr;
    BitVector* assigned = nullptr;             // Only for loops.
    V<Object> exception = OpIndex::Invalid();  // Only for 'try-catch'.

    template <typename... Args>
    explicit Control(Args&&... args) V8_NOEXCEPT
        : ControlBase(std::forward<Args>(args)...) {}
  };

 public:
  // For non-inlined functions.
  TurboshaftGraphBuildingInterface(
      Zone* zone, CompilationEnv* env, Assembler& assembler,
      AssumptionsJournal* assumptions,
      ZoneVector<WasmInliningPosition>* inlining_positions, int func_index,
      const WireBytesStorage* wire_bytes)
      : WasmGraphBuilderBase(zone, assembler),
        mode_(kRegular),
        block_phis_(zone),
        env_(env),
        owned_instance_cache_(std::make_unique<InstanceCache>(assembler)),
        instance_cache_(*owned_instance_cache_.get()),
        assumptions_(assumptions),
        inlining_positions_(inlining_positions),
        ssa_env_(zone),
        func_index_(func_index),
        wire_bytes_(wire_bytes),
        return_phis_(nullptr),
        is_inlined_tail_call_(false) {}

  // For inlined functions.
  TurboshaftGraphBuildingInterface(
      Zone* zone, CompilationEnv* env, Assembler& assembler, Mode mode,
      InstanceCache& instance_cache, AssumptionsJournal* assumptions,
      ZoneVector<WasmInliningPosition>* inlining_positions, int func_index,
      const WireBytesStorage* wire_bytes, base::Vector<OpIndex> real_parameters,
      TSBlock* return_block, BlockPhis* return_phis, TSBlock* catch_block,
      bool is_inlined_tail_call)
      : WasmGraphBuilderBase(zone, assembler),
        mode_(mode),
        block_phis_(zone),
        env_(env),
        instance_cache_(instance_cache),
        assumptions_(assumptions),
        inlining_positions_(inlining_positions),
        ssa_env_(zone),
        func_index_(func_index),
        wire_bytes_(wire_bytes),
        real_parameters_(real_parameters),
        return_block_(return_block),
        return_phis_(return_phis),
        return_catch_block_(catch_block),
        is_inlined_tail_call_(is_inlined_tail_call) {
    DCHECK_NE(mode_, kRegular);
    DCHECK_EQ(return_block == nullptr, mode == kInlinedTailCall);
    DCHECK_EQ(catch_block != nullptr, mode == kInlinedWithCatch);
  }

  void StartFunction(FullDecoder* decoder) {
    if (mode_ == kRegular) __ Bind(__ NewBlock());
    // Set 0 as the current source position (before locals declarations).
    __ SetCurrentOrigin(WasmPositionToOpIndex(0, inlining_id_));
    ssa_env_.resize(decoder->num_locals());
    uint32_t index = 0;
    V<WasmTrustedInstanceData> trusted_instance_data;
    if (mode_ == kRegular) {
      static_assert(kWasmInstanceParameterIndex == 0);
      trusted_instance_data = __ WasmInstanceParameter();
      for (; index < decoder->sig_->parameter_count(); index++) {
        // Parameter indices are shifted by 1 because parameter 0 is the
        // instance.
        ssa_env_[index] = __ Parameter(
            index + 1, RepresentationFor(decoder->sig_->GetParam(index)));
      }
      instance_cache_.Initialize(trusted_instance_data, decoder->module_);
    } else {
      trusted_instance_data = real_parameters_[0];
      for (; index < decoder->sig_->parameter_count(); index++) {
        // Parameter indices are shifted by 1 because parameter 0 is the
        // instance.
        ssa_env_[index] = real_parameters_[index + 1];
      }
      if (!is_inlined_tail_call_) {
        return_phis_->InitReturnPhis(decoder->sig_->returns(), instance_cache_);
      }
    }
    while (index < decoder->num_locals()) {
      ValueType type = decoder->local_type(index);
      OpIndex op;
      if (!type.is_defaultable()) {
        DCHECK(type.is_reference());
        // TODO(jkummerow): Consider using "the hole" instead, to make any
        // illegal uses more obvious.
        op = __ Null(type.AsNullable());
      } else {
        op = DefaultValue(type);
      }
      while (index < decoder->num_locals() &&
             decoder->local_type(index) == type) {
        ssa_env_[index++] = op;
      }
    }

    if (inlining_enabled(decoder)) {
      if (mode_ == kRegular) {
        if (v8_flags.liftoff) {
          inlining_decisions_ = decoder->zone_->New<InliningTree>(
              decoder->zone_, decoder->module_, func_index_,
              0,  // call count
              0,  // wire byte size. We pass 0 so that the initial node is
                  // always expanded, regardless of budget.
              func_index_,
              // Pass dummy values for caller, feedback slot, and case.
              -1, -1, -1,
              0  // inlining depth
          );
          inlining_decisions_->FullyExpand(
              decoder->module_->functions[func_index_].code.length());
        } else {
          set_no_liftoff_inlining_budget(std::max(
              static_cast<int>(v8_flags.wasm_inlining_min_budget),
              static_cast<int>(
                  v8_flags.wasm_inlining_factor *
                  decoder->module_->functions[func_index_].code.length())));
        }
      } else {
#if DEBUG
        if (v8_flags.liftoff && inlining_decisions_) {
          // DCHECK that `inlining_decisions_` is consistent.
          DCHECK(inlining_decisions_->is_inlined());
          DCHECK_EQ(inlining_decisions_->function_index(), func_index_);
          base::SharedMutexGuard<base::kShared> mutex_guard(
              &decoder->module_->type_feedback.mutex);
          if (inlining_decisions_->feedback_found()) {
            DCHECK_NE(
                decoder->module_->type_feedback.feedback_for_function.find(
                    func_index_),
                decoder->module_->type_feedback.feedback_for_function.end());
            DCHECK_EQ(inlining_decisions_->function_calls().size(),
                      decoder->module_->type_feedback.feedback_for_function
                          .find(func_index_)
                          ->second.feedback_vector.size());
            DCHECK_EQ(inlining_decisions_->function_calls().size(),
                      decoder->module_->type_feedback.feedback_for_function
                          .find(func_index_)
                          ->second.call_targets.size());
          }
        }
#endif
      }
    }

    if (v8_flags.debug_code) {
      IF_NOT (LIKELY(__ HasInstanceType(trusted_instance_data,
                                        WASM_TRUSTED_INSTANCE_DATA_TYPE))) {
        OpIndex message_id = __ TaggedIndexConstant(
            static_cast<int32_t>(AbortReason::kUnexpectedInstanceType));
        CallRuntime(decoder->zone(), Runtime::kAbort, {message_id},
                    __ NoContextConstant());
        __ Unreachable();
      }
    }

    if (mode_ == kRegular) {
      StackCheck(StackCheckOp::CheckKind::kFunctionHeaderCheck);
    }

    if (v8_flags.trace_wasm) {
      __ SetCurrentOrigin(
          WasmPositionToOpIndex(decoder->position(), inlining_id_));
      CallRuntime(decoder->zone(), Runtime::kWasmTraceEnter, {},
                  __ NoContextConstant());
    }

    auto branch_hints_it = decoder->module_->branch_hints.find(func_index_);
    if (branch_hints_it != decoder->module_->branch_hints.end()) {
      branch_hints_ = &branch_hints_it->second;
    }
  }

  void StartFunctionBody(FullDecoder* decoder, Control* block) {}

  void FinishFunction(FullDecoder* decoder) {
    if (v8_flags.liftoff && inlining_decisions_ &&
        inlining_decisions_->feedback_found()) {
      DCHECK_EQ(
          feedback_slot_,
          static_cast<int>(inlining_decisions_->function_calls().size()) - 1);
    }
    if (mode_ == kRegular) {
      // Just accessing `source_positions` at the maximum `OpIndex` already
      // pre-allocates the underlying storage such that we avoid repeatedly
      // resizing/copying in the following loop.
      __ output_graph().source_positions()[__ output_graph().EndIndex()];

      for (OpIndex index : __ output_graph().AllOperationIndices()) {
        SourcePosition position = OpIndexToSourcePosition(
            __ output_graph().operation_origins()[index]);
        __ output_graph().source_positions()[index] = position;
      }
    }
  }

  void OnFirstError(FullDecoder*) {}

  void NextInstruction(FullDecoder* decoder, WasmOpcode) {
    __ SetCurrentOrigin(
        WasmPositionToOpIndex(decoder->position(), inlining_id_));
  }

  // ******** Control Flow ********
  // The basic structure of control flow is {block_phis_}. It contains a mapping
  // from blocks to phi inputs corresponding to the SSA values plus the stack
  // merge values at the beginning of the block.
  // - When we create a new block (to be bound in the future), we register it to
  //   {block_phis_} with {NewBlockWithPhis}.
  // - When we encounter an jump to a block, we invoke {SetupControlFlowEdge}.
  // - Finally, when we bind a block, we setup its phis, the SSA environment,
  //   and its merge values, with {BindBlockAndGeneratePhis}.
  // - When we create a loop, we generate PendingLoopPhis for the SSA state and
  //   the incoming stack values. We also create a block which will act as a
  //   merge block for all loop backedges (since a loop in Turboshaft can only
  //   have one backedge). When we PopControl a loop, we enter the merge block
  //   to create its Phis for all backedges as necessary, and use those values
  //   to patch the backedge of the PendingLoopPhis of the loop.

  void Block(FullDecoder* decoder, Control* block) {
    block->merge_block = NewBlockWithPhis(decoder, block->br_merge());
  }

  void Loop(FullDecoder* decoder, Control* block) {
    TSBlock* loop = __ NewLoopHeader();
    __ Goto(loop);
    __ Bind(loop);

    bool can_be_innermost = false;  // unused
    BitVector* assigned = WasmDecoder<ValidationTag>::AnalyzeLoopAssignment(
        decoder, decoder->pc(), decoder->num_locals(), decoder->zone(),
        &can_be_innermost);
    block->assigned = assigned;

    for (uint32_t i = 0; i < decoder->num_locals(); i++) {
      if (!assigned->Contains(i)) continue;
      OpIndex phi = __ PendingLoopPhi(
          ssa_env_[i], RepresentationFor(decoder->local_type(i)));
      ssa_env_[i] = phi;
    }
    uint32_t arity = block->start_merge.arity;
    Value* stack_base = arity > 0 ? decoder->stack_value(arity) : nullptr;
    for (uint32_t i = 0; i < arity; i++) {
      OpIndex phi = __ PendingLoopPhi(stack_base[i].op,
                                      RepresentationFor(stack_base[i].type));
      block->start_merge[i].op = phi;
    }
    if (assigned->Contains(decoder->num_locals())) {
      uint32_t cached_values = instance_cache_.num_mutable_fields();
      for (uint32_t i = 0; i < cached_values; i++) {
        OpIndex phi = __ PendingLoopPhi(
            instance_cache_.mutable_field_value(i),
            RepresentationFor(instance_cache_.mutable_field_type(i)));
        instance_cache_.set_mutable_field_value(i, phi);
      }
    }

    StackCheck(StackCheckOp::CheckKind::kLoopCheck);

    TSBlock* loop_merge = NewBlockWithPhis(decoder, &block->start_merge);
    block->merge_block = loop_merge;
    block->false_or_loop_or_catch_block = loop;
  }

  void If(FullDecoder* decoder, const Value& cond, Control* if_block) {
    TSBlock* true_block = __ NewBlock();
    TSBlock* false_block = NewBlockWithPhis(decoder, nullptr);
    TSBlock* merge_block = NewBlockWithPhis(decoder, &if_block->end_merge);
    if_block->false_or_loop_or_catch_block = false_block;
    if_block->merge_block = merge_block;
    SetupControlFlowEdge(decoder, false_block);
    __ Branch({cond.op, GetBranchHint(decoder)}, true_block, false_block);
    __ Bind(true_block);
  }

  void Else(FullDecoder* decoder, Control* if_block) {
    if (if_block->reachable()) {
      SetupControlFlowEdge(decoder, if_block->merge_block);
      __ Goto(if_block->merge_block);
    }
    BindBlockAndGeneratePhis(decoder, if_block->false_or_loop_or_catch_block,
                             nullptr);
  }

  void BrOrRet(FullDecoder* decoder, uint32_t depth, uint32_t drop_values = 0) {
    if (depth == decoder->control_depth() - 1) {
      DoReturn(decoder, drop_values);
    } else {
      Control* target = decoder->control_at(depth);
      SetupControlFlowEdge(decoder, target->merge_block, drop_values);
      __ Goto(target->merge_block);
    }
  }

  void BrIf(FullDecoder* decoder, const Value& cond, uint32_t depth) {
    BranchHint hint = GetBranchHint(decoder);
    if (depth == decoder->control_depth() - 1) {
      IF ({cond.op, hint}) {
        DoReturn(decoder, 0);
      }
    } else {
      Control* target = decoder->control_at(depth);
      SetupControlFlowEdge(decoder, target->merge_block);
      TSBlock* non_branching = __ NewBlock();
      __ Branch({cond.op, hint}, target->merge_block, non_branching);
      __ Bind(non_branching);
    }
  }

  void BrTable(FullDecoder* decoder, const BranchTableImmediate& imm,
               const Value& key) {
    compiler::turboshaft::SwitchOp::Case* cases =
        __ output_graph()
            .graph_zone()
            ->AllocateArray<compiler::turboshaft::SwitchOp::Case>(
                imm.table_count);
    BranchTableIterator<ValidationTag> new_block_iterator(decoder, imm);
    SmallZoneVector<TSBlock*, 16> intermediate_blocks(decoder->zone_);
    TSBlock* default_case = nullptr;
    while (new_block_iterator.has_next()) {
      TSBlock* intermediate = __ NewBlock();
      intermediate_blocks.emplace_back(intermediate);
      uint32_t i = new_block_iterator.cur_index();
      if (i == imm.table_count) {
        default_case = intermediate;
      } else {
        cases[i] = {static_cast<int>(i), intermediate, BranchHint::kNone};
      }
      new_block_iterator.next();
    }
    DCHECK_NOT_NULL(default_case);
    __ Switch(key.op, base::VectorOf(cases, imm.table_count), default_case);

    int i = 0;
    BranchTableIterator<ValidationTag> branch_iterator(decoder, imm);
    while (branch_iterator.has_next()) {
      TSBlock* intermediate = intermediate_blocks[i];
      i++;
      __ Bind(intermediate);
      BrOrRet(decoder, branch_iterator.next());
    }
  }

  void FallThruTo(FullDecoder* decoder, Control* block) {
    // TODO(14108): Why is {block->reachable()} not reliable here? Maybe it is
    // not in other spots as well.
    if (__ current_block() != nullptr) {
      SetupControlFlowEdge(decoder, block->merge_block);
      __ Goto(block->merge_block);
    }
  }

  void PopControl(FullDecoder* decoder, Control* block) {
    switch (block->kind) {
      case kControlIf:
        if (block->reachable()) {
          SetupControlFlowEdge(decoder, block->merge_block);
          __ Goto(block->merge_block);
        }
        BindBlockAndGeneratePhis(decoder, block->false_or_loop_or_catch_block,
                                 nullptr);
        // Exceptionally for one-armed if, we cannot take the values from the
        // stack; we have to pass the stack values at the beginning of the
        // if-block.
        SetupControlFlowEdge(decoder, block->merge_block, 0, OpIndex::Invalid(),
                             &block->start_merge);
        __ Goto(block->merge_block);
        BindBlockAndGeneratePhis(decoder, block->merge_block,
                                 block->br_merge());
        break;
      case kControlIfElse:
      case kControlBlock:
      case kControlTry:
      case kControlTryCatch:
      case kControlTryCatchAll:
        // {block->reachable()} is not reliable here for exceptions, because
        // the decoder sets the reachability to the upper block's reachability
        // before calling this interface function.
        if (__ current_block() != nullptr) {
          SetupControlFlowEdge(decoder, block->merge_block);
          __ Goto(block->merge_block);
        }
        BindBlockAndGeneratePhis(decoder, block->merge_block,
                                 block->br_merge());
        break;
      case kControlTryTable:
        DCHECK_EQ(__ current_block(), nullptr);
        BindBlockAndGeneratePhis(decoder, block->merge_block,
                                 block->br_merge());
        break;
      case kControlLoop: {
        TSBlock* post_loop = NewBlockWithPhis(decoder, nullptr);
        if (block->reachable()) {
          SetupControlFlowEdge(decoder, post_loop);
          __ Goto(post_loop);
        }
        if (!block->false_or_loop_or_catch_block->IsBound()) {
          // The loop is unreachable. In this case, no operations have been
          // emitted for it. Do nothing.
        } else if (block->merge_block->PredecessorCount() == 0) {
          // Turns out, the loop has no backedges, i.e. it is not quite a loop
          // at all. Replace it with a merge, and its PendingPhis with one-input
          // phis.
          block->false_or_loop_or_catch_block->SetKind(
              compiler::turboshaft::Block::Kind::kMerge);
          auto to = __ output_graph()
                        .operations(*block->false_or_loop_or_catch_block)
                        .begin();
          bool contains_instance_cache =
              block->assigned->Contains(decoder->num_locals());
          size_t num_phis =
              block->assigned->Count() - (contains_instance_cache ? 1 : 0) +
              block->br_merge()->arity +
              (contains_instance_cache ? instance_cache_.num_mutable_fields()
                                       : 0);
          for (uint32_t i = 0; i < num_phis; ++i, ++to) {
            // TODO(manoskouk): Add `->` operator to the iterator.
            PendingLoopPhiOp& pending_phi = (*to).Cast<PendingLoopPhiOp>();
            OpIndex replaced = __ output_graph().Index(*to);
            __ output_graph().Replace<compiler::turboshaft::PhiOp>(
                replaced, base::VectorOf({pending_phi.first()}),
                pending_phi.rep);
          }
        } else {
          // We abuse the start merge of the loop, which is not used otherwise
          // anymore, to store backedge inputs for the pending phi stack values
          // of the loop.
          BindBlockAndGeneratePhis(decoder, block->merge_block,
                                   block->br_merge());
          __ Goto(block->false_or_loop_or_catch_block);
          auto to = __ output_graph()
                        .operations(*block->false_or_loop_or_catch_block)
                        .begin();
          for (auto it = block->assigned->begin(); it != block->assigned->end();
               ++it, ++to) {
            // The last bit represents the instance cache.
            if (*it == static_cast<int>(ssa_env_.size())) break;
            PendingLoopPhiOp& pending_phi = (*to).Cast<PendingLoopPhiOp>();
            OpIndex replaced = __ output_graph().Index(*to);
            __ output_graph().Replace<compiler::turboshaft::PhiOp>(
                replaced, base::VectorOf({pending_phi.first(), ssa_env_[*it]}),
                pending_phi.rep);
          }
          for (uint32_t i = 0; i < block->br_merge()->arity; ++i, ++to) {
            PendingLoopPhiOp& pending_phi = (*to).Cast<PendingLoopPhiOp>();
            OpIndex replaced = __ output_graph().Index(*to);
            __ output_graph().Replace<compiler::turboshaft::PhiOp>(
                replaced,
                base::VectorOf(
                    {pending_phi.first(), (*block->br_merge())[i].op}),
                pending_phi.rep);
          }
          if (block->assigned->Contains(decoder->num_locals())) {
            for (uint32_t i = 0; i < instance_cache_.num_mutable_fields();
                 ++i, ++to) {
              PendingLoopPhiOp& pending_phi = (*to).Cast<PendingLoopPhiOp>();
              OpIndex replaced = __ output_graph().Index(*to);
              __ output_graph().Replace<compiler::turboshaft::PhiOp>(
                  replaced,
                  base::VectorOf({pending_phi.first(),
                                  instance_cache_.mutable_field_value(i)}),
                  pending_phi.rep);
            }
          }
        }
        BindBlockAndGeneratePhis(decoder, post_loop, nullptr);
        break;
      }
    }
  }

  void DoReturn(FullDecoder* decoder, uint32_t drop_values) {
    size_t return_count = decoder->sig_->return_count();
    SmallZoneVector<OpIndex, 16> return_values(return_count, decoder->zone_);
    Value* stack_base = return_count == 0
                            ? nullptr
                            : decoder->stack_value(static_cast<uint32_t>(
                                  return_count + drop_values));
    for (size_t i = 0; i < return_count; i++) {
      return_values[i] = stack_base[i].op;
    }
    if (v8_flags.trace_wasm) {
      V<WordPtr> info = __ IntPtrConstant(0);
      if (return_count == 1) {
        wasm::ValueType return_type = decoder->sig_->GetReturn(0);
        int size = return_type.value_kind_size();
        // TODO(14108): This won't fit everything.
        info = __ StackSlot(size, size);
        // TODO(14108): Write barrier might be needed.
        __ Store(
            info, return_values[0], StoreOp::Kind::RawAligned(),
            MemoryRepresentation::FromMachineType(return_type.machine_type()),
            compiler::kNoWriteBarrier);
      }
      CallRuntime(decoder->zone(), Runtime::kWasmTraceExit, {info},
                  __ NoContextConstant());
    }
    if (mode_ == kRegular || mode_ == kInlinedTailCall) {
      __ Return(__ Word32Constant(0), base::VectorOf(return_values));
    } else {
      // Do not add return values if we are in unreachable code.
      if (__ generating_unreachable_operations()) return;
      for (size_t i = 0; i < return_count; i++) {
        return_phis_->AddInputForPhi(i, return_values[i]);
      }
      uint32_t cached_values = instance_cache_.num_mutable_fields();
      for (uint32_t i = 0; i < cached_values; i++) {
        return_phis_->AddInputForPhi(return_count + i,
                                     instance_cache_.mutable_field_value(i));
      }
      __ Goto(return_block_);
    }
  }

  void UnOp(FullDecoder* decoder, WasmOpcode opcode, const Value& value,
            Value* result) {
    result->op = UnOpImpl(opcode, value.op, value.type);
  }

  void BinOp(FullDecoder* decoder, WasmOpcode opcode, const Value& lhs,
             const Value& rhs, Value* result) {
    result->op = BinOpImpl(opcode, lhs.op, rhs.op);
  }

  void TraceInstruction(FullDecoder* decoder, uint32_t markid) {
    // TODO(14108): Implement.
  }

  void I32Const(FullDecoder* decoder, Value* result, int32_t value) {
    result->op = __ Word32Constant(value);
  }

  void I64Const(FullDecoder* decoder, Value* result, int64_t value) {
    result->op = __ Word64Constant(value);
  }

  void F32Const(FullDecoder* decoder, Value* result, float value) {
    result->op = __ Float32Constant(value);
  }

  void F64Const(FullDecoder* decoder, Value* result, double value) {
    result->op = __ Float64Constant(value);
  }

  void S128Const(FullDecoder* decoder, const Simd128Immediate& imm,
                 Value* result) {
    result->op = __ Simd128Constant(imm.value);
  }

  void RefNull(FullDecoder* decoder, ValueType type, Value* result) {
    result->op = __ Null(type);
  }

  void RefFunc(FullDecoder* decoder, uint32_t function_index, Value* result) {
    result->op = __ WasmRefFunc(trusted_instance_data(), function_index);
  }

  void RefAsNonNull(FullDecoder* decoder, const Value& arg, Value* result) {
    result->op =
        __ AssertNotNull(arg.op, arg.type, TrapId::kTrapNullDereference);
  }

  void Drop(FullDecoder* decoder) {}

  void LocalGet(FullDecoder* decoder, Value* result,
                const IndexImmediate& imm) {
    result->op = ssa_env_[imm.index];
  }

  void LocalSet(FullDecoder* decoder, const Value& value,
                const IndexImmediate& imm) {
    ssa_env_[imm.index] = value.op;
  }

  void LocalTee(FullDecoder* decoder, const Value& value, Value* result,
                const IndexImmediate& imm) {
    ssa_env_[imm.index] = result->op = value.op;
  }

  void GlobalGet(FullDecoder* decoder, Value* result,
                 const GlobalIndexImmediate& imm) {
    result->op = __ GlobalGet(trusted_instance_data(), imm.global);
  }

  void GlobalSet(FullDecoder* decoder, const Value& value,
                 const GlobalIndexImmediate& imm) {
    __ GlobalSet(trusted_instance_data(), value.op, imm.global);
  }

  void Trap(FullDecoder* decoder, TrapReason reason) {
    __ TrapIfNot(__ Word32Constant(0), OpIndex::Invalid(),
                 GetTrapIdForTrap(reason));
    __ Unreachable();
  }

  void AssertNullTypecheck(FullDecoder* decoder, const Value& obj,
                           Value* result) {
    __ TrapIfNot(__ IsNull(obj.op, obj.type), OpIndex::Invalid(),
                 TrapId::kTrapIllegalCast);
    Forward(decoder, obj, result);
  }

  void AssertNotNullTypecheck(FullDecoder* decoder, const Value& obj,
                              Value* result) {
    __ AssertNotNull(obj.op, obj.type, TrapId::kTrapIllegalCast);
    Forward(decoder, obj, result);
  }

  void NopForTestingUnsupportedInLiftoff(FullDecoder* decoder) {
    Bailout(decoder);
  }

  void Select(FullDecoder* decoder, const Value& cond, const Value& fval,
              const Value& tval, Value* result) {
    using Implementation = compiler::turboshaft::SelectOp::Implementation;
    bool use_select = false;
    switch (tval.type.kind()) {
      case kI32:
        if (SupportedOperations::word32_select()) use_select = true;
        break;
      case kI64:
        if (SupportedOperations::word64_select()) use_select = true;
        break;
      case kF32:
        if (SupportedOperations::float32_select()) use_select = true;
        break;
      case kF64:
        if (SupportedOperations::float64_select()) use_select = true;
        break;
      case kRef:
      case kRefNull:
      case kS128:
        break;
      case kI8:
      case kI16:
      case kRtt:
      case kVoid:
      case kBottom:
        UNREACHABLE();
    }
    result->op = __ Select(
        cond.op, tval.op, fval.op, RepresentationFor(tval.type),
        BranchHint::kNone,
        use_select ? Implementation::kCMove : Implementation::kBranch);
  }

  OpIndex BuildChangeEndiannessStore(OpIndex node,
                                     MachineRepresentation mem_rep,
                                     wasm::ValueType wasmtype) {
    OpIndex result;
    OpIndex value = node;
    int value_size_in_bytes = wasmtype.value_kind_size();
    int value_size_in_bits = 8 * value_size_in_bytes;
    bool is_float = false;

    switch (wasmtype.kind()) {
      case wasm::kF64:
        value = __ BitcastFloat64ToWord64(node);
        is_float = true;
        [[fallthrough]];
      case wasm::kI64:
        result = __ Word64Constant(static_cast<uint64_t>(0));
        break;
      case wasm::kF32:
        value = __ BitcastFloat32ToWord32(node);
        is_float = true;
        [[fallthrough]];
      case wasm::kI32:
        result = __ Word32Constant(0);
        break;
      case wasm::kS128:
        DCHECK(ReverseBytesSupported(value_size_in_bytes));
        break;
      default:
        UNREACHABLE();
    }

    if (mem_rep == MachineRepresentation::kWord8) {
      // No need to change endianness for byte size, return original node
      return node;
    }
    if (wasmtype == wasm::kWasmI64 &&
        mem_rep < MachineRepresentation::kWord64) {
      // In case we store lower part of WasmI64 expression, we can truncate
      // upper 32bits.
      value_size_in_bytes = wasm::kWasmI32.value_kind_size();
      value_size_in_bits = 8 * value_size_in_bytes;
      if (mem_rep == MachineRepresentation::kWord16) {
        value = __ Word32ShiftLeft(value, 16);
      }
    } else if (wasmtype == wasm::kWasmI32 &&
               mem_rep == MachineRepresentation::kWord16) {
      value = __ Word32ShiftLeft(value, 16);
    }

    int i;
    uint32_t shift_count;

    if (ReverseBytesSupported(value_size_in_bytes)) {
      switch (value_size_in_bytes) {
        case 4:
          result = __ Word32ReverseBytes(value);
          break;
        case 8:
          result = __ Word64ReverseBytes(value);
          break;
        case 16:
          result = __ Simd128ReverseBytes(value);
          break;
        default:
          UNREACHABLE();
      }
    } else {
      for (i = 0, shift_count = value_size_in_bits - 8;
           i < value_size_in_bits / 2; i += 8, shift_count -= 16) {
        OpIndex shift_lower;
        OpIndex shift_higher;
        OpIndex lower_byte;
        OpIndex higher_byte;

        DCHECK_LT(0, shift_count);
        DCHECK_EQ(0, (shift_count + 8) % 16);

        if (value_size_in_bits > 32) {
          shift_lower = __ Word64ShiftLeft(value, shift_count);
          shift_higher = __ Word64ShiftRightLogical(value, shift_count);
          lower_byte = __ Word64BitwiseAnd(shift_lower,
                                           static_cast<uint64_t>(0xFF)
                                               << (value_size_in_bits - 8 - i));
          higher_byte = __ Word64BitwiseAnd(shift_higher,
                                            static_cast<uint64_t>(0xFF) << i);
          result = __ Word64BitwiseOr(result, lower_byte);
          result = __ Word64BitwiseOr(result, higher_byte);
        } else {
          shift_lower = __ Word32ShiftLeft(value, shift_count);
          shift_higher = __ Word32ShiftRightLogical(value, shift_count);
          lower_byte = __ Word32BitwiseAnd(shift_lower,
                                           static_cast<uint32_t>(0xFF)
                                               << (value_size_in_bits - 8 - i));
          higher_byte = __ Word32BitwiseAnd(shift_higher,
                                            static_cast<uint32_t>(0xFF) << i);
          result = __ Word32BitwiseOr(result, lower_byte);
          result = __ Word32BitwiseOr(result, higher_byte);
        }
      }
    }

    if (is_float) {
      switch (wasmtype.kind()) {
        case wasm::kF64:
          result = __ BitcastWord64ToFloat64(result);
          break;
        case wasm::kF32:
          result = __ BitcastWord32ToFloat32(result);
          break;
        default:
          UNREACHABLE();
      }
    }

    return result;
  }

  OpIndex BuildChangeEndiannessLoad(OpIndex node, MachineType memtype,
                                    wasm::ValueType wasmtype) {
    OpIndex result;
    OpIndex value = node;
    int value_size_in_bytes = ElementSizeInBytes(memtype.representation());
    int value_size_in_bits = 8 * value_size_in_bytes;
    bool is_float = false;

    switch (memtype.representation()) {
      case MachineRepresentation::kFloat64:
        value = __ BitcastFloat64ToWord64(node);
        is_float = true;
        [[fallthrough]];
      case MachineRepresentation::kWord64:
        result = __ Word64Constant(static_cast<uint64_t>(0));
        break;
      case MachineRepresentation::kFloat32:
        value = __ BitcastFloat32ToWord32(node);
        is_float = true;
        [[fallthrough]];
      case MachineRepresentation::kWord32:
      case MachineRepresentation::kWord16:
        result = __ Word32Constant(0);
        break;
      case MachineRepresentation::kWord8:
        // No need to change endianness for byte size, return original node
        return node;
      case MachineRepresentation::kSimd128:
        DCHECK(ReverseBytesSupported(value_size_in_bytes));
        break;
      default:
        UNREACHABLE();
    }

    int i;
    uint32_t shift_count;

    if (ReverseBytesSupported(value_size_in_bytes < 4 ? 4
                                                      : value_size_in_bytes)) {
      switch (value_size_in_bytes) {
        case 2:
          result = __ Word32ReverseBytes(__ Word32ShiftLeft(value, 16));
          break;
        case 4:
          result = __ Word32ReverseBytes(value);
          break;
        case 8:
          result = __ Word64ReverseBytes(value);
          break;
        case 16:
          result = __ Simd128ReverseBytes(value);
          break;
        default:
          UNREACHABLE();
      }
    } else {
      for (i = 0, shift_count = value_size_in_bits - 8;
           i < value_size_in_bits / 2; i += 8, shift_count -= 16) {
        OpIndex shift_lower;
        OpIndex shift_higher;
        OpIndex lower_byte;
        OpIndex higher_byte;

        DCHECK_LT(0, shift_count);
        DCHECK_EQ(0, (shift_count + 8) % 16);

        if (value_size_in_bits > 32) {
          shift_lower = __ Word64ShiftLeft(value, shift_count);
          shift_higher = __ Word64ShiftRightLogical(value, shift_count);
          lower_byte = __ Word64BitwiseAnd(shift_lower,
                                           static_cast<uint64_t>(0xFF)
                                               << (value_size_in_bits - 8 - i));
          higher_byte = __ Word64BitwiseAnd(shift_higher,
                                            static_cast<uint64_t>(0xFF) << i);
          result = __ Word64BitwiseOr(result, lower_byte);
          result = __ Word64BitwiseOr(result, higher_byte);
        } else {
          shift_lower = __ Word32ShiftLeft(value, shift_count);
          shift_higher = __ Word32ShiftRightLogical(value, shift_count);
          lower_byte = __ Word32BitwiseAnd(shift_lower,
                                           static_cast<uint32_t>(0xFF)
                                               << (value_size_in_bits - 8 - i));
          higher_byte = __ Word32BitwiseAnd(shift_higher,
                                            static_cast<uint32_t>(0xFF) << i);
          result = __ Word32BitwiseOr(result, lower_byte);
          result = __ Word32BitwiseOr(result, higher_byte);
        }
      }
    }

    if (is_float) {
      switch (memtype.representation()) {
        case MachineRepresentation::kFloat64:
          result = __ BitcastWord64ToFloat64(result);
          break;
        case MachineRepresentation::kFloat32:
          result = __ BitcastWord32ToFloat32(result);
          break;
        default:
          UNREACHABLE();
      }
    }

    // We need to sign or zero extend the value.
    if (memtype.IsSigned()) {
      DCHECK(!is_float);
      if (value_size_in_bits < 32) {
        // Perform sign extension using following trick
        // result = (x << machine_width - type_width) >> (machine_width -
        // type_width)
        if (wasmtype == wasm::kWasmI64) {
          int shift_bit_count = 64 - value_size_in_bits;
          result = __ Word64ShiftRightArithmeticShiftOutZeros(
              __ Word64ShiftLeft(__ ChangeInt32ToInt64(result),
                                 shift_bit_count),
              shift_bit_count);
        } else if (wasmtype == wasm::kWasmI32) {
          int shift_bit_count = 32 - value_size_in_bits;
          result = __ Word32ShiftRightArithmeticShiftOutZeros(
              __ Word32ShiftLeft(result, shift_bit_count), shift_bit_count);
        }
      }
    } else if (wasmtype == wasm::kWasmI64 && value_size_in_bits < 64) {
      result = __ ChangeUint32ToUint64(result);
    }

    return result;
  }

  void LoadMem(FullDecoder* decoder, LoadType type,
               const MemoryAccessImmediate& imm, const Value& index,
               Value* result) {
    MemoryRepresentation repr =
        MemoryRepresentation::FromMachineType(type.mem_type());

    auto [final_index, strategy] =
        BoundsCheckMem(imm.memory, repr, index.op, imm.offset,
                       compiler::EnforceBoundsCheck::kCanOmitBoundsCheck,
                       compiler::AlignmentCheck::kNo);

    V<WordPtr> mem_start = MemStart(imm.memory->index);

    LoadOp::Kind load_kind = GetMemoryAccessKind(repr, strategy);

    const bool offset_in_int_range =
        imm.offset <= std::numeric_limits<int32_t>::max();
    OpIndex base =
        offset_in_int_range ? mem_start : __ WordPtrAdd(mem_start, imm.offset);
    int32_t offset = offset_in_int_range ? static_cast<int32_t>(imm.offset) : 0;
    OpIndex load = __ Load(base, final_index, load_kind, repr, offset);

#if V8_TARGET_BIG_ENDIAN
    load = BuildChangeEndiannessLoad(load, type.mem_type(), type.value_type());
#endif

    OpIndex extended_load =
        (type.value_type() == kWasmI64 && repr.SizeInBytes() < 8)
            ? (repr.IsSigned() ? __ ChangeInt32ToInt64(load)
                               : __ ChangeUint32ToUint64(load))
            : load;

    if (v8_flags.trace_wasm_memory) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(decoder, false, repr, final_index, imm.offset);
    }

    result->op = extended_load;
  }

  void LoadTransform(FullDecoder* decoder, LoadType type,
                     LoadTransformationKind transform,
                     const MemoryAccessImmediate& imm, const Value& index,
                     Value* result) {
#if defined(V8_TARGET_BIG_ENDIAN)
    // TODO(14108): Implement for big endian.
    Bailout(decoder);
#endif
    MemoryRepresentation repr =
        transform == LoadTransformationKind::kExtend
            ? MemoryRepresentation::Int64()
            : MemoryRepresentation::FromMachineType(type.mem_type());

    auto [final_index, strategy] =
        BoundsCheckMem(imm.memory, repr, index.op, imm.offset,
                       compiler::EnforceBoundsCheck::kCanOmitBoundsCheck,
                       compiler::AlignmentCheck::kNo);

    compiler::turboshaft::Simd128LoadTransformOp::LoadKind load_kind =
        GetMemoryAccessKind(repr, strategy);

    using TransformKind =
        compiler::turboshaft::Simd128LoadTransformOp::TransformKind;

    TransformKind transform_kind;

    if (transform == LoadTransformationKind::kExtend) {
      if (type.mem_type() == MachineType::Int8()) {
        transform_kind = TransformKind::k8x8S;
      } else if (type.mem_type() == MachineType::Uint8()) {
        transform_kind = TransformKind::k8x8U;
      } else if (type.mem_type() == MachineType::Int16()) {
        transform_kind = TransformKind::k16x4S;
      } else if (type.mem_type() == MachineType::Uint16()) {
        transform_kind = TransformKind::k16x4U;
      } else if (type.mem_type() == MachineType::Int32()) {
        transform_kind = TransformKind::k32x2S;
      } else if (type.mem_type() == MachineType::Uint32()) {
        transform_kind = TransformKind::k32x2U;
      } else {
        UNREACHABLE();
      }
    } else if (transform == LoadTransformationKind::kSplat) {
      if (type.mem_type() == MachineType::Int8()) {
        transform_kind = TransformKind::k8Splat;
      } else if (type.mem_type() == MachineType::Int16()) {
        transform_kind = TransformKind::k16Splat;
      } else if (type.mem_type() == MachineType::Int32()) {
        transform_kind = TransformKind::k32Splat;
      } else if (type.mem_type() == MachineType::Int64()) {
        transform_kind = TransformKind::k64Splat;
      } else {
        UNREACHABLE();
      }
    } else {
      if (type.mem_type() == MachineType::Int32()) {
        transform_kind = TransformKind::k32Zero;
      } else if (type.mem_type() == MachineType::Int64()) {
        transform_kind = TransformKind::k64Zero;
      } else {
        UNREACHABLE();
      }
    }

    OpIndex load = __ Simd128LoadTransform(
        __ WordPtrAdd(MemStart(imm.mem_index), imm.offset), final_index,
        load_kind, transform_kind, 0);

    if (v8_flags.trace_wasm_memory) {
      TraceMemoryOperation(decoder, false, repr, final_index, imm.offset);
    }

    result->op = load;
  }

  void LoadLane(FullDecoder* decoder, LoadType type, const Value& value,
                const Value& index, const MemoryAccessImmediate& imm,
                const uint8_t laneidx, Value* result) {
    using compiler::turboshaft::Simd128LaneMemoryOp;
#if defined(V8_TARGET_BIG_ENDIAN)
    // TODO(14108): Implement for big endian.
    Bailout(decoder);
#endif

    MemoryRepresentation repr =
        MemoryRepresentation::FromMachineType(type.mem_type());

    auto [final_index, strategy] =
        BoundsCheckMem(imm.memory, repr, index.op, imm.offset,
                       compiler::EnforceBoundsCheck::kCanOmitBoundsCheck,
                       compiler::AlignmentCheck::kNo);
    Simd128LaneMemoryOp::Kind kind = GetMemoryAccessKind(repr, strategy);

    Simd128LaneMemoryOp::LaneKind lane_kind;

    switch (repr) {
      case MemoryRepresentation::Int8():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k8;
        break;
      case MemoryRepresentation::Int16():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k16;
        break;
      case MemoryRepresentation::Int32():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k32;
        break;
      case MemoryRepresentation::Int64():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k64;
        break;
      default:
        UNREACHABLE();
    }

    // TODO(14108): If `offset` is in int range, use it as static offset, or
    // consider using a larger type as offset.
    OpIndex load = __ Simd128LaneMemory(
        __ WordPtrAdd(MemStart(imm.mem_index), imm.offset), final_index,
        value.op, Simd128LaneMemoryOp::Mode::kLoad, kind, lane_kind, laneidx,
        0);

    if (v8_flags.trace_wasm_memory) {
      TraceMemoryOperation(decoder, false, repr, final_index, imm.offset);
    }

    result->op = load;
  }

  void StoreMem(FullDecoder* decoder, StoreType type,
                const MemoryAccessImmediate& imm, const Value& index,
                const Value& value) {
    MemoryRepresentation repr =
        MemoryRepresentation::FromMachineRepresentation(type.mem_rep());

    auto [final_index, strategy] =
        BoundsCheckMem(imm.memory, repr, index.op, imm.offset,
                       wasm::kPartialOOBWritesAreNoops
                           ? compiler::EnforceBoundsCheck::kCanOmitBoundsCheck
                           : compiler::EnforceBoundsCheck::kNeedsBoundsCheck,
                       compiler::AlignmentCheck::kNo);

    V<WordPtr> mem_start = MemStart(imm.memory->index);

    StoreOp::Kind store_kind = GetMemoryAccessKind(repr, strategy);

    OpIndex store_value = value.op;
    if (value.type == kWasmI64 && repr.SizeInBytes() <= 4) {
      store_value = __ TruncateWord64ToWord32(store_value);
    }

#if defined(V8_TARGET_BIG_ENDIAN)
    store_value = BuildChangeEndiannessStore(store_value, type.mem_rep(),
                                             type.value_type());

#endif
    const bool offset_in_int_range =
        imm.offset <= std::numeric_limits<int32_t>::max();
    OpIndex base =
        offset_in_int_range ? mem_start : __ WordPtrAdd(mem_start, imm.offset);
    int32_t offset = offset_in_int_range ? static_cast<int32_t>(imm.offset) : 0;
    __ Store(base, final_index, store_value, store_kind, repr,
             compiler::kNoWriteBarrier, offset);

    if (v8_flags.trace_wasm_memory) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(decoder, true, repr, final_index, imm.offset);
    }
  }

  void StoreLane(FullDecoder* decoder, StoreType type,
                 const MemoryAccessImmediate& imm, const Value& index,
                 const Value& value, const uint8_t laneidx) {
    using compiler::turboshaft::Simd128LaneMemoryOp;
#if defined(V8_TARGET_BIG_ENDIAN)
    // TODO(14108): Implement for big endian.
    Bailout(decoder);
#endif

    MemoryRepresentation repr =
        MemoryRepresentation::FromMachineRepresentation(type.mem_rep());

    auto [final_index, strategy] =
        BoundsCheckMem(imm.memory, repr, index.op, imm.offset,
                       kPartialOOBWritesAreNoops
                           ? compiler::EnforceBoundsCheck::kCanOmitBoundsCheck
                           : compiler::EnforceBoundsCheck::kNeedsBoundsCheck,
                       compiler::AlignmentCheck::kNo);
    Simd128LaneMemoryOp::Kind kind = GetMemoryAccessKind(repr, strategy);

    Simd128LaneMemoryOp::LaneKind lane_kind;

    switch (repr) {
      // TODO(manoskouk): Why use unsigned representations here as opposed to
      // LoadLane?
      case MemoryRepresentation::Uint8():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k8;
        break;
      case MemoryRepresentation::Uint16():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k16;
        break;
      case MemoryRepresentation::Uint32():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k32;
        break;
      case MemoryRepresentation::Uint64():
        lane_kind = Simd128LaneMemoryOp::LaneKind::k64;
        break;
      default:
        UNREACHABLE();
    }

    // TODO(14108): If `offset` is in int range, use it as static offset, or
    // consider using a larger type as offset.
    __ Simd128LaneMemory(__ WordPtrAdd(MemStart(imm.mem_index), imm.offset),
                         final_index, value.op,
                         Simd128LaneMemoryOp::Mode::kStore, kind, lane_kind,
                         laneidx, 0);

    if (v8_flags.trace_wasm_memory) {
      TraceMemoryOperation(decoder, true, repr, final_index, imm.offset);
    }
  }

  void CurrentMemoryPages(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                          Value* result) {
    V<WordPtr> result_wordptr =
        __ WordPtrShiftRightArithmetic(MemSize(imm.index), kWasmPageSizeLog2);
    // In the 32-bit case, truncation happens implicitly.
    if (imm.memory->is_memory64) {
      result->op = __ ChangeIntPtrToInt64(result_wordptr);
    } else {
      result->op = __ TruncateWordPtrToWord32(result_wordptr);
    }
  }

  void MemoryGrow(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value& value, Value* result) {
    if (!imm.memory->is_memory64) {
      result->op =
          CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmMemoryGrow>(
              decoder, {__ Word32Constant(imm.index), value.op});
    } else {
      Label<Word64> done(&asm_);

      IF (LIKELY(__ Uint64LessThanOrEqual(
              value.op, __ Word64Constant(static_cast<int64_t>(kMaxInt))))) {
        GOTO(done, __ ChangeInt32ToInt64(CallBuiltinThroughJumptable<
                                         BuiltinCallDescriptor::WasmMemoryGrow>(
                       decoder, {__ Word32Constant(imm.index),
                                 __ TruncateWord64ToWord32(value.op)})));
      } ELSE {
        GOTO(done, __ Word64Constant(int64_t{-1}));
      }

      BIND(done, result_64);

      result->op = result_64;
    }
    instance_cache_.ReloadCachedMemory();
  }

  V<Word32> IsExternRefString(const Value value) {
    compiler::WasmTypeCheckConfig config{value.type, kWasmRefExternString};
    V<Map> rtt = OpIndex::Invalid();
    return __ WasmTypeCheck(value.op, rtt, config);
  }

  V<String> ExternRefToString(const Value value, bool null_succeeds = false) {
    wasm::ValueType target_type =
        null_succeeds ? kWasmRefNullExternString : kWasmRefExternString;
    compiler::WasmTypeCheckConfig config{value.type, target_type};
    V<Map> rtt = OpIndex::Invalid();
    return V<String>::Cast(__ WasmTypeCast(value.op, rtt, config));
  }

  bool IsExplicitStringCast(const Value value) {
    if (__ generating_unreachable_operations()) return false;
    const WasmTypeCastOp* cast =
        __ output_graph().Get(value.op).TryCast<WasmTypeCastOp>();
    return cast && cast->config.to == kWasmRefExternString;
  }

  V<Word32> GetStringIndexOf(FullDecoder* decoder, V<String> string,
                             V<String> search, V<Word32> start) {
    // Clamp the start index.
    Label<Word32> clamped_start_label(&asm_);
    GOTO_IF(__ Int32LessThan(start, 0), clamped_start_label,
            __ Word32Constant(0));
    V<Word32> length = __ template LoadField<Word32>(
        string, compiler::AccessBuilder::ForStringLength());
    GOTO_IF(__ Int32LessThan(start, length), clamped_start_label, start);
    GOTO(clamped_start_label, length);
    BIND(clamped_start_label, clamped_start);
    start = clamped_start;

    // This can't overflow because we've clamped `start` above.
    V<Smi> start_smi = __ TagSmi(start);
    BuildModifyThreadInWasmFlag(decoder->zone(), false);

    V<Smi> result_value =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::StringIndexOf>(
            decoder, {string, search, start_smi});
    BuildModifyThreadInWasmFlag(decoder->zone(), true);

    return __ UntagSmi(result_value);
  }

#if V8_INTL_SUPPORT
  V<String> CallStringToLowercase(FullDecoder* decoder, V<String> string) {
    BuildModifyThreadInWasmFlag(decoder->zone(), false);
    OpIndex result = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::StringToLowerCaseIntl>(
        decoder, __ NoContextConstant(), {string});
    BuildModifyThreadInWasmFlag(decoder->zone(), true);
    return result;
  }
#endif

  void SetDataViewOpForErrorMessage(DataViewOp op_type) {
    OpIndex isolate_root = __ LoadRootRegister();
    __ Store(isolate_root, __ Word32Constant(op_type),
             StoreOp::Kind::RawAligned(), MemoryRepresentation::Uint8(),
             compiler::kNoWriteBarrier, Isolate::error_message_param_offset());
  }

  void ThrowDataViewTypeError(FullDecoder* decoder, V<Object> dataview,
                              DataViewOp op_type) {
    SetDataViewOpForErrorMessage(op_type);
    CallBuiltinThroughJumptable<BuiltinCallDescriptor::ThrowDataViewTypeError>(
        decoder, {V<JSDataView>::Cast(dataview)});
    __ Unreachable();
  }

  void ThrowDataViewOutOfBoundsError(FullDecoder* decoder, DataViewOp op_type) {
    SetDataViewOpForErrorMessage(op_type);
    CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::ThrowDataViewOutOfBounds>(decoder, {});
    __ Unreachable();
  }

  void ThrowDataViewDetachedError(FullDecoder* decoder, DataViewOp op_type) {
    SetDataViewOpForErrorMessage(op_type);
    CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::ThrowDataViewDetachedError>(decoder, {});
    __ Unreachable();
  }

  void DataViewRangeCheck(FullDecoder* decoder, V<WordPtr> left,
                          V<WordPtr> right, DataViewOp op_type) {
    IF (UNLIKELY(__ IntPtrLessThan(left, right))) {
      ThrowDataViewOutOfBoundsError(decoder, op_type);
    }
  }

  void DataViewBoundsCheck(FullDecoder* decoder, V<WordPtr> left,
                           V<WordPtr> right, DataViewOp op_type) {
    IF (UNLIKELY(__ IntPtrLessThan(left, right))) {
      ThrowDataViewDetachedError(decoder, op_type);
    }
  }

  V<Word32> IsDetached(V<Object> dataview) {
    // TODO(evih): Make the buffer load immutable.
    V<Object> buffer = __ LoadField<Object>(
        dataview, compiler::AccessBuilder::ForJSArrayBufferViewBuffer());
    V<Word32> bit_field = __ LoadField<Word32>(
        buffer, compiler::AccessBuilder::ForJSArrayBufferBitField());
    return __ Word32BitwiseAnd(bit_field, JSArrayBuffer::WasDetachedBit::kMask);
  }

  void DataViewDetachedBufferCheck(FullDecoder* decoder, V<Object> dataview,
                                   DataViewOp op_type) {
    IF (UNLIKELY(IsDetached(dataview))) {
      ThrowDataViewDetachedError(decoder, op_type);
    }
  }

  V<WordPtr> GetDataViewByteLength(FullDecoder* decoder, V<Object> dataview,
                                   DataViewOp op_type) {
    DCHECK_EQ(op_type, DataViewOp::kByteLength);
    return GetDataViewByteLength(decoder, dataview, __ IntPtrConstant(0),
                                 op_type);
  }

  // Converts a Smi or HeapNumber to an intptr. The input is not validated.
  V<WordPtr> ChangeTaggedNumberToIntPtr(V<Object> tagged) {
    Label<> smi_label(&asm_);
    Label<> heapnumber_label(&asm_);
    Label<WordPtr> done_label(&asm_);

    GOTO_IF(LIKELY(__ IsSmi(tagged)), smi_label);
    GOTO(heapnumber_label);

    BIND(smi_label);
    V<WordPtr> smi_length =
        __ ChangeInt32ToIntPtr(__ UntagSmi(V<Smi>::Cast(tagged)));
    GOTO(done_label, smi_length);

    BIND(heapnumber_label);
    V<Float64> float_value = __ template LoadField<Float64>(
        tagged, AccessBuilder::ForHeapNumberValue());
    if constexpr (Is64()) {
      DCHECK_EQ(WordPtr::bits, Word64::bits);
      GOTO(done_label,
           V<WordPtr>::Cast(
               __ TruncateFloat64ToInt64OverflowUndefined(float_value)));
    } else {
      GOTO(done_label,
           __ ChangeInt32ToIntPtr(
               __ TruncateFloat64ToInt32OverflowUndefined(float_value)));
    }

    BIND(done_label, length);
    return length;
  }

  // An `ArrayBuffer` can be resizable, i.e. it can shrink or grow.
  // A `SharedArrayBuffer` can be growable, i.e. it can only grow. A `DataView`
  // can be length-tracking or non-legth-tracking . A length-tracking `DataView`
  // is tracking the length of the underlying buffer, i.e. it doesn't have a
  // `byteLength` specified, which means that the length of the `DataView` is
  // the length (or remaining length if `byteOffset != 0`) of the underlying
  // array buffer. On the other hand, a non-length-tracking `DataView` has a
  // `byteLength`.
  // Depending on whether the buffer is resizable or growable and the `DataView`
  // is length-tracking or non-length-tracking, getting the byte length has to
  // be handled differently.
  V<WordPtr> GetDataViewByteLength(FullDecoder* decoder, V<Object> dataview,
                                   V<WordPtr> offset, DataViewOp op_type) {
    Label<WordPtr> done_label(&asm_);
    Label<> rab_ltgsab_label(&asm_);
    Label<> type_error_label(&asm_);

    GOTO_IF(UNLIKELY(__ IsSmi(dataview)), type_error_label);

    // Case 1):
    //  - non-resizable ArrayBuffers, length-tracking and non-length-tracking
    //  - non-growable SharedArrayBuffers, length-tracking and non-length-tr.
    //  - growable SharedArrayBuffers, non-length-tracking
    GOTO_IF_NOT(
        LIKELY(__ HasInstanceType(dataview, InstanceType::JS_DATA_VIEW_TYPE)),
        rab_ltgsab_label);
    if (op_type != DataViewOp::kByteLength) {
      DataViewRangeCheck(decoder, offset, __ IntPtrConstant(0), op_type);
    }
    DataViewDetachedBufferCheck(decoder, dataview, op_type);
    V<WordPtr> view_byte_length = __ LoadField<WordPtr>(
        dataview, AccessBuilder::ForJSArrayBufferViewByteLength());
    GOTO(done_label, view_byte_length);

    // Case 2):
    // - resizable ArrayBuffers, length-tracking and non-length-tracking
    // - growable SharedArrayBuffers, length-tracking
    BIND(rab_ltgsab_label);
    GOTO_IF_NOT(LIKELY(__ HasInstanceType(
                    dataview, InstanceType::JS_RAB_GSAB_DATA_VIEW_TYPE)),
                type_error_label);
    if (op_type != DataViewOp::kByteLength) {
      DataViewRangeCheck(decoder, offset, __ IntPtrConstant(0), op_type);
    }
    DataViewDetachedBufferCheck(decoder, dataview, op_type);

    V<Word32> bit_field = __ LoadField<Word32>(
        dataview, AccessBuilder::ForJSArrayBufferViewBitField());
    V<Word32> length_tracking = __ Word32BitwiseAnd(
        bit_field, JSArrayBufferView::IsLengthTrackingBit::kMask);
    V<Word32> backed_by_rab_bit = __ Word32BitwiseAnd(
        bit_field, JSArrayBufferView::IsBackedByRabBit::kMask);

    V<Object> buffer = __ LoadField<Object>(
        dataview, compiler::AccessBuilder::ForJSArrayBufferViewBuffer());
    V<WordPtr> buffer_byte_length = __ LoadField<WordPtr>(
        buffer, AccessBuilder::ForJSArrayBufferByteLength());
    V<WordPtr> view_byte_offset = __ LoadField<WordPtr>(
        dataview, AccessBuilder::ForJSArrayBufferViewByteOffset());

    // The final length for each case in Case 2) is calculated differently.
    // Case: resizable ArrayBuffers, LT and non-LT.
    IF (backed_by_rab_bit) {
      // DataViews with resizable ArrayBuffers can go out of bounds.
      IF (length_tracking) {
        ScopedVar<WordPtr> final_length(this, 0);
        IF (LIKELY(__ UintPtrLessThanOrEqual(view_byte_offset,
                                             buffer_byte_length))) {
          final_length = __ WordPtrSub(buffer_byte_length, view_byte_offset);
        }
        DataViewBoundsCheck(decoder, buffer_byte_length, view_byte_offset,
                            op_type);
        GOTO(done_label, final_length);
      } ELSE {
        V<WordPtr> view_byte_length = __ LoadField<WordPtr>(
            dataview, AccessBuilder::ForJSArrayBufferViewByteLength());
        DataViewBoundsCheck(decoder, buffer_byte_length,
                            __ WordPtrAdd(view_byte_offset, view_byte_length),
                            op_type);

        GOTO(done_label, view_byte_length);
      }
    }
    // Case: growable SharedArrayBuffers, LT.
    ELSE {
      V<Object> gsab_length_tagged = CallRuntime(
          decoder->zone(), Runtime::kGrowableSharedArrayBufferByteLength,
          {buffer}, __ NoContextConstant());
      V<WordPtr> gsab_length = ChangeTaggedNumberToIntPtr(gsab_length_tagged);
      ScopedVar<WordPtr> gsab_buffer_byte_length(this, 0);
      IF (LIKELY(__ UintPtrLessThanOrEqual(view_byte_offset, gsab_length))) {
        gsab_buffer_byte_length = __ WordPtrSub(gsab_length, view_byte_offset);
      }
      GOTO(done_label, gsab_buffer_byte_length);
    }
    __ Unreachable();

    BIND(type_error_label);
    ThrowDataViewTypeError(decoder, dataview, op_type);

    BIND(done_label, final_view_byte_length);
    return final_view_byte_length;
  }

  V<WordPtr> GetDataViewDataPtr(FullDecoder* decoder, V<Object> dataview,
                                V<WordPtr> offset, DataViewOp op_type) {
    V<WordPtr> view_byte_length =
        GetDataViewByteLength(decoder, dataview, offset, op_type);
    V<WordPtr> view_byte_length_minus_size =
        __ WordPtrSub(view_byte_length, GetTypeSize(op_type));
    DataViewRangeCheck(decoder, view_byte_length_minus_size, offset, op_type);
    return __ LoadField<WordPtr>(
        dataview, compiler::AccessBuilder::ForJSDataViewDataPointer());
  }

  OpIndex DataViewGetter(FullDecoder* decoder, const Value args[],
                         DataViewOp op_type) {
    V<Object> dataview = args[0].op;
    V<WordPtr> offset = __ ChangeInt32ToIntPtr(args[1].op);
    V<Word32> is_little_endian =
        (op_type == DataViewOp::kGetInt8 || op_type == DataViewOp::kGetUint8)
            ? __ Word32Constant(1)
            : args[2].op;

    V<WordPtr> data_ptr =
        GetDataViewDataPtr(decoder, dataview, offset, op_type);
    return __ LoadDataViewElement(dataview, data_ptr, offset, is_little_endian,
                                  GetExternalArrayType(op_type));
  }

  void DataViewSetter(FullDecoder* decoder, const Value args[],
                      DataViewOp op_type) {
    V<Object> dataview = args[0].op;
    V<WordPtr> offset = __ ChangeInt32ToIntPtr(args[1].op);
    V<Word32> value = args[2].op;
    V<Word32> is_little_endian =
        (op_type == DataViewOp::kSetInt8 || op_type == DataViewOp::kSetUint8)
            ? __ Word32Constant(1)
            : args[3].op;

    V<WordPtr> data_ptr =
        GetDataViewDataPtr(decoder, dataview, offset, op_type);
    __ StoreDataViewElement(dataview, data_ptr, offset, value, is_little_endian,
                            GetExternalArrayType(op_type));
  }

  // Adds a wasm type annotation to the graph and replaces any extern type with
  // the extern string type.
  V<String> AnnotateAsString(OpIndex value, wasm::ValueType type) {
    DCHECK(type.is_reference_to(HeapType::kString) ||
           type.is_reference_to(HeapType::kExternString) ||
           type.is_reference_to(HeapType::kExtern));
    if (type.is_reference_to(HeapType::kExtern)) {
      type =
          ValueType::RefMaybeNull(HeapType::kExternString, type.nullability());
    }
    return __ AnnotateWasmType(value, type);
  }

  void WellKnown_FastApi(FullDecoder* decoder, const CallFunctionImmediate& imm,
                         const Value args[], Value returns[]) {
    uint32_t func_index = imm.index;
    V<Object> receiver = args[0].op;
    V<FixedArray> imports_array =
        LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), WellKnownImports,
                                      MemoryRepresentation::TaggedPointer());
    V<Object> data = __ LoadFixedArrayElement(imports_array, func_index);
    V<Object> cached_map = __ Load(data, LoadOp::Kind::TaggedBase(),
                                   MemoryRepresentation::TaggedPointer(),
                                   WasmFastApiCallData::kCachedMapOffset);

    Label<> if_equal_maps(&asm_);
    Label<> if_unknown_receiver(&asm_);
    GOTO_IF(__ IsSmi(receiver), if_unknown_receiver);

    V<Map> map = __ LoadMapField(V<Object>::Cast(receiver));

    // Clear the weak bit.
    cached_map = __ BitcastWordPtrToTagged(
        __ WordBitwiseAnd(__ BitcastTaggedToWordPtr(cached_map),
                          __ IntPtrConstant(~kWeakHeapObjectMask),
                          WordRepresentation::WordPtr()));
    GOTO_IF(__ TaggedEqual(map, cached_map), if_equal_maps);
    GOTO(if_unknown_receiver);

    BIND(if_unknown_receiver);
    V<NativeContext> context = instance_cache_.native_context();
    CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmFastApiCallTypeCheckAndUpdateIC>(
        decoder, context, {data, receiver});
    GOTO(if_equal_maps);

    BIND(if_equal_maps);
    OpIndex receiver_handle = __ AdaptLocalArgument(receiver);
    OpIndex options_object;
    {
      const int kAlign = alignof(v8::FastApiCallbackOptions);
      const int kSize = sizeof(v8::FastApiCallbackOptions);

      options_object = __ StackSlot(kSize, kAlign);

      __ Store(options_object, __ Word32Constant(0),
               StoreOp::Kind::RawAligned(), MemoryRepresentation::Uint32(),
               compiler::kNoWriteBarrier,
               offsetof(v8::FastApiCallbackOptions, fallback));

      static_assert(
          sizeof(v8::FastApiCallbackOptions::data) == sizeof(intptr_t),
          "We expected 'data' to be pointer sized, but it is not.");
      // TODO(41492790): Provide the actual data pointer here.
      __ Store(options_object, __ IntPtrConstant(0),
               StoreOp::Kind::RawAligned(), MemoryRepresentation::UintPtr(),
               compiler::kNoWriteBarrier,
               offsetof(v8::FastApiCallbackOptions, data));

      static_assert(
          sizeof(v8::FastApiCallbackOptions::wasm_memory) == sizeof(intptr_t),
          "We expected 'wasm_memory' to be pointer sized, but it is not.");
      __ Store(options_object, __ IntPtrConstant(0),
               StoreOp::Kind::RawAligned(), MemoryRepresentation::UintPtr(),
               compiler::kNoWriteBarrier,
               offsetof(v8::FastApiCallbackOptions, wasm_memory));
    }

    const wasm::FunctionSig* sig = decoder->module_->functions[func_index].sig;
    size_t param_count = sig->parameter_count();
    // All normal parameters + the options as additional parameter at the end.
    MachineSignature::Builder builder(decoder->zone(), 1, param_count + 1);
    builder.AddReturn(sig->GetReturn().machine_type());
    // The first parameter is the receiver. Because of the fake handle on the
    // stack the type is `Pointer`.
    builder.AddParam(MachineType::Pointer());

    for (size_t i = 1; i < sig->parameter_count(); ++i) {
      builder.AddParam(sig->GetParam(i).machine_type());
    }
    // Options object.
    builder.AddParam(MachineType::Pointer());

    base::SmallVector<OpIndex, 16> inputs(param_count + 1);

    inputs[0] = receiver_handle;

    for (size_t i = 1; i < param_count; ++i) {
      if (sig->GetParam(i).is_reference()) {
        inputs[i] = __ AdaptLocalArgument(args[i].op);
      } else {
        inputs[i] = args[i].op;
      }
    }

    inputs[param_count] = options_object;

    const CallDescriptor* call_descriptor =
        compiler::Linkage::GetSimplifiedCDescriptor(__ graph_zone(),
                                                    builder.Get());
    const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
        call_descriptor, compiler::CanThrow::kNo, __ graph_zone());
    Variable result =
        __ NewVariable(RegisterRepresentation::FromMachineRepresentation(
            sig->GetReturn().machine_representation()));
    OpIndex target_address = __ ExternalConstant(ExternalReference::Create(
        env_->fast_api_targets[func_index].load(std::memory_order_relaxed),
        ExternalReference::FAST_C_CALL));
    OpIndex ret_val = __ Call(target_address, OpIndex::Invalid(),
                              base::VectorOf(inputs), ts_call_descriptor);

    if (env_->fast_api_return_is_bool[func_index]) {
      ret_val = __ WordBitwiseAnd(ret_val, __ Word32Constant(0xff),
                                  WordRepresentation::Word32());
    }
    IF (__ Load(options_object, LoadOp::Kind::RawAligned(),
                MemoryRepresentation::Uint32(),
                offsetof(v8::FastApiCallbackOptions, fallback))) {
      auto [target, ref] = BuildImportedFunctionTargetAndRef(imm.index);
      BuildWasmCall(decoder, imm.sig, target, ref, args, returns);
      __ SetVariable(result, returns[0].op);
    } ELSE {
      __ SetVariable(result, ret_val);
    }
    returns[0].op = __ GetVariable(result);
    __ SetVariable(result, OpIndex::Invalid());
  }

  bool HandleWellKnownImport(FullDecoder* decoder,
                             const CallFunctionImmediate& imm,
                             const Value args[], Value returns[]) {
    uint32_t index = imm.index;
    if (!decoder->module_) return false;  // Only needed for tests.
    const WellKnownImportsList& well_known_imports =
        decoder->module_->type_feedback.well_known_imports;
    using WKI = WellKnownImport;
    WKI imported_op = well_known_imports.get(index);
    OpIndex result;
    switch (imported_op) {
      case WKI::kUninstantiated:
      case WKI::kGeneric:
      case WKI::kLinkError:
        return false;

      // JS String Builtins proposal.
      case WKI::kStringCast: {
        result = ExternRefToString(args[0]);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringTest: {
        result = IsExternRefString(args[0]);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringCharCodeAt: {
        V<String> string = ExternRefToString(args[0]);
        V<String> view = V<String>::Cast(__ StringAsWtf16(string));
        // TODO(14108): Annotate `view`'s type.
        result = GetCodeUnitImpl(decoder, view, args[1].op);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringCodePointAt: {
        V<String> string = ExternRefToString(args[0]);
        V<String> view = V<String>::Cast(__ StringAsWtf16(string));
        // TODO(14108): Annotate `view`'s type.
        result = StringCodePointAt(decoder, view, args[1].op);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringCompare: {
        V<String> a_string = ExternRefToString(args[0]);
        V<String> b_string = ExternRefToString(args[1]);
        result = __ UntagSmi(
            CallBuiltinThroughJumptable<BuiltinCallDescriptor::StringCompare>(
                decoder, {a_string, b_string}));
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringConcat: {
        V<String> head_string = ExternRefToString(args[0]);
        V<String> tail_string = ExternRefToString(args[1]);
        V<HeapObject> native_context = instance_cache_.native_context();
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::StringAdd_CheckNone>(
            decoder, V<Context>::Cast(native_context),
            {head_string, tail_string});
        result = __ AnnotateWasmType(result, kWasmRefExternString);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringEquals: {
        // Using nullable type guards here because this instruction needs to
        // handle {null} without trapping.
        static constexpr bool kNullSucceeds = true;
        V<String> a_string = ExternRefToString(args[0], kNullSucceeds);
        V<String> b_string = ExternRefToString(args[1], kNullSucceeds);
        result = StringEqImpl(decoder, a_string, b_string, kWasmExternRef,
                              kWasmExternRef);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringFromCharCode: {
        V<Word32> capped = __ Word32BitwiseAnd(args[0].op, 0xFFFF);
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringFromCodePoint>(decoder, {capped});
        result = __ AnnotateWasmType(result, kWasmRefExternString);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringFromCodePoint:
        // TODO(14179): Fix trapping when the result is unused.
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringFromCodePoint>(decoder,
                                                            {args[0].op});
        result = __ AnnotateWasmType(result, kWasmRefExternString);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      case WKI::kStringFromWtf16Array:
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringNewWtf16Array>(
            decoder,
            {V<WasmArray>::Cast(NullCheck(args[0])), args[1].op, args[2].op});
        result = __ AnnotateWasmType(result, kWasmRefExternString);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      case WKI::kStringFromUtf8Array:
        result = StringNewWtf8ArrayImpl(
            decoder, unibrow::Utf8Variant::kLossyUtf8, args[0], args[1],
            args[2], kWasmRefExternString);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      case WKI::kStringIntoUtf8Array: {
        V<String> string = ExternRefToString(args[0]);
        result = StringEncodeWtf8ArrayImpl(
            decoder, unibrow::Utf8Variant::kLossyUtf8, string,
            V<WasmArray>::Cast(NullCheck(args[1])), args[2].op);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringToUtf8Array: {
        V<String> string = ExternRefToString(args[0]);
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringToUtf8Array>(decoder, {string});
        result = __ AnnotateWasmType(result, returns[0].type);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringLength: {
        V<Object> string = ExternRefToString(args[0]);
        result = __ template LoadField<Word32>(
            string, compiler::AccessBuilder::ForStringLength());
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringMeasureUtf8: {
        V<String> string = ExternRefToString(args[0]);
        result = StringMeasureWtf8Impl(
            decoder, unibrow::Utf8Variant::kLossyUtf8, string);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringSubstring: {
        V<String> string = ExternRefToString(args[0]);
        V<Object> view = __ StringAsWtf16(string);
        // TODO(12868): Consider annotating {view}'s type when the typing story
        //              for string views has been settled.
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringViewWtf16Slice>(
            decoder, {V<String>::Cast(view), args[1].op, args[2].op});
        result = __ AnnotateWasmType(result, kWasmRefExternString);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringToWtf16Array: {
        V<String> string = ExternRefToString(args[0]);
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringEncodeWtf16Array>(
            decoder,
            {string, V<WasmArray>::Cast(NullCheck(args[1])), args[2].op});
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }

      // Other string-related imports.
      case WKI::kDoubleToString:
        BuildModifyThreadInWasmFlag(decoder->zone(), false);
        result = CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmFloat64ToString>(decoder, {args[0].op});
        result = AnnotateAsString(result, returns[0].type);
        BuildModifyThreadInWasmFlag(decoder->zone(), true);
        decoder->detected_->Add(
            returns[0].type.is_reference_to(wasm::HeapType::kString)
                ? kFeature_stringref
                : kFeature_imported_strings);
        break;
      case WKI::kIntToString:
        BuildModifyThreadInWasmFlag(decoder->zone(), false);
        result =
            CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmIntToString>(
                decoder, {args[0].op, args[1].op});
        result = AnnotateAsString(result, returns[0].type);
        BuildModifyThreadInWasmFlag(decoder->zone(), true);
        decoder->detected_->Add(
            returns[0].type.is_reference_to(wasm::HeapType::kString)
                ? kFeature_stringref
                : kFeature_imported_strings);
        break;
      case WKI::kParseFloat: {
        if (args[0].type.is_nullable()) {
          Label<Float64> done(&asm_);
          GOTO_IF(__ IsNull(args[0].op, args[0].type), done,
                  __ Float64Constant(std::numeric_limits<double>::quiet_NaN()));

          BuildModifyThreadInWasmFlag(decoder->zone(), false);
          V<Float64> not_null_res = CallBuiltinThroughJumptable<
              BuiltinCallDescriptor::WasmStringToDouble>(decoder, {args[0].op});
          BuildModifyThreadInWasmFlag(decoder->zone(), true);
          GOTO(done, not_null_res);

          BIND(done, result_f64);
          result = result_f64;
        } else {
          BuildModifyThreadInWasmFlag(decoder->zone(), false);
          result = CallBuiltinThroughJumptable<
              BuiltinCallDescriptor::WasmStringToDouble>(decoder, {args[0].op});
          BuildModifyThreadInWasmFlag(decoder->zone(), true);
        }
        decoder->detected_->Add(kFeature_stringref);
        break;
      }
      case WKI::kStringIndexOf: {
        V<String> string = args[0].op;
        V<String> search = args[1].op;
        V<Word32> start = args[2].op;

        // If string is null, throw.
        if (args[0].type.is_nullable()) {
          IF (__ IsNull(string, args[0].type)) {
            CallBuiltinThroughJumptable<
                BuiltinCallDescriptor::ThrowIndexOfCalledOnNull>(decoder, {});
            __ Unreachable();
          }
        }

        // If search is null, replace it with "null".
        if (args[1].type.is_nullable()) {
          Label<String> search_done_label(&asm_);
          GOTO_IF_NOT(__ IsNull(search, args[1].type), search_done_label,
                      search);
          GOTO(search_done_label, LOAD_ROOT(null_string));
          BIND(search_done_label, search_value);
          search = search_value;
        }

        result = GetStringIndexOf(decoder, string, search, start);
        decoder->detected_->Add(kFeature_stringref);
        break;
      }
      case WKI::kStringIndexOfImported: {
        // As the `string` and `search` parameters are externrefs, we have to
        // make sure they are strings. To enforce this, we inline only if a
        // (successful) `"js-string":"cast"` was performed before.
        if (!(IsExplicitStringCast(args[0]) && IsExplicitStringCast(args[1]))) {
          return false;
        }
        V<String> string = args[0].op;
        V<String> search = args[1].op;
        V<Word32> start = args[2].op;

        result = GetStringIndexOf(decoder, string, search, start);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
      }
      case WKI::kStringToLocaleLowerCaseStringref:
        // TODO(14108): Implement.
        return false;
      case WKI::kStringToLowerCaseStringref: {
#if V8_INTL_SUPPORT
        V<String> string = args[0].op;
        if (args[0].type.is_nullable()) {
          IF (__ IsNull(string, args[0].type)) {
            CallBuiltinThroughJumptable<
                BuiltinCallDescriptor::ThrowToLowerCaseCalledOnNull>(decoder,
                                                                     {});
            __ Unreachable();
          }
        }
        result = CallStringToLowercase(decoder, string);
        __ AnnotateWasmType(result, kWasmRefString);
        decoder->detected_->Add(kFeature_stringref);
        break;
#else
        return false;
#endif
      }
      case WKI::kStringToLowerCaseImported: {
        // We have to make sure that the externref `string` parameter is a
        // string. To enforce this, we inline only if a (successful)
        // `"js-string":"cast"` was performed before.
#if V8_INTL_SUPPORT
        if (!IsExplicitStringCast(args[0])) {
          return false;
        }
        V<String> string = args[0].op;
        result = CallStringToLowercase(decoder, string);
        __ AnnotateWasmType(result, kWasmRefExternString);
        decoder->detected_->Add(kFeature_imported_strings);
        break;
#else
        return false;
#endif
      }

      // DataView related imports.
      // Note that we don't support DataView imports for resizable ArrayBuffers.
      case WKI::kDataViewGetBigInt64: {
        result = DataViewGetter(decoder, args, DataViewOp::kGetBigInt64);
        break;
      }
      case WKI::kDataViewGetBigUint64:
        result = DataViewGetter(decoder, args, DataViewOp::kGetBigUint64);
        break;
      case WKI::kDataViewGetFloat32:
        result = DataViewGetter(decoder, args, DataViewOp::kGetFloat32);
        break;
      case WKI::kDataViewGetFloat64:
        result = DataViewGetter(decoder, args, DataViewOp::kGetFloat64);
        break;
      case WKI::kDataViewGetInt8:
        result = DataViewGetter(decoder, args, DataViewOp::kGetInt8);
        break;
      case WKI::kDataViewGetInt16:
        result = DataViewGetter(decoder, args, DataViewOp::kGetInt16);
        break;
      case WKI::kDataViewGetInt32:
        result = DataViewGetter(decoder, args, DataViewOp::kGetInt32);
        break;
      case WKI::kDataViewGetUint8:
        result = DataViewGetter(decoder, args, DataViewOp::kGetUint8);
        break;
      case WKI::kDataViewGetUint16:
        result = DataViewGetter(decoder, args, DataViewOp::kGetUint16);
        break;
      case WKI::kDataViewGetUint32:
        result = DataViewGetter(decoder, args, DataViewOp::kGetUint32);
        break;
      case WKI::kDataViewSetBigInt64:
        DataViewSetter(decoder, args, DataViewOp::kSetBigInt64);
        break;
      case WKI::kDataViewSetBigUint64:
        DataViewSetter(decoder, args, DataViewOp::kSetBigUint64);
        break;
      case WKI::kDataViewSetFloat32:
        DataViewSetter(decoder, args, DataViewOp::kSetFloat32);
        break;
      case WKI::kDataViewSetFloat64:
        DataViewSetter(decoder, args, DataViewOp::kSetFloat64);
        break;
      case WKI::kDataViewSetInt8:
        DataViewSetter(decoder, args, DataViewOp::kSetInt8);
        break;
      case WKI::kDataViewSetInt16:
        DataViewSetter(decoder, args, DataViewOp::kSetInt16);
        break;
      case WKI::kDataViewSetInt32:
        DataViewSetter(decoder, args, DataViewOp::kSetInt32);
        break;
      case WKI::kDataViewSetUint8:
        DataViewSetter(decoder, args, DataViewOp::kSetUint8);
        break;
      case WKI::kDataViewSetUint16:
        DataViewSetter(decoder, args, DataViewOp::kSetUint16);
        break;
      case WKI::kDataViewSetUint32:
        DataViewSetter(decoder, args, DataViewOp::kSetUint32);
        break;
      case WKI::kDataViewByteLength: {
        V<Object> dataview = args[0].op;

        V<WordPtr> view_byte_length =
            GetDataViewByteLength(decoder, dataview, DataViewOp::kByteLength);
        if constexpr (Is64()) {
          result =
              __ ChangeInt64ToFloat64(__ ChangeIntPtrToInt64(view_byte_length));
        } else {
          result = __ ChangeInt32ToFloat64(
              __ TruncateWordPtrToWord32(view_byte_length));
        }
        break;
      }
      case WKI::kFastAPICall: {
        WellKnown_FastApi(decoder, imm, args, returns);
        result = returns[0].op;
      }
    }
    if (v8_flags.trace_wasm_inlining) {
      PrintF("[function %d: call to %d is well-known %s]\n", func_index_, index,
             WellKnownImportName(imported_op));
    }
    assumptions_->RecordAssumption(index, imported_op);
    returns[0].op = result;
    return true;
  }

  void CallDirect(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[], Value returns[]) {
    feedback_slot_++;
    if (imm.index < decoder->module_->num_imported_functions) {
      if (HandleWellKnownImport(decoder, imm, args, returns)) {
        return;
      }
      auto [target, ref] = BuildImportedFunctionTargetAndRef(imm.index);
      BuildWasmCall(decoder, imm.sig, target, ref, args, returns);
    } else {
      // Locally defined function.
      if (inlining_enabled(decoder) &&
          should_inline(decoder, feedback_slot_,
                        decoder->module_->functions[imm.index].code.length())) {
        if (v8_flags.trace_wasm_inlining) {
          PrintF("[function %d%s: inlining direct call #%d to function %d]\n",
                 func_index_, mode_ == kRegular ? "" : " (inlined)",
                 feedback_slot_, imm.index);
        }
        InlineWasmCall(decoder, imm.index, imm.sig, 0, false, args, returns);
      } else {
        V<WordPtr> callee =
            __ RelocatableConstant(imm.index, RelocInfo::WASM_CALL);
        BuildWasmCall(decoder, imm.sig, callee, trusted_instance_data(), args,
                      returns);
      }
    }
  }

  void ReturnCall(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[]) {
    feedback_slot_++;
    if (imm.index < decoder->module_->num_imported_functions) {
      auto [target, ref] = BuildImportedFunctionTargetAndRef(imm.index);
      BuildWasmMaybeReturnCall(decoder, imm.sig, target, ref, args);
    } else {
      // Locally defined function.
      if (inlining_enabled(decoder) &&
          should_inline(decoder, feedback_slot_,
                        decoder->module_->functions[imm.index].code.length())) {
        if (v8_flags.trace_wasm_inlining) {
          PrintF(
              "[function %d%s: inlining direct tail call #%d to function %d]\n",
              func_index_, mode_ == kRegular ? "" : " (inlined)",
              feedback_slot_, imm.index);
        }
        InlineWasmCall(decoder, imm.index, imm.sig, 0, true, args, nullptr);
      } else {
        BuildWasmMaybeReturnCall(
            decoder, imm.sig,
            __ RelocatableConstant(imm.index, RelocInfo::WASM_CALL),
            trusted_instance_data(), args);
      }
    }
  }

  void CallIndirect(FullDecoder* decoder, const Value& index,
                    const CallIndirectImmediate& imm, const Value args[],
                    Value returns[]) {
    auto [target, ref] = BuildIndirectCallTargetAndRef(decoder, index.op, imm);
    BuildWasmCall(decoder, imm.sig, target, ref, args, returns);
  }

  void ReturnCallIndirect(FullDecoder* decoder, const Value& index,
                          const CallIndirectImmediate& imm,
                          const Value args[]) {
    auto [target, ref] = BuildIndirectCallTargetAndRef(decoder, index.op, imm);
    BuildWasmMaybeReturnCall(decoder, imm.sig, target, ref, args);
  }

  void CallRef(FullDecoder* decoder, const Value& func_ref,
               const FunctionSig* sig, const Value args[], Value returns[]) {
    feedback_slot_++;
    if (__ generating_unreachable_operations()) return;
    if (inlining_enabled(decoder) &&
        should_inline(decoder, feedback_slot_,
                      std::numeric_limits<int>::max())) {
      V<FixedArray> func_refs =
          LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), FuncRefs,
                                        MemoryRepresentation::TaggedPointer());
      size_t return_count = sig->return_count();
      base::Vector<InliningTree*> feedback_cases =
          inlining_decisions_->function_calls()[feedback_slot_];
      std::vector<base::SmallVector<OpIndex, 2>> case_returns(return_count);
      base::SmallVector<TSBlock*, 5> case_blocks;
      for (size_t i = 0; i < feedback_cases.size() + 1; i++) {
        case_blocks.push_back(__ NewBlock());
      }
      TSBlock* merge = __ NewBlock();
      // For the control flow between the case blocks, we don't use the usual
      // NewBlockWithPhis / SetupControlFlowEdge / BindBlockAndGeneratePhis
      // helpers, because we don't need all their functionality. Instead, we
      // inline trimmed-down copies of them, doing only what we need, which is
      // handling the mutable fields cached on the InstanceCache.
      uint32_t cached_fields = instance_cache_.num_mutable_fields();
      BlockPhis merge_phis(decoder->zone_, instance_cache_);
      InstanceCache::Snapshot saved_cache = instance_cache_.SaveState();
      __ Goto(case_blocks[0]);
      for (size_t i = 0; i < feedback_cases.size(); i++) {
        __ Bind(case_blocks[i]);
        InliningTree* tree = feedback_cases[i];
        if (!tree || !tree->is_inlined()) {
          __ Goto(case_blocks[i + 1]);
          continue;
        }
        uint32_t inlined_index = tree->function_index();
        V<Object> inlined_func_ref =
            __ LoadFixedArrayElement(func_refs, inlined_index);
        TSBlock* inline_block = __ NewBlock();
        bool is_last_case = (i == feedback_cases.size() - 1);
        BranchHint hint = is_last_case ? BranchHint::kTrue : BranchHint::kNone;
        __ Branch({__ TaggedEqual(func_ref.op, inlined_func_ref), hint},
                  inline_block, case_blocks[i + 1]);

        __ Bind(inline_block);
        instance_cache_.RestoreFromSnapshot(saved_cache);
        SmallZoneVector<Value, 4> direct_returns(return_count, decoder->zone_);
        if (v8_flags.trace_wasm_inlining) {
          PrintF(
              "[function %d%s: Speculatively inlining call_ref #%d, case #%d, "
              "to function %d]\n",
              func_index_, mode_ == kRegular ? "" : " (inlined)",
              feedback_slot_, static_cast<int>(i), inlined_index);
        }
        InlineWasmCall(decoder, inlined_index, sig, static_cast<uint32_t>(i),
                       false, args, direct_returns.data());
        if (did_bailout()) return;

        if (__ current_block() != nullptr) {
          // Only add phi inputs and a Goto to {merge} if the current_block is
          // not nullptr. If the current_block is nullptr, it means that the
          // inlined body unconditionally exits early (likely an unconditional
          // trap or throw).
          for (size_t ret = 0; ret < direct_returns.size(); ret++) {
            case_returns[ret].push_back(direct_returns[ret].op);
          }
          merge_phis.AddPhiInputs(instance_cache_);
          __ Goto(merge);
        }
      }

      TSBlock* no_inline_block = case_blocks[case_blocks.size() - 1];
      __ Bind(no_inline_block);
      instance_cache_.RestoreFromSnapshot(saved_cache);
      auto [target, ref] =
          BuildFunctionReferenceTargetAndRef(func_ref.op, func_ref.type);
      SmallZoneVector<Value, 4> ref_returns(return_count, decoder->zone_);
      BuildWasmCall(decoder, sig, target, ref, args, ref_returns.data());
      for (size_t ret = 0; ret < ref_returns.size(); ret++) {
        case_returns[ret].push_back(ref_returns[ret].op);
      }
      merge_phis.AddPhiInputs(instance_cache_);
      __ Goto(merge);

      __ Bind(merge);
      for (size_t i = 0; i < case_returns.size(); i++) {
        returns[i].op = __ Phi(base::VectorOf(case_returns[i]),
                               RepresentationFor(sig->GetReturn(i)));
      }
      for (uint32_t i = 0; i < cached_fields; i++) {
        OpIndex phi =
            MaybePhi(merge_phis.phi_inputs(i), merge_phis.phi_type(i));
        instance_cache_.set_mutable_field_value(i, phi);
      }
    } else {
      auto [target, ref] =
          BuildFunctionReferenceTargetAndRef(func_ref.op, func_ref.type);
      BuildWasmCall(decoder, sig, target, ref, args, returns);
    }
  }

  void ReturnCallRef(FullDecoder* decoder, const Value& func_ref,
                     const FunctionSig* sig, const Value args[]) {
    feedback_slot_++;
    if (inlining_enabled(decoder) &&
        should_inline(decoder, feedback_slot_,
                      std::numeric_limits<int>::max())) {
      V<FixedArray> func_refs =
          LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), FuncRefs,
                                        MemoryRepresentation::TaggedPointer());
      base::Vector<InliningTree*> feedback_cases =
          inlining_decisions_->function_calls()[feedback_slot_];
      base::SmallVector<TSBlock*, 5> case_blocks;
      for (size_t i = 0; i < feedback_cases.size() + 1; i++) {
        case_blocks.push_back(__ NewBlock());
      }
      __ Goto(case_blocks[0]);
      for (size_t i = 0; i < feedback_cases.size(); i++) {
        __ Bind(case_blocks[i]);
        InliningTree* tree = feedback_cases[i];
        if (!tree || !tree->is_inlined()) {
          __ Goto(case_blocks[i + 1]);
          continue;
        }
        uint32_t inlined_index = tree->function_index();
        V<Object> inlined_func_ref =
            __ LoadFixedArrayElement(func_refs, inlined_index);
        TSBlock* inline_block = __ NewBlock();
        bool is_last_case = (i == feedback_cases.size() - 1);
        BranchHint hint = is_last_case ? BranchHint::kTrue : BranchHint::kNone;
        __ Branch({__ TaggedEqual(func_ref.op, inlined_func_ref), hint},
                  inline_block, case_blocks[i + 1]);

        __ Bind(inline_block);
        if (v8_flags.trace_wasm_inlining) {
          PrintF(
              "[function %d%s: Speculatively inlining return_call_ref #%d, "
              "case #%d, to function %d]\n",
              func_index_, mode_ == kRegular ? "" : " (inlined)",
              feedback_slot_, static_cast<int>(i), inlined_index);
        }
        InlineWasmCall(decoder, inlined_index, sig, static_cast<uint32_t>(i),
                       true, args, nullptr);
        if (did_bailout()) return;
        // An inlined tail call should still terminate execution.
        DCHECK_EQ(__ current_block(), nullptr);
      }

      TSBlock* no_inline_block = case_blocks[case_blocks.size() - 1];
      __ Bind(no_inline_block);
    }
    auto [target, ref] =
        BuildFunctionReferenceTargetAndRef(func_ref.op, func_ref.type);
    BuildWasmMaybeReturnCall(decoder, sig, target, ref, args);
  }

  void BrOnNull(FullDecoder* decoder, const Value& ref_object, uint32_t depth,
                bool pass_null_along_branch, Value* result_on_fallthrough) {
    result_on_fallthrough->op = ref_object.op;
    IF (UNLIKELY(__ IsNull(ref_object.op, ref_object.type))) {
      int drop_values = pass_null_along_branch ? 0 : 1;
      BrOrRet(decoder, depth, drop_values);
    }
  }

  void BrOnNonNull(FullDecoder* decoder, const Value& ref_object, Value* result,
                   uint32_t depth, bool /* drop_null_on_fallthrough */) {
    result->op = ref_object.op;
    IF_NOT (UNLIKELY(__ IsNull(ref_object.op, ref_object.type))) {
      BrOrRet(decoder, depth);
    }
  }

  void SimdOp(FullDecoder* decoder, WasmOpcode opcode, const Value* args,
              Value* result) {
    switch (opcode) {
#define HANDLE_BINARY_OPCODE(kind)                                            \
  case kExpr##kind:                                                           \
    result->op =                                                              \
        __ Simd128Binop(args[0].op, args[1].op,                               \
                        compiler::turboshaft::Simd128BinopOp::Kind::k##kind); \
    break;
      FOREACH_SIMD_128_BINARY_OPCODE(HANDLE_BINARY_OPCODE)
#undef HANDLE_BINARY_OPCODE

#define HANDLE_INVERSE_COMPARISON(wasm_kind, ts_kind)            \
  case kExpr##wasm_kind:                                         \
    result->op = __ Simd128Binop(                                \
        args[1].op, args[0].op,                                  \
        compiler::turboshaft::Simd128BinopOp::Kind::k##ts_kind); \
    break;

      HANDLE_INVERSE_COMPARISON(I8x16LtS, I8x16GtS)
      HANDLE_INVERSE_COMPARISON(I8x16LtU, I8x16GtU)
      HANDLE_INVERSE_COMPARISON(I8x16LeS, I8x16GeS)
      HANDLE_INVERSE_COMPARISON(I8x16LeU, I8x16GeU)

      HANDLE_INVERSE_COMPARISON(I16x8LtS, I16x8GtS)
      HANDLE_INVERSE_COMPARISON(I16x8LtU, I16x8GtU)
      HANDLE_INVERSE_COMPARISON(I16x8LeS, I16x8GeS)
      HANDLE_INVERSE_COMPARISON(I16x8LeU, I16x8GeU)

      HANDLE_INVERSE_COMPARISON(I32x4LtS, I32x4GtS)
      HANDLE_INVERSE_COMPARISON(I32x4LtU, I32x4GtU)
      HANDLE_INVERSE_COMPARISON(I32x4LeS, I32x4GeS)
      HANDLE_INVERSE_COMPARISON(I32x4LeU, I32x4GeU)

      HANDLE_INVERSE_COMPARISON(I64x2LtS, I64x2GtS)
      HANDLE_INVERSE_COMPARISON(I64x2LeS, I64x2GeS)

      HANDLE_INVERSE_COMPARISON(F32x4Gt, F32x4Lt)
      HANDLE_INVERSE_COMPARISON(F32x4Ge, F32x4Le)
      HANDLE_INVERSE_COMPARISON(F64x2Gt, F64x2Lt)
      HANDLE_INVERSE_COMPARISON(F64x2Ge, F64x2Le)

#undef HANDLE_INVERSE_COMPARISON

#define HANDLE_UNARY_NON_OPTIONAL_OPCODE(kind)                            \
  case kExpr##kind:                                                       \
    result->op = __ Simd128Unary(                                         \
        args[0].op, compiler::turboshaft::Simd128UnaryOp::Kind::k##kind); \
    break;
      FOREACH_SIMD_128_UNARY_NON_OPTIONAL_OPCODE(
          HANDLE_UNARY_NON_OPTIONAL_OPCODE)
#undef HANDLE_UNARY_NON_OPTIONAL_OPCODE

#define HANDLE_UNARY_OPTIONAL_OPCODE(kind, feature, external_ref)           \
  case kExpr##kind:                                                         \
    if (SupportedOperations::feature()) {                                   \
      result->op = __ Simd128Unary(                                         \
          args[0].op, compiler::turboshaft::Simd128UnaryOp::Kind::k##kind); \
    } else {                                                                \
      result->op = CallCStackSlotToStackSlot(                               \
          args[0].op, ExternalReference::external_ref(),                    \
          MemoryRepresentation::Simd128());                                 \
    }                                                                       \
    break;
      HANDLE_UNARY_OPTIONAL_OPCODE(F32x4Ceil, float32_round_up, wasm_f32x4_ceil)
      HANDLE_UNARY_OPTIONAL_OPCODE(F32x4Floor, float32_round_down,
                                   wasm_f32x4_floor)
      HANDLE_UNARY_OPTIONAL_OPCODE(F32x4Trunc, float32_round_to_zero,
                                   wasm_f32x4_trunc)
      HANDLE_UNARY_OPTIONAL_OPCODE(F32x4NearestInt, float32_round_ties_even,
                                   wasm_f32x4_nearest_int)
      HANDLE_UNARY_OPTIONAL_OPCODE(F64x2Ceil, float64_round_up, wasm_f64x2_ceil)
      HANDLE_UNARY_OPTIONAL_OPCODE(F64x2Floor, float64_round_down,
                                   wasm_f64x2_floor)
      HANDLE_UNARY_OPTIONAL_OPCODE(F64x2Trunc, float64_round_to_zero,
                                   wasm_f64x2_trunc)
      HANDLE_UNARY_OPTIONAL_OPCODE(F64x2NearestInt, float64_round_ties_even,
                                   wasm_f64x2_nearest_int)
#undef HANDLE_UNARY_OPTIONAL_OPCODE

#define HANDLE_SHIFT_OPCODE(kind)                                             \
  case kExpr##kind:                                                           \
    result->op =                                                              \
        __ Simd128Shift(args[0].op, args[1].op,                               \
                        compiler::turboshaft::Simd128ShiftOp::Kind::k##kind); \
    break;
      FOREACH_SIMD_128_SHIFT_OPCODE(HANDLE_SHIFT_OPCODE)
#undef HANDLE_SHIFT_OPCODE

#define HANDLE_TEST_OPCODE(kind)                                         \
  case kExpr##kind:                                                      \
    result->op = __ Simd128Test(                                         \
        args[0].op, compiler::turboshaft::Simd128TestOp::Kind::k##kind); \
    break;
      FOREACH_SIMD_128_TEST_OPCODE(HANDLE_TEST_OPCODE)
#undef HANDLE_TEST_OPCODE

#define HANDLE_SPLAT_OPCODE(kind)                                         \
  case kExpr##kind##Splat:                                                \
    result->op = __ Simd128Splat(                                         \
        args[0].op, compiler::turboshaft::Simd128SplatOp::Kind::k##kind); \
    break;
      FOREACH_SIMD_128_SPLAT_OPCODE(HANDLE_SPLAT_OPCODE)
#undef HANDLE_SPLAT_OPCODE

// Ternary mask operators put the mask as first input.
#define HANDLE_TERNARY_MASK_OPCODE(kind)                        \
  case kExpr##kind:                                             \
    result->op = __ Simd128Ternary(                             \
        args[2].op, args[0].op, args[1].op,                     \
        compiler::turboshaft::Simd128TernaryOp::Kind::k##kind); \
    break;
      FOREACH_SIMD_128_TERNARY_MASK_OPCODE(HANDLE_TERNARY_MASK_OPCODE)
#undef HANDLE_TERNARY_MASK_OPCODE

#define HANDLE_TERNARY_OTHER_OPCODE(kind)                       \
  case kExpr##kind:                                             \
    result->op = __ Simd128Ternary(                             \
        args[0].op, args[1].op, args[2].op,                     \
        compiler::turboshaft::Simd128TernaryOp::Kind::k##kind); \
    break;
      FOREACH_SIMD_128_TERNARY_OTHER_OPCODE(HANDLE_TERNARY_OTHER_OPCODE)
#undef HANDLE_TERNARY_OTHER_OPCODE

      default:
        UNREACHABLE();
    }
  }

  void SimdLaneOp(FullDecoder* decoder, WasmOpcode opcode,
                  const SimdLaneImmediate& imm,
                  base::Vector<const Value> inputs, Value* result) {
    using compiler::turboshaft::Simd128ExtractLaneOp;
    using compiler::turboshaft::Simd128ReplaceLaneOp;
    switch (opcode) {
      case kExprI8x16ExtractLaneS:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kI8x16S, imm.lane);
        break;
      case kExprI8x16ExtractLaneU:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kI8x16U, imm.lane);
        break;
      case kExprI16x8ExtractLaneS:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kI16x8S, imm.lane);
        break;
      case kExprI16x8ExtractLaneU:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kI16x8U, imm.lane);
        break;
      case kExprI32x4ExtractLane:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kI32x4, imm.lane);
        break;
      case kExprI64x2ExtractLane:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kI64x2, imm.lane);
        break;
      case kExprF32x4ExtractLane:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kF32x4, imm.lane);
        break;
      case kExprF64x2ExtractLane:
        result->op = __ Simd128ExtractLane(
            inputs[0].op, Simd128ExtractLaneOp::Kind::kF64x2, imm.lane);
        break;
      case kExprI8x16ReplaceLane:
        result->op =
            __ Simd128ReplaceLane(inputs[0].op, inputs[1].op,
                                  Simd128ReplaceLaneOp::Kind::kI8x16, imm.lane);
        break;
      case kExprI16x8ReplaceLane:
        result->op =
            __ Simd128ReplaceLane(inputs[0].op, inputs[1].op,
                                  Simd128ReplaceLaneOp::Kind::kI16x8, imm.lane);
        break;
      case kExprI32x4ReplaceLane:
        result->op =
            __ Simd128ReplaceLane(inputs[0].op, inputs[1].op,
                                  Simd128ReplaceLaneOp::Kind::kI32x4, imm.lane);
        break;
      case kExprI64x2ReplaceLane:
        result->op =
            __ Simd128ReplaceLane(inputs[0].op, inputs[1].op,
                                  Simd128ReplaceLaneOp::Kind::kI64x2, imm.lane);
        break;
      case kExprF32x4ReplaceLane:
        result->op =
            __ Simd128ReplaceLane(inputs[0].op, inputs[1].op,
                                  Simd128ReplaceLaneOp::Kind::kF32x4, imm.lane);
        break;
      case kExprF64x2ReplaceLane:
        result->op =
            __ Simd128ReplaceLane(inputs[0].op, inputs[1].op,
                                  Simd128ReplaceLaneOp::Kind::kF64x2, imm.lane);
        break;
      default:
        UNREACHABLE();
    }
  }

  void Simd8x16ShuffleOp(FullDecoder* decoder, const Simd128Immediate& imm,
                         const Value& input0, const Value& input1,
                         Value* result) {
    result->op = __ Simd128Shuffle(input0.op, input1.op, imm.value);
  }

  void Try(FullDecoder* decoder, Control* block) {
    block->false_or_loop_or_catch_block = NewBlockWithPhis(decoder, nullptr);
    block->merge_block = NewBlockWithPhis(decoder, block->br_merge());
  }

  void Throw(FullDecoder* decoder, const TagIndexImmediate& imm,
             const Value arg_values[]) {
    size_t count = imm.tag->sig->parameter_count();
    SmallZoneVector<OpIndex, 16> values(count, decoder->zone_);
    for (size_t index = 0; index < count; index++) {
      values[index] = arg_values[index].op;
    }

    uint32_t encoded_size = WasmExceptionPackage::GetEncodedSize(imm.tag);

    V<FixedArray> values_array = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmAllocateFixedArray>(
        decoder, {__ IntPtrConstant(encoded_size)});
    uint32_t index = 0;
    const wasm::WasmTagSig* sig = imm.tag->sig;

    // Encode the exception values in {values_array}.
    for (size_t i = 0; i < count; i++) {
      OpIndex value = values[i];
      switch (sig->GetParam(i).kind()) {
        case kF32:
          value = __ BitcastFloat32ToWord32(value);
          [[fallthrough]];
        case kI32:
          BuildEncodeException32BitValue(values_array, index, value);
          // We need 2 Smis to encode a 32-bit value.
          index += 2;
          break;
        case kF64:
          value = __ BitcastFloat64ToWord64(value);
          [[fallthrough]];
        case kI64: {
          OpIndex upper_half =
              __ TruncateWord64ToWord32(__ Word64ShiftRightLogical(value, 32));
          BuildEncodeException32BitValue(values_array, index, upper_half);
          index += 2;
          OpIndex lower_half = __ TruncateWord64ToWord32(value);
          BuildEncodeException32BitValue(values_array, index, lower_half);
          index += 2;
          break;
        }
        case wasm::kRef:
        case wasm::kRefNull:
        case wasm::kRtt:
          __ StoreFixedArrayElement(values_array, index, value,
                                    compiler::kFullWriteBarrier);
          index++;
          break;
        case kS128: {
          using Kind = compiler::turboshaft::Simd128ExtractLaneOp::Kind;
          BuildEncodeException32BitValue(
              values_array, index,
              __ Simd128ExtractLane(value, Kind::kI32x4, 0));
          index += 2;
          BuildEncodeException32BitValue(
              values_array, index,
              __ Simd128ExtractLane(value, Kind::kI32x4, 1));
          index += 2;
          BuildEncodeException32BitValue(
              values_array, index,
              __ Simd128ExtractLane(value, Kind::kI32x4, 2));
          index += 2;
          BuildEncodeException32BitValue(
              values_array, index,
              __ Simd128ExtractLane(value, Kind::kI32x4, 3));
          index += 2;
          break;
        }
        case kI8:
        case kI16:
        case kVoid:
        case kBottom:
          UNREACHABLE();
      }
    }

    V<FixedArray> instance_tags =
        LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), TagsTable,
                                      MemoryRepresentation::TaggedPointer());
    auto tag = V<WasmTagObject>::Cast(
        __ LoadFixedArrayElement(instance_tags, imm.index));

    CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmThrow>(
        decoder, {tag, values_array}, CheckForException::kCatchInThisFrame);
    __ Unreachable();
  }

  void Rethrow(FullDecoder* decoder, Control* block) {
    CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmRethrow>(
        decoder, {block->exception}, CheckForException::kCatchInThisFrame);
    __ Unreachable();
  }

  void CatchException(FullDecoder* decoder, const TagIndexImmediate& imm,
                      Control* block, base::Vector<Value> values) {
    BindBlockAndGeneratePhis(decoder, block->false_or_loop_or_catch_block,
                             nullptr, &block->exception);
    V<NativeContext> native_context = instance_cache_.native_context();
    V<WasmTagObject> caught_tag = V<WasmTagObject>::Cast(
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmGetOwnProperty>(
            decoder, native_context,
            {block->exception, LOAD_ROOT(wasm_exception_tag_symbol)}));
    V<FixedArray> instance_tags =
        LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), TagsTable,
                                      MemoryRepresentation::TaggedPointer());
    auto expected_tag = V<WasmTagObject>::Cast(
        __ LoadFixedArrayElement(instance_tags, imm.index));
    TSBlock* if_catch = __ NewBlock();
    TSBlock* if_no_catch = NewBlockWithPhis(decoder, nullptr);
    SetupControlFlowEdge(decoder, if_no_catch);

    // If the tags don't match we continue with the next tag by setting the
    // no-catch environment as the new {block->false_or_loop_or_catch_block}
    // here.
    block->false_or_loop_or_catch_block = if_no_catch;

    if (imm.tag->sig->parameter_count() == 1 &&
        imm.tag->sig->GetParam(0).is_reference_to(HeapType::kExtern)) {
      // Check for the special case where the tag is WebAssembly.JSTag and the
      // exception is not a WebAssembly.Exception. In this case the exception is
      // caught and pushed on the operand stack.
      // Only perform this check if the tag signature is the same as
      // the JSTag signature, i.e. a single externref or (ref extern), otherwise
      // we know statically that it cannot be the JSTag.
      V<Word32> caught_tag_undefined =
          __ TaggedEqual(caught_tag, LOAD_ROOT(UndefinedValue));
      Label<Object> if_catch(&asm_);
      Label<> no_catch_merge(&asm_);

      IF (UNLIKELY(caught_tag_undefined)) {
        V<Object> tag_object = __ Load(
            native_context, LoadOp::Kind::TaggedBase(),
            MemoryRepresentation::TaggedPointer(),
            NativeContext::OffsetOfElementAt(Context::WASM_JS_TAG_INDEX));
        V<Object> js_tag = __ Load(tag_object, LoadOp::Kind::TaggedBase(),
                                   MemoryRepresentation::TaggedPointer(),
                                   WasmTagObject::kTagOffset);
        GOTO_IF(__ TaggedEqual(expected_tag, js_tag), if_catch,
                block->exception);
        GOTO(no_catch_merge);
      } ELSE {
        IF (__ TaggedEqual(caught_tag, expected_tag)) {
          UnpackWasmException(decoder, block->exception, values);
          GOTO(if_catch, values[0].op);
        }
        GOTO(no_catch_merge);
      }

      BIND(no_catch_merge);
      __ Goto(if_no_catch);

      BIND(if_catch, caught_exception);
      // The first unpacked value is the exception itself in the case of a JS
      // exception.
      values[0].op = caught_exception;
    } else {
      __ Branch(ConditionWithHint(__ TaggedEqual(caught_tag, expected_tag)),
                if_catch, if_no_catch);
      __ Bind(if_catch);
      UnpackWasmException(decoder, block->exception, values);
    }
  }

  void Delegate(FullDecoder* decoder, uint32_t depth, Control* block) {
    BindBlockAndGeneratePhis(decoder, block->false_or_loop_or_catch_block,
                             nullptr, &block->exception);
    if (depth == decoder->control_depth() - 1) {
      if (mode_ == kInlinedWithCatch) {
        if (block->exception.valid()) {
          return_phis_->AddIncomingException(block->exception);
        }
        __ Goto(return_catch_block_);
      } else {
        // We just throw to the caller, no need to handle the exception in this
        // frame.
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmRethrow>(
            decoder, {block->exception});
        __ Unreachable();
      }
    } else {
      DCHECK(decoder->control_at(depth)->is_try());
      TSBlock* target_catch =
          decoder->control_at(depth)->false_or_loop_or_catch_block;
      SetupControlFlowEdge(decoder, target_catch, 0, block->exception);
      __ Goto(target_catch);
    }
  }

  void CatchAll(FullDecoder* decoder, Control* block) {
    DCHECK(block->is_try_catchall() || block->is_try_catch());
    DCHECK_EQ(decoder->control_at(0), block);
    BindBlockAndGeneratePhis(decoder, block->false_or_loop_or_catch_block,
                             nullptr, &block->exception);
  }

  void TryTable(FullDecoder* decoder, Control* block) { Try(decoder, block); }

  void CatchCase(FullDecoder* decoder, Control* block,
                 const CatchCase& catch_case, base::Vector<Value> values) {
    // If this is the first catch case, {block->false_or_loop_or_catch_block} is
    // the block that was created on block entry, and is where all throwing
    // instructions in the try-table jump to if they throw.
    // Otherwise, {block->false_or_loop_or_catch_block} has been overwritten by
    // the previous handler, and is where we jump to if we did not catch the
    // exception yet.
    BindBlockAndGeneratePhis(decoder, block->false_or_loop_or_catch_block,
                             nullptr, &block->exception);
    if (catch_case.kind == kCatchAll || catch_case.kind == kCatchAllRef) {
      if (catch_case.kind == kCatchAllRef) {
        DCHECK_EQ(values.size(), 1);
        values.last().op = block->exception;
      }
      BrOrRet(decoder, catch_case.br_imm.depth);
      return;
    }
    V<WasmTagObject> caught_tag = V<WasmTagObject>::Cast(
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmGetOwnProperty>(
            decoder, instance_cache_.native_context(),
            {block->exception, LOAD_ROOT(wasm_exception_tag_symbol)}));
    V<FixedArray> instance_tags =
        LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), TagsTable,
                                      MemoryRepresentation::TaggedPointer());
    auto expected_tag = V<WasmTagObject>::Cast(__ LoadFixedArrayElement(
        instance_tags, catch_case.maybe_tag.tag_imm.index));
    TSBlock* if_catch = __ NewBlock();
    TSBlock* if_no_catch = NewBlockWithPhis(decoder, nullptr);
    SetupControlFlowEdge(decoder, if_no_catch);
    block->false_or_loop_or_catch_block = if_no_catch;
    __ Branch(ConditionWithHint(__ TaggedEqual(caught_tag, expected_tag)),
              if_catch, block->false_or_loop_or_catch_block);
    __ Bind(if_catch);
    if (catch_case.kind == kCatchRef) {
      UnpackWasmException(decoder, block->exception,
                          values.SubVector(0, values.size() - 1));
      values.last().op = block->exception;
    } else {
      UnpackWasmException(decoder, block->exception, values);
    }
    BrOrRet(decoder, catch_case.br_imm.depth);

    bool is_last = &catch_case == &block->catch_cases.last();
    if (is_last && !decoder->HasCatchAll(block)) {
      BindBlockAndGeneratePhis(decoder, block->false_or_loop_or_catch_block,
                               nullptr, &block->exception);
      ThrowRef(decoder, block->exception);
    }
  }

  void ThrowRef(FullDecoder* decoder, Value* value) {
    ThrowRef(decoder, value->op);
  }

  void AtomicNotify(FullDecoder* decoder, const MemoryAccessImmediate& imm,
                    OpIndex index, OpIndex num_waiters_to_wake, Value* result) {
    V<WordPtr> converted_index;
    compiler::BoundsCheckResult bounds_check_result;
    std::tie(converted_index, bounds_check_result) = BoundsCheckMem(
        imm.memory, MemoryRepresentation::Int32(), index, imm.offset,
        compiler::EnforceBoundsCheck::kNeedsBoundsCheck,
        compiler::AlignmentCheck::kYes);

    OpIndex effective_offset = __ WordPtrAdd(converted_index, imm.offset);
    OpIndex addr = __ WordPtrAdd(MemStart(imm.mem_index), effective_offset);

    auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                   .Params(MachineType::Pointer(), MachineType::Uint32());
    result->op = CallC(&sig, ExternalReference::wasm_atomic_notify(),
                       {addr, num_waiters_to_wake});
  }

  void AtomicWait(FullDecoder* decoder, WasmOpcode opcode,
                  const MemoryAccessImmediate& imm, OpIndex index,
                  OpIndex expected, V<Word64> timeout, Value* result) {
    constexpr StubCallMode kStubMode = StubCallMode::kCallWasmRuntimeStub;
    V<WordPtr> converted_index;
    compiler::BoundsCheckResult bounds_check_result;
    std::tie(converted_index, bounds_check_result) = BoundsCheckMem(
        imm.memory,
        opcode == kExprI32AtomicWait ? MemoryRepresentation::Int32()
                                     : MemoryRepresentation::Int64(),
        index, imm.offset, compiler::EnforceBoundsCheck::kNeedsBoundsCheck,
        compiler::AlignmentCheck::kYes);

    OpIndex effective_offset = __ WordPtrAdd(converted_index, imm.offset);
    V<BigInt> bigint_timeout = BuildChangeInt64ToBigInt(timeout, kStubMode);

    if (opcode == kExprI32AtomicWait) {
      result->op =
          CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmI32AtomicWait>(
              decoder, {__ Word32Constant(imm.memory->index), effective_offset,
                        expected, bigint_timeout});
      return;
    }
    DCHECK_EQ(opcode, kExprI64AtomicWait);
    V<BigInt> bigint_expected = BuildChangeInt64ToBigInt(expected, kStubMode);
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmI64AtomicWait>(
            decoder, {__ Word32Constant(imm.memory->index), effective_offset,
                      bigint_expected, bigint_timeout});
  }

  void AtomicOp(FullDecoder* decoder, WasmOpcode opcode, const Value args[],
                const size_t argc, const MemoryAccessImmediate& imm,
                Value* result) {
    if (opcode == WasmOpcode::kExprAtomicNotify) {
      return AtomicNotify(decoder, imm, args[0].op, args[1].op, result);
    }
    if (opcode == WasmOpcode::kExprI32AtomicWait ||
        opcode == WasmOpcode::kExprI64AtomicWait) {
      return AtomicWait(decoder, opcode, imm, args[0].op, args[1].op,
                        args[2].op, result);
    }
    using Binop = compiler::turboshaft::AtomicRMWOp::BinOp;
    enum OpType { kBinop, kLoad, kStore };
    struct AtomicOpInfo {
      OpType op_type;
      // Initialize with a default value, to allow constexpr constructors.
      Binop bin_op = Binop::kAdd;
      RegisterRepresentation result_rep;
      MemoryRepresentation input_rep;

      constexpr AtomicOpInfo(Binop bin_op, RegisterRepresentation result_rep,
                             MemoryRepresentation input_rep)
          : op_type(kBinop),
            bin_op(bin_op),
            result_rep(result_rep),
            input_rep(input_rep) {}

      constexpr AtomicOpInfo(OpType op_type, RegisterRepresentation result_rep,
                             MemoryRepresentation input_rep)
          : op_type(op_type), result_rep(result_rep), input_rep(input_rep) {}

      static constexpr AtomicOpInfo Get(wasm::WasmOpcode opcode) {
        switch (opcode) {
#define CASE_BINOP(OPCODE, BINOP, RESULT, INPUT)                           \
  case WasmOpcode::kExpr##OPCODE:                                          \
    return AtomicOpInfo(Binop::k##BINOP, RegisterRepresentation::RESULT(), \
                        MemoryRepresentation::INPUT());
#define RMW_OPERATION(V)                                          \
  V(I32AtomicAdd, Add, Word32, Uint32)                            \
  V(I32AtomicAdd8U, Add, Word32, Uint8)                           \
  V(I32AtomicAdd16U, Add, Word32, Uint16)                         \
  V(I32AtomicSub, Sub, Word32, Uint32)                            \
  V(I32AtomicSub8U, Sub, Word32, Uint8)                           \
  V(I32AtomicSub16U, Sub, Word32, Uint16)                         \
  V(I32AtomicAnd, And, Word32, Uint32)                            \
  V(I32AtomicAnd8U, And, Word32, Uint8)                           \
  V(I32AtomicAnd16U, And, Word32, Uint16)                         \
  V(I32AtomicOr, Or, Word32, Uint32)                              \
  V(I32AtomicOr8U, Or, Word32, Uint8)                             \
  V(I32AtomicOr16U, Or, Word32, Uint16)                           \
  V(I32AtomicXor, Xor, Word32, Uint32)                            \
  V(I32AtomicXor8U, Xor, Word32, Uint8)                           \
  V(I32AtomicXor16U, Xor, Word32, Uint16)                         \
  V(I32AtomicExchange, Exchange, Word32, Uint32)                  \
  V(I32AtomicExchange8U, Exchange, Word32, Uint8)                 \
  V(I32AtomicExchange16U, Exchange, Word32, Uint16)               \
  V(I32AtomicCompareExchange, CompareExchange, Word32, Uint32)    \
  V(I32AtomicCompareExchange8U, CompareExchange, Word32, Uint8)   \
  V(I32AtomicCompareExchange16U, CompareExchange, Word32, Uint16) \
  V(I64AtomicAdd, Add, Word64, Uint64)                            \
  V(I64AtomicAdd8U, Add, Word64, Uint8)                           \
  V(I64AtomicAdd16U, Add, Word64, Uint16)                         \
  V(I64AtomicAdd32U, Add, Word64, Uint32)                         \
  V(I64AtomicSub, Sub, Word64, Uint64)                            \
  V(I64AtomicSub8U, Sub, Word64, Uint8)                           \
  V(I64AtomicSub16U, Sub, Word64, Uint16)                         \
  V(I64AtomicSub32U, Sub, Word64, Uint32)                         \
  V(I64AtomicAnd, And, Word64, Uint64)                            \
  V(I64AtomicAnd8U, And, Word64, Uint8)                           \
  V(I64AtomicAnd16U, And, Word64, Uint16)                         \
  V(I64AtomicAnd32U, And, Word64, Uint32)                         \
  V(I64AtomicOr, Or, Word64, Uint64)                              \
  V(I64AtomicOr8U, Or, Word64, Uint8)                             \
  V(I64AtomicOr16U, Or, Word64, Uint16)                           \
  V(I64AtomicOr32U, Or, Word64, Uint32)                           \
  V(I64AtomicXor, Xor, Word64, Uint64)                            \
  V(I64AtomicXor8U, Xor, Word64, Uint8)                           \
  V(I64AtomicXor16U, Xor, Word64, Uint16)                         \
  V(I64AtomicXor32U, Xor, Word64, Uint32)                         \
  V(I64AtomicExchange, Exchange, Word64, Uint64)                  \
  V(I64AtomicExchange8U, Exchange, Word64, Uint8)                 \
  V(I64AtomicExchange16U, Exchange, Word64, Uint16)               \
  V(I64AtomicExchange32U, Exchange, Word64, Uint32)               \
  V(I64AtomicCompareExchange, CompareExchange, Word64, Uint64)    \
  V(I64AtomicCompareExchange8U, CompareExchange, Word64, Uint8)   \
  V(I64AtomicCompareExchange16U, CompareExchange, Word64, Uint16) \
  V(I64AtomicCompareExchange32U, CompareExchange, Word64, Uint32)

          RMW_OPERATION(CASE_BINOP)
#undef RMW_OPERATION
#undef CASE
#define CASE_LOAD(OPCODE, RESULT, INPUT)                         \
  case WasmOpcode::kExpr##OPCODE:                                \
    return AtomicOpInfo(kLoad, RegisterRepresentation::RESULT(), \
                        MemoryRepresentation::INPUT());
#define LOAD_OPERATION(V)             \
  V(I32AtomicLoad, Word32, Uint32)    \
  V(I32AtomicLoad16U, Word32, Uint16) \
  V(I32AtomicLoad8U, Word32, Uint8)   \
  V(I64AtomicLoad, Word64, Uint64)    \
  V(I64AtomicLoad32U, Word64, Uint32) \
  V(I64AtomicLoad16U, Word64, Uint16) \
  V(I64AtomicLoad8U, Word64, Uint8)
          LOAD_OPERATION(CASE_LOAD)
#undef LOAD_OPERATION
#undef CASE_LOAD
#define CASE_STORE(OPCODE, INPUT, OUTPUT)                        \
  case WasmOpcode::kExpr##OPCODE:                                \
    return AtomicOpInfo(kStore, RegisterRepresentation::INPUT(), \
                        MemoryRepresentation::OUTPUT());
#define STORE_OPERATION(V)             \
  V(I32AtomicStore, Word32, Uint32)    \
  V(I32AtomicStore16U, Word32, Uint16) \
  V(I32AtomicStore8U, Word32, Uint8)   \
  V(I64AtomicStore, Word64, Uint64)    \
  V(I64AtomicStore32U, Word64, Uint32) \
  V(I64AtomicStore16U, Word64, Uint16) \
  V(I64AtomicStore8U, Word64, Uint8)
          STORE_OPERATION(CASE_STORE)
#undef STORE_OPERATION_OPERATION
#undef CASE_STORE
          default:
            UNREACHABLE();
        }
      }
    };

    AtomicOpInfo info = AtomicOpInfo::Get(opcode);
    V<WordPtr> index;
    compiler::BoundsCheckResult bounds_check_result;
    std::tie(index, bounds_check_result) =
        BoundsCheckMem(imm.memory, info.input_rep, args[0].op, imm.offset,
                       compiler::EnforceBoundsCheck::kCanOmitBoundsCheck,
                       compiler::AlignmentCheck::kYes);
    // MemoryAccessKind::kUnaligned is impossible due to explicit aligment
    // check.
    MemoryAccessKind access_kind =
        bounds_check_result == compiler::BoundsCheckResult::kTrapHandler
            ? MemoryAccessKind::kProtected
            : MemoryAccessKind::kNormal;

    if (info.op_type == kBinop) {
      if (info.bin_op == Binop::kCompareExchange) {
        result->op = __ AtomicCompareExchange(
            MemBuffer(imm.memory->index, imm.offset), index, args[1].op,
            args[2].op, info.result_rep, info.input_rep, access_kind);
        return;
      }
      result->op = __ AtomicRMW(MemBuffer(imm.memory->index, imm.offset), index,
                                args[1].op, info.bin_op, info.result_rep,
                                info.input_rep, access_kind);
      return;
    }
    if (info.op_type == kStore) {
      OpIndex value = args[1].op;
      if (info.result_rep == RegisterRepresentation::Word64() &&
          info.input_rep != MemoryRepresentation::Uint64()) {
        value = __ TruncateWord64ToWord32(value);
      }
#ifdef V8_TARGET_BIG_ENDIAN
      // Reverse the value bytes before storing.
      DCHECK(info.result_rep == RegisterRepresentation::Word32() ||
             info.result_rep == RegisterRepresentation::Word64());
      wasm::ValueType wasm_type =
          info.result_rep == RegisterRepresentation::Word32() ? wasm::kWasmI32
                                                              : wasm::kWasmI64;
      value = BuildChangeEndiannessStore(
          value, info.input_rep.ToMachineType().representation(), wasm_type);
#endif
      __ Store(MemBuffer(imm.memory->index, imm.offset), index, value,
               access_kind == MemoryAccessKind::kProtected
                   ? LoadOp::Kind::Protected().Atomic()
                   : LoadOp::Kind::RawAligned().Atomic(),
               info.input_rep, compiler::kNoWriteBarrier);
      return;
    }
    DCHECK_EQ(info.op_type, kLoad);
    result->op = __ Load(MemBuffer(imm.memory->index, imm.offset), index,
                         access_kind == MemoryAccessKind::kProtected
                             ? LoadOp::Kind::Protected().Atomic()
                             : LoadOp::Kind::RawAligned().Atomic(),
                         info.input_rep, info.result_rep);

#ifdef V8_TARGET_BIG_ENDIAN
    // Reverse the value bytes after load.
    DCHECK(info.result_rep == RegisterRepresentation::Word32() ||
           info.result_rep == RegisterRepresentation::Word64());
    wasm::ValueType wasm_type =
        info.result_rep == RegisterRepresentation::Word32() ? wasm::kWasmI32
                                                            : wasm::kWasmI64;
    result->op = BuildChangeEndiannessLoad(
        result->op, info.input_rep.ToMachineType(), wasm_type);
#endif
  }

  void AtomicFence(FullDecoder* decoder) {
    __ MemoryBarrier(AtomicMemoryOrder::kSeqCst);
  }

  void MemoryInit(FullDecoder* decoder, const MemoryInitImmediate& imm,
                  const Value& dst, const Value& src, const Value& size) {
    V<WordPtr> dst_uintptr =
        MemoryIndexToUintPtrOrOOBTrap(imm.memory.memory->is_memory64, dst.op);
    DCHECK_EQ(size.type, kWasmI32);
    auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                   .Params(MachineType::Pointer(), MachineType::Uint32(),
                           MachineType::UintPtr(), MachineType::Uint32(),
                           MachineType::Uint32(), MachineType::Uint32());
    V<Word32> result =
        CallC(&sig, ExternalReference::wasm_memory_init(),
              {__ BitcastHeapObjectToWordPtr(trusted_instance_data()),
               __ Word32Constant(imm.memory.index), dst_uintptr, src.op,
               __ Word32Constant(imm.data_segment.index), size.op});
    __ TrapIfNot(result, OpIndex::Invalid(), TrapId::kTrapMemOutOfBounds);
  }

  void MemoryCopy(FullDecoder* decoder, const MemoryCopyImmediate& imm,
                  const Value& dst, const Value& src, const Value& size) {
    bool is_memory_64 = imm.memory_src.memory->is_memory64;
    DCHECK_EQ(is_memory_64, imm.memory_dst.memory->is_memory64);
    V<WordPtr> dst_uintptr =
        MemoryIndexToUintPtrOrOOBTrap(is_memory_64, dst.op);
    V<WordPtr> src_uintptr =
        MemoryIndexToUintPtrOrOOBTrap(is_memory_64, src.op);
    V<WordPtr> size_uintptr =
        MemoryIndexToUintPtrOrOOBTrap(is_memory_64, size.op);
    auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                   .Params(MachineType::Pointer(), MachineType::Uint32(),
                           MachineType::Uint32(), MachineType::UintPtr(),
                           MachineType::UintPtr(), MachineType::UintPtr());
    V<Word32> result =
        CallC(&sig, ExternalReference::wasm_memory_copy(),
              {__ BitcastHeapObjectToWordPtr(trusted_instance_data()),
               __ Word32Constant(imm.memory_dst.index),
               __ Word32Constant(imm.memory_src.index), dst_uintptr,
               src_uintptr, size_uintptr});
    __ TrapIfNot(result, OpIndex::Invalid(), TrapId::kTrapMemOutOfBounds);
  }

  void MemoryFill(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value& dst, const Value& value, const Value& size) {
    bool is_memory_64 = imm.memory->is_memory64;
    V<WordPtr> dst_uintptr =
        MemoryIndexToUintPtrOrOOBTrap(is_memory_64, dst.op);
    V<WordPtr> size_uintptr =
        MemoryIndexToUintPtrOrOOBTrap(is_memory_64, size.op);
    auto sig = FixedSizeSignature<MachineType>::Returns(MachineType::Int32())
                   .Params(MachineType::Pointer(), MachineType::Uint32(),
                           MachineType::UintPtr(), MachineType::Uint8(),
                           MachineType::UintPtr());
    V<Word32> result = CallC(
        &sig, ExternalReference::wasm_memory_fill(),
        {__ BitcastHeapObjectToWordPtr(trusted_instance_data()),
         __ Word32Constant(imm.index), dst_uintptr, value.op, size_uintptr});

    __ TrapIfNot(result, OpIndex::Invalid(), TrapId::kTrapMemOutOfBounds);
  }

  void DataDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    V<FixedUInt32Array> data_segment_sizes =
        LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), DataSegmentSizes,
                                      MemoryRepresentation::TaggedPointer());
    __ Store(data_segment_sizes, __ Word32Constant(0),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::Int32(),
             compiler::kNoWriteBarrier,
             FixedUInt32Array::kHeaderSize + imm.index * kUInt32Size);
  }

  void TableGet(FullDecoder* decoder, const Value& index, Value* result,
                const IndexImmediate& imm) {
    ValueType element_type = decoder->module_->tables[imm.index].type;
    V<WasmTableObject> table = LoadTable(imm.index);
    V<Smi> size_smi = __ Load(table, LoadOp::Kind::TaggedBase(),
                              MemoryRepresentation::TaggedSigned(),
                              WasmTableObject::kCurrentLengthOffset);
    V<Word32> in_bounds = __ Uint32LessThan(index.op, __ UntagSmi(size_smi));
    __ TrapIfNot(in_bounds, OpIndex::Invalid(), TrapId::kTrapTableOutOfBounds);
    V<FixedArray> entries = __ Load(table, LoadOp::Kind::TaggedBase(),
                                    MemoryRepresentation::TaggedPointer(),
                                    WasmTableObject::kEntriesOffset);
    OpIndex entry =
        __ LoadFixedArrayElement(entries, __ ChangeInt32ToIntPtr(index.op));

    if (IsSubtypeOf(element_type, kWasmFuncRef, decoder->module_)) {
      // If the entry has map type Tuple2, call WasmFunctionTableGet which will
      // initialize the function table entry.
      Label<Object> resolved(&asm_);
      Label<> call_runtime(&asm_);
      // The entry is a WasmFuncRef, WasmNull, or Tuple2. Hence
      // it is safe to cast it to HeapObject.
      V<Map> entry_map = __ LoadMapField(V<HeapObject>::Cast(entry));
      V<Word32> instance_type = __ LoadInstanceTypeField(entry_map);
      GOTO_IF(
          UNLIKELY(__ Word32Equal(instance_type, InstanceType::TUPLE2_TYPE)),
          call_runtime);
      // Otherwise the entry is WasmFuncRef or WasmNull; we are done.
      GOTO(resolved, entry);

      BIND(call_runtime);
      GOTO(resolved, CallBuiltinThroughJumptable<
                         BuiltinCallDescriptor::WasmFunctionTableGet>(
                         decoder, {__ IntPtrConstant(imm.index), index.op}));

      BIND(resolved, resolved_entry);
      result->op = resolved_entry;
    } else {
      result->op = entry;
    }
    result->op = AnnotateResultIfReference(result->op, element_type);
  }

  void TableSet(FullDecoder* decoder, const Value& index, const Value& value,
                const IndexImmediate& imm) {
    ValueType element_type = decoder->module_->tables[imm.index].type;
    if (IsSubtypeOf(element_type, kWasmFuncRef, decoder->module_)) {
      CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmTableSetFuncRef>(
          decoder, {__ IntPtrConstant(imm.index), index.op, value.op});
    } else {
      CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmTableSet>(
          decoder, {__ IntPtrConstant(imm.index), index.op, value.op});
    }
  }

  void TableInit(FullDecoder* decoder, const TableInitImmediate& imm,
                 const Value* args) {
    V<Word32> dst = args[0].op;
    V<Word32> src = args[1].op;
    V<Word32> size = args[2].op;
    CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmTableInit>(
        decoder, {dst, src, size, __ NumberConstant(imm.table.index),
                  __ NumberConstant(imm.element_segment.index)});
  }

  void TableCopy(FullDecoder* decoder, const TableCopyImmediate& imm,
                 const Value args[]) {
    V<Word32> dst = args[0].op;
    V<Word32> src = args[1].op;
    V<Word32> size = args[2].op;
    CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmTableCopy>(
        decoder, {dst, src, size, __ NumberConstant(imm.table_dst.index),
                  __ NumberConstant(imm.table_src.index)});
  }

  void TableGrow(FullDecoder* decoder, const IndexImmediate& imm,
                 const Value& value, const Value& delta, Value* result) {
    V<Smi> result_smi =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmTableGrow>(
            decoder, {__ NumberConstant(imm.index), delta.op, value.op});
    result->op = __ UntagSmi(result_smi);
  }

  void TableFill(FullDecoder* decoder, const IndexImmediate& imm,
                 const Value& start, const Value& value, const Value& count) {
    CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmTableFill>(
        decoder, {__ NumberConstant(imm.index), start.op, count.op, value.op});
  }

  V<WasmTableObject> LoadTable(uint32_t table_index) {
    V<FixedArray> tables = LOAD_IMMUTABLE_INSTANCE_FIELD(
        trusted_instance_data(), Tables, MemoryRepresentation::TaggedPointer());
    return V<WasmTableObject>::Cast(
        __ LoadFixedArrayElement(tables, table_index));
  }

  void TableSize(FullDecoder* decoder, const IndexImmediate& imm,
                 Value* result) {
    V<WasmTableObject> table = LoadTable(imm.index);
    V<Smi> size_smi = __ Load(table, LoadOp::Kind::TaggedBase(),
                              MemoryRepresentation::TaggedSigned(),
                              WasmTableObject::kCurrentLengthOffset);
    result->op = __ UntagSmi(size_smi);
  }

  void ElemDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    V<FixedArray> elem_segments =
        LOAD_IMMUTABLE_INSTANCE_FIELD(trusted_instance_data(), ElementSegments,
                                      MemoryRepresentation::TaggedPointer());
    __ StoreFixedArrayElement(elem_segments, imm.index,
                              LOAD_ROOT(EmptyFixedArray),
                              compiler::kFullWriteBarrier);
  }

  void StructNew(FullDecoder* decoder, const StructIndexImmediate& imm,
                 const Value args[], Value* result) {
    uint32_t field_count = imm.struct_type->field_count();
    SmallZoneVector<OpIndex, 16> args_vector(field_count, decoder->zone_);
    for (uint32_t i = 0; i < field_count; ++i) {
      args_vector[i] = args[i].op;
    }
    result->op = StructNewImpl(imm, args_vector.data());
  }

  void StructNewDefault(FullDecoder* decoder, const StructIndexImmediate& imm,
                        Value* result) {
    uint32_t field_count = imm.struct_type->field_count();
    SmallZoneVector<OpIndex, 16> args(field_count, decoder->zone_);
    for (uint32_t i = 0; i < field_count; i++) {
      ValueType field_type = imm.struct_type->field(i);
      args[i] = DefaultValue(field_type);
    }
    result->op = StructNewImpl(imm, args.data());
  }

  void StructGet(FullDecoder* decoder, const Value& struct_object,
                 const FieldImmediate& field, bool is_signed, Value* result) {
    result->op = __ StructGet(
        struct_object.op, field.struct_imm.struct_type, field.struct_imm.index,
        field.field_imm.index, is_signed,
        struct_object.type.is_nullable() ? compiler::kWithNullCheck
                                         : compiler::kWithoutNullCheck);
  }

  void StructSet(FullDecoder* decoder, const Value& struct_object,
                 const FieldImmediate& field, const Value& field_value) {
    __ StructSet(struct_object.op, field_value.op, field.struct_imm.struct_type,
                 field.struct_imm.index, field.field_imm.index,
                 struct_object.type.is_nullable()
                     ? compiler::kWithNullCheck
                     : compiler::kWithoutNullCheck);
  }

  void ArrayNew(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                const Value& length, const Value& initial_value,
                Value* result) {
    result->op = ArrayNewImpl(decoder, imm.index, imm.array_type, length.op,
                              initial_value.op);
  }

  void ArrayNewDefault(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                       const Value& length, Value* result) {
    OpIndex initial_value = DefaultValue(imm.array_type->element_type());
    result->op = ArrayNewImpl(decoder, imm.index, imm.array_type, length.op,
                              initial_value);
  }

  void ArrayGet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index,
                bool is_signed, Value* result) {
    BoundsCheckArray(array_obj.op, index.op, array_obj.type);
    result->op = __ ArrayGet(array_obj.op, index.op, imm.array_type, is_signed);
  }

  void ArraySet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index,
                const Value& value) {
    BoundsCheckArray(array_obj.op, index.op, array_obj.type);
    __ ArraySet(array_obj.op, index.op, value.op,
                imm.array_type->element_type());
  }

  void ArrayLen(FullDecoder* decoder, const Value& array_obj, Value* result) {
    result->op =
        __ ArrayLength(array_obj.op, array_obj.type.is_nullable()
                                         ? compiler::kWithNullCheck
                                         : compiler::kWithoutNullCheck);
  }

  void ArrayCopy(FullDecoder* decoder, const Value& dst, const Value& dst_index,
                 const Value& src, const Value& src_index,
                 const ArrayIndexImmediate& src_imm, const Value& length) {
    BoundsCheckArrayWithLength(dst.op, dst_index.op, length.op,
                               dst.type.is_nullable()
                                   ? compiler::kWithNullCheck
                                   : compiler::kWithoutNullCheck);
    BoundsCheckArrayWithLength(src.op, src_index.op, length.op,
                               src.type.is_nullable()
                                   ? compiler::kWithNullCheck
                                   : compiler::kWithoutNullCheck);

    ValueType element_type = src_imm.array_type->element_type();

    IF_NOT (__ Word32Equal(length.op, 0)) {
      // Values determined by test/mjsunit/wasm/array-copy-benchmark.js on x64.
      int array_copy_max_loop_length;
      switch (element_type.kind()) {
        case wasm::kI32:
        case wasm::kI64:
        case wasm::kI8:
        case wasm::kI16:
          array_copy_max_loop_length = 20;
          break;
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

      IF (__ Uint32LessThan(array_copy_max_loop_length, length.op)) {
        // Builtin
        MachineType arg_types[]{
            MachineType::TaggedPointer(), MachineType::TaggedPointer(),
            MachineType::Uint32(),        MachineType::TaggedPointer(),
            MachineType::Uint32(),        MachineType::Uint32()};
        MachineSignature sig(0, 6, arg_types);

        CallC(&sig, ExternalReference::wasm_array_copy(),
              {trusted_instance_data(), dst.op, dst_index.op, src.op,
               src_index.op, length.op});
      } ELSE {
        V<Word32> src_end_index =
            __ Word32Sub(__ Word32Add(src_index.op, length.op), 1);

        IF (__ Uint32LessThan(src_index.op, dst_index.op)) {
          // Reverse
          V<Word32> dst_end_index =
              __ Word32Sub(__ Word32Add(dst_index.op, length.op), 1);
          ScopedVar<Word32> src_index_loop(this, src_end_index);
          ScopedVar<Word32> dst_index_loop(this, dst_end_index);

          WHILE(__ Word32Constant(1)) {
            OpIndex value =
                __ ArrayGet(src.op, src_index_loop, src_imm.array_type, true);
            __ ArraySet(dst.op, dst_index_loop, value, element_type);

            IF_NOT (__ Uint32LessThan(src_index.op, src_index_loop)) BREAK;

            src_index_loop = __ Word32Sub(src_index_loop, 1);
            dst_index_loop = __ Word32Sub(dst_index_loop, 1);
          }
        } ELSE {
          ScopedVar<Word32> src_index_loop(this, src_index.op);
          ScopedVar<Word32> dst_index_loop(this, dst_index.op);

          WHILE(__ Word32Constant(1)) {
            OpIndex value =
                __ ArrayGet(src.op, src_index_loop, src_imm.array_type, true);
            __ ArraySet(dst.op, dst_index_loop, value, element_type);

            IF_NOT (__ Uint32LessThan(src_index_loop, src_end_index)) BREAK;

            src_index_loop = __ Word32Add(src_index_loop, 1);
            dst_index_loop = __ Word32Add(dst_index_loop, 1);
          }
        }
      }
    }
  }

  void ArrayFill(FullDecoder* decoder, ArrayIndexImmediate& imm,
                 const Value& array, const Value& index, const Value& value,
                 const Value& length) {
    const bool emit_write_barrier =
        imm.array_type->element_type().is_reference();
    BoundsCheckArrayWithLength(array.op, index.op, length.op,
                               array.type.is_nullable()
                                   ? compiler::kWithNullCheck
                                   : compiler::kWithoutNullCheck);
    ArrayFillImpl(array.op, index.op, value.op, length.op, imm.array_type,
                  emit_write_barrier);
  }

  void ArrayNewFixed(FullDecoder* decoder, const ArrayIndexImmediate& array_imm,
                     const IndexImmediate& length_imm, const Value elements[],
                     Value* result) {
    const wasm::ArrayType* type = array_imm.array_type;
    wasm::ValueType element_type = type->element_type();
    int element_count = length_imm.index;
    // Initialize the array header.
    V<Map> rtt =
        __ RttCanon(instance_cache_.managed_object_maps(), array_imm.index);
    V<HeapObject> array = __ WasmAllocateArray(rtt, element_count, type);
    // Initialize all elements.
    for (int i = 0; i < element_count; i++) {
      __ ArraySet(array, __ Word32Constant(i), elements[i].op, element_type);
    }
    result->op = array;
  }

  void ArrayNewSegment(FullDecoder* decoder,
                       const ArrayIndexImmediate& array_imm,
                       const IndexImmediate& segment_imm, const Value& offset,
                       const Value& length, Value* result) {
    bool is_element = array_imm.array_type->element_type().is_reference();
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmArrayNewSegment>(
            decoder,
            {__ Word32Constant(segment_imm.index), offset.op, length.op,
             __ SmiConstant(Smi::FromInt(is_element ? 1 : 0)),
             __ RttCanon(instance_cache_.managed_object_maps(),
                         array_imm.index)});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void ArrayInitSegment(FullDecoder* decoder,
                        const ArrayIndexImmediate& array_imm,
                        const IndexImmediate& segment_imm, const Value& array,
                        const Value& array_index, const Value& segment_offset,
                        const Value& length) {
    bool is_element = array_imm.array_type->element_type().is_reference();
    CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmArrayInitSegment>(
        decoder, {array_index.op, segment_offset.op, length.op,
                  __ SmiConstant(Smi::FromInt(segment_imm.index)),
                  __ SmiConstant(Smi::FromInt(is_element ? 1 : 0)), array.op});
  }

  void RefI31(FullDecoder* decoder, const Value& input, Value* result) {
    if constexpr (SmiValuesAre31Bits()) {
      V<Word32> shifted =
          __ Word32ShiftLeft(input.op, kSmiTagSize + kSmiShiftSize);
      if constexpr (Is64()) {
        // The uppermost bits don't matter.
        result->op = __ BitcastWord32ToWord64(shifted);
      } else {
        result->op = shifted;
      }
    } else {
      // Set the topmost bit to sign-extend the second bit. This way,
      // interpretation in JS (if this value escapes there) will be the same as
      // i31.get_s.
      V<WordPtr> input_wordptr = __ ChangeUint32ToUintPtr(input.op);
      result->op = __ WordPtrShiftRightArithmetic(
          __ WordPtrShiftLeft(input_wordptr, kSmiShiftSize + kSmiTagSize + 1),
          1);
    }
    result->op = __ AnnotateWasmType(__ BitcastWordPtrToSmi(result->op),
                                     kWasmI31Ref.AsNonNull());
  }

  void I31GetS(FullDecoder* decoder, const Value& input, Value* result) {
    V<Object> input_non_null = NullCheck(input);
    if constexpr (SmiValuesAre31Bits()) {
      result->op = __ Word32ShiftRightArithmeticShiftOutZeros(
          __ TruncateWordPtrToWord32(__ BitcastTaggedToWordPtr(input_non_null)),
          kSmiTagSize + kSmiShiftSize);
    } else {
      // Topmost bit is already sign-extended.
      result->op = __ TruncateWordPtrToWord32(
          __ WordPtrShiftRightArithmeticShiftOutZeros(
              __ BitcastTaggedToWordPtr(input_non_null),
              kSmiTagSize + kSmiShiftSize));
    }
  }

  void I31GetU(FullDecoder* decoder, const Value& input, Value* result) {
    V<Object> input_non_null = NullCheck(input);
    if constexpr (SmiValuesAre31Bits()) {
      result->op = __ Word32ShiftRightLogical(
          __ TruncateWordPtrToWord32(__ BitcastTaggedToWordPtr(input_non_null)),
          kSmiTagSize + kSmiShiftSize);
    } else {
      // Topmost bit is sign-extended, remove it.
      result->op = __ TruncateWordPtrToWord32(__ WordPtrShiftRightLogical(
          __ WordPtrShiftLeft(__ BitcastTaggedToWordPtr(input_non_null), 1),
          kSmiTagSize + kSmiShiftSize + 1));
    }
  }

  void RefTest(FullDecoder* decoder, uint32_t ref_index, const Value& object,
               Value* result, bool null_succeeds) {
    V<Map> rtt = __ RttCanon(instance_cache_.managed_object_maps(), ref_index);
    compiler::WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         ref_index, null_succeeds ? kNullable : kNonNullable)};
    result->op = __ WasmTypeCheck(object.op, rtt, config);
  }

  void RefTestAbstract(FullDecoder* decoder, const Value& object, HeapType type,
                       Value* result, bool null_succeeds) {
    compiler::WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         type, null_succeeds ? kNullable : kNonNullable)};
    V<Map> rtt = OpIndex::Invalid();
    result->op = __ WasmTypeCheck(object.op, rtt, config);
  }

  void RefCast(FullDecoder* decoder, uint32_t ref_index, const Value& object,
               Value* result, bool null_succeeds) {
    if (v8_flags.experimental_wasm_assume_ref_cast_succeeds) {
      // TODO(14108): Implement type guards.
      Forward(decoder, object, result);
      return;
    }
    V<Map> rtt = __ RttCanon(instance_cache_.managed_object_maps(), ref_index);
    DCHECK_EQ(result->type.is_nullable(), null_succeeds);
    compiler::WasmTypeCheckConfig config{object.type, result->type};
    result->op = __ WasmTypeCast(object.op, rtt, config);
  }

  void RefCastAbstract(FullDecoder* decoder, const Value& object, HeapType type,
                       Value* result, bool null_succeeds) {
    if (v8_flags.experimental_wasm_assume_ref_cast_succeeds) {
      // TODO(14108): Implement type guards.
      Forward(decoder, object, result);
      return;
    }
    // TODO(jkummerow): {type} is redundant.
    DCHECK_IMPLIES(null_succeeds, result->type.is_nullable());
    DCHECK_EQ(type, result->type.heap_type());
    compiler::WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         type, null_succeeds ? kNullable : kNonNullable)};
    V<Map> rtt = OpIndex::Invalid();
    result->op = __ WasmTypeCast(object.op, rtt, config);
  }

  void BrOnCast(FullDecoder* decoder, uint32_t ref_index, const Value& object,
                Value* value_on_branch, uint32_t br_depth, bool null_succeeds) {
    V<Map> rtt = __ RttCanon(instance_cache_.managed_object_maps(), ref_index);
    compiler::WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         ref_index, null_succeeds ? kNullable : kNonNullable)};
    return BrOnCastImpl(decoder, rtt, config, object, value_on_branch, br_depth,
                        null_succeeds);
  }

  void BrOnCastAbstract(FullDecoder* decoder, const Value& object,
                        HeapType type, Value* value_on_branch,
                        uint32_t br_depth, bool null_succeeds) {
    V<Map> rtt = OpIndex::Invalid();
    compiler::WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         type, null_succeeds ? kNullable : kNonNullable)};
    return BrOnCastImpl(decoder, rtt, config, object, value_on_branch, br_depth,
                        null_succeeds);
  }

  void BrOnCastFail(FullDecoder* decoder, uint32_t ref_index,
                    const Value& object, Value* value_on_fallthrough,
                    uint32_t br_depth, bool null_succeeds) {
    V<Map> rtt = __ RttCanon(instance_cache_.managed_object_maps(), ref_index);
    compiler::WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         ref_index, null_succeeds ? kNullable : kNonNullable)};
    return BrOnCastFailImpl(decoder, rtt, config, object, value_on_fallthrough,
                            br_depth, null_succeeds);
  }

  void BrOnCastFailAbstract(FullDecoder* decoder, const Value& object,
                            HeapType type, Value* value_on_fallthrough,
                            uint32_t br_depth, bool null_succeeds) {
    V<Map> rtt = OpIndex::Invalid();
    compiler::WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         type, null_succeeds ? kNullable : kNonNullable)};
    return BrOnCastFailImpl(decoder, rtt, config, object, value_on_fallthrough,
                            br_depth, null_succeeds);
  }

  void StringNewWtf8(FullDecoder* decoder, const MemoryIndexImmediate& memory,
                     const unibrow::Utf8Variant variant, const Value& offset,
                     const Value& size, Value* result) {
    V<Smi> memory_smi = __ SmiConstant(Smi::FromInt(memory.index));
    V<Smi> variant_smi =
        __ SmiConstant(Smi::FromInt(static_cast<int>(variant)));
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmStringNewWtf8>(
            decoder, {offset.op, size.op, memory_smi, variant_smi});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  // TODO(jkummerow): This check would be more elegant if we made
  // {ArrayNewSegment} a high-level node that's lowered later.
  // Returns the call on success, nullptr otherwise (like `TryCast`).
  const CallOp* IsArrayNewSegment(V<Object> array) {
    DCHECK_IMPLIES(!array.valid(), __ generating_unreachable_operations());
    if (__ generating_unreachable_operations()) return nullptr;
    if (const WasmTypeAnnotationOp* annotation =
            __ output_graph().Get(array).TryCast<WasmTypeAnnotationOp>()) {
      array = annotation->value();
    }
    if (const DidntThrowOp* didnt_throw =
            __ output_graph().Get(array).TryCast<DidntThrowOp>()) {
      array = didnt_throw->throwing_operation();
    }
    const CallOp* call = __ output_graph().Get(array).TryCast<CallOp>();
    if (call == nullptr) return nullptr;
    uint64_t stub_id{};
    if (!OperationMatcher(__ output_graph())
             .MatchWasmStubCallConstant(call->callee(), &stub_id)) {
      return nullptr;
    }
    DCHECK_LT(stub_id, static_cast<uint64_t>(Builtin::kFirstBytecodeHandler));
    if (stub_id == static_cast<uint64_t>(Builtin::kWasmArrayNewSegment)) {
      return call;
    }
    return nullptr;
  }

  V<Object> StringNewWtf8ArrayImpl(FullDecoder* decoder,
                                   const unibrow::Utf8Variant variant,
                                   const Value& array, const Value& start,
                                   const Value& end, ValueType result_type) {
    // Special case: shortcut a sequence "array from data segment" + "string
    // from wtf8 array" to directly create a string from the segment.
    V<Object> call;
    if (const CallOp* array_new = IsArrayNewSegment(array.op)) {
      // We can only pass 3 untagged parameters to the builtin (on 32-bit
      // platforms). The segment index is easy to tag: if it validated, it must
      // be in Smi range.
      OpIndex segment_index = array_new->input(1);
      int32_t index_val;
      OperationMatcher(__ output_graph())
          .MatchIntegralWord32Constant(segment_index, &index_val);
      V<Smi> index_smi = __ SmiConstant(Smi::FromInt(index_val));
      // Arbitrary choice for the second tagged parameter: the segment offset.
      OpIndex segment_offset = array_new->input(2);
      __ TrapIfNot(
          __ Uint32LessThan(segment_offset, __ Word32Constant(Smi::kMaxValue)),
          OpIndex::Invalid(), TrapId::kTrapDataSegmentOutOfBounds);
      V<Smi> offset_smi = __ TagSmi(segment_offset);
      OpIndex segment_length = array_new->input(3);
      V<Smi> variant_smi =
          __ SmiConstant(Smi::FromInt(static_cast<int32_t>(variant)));
      call = CallBuiltinThroughJumptable<
          BuiltinCallDescriptor::WasmStringFromDataSegment>(
          decoder, {segment_length, start.op, end.op, index_smi, offset_smi,
                    variant_smi});
    } else {
      // Regular path if the shortcut wasn't taken.
      call = CallBuiltinThroughJumptable<
          BuiltinCallDescriptor::WasmStringNewWtf8Array>(
          decoder,
          {start.op, end.op, V<WasmArray>::Cast(NullCheck(array)),
           __ SmiConstant(Smi::FromInt(static_cast<int32_t>(variant)))});
    }
    DCHECK_IMPLIES(variant == unibrow::Utf8Variant::kUtf8NoTrap,
                   result_type.is_nullable());
    // The builtin returns a WasmNull for kUtf8NoTrap, so nullable values in
    // combination with extern strings are not supported.
    DCHECK_NE(result_type, wasm::kWasmExternRef);
    return AnnotateAsString(call, result_type);
  }

  void StringNewWtf8Array(FullDecoder* decoder,
                          const unibrow::Utf8Variant variant,
                          const Value& array, const Value& start,
                          const Value& end, Value* result) {
    result->op = StringNewWtf8ArrayImpl(decoder, variant, array, start, end,
                                        result->type);
  }

  void StringNewWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                      const Value& offset, const Value& size, Value* result) {
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmStringNewWtf16>(
            decoder, {__ Word32Constant(imm.index), offset.op, size.op});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringNewWtf16Array(FullDecoder* decoder, const Value& array,
                           const Value& start, const Value& end,
                           Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringNewWtf16Array>(
        decoder, {V<WasmArray>::Cast(NullCheck(array)), start.op, end.op});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringConst(FullDecoder* decoder, const StringConstImmediate& imm,
                   Value* result) {
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmStringConst>(
            decoder, {__ Word32Constant(imm.index)});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringMeasureWtf8(FullDecoder* decoder,
                         const unibrow::Utf8Variant variant, const Value& str,
                         Value* result) {
    result->op = StringMeasureWtf8Impl(decoder, variant,
                                       V<String>::Cast(NullCheck(str)));
  }

  OpIndex StringMeasureWtf8Impl(FullDecoder* decoder,
                                const unibrow::Utf8Variant variant,
                                V<String> string) {
    switch (variant) {
      case unibrow::Utf8Variant::kUtf8:
        return CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringMeasureUtf8>(decoder, {string});
      case unibrow::Utf8Variant::kLossyUtf8:
      case unibrow::Utf8Variant::kWtf8:
        return CallBuiltinThroughJumptable<
            BuiltinCallDescriptor::WasmStringMeasureWtf8>(decoder, {string});
      case unibrow::Utf8Variant::kUtf8NoTrap:
        UNREACHABLE();
    }
  }

  V<Word32> LoadStringLength(V<Object> string) {
    return __ template LoadField<Word32>(
        string, compiler::AccessBuilder::ForStringLength());
  }

  void StringMeasureWtf16(FullDecoder* decoder, const Value& str,
                          Value* result) {
    result->op = LoadStringLength(NullCheck(str));
  }

  void StringEncodeWtf8(FullDecoder* decoder,
                        const MemoryIndexImmediate& memory,
                        const unibrow::Utf8Variant variant, const Value& str,
                        const Value& offset, Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringEncodeWtf8>(
        decoder, {V<String>::Cast(NullCheck(str)), offset.op,
                  __ SmiConstant(Smi::FromInt(memory.index)),
                  __ SmiConstant(Smi::FromInt(static_cast<int32_t>(variant)))});
  }

  void StringEncodeWtf8Array(FullDecoder* decoder,
                             const unibrow::Utf8Variant variant,
                             const Value& str, const Value& array,
                             const Value& start, Value* result) {
    result->op = StringEncodeWtf8ArrayImpl(
        decoder, variant, V<String>::Cast(NullCheck(str)),
        V<WasmArray>::Cast(NullCheck(array)), start.op);
  }

  OpIndex StringEncodeWtf8ArrayImpl(FullDecoder* decoder,
                                    const unibrow::Utf8Variant variant,
                                    V<String> str, V<WasmArray> array,
                                    V<Word32> start) {
    return CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringEncodeWtf8Array>(
        decoder, {str, array, start,
                  __ SmiConstant(Smi::FromInt(static_cast<int32_t>(variant)))});
  }

  void StringEncodeWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                         const Value& str, const Value& offset, Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringEncodeWtf16>(
        decoder,
        {V<String>::Cast(NullCheck(str)), offset.op,
         __ SmiConstant(Smi::FromInt(static_cast<int32_t>(imm.index)))});
  }

  void StringEncodeWtf16Array(FullDecoder* decoder, const Value& str,
                              const Value& array, const Value& start,
                              Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringEncodeWtf16Array>(
        decoder, {V<String>::Cast(NullCheck(str)),
                  V<WasmArray>::Cast(NullCheck(array)), start.op});
  }

  void StringConcat(FullDecoder* decoder, const Value& head, const Value& tail,
                    Value* result) {
    V<NativeContext> native_context = instance_cache_.native_context();
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::StringAdd_CheckNone>(
            decoder, native_context,
            {V<String>::Cast(NullCheck(head)),
             V<String>::Cast(NullCheck(tail))});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  V<Word32> StringEqImpl(FullDecoder* decoder, V<String> a, V<String> b,
                         ValueType a_type, ValueType b_type) {
    Label<Word32> done(&asm_);
    // Covers "identical string pointer" and "both are null" cases.
    GOTO_IF(__ TaggedEqual(a, b), done, __ Word32Constant(1));
    if (a_type.is_nullable()) {
      GOTO_IF(__ IsNull(a, a_type), done, __ Word32Constant(0));
    }
    if (b_type.is_nullable()) {
      GOTO_IF(__ IsNull(b, b_type), done, __ Word32Constant(0));
    }
    // TODO(jkummerow): Call Builtin::kStringEqual directly.
    GOTO(done,
         CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmStringEqual>(
             decoder, {a, b}));
    BIND(done, eq_result);
    return eq_result;
  }

  void StringEq(FullDecoder* decoder, const Value& a, const Value& b,
                Value* result) {
    result->op = StringEqImpl(decoder, a.op, b.op, a.type, b.type);
  }

  void StringIsUSVSequence(FullDecoder* decoder, const Value& str,
                           Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringIsUSVSequence>(
        decoder, {V<String>::Cast(NullCheck(str))});
  }

  void StringAsWtf8(FullDecoder* decoder, const Value& str, Value* result) {
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmStringAsWtf8>(
            decoder, {V<String>::Cast(NullCheck(str))});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringViewWtf8Advance(FullDecoder* decoder, const Value& view,
                             const Value& pos, const Value& bytes,
                             Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewWtf8Advance>(
        decoder, {V<ByteArray>::Cast(NullCheck(view)), pos.op, bytes.op});
  }

  void StringViewWtf8Encode(FullDecoder* decoder,
                            const MemoryIndexImmediate& memory,
                            const unibrow::Utf8Variant variant,
                            const Value& view, const Value& addr,
                            const Value& pos, const Value& bytes,
                            Value* next_pos, Value* bytes_written) {
    OpIndex result = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewWtf8Encode>(
        decoder,
        {addr.op, pos.op, bytes.op, V<ByteArray>::Cast(NullCheck(view)),
         __ SmiConstant(Smi::FromInt(memory.index)),
         __ SmiConstant(Smi::FromInt(static_cast<int32_t>(variant)))});
    next_pos->op = __ Projection(result, 0, RepresentationFor(next_pos->type));
    bytes_written->op =
        __ Projection(result, 1, RepresentationFor(bytes_written->type));
  }

  void StringViewWtf8Slice(FullDecoder* decoder, const Value& view,
                           const Value& start, const Value& end,
                           Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewWtf8Slice>(
        decoder, {V<ByteArray>::Cast(NullCheck(view)), start.op, end.op});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringAsWtf16(FullDecoder* decoder, const Value& str, Value* result) {
    result->op = __ StringAsWtf16(NullCheck(str));
  }

  V<Word32> GetCodeUnitImpl(FullDecoder* decoder, V<String> string,
                            V<Word32> offset) {
    OpIndex prepare = __ StringPrepareForGetCodeUnit(string);
    V<Object> base =
        __ Projection(prepare, 0, RegisterRepresentation::Tagged());
    V<WordPtr> base_offset =
        __ Projection(prepare, 1, RegisterRepresentation::WordPtr());
    V<Word32> charwidth_shift =
        __ Projection(prepare, 2, RegisterRepresentation::Word32());

    // Bounds check.
    V<Word32> length = LoadStringLength(string);
    __ TrapIfNot(__ Uint32LessThan(offset, length), OpIndex::Invalid(),
                 TrapId::kTrapStringOffsetOutOfBounds);

    Label<> onebyte(&asm_);
    Label<> bailout(&asm_);
    Label<Word32> done(&asm_);
    GOTO_IF(UNLIKELY(__ Word32Equal(charwidth_shift,
                                    compiler::kCharWidthBailoutSentinel)),
            bailout);
    GOTO_IF(__ Word32Equal(charwidth_shift, 0), onebyte);

    // Two-byte.
    V<WordPtr> object_offset = __ WordPtrAdd(
        __ WordPtrMul(__ ChangeInt32ToIntPtr(offset), 2), base_offset);
    // Bitcast the tagged to a wordptr as the offset already contains the
    // kHeapObjectTag handling. Furthermore, in case of external strings the
    // tagged value is a smi 0, which doesn't really encode a tagged load.
    V<WordPtr> base_ptr = __ BitcastTaggedToWordPtr(base);
    V<Word32> result_value =
        __ Load(base_ptr, object_offset, LoadOp::Kind::RawAligned().Immutable(),
                MemoryRepresentation::Uint16());
    GOTO(done, result_value);

    // One-byte.
    BIND(onebyte);
    object_offset = __ WordPtrAdd(__ ChangeInt32ToIntPtr(offset), base_offset);
    // Bitcast the tagged to a wordptr as the offset already contains the
    // kHeapObjectTag handling. Furthermore, in case of external strings the
    // tagged value is a smi 0, which doesn't really encode a tagged load.
    base_ptr = __ BitcastTaggedToWordPtr(base);
    result_value =
        __ Load(base_ptr, object_offset, LoadOp::Kind::RawAligned().Immutable(),
                MemoryRepresentation::Uint8());
    GOTO(done, result_value);

    BIND(bailout);
    GOTO(done, CallBuiltinThroughJumptable<
                   BuiltinCallDescriptor::WasmStringViewWtf16GetCodeUnit>(
                   decoder, {string, offset}));

    BIND(done, final_result);
    // Make sure the original string is kept alive as long as we're operating
    // on pointers extracted from it (otherwise e.g. external strings' resources
    // might get freed prematurely).
    __ Retain(string);
    return final_result;
  }

  void StringViewWtf16GetCodeUnit(FullDecoder* decoder, const Value& view,
                                  const Value& pos, Value* result) {
    result->op =
        GetCodeUnitImpl(decoder, V<String>::Cast(NullCheck(view)), pos.op);
  }

  V<Word32> StringCodePointAt(FullDecoder* decoder, V<String> string,
                              V<Word32> offset) {
    OpIndex prepare = __ StringPrepareForGetCodeUnit(string);
    V<Object> base =
        __ Projection(prepare, 0, RegisterRepresentation::Tagged());
    V<WordPtr> base_offset =
        __ Projection(prepare, 1, RegisterRepresentation::WordPtr());
    V<Word32> charwidth_shift =
        __ Projection(prepare, 2, RegisterRepresentation::Word32());

    // Bounds check.
    V<Word32> length = LoadStringLength(string);
    __ TrapIfNot(__ Uint32LessThan(offset, length), OpIndex::Invalid(),
                 TrapId::kTrapStringOffsetOutOfBounds);

    Label<> onebyte(&asm_);
    Label<> bailout(&asm_);
    Label<Word32> done(&asm_);
    GOTO_IF(
        __ Word32Equal(charwidth_shift, compiler::kCharWidthBailoutSentinel),
        bailout);
    GOTO_IF(__ Word32Equal(charwidth_shift, 0), onebyte);

    // Two-byte.
    V<WordPtr> object_offset = __ WordPtrAdd(
        __ WordPtrMul(__ ChangeInt32ToIntPtr(offset), 2), base_offset);
    // Bitcast the tagged to a wordptr as the offset already contains the
    // kHeapObjectTag handling. Furthermore, in case of external strings the
    // tagged value is a smi 0, which doesn't really encode a tagged load.
    V<WordPtr> base_ptr = __ BitcastTaggedToWordPtr(base);
    V<Word32> lead =
        __ Load(base_ptr, object_offset, LoadOp::Kind::RawAligned().Immutable(),
                MemoryRepresentation::Uint16());
    V<Word32> is_lead_surrogate =
        __ Word32Equal(__ Word32BitwiseAnd(lead, 0xFC00), 0xD800);
    GOTO_IF_NOT(is_lead_surrogate, done, lead);
    V<Word32> trail_offset = __ Word32Add(offset, 1);
    GOTO_IF_NOT(__ Uint32LessThan(trail_offset, length), done, lead);
    V<Word32> trail = __ Load(
        base_ptr, __ WordPtrAdd(object_offset, __ IntPtrConstant(2)),
        LoadOp::Kind::RawAligned().Immutable(), MemoryRepresentation::Uint16());
    V<Word32> is_trail_surrogate =
        __ Word32Equal(__ Word32BitwiseAnd(trail, 0xFC00), 0xDC00);
    GOTO_IF_NOT(is_trail_surrogate, done, lead);
    V<Word32> surrogate_bias =
        __ Word32Constant(0x10000 - (0xD800 << 10) - 0xDC00);
    V<Word32> result = __ Word32Add(__ Word32ShiftLeft(lead, 10),
                                    __ Word32Add(trail, surrogate_bias));
    GOTO(done, result);

    // One-byte.
    BIND(onebyte);
    object_offset = __ WordPtrAdd(__ ChangeInt32ToIntPtr(offset), base_offset);
    // Bitcast the tagged to a wordptr as the offset already contains the
    // kHeapObjectTag handling. Furthermore, in case of external strings the
    // tagged value is a smi 0, which doesn't really encode a tagged load.
    base_ptr = __ BitcastTaggedToWordPtr(base);
    result =
        __ Load(base_ptr, object_offset, LoadOp::Kind::RawAligned().Immutable(),
                MemoryRepresentation::Uint8());
    GOTO(done, result);

    BIND(bailout);
    GOTO(done, CallBuiltinThroughJumptable<
                   BuiltinCallDescriptor::WasmStringCodePointAt>(
                   decoder, {string, offset}));

    BIND(done, final_result);
    // Make sure the original string is kept alive as long as we're operating
    // on pointers extracted from it (otherwise e.g. external strings' resources
    // might get freed prematurely).
    __ Retain(string);
    return final_result;
  }

  void StringViewWtf16Encode(FullDecoder* decoder,
                             const MemoryIndexImmediate& imm, const Value& view,
                             const Value& offset, const Value& pos,
                             const Value& codeunits, Value* result) {
    V<String> string = V<String>::Cast(NullCheck(view));
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewWtf16Encode>(
        decoder, {offset.op, pos.op, codeunits.op, string,
                  __ SmiConstant(Smi::FromInt(imm.index))});
  }

  void StringViewWtf16Slice(FullDecoder* decoder, const Value& view,
                            const Value& start, const Value& end,
                            Value* result) {
    V<String> string = V<String>::Cast(NullCheck(view));
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewWtf16Slice>(
        decoder, {string, start.op, end.op});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringAsIter(FullDecoder* decoder, const Value& str, Value* result) {
    V<String> string = V<String>::Cast(NullCheck(str));
    result->op =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmStringAsIter>(
            decoder, {string});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringViewIterNext(FullDecoder* decoder, const Value& view,
                          Value* result) {
    V<Object> string = NullCheck(view);
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewIterNext>(decoder, {string});
  }

  void StringViewIterAdvance(FullDecoder* decoder, const Value& view,
                             const Value& codepoints, Value* result) {
    V<Object> string = NullCheck(view);
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewIterAdvance>(
        decoder, {string, codepoints.op});
  }

  void StringViewIterRewind(FullDecoder* decoder, const Value& view,
                            const Value& codepoints, Value* result) {
    V<Object> string = NullCheck(view);
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewIterRewind>(
        decoder, {string, codepoints.op});
  }

  void StringViewIterSlice(FullDecoder* decoder, const Value& view,
                           const Value& codepoints, Value* result) {
    V<Object> string = NullCheck(view);
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringViewIterSlice>(
        decoder, {string, codepoints.op});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringCompare(FullDecoder* decoder, const Value& lhs, const Value& rhs,
                     Value* result) {
    V<String> lhs_val = V<String>::Cast(NullCheck(lhs));
    V<String> rhs_val = V<String>::Cast(NullCheck(rhs));
    result->op = __ UntagSmi(
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::StringCompare>(
            decoder, {lhs_val, rhs_val}));
  }

  void StringFromCodePoint(FullDecoder* decoder, const Value& code_point,
                           Value* result) {
    result->op = CallBuiltinThroughJumptable<
        BuiltinCallDescriptor::WasmStringFromCodePoint>(decoder,
                                                        {code_point.op});
    result->op = __ AnnotateWasmType(result->op, result->type);
  }

  void StringHash(FullDecoder* decoder, const Value& string, Value* result) {
    V<String> string_val = V<String>::Cast(NullCheck(string));

    Label<> runtime_label(&Asm());
    Label<Word32> end_label(&Asm());

    V<Word32> raw_hash = __ template LoadField<Word32>(
        string_val, compiler::AccessBuilder::ForNameRawHashField());
    V<Word32> hash_not_computed_mask =
        __ Word32Constant(static_cast<int32_t>(Name::kHashNotComputedMask));
    static_assert(Name::HashFieldTypeBits::kShift == 0);
    V<Word32> hash_not_computed =
        __ Word32BitwiseAnd(raw_hash, hash_not_computed_mask);
    GOTO_IF(hash_not_computed, runtime_label);

    // Fast path if hash is already computed: Decode raw hash value.
    static_assert(Name::HashBits::kLastUsedBit == kBitsPerInt - 1);
    V<Word32> hash = __ Word32ShiftRightLogical(
        raw_hash, static_cast<int32_t>(Name::HashBits::kShift));
    GOTO(end_label, hash);

    BIND(runtime_label);
    V<Word32> hash_runtime =
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmStringHash>(
            decoder, {string_val});
    GOTO(end_label, hash_runtime);

    BIND(end_label, hash_val);
    result->op = hash_val;
  }

  void Forward(FullDecoder* decoder, const Value& from, Value* to) {
    to->op = from.op;
  }

  bool did_bailout() { return did_bailout_; }

 private:
  // The InstanceCache caches commonly used fields of the
  // WasmTrustedInstanceData.
  // We can extend the set of cached fields as needed.
  // This caching serves two purposes:
  // (1) It makes sure that the respective fields are loaded early on, as
  //     opposed to within conditional branches, so the values are easily
  //     reusable.
  // (2) It makes sure that the loaded values are actually reused.
  // It achieves these effects more reliably and more cheaply than general-
  // purpose optimizations could (loop peeling isn't always used; load
  // elimination struggles with arbitrary side effects of indexed stores;
  // we don't currently have a generic mechanism for hoisting loads out of
  // conditional branches).
  class InstanceCache {
   public:
    explicit InstanceCache(Assembler& assembler) : asm_(assembler) {}

    void Initialize(V<WasmTrustedInstanceData> trusted_instance_data,
                    const WasmModule* mod) {
      DCHECK(!trusted_data_.valid());  // Only call {Initialize()} once.
      trusted_data_ = trusted_instance_data;
      managed_object_maps_ =
          __ Load(trusted_instance_data, LoadOp::Kind::TaggedBase().Immutable(),
                  MemoryRepresentation::TaggedPointer(),
                  WasmTrustedInstanceData::kManagedObjectMapsOffset);
      native_context_ =
          __ Load(trusted_instance_data, LoadOp::Kind::TaggedBase().Immutable(),
                  MemoryRepresentation::TaggedPointer(),
                  WasmTrustedInstanceData::kNativeContextOffset);

      if (!mod->memories.empty()) {
#if DEBUG
        has_memory_ = true;
#endif
        const WasmMemory& mem = mod->memories[0];
        memory_can_grow_ = mem.initial_pages != mem.maximum_pages;
        // For now, we don't cache the size of shared growable memories.
        // If we wanted to support this case, we would have to reload the
        // memory size when loop stack checks detect an interrupt request.
        // Since memory size caching is particularly important for asm.js,
        // which never uses growable or shared memories, this limitation is
        // considered acceptable for now.
        memory_size_cached_ = !mem.is_shared || !memory_can_grow_;
        // Trap handler enabled memories never move.
        // Memories that can't grow have no reason to move.
        // Shared memories can only be grown in-place.
        bool memory_can_move = mem.bounds_checks != kTrapHandler &&
                               memory_can_grow_ && !mem.is_shared;
        memory_is_shared_ = mem.is_shared;
        if (memory_size_cached_) {
          if (memory_can_grow_) memory_size_index_ = num_mutable_fields_++;
          mem_size_ = LoadMemSize();
        }
        if (memory_can_move) {
          memory_start_index_ = num_mutable_fields_++;
        }
        mem_start_ = LoadMemStart();
      }
    }

    using Snapshot = base::SmallVector<OpIndex, 2>;

    Snapshot SaveState() {
      Snapshot snapshot(num_mutable_fields_);
      for (uint32_t i = 0; i < num_mutable_fields_; i++) {
        snapshot[i] = mutable_field_value(i);
      }
      return snapshot;
    }

    void RestoreFromSnapshot(Snapshot& snapshot) {
      DCHECK_EQ(snapshot.size(), num_mutable_fields_);
      for (uint32_t i = 0; i < num_mutable_fields_; i++) {
        set_mutable_field_value(i, snapshot[i]);
      }
    }

    // TODO(14108): Port the dynamic "cached_memory_index" infrastructure
    // from Turbofan.
    void ReloadCachedMemory() {
      if (memory_can_move()) mem_start_ = LoadMemStart();
      if (memory_can_grow_ && memory_size_cached_) mem_size_ = LoadMemSize();
    }

    uint32_t num_mutable_fields() { return num_mutable_fields_; }

    ValueType mutable_field_type(uint32_t index) {
      // Currently both cached fields have WordPtr type.
      DCHECK(0 <= index && index <= 1);
      static constexpr ValueType kWordPtrType = Is64() ? kWasmI64 : kWasmI32;
      return kWordPtrType;
    }

    OpIndex mutable_field_value(uint32_t index) {
      if (index == memory_size_index_) return mem_size_;
      DCHECK_EQ(memory_start_index_, index);
      return mem_start_;
    }

    void set_mutable_field_value(uint32_t index, OpIndex value) {
      if (index == memory_size_index_) {
        mem_size_ = V<WordPtr>::Cast(value);
        return;
      }
      DCHECK_EQ(memory_start_index_, index);
      mem_start_ = V<WordPtr>::Cast(value);
    }

    V<WasmTrustedInstanceData> trusted_instance_data() { return trusted_data_; }
    V<FixedArray> managed_object_maps() { return managed_object_maps_; }
    V<NativeContext> native_context() { return native_context_; }
    V<WordPtr> memory0_start() {
      DCHECK(has_memory_);
      return mem_start_;
    }
    V<WordPtr> memory0_size() {
      DCHECK(has_memory_);
      if (!memory_size_cached_) return LoadMemSize();
      return mem_size_;
    }

   private:
    static constexpr uint8_t kUnused = ~uint8_t{0};

    V<WordPtr> LoadMemStart() {
      DCHECK(has_memory_);
      // In contrast to memory size loads, we can mark memory start loads as
      // eliminable: shared memories never move, and non-shared memories can't
      // have their start modified by other threads.
      LoadOp::Kind kind = LoadOp::Kind::TaggedBase();
      if (!memory_can_move()) kind = kind.Immutable();
      return __ Load(trusted_data_, kind, MemoryRepresentation::UintPtr(),
                     WasmTrustedInstanceData::kMemory0StartOffset);
    }

    V<WordPtr> LoadMemSize() {
      DCHECK(has_memory_);
      LoadOp::Kind kind = LoadOp::Kind::TaggedBase();
      if (memory_is_shared_ && memory_can_grow_) {
        // Memory size loads should not be load-eliminated as the memory size
        // can be modified by another thread.
        kind = kind.NotLoadEliminable();
      }
      if (!memory_can_grow_) kind = kind.Immutable();
      return __ Load(trusted_data_, kind, MemoryRepresentation::UintPtr(),
                     WasmTrustedInstanceData::kMemory0SizeOffset);
    }

    bool memory_can_move() { return memory_start_index_ != kUnused; }

    // For compatibility with `__` macro.
    Assembler& Asm() { return asm_; }

    // Cached immutable fields (need no Phi nodes):
    V<WasmTrustedInstanceData> trusted_data_;
    V<FixedArray> managed_object_maps_;
    V<NativeContext> native_context_;

    // Cached mutable fields (must be integrated with Phi handling):
    V<WordPtr> mem_start_;
    V<WordPtr> mem_size_;

    // Other fields for internal usage.
    Assembler& asm_;
    bool memory_is_shared_{false};
    bool memory_can_grow_{false};
    bool memory_size_cached_{false};
    uint8_t memory_size_index_{kUnused};
    uint8_t memory_start_index_{kUnused};
    uint8_t num_mutable_fields_{0};
#if DEBUG
    bool has_memory_{false};
#endif
  };

  enum class CheckForException { kNo, kCatchInThisFrame, kCatchInParentFrame };

 private:
  // Holds phi inputs for a specific block. These include SSA values, stack
  // merge values, and cached fields from the instance..
  // Conceptually, this is a two-dimensional, rectangular array of size
  // `phi_count * inputs_per_phi`, since each phi has the same number of inputs,
  // namely the number of incoming edges for this block.
  class BlockPhis {
   public:
    // Ctor for regular blocks.
    V8_INLINE BlockPhis(FullDecoder* decoder, Merge<Value>* merge,
                        InstanceCache& instance_cache)
        : incoming_exceptions_(decoder->zone()) {
      // Allocate space and initialize the types of all phis.
      uint32_t num_locals = decoder->num_locals();
      uint32_t merge_arity = merge != nullptr ? merge->arity : 0;
      uint32_t cached_fields = instance_cache.num_mutable_fields();

      phi_count_ = num_locals + merge_arity + cached_fields;
      phi_types_ = decoder->zone()->AllocateArray<ValueType>(phi_count_);

      base::Vector<ValueType> locals = decoder->local_types();
      std::uninitialized_copy(locals.begin(), locals.end(), phi_types_);
      for (uint32_t i = 0; i < merge_arity; i++) {
        new (&phi_types_[num_locals + i]) ValueType((*merge)[i].type);
      }
      for (uint32_t i = 0; i < cached_fields; i++) {
        new (&phi_types_[num_locals + merge_arity + i])
            ValueType(instance_cache.mutable_field_type(i));
      }
      AllocatePhiInputs(decoder->zone());
    }

    // Consider this "private"; it's next to the constructors (where it's
    // called) for context.
    void AllocatePhiInputs(Zone* zone) {
      // Only reserve some space for the inputs to be added later.
      phi_inputs_capacity_total_ = phi_count_ * input_capacity_per_phi_;
      phi_inputs_ = zone->AllocateArray<OpIndex>(phi_inputs_capacity_total_);

#ifdef DEBUG
      constexpr uint32_t kNoInputs = 0;
      input_count_per_phi_ = std::vector(phi_count_, kNoInputs);
#endif
    }

    // Ctor for places of "compiler-internal" control flow where we only
    // need to merge the InstanceCache, but no locals or stack. An example
    // is CallRef inlining.
    BlockPhis(Zone* zone, InstanceCache& instance_cache)
        : phi_count_(instance_cache.num_mutable_fields()),
          incoming_exceptions_(zone) {
      phi_types_ = zone->AllocateArray<ValueType>(phi_count_);
      for (uint32_t i = 0; i < phi_count_; i++) {
        new (&phi_types_[i]) ValueType(instance_cache.mutable_field_type(i));
      }
      AllocatePhiInputs(zone);
    }
    void AddPhiInputs(InstanceCache& instance_cache) {
      DCHECK_EQ(phi_count_, instance_cache.num_mutable_fields());
      for (uint32_t i = 0; i < phi_count_; i++) {
        DCHECK(instance_cache.mutable_field_value(i).valid());
        AddInputForPhi(i, instance_cache.mutable_field_value(i));
      }
    }

    // Default ctor and later initialization for function returns.
    explicit BlockPhis(Zone* zone) : incoming_exceptions_(zone) {}
    void InitReturnPhis(base::iterator_range<const ValueType*> return_types,
                        InstanceCache& instance_cache) {
      // For `return_phis_`, nobody should have inserted into `this` before
      // calling `InitReturnPhis`.
      DCHECK_EQ(phi_count_, 0);
      DCHECK_EQ(inputs_per_phi_, 0);

      uint32_t return_count = static_cast<uint32_t>(return_types.size());
      phi_count_ = return_count + instance_cache.num_mutable_fields();
      phi_types_ = zone()->AllocateArray<ValueType>(phi_count_);

      std::uninitialized_copy(return_types.begin(), return_types.end(),
                              phi_types_);
      for (uint32_t i = 0; i < instance_cache.num_mutable_fields(); i++) {
        phi_types_[return_count + i] = instance_cache.mutable_field_type(i);
      }
      AllocatePhiInputs(zone());
    }

    void AddInputForPhi(size_t phi_i, OpIndex input) {
      if (V8_UNLIKELY(phi_inputs_total_ >= phi_inputs_capacity_total_)) {
        GrowInputsVector();
      }

#ifdef DEBUG
      // We rely on adding inputs in the order of phis, i.e.,
      // `AddInputForPhi(0, ...); AddInputForPhi(1, ...); ...`.
      size_t phi_inputs_start = phi_i * input_capacity_per_phi_;
      size_t phi_input_offset_from_start = inputs_per_phi_;
      CHECK_EQ(input_count_per_phi_[phi_i]++, phi_input_offset_from_start);
      size_t phi_input_offset = phi_inputs_start + phi_input_offset_from_start;
      CHECK_EQ(next_phi_input_add_offset_, phi_input_offset);
#endif
      new (&phi_inputs_[next_phi_input_add_offset_]) OpIndex(input);

      phi_inputs_total_++;
      next_phi_input_add_offset_ += input_capacity_per_phi_;
      if (next_phi_input_add_offset_ >= phi_inputs_capacity_total_) {
        // We have finished adding the last input for all phis.
        inputs_per_phi_++;
        next_phi_input_add_offset_ = inputs_per_phi_;
#ifdef DEBUG
        EnsureAllPhisHaveSameInputCount();
#endif
      }
    }

    uint32_t phi_count() const { return phi_count_; }

    ValueType phi_type(size_t phi_i) const { return phi_types_[phi_i]; }
    base::Vector<const OpIndex> phi_inputs(size_t phi_i) const {
#ifdef DEBUG
      EnsureAllPhisHaveSameInputCount();
#endif
      size_t phi_inputs_start = phi_i * input_capacity_per_phi_;
      return base::VectorOf(&phi_inputs_[phi_inputs_start], inputs_per_phi_);
    }

    void AddIncomingException(OpIndex exception) {
      incoming_exceptions_.push_back(exception);
    }

    base::Vector<const OpIndex> incoming_exceptions() const {
      return base::VectorOf(incoming_exceptions_);
    }

   private:
    // Invariants:
    // The number of phis for a given block (e.g., locals, merged stack values,
    // and cached instance fields) is known when constructing the `BlockPhis`
    // and doesn't grow afterwards.
    // The number of _inputs_ for each phi is however _not_ yet known when
    // constructing this, but grows over time as new incoming edges for a given
    // block are created.
    // After such an edge is created, each phi has the same number of inputs.
    // When eventually creating a phi, we also need all inputs layed out
    // contiguously.
    // Due to those requirements, we write our own little container, see below.

    // First the backing storage:
    // Of size `phi_count_`, one type per phi.
    ValueType* phi_types_ = nullptr;
    // Of size `phi_inputs_capacity_total_ == phi_count_ *
    // input_capacity_per_phi_`, of which `phi_inputs_total_ == phi_count_ *
    // inputs_per_phi_` are set/initialized. All inputs for a given phi are
    // stored contiguously, but between them are uninitialized elements for
    // adding new inputs without reallocating.
    OpIndex* phi_inputs_ = nullptr;

    // Stored explicitly to save multiplications in the hot `AddInputForPhi()`.
    // Also pulled up to be in the same cache-line as `phi_inputs_`.
    uint32_t phi_inputs_capacity_total_ = 0;  // Updated with `phi_inputs_`.
    uint32_t phi_inputs_total_ = 0;
    uint32_t next_phi_input_add_offset_ = 0;

    // The dimensions.
    uint32_t phi_count_ = 0;
    uint32_t inputs_per_phi_ = 0;
    static constexpr uint32_t kInitialInputCapacityPerPhi = 2;
    uint32_t input_capacity_per_phi_ = kInitialInputCapacityPerPhi;

#ifdef DEBUG
    std::vector<uint32_t> input_count_per_phi_;
    void EnsureAllPhisHaveSameInputCount() const {
      CHECK_EQ(phi_inputs_total_, phi_count() * inputs_per_phi_);
      CHECK_EQ(phi_count(), input_count_per_phi_.size());
      CHECK(std::all_of(input_count_per_phi_.begin(),
                        input_count_per_phi_.end(), [=](uint32_t input_count) {
                          return input_count == inputs_per_phi_;
                        }));
    }
#endif

    // The number of `incoming_exceptions` is also not known when constructing
    // the block, but at least it is only one-dimensional, so we can use a
    // simple `ZoneVector`.
    ZoneVector<OpIndex> incoming_exceptions_;

    Zone* zone() { return incoming_exceptions_.zone(); }

    V8_NOINLINE V8_PRESERVE_MOST void GrowInputsVector() {
      // We should have always initialized some storage, see
      // `kInititalInputCapacityPerPhi`.
      DCHECK_NOT_NULL(phi_inputs_);
      DCHECK_NE(phi_inputs_capacity_total_, 0);

      OpIndex* old_phi_inputs = phi_inputs_;
      uint32_t old_input_capacity_per_phi = input_capacity_per_phi_;
      uint32_t old_phi_inputs_capacity_total = phi_inputs_capacity_total_;

      input_capacity_per_phi_ *= 2;
      phi_inputs_capacity_total_ *= 2;
      phi_inputs_ = zone()->AllocateArray<OpIndex>(phi_inputs_capacity_total_);

      // This is essentially a strided copy, where we expand the storage by
      // "inserting" unitialized elements in between contiguous stretches of
      // inputs belonging to the same phi.
      for (size_t phi_i = 0; phi_i < phi_count(); ++phi_i) {
#ifdef DEBUG
        EnsureAllPhisHaveSameInputCount();
#endif
        const OpIndex* old_begin =
            &old_phi_inputs[phi_i * old_input_capacity_per_phi];
        const OpIndex* old_end = old_begin + inputs_per_phi_;
        OpIndex* begin = &phi_inputs_[phi_i * input_capacity_per_phi_];
        std::uninitialized_copy(old_begin, old_end, begin);
      }

      zone()->DeleteArray(old_phi_inputs, old_phi_inputs_capacity_total);
    }
  };

  void Bailout(FullDecoder* decoder) {
    decoder->errorf("Unsupported Turboshaft operation: %s",
                    decoder->SafeOpcodeNameAt(decoder->pc()));
    did_bailout_ = true;
  }

  // Perform a null check if the input type is nullable.
  V<Object> NullCheck(const Value& value,
                      TrapId trap_id = TrapId::kTrapNullDereference) {
    OpIndex not_null_value = value.op;
    if (value.type.is_nullable()) {
      not_null_value = __ AssertNotNull(value.op, value.type, trap_id);
    }
    return not_null_value;
  }

  // Creates a new block, initializes a {BlockPhis} for it, and registers it
  // with block_phis_. We pass a {merge} only if we later need to recover values
  // for that merge.
  TSBlock* NewBlockWithPhis(FullDecoder* decoder, Merge<Value>* merge) {
    TSBlock* block = __ NewBlock();
    block_phis_.emplace(block, BlockPhis(decoder, merge, instance_cache_));
    return block;
  }

  // Sets up a control flow edge from the current SSA environment and a stack to
  // {block}. The stack is {stack_values} if present, otherwise the current
  // decoder stack.
  void SetupControlFlowEdge(FullDecoder* decoder, TSBlock* block,
                            uint32_t drop_values = 0,
                            V<Object> exception = OpIndex::Invalid(),
                            Merge<Value>* stack_values = nullptr) {
    if (__ current_block() == nullptr) return;
    // It is guaranteed that this element exists.
    BlockPhis& phis_for_block = block_phis_.find(block)->second;
    uint32_t cached_fields = instance_cache_.num_mutable_fields();
    uint32_t merge_arity = static_cast<uint32_t>(phis_for_block.phi_count()) -
                           decoder->num_locals() - cached_fields;

    for (size_t i = 0; i < ssa_env_.size(); i++) {
      phis_for_block.AddInputForPhi(i, ssa_env_[i]);
    }
    // We never drop values from an explicit merge.
    DCHECK_IMPLIES(stack_values != nullptr, drop_values == 0);
    Value* stack_base = merge_arity == 0 ? nullptr
                        : stack_values != nullptr
                            ? &(*stack_values)[0]
                            : decoder->stack_value(merge_arity + drop_values);
    for (size_t i = 0; i < merge_arity; i++) {
      DCHECK(stack_base[i].op.valid());
      phis_for_block.AddInputForPhi(decoder->num_locals() + i,
                                    stack_base[i].op);
    }
    for (uint32_t i = 0; i < cached_fields; i++) {
      phis_for_block.AddInputForPhi(decoder->num_locals() + merge_arity + i,
                                    instance_cache_.mutable_field_value(i));
    }
    if (exception.valid()) {
      phis_for_block.AddIncomingException(exception);
    }
  }

  OpIndex MaybePhi(base::Vector<const OpIndex> elements, ValueType type) {
    if (elements.empty()) return OpIndex::Invalid();
    for (size_t i = 1; i < elements.size(); i++) {
      if (elements[i] != elements[0]) {
        return __ Phi(elements, RepresentationFor(type));
      }
    }
    return elements[0];
  }

  // Binds a block, initializes phis for its SSA environment from its entry in
  // {block_phis_}, and sets values to its {merge} (if available) from the
  // its entry in {block_phis_}.
  void BindBlockAndGeneratePhis(FullDecoder* decoder, TSBlock* tsblock,
                                Merge<Value>* merge,
                                OpIndex* exception = nullptr) {
    __ Bind(tsblock);
    auto block_phis_it = block_phis_.find(tsblock);
    DCHECK_NE(block_phis_it, block_phis_.end());
    BlockPhis& block_phis = block_phis_it->second;

    uint32_t merge_arity = merge != nullptr ? merge->arity : 0;
    uint32_t cached_fields = instance_cache_.num_mutable_fields();
    DCHECK_EQ(decoder->num_locals() + merge_arity + cached_fields,
              block_phis.phi_count());

    for (uint32_t i = 0; i < decoder->num_locals(); i++) {
      ssa_env_[i] = MaybePhi(block_phis.phi_inputs(i), block_phis.phi_type(i));
    }
    for (uint32_t i = 0; i < merge_arity; i++) {
      uint32_t phi_index = decoder->num_locals() + i;
      (*merge)[i].op = MaybePhi(block_phis.phi_inputs(phi_index),
                                block_phis.phi_type(phi_index));
    }
    for (uint32_t i = 0; i < cached_fields; i++) {
      uint32_t phi_index = decoder->num_locals() + merge_arity + i;
      instance_cache_.set_mutable_field_value(
          i, MaybePhi(block_phis.phi_inputs(phi_index),
                      block_phis.phi_type(phi_index)));
    }
    DCHECK_IMPLIES(exception == nullptr,
                   block_phis.incoming_exceptions().empty());
    if (exception != nullptr && !exception->valid()) {
      *exception = MaybePhi(block_phis.incoming_exceptions(), kWasmExternRef);
    }
    block_phis_.erase(block_phis_it);
  }

  OpIndex DefaultValue(ValueType type) {
    switch (type.kind()) {
      case kI8:
      case kI16:
      case kI32:
        return __ Word32Constant(int32_t{0});
      case kI64:
        return __ Word64Constant(int64_t{0});
      case kF32:
        return __ Float32Constant(0.0f);
      case kF64:
        return __ Float64Constant(0.0);
      case kRefNull:
        return __ Null(type);
      case kS128: {
        uint8_t value[kSimd128Size] = {};
        return __ Simd128Constant(value);
      }
      case kVoid:
      case kRtt:
      case kRef:
      case kBottom:
        UNREACHABLE();
    }
  }

 private:
  V<Word64> ExtractTruncationProjections(OpIndex truncated) {
    V<Word64> result =
        __ Projection(truncated, 0, RegisterRepresentation::Word64());
    V<Word32> check =
        __ Projection(truncated, 1, RegisterRepresentation::Word32());
    __ TrapIf(__ Word32Equal(check, 0), OpIndex::Invalid(),
              TrapId::kTrapFloatUnrepresentable);
    return result;
  }

  std::pair<OpIndex, V<Word32>> BuildCCallForFloatConversion(
      OpIndex arg, MemoryRepresentation float_type,
      ExternalReference ccall_ref) {
    uint8_t slot_size = MemoryRepresentation::Int64().SizeInBytes();
    V<WordPtr> stack_slot = __ StackSlot(slot_size, slot_size);
    __ Store(stack_slot, arg, StoreOp::Kind::RawAligned(), float_type,
             compiler::WriteBarrierKind::kNoWriteBarrier);
    MachineType reps[]{MachineType::Int32(), MachineType::Pointer()};
    MachineSignature sig(1, 1, reps);
    V<Word32> overflow = CallC(&sig, ccall_ref, stack_slot);
    return {stack_slot, overflow};
  }

  OpIndex BuildCcallConvertFloat(OpIndex arg, MemoryRepresentation float_type,
                                 ExternalReference ccall_ref) {
    auto [stack_slot, overflow] =
        BuildCCallForFloatConversion(arg, float_type, ccall_ref);
    __ TrapIf(__ Word32Equal(overflow, 0), OpIndex::Invalid(),
              compiler::TrapId::kTrapFloatUnrepresentable);
    MemoryRepresentation int64 = MemoryRepresentation::Int64();
    return __ Load(stack_slot, LoadOp::Kind::RawAligned(), int64);
  }

  OpIndex BuildCcallConvertFloatSat(OpIndex arg,
                                    MemoryRepresentation float_type,
                                    ExternalReference ccall_ref,
                                    bool is_signed) {
    MemoryRepresentation int64 = MemoryRepresentation::Int64();
    uint8_t slot_size = int64.SizeInBytes();
    V<WordPtr> stack_slot = __ StackSlot(slot_size, slot_size);
    __ Store(stack_slot, arg, StoreOp::Kind::RawAligned(), float_type,
             compiler::WriteBarrierKind::kNoWriteBarrier);
    MachineType reps[]{MachineType::Pointer()};
    MachineSignature sig(0, 1, reps);
    CallC(&sig, ccall_ref, stack_slot);
    return __ Load(stack_slot, LoadOp::Kind::RawAligned(), int64);
  }

  OpIndex BuildIntToFloatConversionInstruction(
      OpIndex input, ExternalReference ccall_ref,
      MemoryRepresentation input_representation,
      MemoryRepresentation result_representation) {
    uint8_t slot_size = std::max(input_representation.SizeInBytes(),
                                 result_representation.SizeInBytes());
    V<WordPtr> stack_slot = __ StackSlot(slot_size, slot_size);
    __ Store(stack_slot, input, StoreOp::Kind::RawAligned(),
             input_representation, compiler::WriteBarrierKind::kNoWriteBarrier);
    MachineType reps[]{MachineType::Pointer()};
    MachineSignature sig(0, 1, reps);
    CallC(&sig, ccall_ref, stack_slot);
    return __ Load(stack_slot, LoadOp::Kind::RawAligned(),
                   result_representation);
  }

  OpIndex BuildDiv64Call(OpIndex lhs, OpIndex rhs, ExternalReference ccall_ref,
                         wasm::TrapId trap_zero) {
    MemoryRepresentation int64_rep = MemoryRepresentation::Int64();
    V<WordPtr> stack_slot =
        __ StackSlot(2 * int64_rep.SizeInBytes(), int64_rep.SizeInBytes());
    __ Store(stack_slot, lhs, StoreOp::Kind::RawAligned(), int64_rep,
             compiler::WriteBarrierKind::kNoWriteBarrier);
    __ Store(stack_slot, rhs, StoreOp::Kind::RawAligned(), int64_rep,
             compiler::WriteBarrierKind::kNoWriteBarrier,
             int64_rep.SizeInBytes());

    MachineType sig_types[] = {MachineType::Int32(), MachineType::Pointer()};
    MachineSignature sig(1, 1, sig_types);
    OpIndex rc = CallC(&sig, ccall_ref, stack_slot);
    __ TrapIf(__ Word32Equal(rc, 0), OpIndex::Invalid(), trap_zero);
    __ TrapIf(__ Word32Equal(rc, -1), OpIndex::Invalid(),
              TrapId::kTrapDivUnrepresentable);
    return __ Load(stack_slot, LoadOp::Kind::RawAligned(), int64_rep);
  }

  OpIndex UnOpImpl(WasmOpcode opcode, OpIndex arg,
                   ValueType input_type /* for ref.is_null only*/) {
    switch (opcode) {
      case kExprI32Eqz:
        return __ Word32Equal(arg, 0);
      case kExprF32Abs:
        return __ Float32Abs(arg);
      case kExprF32Neg:
        return __ Float32Negate(arg);
      case kExprF32Sqrt:
        return __ Float32Sqrt(arg);
      case kExprF64Abs:
        return __ Float64Abs(arg);
      case kExprF64Neg:
        return __ Float64Negate(arg);
      case kExprF64Sqrt:
        return __ Float64Sqrt(arg);
      case kExprI32SConvertF32: {
        V<Float32> truncated = UnOpImpl(kExprF32Trunc, arg, kWasmF32);
        V<Word32> result = __ TruncateFloat32ToInt32OverflowToMin(truncated);
        V<Float32> converted_back = __ ChangeInt32ToFloat32(result);
        __ TrapIf(__ Word32Equal(__ Float32Equal(converted_back, truncated), 0),
                  OpIndex::Invalid(), TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI32UConvertF32: {
        V<Float32> truncated = UnOpImpl(kExprF32Trunc, arg, kWasmF32);
        V<Word32> result = __ TruncateFloat32ToUint32OverflowToMin(truncated);
        V<Float32> converted_back = __ ChangeUint32ToFloat32(result);
        __ TrapIf(__ Word32Equal(__ Float32Equal(converted_back, truncated), 0),
                  OpIndex::Invalid(), TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI32SConvertF64: {
        V<Float64> truncated = UnOpImpl(kExprF64Trunc, arg, kWasmF64);
        V<Word32> result =
            __ TruncateFloat64ToInt32OverflowUndefined(truncated);
        V<Float64> converted_back = __ ChangeInt32ToFloat64(result);
        __ TrapIf(__ Word32Equal(__ Float64Equal(converted_back, truncated), 0),
                  OpIndex::Invalid(), TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI32UConvertF64: {
        V<Float64> truncated = UnOpImpl(kExprF64Trunc, arg, kWasmF64);
        V<Word32> result = __ TruncateFloat64ToUint32OverflowToMin(truncated);
        V<Float64> converted_back = __ ChangeUint32ToFloat64(result);
        __ TrapIf(__ Word32Equal(__ Float64Equal(converted_back, truncated), 0),
                  OpIndex::Invalid(), TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI64SConvertF32:
        return Is64() ? ExtractTruncationProjections(
                            __ TryTruncateFloat32ToInt64(arg))
                      : BuildCcallConvertFloat(
                            arg, MemoryRepresentation::Float32(),
                            ExternalReference::wasm_float32_to_int64());
      case kExprI64UConvertF32:
        return Is64() ? ExtractTruncationProjections(
                            __ TryTruncateFloat32ToUint64(arg))
                      : BuildCcallConvertFloat(
                            arg, MemoryRepresentation::Float32(),
                            ExternalReference::wasm_float32_to_uint64());
      case kExprI64SConvertF64:
        return Is64() ? ExtractTruncationProjections(
                            __ TryTruncateFloat64ToInt64(arg))
                      : BuildCcallConvertFloat(
                            arg, MemoryRepresentation::Float64(),
                            ExternalReference::wasm_float64_to_int64());
      case kExprI64UConvertF64:
        return Is64() ? ExtractTruncationProjections(
                            __ TryTruncateFloat64ToUint64(arg))
                      : BuildCcallConvertFloat(
                            arg, MemoryRepresentation::Float64(),
                            ExternalReference::wasm_float64_to_uint64());
      case kExprF64SConvertI32:
        return __ ChangeInt32ToFloat64(arg);
      case kExprF64UConvertI32:
        return __ ChangeUint32ToFloat64(arg);
      case kExprF32SConvertI32:
        return __ ChangeInt32ToFloat32(arg);
      case kExprF32UConvertI32:
        return __ ChangeUint32ToFloat32(arg);
      case kExprI32SConvertSatF32: {
        V<Float32> truncated = UnOpImpl(kExprF32Trunc, arg, kWasmF32);
        V<Word32> converted =
            __ TruncateFloat32ToInt32OverflowUndefined(truncated);
        V<Float32> converted_back = __ ChangeInt32ToFloat32(converted);

        Label<Word32> done(&asm_);

        IF (LIKELY(__ Float32Equal(truncated, converted_back))) {
          GOTO(done, converted);
        } ELSE {
          // Overflow.
          IF (__ Float32Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   __ Word32Constant(std::numeric_limits<int32_t>::min()));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word32Constant(std::numeric_limits<int32_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word32Constant(0));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprI32UConvertSatF32: {
        V<Float32> truncated = UnOpImpl(kExprF32Trunc, arg, kWasmF32);
        V<Word32> converted =
            __ TruncateFloat32ToUint32OverflowUndefined(truncated);
        V<Float32> converted_back = __ ChangeUint32ToFloat32(converted);

        Label<Word32> done(&asm_);

        IF (LIKELY(__ Float32Equal(truncated, converted_back))) {
          GOTO(done, converted);
        } ELSE {
          // Overflow.
          IF (__ Float32Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, __ Word32Constant(0));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word32Constant(std::numeric_limits<uint32_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word32Constant(0));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprI32SConvertSatF64: {
        V<Float64> truncated = UnOpImpl(kExprF64Trunc, arg, kWasmF64);
        V<Word32> converted =
            __ TruncateFloat64ToInt32OverflowUndefined(truncated);
        V<Float64> converted_back = __ ChangeInt32ToFloat64(converted);

        Label<Word32> done(&asm_);

        IF (LIKELY(__ Float64Equal(truncated, converted_back))) {
          GOTO(done, converted);
        } ELSE {
          // Overflow.
          IF (__ Float64Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   __ Word32Constant(std::numeric_limits<int32_t>::min()));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word32Constant(std::numeric_limits<int32_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word32Constant(0));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprI32UConvertSatF64: {
        V<Float64> truncated = UnOpImpl(kExprF64Trunc, arg, kWasmF64);
        V<Word32> converted =
            __ TruncateFloat64ToUint32OverflowUndefined(truncated);
        V<Float64> converted_back = __ ChangeUint32ToFloat64(converted);

        Label<Word32> done(&asm_);

        IF (LIKELY(__ Float64Equal(truncated, converted_back))) {
          GOTO(done, converted);
        } ELSE {
          // Overflow.
          IF (__ Float64Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, __ Word32Constant(0));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word32Constant(std::numeric_limits<uint32_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word32Constant(0));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprI64SConvertSatF32: {
        if constexpr (!Is64()) {
          bool is_signed = true;
          return BuildCcallConvertFloatSat(
              arg, MemoryRepresentation::Float32(),
              ExternalReference::wasm_float32_to_int64_sat(), is_signed);
        }
        V<Word64> converted = __ TryTruncateFloat32ToInt64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return __ Projection(converted, 0, RegisterRepresentation::Word64());
        }
        IF (LIKELY(__ Projection(converted, 1,
                                 RegisterRepresentation::Word32()))) {
          GOTO(done,
               __ Projection(converted, 0, RegisterRepresentation::Word64()));
        } ELSE {
          // Overflow.
          IF (__ Float32Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   __ Word64Constant(std::numeric_limits<int64_t>::min()));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word64Constant(std::numeric_limits<int64_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word64Constant(int64_t{0}));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprI64UConvertSatF32: {
        if constexpr (!Is64()) {
          bool is_signed = false;
          return BuildCcallConvertFloatSat(
              arg, MemoryRepresentation::Float32(),
              ExternalReference::wasm_float32_to_uint64_sat(), is_signed);
        }
        V<Word64> converted = __ TryTruncateFloat32ToUint64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return __ Projection(converted, 0, RegisterRepresentation::Word64());
        }

        IF (LIKELY(__ Projection(converted, 1,
                                 RegisterRepresentation::Word32()))) {
          GOTO(done,
               __ Projection(converted, 0, RegisterRepresentation::Word64()));
        } ELSE {
          // Overflow.
          IF (__ Float32Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, __ Word64Constant(int64_t{0}));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word64Constant(std::numeric_limits<uint64_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word64Constant(int64_t{0}));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprI64SConvertSatF64: {
        if constexpr (!Is64()) {
          bool is_signed = true;
          return BuildCcallConvertFloatSat(
              arg, MemoryRepresentation::Float64(),
              ExternalReference::wasm_float64_to_int64_sat(), is_signed);
        }
        V<Word64> converted = __ TryTruncateFloat64ToInt64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return __ Projection(converted, 0, RegisterRepresentation::Word64());
        }

        IF (LIKELY(__ Projection(converted, 1,
                                 RegisterRepresentation::Word32()))) {
          GOTO(done,
               __ Projection(converted, 0, RegisterRepresentation::Word64()));
        } ELSE {
          // Overflow.
          IF (__ Float64Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   __ Word64Constant(std::numeric_limits<int64_t>::min()));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word64Constant(std::numeric_limits<int64_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word64Constant(int64_t{0}));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprI64UConvertSatF64: {
        if constexpr (!Is64()) {
          bool is_signed = false;
          return BuildCcallConvertFloatSat(
              arg, MemoryRepresentation::Float64(),
              ExternalReference::wasm_float64_to_uint64_sat(), is_signed);
        }
        V<Word64> converted = __ TryTruncateFloat64ToUint64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return __ Projection(converted, 0, RegisterRepresentation::Word64());
        }

        IF (LIKELY(__ Projection(converted, 1,
                                 RegisterRepresentation::Word32()))) {
          GOTO(done,
               __ Projection(converted, 0, RegisterRepresentation::Word64()));
        } ELSE {
          // Overflow.
          IF (__ Float64Equal(arg, arg)) {
            // Not NaN.
            IF (__ Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, __ Word64Constant(int64_t{0}));
            } ELSE {
              // Positive arg.
              GOTO(done,
                   __ Word64Constant(std::numeric_limits<uint64_t>::max()));
            }
          } ELSE {
            // NaN.
            GOTO(done, __ Word64Constant(int64_t{0}));
          }
        }
        BIND(done, result);

        return result;
      }
      case kExprF32ConvertF64:
        return __ TruncateFloat64ToFloat32(arg);
      case kExprF64ConvertF32:
        return __ ChangeFloat32ToFloat64(arg);
      case kExprF32ReinterpretI32:
        return __ BitcastWord32ToFloat32(arg);
      case kExprI32ReinterpretF32:
        return __ BitcastFloat32ToWord32(arg);
      case kExprI32Clz:
        return __ Word32CountLeadingZeros(arg);
      case kExprI32Ctz:
        if (SupportedOperations::word32_ctz()) {
          return __ Word32CountTrailingZeros(arg);
        } else {
          // TODO(14108): Use reverse_bits if supported.
          auto sig =
              FixedSizeSignature<MachineType>::Returns(MachineType::Uint32())
                  .Params(MachineType::Uint32());
          return CallC(&sig, ExternalReference::wasm_word32_ctz(), arg);
        }
      case kExprI32Popcnt:
        if (SupportedOperations::word32_popcnt()) {
          return __ Word32PopCount(arg);
        } else {
          auto sig =
              FixedSizeSignature<MachineType>::Returns(MachineType::Uint32())
                  .Params(MachineType::Uint32());
          return CallC(&sig, ExternalReference::wasm_word32_popcnt(), arg);
        }
      case kExprF32Floor:
        if (SupportedOperations::float32_round_down()) {
          return __ Float32RoundDown(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f32_floor(),
                                           MemoryRepresentation::Float32());
        }
      case kExprF32Ceil:
        if (SupportedOperations::float32_round_up()) {
          return __ Float32RoundUp(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f32_ceil(),
                                           MemoryRepresentation::Float32());
        }
      case kExprF32Trunc:
        if (SupportedOperations::float32_round_to_zero()) {
          return __ Float32RoundToZero(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f32_trunc(),
                                           MemoryRepresentation::Float32());
        }
      case kExprF32NearestInt:
        if (SupportedOperations::float32_round_ties_even()) {
          return __ Float32RoundTiesEven(arg);
        } else {
          return CallCStackSlotToStackSlot(
              arg, ExternalReference::wasm_f32_nearest_int(),
              MemoryRepresentation::Float32());
        }
      case kExprF64Floor:
        if (SupportedOperations::float64_round_down()) {
          return __ Float64RoundDown(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f64_floor(),
                                           MemoryRepresentation::Float64());
        }
      case kExprF64Ceil:
        if (SupportedOperations::float64_round_up()) {
          return __ Float64RoundUp(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f64_ceil(),
                                           MemoryRepresentation::Float64());
        }
      case kExprF64Trunc:
        if (SupportedOperations::float64_round_to_zero()) {
          return __ Float64RoundToZero(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f64_trunc(),
                                           MemoryRepresentation::Float64());
        }
      case kExprF64NearestInt:
        if (SupportedOperations::float64_round_ties_even()) {
          return __ Float64RoundTiesEven(arg);
        } else {
          return CallCStackSlotToStackSlot(
              arg, ExternalReference::wasm_f64_nearest_int(),
              MemoryRepresentation::Float64());
        }
      case kExprF64Acos:
        return CallCStackSlotToStackSlot(
            arg, ExternalReference::f64_acos_wrapper_function(),
            MemoryRepresentation::Float64());
      case kExprF64Asin:
        return CallCStackSlotToStackSlot(
            arg, ExternalReference::f64_asin_wrapper_function(),
            MemoryRepresentation::Float64());
      case kExprF64Atan:
        return __ Float64Atan(arg);
      case kExprF64Cos:
        return __ Float64Cos(arg);
      case kExprF64Sin:
        return __ Float64Sin(arg);
      case kExprF64Tan:
        return __ Float64Tan(arg);
      case kExprF64Exp:
        return __ Float64Exp(arg);
      case kExprF64Log:
        return __ Float64Log(arg);
      case kExprI32ConvertI64:
        return __ TruncateWord64ToWord32(arg);
      case kExprI64SConvertI32:
        return __ ChangeInt32ToInt64(arg);
      case kExprI64UConvertI32:
        return __ ChangeUint32ToUint64(arg);
      case kExprF64ReinterpretI64:
        return __ BitcastWord64ToFloat64(arg);
      case kExprI64ReinterpretF64:
        return __ BitcastFloat64ToWord64(arg);
      case kExprI64Clz:
        return __ Word64CountLeadingZeros(arg);
      case kExprI64Ctz:
        if (SupportedOperations::word64_ctz() ||
            (!Is64() && SupportedOperations::word32_ctz())) {
          return __ Word64CountTrailingZeros(arg);
        } else if (Is64()) {
          // TODO(14108): Use reverse_bits if supported.
          auto sig =
              FixedSizeSignature<MachineType>::Returns(MachineType::Uint32())
                  .Params(MachineType::Uint64());
          return __ ChangeUint32ToUint64(
              CallC(&sig, ExternalReference::wasm_word64_ctz(), arg));
        } else {
          // lower_word == 0 ? 32 + CTZ32(upper_word) : CTZ32(lower_word);
          OpIndex upper_word =
              __ TruncateWord64ToWord32(__ Word64ShiftRightLogical(arg, 32));
          OpIndex lower_word = __ TruncateWord64ToWord32(arg);
          auto sig =
              FixedSizeSignature<MachineType>::Returns(MachineType::Uint32())
                  .Params(MachineType::Uint32());
          Label<Word32> done(&asm_);
          IF (__ Word32Equal(lower_word, 0)) {
            GOTO(done,
                 __ Word32Add(CallC(&sig, ExternalReference::wasm_word32_ctz(),
                                    upper_word),
                              32));
          } ELSE {
            GOTO(done,
                 CallC(&sig, ExternalReference::wasm_word32_ctz(), lower_word));
          }
          BIND(done, result);
          return __ ChangeUint32ToUint64(result);
        }
      case kExprI64Popcnt:
        if (SupportedOperations::word64_popcnt() ||
            (!Is64() && SupportedOperations::word32_popcnt())) {
          return __ Word64PopCount(arg);
        } else if (Is64()) {
          // Call wasm_word64_popcnt.
          auto sig =
              FixedSizeSignature<MachineType>::Returns(MachineType::Uint32())
                  .Params(MachineType::Uint64());
          return __ ChangeUint32ToUint64(
              CallC(&sig, ExternalReference::wasm_word64_popcnt(), arg));
        } else {
          // Emit two calls to wasm_word32_popcnt.
          OpIndex upper_word =
              __ TruncateWord64ToWord32(__ Word64ShiftRightLogical(arg, 32));
          OpIndex lower_word = __ TruncateWord64ToWord32(arg);
          auto sig =
              FixedSizeSignature<MachineType>::Returns(MachineType::Uint32())
                  .Params(MachineType::Uint32());
          return __ ChangeUint32ToUint64(__ Word32Add(
              CallC(&sig, ExternalReference::wasm_word32_popcnt(), lower_word),
              CallC(&sig, ExternalReference::wasm_word32_popcnt(),
                    upper_word)));
        }
      case kExprI64Eqz:
        return __ Word64Equal(arg, 0);
      case kExprF32SConvertI64:
        if constexpr (!Is64()) {
          return BuildIntToFloatConversionInstruction(
              arg, ExternalReference::wasm_int64_to_float32(),
              MemoryRepresentation::Int64(), MemoryRepresentation::Float32());
        }
        return __ ChangeInt64ToFloat32(arg);
      case kExprF32UConvertI64:
        if constexpr (!Is64()) {
          return BuildIntToFloatConversionInstruction(
              arg, ExternalReference::wasm_uint64_to_float32(),
              MemoryRepresentation::Uint64(), MemoryRepresentation::Float32());
        }
        return __ ChangeUint64ToFloat32(arg);
      case kExprF64SConvertI64:
        if constexpr (!Is64()) {
          return BuildIntToFloatConversionInstruction(
              arg, ExternalReference::wasm_int64_to_float64(),
              MemoryRepresentation::Int64(), MemoryRepresentation::Float64());
        }
        return __ ChangeInt64ToFloat64(arg);
      case kExprF64UConvertI64:
        if constexpr (!Is64()) {
          return BuildIntToFloatConversionInstruction(
              arg, ExternalReference::wasm_uint64_to_float64(),
              MemoryRepresentation::Uint64(), MemoryRepresentation::Float64());
        }
        return __ ChangeUint64ToFloat64(arg);
      case kExprI32SExtendI8:
        return __ Word32SignExtend8(arg);
      case kExprI32SExtendI16:
        return __ Word32SignExtend16(arg);
      case kExprI64SExtendI8:
        return __ Word64SignExtend8(arg);
      case kExprI64SExtendI16:
        return __ Word64SignExtend16(arg);
      case kExprI64SExtendI32:
        return __ ChangeInt32ToInt64(__ TruncateWord64ToWord32(arg));
      case kExprRefIsNull:
        return __ IsNull(arg, input_type);
      case kExprI32AsmjsLoadMem8S:
        return AsmjsLoadMem(arg, MemoryRepresentation::Int8());
      case kExprI32AsmjsLoadMem8U:
        return AsmjsLoadMem(arg, MemoryRepresentation::Uint8());
      case kExprI32AsmjsLoadMem16S:
        return AsmjsLoadMem(arg, MemoryRepresentation::Int16());
      case kExprI32AsmjsLoadMem16U:
        return AsmjsLoadMem(arg, MemoryRepresentation::Uint16());
      case kExprI32AsmjsLoadMem:
        return AsmjsLoadMem(arg, MemoryRepresentation::Int32());
      case kExprF32AsmjsLoadMem:
        return AsmjsLoadMem(arg, MemoryRepresentation::Float32());
      case kExprF64AsmjsLoadMem:
        return AsmjsLoadMem(arg, MemoryRepresentation::Float64());
      case kExprI32AsmjsSConvertF32:
      case kExprI32AsmjsUConvertF32:
        return __ JSTruncateFloat64ToWord32(__ ChangeFloat32ToFloat64(arg));
      case kExprI32AsmjsSConvertF64:
      case kExprI32AsmjsUConvertF64:
        return __ JSTruncateFloat64ToWord32(arg);
      case kExprRefAsNonNull:
        // We abuse ref.as_non_null, which isn't otherwise used in this switch,
        // as a sentinel for the negation of ref.is_null.
        return __ Word32Equal(__ IsNull(arg, input_type), 0);
      case kExprAnyConvertExtern:
        return __ AnyConvertExtern(arg);
      case kExprExternConvertAny:
        return __ ExternConvertAny(arg);
      default:
        UNREACHABLE();
    }
  }

  OpIndex BinOpImpl(WasmOpcode opcode, OpIndex lhs, OpIndex rhs) {
    switch (opcode) {
      case kExprI32Add:
        return __ Word32Add(lhs, rhs);
      case kExprI32Sub:
        return __ Word32Sub(lhs, rhs);
      case kExprI32Mul:
        return __ Word32Mul(lhs, rhs);
      case kExprI32DivS: {
        __ TrapIf(__ Word32Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapDivByZero);
        V<Word32> unrepresentable_condition = __ Word32BitwiseAnd(
            __ Word32Equal(rhs, -1), __ Word32Equal(lhs, kMinInt));
        __ TrapIf(unrepresentable_condition, OpIndex::Invalid(),
                  TrapId::kTrapDivUnrepresentable);
        return __ Int32Div(lhs, rhs);
      }
      case kExprI32DivU:
        __ TrapIf(__ Word32Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapDivByZero);
        return __ Uint32Div(lhs, rhs);
      case kExprI32RemS: {
        __ TrapIf(__ Word32Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapRemByZero);
        Label<Word32> done(&asm_);
        IF (UNLIKELY(__ Word32Equal(rhs, -1))) {
          GOTO(done, __ Word32Constant(0));
        } ELSE {
          GOTO(done, __ Int32Mod(lhs, rhs));
        };

        BIND(done, result);
        return result;
      }
      case kExprI32RemU:
        __ TrapIf(__ Word32Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapRemByZero);
        return __ Uint32Mod(lhs, rhs);
      case kExprI32And:
        return __ Word32BitwiseAnd(lhs, rhs);
      case kExprI32Ior:
        return __ Word32BitwiseOr(lhs, rhs);
      case kExprI32Xor:
        return __ Word32BitwiseXor(lhs, rhs);
      case kExprI32Shl:
        // If possible, the bitwise-and gets optimized away later.
        return __ Word32ShiftLeft(lhs, __ Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32ShrS:
        return __ Word32ShiftRightArithmetic(lhs,
                                             __ Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32ShrU:
        return __ Word32ShiftRightLogical(lhs, __ Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32Ror:
        return __ Word32RotateRight(lhs, __ Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32Rol:
        if (SupportedOperations::word32_rol()) {
          return __ Word32RotateLeft(lhs, __ Word32BitwiseAnd(rhs, 0x1f));
        } else {
          return __ Word32RotateRight(
              lhs, __ Word32Sub(32, __ Word32BitwiseAnd(rhs, 0x1f)));
        }
      case kExprI32Eq:
        return __ Word32Equal(lhs, rhs);
      case kExprI32Ne:
        return __ Word32Equal(__ Word32Equal(lhs, rhs), 0);
      case kExprI32LtS:
        return __ Int32LessThan(lhs, rhs);
      case kExprI32LeS:
        return __ Int32LessThanOrEqual(lhs, rhs);
      case kExprI32LtU:
        return __ Uint32LessThan(lhs, rhs);
      case kExprI32LeU:
        return __ Uint32LessThanOrEqual(lhs, rhs);
      case kExprI32GtS:
        return __ Int32LessThan(rhs, lhs);
      case kExprI32GeS:
        return __ Int32LessThanOrEqual(rhs, lhs);
      case kExprI32GtU:
        return __ Uint32LessThan(rhs, lhs);
      case kExprI32GeU:
        return __ Uint32LessThanOrEqual(rhs, lhs);
      case kExprI64Add:
        return __ Word64Add(lhs, rhs);
      case kExprI64Sub:
        return __ Word64Sub(lhs, rhs);
      case kExprI64Mul:
        return __ Word64Mul(lhs, rhs);
      case kExprI64DivS: {
        if constexpr (!Is64()) {
          return BuildDiv64Call(lhs, rhs, ExternalReference::wasm_int64_div(),
                                wasm::TrapId::kTrapDivByZero);
        }
        __ TrapIf(__ Word64Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapDivByZero);
        V<Word32> unrepresentable_condition = __ Word32BitwiseAnd(
            __ Word64Equal(rhs, -1),
            __ Word64Equal(lhs, std::numeric_limits<int64_t>::min()));
        __ TrapIf(unrepresentable_condition, OpIndex::Invalid(),
                  TrapId::kTrapDivUnrepresentable);
        return __ Int64Div(lhs, rhs);
      }
      case kExprI64DivU:
        if constexpr (!Is64()) {
          return BuildDiv64Call(lhs, rhs, ExternalReference::wasm_uint64_div(),
                                wasm::TrapId::kTrapDivByZero);
        }
        __ TrapIf(__ Word64Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapDivByZero);
        return __ Uint64Div(lhs, rhs);
      case kExprI64RemS: {
        if constexpr (!Is64()) {
          return BuildDiv64Call(lhs, rhs, ExternalReference::wasm_int64_mod(),
                                wasm::TrapId::kTrapRemByZero);
        }
        __ TrapIf(__ Word64Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapRemByZero);
        Label<Word64> done(&asm_);
        IF (UNLIKELY(__ Word64Equal(rhs, -1))) {
          GOTO(done, __ Word64Constant(int64_t{0}));
        } ELSE {
          GOTO(done, __ Int64Mod(lhs, rhs));
        };

        BIND(done, result);
        return result;
      }
      case kExprI64RemU:
        if constexpr (!Is64()) {
          return BuildDiv64Call(lhs, rhs, ExternalReference::wasm_uint64_mod(),
                                wasm::TrapId::kTrapRemByZero);
        }
        __ TrapIf(__ Word64Equal(rhs, 0), OpIndex::Invalid(),
                  TrapId::kTrapRemByZero);
        return __ Uint64Mod(lhs, rhs);
      case kExprI64And:
        return __ Word64BitwiseAnd(lhs, rhs);
      case kExprI64Ior:
        return __ Word64BitwiseOr(lhs, rhs);
      case kExprI64Xor:
        return __ Word64BitwiseXor(lhs, rhs);
      case kExprI64Shl:
        // If possible, the bitwise-and gets optimized away later.
        return __ Word64ShiftLeft(
            lhs, __ Word32BitwiseAnd(__ TruncateWord64ToWord32(rhs), 0x3f));
      case kExprI64ShrS:
        return __ Word64ShiftRightArithmetic(
            lhs, __ Word32BitwiseAnd(__ TruncateWord64ToWord32(rhs), 0x3f));
      case kExprI64ShrU:
        return __ Word64ShiftRightLogical(
            lhs, __ Word32BitwiseAnd(__ TruncateWord64ToWord32(rhs), 0x3f));
      case kExprI64Ror:
        return __ Word64RotateRight(
            lhs, __ Word32BitwiseAnd(__ TruncateWord64ToWord32(rhs), 0x3f));
      case kExprI64Rol:
        if (SupportedOperations::word64_rol()) {
          return __ Word64RotateLeft(
              lhs, __ Word32BitwiseAnd(__ TruncateWord64ToWord32(rhs), 0x3f));
        } else {
          return __ Word64RotateRight(
              lhs, __ Word32BitwiseAnd(
                       __ Word32Sub(64, __ TruncateWord64ToWord32(rhs)), 0x3f));
        }
      case kExprI64Eq:
        return __ Word64Equal(lhs, rhs);
      case kExprI64Ne:
        return __ Word32Equal(__ Word64Equal(lhs, rhs), 0);
      case kExprI64LtS:
        return __ Int64LessThan(lhs, rhs);
      case kExprI64LeS:
        return __ Int64LessThanOrEqual(lhs, rhs);
      case kExprI64LtU:
        return __ Uint64LessThan(lhs, rhs);
      case kExprI64LeU:
        return __ Uint64LessThanOrEqual(lhs, rhs);
      case kExprI64GtS:
        return __ Int64LessThan(rhs, lhs);
      case kExprI64GeS:
        return __ Int64LessThanOrEqual(rhs, lhs);
      case kExprI64GtU:
        return __ Uint64LessThan(rhs, lhs);
      case kExprI64GeU:
        return __ Uint64LessThanOrEqual(rhs, lhs);
      case kExprF32CopySign: {
        V<Word32> lhs_without_sign =
            __ Word32BitwiseAnd(__ BitcastFloat32ToWord32(lhs), 0x7fffffff);
        V<Word32> rhs_sign =
            __ Word32BitwiseAnd(__ BitcastFloat32ToWord32(rhs), 0x80000000);
        return __ BitcastWord32ToFloat32(
            __ Word32BitwiseOr(lhs_without_sign, rhs_sign));
      }
      case kExprF32Add:
        return __ Float32Add(lhs, rhs);
      case kExprF32Sub:
        return __ Float32Sub(lhs, rhs);
      case kExprF32Mul:
        return __ Float32Mul(lhs, rhs);
      case kExprF32Div:
        return __ Float32Div(lhs, rhs);
      case kExprF32Eq:
        return __ Float32Equal(lhs, rhs);
      case kExprF32Ne:
        return __ Word32Equal(__ Float32Equal(lhs, rhs), 0);
      case kExprF32Lt:
        return __ Float32LessThan(lhs, rhs);
      case kExprF32Le:
        return __ Float32LessThanOrEqual(lhs, rhs);
      case kExprF32Gt:
        return __ Float32LessThan(rhs, lhs);
      case kExprF32Ge:
        return __ Float32LessThanOrEqual(rhs, lhs);
      case kExprF32Min:
        return __ Float32Min(rhs, lhs);
      case kExprF32Max:
        return __ Float32Max(rhs, lhs);
      case kExprF64CopySign: {
        V<Word64> lhs_without_sign = __ Word64BitwiseAnd(
            __ BitcastFloat64ToWord64(lhs), 0x7fffffffffffffff);
        V<Word64> rhs_sign = __ Word64BitwiseAnd(__ BitcastFloat64ToWord64(rhs),
                                                 0x8000000000000000);
        return __ BitcastWord64ToFloat64(
            __ Word64BitwiseOr(lhs_without_sign, rhs_sign));
      }
      case kExprF64Add:
        return __ Float64Add(lhs, rhs);
      case kExprF64Sub:
        return __ Float64Sub(lhs, rhs);
      case kExprF64Mul:
        return __ Float64Mul(lhs, rhs);
      case kExprF64Div:
        return __ Float64Div(lhs, rhs);
      case kExprF64Eq:
        return __ Float64Equal(lhs, rhs);
      case kExprF64Ne:
        return __ Word32Equal(__ Float64Equal(lhs, rhs), 0);
      case kExprF64Lt:
        return __ Float64LessThan(lhs, rhs);
      case kExprF64Le:
        return __ Float64LessThanOrEqual(lhs, rhs);
      case kExprF64Gt:
        return __ Float64LessThan(rhs, lhs);
      case kExprF64Ge:
        return __ Float64LessThanOrEqual(rhs, lhs);
      case kExprF64Min:
        return __ Float64Min(lhs, rhs);
      case kExprF64Max:
        return __ Float64Max(lhs, rhs);
      case kExprF64Pow:
        return __ Float64Power(lhs, rhs);
      case kExprF64Atan2:
        return __ Float64Atan2(lhs, rhs);
      case kExprF64Mod:
        return CallCStackSlotToStackSlot(
            lhs, rhs, ExternalReference::f64_mod_wrapper_function(),
            MemoryRepresentation::Float64());
      case kExprRefEq:
        return __ TaggedEqual(lhs, rhs);
      case kExprI32AsmjsDivS: {
        // asmjs semantics return 0 when dividing by 0.
        if (SupportedOperations::int32_div_is_safe()) {
          return __ Int32Div(lhs, rhs);
        }
        Label<Word32> done(&asm_);
        IF (UNLIKELY(__ Word32Equal(rhs, 0))) {
          GOTO(done, __ Word32Constant(0));
        } ELSE {
          IF (UNLIKELY(__ Word32Equal(rhs, -1))) {
            GOTO(done, __ Word32Sub(0, lhs));
          } ELSE {
            GOTO(done, __ Int32Div(lhs, rhs));
          }
        }
        BIND(done, result);
        return result;
      }
      case kExprI32AsmjsDivU: {
        // asmjs semantics return 0 when dividing by 0.
        if (SupportedOperations::uint32_div_is_safe()) {
          return __ Uint32Div(lhs, rhs);
        }
        Label<Word32> done(&asm_);
        IF (UNLIKELY(__ Word32Equal(rhs, 0))) {
          GOTO(done, __ Word32Constant(0));
        } ELSE {
          GOTO(done, __ Uint32Div(lhs, rhs));
        }
        BIND(done, result);
        return result;
      }
      case kExprI32AsmjsRemS: {
        // General case for signed integer modulus, with optimization for
        // (unknown) power of 2 right hand side.
        //
        //   if 0 < rhs then
        //     mask = rhs - 1
        //     if rhs & mask != 0 then
        //       lhs % rhs
        //     else
        //       if lhs < 0 then
        //         -(-lhs & mask)
        //       else
        //         lhs & mask
        //   else
        //     if rhs < -1 then
        //       lhs % rhs
        //     else
        //       zero
        Label<Word32> done(&asm_);
        IF (__ Int32LessThan(0, rhs)) {
          V<Word32> mask = __ Word32Sub(rhs, 1);
          IF (__ Word32Equal(__ Word32BitwiseAnd(rhs, mask), 0)) {
            IF (UNLIKELY(__ Int32LessThan(lhs, 0))) {
              V<Word32> neg_lhs = __ Word32Sub(0, lhs);
              V<Word32> combined = __ Word32BitwiseAnd(neg_lhs, mask);
              GOTO(done, __ Word32Sub(0, combined));
            } ELSE {
              GOTO(done, __ Word32BitwiseAnd(lhs, mask));
            }
          } ELSE {
            GOTO(done, __ Int32Mod(lhs, rhs));
          }
        } ELSE {
          IF (__ Int32LessThan(rhs, -1)) {
            GOTO(done, __ Int32Mod(lhs, rhs));
          } ELSE {
            GOTO(done, __ Word32Constant(0));
          }
        }
        BIND(done, result);
        return result;
      }
      case kExprI32AsmjsRemU: {
        // asmjs semantics return 0 for mod with 0.
        Label<Word32> done(&asm_);
        IF (UNLIKELY(__ Word32Equal(rhs, 0))) {
          GOTO(done, __ Word32Constant(0));
        } ELSE {
          GOTO(done, __ Uint32Mod(lhs, rhs));
        }
        BIND(done, result);
        return result;
      }
      case kExprI32AsmjsStoreMem8:
        AsmjsStoreMem(lhs, rhs, MemoryRepresentation::Int8());
        return rhs;
      case kExprI32AsmjsStoreMem16:
        AsmjsStoreMem(lhs, rhs, MemoryRepresentation::Int16());
        return rhs;
      case kExprI32AsmjsStoreMem:
        AsmjsStoreMem(lhs, rhs, MemoryRepresentation::Int32());
        return rhs;
      case kExprF32AsmjsStoreMem:
        AsmjsStoreMem(lhs, rhs, MemoryRepresentation::Float32());
        return rhs;
      case kExprF64AsmjsStoreMem:
        AsmjsStoreMem(lhs, rhs, MemoryRepresentation::Float64());
        return rhs;
      default:
        UNREACHABLE();
    }
  }

  std::pair<V<WordPtr>, compiler::BoundsCheckResult> BoundsCheckMem(
      const wasm::WasmMemory* memory, MemoryRepresentation repr, OpIndex index,
      uintptr_t offset, compiler::EnforceBoundsCheck enforce_bounds_check,
      compiler::AlignmentCheck alignment_check) {
    // The function body decoder already validated that the access is not
    // statically OOB.
    DCHECK(base::IsInBounds(offset, static_cast<uintptr_t>(repr.SizeInBytes()),
                            memory->max_memory_size));

    wasm::BoundsCheckStrategy bounds_checks = memory->bounds_checks;
    // Convert the index to uintptr.
    V<WordPtr> converted_index = index;
    if (!memory->is_memory64) {
      converted_index = __ ChangeUint32ToUintPtr(index);
    } else if (kSystemPointerSize == kInt32Size) {
      // Truncate index to 32-bit.
      converted_index = V<WordPtr>::Cast(__ TruncateWord64ToWord32(index));
    }

    const uintptr_t align_mask = repr.SizeInBytes() - 1;
    // Do alignment checks only for > 1 byte accesses (otherwise they trivially
    // pass).
    if (static_cast<bool>(alignment_check) && align_mask != 0) {
      // TODO(14108): Optimize constant index as per wasm-compiler.cc.

      // Unlike regular memory accesses, atomic memory accesses should trap if
      // the effective offset is misaligned.
      // TODO(wasm): this addition is redundant with one inserted by
      // {MemBuffer}.
      OpIndex effective_offset =
          __ WordPtrAdd(MemBuffer(memory->index, offset), converted_index);

      V<Word32> cond = __ TruncateWordPtrToWord32(__ WordPtrBitwiseAnd(
          effective_offset, __ IntPtrConstant(align_mask)));
      __ TrapIfNot(__ Word32Equal(cond, __ Word32Constant(0)),
                   OpIndex::Invalid(), TrapId::kTrapUnalignedAccess);
    }

    // If no bounds checks should be performed (for testing), just return the
    // converted index and assume it to be in-bounds.
    if (bounds_checks == wasm::kNoBoundsChecks) {
      return {converted_index, compiler::BoundsCheckResult::kInBounds};
    }

    if (memory->is_memory64 && kSystemPointerSize == kInt32Size) {
      // In memory64 mode on 32-bit systems, the upper 32 bits need to be zero
      // to succeed the bounds check.
      DCHECK_EQ(kExplicitBoundsChecks, bounds_checks);
      V<Word32> high_word =
          __ TruncateWord64ToWord32(__ Word64ShiftRightLogical(index, 32));
      __ TrapIf(high_word, OpIndex::Invalid(), TrapId::kTrapMemOutOfBounds);
    }

    // We already checked that offset is below the max memory size.
    DCHECK_LT(offset, memory->max_memory_size);

    uintptr_t end_offset = offset + repr.SizeInBytes() - 1u;

    // The index can be invalid if we are generating unreachable operations.
    if (end_offset <= memory->min_memory_size && index.valid() &&
        __ output_graph().Get(index).Is<ConstantOp>()) {
      ConstantOp& constant_index_op =
          __ output_graph().Get(index).Cast<ConstantOp>();
      uintptr_t constant_index = memory->is_memory64
                                     ? constant_index_op.word64()
                                     : constant_index_op.word32();
      if (constant_index < memory->min_memory_size - end_offset) {
        return {converted_index, compiler::BoundsCheckResult::kInBounds};
      }
    }

    using Implementation = compiler::turboshaft::SelectOp::Implementation;
    if (bounds_checks == kTrapHandler &&
        enforce_bounds_check ==
            compiler::EnforceBoundsCheck::kCanOmitBoundsCheck) {
      if (memory->is_memory64) {
        converted_index = __ Select(
            __ Word64ShiftRightLogical(
                V<Word64>::Cast(converted_index),
                memory->GetMemory64GuardsShift()),  // cond
            __ Load(__ LoadRootRegister(), LoadOp::Kind::RawAligned(),
                    MemoryRepresentation::UintPtr(),
                    IsolateData::wasm64_oob_offset_offset()),  // vtrue
            V<Word64>::Cast(converted_index),                  // vfalse
            RegisterRepresentation::Word64(), BranchHint::kNone,
            Implementation::kCMove);
      }
      return {converted_index, compiler::BoundsCheckResult::kTrapHandler};
    }

    V<WordPtr> memory_size = MemSize(memory->index);
    if (end_offset > memory->min_memory_size) {
      // The end offset is larger than the smallest memory.
      // Dynamically check the end offset against the dynamic memory size.
      __ TrapIfNot(
          __ UintPtrLessThan(__ UintPtrConstant(end_offset), memory_size),
          OpIndex::Invalid(), TrapId::kTrapMemOutOfBounds);
    }

    // This produces a positive number since {end_offset <= min_size <=
    // mem_size}.
    V<WordPtr> effective_size = __ WordPtrSub(memory_size, end_offset);
    __ TrapIfNot(__ UintPtrLessThan(converted_index, effective_size),
                 OpIndex::Invalid(), TrapId::kTrapMemOutOfBounds);
    return {converted_index, compiler::BoundsCheckResult::kDynamicallyChecked};
  }

  V<WordPtr> MemStart(uint32_t index) {
    if (index == 0) {
      // TODO(14108): Port TF's dynamic "cached_memory_index" infrastructure.
      return instance_cache_.memory0_start();
    } else {
      V<ByteArray> instance_memories = LOAD_IMMUTABLE_INSTANCE_FIELD(
          trusted_instance_data(), MemoryBasesAndSizes,
          MemoryRepresentation::TaggedPointer());
      return __ Load(instance_memories, LoadOp::Kind::TaggedBase(),
                     kMaybeSandboxedPointer,
                     ByteArray::kHeaderSize +
                         2 * index * kMaybeSandboxedPointer.SizeInBytes());
    }
  }

  V<WordPtr> MemBuffer(uint32_t mem_index, uintptr_t offset) {
    V<WordPtr> mem_start = MemStart(mem_index);
    if (offset == 0) return mem_start;
    return __ WordPtrAdd(mem_start, offset);
  }

  V<WordPtr> MemSize(uint32_t index) {
    if (index == 0) {
      // TODO(14108): Port TF's dynamic "cached_memory_index" infrastructure.
      return instance_cache_.memory0_size();
    } else {
      V<ByteArray> instance_memories = LOAD_IMMUTABLE_INSTANCE_FIELD(
          trusted_instance_data(), MemoryBasesAndSizes,
          MemoryRepresentation::TaggedPointer());
      return __ Load(
          instance_memories, LoadOp::Kind::TaggedBase().NotLoadEliminable(),
          MemoryRepresentation::UintPtr(),
          ByteArray::kHeaderSize + (2 * index + 1) * kSystemPointerSize);
    }
  }

  LoadOp::Kind GetMemoryAccessKind(
      MemoryRepresentation repr,
      compiler::BoundsCheckResult bounds_check_result) {
    LoadOp::Kind result;
    if (bounds_check_result == compiler::BoundsCheckResult::kTrapHandler) {
      DCHECK(repr == MemoryRepresentation::Int8() ||
             repr == MemoryRepresentation::Uint8() ||
             SupportedOperations::IsUnalignedLoadSupported(repr));
      result = LoadOp::Kind::Protected();
    } else if (repr != MemoryRepresentation::Int8() &&
               repr != MemoryRepresentation::Uint8() &&
               !SupportedOperations::IsUnalignedLoadSupported(repr)) {
      result = LoadOp::Kind::RawUnaligned();
    } else {
      result = LoadOp::Kind::RawAligned();
    }
    return result.NotLoadEliminable();
  }

  void TraceMemoryOperation(FullDecoder* decoder, bool is_store,
                            MemoryRepresentation repr, V<WordPtr> index,
                            uintptr_t offset) {
    int kAlign = 4;  // Ensure that the LSB is 0, like a Smi.
    V<WordPtr> info = __ StackSlot(sizeof(MemoryTracingInfo), kAlign);
    V<WordPtr> effective_offset = __ WordPtrAdd(index, offset);
    __ Store(info, effective_offset, StoreOp::Kind::RawAligned(),
             MemoryRepresentation::UintPtr(), compiler::kNoWriteBarrier,
             offsetof(MemoryTracingInfo, offset));
    __ Store(info, __ Word32Constant(is_store ? 1 : 0),
             StoreOp::Kind::RawAligned(), MemoryRepresentation::Uint8(),
             compiler::kNoWriteBarrier, offsetof(MemoryTracingInfo, is_store));
    V<Word32> rep_as_int = __ Word32Constant(
        static_cast<int>(repr.ToMachineType().representation()));
    __ Store(info, rep_as_int, StoreOp::Kind::RawAligned(),
             MemoryRepresentation::Uint8(), compiler::kNoWriteBarrier,
             offsetof(MemoryTracingInfo, mem_rep));
    CallRuntime(decoder->zone(), Runtime::kWasmTraceMemory, {info},
                __ NoContextConstant());
  }

  void StackCheck(StackCheckOp::CheckKind kind) {
    if (V8_UNLIKELY(!v8_flags.wasm_stack_checks)) return;
    __ StackCheck(StackCheckOp::CheckOrigin::kFromWasm, kind);
  }

 private:
  std::pair<V<WordPtr>, V<HeapObject>> BuildImportedFunctionTargetAndRef(
      ConstOrV<Word32> function_index) {
    return WasmGraphBuilderBase::BuildImportedFunctionTargetAndRef(
        function_index, instance_cache_.trusted_instance_data());
  }

  // Returns the call target and the ref (WasmTrustedInstanceData or
  // WasmApiFunctionRef) for an indirect call.
  std::pair<V<WordPtr>, V<ExposedTrustedObject>> BuildIndirectCallTargetAndRef(
      FullDecoder* decoder, OpIndex index, CallIndirectImmediate imm) {
    uint32_t table_index = imm.table_imm.index;
    // Use zero-extension instead of sign-extension; for negative values this
    // will still fail the bounds check because tables have limited size.
    static_assert(kV8MaxWasmTableSize < size_t{kMaxInt});
    V<WordPtr> index_intptr = __ ChangeUint32ToUintPtr(index);
    uint32_t sig_index = imm.sig_imm.index;

    /* Step 1: Load the indirect function tables for this table. */
    V<WasmDispatchTable> dispatch_table;
    if (table_index == 0) {
      dispatch_table = V<WasmDispatchTable>::Cast(LOAD_PROTECTED_INSTANCE_FIELD(
          trusted_instance_data(), DispatchTable0));
    } else {
      V<ProtectedFixedArray> dispatch_tables =
          V<ProtectedFixedArray>::Cast(LOAD_PROTECTED_INSTANCE_FIELD(
              trusted_instance_data(), DispatchTables));
      dispatch_table = V<WasmDispatchTable>::Cast(
          __ LoadProtectedFixedArrayElement(dispatch_tables, table_index));
    }

    /* Step 2: Bounds check against the table size. */
    const WasmTable& table = decoder->module_->tables[table_index];
    V<Word32> table_length;
    bool needs_dynamic_size =
        !table.has_maximum_size || table.maximum_size != table.initial_size;
    if (needs_dynamic_size) {
      table_length = __ LoadField<Word32>(
          dispatch_table, AccessBuilder::ForWasmDispatchTableLength());
    } else {
      table_length = __ Word32Constant(table.initial_size);
    }
    __ TrapIfNot(__ Uint32LessThan(index, table_length), OpIndex::Invalid(),
                 TrapId::kTrapTableOutOfBounds);

    /* Step 3: Check the canonical real signature against the canonical declared
     * signature. */
    bool needs_type_check =
        !EquivalentTypes(table.type.AsNonNull(), ValueType::Ref(sig_index),
                         decoder->module_, decoder->module_);
    bool needs_null_check = table.type.is_nullable();

    V<WordPtr> dispatch_table_entry_offset = __ WordPtrAdd(
        __ WordPtrMul(index_intptr, WasmDispatchTable::kEntrySize),
        WasmDispatchTable::kEntriesOffset);

    if (needs_type_check) {
      V<WordPtr> isorecursive_canonical_types = LOAD_IMMUTABLE_INSTANCE_FIELD(
          trusted_instance_data(), IsorecursiveCanonicalTypes,
          MemoryRepresentation::UintPtr());
      V<Word32> expected_sig_id =
          __ Load(isorecursive_canonical_types, LoadOp::Kind::RawAligned(),
                  MemoryRepresentation::Uint32(), sig_index * kUInt32Size);
      V<Word32> loaded_sig =
          __ Load(dispatch_table, dispatch_table_entry_offset,
                  LoadOp::Kind::TaggedBase(), MemoryRepresentation::Uint32(),
                  WasmDispatchTable::kSigBias);
      V<Word32> sigs_match = __ Word32Equal(expected_sig_id, loaded_sig);
      if (!decoder->module_->types[sig_index].is_final) {
        // In this case, a full type check is needed.
        Label<> end(&asm_);

        // First, check if signatures happen to match exactly.
        GOTO_IF(sigs_match, end);

        if (needs_null_check) {
          // Trap on null element.
          __ TrapIf(__ Word32Equal(loaded_sig, -1), OpIndex::Invalid(),
                    TrapId::kTrapFuncSigMismatch);
        }

        V<Map> formal_rtt =
            __ RttCanon(instance_cache_.managed_object_maps(), sig_index);
        int rtt_depth = GetSubtypingDepth(decoder->module_, sig_index);
        DCHECK_GE(rtt_depth, 0);

        // Since we have the canonical index of the real rtt, we have to load it
        // from the isolate rtt-array (which is canonically indexed). Since this
        // reference is weak, we have to promote it to a strong reference.
        // Note: The reference cannot have been cleared: Since the loaded_sig
        // corresponds to a function of the same canonical type, that function
        // will have kept the type alive.
        V<WeakArrayList> rtts = LOAD_ROOT(WasmCanonicalRtts);
        V<Object> weak_rtt = __ Load(
            rtts, __ ChangeInt32ToIntPtr(loaded_sig),
            LoadOp::Kind::TaggedBase(), MemoryRepresentation::TaggedPointer(),
            WeakArrayList::kHeaderSize, kTaggedSizeLog2);
        V<Map> real_rtt =
            V<Map>::Cast(__ BitcastWordPtrToTagged(__ WordPtrBitwiseAnd(
                __ BitcastHeapObjectToWordPtr(V<HeapObject>::Cast(weak_rtt)),
                ~kWeakHeapObjectMask)));
        V<WasmTypeInfo> type_info =
            __ Load(real_rtt, LoadOp::Kind::TaggedBase(),
                    MemoryRepresentation::TaggedPointer(),
                    Map::kConstructorOrBackPointerOrNativeContextOffset);
        // If the depth of the rtt is known to be less than the minimum
        // supertype array length, we can access the supertype without
        // bounds-checking the supertype array.
        if (static_cast<uint32_t>(rtt_depth) >=
            wasm::kMinimumSupertypeArraySize) {
          V<Word32> supertypes_length =
              __ UntagSmi(__ Load(type_info, LoadOp::Kind::TaggedBase(),
                                  MemoryRepresentation::TaggedSigned(),
                                  WasmTypeInfo::kSupertypesLengthOffset));
          __ TrapIfNot(__ Uint32LessThan(rtt_depth, supertypes_length),
                       OpIndex::Invalid(), TrapId::kTrapFuncSigMismatch);
        }
        V<Map> maybe_match =
            __ Load(type_info, LoadOp::Kind::TaggedBase(),
                    MemoryRepresentation::TaggedPointer(),
                    WasmTypeInfo::kSupertypesOffset + kTaggedSize * rtt_depth);
        __ TrapIfNot(__ TaggedEqual(maybe_match, formal_rtt),
                     OpIndex::Invalid(), TrapId::kTrapFuncSigMismatch);
        GOTO(end);
        BIND(end);
      } else {
        // In this case, signatures must match exactly.
        __ TrapIfNot(sigs_match, OpIndex::Invalid(),
                     TrapId::kTrapFuncSigMismatch);
      }
    } else if (needs_null_check) {
      V<Word32> loaded_sig =
          __ Load(dispatch_table, dispatch_table_entry_offset,
                  LoadOp::Kind::TaggedBase(), MemoryRepresentation::Uint32(),
                  WasmDispatchTable::kSigBias);
      __ TrapIf(__ Word32Equal(-1, loaded_sig), OpIndex::Invalid(),
                TrapId::kTrapFuncSigMismatch);
    }

    /* Step 4: Extract ref and target. */
    V<WordPtr> target = __ Load(
        dispatch_table, dispatch_table_entry_offset, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::UintPtr(), WasmDispatchTable::kTargetBias);
    V<ExposedTrustedObject> ref =
        V<ExposedTrustedObject>::Cast(__ LoadProtectedPointerField(
            dispatch_table, dispatch_table_entry_offset,
            LoadOp::Kind::TaggedBase(), WasmDispatchTable::kRefBias, 0));

    return {target, ref};
  }

  // Load the call target and ref (WasmTrustedInstanceData or
  // WasmApiFunctionRef) from a function reference.
  std::pair<V<WordPtr>, V<ExposedTrustedObject>>
  BuildFunctionReferenceTargetAndRef(V<WasmFuncRef> func_ref, ValueType type) {
    if (type.is_nullable() &&
        null_check_strategy_ == compiler::NullCheckStrategy::kExplicit) {
      func_ref = V<WasmFuncRef>::Cast(
          __ AssertNotNull(func_ref, type, TrapId::kTrapNullDereference));
    }

    LoadOp::Kind load_kind =
        type.is_nullable() && null_check_strategy_ ==
                                  compiler::NullCheckStrategy::kTrapHandler
            ? LoadOp::Kind::TrapOnNull()
            : LoadOp::Kind::TaggedBase().Immutable();

    V<WasmInternalFunction> internal_function = V<WasmInternalFunction>::Cast(
        __ Load(func_ref, load_kind, MemoryRepresentation::AnyTagged(),
                WasmInternalFunction::kIndirectRefOffset));

    V<ExposedTrustedObject> ref =
        V<ExposedTrustedObject>::Cast(__ LoadTrustedPointerField(
            internal_function, LoadOp::Kind::TaggedBase().Immutable(),
            kUnknownIndirectPointerTag,
            WasmInternalFunction::kIndirectRefOffset));

#ifdef V8_ENABLE_SANDBOX
    V<Word32> target_handle =
        __ Load(internal_function, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::Uint32(),
                WasmInternalFunction::kCallTargetOffset);
    V<WordPtr> target = __ DecodeExternalPointer(
        target_handle, kWasmInternalFunctionCallTargetTag);
#else
    V<WordPtr> target = __ Load(internal_function, LoadOp::Kind::TaggedBase(),
                                MemoryRepresentation::UintPtr(),
                                WasmInternalFunction::kCallTargetOffset);
#endif
    Label<WordPtr> done(&asm_);
    // For wasm functions, we have a cached handle to a pointer to off-heap
    // code. For WasmJSFunctions we used not to be able to cache this handle
    // because the code was on-heap, so we needed to fetch it every time from
    // the on-heap Code object. However now, when code pointer sandboxing is on,
    // the on-heap Code object also has a handle to off-heap code, so we could
    // probably also cache that somehow.
    // TODO(manoskouk): Figure out how to improve the situation.
    IF (UNLIKELY(__ WordPtrEqual(target, 0))) {
#ifdef V8_ENABLE_SANDBOX
      // In this case we can use a shortcut: the code pointer table (CPT) entry
      // through which we reference the Code object also directly contains the
      // entrypoint, so we don't have to load it from the Code object.
      V<Word32> call_target_handle = __ Load(
          internal_function, LoadOp::Kind::TaggedBase(),
          MemoryRepresentation::Uint32(), WasmInternalFunction::kCodeOffset);
      V<WordPtr> call_target =
          BuildLoadWasmCodeEntrypointViaCodePointer(call_target_handle);
#else
      V<Code> wrapper_code =
          __ Load(internal_function, LoadOp::Kind::TaggedBase(),
                  MemoryRepresentation::TaggedPointer(),
                  WasmInternalFunction::kCodeOffset);
      V<WordPtr> call_target = __ Load(wrapper_code, LoadOp::Kind::TaggedBase(),
                                       MemoryRepresentation::UintPtr(),
                                       Code::kInstructionStartOffset);
#endif
      GOTO(done, call_target);
    } ELSE {
      GOTO(done, target);
    }

    BIND(done, final_target);

    return {final_target, ref};
  }

  OpIndex AnnotateResultIfReference(OpIndex result, wasm::ValueType type) {
    return type.is_object_reference() ? __ AnnotateWasmType(result, type)
                                      : result;
  }

  void BuildWasmCall(FullDecoder* decoder, const FunctionSig* sig,
                     V<WordPtr> callee, V<HeapObject> ref, const Value args[],
                     Value returns[],
                     CheckForException check_for_exception =
                         CheckForException::kCatchInThisFrame) {
    const TSCallDescriptor* descriptor = TSCallDescriptor::Create(
        compiler::GetWasmCallDescriptor(__ graph_zone(), sig),
        compiler::CanThrow::kYes, __ graph_zone());

    SmallZoneVector<OpIndex, 16> arg_indices(sig->parameter_count() + 1,
                                             decoder->zone());
    arg_indices[0] = ref;
    for (uint32_t i = 0; i < sig->parameter_count(); i++) {
      arg_indices[i + 1] = args[i].op;
    }

    OpIndex call = CallAndMaybeCatchException(
        decoder, callee, base::VectorOf(arg_indices), descriptor,
        check_for_exception, OpEffects().CanCallAnything());

    if (sig->return_count() == 1) {
      returns[0].op = AnnotateResultIfReference(call, sig->GetReturn(0));
    } else if (sig->return_count() > 1) {
      for (uint32_t i = 0; i < sig->return_count(); i++) {
        wasm::ValueType type = sig->GetReturn(i);
        returns[i].op = AnnotateResultIfReference(
            __ Projection(call, i, RepresentationFor(type)), type);
      }
    }
    // Calls might mutate cached instance fields.
    instance_cache_.ReloadCachedMemory();
  }

 private:
  void BuildWasmMaybeReturnCall(FullDecoder* decoder, const FunctionSig* sig,
                                V<WordPtr> callee, V<HeapObject> ref,
                                const Value args[]) {
    if (mode_ == kRegular || mode_ == kInlinedTailCall) {
      const TSCallDescriptor* descriptor = TSCallDescriptor::Create(
          compiler::GetWasmCallDescriptor(__ graph_zone(), sig),
          compiler::CanThrow::kYes, __ graph_zone());

      SmallZoneVector<OpIndex, 16> arg_indices(sig->parameter_count() + 1,
                                               decoder->zone_);
      arg_indices[0] = ref;
      for (uint32_t i = 0; i < sig->parameter_count(); i++) {
        arg_indices[i + 1] = args[i].op;
      }
      __ TailCall(callee, base::VectorOf(arg_indices), descriptor);
    } else {
      if (__ generating_unreachable_operations()) return;
      // This is a tail call in the inlinee, which in turn was a regular call.
      // Transform the tail call into a regular call, and return the return
      // values to the caller.
      size_t return_count = sig->return_count();
      SmallZoneVector<Value, 16> returns(return_count, decoder->zone_);
      // Since an exception in a tail call cannot be caught in this frame, we
      // should only catch exceptions in the generated call if this is a
      // recursively inlined function, and the parent frame provides a handler.
      BuildWasmCall(decoder, sig, callee, ref, args, returns.data(),
                    CheckForException::kCatchInParentFrame);
      for (size_t i = 0; i < return_count; i++) {
        return_phis_->AddInputForPhi(i, returns[i].op);
      }
      uint32_t cached_values = instance_cache_.num_mutable_fields();
      for (uint32_t i = 0; i < cached_values; i++) {
        return_phis_->AddInputForPhi(return_count + i,
                                     instance_cache_.mutable_field_value(i));
      }
      __ Goto(return_block_);
    }
  }

  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsContext,
                   compiler::turboshaft::detail::index_type_for_t<
                       typename Descriptor::results_t>>
  CallBuiltinThroughJumptable(
      FullDecoder* decoder, const typename Descriptor::arguments_t& args,
      CheckForException check_for_exception = CheckForException::kNo) {
    DCHECK_NE(check_for_exception, CheckForException::kCatchInParentFrame);

    V<WordPtr> callee =
        __ RelocatableWasmBuiltinCallTarget(Descriptor::kFunction);
    auto arguments = std::apply(
        [](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t> + 1>{
              std::forward<decltype(as)>(as)...};
        },
        args);

    return CallAndMaybeCatchException(
        decoder, callee, base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallWasmRuntimeStub,
                           __ output_graph().graph_zone()),
        check_for_exception, Descriptor::kEffects);
  }

  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsContext,
                   compiler::turboshaft::detail::index_type_for_t<
                       typename Descriptor::results_t>>
  CallBuiltinThroughJumptable(
      FullDecoder* decoder, V<Context> context,
      const typename Descriptor::arguments_t& args,
      CheckForException check_for_exception = CheckForException::kNo) {
    DCHECK_NE(check_for_exception, CheckForException::kCatchInParentFrame);

    V<WordPtr> callee =
        __ RelocatableWasmBuiltinCallTarget(Descriptor::kFunction);
    auto arguments = std::apply(
        [context](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t> + 1>{
              std::forward<decltype(as)>(as)..., context};
        },
        args);

    return CallAndMaybeCatchException(
        decoder, callee, base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallWasmRuntimeStub,
                           __ output_graph().graph_zone()),
        check_for_exception, Descriptor::kEffects);
  }

 private:
  void MaybeSetPositionToParent(OpIndex call,
                                CheckForException check_for_exception) {
    // For tail calls that we transform to regular calls, we need to set the
    // call's position to that of the inlined call node to get correct stack
    // traces.
    if (check_for_exception == CheckForException::kCatchInParentFrame) {
      __ output_graph().operation_origins()[call] = WasmPositionToOpIndex(
          parent_position_.ScriptOffset(), parent_position_.InliningId() == -1
                                               ? kNoInliningId
                                               : parent_position_.InliningId());
    }
  }

  OpIndex CallAndMaybeCatchException(FullDecoder* decoder, V<WordPtr> callee,
                                     base::Vector<const OpIndex> args,
                                     const TSCallDescriptor* descriptor,
                                     CheckForException check_for_exception,
                                     OpEffects effects) {
    if (check_for_exception == CheckForException::kNo) {
      return __ Call(callee, OpIndex::Invalid(), args, descriptor, effects);
    }
    bool handled_in_this_frame =
        decoder && decoder->current_catch() != -1 &&
        check_for_exception == CheckForException::kCatchInThisFrame;
    if (!handled_in_this_frame && mode_ != kInlinedWithCatch) {
      OpIndex call =
          __ Call(callee, OpIndex::Invalid(), args, descriptor, effects);
      MaybeSetPositionToParent(call, check_for_exception);
      return call;
    }

    TSBlock* catch_block;
    if (handled_in_this_frame) {
      Control* current_catch =
          decoder->control_at(decoder->control_depth_of_current_catch());
      catch_block = current_catch->false_or_loop_or_catch_block;
    } else {
      DCHECK_EQ(mode_, kInlinedWithCatch);
      catch_block = return_catch_block_;
    }
    TSBlock* success_block = __ NewBlock();
    TSBlock* exception_block = __ NewBlock();
    OpIndex call;
    {
      Assembler::CatchScope scope(asm_, exception_block);

      call = __ Call(callee, OpIndex::Invalid(), args, descriptor, effects);
      __ Goto(success_block);
    }

    __ Bind(exception_block);
    OpIndex exception = __ CatchBlockBegin();
    if (handled_in_this_frame) {
      // The exceptional operation could have modified memory size; we need
      // to reload the memory context into the exceptional control path.
      // Saving and restoring the InstanceCache's state makes sure that once
      // we get back to handling the success path, the cache correctly
      // reflects the values available on that path.
      InstanceCache::Snapshot saved = instance_cache_.SaveState();
      instance_cache_.ReloadCachedMemory();
      SetupControlFlowEdge(decoder, catch_block, 0, exception);
      instance_cache_.RestoreFromSnapshot(saved);
    } else {
      DCHECK_EQ(mode_, kInlinedWithCatch);
      if (exception.valid()) return_phis_->AddIncomingException(exception);
      // Reloading the InstanceCache will happen when {return_exception_phis_}
      // are retrieved.
    }
    __ Goto(catch_block);

    __ Bind(success_block);

    MaybeSetPositionToParent(call, check_for_exception);

    return call;
  }

  OpIndex CallCStackSlotToInt32(OpIndex arg, ExternalReference ref,
                                MemoryRepresentation arg_type) {
    OpIndex stack_slot_param =
        __ StackSlot(arg_type.SizeInBytes(), arg_type.SizeInBytes());
    __ Store(stack_slot_param, arg, StoreOp::Kind::RawAligned(), arg_type,
             compiler::WriteBarrierKind::kNoWriteBarrier);
    MachineType reps[]{MachineType::Int32(), MachineType::Pointer()};
    MachineSignature sig(1, 1, reps);
    return CallC(&sig, ref, stack_slot_param);
  }

  V<Word32> CallCStackSlotToInt32(
      ExternalReference ref,
      std::initializer_list<std::pair<OpIndex, MemoryRepresentation>> args) {
    int slot_size = 0;
    for (auto arg : args) slot_size += arg.second.SizeInBytes();
    // Since we are storing the arguments unaligned anyway, we do not need
    // alignment > 0.
    V<WordPtr> stack_slot_param = __ StackSlot(slot_size, 0);
    int offset = 0;
    for (auto arg : args) {
      __ Store(stack_slot_param, arg.first,
               StoreOp::Kind::MaybeUnaligned(arg.second), arg.second,
               compiler::WriteBarrierKind::kNoWriteBarrier, offset);
      offset += arg.second.SizeInBytes();
    }
    MachineType reps[]{MachineType::Int32(), MachineType::Pointer()};
    MachineSignature sig(1, 1, reps);
    return CallC(&sig, ref, stack_slot_param);
  }

  OpIndex CallCStackSlotToStackSlot(OpIndex arg, ExternalReference ref,
                                    MemoryRepresentation arg_type) {
    V<WordPtr> stack_slot =
        __ StackSlot(arg_type.SizeInBytes(), arg_type.SizeInBytes());
    __ Store(stack_slot, arg, StoreOp::Kind::RawAligned(), arg_type,
             compiler::WriteBarrierKind::kNoWriteBarrier);
    MachineType reps[]{MachineType::Pointer()};
    MachineSignature sig(0, 1, reps);
    CallC(&sig, ref, stack_slot);
    return __ Load(stack_slot, LoadOp::Kind::RawAligned(), arg_type);
  }

  OpIndex CallCStackSlotToStackSlot(OpIndex arg0, OpIndex arg1,
                                    ExternalReference ref,
                                    MemoryRepresentation arg_type) {
    V<WordPtr> stack_slot =
        __ StackSlot(2 * arg_type.SizeInBytes(), arg_type.SizeInBytes());
    __ Store(stack_slot, arg0, StoreOp::Kind::RawAligned(), arg_type,
             compiler::WriteBarrierKind::kNoWriteBarrier);
    __ Store(stack_slot, arg1, StoreOp::Kind::RawAligned(), arg_type,
             compiler::WriteBarrierKind::kNoWriteBarrier,
             arg_type.SizeInBytes());
    MachineType reps[]{MachineType::Pointer()};
    MachineSignature sig(0, 1, reps);
    CallC(&sig, ref, stack_slot);
    return __ Load(stack_slot, LoadOp::Kind::RawAligned(), arg_type);
  }

  V<WordPtr> MemoryIndexToUintPtrOrOOBTrap(bool memory_is_64, OpIndex index) {
    if (!memory_is_64) return __ ChangeUint32ToUintPtr(index);
    if (kSystemPointerSize == kInt64Size) return index;
    __ TrapIf(__ TruncateWord64ToWord32(__ Word64ShiftRightLogical(index, 32)),
              OpIndex::Invalid(), TrapId::kTrapMemOutOfBounds);
    return OpIndex(__ TruncateWord64ToWord32(index));
  }

  V<Smi> ChangeUint31ToSmi(V<Word32> value) {
    if constexpr (COMPRESS_POINTERS_BOOL) {
      return V<Smi>::Cast(
          __ Word32ShiftLeft(value, kSmiShiftSize + kSmiTagSize));
    } else {
      return V<Smi>::Cast(__ WordPtrShiftLeft(__ ChangeUint32ToUintPtr(value),
                                              kSmiShiftSize + kSmiTagSize));
    }
  }

  V<Word32> ChangeSmiToUint32(V<Smi> value) {
    if constexpr (COMPRESS_POINTERS_BOOL) {
      return __ Word32ShiftRightLogical(V<Word32>::Cast(value),
                                        kSmiShiftSize + kSmiTagSize);
    } else {
      return __ TruncateWordPtrToWord32(__ WordPtrShiftRightLogical(
          V<WordPtr>::Cast(value), kSmiShiftSize + kSmiTagSize));
    }
  }

  V<WordPtr> BuildLoadWasmCodeEntrypointViaCodePointer(V<Word32> handle) {
#ifdef V8_ENABLE_SANDBOX
    V<Word32> index =
        __ Word32ShiftRightLogical(handle, kCodePointerHandleShift);
    V<WordPtr> offset = __ ChangeUint32ToUintPtr(
        __ Word32ShiftLeft(index, kCodePointerTableEntrySizeLog2));
    V<WordPtr> table =
        __ ExternalConstant(ExternalReference::code_pointer_table_address());
    V<WordPtr> entry = __ Load(table, offset, LoadOp::Kind::RawAligned(),
                               MemoryRepresentation::UintPtr());
    return __ Word64BitwiseXor(entry, __ UintPtrConstant(kWasmEntrypointTag));
#else
    UNREACHABLE();
#endif
  }

  void BuildEncodeException32BitValue(V<FixedArray> values_array,
                                      uint32_t index, V<Word32> value) {
    V<Smi> upper_half =
        ChangeUint31ToSmi(__ Word32ShiftRightLogical(value, 16));
    __ StoreFixedArrayElement(values_array, index, upper_half,
                              compiler::kNoWriteBarrier);
    V<Smi> lower_half = ChangeUint31ToSmi(__ Word32BitwiseAnd(value, 0xffffu));
    __ StoreFixedArrayElement(values_array, index + 1, lower_half,
                              compiler::kNoWriteBarrier);
  }

  V<Word32> BuildDecodeException32BitValue(V<FixedArray> exception_values_array,
                                           int index) {
    V<Word32> upper_half = __ Word32ShiftLeft(
        ChangeSmiToUint32(V<Smi>::Cast(
            __ LoadFixedArrayElement(exception_values_array, index))),
        16);
    V<Word32> lower_half = ChangeSmiToUint32(V<Smi>::Cast(
        __ LoadFixedArrayElement(exception_values_array, index + 1)));
    return __ Word32BitwiseOr(upper_half, lower_half);
  }

  V<Word64> BuildDecodeException64BitValue(V<FixedArray> exception_values_array,
                                           int index) {
    V<Word64> upper_half = __ Word64ShiftLeft(
        __ ChangeUint32ToUint64(
            BuildDecodeException32BitValue(exception_values_array, index)),
        32);
    V<Word64> lower_half = __ ChangeUint32ToUint64(
        BuildDecodeException32BitValue(exception_values_array, index + 2));
    return __ Word64BitwiseOr(upper_half, lower_half);
  }

  void UnpackWasmException(FullDecoder* decoder, V<Object> exception,
                           base::Vector<Value> values) {
    V<FixedArray> exception_values_array = V<FixedArray>::Cast(
        CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmGetOwnProperty>(
            decoder, instance_cache_.native_context(),
            {exception, LOAD_ROOT(wasm_exception_values_symbol)}));

    int index = 0;
    for (Value& value : values) {
      switch (value.type.kind()) {
        case kI32:
          value.op =
              BuildDecodeException32BitValue(exception_values_array, index);
          index += 2;
          break;
        case kI64:
          value.op =
              BuildDecodeException64BitValue(exception_values_array, index);
          index += 4;
          break;
        case kF32:
          value.op = __ BitcastWord32ToFloat32(
              BuildDecodeException32BitValue(exception_values_array, index));
          index += 2;
          break;
        case kF64:
          value.op = __ BitcastWord64ToFloat64(
              BuildDecodeException64BitValue(exception_values_array, index));
          index += 4;
          break;
        case kS128: {
          value.op = __ Simd128Splat(
              BuildDecodeException32BitValue(exception_values_array, index),
              compiler::turboshaft::Simd128SplatOp::Kind::kI32x4);
          index += 2;
          using Kind = compiler::turboshaft::Simd128ReplaceLaneOp::Kind;
          value.op = __ Simd128ReplaceLane(
              value.op,
              BuildDecodeException32BitValue(exception_values_array, index),
              Kind::kI32x4, 1);
          index += 2;
          value.op = __ Simd128ReplaceLane(
              value.op,
              BuildDecodeException32BitValue(exception_values_array, index),
              Kind::kI32x4, 2);
          index += 2;
          value.op = __ Simd128ReplaceLane(
              value.op,
              BuildDecodeException32BitValue(exception_values_array, index),
              Kind::kI32x4, 3);
          index += 2;
          break;
        }
        case kRtt:
        case kRef:
        case kRefNull:
          value.op = __ LoadFixedArrayElement(exception_values_array, index);
          index++;
          break;
        case kI8:
        case kI16:
        case kVoid:
        case kBottom:
          UNREACHABLE();
      }
    }
  }

  void ThrowRef(FullDecoder* decoder, OpIndex exn) {
    CallBuiltinThroughJumptable<BuiltinCallDescriptor::WasmRethrow>(
        decoder, {exn}, CheckForException::kCatchInThisFrame);
    __ Unreachable();
  }

  void AsmjsStoreMem(V<Word32> index, OpIndex value,
                     MemoryRepresentation repr) {
    // Since asmjs does not support unaligned accesses, we can bounds-check
    // ignoring the access size.
    // Technically, we should do a signed 32-to-ptr extension here. However,
    // that is an explicit instruction, whereas unsigned extension is implicit.
    // Since the difference is only observable for memories larger than 2 GiB,
    // and since we disallow such memories, we can use unsigned extension.
    V<WordPtr> index_ptr = __ ChangeUint32ToUintPtr(index);
    IF (LIKELY(__ UintPtrLessThan(index_ptr, MemSize(0)))) {
      __ Store(MemStart(0), index_ptr, value, StoreOp::Kind::RawAligned(), repr,
               compiler::kNoWriteBarrier, 0);
    }
  }

  OpIndex AsmjsLoadMem(V<Word32> index, MemoryRepresentation repr) {
    // Since asmjs does not support unaligned accesses, we can bounds-check
    // ignoring the access size.
    Variable result = __ NewVariable(repr.ToRegisterRepresentation());

    // Technically, we should do a signed 32-to-ptr extension here. However,
    // that is an explicit instruction, whereas unsigned extension is implicit.
    // Since the difference is only observable for memories larger than 2 GiB,
    // and since we disallow such memories, we can use unsigned extension.
    V<WordPtr> index_ptr = __ ChangeUint32ToUintPtr(index);
    IF (LIKELY(__ UintPtrLessThan(index_ptr, MemSize(0)))) {
      __ SetVariable(result, __ Load(MemStart(0), index_ptr,
                                     LoadOp::Kind::RawAligned(), repr));
    } ELSE {
      switch (repr) {
        case MemoryRepresentation::Int8():
        case MemoryRepresentation::Int16():
        case MemoryRepresentation::Int32():
        case MemoryRepresentation::Uint8():
        case MemoryRepresentation::Uint16():
        case MemoryRepresentation::Uint32():
          __ SetVariable(result, __ Word32Constant(0));
          break;
        case MemoryRepresentation::Float32():
          __ SetVariable(result, __ Float32Constant(
                                     std::numeric_limits<float>::quiet_NaN()));
          break;
        case MemoryRepresentation::Float64():
          __ SetVariable(result, __ Float64Constant(
                                     std::numeric_limits<double>::quiet_NaN()));
          break;
        default:
          UNREACHABLE();
      }
    }

    OpIndex result_op = __ GetVariable(result);
    __ SetVariable(result, OpIndex::Invalid());
    return result_op;
  }

  void BoundsCheckArray(V<HeapObject> array, V<Word32> index,
                        ValueType array_type) {
    if (V8_UNLIKELY(v8_flags.experimental_wasm_skip_bounds_checks)) {
      if (array_type.is_nullable()) {
        __ AssertNotNull(array, array_type, TrapId::kTrapNullDereference);
      }
    } else {
      OpIndex length = __ ArrayLength(array, array_type.is_nullable()
                                                 ? compiler::kWithNullCheck
                                                 : compiler::kWithoutNullCheck);
      __ TrapIfNot(__ Uint32LessThan(index, length), OpIndex::Invalid(),
                   TrapId::kTrapArrayOutOfBounds);
    }
  }

  void BoundsCheckArrayWithLength(V<HeapObject> array, V<Word32> index,
                                  V<Word32> length,
                                  compiler::CheckForNull null_check) {
    if (V8_UNLIKELY(v8_flags.experimental_wasm_skip_bounds_checks)) return;
    V<Word32> array_length = __ ArrayLength(array, null_check);
    V<Word32> range_end = __ Word32Add(index, length);
    V<Word32> range_valid = __ Word32BitwiseAnd(
        // OOB if (index + length > array.len).
        __ Uint32LessThanOrEqual(range_end, array_length),
        // OOB if (index + length) overflows.
        __ Uint32LessThanOrEqual(index, range_end));
    __ TrapIfNot(range_valid, OpIndex::Invalid(),
                 TrapId::kTrapArrayOutOfBounds);
  }

  void BrOnCastImpl(FullDecoder* decoder, V<Map> rtt,
                    compiler::WasmTypeCheckConfig config, const Value& object,
                    Value* value_on_branch, uint32_t br_depth,
                    bool null_succeeds) {
    OpIndex cast_succeeds = __ WasmTypeCheck(object.op, rtt, config);
    IF (cast_succeeds) {
      // Narrow type for the successful cast target branch.
      Forward(decoder, object, value_on_branch);
      BrOrRet(decoder, br_depth);
    }
    // Note: Differently to below for br_on_cast_fail, we do not Forward
    // the value here to perform a TypeGuard. It can't be done here due to
    // asymmetric decoder code. A Forward here would be popped from the stack
    // and ignored by the decoder. Therefore the decoder has to call Forward
    // itself.
  }

  void BrOnCastFailImpl(FullDecoder* decoder, V<Map> rtt,
                        compiler::WasmTypeCheckConfig config,
                        const Value& object, Value* value_on_fallthrough,
                        uint32_t br_depth, bool null_succeeds) {
    OpIndex cast_succeeds = __ WasmTypeCheck(object.op, rtt, config);
    IF (__ Word32Equal(cast_succeeds, 0)) {
      // It is necessary in case of {null_succeeds} to forward the value.
      // This will add a TypeGuard to the non-null type (as in this case the
      // object is non-nullable).
      Forward(decoder, object, decoder->stack_value(1));
      BrOrRet(decoder, br_depth);
    }
    // Narrow type for the successful cast fallthrough branch.
    Forward(decoder, object, value_on_fallthrough);
  }

  V<HeapObject> ArrayNewImpl(FullDecoder* decoder, uint32_t index,
                             const ArrayType* array_type, OpIndex length,
                             OpIndex initial_value) {
    // Initialize the array header.
    V<Map> rtt = __ RttCanon(instance_cache_.managed_object_maps(), index);
    V<HeapObject> array = __ WasmAllocateArray(rtt, length, array_type);
    // Initialize the elements.
    ArrayFillImpl(array, __ Word32Constant(0), initial_value, length,
                  array_type, false);
    return array;
  }

  V<HeapObject> StructNewImpl(const StructIndexImmediate& imm, OpIndex args[]) {
    V<Map> rtt = __ RttCanon(instance_cache_.managed_object_maps(), imm.index);

    V<HeapObject> struct_value = __ WasmAllocateStruct(rtt, imm.struct_type);
    for (uint32_t i = 0; i < imm.struct_type->field_count(); ++i) {
      __ StructSet(struct_value, args[i], imm.struct_type, imm.index, i,
                   compiler::kWithoutNullCheck);
    }
    // If this assert fails then initialization of padding field might be
    // necessary.
    static_assert(Heap::kMinObjectSizeInTaggedWords == 2 &&
                      WasmStruct::kHeaderSize == 2 * kTaggedSize,
                  "empty struct might require initialization of padding field");
    return struct_value;
  }

  bool IsSimd128ZeroConstant(OpIndex op) {
    DCHECK_IMPLIES(!op.valid(), __ generating_unreachable_operations());
    if (__ generating_unreachable_operations()) return false;
    const Simd128ConstantOp* s128_op =
        __ output_graph().Get(op).TryCast<Simd128ConstantOp>();
    return s128_op && s128_op->IsZero();
  }

  void ArrayFillImpl(V<HeapObject> array, OpIndex index, OpIndex value,
                     OpIndex length, const wasm::ArrayType* type,
                     bool emit_write_barrier) {
    wasm::ValueType element_type = type->element_type();

    // Initialize the array. Use an external function for large arrays with
    // null/number initializer. Use a loop for small arrays and reference arrays
    // with a non-null initial value.
    Label<> done(&asm_);

    // The builtin cannot handle s128 values other than 0.
    if (!(element_type == wasm::kWasmS128 && !IsSimd128ZeroConstant(value))) {
      constexpr uint32_t kArrayNewMinimumSizeForMemSet = 16;
      IF_NOT (__ Uint32LessThan(
                  length, __ Word32Constant(kArrayNewMinimumSizeForMemSet))) {
        OpIndex stack_slot = StoreInInt64StackSlot(value, element_type);
        MachineType arg_types[]{
            MachineType::TaggedPointer(), MachineType::Uint32(),
            MachineType::Uint32(),        MachineType::Uint32(),
            MachineType::Uint32(),        MachineType::Pointer()};
        MachineSignature sig(0, 6, arg_types);
        CallC(&sig, ExternalReference::wasm_array_fill(),
              {array, index, length,
               __ Word32Constant(emit_write_barrier ? 1 : 0),
               __ Word32Constant(element_type.raw_bit_field()), stack_slot});
        GOTO(done);
      }
    }

    ScopedVar<Word32> current_index(this, index);

    WHILE(__ Uint32LessThan(current_index, __ Word32Add(index, length))) {
      __ ArraySet(array, current_index, value, type->element_type());
      current_index = __ Word32Add(current_index, 1);
    }

    GOTO(done);

    BIND(done);
  }

  V<WordPtr> StoreInInt64StackSlot(OpIndex value, wasm::ValueType type) {
    OpIndex value_int64;
    switch (type.kind()) {
      case wasm::kI32:
      case wasm::kI8:
      case wasm::kI16:
        value_int64 = __ ChangeInt32ToInt64(value);
        break;
      case wasm::kI64:
        value_int64 = value;
        break;
      case wasm::kS128:
        // We can only get here if {value} is the constant 0.
        DCHECK(__ output_graph().Get(value).Cast<Simd128ConstantOp>().IsZero());
        value_int64 = __ Word64Constant(uint64_t{0});
        break;
      case wasm::kF32:
        value_int64 = __ ChangeUint32ToUint64(__ BitcastFloat32ToWord32(value));
        break;
      case wasm::kF64:
        value_int64 = __ BitcastFloat64ToWord64(value);
        break;
      case wasm::kRefNull:
      case wasm::kRef:
        value_int64 = kTaggedSize == 4 ? __ ChangeInt32ToInt64(value) : value;
        break;
      case wasm::kRtt:
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }

    MemoryRepresentation int64_rep = MemoryRepresentation::Int64();
    V<WordPtr> stack_slot =
        __ StackSlot(int64_rep.SizeInBytes(), int64_rep.SizeInBytes());
    __ Store(stack_slot, value_int64, StoreOp::Kind::RawAligned(), int64_rep,
             compiler::WriteBarrierKind::kNoWriteBarrier);
    return stack_slot;
  }

  void InlineWasmCall(FullDecoder* decoder, uint32_t func_index,
                      const FunctionSig* sig, uint32_t feedback_case,
                      bool is_tail_call, const Value args[], Value returns[]) {
    DCHECK_IMPLIES(is_tail_call, returns == nullptr);
    const WasmFunction& inlinee = decoder->module_->functions[func_index];
    DCHECK_EQ(inlinee.sig->return_count(), sig->return_count());
    DCHECK_EQ(inlinee.sig->parameter_count(), sig->parameter_count());
#ifdef DEBUG
    for (size_t i = 0; i < sig->return_count(); ++i) {
      DCHECK(IsSubtypeOf(inlinee.sig->GetReturn(i), sig->GetReturn(i),
                         decoder->module_));
    }
    for (size_t i = 0; i < sig->parameter_count(); ++i) {
      DCHECK(IsSubtypeOf(sig->GetParam(i), inlinee.sig->GetParam(i),
                         decoder->module_));
    }
#endif

    SmallZoneVector<OpIndex, 16> inlinee_args(
        inlinee.sig->parameter_count() + 1, decoder->zone_);
    inlinee_args[0] = trusted_instance_data();
    for (size_t i = 0; i < inlinee.sig->parameter_count(); i++) {
      inlinee_args[i + 1] = args[i].op;
    }

    base::Vector<const uint8_t> function_bytes =
        wire_bytes_->GetCode(inlinee.code);

    bool is_shared = decoder->module_->types[inlinee.sig_index].is_shared;

    const wasm::FunctionBody inlinee_body{inlinee.sig, inlinee.code.offset(),
                                          function_bytes.begin(),
                                          function_bytes.end(), is_shared};

    // If the inlinee was not validated before, do that now.
    if (V8_UNLIKELY(!decoder->module_->function_was_validated(func_index))) {
      if (ValidateFunctionBody(decoder->zone_, decoder->enabled_,
                               decoder->module_, decoder->detected_,
                               inlinee_body)
              .failed()) {
        // At this point we cannot easily raise a compilation error any more.
        // Since this situation is highly unlikely though, we just ignore this
        // inlinee, emit a regular call, and move on. The same validation error
        // will be triggered again when actually compiling the invalid function.
        V<WordPtr> callee =
            __ RelocatableConstant(func_index, RelocInfo::WASM_CALL);
        BuildWasmCall(decoder, sig, callee, trusted_instance_data(), args,
                      returns);
        return;
      }
      decoder->module_->set_function_validated(func_index);
    }

    BlockPhis fresh_return_phis(decoder->zone_);

    Mode inlinee_mode;
    TSBlock* callee_catch_block = nullptr;
    TSBlock* callee_return_block;
    BlockPhis* inlinee_return_phis;

    if (is_tail_call) {
      if (mode_ == kInlinedTailCall || mode_ == kRegular) {
        inlinee_mode = kInlinedTailCall;
        callee_return_block = nullptr;
        inlinee_return_phis = nullptr;
      } else {
        // A tail call inlined inside a regular call inherits its settings,
        // as any `return` statement returns to the nearest non-tail caller.
        inlinee_mode = mode_;
        callee_return_block = return_block_;
        inlinee_return_phis = return_phis_;
        if (mode_ == kInlinedWithCatch) {
          callee_catch_block = return_catch_block_;
        }
      }
    } else {
      // Regular call (i.e. not a tail call).
      if (mode_ == kInlinedWithCatch || decoder->current_catch() != -1) {
        inlinee_mode = kInlinedWithCatch;
        // TODO(14108): If this is a nested inlining, can we forward the
        // caller's catch block instead?
        callee_catch_block = __ NewBlock();
      } else {
        inlinee_mode = kInlinedUnhandled;
      }
      callee_return_block = __ NewBlock();
      inlinee_return_phis = &fresh_return_phis;
    }

    WasmFullDecoder<Decoder::FullValidationTag,
                    TurboshaftGraphBuildingInterface>
        inlinee_decoder(decoder->zone_, decoder->module_, decoder->enabled_,
                        decoder->detected_, inlinee_body, decoder->zone_,
                        nullptr, asm_, inlinee_mode, instance_cache_,
                        assumptions_, inlining_positions_, func_index,
                        wire_bytes_, base::VectorOf(inlinee_args),
                        callee_return_block, inlinee_return_phis,
                        callee_catch_block, is_tail_call);
    SourcePosition call_position =
        SourcePosition(decoder->position(), inlining_id_ == kNoInliningId
                                                ? SourcePosition::kNotInlined
                                                : inlining_id_);
    inlining_positions_->push_back(
        {static_cast<int>(func_index), is_tail_call, call_position});
    inlinee_decoder.interface().set_inlining_id(
        static_cast<uint8_t>(inlining_positions_->size() - 1));
    inlinee_decoder.interface().set_parent_position(call_position);
    if (v8_flags.liftoff) {
      if (inlining_decisions_ && inlining_decisions_->feedback_found()) {
        inlinee_decoder.interface().set_inlining_decisions(
            inlining_decisions_
                ->function_calls()[feedback_slot_][feedback_case]);
      }
    } else {
      no_liftoff_inlining_budget_ -= inlinee.code.length();
      inlinee_decoder.interface().set_no_liftoff_inlining_budget(
          no_liftoff_inlining_budget_);
    }
    inlinee_decoder.Decode();
    // Turboshaft runs with validation, but the function should already be
    // validated, so graph building must always succeed, unless we bailed out.
    DCHECK_IMPLIES(!inlinee_decoder.ok(),
                   inlinee_decoder.interface().did_bailout());
    if (!inlinee_decoder.ok()) {
      Bailout(decoder);
      return;
    }

    DCHECK_IMPLIES(!is_tail_call && inlinee_mode == kInlinedWithCatch,
                   inlinee_return_phis != nullptr);

    if (!is_tail_call && inlinee_mode == kInlinedWithCatch &&
        !inlinee_return_phis->incoming_exceptions().empty()) {
      // We need to handle exceptions in the inlined call.
      __ Bind(callee_catch_block);
      OpIndex exception =
          MaybePhi(inlinee_return_phis->incoming_exceptions(), kWasmExternRef);
      bool handled_in_this_frame = decoder->current_catch() != -1;
      TSBlock* catch_block;
      if (handled_in_this_frame) {
        Control* current_catch =
            decoder->control_at(decoder->control_depth_of_current_catch());
        catch_block = current_catch->false_or_loop_or_catch_block;
        // The exceptional operation could have modified memory size; we need
        // to reload the memory context into the exceptional control path.
        // Saving and restoring the InstanceCache's state makes sure that once
        // we get back to handling the success path, the cache correctly
        // reflects the values available on that path.
        InstanceCache::Snapshot saved = instance_cache_.SaveState();
        instance_cache_.ReloadCachedMemory();
        SetupControlFlowEdge(decoder, catch_block, 0, exception);
        instance_cache_.RestoreFromSnapshot(saved);
      } else {
        DCHECK_EQ(mode_, kInlinedWithCatch);
        catch_block = return_catch_block_;
        if (exception.valid()) return_phis_->AddIncomingException(exception);
        // Reloading the InstanceCache will happen when {return_exception_phis_}
        // are retrieved.
      }
      __ Goto(catch_block);
    }

    if (!is_tail_call) {
      __ Bind(callee_return_block);
      BlockPhis* return_phis = inlinee_decoder.interface().return_phis();
      size_t return_count = inlinee.sig->return_count();
      for (size_t i = 0; i < return_count; i++) {
        returns[i].op =
            MaybePhi(return_phis->phi_inputs(i), return_phis->phi_type(i));
      }

      uint32_t cached_values = instance_cache_.num_mutable_fields();
      for (uint32_t i = 0; i < cached_values; i++) {
        OpIndex phi = MaybePhi(return_phis->phi_inputs(i + return_count),
                               instance_cache_.mutable_field_type(i));
        instance_cache_.set_mutable_field_value(i, phi);
      }
    }

    if (!v8_flags.liftoff) {
      set_no_liftoff_inlining_budget(
          inlinee_decoder.interface().no_liftoff_inlining_budget());
    }
  }

  TrapId GetTrapIdForTrap(wasm::TrapReason reason) {
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

  // We need this shift so that resulting OpIndex offsets are multiples of
  // `sizeof(OperationStorageSlot)`.
  static constexpr int kPositionFieldShift = 3;
  static_assert(sizeof(compiler::turboshaft::OperationStorageSlot) ==
                1 << kPositionFieldShift);
  static constexpr int kPositionFieldSize = 23;
  static_assert(kV8MaxWasmFunctionSize < (1 << kPositionFieldSize));
  static constexpr int kInliningIdFieldSize = 6;
  static constexpr uint8_t kNoInliningId = 63;
  static_assert((1 << kInliningIdFieldSize) - 1 == kNoInliningId);
  // We need to assign inlining_ids to inlined nodes.
  static_assert(kNoInliningId > InliningTree::kMaxInlinedCount);

  // We encode the wasm code position and the inlining index in an OpIndex
  // stored in the output graph's node origins.
  using PositionField =
      base::BitField<WasmCodePosition, kPositionFieldShift, kPositionFieldSize>;
  using InliningIdField = PositionField::Next<uint8_t, kInliningIdFieldSize>;

  OpIndex WasmPositionToOpIndex(WasmCodePosition position, int inlining_id) {
    return OpIndex::FromOffset(PositionField::encode(position) |
                               InliningIdField::encode(inlining_id));
  }

  SourcePosition OpIndexToSourcePosition(OpIndex index) {
    DCHECK(index.valid());
    uint8_t inlining_id = InliningIdField::decode(index.offset());
    return SourcePosition(PositionField::decode(index.offset()),
                          inlining_id == kNoInliningId
                              ? SourcePosition::kNotInlined
                              : inlining_id);
  }

  BranchHint GetBranchHint(FullDecoder* decoder) {
    WasmBranchHint hint =
        branch_hints_ ? branch_hints_->GetHintFor(decoder->pc_relative_offset())
                      : WasmBranchHint::kNoHint;
    switch (hint) {
      case WasmBranchHint::kNoHint:
        return BranchHint::kNone;
      case WasmBranchHint::kUnlikely:
        return BranchHint::kFalse;
      case WasmBranchHint::kLikely:
        return BranchHint::kTrue;
    }
  }

 private:
  bool inlining_enabled(FullDecoder* decoder) {
    return decoder->enabled_.has_inlining() || decoder->module_->is_wasm_gc;
  }

  bool should_inline(FullDecoder* decoder, int feedback_slot, int size) {
    if (v8_flags.liftoff) {
      if (inlining_decisions_ && inlining_decisions_->feedback_found()) {
        DCHECK_GT(inlining_decisions_->function_calls().size(), feedback_slot);
        // We should inline if at least one case for this feedback slot needs
        // to be inlined.
        for (InliningTree* tree :
             inlining_decisions_->function_calls()[feedback_slot]) {
          if (tree && tree->is_inlined()) return true;
        }
        return false;
      } else {
        return false;
      }
    } else {
      // We check the wasm feature here because we want the ability to force
      // inlining off in unit tests, whereas {inlining_enabled()} turns it on
      // for all WasmGC modules.
      return decoder->enabled_.has_inlining() &&
             size < no_liftoff_inlining_budget_ &&
             inlining_positions_->size() < InliningTree::kMaxInlinedCount;
    }
  }

  void set_inlining_decisions(InliningTree* inlining_decisions) {
    inlining_decisions_ = inlining_decisions;
  }

  BlockPhis* return_phis() { return return_phis_; }
  void set_inlining_id(uint8_t inlining_id) {
    DCHECK_NE(inlining_id, kNoInliningId);
    inlining_id_ = inlining_id;
  }
  void set_parent_position(SourcePosition position) {
    parent_position_ = position;
  }
  int no_liftoff_inlining_budget() { return no_liftoff_inlining_budget_; }
  void set_no_liftoff_inlining_budget(int no_liftoff_inlining_budget) {
    no_liftoff_inlining_budget_ = no_liftoff_inlining_budget;
  }

  V<WasmTrustedInstanceData> trusted_instance_data() {
    return instance_cache_.trusted_instance_data();
  }

 private:
  Mode mode_;
  ZoneAbslFlatHashMap<TSBlock*, BlockPhis> block_phis_;
  CompilationEnv* env_;
  // Only used for "top-level" instantiations, not for inlining.
  std::unique_ptr<InstanceCache> owned_instance_cache_;

  // The instance cache to use (may be owned or passed in).
  InstanceCache& instance_cache_;

  AssumptionsJournal* assumptions_;
  ZoneVector<WasmInliningPosition>* inlining_positions_;
  uint8_t inlining_id_ = kNoInliningId;
  ZoneVector<OpIndex> ssa_env_;
  bool did_bailout_ = false;
  compiler::NullCheckStrategy null_check_strategy_ =
      trap_handler::IsTrapHandlerEnabled() && V8_STATIC_ROOTS_BOOL
          ? compiler::NullCheckStrategy::kTrapHandler
          : compiler::NullCheckStrategy::kExplicit;
  int func_index_;
  const WireBytesStorage* wire_bytes_;
  const BranchHintMap* branch_hints_ = nullptr;
  InliningTree* inlining_decisions_ = nullptr;
  int feedback_slot_ = -1;
  // Inlining budget in case of --no-liftoff.
  int no_liftoff_inlining_budget_ = 0;

  /* Used for inlining modes */
  // Contains real parameters for this inlined function, including the instance.
  // Used only in StartFunction();
  base::Vector<OpIndex> real_parameters_;
  // The block where this function returns its values (passed by the caller).
  TSBlock* return_block_ = nullptr;
  // The return values and exception values for this function.
  // The caller will reconstruct each one with a Phi.
  BlockPhis* return_phis_ = nullptr;
  // The block where exceptions from this function are caught (passed by the
  // caller).
  TSBlock* return_catch_block_ = nullptr;
  // The position of the call that is being inlined.
  SourcePosition parent_position_;
  bool is_inlined_tail_call_ = false;
};

V8_EXPORT_PRIVATE bool BuildTSGraph(
    AccountingAllocator* allocator, CompilationEnv* env, WasmFeatures* detected,
    Graph& graph, const FunctionBody& func_body,
    const WireBytesStorage* wire_bytes, AssumptionsJournal* assumptions,
    ZoneVector<WasmInliningPosition>* inlining_positions, int func_index) {
  Zone zone(allocator, ZONE_NAME);
  WasmGraphBuilderBase::Assembler assembler(graph, graph, &zone);
  WasmFullDecoder<Decoder::FullValidationTag, TurboshaftGraphBuildingInterface>
      decoder(&zone, env->module, env->enabled_features, detected, func_body,
              &zone, env, assembler, assumptions, inlining_positions,
              func_index, wire_bytes);
  decoder.Decode();
  // Turboshaft runs with validation, but the function should already be
  // validated, so graph building must always succeed, unless we bailed out.
  DCHECK_IMPLIES(!decoder.ok(), decoder.interface().did_bailout());
  return decoder.ok();
}

#undef LOAD_IMMUTABLE_INSTANCE_FIELD
#undef LOAD_INSTANCE_FIELD
#undef LOAD_ROOT
#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::wasm
