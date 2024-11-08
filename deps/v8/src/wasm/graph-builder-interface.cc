// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/graph-builder-interface.h"

#include "src/base/vector.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/compiler/wasm-compiler.h"
#include "src/flags/flags.h"
#include "src/wasm/branch-hint-map.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/well-known-imports.h"

namespace v8::internal::wasm {

namespace {

// Expose {compiler::Node} opaquely as {wasm::TFNode}.
using TFNode = compiler::Node;
using LocalsAllocator = RecyclingZoneAllocator<TFNode*>;

class LocalsVector {
 public:
  LocalsVector(LocalsAllocator* allocator, size_t size)
      : allocator_(allocator), data_(allocator->allocate(size), size) {
    std::fill(data_.begin(), data_.end(), nullptr);
  }
  LocalsVector(const LocalsVector& other) V8_NOEXCEPT
      : allocator_(other.allocator_),
        data_(allocator_->allocate(other.size()), other.size()) {
    data_.OverwriteWith(other.data_);
  }
  LocalsVector(LocalsVector&& other) V8_NOEXCEPT
      : allocator_(other.allocator_),
        data_(other.data_.begin(), other.size()) {
    other.data_.Truncate(0);
  }
  ~LocalsVector() { Clear(); }

  LocalsVector& operator=(const LocalsVector& other) V8_NOEXCEPT {
    allocator_ = other.allocator_;
    if (data_.empty()) {
      data_ = base::Vector<TFNode*>(allocator_->allocate(other.size()),
                                    other.size());
    }
    data_.OverwriteWith(other.data_);
    return *this;
  }
  TFNode*& operator[](size_t index) { return data_[index]; }
  size_t size() const { return data_.size(); }

  void Clear() {
    if (size()) allocator_->deallocate(data_.begin(), size());
    data_.Truncate(0);
  }

 private:
  LocalsAllocator* allocator_ = nullptr;
  base::Vector<TFNode*> data_;
};

// An SsaEnv environment carries the current local variable renaming
// as well as the current effect and control dependency in the TF graph.
// It maintains a control state that tracks whether the environment
// is reachable, has reached a control end, or has been merged.
// It's encouraged to manage lifetime of SsaEnv by `ScopedSsaEnv` or
// `Control` (`block_env`, `false_env`, or `try_info->catch_env`).
struct SsaEnv : public ZoneObject {
  enum State { kUnreachable, kReached, kMerged };

  State state;
  TFNode* effect;
  TFNode* control;
  compiler::WasmInstanceCacheNodes instance_cache;
  LocalsVector locals;

  SsaEnv(LocalsAllocator* alloc, State state, TFNode* effect, TFNode* control,
         uint32_t locals_size)
      : state(state),
        effect(effect),
        control(control),
        locals(alloc, locals_size) {}

  SsaEnv(const SsaEnv& other) V8_NOEXCEPT = default;
  SsaEnv(SsaEnv&& other) V8_NOEXCEPT : state(other.state),
                                       effect(other.effect),
                                       control(other.control),
                                       instance_cache(other.instance_cache),
                                       locals(std::move(other.locals)) {
    other.Kill();
  }

  void Kill() {
    state = kUnreachable;
    control = nullptr;
    effect = nullptr;
    instance_cache = {};
    locals.Clear();
  }
  void SetNotMerged() {
    if (state == kMerged) state = kReached;
  }
};

class WasmGraphBuildingInterface {
 public:
  using ValidationTag = Decoder::NoValidationTag;
  using FullDecoder =
      WasmFullDecoder<ValidationTag, WasmGraphBuildingInterface>;
  using CheckForNull = compiler::CheckForNull;
  static constexpr bool kUsesPoppedArgs = true;

  struct Value : public ValueBase<ValidationTag> {
    TFNode* node = nullptr;

    template <typename... Args>
    explicit Value(Args&&... args) V8_NOEXCEPT
        : ValueBase(std::forward<Args>(args)...) {}
  };
  using ValueVector = base::SmallVector<Value, 8>;
  using NodeVector = base::SmallVector<TFNode*, 8>;

  struct TryInfo : public ZoneObject {
    SsaEnv* catch_env;
    TFNode* exception = nullptr;

    MOVE_ONLY_NO_DEFAULT_CONSTRUCTOR(TryInfo);

    explicit TryInfo(SsaEnv* c) : catch_env(c) {}
  };

  struct Control : public ControlBase<Value, ValidationTag> {
    SsaEnv* merge_env = nullptr;  // merge environment for the construct.
    SsaEnv* false_env = nullptr;  // false environment (only for if).
    SsaEnv* block_env = nullptr;  // environment that dies with this block.
    TryInfo* try_info = nullptr;  // information about try statements.
    int32_t previous_catch = -1;  // previous Control with a catch.
    bool loop_innermost = false;  // whether this loop can be innermost.
    BitVector* loop_assignments = nullptr;  // locals assigned in this loop.
    TFNode* loop_node = nullptr;            // loop header of this loop.

    template <typename... Args>
    explicit Control(Args&&... args) V8_NOEXCEPT
        : ControlBase(std::forward<Args>(args)...) {}
    Control(Control&& other) V8_NOEXCEPT
        : ControlBase(std::move(other)),
          merge_env(other.merge_env),
          false_env(other.false_env),
          block_env(other.block_env),
          try_info(other.try_info),
          previous_catch(other.previous_catch),
          loop_innermost(other.loop_innermost),
          loop_assignments(other.loop_assignments),
          loop_node(other.loop_node) {
      // The `control_` vector in WasmFullDecoder calls destructor of this when
      // growing capacity. Nullify these pointers to avoid destroying
      // environments before used.
      other.false_env = nullptr;
      other.block_env = nullptr;
      other.try_info = nullptr;
    }
    ~Control() {
      if (false_env) false_env->Kill();
      if (block_env) block_env->Kill();
      if (try_info) try_info->catch_env->Kill();
    }
    DISALLOW_IMPLICIT_CONSTRUCTORS(Control);
  };

  WasmGraphBuildingInterface(compiler::WasmGraphBuilder* builder,
                             int func_index, AssumptionsJournal* assumptions,
                             InlinedStatus inlined_status, Zone* zone)
      : locals_allocator_(zone),
        builder_(builder),
        func_index_(func_index),
        assumptions_(assumptions),
        inlined_status_(inlined_status) {}

  void StartFunction(FullDecoder* decoder) {
    // Get the branch hints map and type feedback for this function (if
    // available).
    if (decoder->module_) {
      auto branch_hints_it = decoder->module_->branch_hints.find(func_index_);
      if (branch_hints_it != decoder->module_->branch_hints.end()) {
        branch_hints_ = &branch_hints_it->second;
      }
      const TypeFeedbackStorage& feedbacks = decoder->module_->type_feedback;
      base::SharedMutexGuard<base::kShared> mutex_guard(&feedbacks.mutex);
      auto feedback = feedbacks.feedback_for_function.find(func_index_);
      if (feedback != feedbacks.feedback_for_function.end()) {
        // This creates a copy of the vector, which is cheaper than holding on
        // to the mutex throughout graph building.
        type_feedback_ = feedback->second.feedback_vector;
        // Preallocate space for storing call counts to save Zone memory.
        int total_calls = 0;
        for (size_t i = 0; i < type_feedback_.size(); i++) {
          total_calls += type_feedback_[i].num_cases();
        }
        builder_->ReserveCallCounts(static_cast<size_t>(total_calls));
        // We need to keep the feedback in the module to inline later. However,
        // this means we are stuck with it forever.
        // TODO(jkummerow): Reconsider our options here.
      }
    }
    // The first '+ 1' is needed by TF Start node, the second '+ 1' is for the
    // instance parameter.
    builder_->Start(static_cast<int>(decoder->sig_->parameter_count() + 1 + 1));
    uint32_t num_locals = decoder->num_locals();
    SsaEnv* ssa_env = decoder->zone()->New<SsaEnv>(
        &locals_allocator_, SsaEnv::kReached, effect(), control(), num_locals);
    SetEnv(ssa_env);

    // Initialize local variables. Parameters are shifted by 1 because of the
    // the instance parameter.
    uint32_t index = 0;
    for (; index < decoder->sig_->parameter_count(); ++index) {
      ssa_env->locals[index] = builder_->SetType(
          builder_->Param(index + 1), decoder->sig_->GetParam(index));
    }
    while (index < num_locals) {
      ValueType type = decoder->local_type(index);
      TFNode* node;
      if (!type.is_defaultable()) {
        DCHECK(type.is_reference());
        // TODO(jkummerow): Consider using "the hole" instead, to make any
        // illegal uses more obvious.
        node = builder_->SetType(builder_->RefNull(type), type);
      } else {
        node = builder_->SetType(builder_->DefaultValue(type), type);
      }
      while (index < num_locals && decoder->local_type(index) == type) {
        // Do a whole run of like-typed locals at a time.
        ssa_env->locals[index++] = node;
      }
    }

    size_t num_memories =
        decoder->module_ == nullptr ? 0 : decoder->module_->memories.size();
    if (num_memories == 1) {
      builder_->set_cached_memory_index(0);
    } else if (num_memories > 1) {
      int first_used_mem_index = FindFirstUsedMemoryIndex(
          base::VectorOf(decoder->start(), decoder->end() - decoder->start()),
          decoder->zone());
      if (first_used_mem_index >= 0) {
        builder_->set_cached_memory_index(first_used_mem_index);
      }
    }
    LoadInstanceCacheIntoSsa(ssa_env);

    if (v8_flags.trace_wasm && inlined_status_ == kRegularFunction) {
      builder_->TraceFunctionEntry(decoder->position());
    }
  }

  // Load the instance cache entries into the SSA Environment.
  void LoadInstanceCacheIntoSsa(SsaEnv* ssa_env) {
    builder_->InitInstanceCache(&ssa_env->instance_cache);
  }

  // Reload the instance cache entries into the SSA Environment, if memory can
  // actually grow.
  void ReloadInstanceCacheIntoSsa(SsaEnv* ssa_env, const WasmModule* module) {
    if (!builder_->has_cached_memory()) return;
    const WasmMemory* cached_memory =
        &module->memories[builder_->cached_memory_index()];
    if (cached_memory->initial_pages == cached_memory->maximum_pages) return;
    LoadInstanceCacheIntoSsa(ssa_env);
  }

  void StartFunctionBody(FullDecoder* decoder, Control* block) {}

  void FinishFunction(FullDecoder* decoder) {
    if (inlining_enabled(decoder)) {
      DCHECK_EQ(feedback_instruction_index_, type_feedback_.size());
    }
    if (inlined_status_ == kRegularFunction) {
      builder_->PatchInStackCheckIfNeeded();
    }
  }

  void OnFirstError(FullDecoder*) {}

  void NextInstruction(FullDecoder*, WasmOpcode) {}

  void Block(FullDecoder* decoder, Control* block) {
    // The branch environment is the outer environment.
    block->merge_env = ssa_env_;
    SetEnv(Steal(decoder->zone(), ssa_env_));
    block->block_env = ssa_env_;
  }

  void Loop(FullDecoder* decoder, Control* block) {
    // This is the merge environment at the beginning of the loop.
    SsaEnv* merge_env = Steal(decoder->zone(), ssa_env_);
    block->merge_env = block->block_env = merge_env;
    SetEnv(merge_env);

    ssa_env_->state = SsaEnv::kMerged;

    TFNode* loop_node = builder_->Loop(control());

    builder_->SetControl(loop_node);
    decoder->control_at(0)->loop_node = loop_node;

    TFNode* effect_inputs[] = {effect(), control()};
    builder_->SetEffect(builder_->EffectPhi(1, effect_inputs));
    builder_->TerminateLoop(effect(), control());
    // Doing a preprocessing pass to analyze loop assignments seems to pay off
    // compared to reallocating Nodes when rearranging Phis in Goto.
    bool can_be_innermost = false;
    BitVector* assigned = WasmDecoder<ValidationTag>::AnalyzeLoopAssignment(
        decoder, decoder->pc(), decoder->num_locals(), decoder->zone(),
        &can_be_innermost);
    if (decoder->failed()) return;
    int instance_cache_index = decoder->num_locals();
    // If the cached memory is shared, the stack guard might reallocate the
    // backing store. We have to assume the instance cache will be updated.
    bool cached_mem_is_shared =
        builder_->has_cached_memory() &&
        decoder->module_->memories[builder_->cached_memory_index()].is_shared;
    if (cached_mem_is_shared) assigned->Add(instance_cache_index);
    DCHECK_NOT_NULL(assigned);
    decoder->control_at(0)->loop_assignments = assigned;

    if (emit_loop_exits()) {
      uint32_t nesting_depth = 0;
      for (uint32_t depth = 1; depth < decoder->control_depth(); depth++) {
        if (decoder->control_at(depth)->is_loop()) {
          nesting_depth++;
        }
      }
      loop_infos_.emplace_back(loop_node, nesting_depth, can_be_innermost);
      // Only innermost loops can be unrolled. We can avoid allocating
      // unnecessary nodes if this loop can not be innermost.
      decoder->control_at(0)->loop_innermost = can_be_innermost;
    }

    // Only introduce phis for variables assigned in this loop.
    for (int i = decoder->num_locals() - 1; i >= 0; i--) {
      if (!assigned->Contains(i)) continue;
      TFNode* inputs[] = {ssa_env_->locals[i], control()};
      ssa_env_->locals[i] =
          builder_->SetType(builder_->Phi(decoder->local_type(i), 1, inputs),
                            decoder->local_type(i));
    }
    // Introduce phis for instance cache pointers if necessary.
    if (assigned->Contains(instance_cache_index)) {
      builder_->PrepareInstanceCacheForLoop(&ssa_env_->instance_cache,
                                            control());
    }

    // Now we setup a new environment for the inside of the loop.
    // TODO(choongwoo): Clear locals of the following SsaEnv after use.
    SetEnv(Split(decoder->zone(), ssa_env_));
    builder_->StackCheck(
        cached_mem_is_shared ? &ssa_env_->instance_cache : nullptr,
        decoder->position());
    ssa_env_->SetNotMerged();

    // Wrap input merge into phis.
    for (uint32_t i = 0; i < block->start_merge.arity; ++i) {
      Value& val = block->start_merge[i];
      TFNode* inputs[] = {val.node, block->merge_env->control};
      SetAndTypeNode(&val, builder_->Phi(val.type, 1, inputs));
    }
  }

  void Try(FullDecoder* decoder, Control* block) {
    SsaEnv* outer_env = ssa_env_;
    SsaEnv* catch_env = Steal(decoder->zone(), outer_env);
    // Steal catch_env to make catch_env unreachable and clear locals.
    // The unreachable catch_env will create and copy locals in `Goto`.
    SsaEnv* try_env = Steal(decoder->zone(), catch_env);
    SetEnv(try_env);
    TryInfo* try_info = decoder->zone()->New<TryInfo>(catch_env);
    block->merge_env = outer_env;
    block->try_info = try_info;
    block->block_env = try_env;
  }

  void If(FullDecoder* decoder, const Value& cond, Control* if_block) {
    WasmBranchHint hint = WasmBranchHint::kNoHint;
    if (branch_hints_) {
      hint = branch_hints_->GetHintFor(decoder->pc_relative_offset());
    }
    auto [if_true, if_false] = hint == WasmBranchHint::kUnlikely
                                   ? builder_->BranchExpectFalse(cond.node)
                               : hint == WasmBranchHint::kLikely
                                   ? builder_->BranchExpectTrue(cond.node)
                                   : builder_->BranchNoHint(cond.node);
    SsaEnv* merge_env = ssa_env_;
    SsaEnv* false_env = Split(decoder->zone(), ssa_env_);
    false_env->control = if_false;
    SsaEnv* true_env = Steal(decoder->zone(), ssa_env_);
    true_env->control = if_true;
    if_block->merge_env = merge_env;
    if_block->false_env = false_env;
    if_block->block_env = true_env;
    SetEnv(true_env);
  }

  void FallThruTo(FullDecoder* decoder, Control* c) {
    DCHECK(!c->is_loop());
    MergeValuesInto(decoder, c, &c->end_merge);
  }

  void PopControl(FullDecoder* decoder, Control* block) {
    // A loop just continues with the end environment. There is no merge.
    // However, if loop unrolling is enabled, we must create a loop exit and
    // wrap the fallthru values on the stack.
    if (block->is_loop()) {
      if (emit_loop_exits() && block->reachable() && block->loop_innermost) {
        BuildLoopExits(decoder, block);
        WrapLocalsAtLoopExit(decoder, block);
        uint32_t arity = block->end_merge.arity;
        if (arity > 0) {
          Value* stack_base = decoder->stack_value(arity);
          for (uint32_t i = 0; i < arity; i++) {
            Value* val = stack_base + i;
            SetAndTypeNode(val,
                           builder_->LoopExitValue(
                               val->node, val->type.machine_representation()));
          }
        }
      }
      return;
    }
    // Any other block falls through to the parent block.
    if (block->reachable()) FallThruTo(decoder, block);
    if (block->is_onearmed_if()) {
      // Merge the else branch into the end merge.
      SetEnv(block->false_env);
      DCHECK_EQ(block->start_merge.arity, block->end_merge.arity);
      Value* values =
          block->start_merge.arity > 0 ? &block->start_merge[0] : nullptr;
      MergeValuesInto(decoder, block, &block->end_merge, values);
    }
    // Now continue with the merged environment.
    SetEnv(block->merge_env);
  }

  void UnOp(FullDecoder* decoder, WasmOpcode opcode, const Value& value,
            Value* result) {
    SetAndTypeNode(result, builder_->Unop(opcode, value.node, value.type,
                                          decoder->position()));
  }

  void BinOp(FullDecoder* decoder, WasmOpcode opcode, const Value& lhs,
             const Value& rhs, Value* result) {
    TFNode* node =
        builder_->Binop(opcode, lhs.node, rhs.node, decoder->position());
    if (result) SetAndTypeNode(result, node);
  }

  void TraceInstruction(FullDecoder* decoder, uint32_t markid) {
    builder_->TraceInstruction(markid);
  }

  void I32Const(FullDecoder* decoder, Value* result, int32_t value) {
    SetAndTypeNode(result, builder_->Int32Constant(value));
  }

  void I64Const(FullDecoder* decoder, Value* result, int64_t value) {
    SetAndTypeNode(result, builder_->Int64Constant(value));
  }

  void F32Const(FullDecoder* decoder, Value* result, float value) {
    SetAndTypeNode(result, builder_->Float32Constant(value));
  }

  void F64Const(FullDecoder* decoder, Value* result, double value) {
    SetAndTypeNode(result, builder_->Float64Constant(value));
  }

  void S128Const(FullDecoder* decoder, const Simd128Immediate& imm,
                 Value* result) {
    SetAndTypeNode(result, builder_->Simd128Constant(imm.value));
  }

  void RefNull(FullDecoder* decoder, ValueType type, Value* result) {
    SetAndTypeNode(result, builder_->RefNull(type));
  }

  void RefFunc(FullDecoder* decoder, uint32_t function_index, Value* result) {
    SetAndTypeNode(result, builder_->RefFunc(function_index));
  }

  void RefAsNonNull(FullDecoder* decoder, const Value& arg, Value* result) {
    TFNode* cast_node =
        builder_->AssertNotNull(arg.node, arg.type, decoder->position());
    SetAndTypeNode(result, cast_node);
  }

  void Drop(FullDecoder* decoder) {}

  void LocalGet(FullDecoder* decoder, Value* result,
                const IndexImmediate& imm) {
    result->node = ssa_env_->locals[imm.index];
  }

  void LocalSet(FullDecoder* decoder, const Value& value,
                const IndexImmediate& imm) {
    ssa_env_->locals[imm.index] = value.node;
  }

  void LocalTee(FullDecoder* decoder, const Value& value, Value* result,
                const IndexImmediate& imm) {
    result->node = value.node;
    ssa_env_->locals[imm.index] = value.node;
  }

  void GlobalGet(FullDecoder* decoder, Value* result,
                 const GlobalIndexImmediate& imm) {
    SetAndTypeNode(result, builder_->GlobalGet(imm.index));
  }

  void GlobalSet(FullDecoder* decoder, const Value& value,
                 const GlobalIndexImmediate& imm) {
    builder_->GlobalSet(imm.index, value.node);
  }

  void TableGet(FullDecoder* decoder, const Value& index, Value* result,
                const TableIndexImmediate& imm) {
    SetAndTypeNode(
        result, builder_->TableGet(imm.index, index.node, decoder->position()));
  }

  void TableSet(FullDecoder* decoder, const Value& index, const Value& value,
                const TableIndexImmediate& imm) {
    builder_->TableSet(imm.index, index.node, value.node, decoder->position());
  }

  void Trap(FullDecoder* decoder, TrapReason reason) {
    builder_->Trap(reason, decoder->position());
  }

  void AssertNullTypecheck(FullDecoder* decoder, const Value& obj,
                           Value* result) {
    builder_->TrapIfFalse(wasm::TrapReason::kTrapIllegalCast,
                          builder_->IsNull(obj.node, obj.type),
                          decoder->position());
    Forward(decoder, obj, result);
  }

  void AssertNotNullTypecheck(FullDecoder* decoder, const Value& obj,
                              Value* result) {
    SetAndTypeNode(
        result, builder_->AssertNotNull(obj.node, obj.type, decoder->position(),
                                        TrapReason::kTrapIllegalCast));
  }

  void NopForTestingUnsupportedInLiftoff(FullDecoder* decoder) {}

  void Select(FullDecoder* decoder, const Value& cond, const Value& fval,
              const Value& tval, Value* result) {
    SetAndTypeNode(result, builder_->Select(cond.node, tval.node, fval.node,
                                            result->type));
  }

  ValueVector CopyStackValues(FullDecoder* decoder, uint32_t count,
                              uint32_t drop_values) {
    Value* stack_base =
        count > 0 ? decoder->stack_value(count + drop_values) : nullptr;
    ValueVector stack_values(count);
    for (uint32_t i = 0; i < count; i++) {
      stack_values[i] = stack_base[i];
    }
    return stack_values;
  }

  void DoReturn(FullDecoder* decoder, uint32_t drop_values) {
    uint32_t ret_count = static_cast<uint32_t>(decoder->sig_->return_count());
    NodeVector values(ret_count);
    SsaEnv* internal_env = ssa_env_;
    SsaEnv* exit_env = nullptr;
    if (emit_loop_exits()) {
      exit_env = Split(decoder->zone(), ssa_env_);
      SetEnv(exit_env);
      auto stack_values = CopyStackValues(decoder, ret_count, drop_values);
      BuildNestedLoopExits(decoder, decoder->control_depth() - 1, false,
                           stack_values);
      GetNodes(values.begin(), base::VectorOf(stack_values));
    } else {
      Value* stack_base = ret_count == 0
                              ? nullptr
                              : decoder->stack_value(ret_count + drop_values);
      GetNodes(values.begin(), stack_base, ret_count);
    }
    if (v8_flags.trace_wasm && inlined_status_ == kRegularFunction) {
      builder_->TraceFunctionExit(base::VectorOf(values), decoder->position());
    }
    builder_->Return(base::VectorOf(values));
    if (exit_env) exit_env->Kill();
    SetEnv(internal_env);
  }

  void BrOrRet(FullDecoder* decoder, uint32_t depth, uint32_t drop_values = 0) {
    if (depth == decoder->control_depth() - 1) {
      DoReturn(decoder, drop_values);
    } else {
      Control* target = decoder->control_at(depth);
      if (emit_loop_exits()) {
        ScopedSsaEnv exit_env(this, Split(decoder->zone(), ssa_env_));
        uint32_t value_count = target->br_merge()->arity;
        auto stack_values = CopyStackValues(decoder, value_count, drop_values);
        BuildNestedLoopExits(decoder, depth, true, stack_values);
        MergeValuesInto(decoder, target, target->br_merge(),
                        stack_values.data());
      } else {
        MergeValuesInto(decoder, target, target->br_merge(), drop_values);
      }
    }
  }

  void BrIf(FullDecoder* decoder, const Value& cond, uint32_t depth) {
    SsaEnv* fenv = ssa_env_;
    SsaEnv* tenv = Split(decoder->zone(), fenv);
    fenv->SetNotMerged();
    WasmBranchHint hint = WasmBranchHint::kNoHint;
    if (branch_hints_) {
      hint = branch_hints_->GetHintFor(decoder->pc_relative_offset());
    }
    switch (hint) {
      case WasmBranchHint::kNoHint:
        std::tie(tenv->control, fenv->control) =
            builder_->BranchNoHint(cond.node);
        break;
      case WasmBranchHint::kUnlikely:
        std::tie(tenv->control, fenv->control) =
            builder_->BranchExpectFalse(cond.node);
        break;
      case WasmBranchHint::kLikely:
        std::tie(tenv->control, fenv->control) =
            builder_->BranchExpectTrue(cond.node);
        break;
    }
    builder_->SetControl(fenv->control);
    ScopedSsaEnv scoped_env(this, tenv);
    BrOrRet(decoder, depth);
  }

  void BrTable(FullDecoder* decoder, const BranchTableImmediate& imm,
               const Value& key) {
    if (imm.table_count == 0) {
      // Only a default target. Do the equivalent of br.
      uint32_t target = BranchTableIterator<ValidationTag>(decoder, imm).next();
      BrOrRet(decoder, target);
      return;
    }

    // Build branches to the various blocks based on the table.
    TFNode* sw = builder_->Switch(imm.table_count + 1, key.node);

    BranchTableIterator<ValidationTag> iterator(decoder, imm);
    while (iterator.has_next()) {
      uint32_t i = iterator.cur_index();
      uint32_t target = iterator.next();
      ScopedSsaEnv env(this, Split(decoder->zone(), ssa_env_));
      builder_->SetControl(i == imm.table_count ? builder_->IfDefault(sw)
                                                : builder_->IfValue(i, sw));
      BrOrRet(decoder, target);
    }
    DCHECK(decoder->ok());
  }

  void Else(FullDecoder* decoder, Control* if_block) {
    if (if_block->reachable()) {
      // Merge the if branch into the end merge.
      MergeValuesInto(decoder, if_block, &if_block->end_merge);
    }
    SetEnv(if_block->false_env);
  }

  void LoadMem(FullDecoder* decoder, LoadType type,
               const MemoryAccessImmediate& imm, const Value& index,
               Value* result) {
    SetAndTypeNode(result,
                   builder_->LoadMem(imm.memory, type.value_type(),
                                     type.mem_type(), index.node, imm.offset,
                                     imm.alignment, decoder->position()));
  }

  void LoadTransform(FullDecoder* decoder, LoadType type,
                     LoadTransformationKind transform,
                     const MemoryAccessImmediate& imm, const Value& index,
                     Value* result) {
    SetAndTypeNode(result, builder_->LoadTransform(
                               imm.memory, type.value_type(), type.mem_type(),
                               transform, index.node, imm.offset, imm.alignment,
                               decoder->position()));
  }

  void LoadLane(FullDecoder* decoder, LoadType type, const Value& value,
                const Value& index, const MemoryAccessImmediate& imm,
                const uint8_t laneidx, Value* result) {
    SetAndTypeNode(result, builder_->LoadLane(
                               imm.memory, type.value_type(), type.mem_type(),
                               value.node, index.node, imm.offset,
                               imm.alignment, laneidx, decoder->position()));
  }

  void StoreMem(FullDecoder* decoder, StoreType type,
                const MemoryAccessImmediate& imm, const Value& index,
                const Value& value) {
    builder_->StoreMem(imm.memory, type.mem_rep(), index.node, imm.offset,
                       imm.alignment, value.node, decoder->position(),
                       type.value_type());
  }

  void StoreLane(FullDecoder* decoder, StoreType type,
                 const MemoryAccessImmediate& imm, const Value& index,
                 const Value& value, const uint8_t laneidx) {
    builder_->StoreLane(imm.memory, type.mem_rep(), index.node, imm.offset,
                        imm.alignment, value.node, laneidx, decoder->position(),
                        type.value_type());
  }

  void CurrentMemoryPages(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                          Value* result) {
    SetAndTypeNode(result, builder_->CurrentMemoryPages(imm.memory));
  }

  void MemoryGrow(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value& value, Value* result) {
    SetAndTypeNode(result, builder_->MemoryGrow(imm.memory, value.node));
    // Always reload the instance cache after growing memory.
    ReloadInstanceCacheIntoSsa(ssa_env_, decoder->module_);
  }

  TFNode* ExternRefToString(FullDecoder* decoder, const Value value,
                            bool null_succeeds = false) {
    wasm::ValueType target_type =
        null_succeeds ? kWasmRefNullExternString : kWasmRefExternString;
    WasmTypeCheckConfig config{value.type, target_type};
    TFNode* string =
        builder_->RefCastAbstract(value.node, config, decoder->position());
    TFNode* rename = builder_->TypeGuard(string, target_type);
    return builder_->SetType(rename, target_type);
  }

  bool HandleWellKnownImport(FullDecoder* decoder, uint32_t index,
                             const Value args[], Value returns[]) {
    if (!decoder->module_) return false;  // Only needed for tests.
    if (index >= decoder->module_->num_imported_functions) return false;
    const WellKnownImportsList& well_known_imports =
        decoder->module_->type_feedback.well_known_imports;
    using WKI = WellKnownImport;
    WKI import = well_known_imports.get(index);
    TFNode* result = nullptr;
    switch (import) {
      case WKI::kUninstantiated:
      case WKI::kGeneric:
      case WKI::kLinkError:
        return false;

      // JS String Builtins proposal.
      case WKI::kStringCast:
        result = ExternRefToString(decoder, args[0]);
        decoder->detected_->add_imported_strings();
        break;
      case WKI::kStringTest: {
        WasmTypeCheckConfig config{args[0].type, kWasmRefExternString};
        result = builder_->RefTestAbstract(args[0].node, config);
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringCharCodeAt: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        TFNode* view = builder_->StringAsWtf16(
            string, compiler::kWithoutNullCheck, decoder->position());
        builder_->SetType(view, kWasmRefExternString);
        result = builder_->StringViewWtf16GetCodeUnit(
            view, compiler::kWithoutNullCheck, args[1].node,
            decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringCodePointAt: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        TFNode* view = builder_->StringAsWtf16(
            string, compiler::kWithoutNullCheck, decoder->position());
        builder_->SetType(view, kWasmRefExternString);
        result = builder_->StringCodePointAt(view, compiler::kWithoutNullCheck,
                                             args[1].node, decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringCompare: {
        TFNode* a_string = ExternRefToString(decoder, args[0]);
        TFNode* b_string = ExternRefToString(decoder, args[1]);
        result = builder_->StringCompare(a_string, compiler::kWithoutNullCheck,
                                         b_string, compiler::kWithoutNullCheck,
                                         decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringConcat: {
        TFNode* head_string = ExternRefToString(decoder, args[0]);
        TFNode* tail_string = ExternRefToString(decoder, args[1]);
        result = builder_->StringConcat(
            head_string, compiler::kWithoutNullCheck, tail_string,
            compiler::kWithoutNullCheck, decoder->position());
        builder_->SetType(result, kWasmRefExternString);
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringEquals: {
        // Using nullable type guards here because this instruction needs to
        // handle {null} without trapping.
        static constexpr bool kNullSucceeds = true;
        TFNode* a_string = ExternRefToString(decoder, args[0], kNullSucceeds);
        TFNode* b_string = ExternRefToString(decoder, args[1], kNullSucceeds);
        result = builder_->StringEqual(a_string, args[0].type, b_string,
                                       args[1].type, decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringFromCharCode:
        result = builder_->StringFromCharCode(args[0].node);
        builder_->SetType(result, kWasmRefExternString);
        decoder->detected_->add_imported_strings();
        break;
      case WKI::kStringFromCodePoint:
        result = builder_->StringFromCodePoint(args[0].node);
        builder_->SetType(result, kWasmRefExternString);
        decoder->detected_->add_imported_strings();
        break;
      case WKI::kStringFromWtf16Array:
        result = builder_->StringNewWtf16Array(
            args[0].node, NullCheckFor(args[0].type), args[1].node,
            args[2].node, decoder->position());
        builder_->SetType(result, kWasmRefExternString);
        decoder->detected_->add_imported_strings();
        break;
      case WKI::kStringFromUtf8Array:
        result = builder_->StringNewWtf8Array(
            unibrow::Utf8Variant::kLossyUtf8, args[0].node,
            NullCheckFor(args[0].type), args[1].node, args[2].node,
            decoder->position());
        builder_->SetType(result, kWasmRefExternString);
        decoder->detected_->add_imported_strings();
        break;
      case WKI::kStringIntoUtf8Array: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        result = builder_->StringEncodeWtf8Array(
            unibrow::Utf8Variant::kLossyUtf8, string,
            compiler::kWithoutNullCheck, args[1].node,
            NullCheckFor(args[1].type), args[2].node, decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringToUtf8Array: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        result = builder_->StringToUtf8Array(
            string, compiler::kWithoutNullCheck, decoder->position());
        builder_->SetType(result, returns[0].type);
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringLength: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        result = builder_->StringMeasureWtf16(
            string, compiler::kWithoutNullCheck, decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringMeasureUtf8: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        result = builder_->StringMeasureWtf8(string, compiler::kWithNullCheck,
                                             decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringSubstring: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        TFNode* view = builder_->StringAsWtf16(
            string, compiler::kWithoutNullCheck, decoder->position());
        builder_->SetType(view, kWasmRefExternString);
        result = builder_->StringViewWtf16Slice(
            view, compiler::kWithoutNullCheck, args[1].node, args[2].node,
            decoder->position());
        builder_->SetType(result, kWasmRefExternString);
        decoder->detected_->add_imported_strings();
        break;
      }
      case WKI::kStringToWtf16Array: {
        TFNode* string = ExternRefToString(decoder, args[0]);
        result = builder_->StringEncodeWtf16Array(
            string, compiler::kWithoutNullCheck, args[1].node,
            NullCheckFor(args[1].type), args[2].node, decoder->position());
        decoder->detected_->add_imported_strings();
        break;
      }

      // Other string-related imports.
      case WKI::kDoubleToString:
        result = builder_->WellKnown_DoubleToString(args[0].node);
        break;
      case WKI::kIntToString:
        result = builder_->WellKnown_IntToString(args[0].node, args[1].node);
        break;
      case WKI::kParseFloat:
        result = builder_->WellKnown_ParseFloat(args[0].node,
                                                NullCheckFor(args[0].type));
        decoder->detected_->add_stringref();
        break;
      case WKI::kStringIndexOf:
        result = builder_->WellKnown_StringIndexOf(
            args[0].node, args[1].node, args[2].node,
            NullCheckFor(args[0].type), NullCheckFor(args[1].type));
        decoder->detected_->add_stringref();
        break;
      case WKI::kStringToLocaleLowerCaseStringref:
        // Temporarily ignored because of bugs (v8:13977, v8:13985).
        // TODO(jkummerow): Fix and re-enable.
        return false;
        // result = builder_->WellKnown_StringToLocaleLowerCaseStringref(
        //     args[0].node, args[1].node, NullCheckFor(args[0].type));
        // decoder->detected_->add_stringref();
        // break;
      case WKI::kStringToLowerCaseStringref:
        result = builder_->WellKnown_StringToLowerCaseStringref(
            args[0].node, NullCheckFor(args[0].type));
        decoder->detected_->add_stringref();
        break;
        // Not implementing for Turbofan.
      case WKI::kStringIndexOfImported:
      case WKI::kStringToLowerCaseImported:
      case WKI::kDataViewGetBigInt64:
      case WKI::kDataViewGetBigUint64:
      case WKI::kDataViewGetFloat32:
      case WKI::kDataViewGetFloat64:
      case WKI::kDataViewGetInt8:
      case WKI::kDataViewGetInt16:
      case WKI::kDataViewGetInt32:
      case WKI::kDataViewGetUint8:
      case WKI::kDataViewGetUint16:
      case WKI::kDataViewGetUint32:
      case WKI::kDataViewSetBigInt64:
      case WKI::kDataViewSetBigUint64:
      case WKI::kDataViewSetFloat32:
      case WKI::kDataViewSetFloat64:
      case WKI::kDataViewSetInt8:
      case WKI::kDataViewSetInt16:
      case WKI::kDataViewSetInt32:
      case WKI::kDataViewSetUint8:
      case WKI::kDataViewSetUint16:
      case WKI::kDataViewSetUint32:
      case WKI::kDataViewByteLength:
      case WKI::kFastAPICall:
        return false;
    }
    if (v8_flags.trace_wasm_inlining) {
      PrintF("[function %d: call to %d is well-known %s]\n", func_index_, index,
             WellKnownImportName(import));
    }
    assumptions_->RecordAssumption(index, import);
    SetAndTypeNode(&returns[0], result);
    // The decoder assumes that any call might throw, so if we are in a try
    // block, it marks the associated catch block as reachable, and will
    // later ask the graph builder to build the catch block's graph.
    // However, we just replaced the call with a sequence that doesn't throw,
    // which might make the catch block unreachable as far as the graph builder
    // is concerned, which would violate assumptions when trying to build a
    // graph for it. So we insert a fake branch to the catch block to make it
    // reachable. Later phases will optimize this out.
    if (decoder->current_catch() != -1) {
      TryInfo* try_info = current_try_info(decoder);
      if (try_info->catch_env->state == SsaEnv::kUnreachable) {
        auto [true_cont, false_cont] =
            builder_->BranchExpectTrue(builder_->Int32Constant(1));
        SsaEnv* success_env = Steal(decoder->zone(), ssa_env_);
        success_env->control = true_cont;

        SsaEnv* exception_env = Split(decoder->zone(), success_env);
        exception_env->control = false_cont;

        ScopedSsaEnv scoped_env(this, exception_env, success_env);

        if (emit_loop_exits()) {
          ValueVector stack_values;
          uint32_t depth = decoder->control_depth_of_current_catch();
          BuildNestedLoopExits(decoder, depth, true, stack_values);
        }
        Goto(decoder, try_info->catch_env);
        try_info->exception = builder_->Int32Constant(1);
      }
    }
    return true;
  }

  void CallDirect(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[], Value returns[]) {
    int maybe_call_count = -1;
    if (inlining_enabled(decoder) && !type_feedback_.empty()) {
      const CallSiteFeedback& feedback = next_call_feedback();
      DCHECK_EQ(feedback.num_cases(), 1);
      maybe_call_count = feedback.call_count(0);
    }
    // This must happen after the {next_call_feedback()} call.
    if (HandleWellKnownImport(decoder, imm.index, args, returns)) return;

    DoCall(decoder, CallInfo::CallDirect(imm.index, maybe_call_count), imm.sig,
           args, returns);
  }

  void ReturnCall(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[]) {
    int maybe_call_count = -1;
    if (inlining_enabled(decoder) && !type_feedback_.empty()) {
      const CallSiteFeedback& feedback = next_call_feedback();
      DCHECK_EQ(feedback.num_cases(), 1);
      maybe_call_count = feedback.call_count(0);
    }
    DoReturnCall(decoder, CallInfo::CallDirect(imm.index, maybe_call_count),
                 imm.sig, args);
  }

  void CallIndirect(FullDecoder* decoder, const Value& index,
                    const CallIndirectImmediate& imm, const Value args[],
                    Value returns[]) {
    DoCall(
        decoder,
        CallInfo::CallIndirect(index, imm.table_imm.index, imm.sig_imm.index),
        imm.sig, args, returns);
  }

  void ReturnCallIndirect(FullDecoder* decoder, const Value& index,
                          const CallIndirectImmediate& imm,
                          const Value args[]) {
    DoReturnCall(
        decoder,
        CallInfo::CallIndirect(index, imm.table_imm.index, imm.sig_imm.index),
        imm.sig, args);
  }

  void CallRef(FullDecoder* decoder, const Value& func_ref,
               const FunctionSig* sig, const Value args[], Value returns[]) {
    const CallSiteFeedback* feedback = nullptr;
    if (inlining_enabled(decoder) && !type_feedback_.empty()) {
      feedback = &next_call_feedback();
    }
    if (feedback == nullptr || feedback->num_cases() == 0) {
      DoCall(decoder, CallInfo::CallRef(func_ref, NullCheckFor(func_ref.type)),
             sig, args, returns);
      return;
    }

    // Check for equality against a function at a specific index, and if
    // successful, just emit a direct call.
    int num_cases = feedback->num_cases();
    std::vector<TFNode*> control_args;
    std::vector<TFNode*> effect_args;
    std::vector<Value*> returns_values;
    control_args.reserve(num_cases + 1);
    effect_args.reserve(num_cases + 2);
    returns_values.reserve(num_cases);
    for (int i = 0; i < num_cases; i++) {
      const uint32_t expected_function_index = feedback->function_index(i);

      if (v8_flags.trace_wasm_inlining) {
        PrintF("[function %d: call #%d: graph support for inlining #%d]\n",
               func_index_, feedback_instruction_index_ - 1,
               expected_function_index);
      }

      TFNode* success_control;
      TFNode* failure_control;
      builder_->CompareToFuncRefAtIndex(func_ref.node, expected_function_index,
                                        &success_control, &failure_control,
                                        i == num_cases - 1);
      TFNode* initial_effect = effect();

      builder_->SetControl(success_control);
      ssa_env_->control = success_control;
      Value* returns_direct =
          decoder->zone()->AllocateArray<Value>(sig->return_count());
      for (size_t i = 0; i < sig->return_count(); i++) {
        returns_direct[i].type = returns[i].type;
      }
      DoCall(decoder,
             CallInfo::CallDirect(expected_function_index,
                                  feedback->call_count(i)),
             sig, args, returns_direct);
      control_args.push_back(control());
      effect_args.push_back(effect());
      returns_values.push_back(returns_direct);

      builder_->SetEffectControl(initial_effect, failure_control);
      ssa_env_->effect = initial_effect;
      ssa_env_->control = failure_control;
    }
    Value* returns_ref =
        decoder->zone()->AllocateArray<Value>(sig->return_count());
    for (size_t i = 0; i < sig->return_count(); i++) {
      returns_ref[i].type = returns[i].type;
    }
    DoCall(decoder, CallInfo::CallRef(func_ref, NullCheckFor(func_ref.type)),
           sig, args, returns_ref);

    control_args.push_back(control());
    TFNode* control = builder_->Merge(num_cases + 1, control_args.data());

    effect_args.push_back(effect());
    effect_args.push_back(control);
    TFNode* effect = builder_->EffectPhi(num_cases + 1, effect_args.data());

    ssa_env_->control = control;
    ssa_env_->effect = effect;
    builder_->SetEffectControl(effect, control);

    // Each of the {DoCall} helpers above has created a reload of the instance
    // cache nodes. Rather than merging all of them into a Phi, just
    // let them get DCE'ed and perform a single reload after the merge.
    ReloadInstanceCacheIntoSsa(ssa_env_, decoder->module_);

    for (uint32_t i = 0; i < sig->return_count(); i++) {
      std::vector<TFNode*> phi_args;
      phi_args.reserve(num_cases + 2);
      for (int j = 0; j < num_cases; j++) {
        phi_args.push_back(returns_values[j][i].node);
      }
      phi_args.push_back(returns_ref[i].node);
      phi_args.push_back(control);
      SetAndTypeNode(
          &returns[i],
          builder_->Phi(sig->GetReturn(i), num_cases + 1, phi_args.data()));
    }
  }

  void ReturnCallRef(FullDecoder* decoder, const Value& func_ref,
                     const FunctionSig* sig, const Value args[]) {
    const CallSiteFeedback* feedback = nullptr;
    if (inlining_enabled(decoder) && !type_feedback_.empty()) {
      feedback = &next_call_feedback();
    }
    if (feedback == nullptr || feedback->num_cases() == 0) {
      DoReturnCall(decoder,
                   CallInfo::CallRef(func_ref, NullCheckFor(func_ref.type)),
                   sig, args);
      return;
    }

    // Check for equality against a function at a specific index, and if
    // successful, just emit a direct call.
    int num_cases = feedback->num_cases();
    for (int i = 0; i < num_cases; i++) {
      const uint32_t expected_function_index = feedback->function_index(i);

      if (v8_flags.trace_wasm_inlining) {
        PrintF("[function %d: call #%d: graph support for inlining #%d]\n",
               func_index_, feedback_instruction_index_ - 1,
               expected_function_index);
      }

      TFNode* success_control;
      TFNode* failure_control;
      builder_->CompareToFuncRefAtIndex(func_ref.node, expected_function_index,
                                        &success_control, &failure_control,
                                        i == num_cases - 1);
      TFNode* initial_effect = effect();

      builder_->SetControl(success_control);
      ssa_env_->control = success_control;
      DoReturnCall(decoder,
                   CallInfo::CallDirect(expected_function_index,
                                        feedback->call_count(i)),
                   sig, args);

      builder_->SetEffectControl(initial_effect, failure_control);
      ssa_env_->effect = initial_effect;
      ssa_env_->control = failure_control;
    }

    DoReturnCall(decoder,
                 CallInfo::CallRef(func_ref, NullCheckFor(func_ref.type)), sig,
                 args);
  }

  void BrOnNull(FullDecoder* decoder, const Value& ref_object, uint32_t depth,
                bool pass_null_along_branch, Value* result_on_fallthrough) {
    SsaEnv* false_env = ssa_env_;
    SsaEnv* true_env = Split(decoder->zone(), false_env);
    false_env->SetNotMerged();
    std::tie(true_env->control, false_env->control) =
        builder_->BrOnNull(ref_object.node, ref_object.type);
    builder_->SetControl(false_env->control);
    {
      ScopedSsaEnv scoped_env(this, true_env);
      int drop_values = pass_null_along_branch ? 0 : 1;
      BrOrRet(decoder, depth, drop_values);
    }
    SetAndTypeNode(
        result_on_fallthrough,
        builder_->TypeGuard(ref_object.node, result_on_fallthrough->type));
  }

  void BrOnNonNull(FullDecoder* decoder, const Value& ref_object, Value* result,
                   uint32_t depth, bool /* drop_null_on_fallthrough */) {
    SsaEnv* false_env = ssa_env_;
    SsaEnv* true_env = Split(decoder->zone(), false_env);
    false_env->SetNotMerged();
    std::tie(false_env->control, true_env->control) =
        builder_->BrOnNull(ref_object.node, ref_object.type);
    builder_->SetControl(false_env->control);
    ScopedSsaEnv scoped_env(this, true_env);
    // Make sure the TypeGuard has the right Control dependency.
    SetAndTypeNode(result, builder_->TypeGuard(ref_object.node, result->type));
    BrOrRet(decoder, depth);
  }

  void SimdOp(FullDecoder* decoder, WasmOpcode opcode, const Value* args,
              Value* result) {
    size_t num_inputs = WasmOpcodes::Signature(opcode)->parameter_count();
    NodeVector inputs(num_inputs);
    GetNodes(inputs.begin(), args, num_inputs);
    TFNode* node = builder_->SimdOp(opcode, inputs.begin());
    if (result) SetAndTypeNode(result, node);
  }

  void SimdLaneOp(FullDecoder* decoder, WasmOpcode opcode,
                  const SimdLaneImmediate& imm,
                  base::Vector<const Value> inputs, Value* result) {
    NodeVector nodes(inputs.size());
    GetNodes(nodes.begin(), inputs);
    SetAndTypeNode(result,
                   builder_->SimdLaneOp(opcode, imm.lane, nodes.begin()));
  }

  void Simd8x16ShuffleOp(FullDecoder* decoder, const Simd128Immediate& imm,
                         const Value& input0, const Value& input1,
                         Value* result) {
    TFNode* input_nodes[] = {input0.node, input1.node};
    SetAndTypeNode(result, builder_->Simd8x16ShuffleOp(imm.value, input_nodes));
  }

  void Throw(FullDecoder* decoder, const TagIndexImmediate& imm,
             const Value arg_values[]) {
    int count = static_cast<int>(imm.tag->sig->parameter_count());
    NodeVector args(count);
    GetNodes(args.data(), base::VectorOf(arg_values, count));
    CheckForException(decoder,
                      builder_->Throw(imm.index, imm.tag, base::VectorOf(args),
                                      decoder->position()),
                      false);
    builder_->TerminateThrow(effect(), control());
  }

  void Rethrow(FullDecoder* decoder, Control* block) {
    DCHECK(block->is_try_catchall() || block->is_try_catch());
    TFNode* exception = block->try_info->exception;
    DCHECK_NOT_NULL(exception);
    CheckForException(decoder, builder_->Rethrow(exception), false);
    builder_->TerminateThrow(effect(), control());
  }

  void CatchAndUnpackWasmException(FullDecoder* decoder, Control* block,
                                   TFNode* exception, const WasmTag* tag,
                                   TFNode* caught_tag, TFNode* exception_tag,
                                   base::Vector<Value> values) {
    TFNode* compare = builder_->ExceptionTagEqual(caught_tag, exception_tag);
    auto [if_catch, if_no_catch] = builder_->BranchNoHint(compare);
    // If the tags don't match we continue with the next tag by setting the
    // false environment as the new {TryInfo::catch_env} here.
    block->try_info->catch_env = Split(decoder->zone(), ssa_env_);
    block->try_info->catch_env->control = if_no_catch;
    block->block_env = Steal(decoder->zone(), ssa_env_);
    block->block_env->control = if_catch;
    SetEnv(block->block_env);
    NodeVector caught_values(values.size());
    base::Vector<TFNode*> caught_vector = base::VectorOf(caught_values);
    builder_->GetExceptionValues(exception, tag, caught_vector);
    for (size_t i = 0, e = values.size(); i < e; ++i) {
      SetAndTypeNode(&values[i], caught_values[i]);
    }
  }

  void CatchException(FullDecoder* decoder, const TagIndexImmediate& imm,
                      Control* block, base::Vector<Value> values) {
    DCHECK(block->is_try_catch());
    TFNode* exception = block->try_info->exception;
    SetEnv(block->try_info->catch_env);

    TFNode* caught_tag = builder_->GetExceptionTag(exception);
    TFNode* expected_tag = builder_->LoadTagFromTable(imm.index);

    if (imm.tag->sig->parameter_count() == 1 &&
        imm.tag->sig->GetParam(0).is_reference_to(HeapType::kExtern)) {
      // Check for the special case where the tag is WebAssembly.JSTag and the
      // exception is not a WebAssembly.Exception. In this case the exception is
      // caught and pushed on the operand stack.
      // Only perform this check if the tag signature is the same as
      // the JSTag signature, i.e. a single externref or (ref extern), otherwise
      // we know statically that it cannot be the JSTag.

      TFNode* is_js_exn = builder_->IsExceptionTagUndefined(caught_tag);
      auto [exn_is_js, exn_is_wasm] = builder_->BranchExpectFalse(is_js_exn);
      SsaEnv* exn_is_js_env = Split(decoder->zone(), ssa_env_);
      exn_is_js_env->control = exn_is_js;
      SsaEnv* exn_is_wasm_env = Steal(decoder->zone(), ssa_env_);
      exn_is_wasm_env->control = exn_is_wasm;

      // Case 1: A wasm exception.
      SetEnv(exn_is_wasm_env);
      CatchAndUnpackWasmException(decoder, block, exception, imm.tag,
                                  caught_tag, expected_tag, values);

      // Case 2: A JS exception.
      SetEnv(exn_is_js_env);
      TFNode* js_tag = builder_->LoadJSTag();
      TFNode* compare = builder_->ExceptionTagEqual(expected_tag, js_tag);
      auto [if_catch, if_no_catch] = builder_->BranchNoHint(compare);
      // Merge the wasm no-catch and JS no-catch paths.
      SsaEnv* if_no_catch_env = Split(decoder->zone(), ssa_env_);
      if_no_catch_env->control = if_no_catch;
      SetEnv(if_no_catch_env);
      Goto(decoder, block->try_info->catch_env);
      // Merge the wasm catch and JS catch paths.
      SsaEnv* if_catch_env = Steal(decoder->zone(), ssa_env_);
      if_catch_env->control = if_catch;
      SetEnv(if_catch_env);
      Goto(decoder, block->block_env);

      // The final env is a merge of case 1 and 2. The unpacked value is a Phi
      // of the unpacked value (case 1) and the exception itself (case 2).
      SetEnv(block->block_env);
      TFNode* phi_inputs[] = {values[0].node, exception,
                              block->block_env->control};
      TFNode* ref = builder_->Phi(wasm::kWasmExternRef, 2, phi_inputs);
      SetAndTypeNode(&values[0], ref);
    } else {
      CatchAndUnpackWasmException(decoder, block, exception, imm.tag,
                                  caught_tag, expected_tag, values);
    }
  }

  void Delegate(FullDecoder* decoder, uint32_t depth, Control* block) {
    DCHECK_EQ(decoder->control_at(0), block);
    DCHECK(block->is_incomplete_try());

    if (block->try_info->exception) {
      // Merge the current env into the target handler's env.
      SetEnv(block->try_info->catch_env);
      if (depth == decoder->control_depth() - 1) {
        if (inlined_status_ == kInlinedHandledCall) {
          if (emit_loop_exits()) {
            ValueVector stack_values;
            BuildNestedLoopExits(decoder, depth, false, stack_values,
                                 &block->try_info->exception);
          }
          // We are inlining this function and the inlined Call has a handler.
          // Add the delegated exception to {dangling_exceptions_}.
          dangling_exceptions_.Add(block->try_info->exception, effect(),
                                   control());
          return;
        }
        // We just throw to the caller here, so no need to generate IfSuccess
        // and IfFailure nodes.
        builder_->Rethrow(block->try_info->exception);
        builder_->TerminateThrow(effect(), control());
        return;
      }
      DCHECK(decoder->control_at(depth)->is_try());
      TryInfo* target_try = decoder->control_at(depth)->try_info;
      if (emit_loop_exits()) {
        ValueVector stack_values;
        BuildNestedLoopExits(decoder, depth, true, stack_values,
                             &block->try_info->exception);
      }
      Goto(decoder, target_try->catch_env);

      // Create or merge the exception.
      if (target_try->catch_env->state == SsaEnv::kReached) {
        target_try->exception = block->try_info->exception;
      } else {
        DCHECK_EQ(target_try->catch_env->state, SsaEnv::kMerged);
        target_try->exception = builder_->CreateOrMergeIntoPhi(
            MachineRepresentation::kTagged, target_try->catch_env->control,
            target_try->exception, block->try_info->exception);
      }
    }
  }

  void CatchAll(FullDecoder* decoder, Control* block) {
    DCHECK(block->is_try_catchall() || block->is_try_catch());
    DCHECK_EQ(decoder->control_at(0), block);
    SetEnv(block->try_info->catch_env);
  }

  void TryTable(FullDecoder* decoder, Control* block) { Try(decoder, block); }

  void CatchCase(FullDecoder* decoder, Control* block,
                 const CatchCase& catch_case, base::Vector<Value> values) {
    DCHECK(block->is_try_table());
    TFNode* exception = block->try_info->exception;
    SetEnv(block->try_info->catch_env);

    if (catch_case.kind == kCatchAll || catch_case.kind == kCatchAllRef) {
      if (catch_case.kind == kCatchAllRef) {
        DCHECK_EQ(values[0].type, kWasmExnRef);
        values[0].node = block->try_info->exception;
      }
      BrOrRet(decoder, catch_case.br_imm.depth);
      return;
    }

    TFNode* caught_tag = builder_->GetExceptionTag(exception);
    TFNode* expected_tag =
        builder_->LoadTagFromTable(catch_case.maybe_tag.tag_imm.index);

    base::Vector<Value> values_without_exnref =
        catch_case.kind == kCatch ? values
                                  : values.SubVector(0, values.size() - 1);

    if (catch_case.maybe_tag.tag_imm.tag->sig->parameter_count() == 1 &&
        catch_case.maybe_tag.tag_imm.tag->sig->GetParam(0) == kWasmExternRef) {
      // Check for the special case where the tag is WebAssembly.JSTag and the
      // exception is not a WebAssembly.Exception. In this case the exception is
      // caught and pushed on the operand stack.
      // Only perform this check if the tag signature is the same as
      // the JSTag signature, i.e. a single externref, otherwise
      // we know statically that it cannot be the JSTag.

      TFNode* is_js_exn = builder_->IsExceptionTagUndefined(caught_tag);
      auto [exn_is_js, exn_is_wasm] = builder_->BranchExpectFalse(is_js_exn);
      SsaEnv* exn_is_js_env = Split(decoder->zone(), ssa_env_);
      exn_is_js_env->control = exn_is_js;
      SsaEnv* exn_is_wasm_env = Steal(decoder->zone(), ssa_env_);
      exn_is_wasm_env->control = exn_is_wasm;

      // Case 1: A wasm exception.
      SetEnv(exn_is_wasm_env);
      CatchAndUnpackWasmException(decoder, block, exception,
                                  catch_case.maybe_tag.tag_imm.tag, caught_tag,
                                  expected_tag, values_without_exnref);

      // Case 2: A JS exception.
      SetEnv(exn_is_js_env);
      TFNode* js_tag = builder_->LoadJSTag();
      TFNode* compare = builder_->ExceptionTagEqual(expected_tag, js_tag);
      auto [if_catch, if_no_catch] = builder_->BranchNoHint(compare);
      // Merge the wasm no-catch and JS no-catch paths.
      SsaEnv* if_no_catch_env = Split(decoder->zone(), ssa_env_);
      if_no_catch_env->control = if_no_catch;
      SetEnv(if_no_catch_env);
      Goto(decoder, block->try_info->catch_env);
      // Merge the wasm catch and JS catch paths.
      SsaEnv* if_catch_env = Steal(decoder->zone(), ssa_env_);
      if_catch_env->control = if_catch;
      SetEnv(if_catch_env);
      Goto(decoder, block->block_env);

      // The final env is a merge of case 1 and 2. The unpacked value is a Phi
      // of the unpacked value (case 1) and the exception itself (case 2).
      SetEnv(block->block_env);
      TFNode* phi_inputs[] = {values[0].node, exception,
                              block->block_env->control};
      TFNode* ref = builder_->Phi(wasm::kWasmExternRef, 2, phi_inputs);
      SetAndTypeNode(&values[0], ref);
    } else {
      CatchAndUnpackWasmException(decoder, block, exception,
                                  catch_case.maybe_tag.tag_imm.tag, caught_tag,
                                  expected_tag, values_without_exnref);
    }

    if (catch_case.kind == kCatchRef) {
      DCHECK_EQ(values.last().type, kWasmExnRef);
      values.last().node = block->try_info->exception;
    }
    BrOrRet(decoder, catch_case.br_imm.depth);
    bool is_last = &catch_case == &block->catch_cases.last();
    if (is_last && !decoder->HasCatchAll(block)) {
      SetEnv(block->try_info->catch_env);
      ThrowRef(decoder, block->try_info->exception);
    }
  }

  void ThrowRef(FullDecoder* decoder, Value* value) {
    ThrowRef(decoder, value->node);
  }

  void AtomicOp(FullDecoder* decoder, WasmOpcode opcode, const Value args[],
                const size_t argc, const MemoryAccessImmediate& imm,
                Value* result) {
    NodeVector inputs(argc);
    GetNodes(inputs.begin(), args, argc);
    TFNode* node =
        builder_->AtomicOp(imm.memory, opcode, inputs.begin(), imm.alignment,
                           imm.offset, decoder->position());
    if (result) SetAndTypeNode(result, node);
  }

  void AtomicFence(FullDecoder* decoder) { builder_->AtomicFence(); }

  void MemoryInit(FullDecoder* decoder, const MemoryInitImmediate& imm,
                  const Value& dst, const Value& src, const Value& size) {
    builder_->MemoryInit(imm.memory.memory, imm.data_segment.index, dst.node,
                         src.node, size.node, decoder->position());
  }

  void DataDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    builder_->DataDrop(imm.index, decoder->position());
  }

  void MemoryCopy(FullDecoder* decoder, const MemoryCopyImmediate& imm,
                  const Value& dst, const Value& src, const Value& size) {
    builder_->MemoryCopy(imm.memory_dst.memory, imm.memory_src.memory, dst.node,
                         src.node, size.node, decoder->position());
  }

  void MemoryFill(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value& dst, const Value& value, const Value& size) {
    builder_->MemoryFill(imm.memory, dst.node, value.node, size.node,
                         decoder->position());
  }

  void TableInit(FullDecoder* decoder, const TableInitImmediate& imm,
                 const Value& dst, const Value& src, const Value& size) {
    builder_->TableInit(imm.table.index, imm.element_segment.index, dst.node,
                        src.node, size.node, decoder->position());
  }

  void ElemDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    builder_->ElemDrop(imm.index, decoder->position());
  }

  void TableCopy(FullDecoder* decoder, const TableCopyImmediate& imm,
                 const Value& dst, const Value& src, const Value& size) {
    builder_->TableCopy(imm.table_dst.index, imm.table_src.index, dst.node,
                        src.node, size.node, decoder->position());
  }

  void TableGrow(FullDecoder* decoder, const TableIndexImmediate& imm,
                 const Value& value, const Value& delta, Value* result) {
    SetAndTypeNode(result,
                   builder_->TableGrow(imm.index, value.node, delta.node,
                                       decoder->position()));
  }

  void TableSize(FullDecoder* decoder, const TableIndexImmediate& imm,
                 Value* result) {
    SetAndTypeNode(result, builder_->TableSize(imm.index));
  }

  void TableFill(FullDecoder* decoder, const TableIndexImmediate& imm,
                 const Value& start, const Value& value, const Value& count) {
    builder_->TableFill(imm.index, start.node, value.node, count.node,
                        decoder->position());
  }

  void StructNew(FullDecoder* decoder, const StructIndexImmediate& imm,
                 const Value args[], Value* result) {
    TFNode* rtt = builder_->RttCanon(imm.index);
    uint32_t field_count = imm.struct_type->field_count();
    NodeVector arg_nodes(field_count);
    for (uint32_t i = 0; i < field_count; i++) {
      arg_nodes[i] = args[i].node;
    }
    SetAndTypeNode(result, builder_->StructNew(imm.index, imm.struct_type, rtt,
                                               base::VectorOf(arg_nodes)));
  }
  void StructNewDefault(FullDecoder* decoder, const StructIndexImmediate& imm,
                        Value* result) {
    TFNode* rtt = builder_->RttCanon(imm.index);
    uint32_t field_count = imm.struct_type->field_count();
    NodeVector arg_nodes(field_count);
    for (uint32_t i = 0; i < field_count; i++) {
      ValueType field_type = imm.struct_type->field(i);
      arg_nodes[i] = builder_->SetType(builder_->DefaultValue(field_type),
                                       field_type.Unpacked());
    }
    SetAndTypeNode(result, builder_->StructNew(imm.index, imm.struct_type, rtt,
                                               base::VectorOf(arg_nodes)));
  }

  void StructGet(FullDecoder* decoder, const Value& struct_object,
                 const FieldImmediate& field, bool is_signed, Value* result) {
    SetAndTypeNode(result, builder_->StructGet(struct_object.node,
                                               field.struct_imm.struct_type,
                                               field.field_imm.index,
                                               NullCheckFor(struct_object.type),
                                               is_signed, decoder->position()));
  }

  void StructSet(FullDecoder* decoder, const Value& struct_object,
                 const FieldImmediate& field, const Value& field_value) {
    builder_->StructSet(struct_object.node, field.struct_imm.struct_type,
                        field.field_imm.index, field_value.node,
                        NullCheckFor(struct_object.type), decoder->position());
  }

  void ArrayNew(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                const Value& length, const Value& initial_value,
                Value* result) {
    TFNode* rtt = builder_->RttCanon(imm.index);
    SetAndTypeNode(result, builder_->ArrayNew(imm.index, imm.array_type,
                                              length.node, initial_value.node,
                                              rtt, decoder->position()));
    // array.new(_default) introduces a loop. Therefore, we have to mark the
    // immediately nesting loop (if any) as non-innermost.
    if (!loop_infos_.empty()) loop_infos_.back().can_be_innermost = false;
  }

  void ArrayNewDefault(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                       const Value& length, Value* result) {
    TFNode* rtt = builder_->RttCanon(imm.index);
    // This will be set in {builder_}.
    TFNode* initial_value = nullptr;
    SetAndTypeNode(result,
                   builder_->ArrayNew(imm.index, imm.array_type, length.node,
                                      initial_value, rtt, decoder->position()));
    // array.new(_default) introduces a loop. Therefore, we have to mark the
    // immediately nesting loop (if any) as non-innermost.
    if (!loop_infos_.empty()) loop_infos_.back().can_be_innermost = false;
  }

  void ArrayGet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index,
                bool is_signed, Value* result) {
    SetAndTypeNode(
        result, builder_->ArrayGet(array_obj.node, imm.array_type, index.node,
                                   NullCheckFor(array_obj.type), is_signed,
                                   decoder->position()));
  }

  void ArraySet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index,
                const Value& value) {
    builder_->ArraySet(array_obj.node, imm.array_type, index.node, value.node,
                       NullCheckFor(array_obj.type), decoder->position());
  }

  void ArrayLen(FullDecoder* decoder, const Value& array_obj, Value* result) {
    SetAndTypeNode(
        result, builder_->ArrayLen(array_obj.node, NullCheckFor(array_obj.type),
                                   decoder->position()));
  }

  void ArrayCopy(FullDecoder* decoder, const Value& dst, const Value& dst_index,
                 const Value& src, const Value& src_index,
                 const ArrayIndexImmediate& src_imm, const Value& length) {
    builder_->ArrayCopy(dst.node, dst_index.node, NullCheckFor(dst.type),
                        src.node, src_index.node, NullCheckFor(src.type),
                        length.node, src_imm.array_type, decoder->position());
  }

  void ArrayFill(FullDecoder* decoder, ArrayIndexImmediate& imm,
                 const Value& array, const Value& index, const Value& value,
                 const Value& length) {
    builder_->ArrayFill(array.node, index.node, value.node, length.node,
                        imm.array_type, NullCheckFor(array.type),
                        decoder->position());
    // array.fill introduces a loop. Therefore, we have to mark the immediately
    // nesting loop (if any) as non-innermost.
    if (!loop_infos_.empty()) loop_infos_.back().can_be_innermost = false;
  }

  void ArrayNewFixed(FullDecoder* decoder, const ArrayIndexImmediate& array_imm,
                     const IndexImmediate& length_imm, const Value elements[],
                     Value* result) {
    TFNode* rtt = builder_->RttCanon(array_imm.index);
    NodeVector element_nodes(length_imm.index);
    GetNodes(element_nodes.data(), elements, length_imm.index);
    SetAndTypeNode(result, builder_->ArrayNewFixed(array_imm.array_type, rtt,
                                                   VectorOf(element_nodes)));
  }

  void ArrayNewSegment(FullDecoder* decoder,
                       const ArrayIndexImmediate& array_imm,
                       const IndexImmediate& segment_imm, const Value& offset,
                       const Value& length, Value* result) {
    TFNode* rtt = builder_->RttCanon(array_imm.index);
    SetAndTypeNode(result,
                   builder_->ArrayNewSegment(
                       segment_imm.index, offset.node, length.node, rtt,
                       array_imm.array_type->element_type().is_reference(),
                       decoder->position()));
  }

  void ArrayInitSegment(FullDecoder* decoder,
                        const ArrayIndexImmediate& array_imm,
                        const IndexImmediate& segment_imm, const Value& array,
                        const Value& array_index, const Value& segment_offset,
                        const Value& length) {
    builder_->ArrayInitSegment(
        segment_imm.index, array.node, array_index.node, segment_offset.node,
        length.node, array_imm.array_type->element_type().is_reference(),
        decoder->position());
  }

  void RefI31(FullDecoder* decoder, const Value& input, Value* result) {
    SetAndTypeNode(result, builder_->RefI31(input.node));
  }

  void I31GetS(FullDecoder* decoder, const Value& input, Value* result) {
    SetAndTypeNode(result,
                   builder_->I31GetS(input.node, NullCheckFor(input.type),
                                     decoder->position()));
  }

  void I31GetU(FullDecoder* decoder, const Value& input, Value* result) {
    SetAndTypeNode(result,
                   builder_->I31GetU(input.node, NullCheckFor(input.type),
                                     decoder->position()));
  }

  using WasmTypeCheckConfig = v8::internal::compiler::WasmTypeCheckConfig;

  void RefTest(FullDecoder* decoder, uint32_t ref_index, const Value& object,
               Value* result, bool null_succeeds) {
    TFNode* rtt = builder_->RttCanon(ref_index);
    WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         ref_index, null_succeeds ? kNullable : kNonNullable)};
    SetAndTypeNode(result, builder_->RefTest(object.node, rtt, config));
  }

  void RefTestAbstract(FullDecoder* decoder, const Value& object,
                       wasm::HeapType type, Value* result, bool null_succeeds) {
    WasmTypeCheckConfig config{
        object.type, ValueType::RefMaybeNull(
                         type, null_succeeds ? kNullable : kNonNullable)};
    SetAndTypeNode(result, builder_->RefTestAbstract(object.node, config));
  }

  void RefCast(FullDecoder* decoder, uint32_t ref_index, const Value& object,
               Value* result, bool null_succeeds) {
    TFNode* node = object.node;
    if (v8_flags.experimental_wasm_assume_ref_cast_succeeds) {
      node = builder_->TypeGuard(node, result->type);
    } else {
      TFNode* rtt = builder_->RttCanon(ref_index);
      WasmTypeCheckConfig config{object.type, result->type};
      node = builder_->RefCast(object.node, rtt, config, decoder->position());
    }
    SetAndTypeNode(result, node);
  }

  // TODO(jkummerow): {type} is redundant.
  void RefCastAbstract(FullDecoder* decoder, const Value& object,
                       wasm::HeapType type, Value* result, bool null_succeeds) {
    TFNode* node = object.node;
    if (v8_flags.experimental_wasm_assume_ref_cast_succeeds) {
      node = builder_->TypeGuard(node, result->type);
    } else {
      WasmTypeCheckConfig config{object.type, result->type};
      node =
          builder_->RefCastAbstract(object.node, config, decoder->position());
    }
    SetAndTypeNode(result, node);
  }

  template <compiler::WasmGraphBuilder::ResultNodesOfBr (
      compiler::WasmGraphBuilder::*branch_function)(TFNode*, TFNode*,
                                                    WasmTypeCheckConfig)>
  void BrOnCastAbs(FullDecoder* decoder, HeapType type, const Value& object,
                   Value* forwarding_value, uint32_t br_depth,
                   bool branch_on_match, bool null_succeeds) {
    TFNode* rtt =
        type.is_bottom() ? nullptr : builder_->RttCanon(type.ref_index());
    // If the type is bottom (used for abstract types), set HeapType to None.
    // The heap type is not read but the null information is needed for the
    // cast.
    ValueType to_type = ValueType::RefMaybeNull(
        type.is_bottom() ? HeapType::kNone : type.ref_index(),
        null_succeeds ? kNullable : kNonNullable);
    WasmTypeCheckConfig config{object.type, to_type};
    SsaEnv* branch_env = Split(decoder->zone(), ssa_env_);
    // TODO(choongwoo): Clear locals of `no_branch_env` after use.
    SsaEnv* no_branch_env = Steal(decoder->zone(), ssa_env_);
    no_branch_env->SetNotMerged();
    auto nodes_after_br =
        (builder_->*branch_function)(object.node, rtt, config);

    SsaEnv* match_env = branch_on_match ? branch_env : no_branch_env;
    SsaEnv* no_match_env = branch_on_match ? no_branch_env : branch_env;
    match_env->control = nodes_after_br.control_on_match;
    match_env->effect = nodes_after_br.effect_on_match;
    no_match_env->control = nodes_after_br.control_on_no_match;
    no_match_env->effect = nodes_after_br.effect_on_no_match;

    builder_->SetControl(no_branch_env->control);

    if (branch_on_match) {
      ScopedSsaEnv scoped_env(this, branch_env, no_branch_env);
      // Narrow type for the successful cast target branch.
      Forward(decoder, object, forwarding_value);
      // Currently, br_on_* instructions modify the value stack before calling
      // the interface function, so we don't need to drop any values here.
      BrOrRet(decoder, br_depth);
      // Note: Differently to below for !{branch_on_match}, we do not Forward
      // the value here to perform a TypeGuard. It can't be done here due to
      // asymmetric decoder code. A Forward here would be poped from the stack
      // and ignored by the decoder. Therefore the decoder has to call Forward
      // itself.
    } else {
      {
        ScopedSsaEnv scoped_env(this, branch_env, no_branch_env);
        // It is necessary in case of {null_succeeds} to forward the value.
        // This will add a TypeGuard to the non-null type (as in this case the
        // object is non-nullable).
        Forward(decoder, object, decoder->stack_value(1));
        BrOrRet(decoder, br_depth);
      }
      // Narrow type for the successful cast fallthrough branch.
      Forward(decoder, object, forwarding_value);
    }
  }

  void BrOnCast(FullDecoder* decoder, uint32_t ref_index, const Value& object,
                Value* value_on_branch, uint32_t br_depth, bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnCast>(
        decoder, HeapType{ref_index}, object, value_on_branch, br_depth, true,
        null_succeeds);
  }

  void BrOnCastFail(FullDecoder* decoder, uint32_t ref_index,
                    const Value& object, Value* value_on_fallthrough,
                    uint32_t br_depth, bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnCast>(
        decoder, HeapType{ref_index}, object, value_on_fallthrough, br_depth,
        false, null_succeeds);
  }

  void BrOnCastAbstract(FullDecoder* decoder, const Value& object,
                        HeapType type, Value* value_on_branch,
                        uint32_t br_depth, bool null_succeeds) {
    switch (type.representation()) {
      case HeapType::kEq:
        return BrOnEq(decoder, object, value_on_branch, br_depth,
                      null_succeeds);
      case HeapType::kI31:
        return BrOnI31(decoder, object, value_on_branch, br_depth,
                       null_succeeds);
      case HeapType::kStruct:
        return BrOnStruct(decoder, object, value_on_branch, br_depth,
                          null_succeeds);
      case HeapType::kArray:
        return BrOnArray(decoder, object, value_on_branch, br_depth,
                         null_succeeds);
      case HeapType::kString:
        return BrOnString(decoder, object, value_on_branch, br_depth,
                          null_succeeds);
      case HeapType::kNone:
      case HeapType::kNoExtern:
      case HeapType::kNoFunc:
      case HeapType::kNoExn:
        DCHECK(null_succeeds);
        // This is needed for BrOnNull. {value_on_branch} is on the value stack
        // and BrOnNull interacts with the values on the stack.
        // TODO(14034): The compiler shouldn't have to access the stack used by
        // the decoder ideally.
        // Note: This TypeGuard doesn't add any new type information but we need
        // a non-const Value that we can return to the decoder for
        // value_on_branch. The clean solution would be to emit the TypeGuard
        // with the branch-type in the effect edge of the branch and use
        // value_on_branch->type as type there.
        SetAndTypeNode(value_on_branch,
                       builder_->TypeGuard(object.node, object.type));
        return BrOnNull(decoder, object, br_depth, true, value_on_branch);
      case HeapType::kAny:
        // Any may never need a cast as it is either implicitly convertible or
        // never convertible for any given type.
      default:
        UNREACHABLE();
    }
  }

  void BrOnCastFailAbstract(FullDecoder* decoder, const Value& object,
                            HeapType type, Value* value_on_fallthrough,
                            uint32_t br_depth, bool null_succeeds) {
    switch (type.representation()) {
      case HeapType::kEq:
        return BrOnNonEq(decoder, object, value_on_fallthrough, br_depth,
                         null_succeeds);
      case HeapType::kI31:
        return BrOnNonI31(decoder, object, value_on_fallthrough, br_depth,
                          null_succeeds);
      case HeapType::kStruct:
        return BrOnNonStruct(decoder, object, value_on_fallthrough, br_depth,
                             null_succeeds);
      case HeapType::kArray:
        return BrOnNonArray(decoder, object, value_on_fallthrough, br_depth,
                            null_succeeds);
      case HeapType::kString:
        return BrOnNonString(decoder, object, value_on_fallthrough, br_depth,
                             null_succeeds);
      case HeapType::kNone:
      case HeapType::kNoExtern:
      case HeapType::kNoFunc:
      case HeapType::kNoExn:
        DCHECK(null_succeeds);
        // We need to store a node in the stack where the decoder so far only
        // pushed a value and expects the `BrOnCastFailAbstract` to set it.
        // TODO(14034): The compiler shouldn't have to access the stack used by
        // the decoder ideally.
        Forward(decoder, object, decoder->stack_value(1));
        return BrOnNonNull(decoder, object, value_on_fallthrough, br_depth,
                           true);
      case HeapType::kAny:
        // Any may never need a cast as it is either implicitly convertible or
        // never convertible for any given type.
      default:
        UNREACHABLE();
    }
  }

  void BrOnEq(FullDecoder* decoder, const Value& object, Value* value_on_branch,
              uint32_t br_depth, bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnEq>(
        decoder, HeapType{kBottom}, object, value_on_branch, br_depth, true,
        null_succeeds);
  }

  void BrOnNonEq(FullDecoder* decoder, const Value& object,
                 Value* value_on_fallthrough, uint32_t br_depth,
                 bool null_succeeds) {
    // TODO(14034): Merge BrOn* and BrOnNon* instructions as their only
    // difference is a boolean flag passed to BrOnCastAbs. This could also be
    // leveraged to merge BrOnCastFailAbstract and BrOnCastAbstract.
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnEq>(
        decoder, HeapType{kBottom}, object, value_on_fallthrough, br_depth,
        false, null_succeeds);
  }

  void BrOnStruct(FullDecoder* decoder, const Value& object,
                  Value* value_on_branch, uint32_t br_depth,
                  bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnStruct>(
        decoder, HeapType{kBottom}, object, value_on_branch, br_depth, true,
        null_succeeds);
  }

  void BrOnNonStruct(FullDecoder* decoder, const Value& object,
                     Value* value_on_fallthrough, uint32_t br_depth,
                     bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnStruct>(
        decoder, HeapType{kBottom}, object, value_on_fallthrough, br_depth,
        false, null_succeeds);
  }

  void BrOnArray(FullDecoder* decoder, const Value& object,
                 Value* value_on_branch, uint32_t br_depth,
                 bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnArray>(
        decoder, HeapType{kBottom}, object, value_on_branch, br_depth, true,
        null_succeeds);
  }

  void BrOnNonArray(FullDecoder* decoder, const Value& object,
                    Value* value_on_fallthrough, uint32_t br_depth,
                    bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnArray>(
        decoder, HeapType{kBottom}, object, value_on_fallthrough, br_depth,
        false, null_succeeds);
  }

  void BrOnI31(FullDecoder* decoder, const Value& object,
               Value* value_on_branch, uint32_t br_depth, bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnI31>(
        decoder, HeapType{kBottom}, object, value_on_branch, br_depth, true,
        null_succeeds);
  }

  void BrOnNonI31(FullDecoder* decoder, const Value& object,
                  Value* value_on_fallthrough, uint32_t br_depth,
                  bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnI31>(
        decoder, HeapType{kBottom}, object, value_on_fallthrough, br_depth,
        false, null_succeeds);
  }

  void BrOnString(FullDecoder* decoder, const Value& object,
                  Value* value_on_branch, uint32_t br_depth,
                  bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnString>(
        decoder, HeapType{kBottom}, object, value_on_branch, br_depth, true,
        null_succeeds);
  }

  void BrOnNonString(FullDecoder* decoder, const Value& object,
                     Value* value_on_fallthrough, uint32_t br_depth,
                     bool null_succeeds) {
    BrOnCastAbs<&compiler::WasmGraphBuilder::BrOnString>(
        decoder, HeapType{kBottom}, object, value_on_fallthrough, br_depth,
        false, null_succeeds);
  }

  void StringNewWtf8(FullDecoder* decoder, const MemoryIndexImmediate& memory,
                     const unibrow::Utf8Variant variant, const Value& offset,
                     const Value& size, Value* result) {
    SetAndTypeNode(result,
                   builder_->StringNewWtf8(memory.memory, variant, offset.node,
                                           size.node, decoder->position()));
  }

  void StringNewWtf8Array(FullDecoder* decoder,
                          const unibrow::Utf8Variant variant,
                          const Value& array, const Value& start,
                          const Value& end, Value* result) {
    SetAndTypeNode(result, builder_->StringNewWtf8Array(
                               variant, array.node, NullCheckFor(array.type),
                               start.node, end.node, decoder->position()));
  }

  void StringNewWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                      const Value& offset, const Value& size, Value* result) {
    SetAndTypeNode(result,
                   builder_->StringNewWtf16(imm.memory, offset.node, size.node,
                                            decoder->position()));
  }

  void StringNewWtf16Array(FullDecoder* decoder, const Value& array,
                           const Value& start, const Value& end,
                           Value* result) {
    SetAndTypeNode(result, builder_->StringNewWtf16Array(
                               array.node, NullCheckFor(array.type), start.node,
                               end.node, decoder->position()));
  }

  void StringConst(FullDecoder* decoder, const StringConstImmediate& imm,
                   Value* result) {
    SetAndTypeNode(result, builder_->StringConst(imm.index));
  }

  void StringMeasureWtf8(FullDecoder* decoder,
                         const unibrow::Utf8Variant variant, const Value& str,
                         Value* result) {
    switch (variant) {
      case unibrow::Utf8Variant::kUtf8:
        SetAndTypeNode(
            result, builder_->StringMeasureUtf8(
                        str.node, NullCheckFor(str.type), decoder->position()));
        break;
      case unibrow::Utf8Variant::kLossyUtf8:
      case unibrow::Utf8Variant::kWtf8:
        SetAndTypeNode(
            result, builder_->StringMeasureWtf8(
                        str.node, NullCheckFor(str.type), decoder->position()));
        break;
      case unibrow::Utf8Variant::kUtf8NoTrap:
        UNREACHABLE();
    }
  }

  void StringMeasureWtf16(FullDecoder* decoder, const Value& str,
                          Value* result) {
    SetAndTypeNode(
        result, builder_->StringMeasureWtf16(str.node, NullCheckFor(str.type),
                                             decoder->position()));
  }

  void StringEncodeWtf8(FullDecoder* decoder,
                        const MemoryIndexImmediate& memory,
                        const unibrow::Utf8Variant variant, const Value& str,
                        const Value& offset, Value* result) {
    SetAndTypeNode(
        result, builder_->StringEncodeWtf8(memory.memory, variant, str.node,
                                           NullCheckFor(str.type), offset.node,
                                           decoder->position()));
  }

  void StringEncodeWtf8Array(FullDecoder* decoder,
                             const unibrow::Utf8Variant variant,
                             const Value& str, const Value& array,
                             const Value& start, Value* result) {
    SetAndTypeNode(
        result, builder_->StringEncodeWtf8Array(
                    variant, str.node, NullCheckFor(str.type), array.node,
                    NullCheckFor(array.type), start.node, decoder->position()));
  }

  void StringEncodeWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                         const Value& str, const Value& offset, Value* result) {
    SetAndTypeNode(result, builder_->StringEncodeWtf16(
                               imm.memory, str.node, NullCheckFor(str.type),
                               offset.node, decoder->position()));
  }

  void StringEncodeWtf16Array(FullDecoder* decoder, const Value& str,
                              const Value& array, const Value& start,
                              Value* result) {
    SetAndTypeNode(
        result, builder_->StringEncodeWtf16Array(
                    str.node, NullCheckFor(str.type), array.node,
                    NullCheckFor(array.type), start.node, decoder->position()));
  }

  void StringConcat(FullDecoder* decoder, const Value& head, const Value& tail,
                    Value* result) {
    SetAndTypeNode(result, builder_->StringConcat(
                               head.node, NullCheckFor(head.type), tail.node,
                               NullCheckFor(tail.type), decoder->position()));
  }

  void StringEq(FullDecoder* decoder, const Value& a, const Value& b,
                Value* result) {
    SetAndTypeNode(result, builder_->StringEqual(a.node, a.type, b.node, b.type,
                                                 decoder->position()));
  }

  void StringIsUSVSequence(FullDecoder* decoder, const Value& str,
                           Value* result) {
    SetAndTypeNode(
        result, builder_->StringIsUSVSequence(str.node, NullCheckFor(str.type),
                                              decoder->position()));
  }

  void StringAsWtf8(FullDecoder* decoder, const Value& str, Value* result) {
    SetAndTypeNode(result,
                   builder_->StringAsWtf8(str.node, NullCheckFor(str.type),
                                          decoder->position()));
  }

  void StringViewWtf8Advance(FullDecoder* decoder, const Value& view,
                             const Value& pos, const Value& bytes,
                             Value* result) {
    SetAndTypeNode(result, builder_->StringViewWtf8Advance(
                               view.node, NullCheckFor(view.type), pos.node,
                               bytes.node, decoder->position()));
  }

  void StringViewWtf8Encode(FullDecoder* decoder,
                            const MemoryIndexImmediate& memory,
                            const unibrow::Utf8Variant variant,
                            const Value& view, const Value& addr,
                            const Value& pos, const Value& bytes,
                            Value* next_pos, Value* bytes_written) {
    builder_->StringViewWtf8Encode(memory.memory, variant, view.node,
                                   NullCheckFor(view.type), addr.node, pos.node,
                                   bytes.node, &next_pos->node,
                                   &bytes_written->node, decoder->position());
    builder_->SetType(next_pos->node, next_pos->type);
    builder_->SetType(bytes_written->node, bytes_written->type);
  }

  void StringViewWtf8Slice(FullDecoder* decoder, const Value& view,
                           const Value& start, const Value& end,
                           Value* result) {
    SetAndTypeNode(result, builder_->StringViewWtf8Slice(
                               view.node, NullCheckFor(view.type), start.node,
                               end.node, decoder->position()));
  }

  void StringAsWtf16(FullDecoder* decoder, const Value& str, Value* result) {
    SetAndTypeNode(result,
                   builder_->StringAsWtf16(str.node, NullCheckFor(str.type),
                                           decoder->position()));
  }

  void StringViewWtf16GetCodeUnit(FullDecoder* decoder, const Value& view,
                                  const Value& pos, Value* result) {
    SetAndTypeNode(result, builder_->StringViewWtf16GetCodeUnit(
                               view.node, NullCheckFor(view.type), pos.node,
                               decoder->position()));
  }

  void StringViewWtf16Encode(FullDecoder* decoder,
                             const MemoryIndexImmediate& imm, const Value& view,
                             const Value& offset, const Value& pos,
                             const Value& codeunits, Value* result) {
    SetAndTypeNode(
        result, builder_->StringViewWtf16Encode(
                    imm.memory, view.node, NullCheckFor(view.type), offset.node,
                    pos.node, codeunits.node, decoder->position()));
  }

  void StringViewWtf16Slice(FullDecoder* decoder, const Value& view,
                            const Value& start, const Value& end,
                            Value* result) {
    SetAndTypeNode(result, builder_->StringViewWtf16Slice(
                               view.node, NullCheckFor(view.type), start.node,
                               end.node, decoder->position()));
  }

  void StringAsIter(FullDecoder* decoder, const Value& str, Value* result) {
    SetAndTypeNode(result,
                   builder_->StringAsIter(str.node, NullCheckFor(str.type),
                                          decoder->position()));
  }

  void StringViewIterNext(FullDecoder* decoder, const Value& view,
                          Value* result) {
    SetAndTypeNode(
        result, builder_->StringViewIterNext(view.node, NullCheckFor(view.type),
                                             decoder->position()));
  }

  void StringViewIterAdvance(FullDecoder* decoder, const Value& view,
                             const Value& codepoints, Value* result) {
    SetAndTypeNode(result, builder_->StringViewIterAdvance(
                               view.node, NullCheckFor(view.type),
                               codepoints.node, decoder->position()));
  }

  void StringViewIterRewind(FullDecoder* decoder, const Value& view,
                            const Value& codepoints, Value* result) {
    SetAndTypeNode(result, builder_->StringViewIterRewind(
                               view.node, NullCheckFor(view.type),
                               codepoints.node, decoder->position()));
  }

  void StringViewIterSlice(FullDecoder* decoder, const Value& view,
                           const Value& codepoints, Value* result) {
    SetAndTypeNode(result, builder_->StringViewIterSlice(
                               view.node, NullCheckFor(view.type),
                               codepoints.node, decoder->position()));
  }

  void StringCompare(FullDecoder* decoder, const Value& lhs, const Value& rhs,
                     Value* result) {
    SetAndTypeNode(result, builder_->StringCompare(
                               lhs.node, NullCheckFor(lhs.type), rhs.node,
                               NullCheckFor(rhs.type), decoder->position()));
  }

  void StringFromCodePoint(FullDecoder* decoder, const Value& code_point,
                           Value* result) {
    SetAndTypeNode(result, builder_->StringFromCodePoint(code_point.node));
  }

  void StringHash(FullDecoder* decoder, const Value& string, Value* result) {
    SetAndTypeNode(result,
                   builder_->StringHash(string.node, NullCheckFor(string.type),
                                        decoder->position()));
  }

  void Forward(FullDecoder* decoder, const Value& from, Value* to) {
    if (from.type == to->type) {
      to->node = from.node;
    } else {
      SetAndTypeNode(to, builder_->TypeGuard(from.node, to->type));
    }
  }

  std::vector<compiler::WasmLoopInfo>& loop_infos() { return loop_infos_; }
  DanglingExceptions& dangling_exceptions() { return dangling_exceptions_; }

 private:
  LocalsAllocator locals_allocator_;
  SsaEnv* ssa_env_ = nullptr;
  compiler::WasmGraphBuilder* builder_;
  int func_index_;
  const BranchHintMap* branch_hints_ = nullptr;
  // Tracks loop data for loop unrolling.
  std::vector<compiler::WasmLoopInfo> loop_infos_;
  // When inlining, tracks exception handlers that are left dangling and must be
  // handled by the callee.
  DanglingExceptions dangling_exceptions_;
  AssumptionsJournal* assumptions_;
  InlinedStatus inlined_status_;
  // The entries in {type_feedback_} are indexed by the position of feedback-
  // consuming instructions (currently only calls).
  int feedback_instruction_index_ = 0;
  std::vector<CallSiteFeedback> type_feedback_;

  class V8_NODISCARD ScopedSsaEnv {
   public:
    ScopedSsaEnv(WasmGraphBuildingInterface* interface, SsaEnv* env,
                 SsaEnv* next_env = nullptr)
        : interface_(interface),
          next_env_(next_env ? next_env : interface->ssa_env_) {
      interface_->SetEnv(env);
    }
    ~ScopedSsaEnv() {
      interface_->ssa_env_->Kill();
      interface_->SetEnv(next_env_);
    }

   private:
    WasmGraphBuildingInterface* interface_;
    SsaEnv* next_env_;
  };

  TFNode* effect() { return builder_->effect(); }

  TFNode* control() { return builder_->control(); }

  TryInfo* current_try_info(FullDecoder* decoder) {
    DCHECK_LT(decoder->current_catch(), decoder->control_depth());
    return decoder->control_at(decoder->control_depth_of_current_catch())
        ->try_info;
  }

  // If {emit_loop_exits()} returns true, we need to emit LoopExit,
  // LoopExitEffect, and LoopExit nodes whenever a control resp. effect resp.
  // value escapes a loop. We emit loop exits in the following cases:
  // - When popping the control of a loop.
  // - At some nodes which connect to the graph's end. We do not always need to
  //   emit loop exits for such nodes, since the wasm loop analysis algorithm
  //   can handle a loop body which connects directly to the graph's end.
  //   However, we need to emit them anyway for nodes that may be rewired to
  //   different nodes during inlining. These are Return and TailCall nodes.
  // - After IfFailure nodes.
  // - When exiting a loop through Delegate.
  bool emit_loop_exits() {
    return v8_flags.wasm_loop_unrolling || v8_flags.wasm_loop_peeling;
  }

  bool inlining_enabled(FullDecoder* decoder) {
    return decoder->enabled_.has_inlining() || decoder->module_->is_wasm_gc;
  }

  void GetNodes(TFNode** nodes, const Value* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      nodes[i] = values[i].node;
    }
  }

  void GetNodes(TFNode** nodes, base::Vector<const Value> values) {
    GetNodes(nodes, values.begin(), values.size());
  }

  void SetEnv(SsaEnv* env) {
    if (v8_flags.trace_wasm_decoder) {
      char state = 'X';
      if (env) {
        switch (env->state) {
          case SsaEnv::kReached:
            state = 'R';
            break;
          case SsaEnv::kUnreachable:
            state = 'U';
            break;
          case SsaEnv::kMerged:
            state = 'M';
            break;
        }
      }
      PrintF("{set_env = %p, state = %c", env, state);
      if (env && env->control) {
        PrintF(", control = ");
        compiler::WasmGraphBuilder::PrintDebugName(env->control);
      }
      PrintF("}\n");
    }
    if (ssa_env_) {
      ssa_env_->control = control();
      ssa_env_->effect = effect();
    }
    ssa_env_ = env;
    builder_->SetEffectControl(env->effect, env->control);
    builder_->set_instance_cache(&env->instance_cache);
  }

  TFNode* CheckForException(FullDecoder* decoder, TFNode* node,
                            bool may_modify_instance_cache) {
    DCHECK_NOT_NULL(node);

    // We need to emit IfSuccess/IfException nodes if this node throws and has
    // an exception handler. An exception handler can either be a try-scope
    // around this node, or if this function is being inlined, the IfException
    // output of the inlined Call node.
    const bool inside_try_scope = decoder->current_catch() != -1;
    if (inlined_status_ != kInlinedHandledCall && !inside_try_scope) {
      return node;
    }

    TFNode* if_success = nullptr;
    TFNode* if_exception = nullptr;
    if (!builder_->ThrowsException(node, &if_success, &if_exception)) {
      return node;
    }

    // TODO(choongwoo): Clear locals of `success_env` after use.
    SsaEnv* success_env = Steal(decoder->zone(), ssa_env_);
    success_env->control = if_success;

    SsaEnv* exception_env = Split(decoder->zone(), success_env);
    exception_env->control = if_exception;
    exception_env->effect = if_exception;

    ScopedSsaEnv scoped_env(this, exception_env, success_env);

    // The exceptional operation could have modified memory size; we need to
    // reload the memory context into the exceptional control path.
    if (may_modify_instance_cache) {
      ReloadInstanceCacheIntoSsa(ssa_env_, decoder->module_);
    }

    if (emit_loop_exits()) {
      ValueVector values;
      BuildNestedLoopExits(decoder,
                           inside_try_scope
                               ? decoder->control_depth_of_current_catch()
                               : decoder->control_depth() - 1,
                           true, values, &if_exception);
    }
    if (inside_try_scope) {
      TryInfo* try_info = current_try_info(decoder);
      Goto(decoder, try_info->catch_env);
      if (try_info->exception == nullptr) {
        DCHECK_EQ(SsaEnv::kReached, try_info->catch_env->state);
        try_info->exception = if_exception;
      } else {
        DCHECK_EQ(SsaEnv::kMerged, try_info->catch_env->state);
        try_info->exception = builder_->CreateOrMergeIntoPhi(
            MachineRepresentation::kTaggedPointer, try_info->catch_env->control,
            try_info->exception, if_exception);
      }
    } else {
      DCHECK_EQ(inlined_status_, kInlinedHandledCall);
      // We leave the IfException/LoopExit node dangling, and record the
      // exception/effect/control here. We will connect them to the handler of
      // the inlined call during inlining.
      // Note: We have to generate the handler now since we have no way of
      // generating a LoopExit if needed in the inlining code.
      dangling_exceptions_.Add(if_exception, effect(), control());
    }
    return node;
  }

  void MergeValuesInto(FullDecoder* decoder, Control* c, Merge<Value>* merge,
                       Value* values) {
    DCHECK(merge == &c->start_merge || merge == &c->end_merge);

    SsaEnv* target = c->merge_env;
    // This has to be computed before calling Goto().
    const bool first = target->state == SsaEnv::kUnreachable;

    Goto(decoder, target);

    if (merge->arity == 0) return;

    for (uint32_t i = 0; i < merge->arity; ++i) {
      Value& val = values[i];
      Value& old = (*merge)[i];
      DCHECK_NOT_NULL(val.node);
      DCHECK(val.type == kWasmBottom || val.type.machine_representation() ==
                                            old.type.machine_representation());
      old.node = first ? val.node
                       : builder_->CreateOrMergeIntoPhi(
                             old.type.machine_representation(), target->control,
                             old.node, val.node);
    }
  }

  void MergeValuesInto(FullDecoder* decoder, Control* c, Merge<Value>* merge,
                       uint32_t drop_values = 0) {
#ifdef DEBUG
    uint32_t avail = decoder->stack_size() -
                     decoder->control_at(0)->stack_depth - drop_values;
    DCHECK_GE(avail, merge->arity);
#endif
    Value* stack_values = merge->arity > 0
                              ? decoder->stack_value(merge->arity + drop_values)
                              : nullptr;
    MergeValuesInto(decoder, c, merge, stack_values);
  }

  void Goto(FullDecoder* decoder, SsaEnv* to) {
    DCHECK_NOT_NULL(to);
    switch (to->state) {
      case SsaEnv::kUnreachable: {  // Overwrite destination.
        to->state = SsaEnv::kReached;
        DCHECK_EQ(ssa_env_->locals.size(), decoder->num_locals());
        to->locals = ssa_env_->locals;
        to->control = control();
        to->effect = effect();
        to->instance_cache = ssa_env_->instance_cache;
        break;
      }
      case SsaEnv::kReached: {  // Create a new merge.
        to->state = SsaEnv::kMerged;
        // Merge control.
        TFNode* controls[] = {to->control, control()};
        TFNode* merge = builder_->Merge(2, controls);
        to->control = merge;
        // Merge effects.
        TFNode* old_effect = effect();
        if (old_effect != to->effect) {
          TFNode* inputs[] = {to->effect, old_effect, merge};
          to->effect = builder_->EffectPhi(2, inputs);
        }
        // Merge locals.
        DCHECK_EQ(ssa_env_->locals.size(), decoder->num_locals());
        for (uint32_t i = 0; i < to->locals.size(); i++) {
          TFNode* a = to->locals[i];
          TFNode* b = ssa_env_->locals[i];
          if (a != b) {
            TFNode* inputs[] = {a, b, merge};
            to->locals[i] = builder_->Phi(decoder->local_type(i), 2, inputs);
          }
        }
        // Start a new merge from the instance cache.
        builder_->NewInstanceCacheMerge(&to->instance_cache,
                                        &ssa_env_->instance_cache, merge);
        break;
      }
      case SsaEnv::kMerged: {
        TFNode* merge = to->control;
        // Extend the existing merge control node.
        builder_->AppendToMerge(merge, control());
        // Merge effects.
        to->effect =
            builder_->CreateOrMergeIntoEffectPhi(merge, to->effect, effect());
        // Merge locals.
        for (uint32_t i = 0; i < to->locals.size(); i++) {
          to->locals[i] = builder_->CreateOrMergeIntoPhi(
              decoder->local_type(i).machine_representation(), merge,
              to->locals[i], ssa_env_->locals[i]);
        }
        // Merge the instance caches.
        builder_->MergeInstanceCacheInto(&to->instance_cache,
                                         &ssa_env_->instance_cache, merge);
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  // Create a complete copy of {from}.
  SsaEnv* Split(Zone* zone, SsaEnv* from) {
    DCHECK_NOT_NULL(from);
    if (from == ssa_env_) {
      ssa_env_->control = control();
      ssa_env_->effect = effect();
    }
    SsaEnv* result = zone->New<SsaEnv>(*from);
    result->state = SsaEnv::kReached;
    return result;
  }

  // Create a copy of {from} that steals its state and leaves {from}
  // unreachable.
  SsaEnv* Steal(Zone* zone, SsaEnv* from) {
    DCHECK_NOT_NULL(from);
    if (from == ssa_env_) {
      ssa_env_->control = control();
      ssa_env_->effect = effect();
    }
    SsaEnv* result = zone->New<SsaEnv>(std::move(*from));
    result->state = SsaEnv::kReached;
    return result;
  }

  class CallInfo {
   public:
    enum CallMode { kCallDirect, kCallIndirect, kCallRef };

    static CallInfo CallDirect(uint32_t callee_index, int call_count) {
      return {kCallDirect, callee_index, nullptr,
              static_cast<uint32_t>(call_count),
              CheckForNull::kWithoutNullCheck};
    }

    static CallInfo CallIndirect(const Value& index_value, uint32_t table_index,
                                 uint32_t sig_index) {
      return {kCallIndirect, sig_index, &index_value, table_index,
              CheckForNull::kWithoutNullCheck};
    }

    static CallInfo CallRef(const Value& funcref_value,
                            CheckForNull null_check) {
      return {kCallRef, 0, &funcref_value, 0, null_check};
    }

    CallMode call_mode() { return call_mode_; }

    uint32_t sig_index() {
      DCHECK_EQ(call_mode_, kCallIndirect);
      return callee_or_sig_index_;
    }

    uint32_t callee_index() {
      DCHECK_EQ(call_mode_, kCallDirect);
      return callee_or_sig_index_;
    }

    int call_count() {
      DCHECK_EQ(call_mode_, kCallDirect);
      return static_cast<int>(table_index_or_call_count_);
    }

    CheckForNull null_check() {
      DCHECK_EQ(call_mode_, kCallRef);
      return null_check_;
    }

    const Value* index_or_callee_value() {
      DCHECK_NE(call_mode_, kCallDirect);
      return index_or_callee_value_;
    }

    uint32_t table_index() {
      DCHECK_EQ(call_mode_, kCallIndirect);
      return table_index_or_call_count_;
    }

   private:
    CallInfo(CallMode call_mode, uint32_t callee_or_sig_index,
             const Value* index_or_callee_value,
             uint32_t table_index_or_call_count, CheckForNull null_check)
        : call_mode_(call_mode),
          callee_or_sig_index_(callee_or_sig_index),
          index_or_callee_value_(index_or_callee_value),
          table_index_or_call_count_(table_index_or_call_count),
          null_check_(null_check) {}
    CallMode call_mode_;
    uint32_t callee_or_sig_index_;
    const Value* index_or_callee_value_;
    uint32_t table_index_or_call_count_;
    CheckForNull null_check_;
  };

  void DoCall(FullDecoder* decoder, CallInfo call_info, const FunctionSig* sig,
              const Value args[], Value returns[]) {
    size_t param_count = sig->parameter_count();
    size_t return_count = sig->return_count();
    NodeVector arg_nodes(param_count + 1);
    base::SmallVector<TFNode*, 1> return_nodes(return_count);
    arg_nodes[0] = (call_info.call_mode() == CallInfo::kCallDirect)
                       ? nullptr
                       : call_info.index_or_callee_value()->node;

    for (size_t i = 0; i < param_count; ++i) {
      arg_nodes[i + 1] = args[i].node;
    }
    switch (call_info.call_mode()) {
      case CallInfo::kCallIndirect: {
        TFNode* call = builder_->CallIndirect(
            call_info.table_index(), call_info.sig_index(),
            base::VectorOf(arg_nodes), base::VectorOf(return_nodes),
            decoder->position());
        CheckForException(decoder, call, true);
        break;
      }
      case CallInfo::kCallDirect: {
        TFNode* call = builder_->CallDirect(
            call_info.callee_index(), base::VectorOf(arg_nodes),
            base::VectorOf(return_nodes), decoder->position());
        builder_->StoreCallCount(call, call_info.call_count());
        CheckForException(decoder, call, true);
        break;
      }
      case CallInfo::kCallRef: {
        TFNode* call = builder_->CallRef(
            sig, base::VectorOf(arg_nodes), base::VectorOf(return_nodes),
            call_info.null_check(), decoder->position());
        CheckForException(decoder, call, true);
        break;
      }
    }
    for (size_t i = 0; i < return_count; ++i) {
      SetAndTypeNode(&returns[i], return_nodes[i]);
    }
    // The invoked function could have used grow_memory, so we need to
    // reload memory information.
    ReloadInstanceCacheIntoSsa(ssa_env_, decoder->module_);
  }

  void DoReturnCall(FullDecoder* decoder, CallInfo call_info,
                    const FunctionSig* sig, const Value args[]) {
    size_t arg_count = sig->parameter_count();

    ValueVector arg_values(arg_count + 1);
    if (call_info.call_mode() == CallInfo::kCallDirect) {
      arg_values[0].node = nullptr;
    } else {
      arg_values[0] = *call_info.index_or_callee_value();
      // This is not done by copy assignment.
      arg_values[0].node = call_info.index_or_callee_value()->node;
    }
    if (arg_count > 0) {
      std::memcpy(arg_values.data() + 1, args, arg_count * sizeof(Value));
    }

    if (emit_loop_exits()) {
      BuildNestedLoopExits(decoder, decoder->control_depth(), false,
                           arg_values);
    }

    NodeVector arg_nodes(arg_count + 1);
    GetNodes(arg_nodes.data(), base::VectorOf(arg_values));

    switch (call_info.call_mode()) {
      case CallInfo::kCallIndirect:
        builder_->ReturnCallIndirect(
            call_info.table_index(), call_info.sig_index(),
            base::VectorOf(arg_nodes), decoder->position());
        break;
      case CallInfo::kCallDirect: {
        TFNode* call = builder_->ReturnCall(call_info.callee_index(),
                                            base::VectorOf(arg_nodes),
                                            decoder->position());
        builder_->StoreCallCount(call, call_info.call_count());
        break;
      }
      case CallInfo::kCallRef:
        builder_->ReturnCallRef(sig, base::VectorOf(arg_nodes),
                                call_info.null_check(), decoder->position());
        break;
    }
  }

  const CallSiteFeedback& next_call_feedback() {
    DCHECK_LT(feedback_instruction_index_, type_feedback_.size());
    return type_feedback_[feedback_instruction_index_++];
  }

  void BuildLoopExits(FullDecoder* decoder, Control* loop) {
    builder_->LoopExit(loop->loop_node);
    ssa_env_->control = control();
    ssa_env_->effect = effect();
  }

  void WrapLocalsAtLoopExit(FullDecoder* decoder, Control* loop) {
    for (uint32_t index = 0; index < decoder->num_locals(); index++) {
      if (loop->loop_assignments->Contains(static_cast<int>(index))) {
        ssa_env_->locals[index] = builder_->LoopExitValue(
            ssa_env_->locals[index],
            decoder->local_type(index).machine_representation());
      }
    }
    if (loop->loop_assignments->Contains(decoder->num_locals())) {
      for (auto field : compiler::WasmInstanceCacheNodes::kFields) {
        if (ssa_env_->instance_cache.*field == nullptr) continue;
        ssa_env_->instance_cache.*field =
            builder_->LoopExitValue(ssa_env_->instance_cache.*field,
                                    MachineType::PointerRepresentation());
      }
    }
  }

  void BuildNestedLoopExits(FullDecoder* decoder, uint32_t depth_limit,
                            bool wrap_exit_values, ValueVector& stack_values,
                            TFNode** exception_value = nullptr) {
    DCHECK(emit_loop_exits());
    Control* control = nullptr;
    // We are only interested in exits from the innermost loop.
    for (uint32_t i = 0; i < depth_limit; i++) {
      Control* c = decoder->control_at(i);
      if (c->is_loop()) {
        control = c;
        break;
      }
    }
    if (control != nullptr && control->loop_innermost) {
      BuildLoopExits(decoder, control);
      for (Value& value : stack_values) {
        if (value.node != nullptr) {
          value.node = builder_->SetType(
              builder_->LoopExitValue(value.node,
                                      value.type.machine_representation()),
              value.type);
        }
      }
      if (exception_value != nullptr) {
        *exception_value = builder_->LoopExitValue(
            *exception_value, MachineRepresentation::kTaggedPointer);
      }
      if (wrap_exit_values) {
        WrapLocalsAtLoopExit(decoder, control);
      }
    }
  }

  CheckForNull NullCheckFor(ValueType type) {
    DCHECK(type.is_object_reference());
    return type.is_nullable() ? CheckForNull::kWithNullCheck
                              : CheckForNull::kWithoutNullCheck;
  }

  void SetAndTypeNode(Value* value, TFNode* node) {
    // This DCHECK will help us catch uninitialized values.
    DCHECK_LT(value->type.kind(), kBottom);
    value->node = builder_->SetType(node, value->type);
  }

  // In order to determine the memory index to cache in an SSA value, we try to
  // determine the first memory index that will be accessed in the function. If
  // we do not find a memory access this method returns -1.
  // This is a best-effort implementation: It ignores potential control flow and
  // only looks for basic memory load and store operations.
  int FindFirstUsedMemoryIndex(base::Vector<const uint8_t> body, Zone* zone) {
    BodyLocalDecls locals;
    for (BytecodeIterator it{body.begin(), body.end(), &locals, zone};
         it.has_next(); it.next()) {
      WasmOpcode opcode = it.current();
      constexpr bool kConservativelyAssumeMemory64 = true;
      switch (opcode) {
        default:
          break;
#define CASE(name, ...) case kExpr##name:
          FOREACH_LOAD_MEM_OPCODE(CASE)
          FOREACH_STORE_MEM_OPCODE(CASE)
#undef CASE
          MemoryAccessImmediate imm(&it, it.pc() + 1, UINT32_MAX,
                                    kConservativelyAssumeMemory64,
                                    Decoder::kNoValidation);
          return imm.mem_index;
      }
    }
    return -1;
  }

  void ThrowRef(FullDecoder* decoder, TFNode* exception) {
    DCHECK_NOT_NULL(exception);
    CheckForException(decoder, builder_->Rethrow(exception), false);
    builder_->TerminateThrow(effect(), control());
  }
};

}  // namespace

void BuildTFGraph(AccountingAllocator* allocator, WasmEnabledFeatures enabled,
                  const WasmModule* module, compiler::WasmGraphBuilder* builder,
                  WasmDetectedFeatures* detected, const FunctionBody& body,
                  std::vector<compiler::WasmLoopInfo>* loop_infos,
                  DanglingExceptions* dangling_exceptions,
                  compiler::NodeOriginTable* node_origins, int func_index,
                  AssumptionsJournal* assumptions,
                  InlinedStatus inlined_status) {
  Zone zone(allocator, ZONE_NAME);
  WasmFullDecoder<Decoder::NoValidationTag, WasmGraphBuildingInterface> decoder(
      &zone, module, enabled, detected, body, builder, func_index, assumptions,
      inlined_status, &zone);
  if (node_origins) {
    builder->AddBytecodePositionDecorator(node_origins, &decoder);
  }
  decoder.Decode();
  if (node_origins) {
    builder->RemoveBytecodePositionDecorator();
  }
  *loop_infos = std::move(decoder.interface().loop_infos());
  if (dangling_exceptions != nullptr) {
    *dangling_exceptions = std::move(decoder.interface().dangling_exceptions());
  }
  // TurboFan does not run with validation, so graph building must always
  // succeed.
  CHECK(decoder.ok());
}

}  // namespace v8::internal::wasm
