// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/instruction-selector.h"

#include <limits>
#include <optional>

#include "include/v8-internal.h"
#include "src/base/iterator.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/tick-counter.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/globals.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/schedule.h"
#include "src/compiler/state-values-utils.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/numbers/conversions-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/simd-shuffle.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

#define VISIT_UNSUPPORTED_OP(op)                          \
  template <typename Adapter>                             \
  void InstructionSelectorT<Adapter>::Visit##op(node_t) { \
    UNIMPLEMENTED();                                      \
  }

namespace {
// Here we really want the raw Bits of the mask, but the `.bits()` method is
// not constexpr, and so users of this constant need to call it.
// TODO(turboshaft): EffectDimensions could probably be defined via
// base::Flags<> instead, which should solve this.
constexpr turboshaft::EffectDimensions kTurboshaftEffectLevelMask =
    turboshaft::OpEffects().CanReadMemory().produces;
}

Tagged<Smi> NumberConstantToSmi(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kNumberConstant);
  const double d = OpParameter<double>(node->op());
  Tagged<Smi> smi = Smi::FromInt(static_cast<int32_t>(d));
  CHECK_EQ(smi.value(), d);
  return smi;
}

template <typename Adapter>
InstructionSelectorT<Adapter>::InstructionSelectorT(
    Zone* zone, size_t node_count, Linkage* linkage,
    InstructionSequence* sequence, schedule_t schedule,
    source_position_table_t* source_positions, Frame* frame,
    InstructionSelector::EnableSwitchJumpTable enable_switch_jump_table,
    TickCounter* tick_counter, JSHeapBroker* broker,
    size_t* max_unoptimized_frame_height, size_t* max_pushed_argument_count,
    InstructionSelector::SourcePositionMode source_position_mode,
    Features features, InstructionSelector::EnableScheduling enable_scheduling,
    InstructionSelector::EnableRootsRelativeAddressing
        enable_roots_relative_addressing,
    InstructionSelector::EnableTraceTurboJson trace_turbo)
    : Adapter(schedule),
      zone_(zone),
      linkage_(linkage),
      sequence_(sequence),
      source_positions_(source_positions),
      source_position_mode_(source_position_mode),
      features_(features),
      schedule_(schedule),
      current_block_(nullptr),
      instructions_(zone),
      continuation_inputs_(sequence->zone()),
      continuation_outputs_(sequence->zone()),
      continuation_temps_(sequence->zone()),
      defined_(static_cast<int>(node_count), zone),
      used_(static_cast<int>(node_count), zone),
      effect_level_(node_count, 0, zone),
      virtual_registers_(node_count,
                         InstructionOperand::kInvalidVirtualRegister, zone),
      virtual_register_rename_(zone),
      scheduler_(nullptr),
      enable_scheduling_(enable_scheduling),
      enable_roots_relative_addressing_(enable_roots_relative_addressing),
      enable_switch_jump_table_(enable_switch_jump_table),
      state_values_cache_(zone),
      frame_(frame),
      instruction_selection_failed_(false),
      instr_origins_(sequence->zone()),
      trace_turbo_(trace_turbo),
      tick_counter_(tick_counter),
      broker_(broker),
      max_unoptimized_frame_height_(max_unoptimized_frame_height),
      max_pushed_argument_count_(max_pushed_argument_count)
#if V8_TARGET_ARCH_64_BIT
      ,
      phi_states_(node_count, Upper32BitsState::kNotYetChecked, zone)
#endif
{
  if constexpr (Adapter::IsTurboshaft) {
    turboshaft_use_map_.emplace(*schedule_, zone);
    protected_loads_to_remove_.emplace(static_cast<int>(node_count), zone);
    additional_protected_instructions_.emplace(static_cast<int>(node_count),
                                               zone);
  }

  DCHECK_EQ(*max_unoptimized_frame_height, 0);  // Caller-initialized.

  instructions_.reserve(node_count);
  continuation_inputs_.reserve(5);
  continuation_outputs_.reserve(2);

  if (trace_turbo_ == InstructionSelector::kEnableTraceTurboJson) {
    instr_origins_.assign(node_count, {-1, 0});
  }
}

template <typename Adapter>
std::optional<BailoutReason>
InstructionSelectorT<Adapter>::SelectInstructions() {
  // Mark the inputs of all phis in loop headers as used.
  block_range_t blocks = this->rpo_order(schedule());
  for (const block_t block : blocks) {
    if (!this->IsLoopHeader(block)) continue;
    DCHECK_LE(2u, this->PredecessorCount(block));
    for (node_t node : this->nodes(block)) {
      if (!this->IsPhi(node)) continue;

      // Mark all inputs as used.
      for (node_t input : this->inputs(node)) {
        MarkAsUsed(input);
      }
    }
  }

  // Visit each basic block in post order.
  for (auto i = blocks.rbegin(); i != blocks.rend(); ++i) {
    VisitBlock(*i);
    if (instruction_selection_failed())
      return BailoutReason::kCodeGenerationFailed;
  }

  // Schedule the selected instructions.
  if (UseInstructionScheduling()) {
    scheduler_ = zone()->template New<InstructionScheduler>(zone(), sequence());
  }

  for (const block_t block : blocks) {
    InstructionBlock* instruction_block =
        sequence()->InstructionBlockAt(this->rpo_number(block));
    for (size_t i = 0; i < instruction_block->phis().size(); i++) {
      UpdateRenamesInPhi(instruction_block->PhiAt(i));
    }
    size_t end = instruction_block->code_end();
    size_t start = instruction_block->code_start();
    DCHECK_LE(end, start);
    StartBlock(this->rpo_number(block));
    if (end != start) {
      while (start-- > end + 1) {
        UpdateRenames(instructions_[start]);
        AddInstruction(instructions_[start]);
      }
      UpdateRenames(instructions_[end]);
      AddTerminator(instructions_[end]);
    }
    EndBlock(this->rpo_number(block));
  }
#if DEBUG
  sequence()->ValidateSSA();
#endif
  return std::nullopt;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::StartBlock(RpoNumber rpo) {
  if (UseInstructionScheduling()) {
    DCHECK_NOT_NULL(scheduler_);
    scheduler_->StartBlock(rpo);
  } else {
    sequence()->StartBlock(rpo);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EndBlock(RpoNumber rpo) {
  if (UseInstructionScheduling()) {
    DCHECK_NOT_NULL(scheduler_);
    scheduler_->EndBlock(rpo);
  } else {
    sequence()->EndBlock(rpo);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddTerminator(Instruction* instr) {
  if (UseInstructionScheduling()) {
    DCHECK_NOT_NULL(scheduler_);
    scheduler_->AddTerminator(instr);
  } else {
    sequence()->AddInstruction(instr);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddInstruction(Instruction* instr) {
  if (UseInstructionScheduling()) {
    DCHECK_NOT_NULL(scheduler_);
    scheduler_->AddInstruction(instr);
  } else {
    sequence()->AddInstruction(instr);
  }
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(InstructionCode opcode,
                                                 InstructionOperand output,
                                                 size_t temp_count,
                                                 InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  return Emit(opcode, output_count, &output, 0, nullptr, temp_count, temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(InstructionCode opcode,
                                                 InstructionOperand output,
                                                 InstructionOperand a,
                                                 size_t temp_count,
                                                 InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  return Emit(opcode, output_count, &output, 1, &a, temp_count, temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(
    InstructionCode opcode, InstructionOperand output, InstructionOperand a,
    InstructionOperand b, size_t temp_count, InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  InstructionOperand inputs[] = {a, b};
  size_t input_count = arraysize(inputs);
  return Emit(opcode, output_count, &output, input_count, inputs, temp_count,
              temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(
    InstructionCode opcode, InstructionOperand output, InstructionOperand a,
    InstructionOperand b, InstructionOperand c, size_t temp_count,
    InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  InstructionOperand inputs[] = {a, b, c};
  size_t input_count = arraysize(inputs);
  return Emit(opcode, output_count, &output, input_count, inputs, temp_count,
              temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(
    InstructionCode opcode, InstructionOperand output, InstructionOperand a,
    InstructionOperand b, InstructionOperand c, InstructionOperand d,
    size_t temp_count, InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  InstructionOperand inputs[] = {a, b, c, d};
  size_t input_count = arraysize(inputs);
  return Emit(opcode, output_count, &output, input_count, inputs, temp_count,
              temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(
    InstructionCode opcode, InstructionOperand output, InstructionOperand a,
    InstructionOperand b, InstructionOperand c, InstructionOperand d,
    InstructionOperand e, size_t temp_count, InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  InstructionOperand inputs[] = {a, b, c, d, e};
  size_t input_count = arraysize(inputs);
  return Emit(opcode, output_count, &output, input_count, inputs, temp_count,
              temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(
    InstructionCode opcode, InstructionOperand output, InstructionOperand a,
    InstructionOperand b, InstructionOperand c, InstructionOperand d,
    InstructionOperand e, InstructionOperand f, size_t temp_count,
    InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  InstructionOperand inputs[] = {a, b, c, d, e, f};
  size_t input_count = arraysize(inputs);
  return Emit(opcode, output_count, &output, input_count, inputs, temp_count,
              temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(
    InstructionCode opcode, InstructionOperand output, InstructionOperand a,
    InstructionOperand b, InstructionOperand c, InstructionOperand d,
    InstructionOperand e, InstructionOperand f, InstructionOperand g,
    InstructionOperand h, size_t temp_count, InstructionOperand* temps) {
  size_t output_count = output.IsInvalid() ? 0 : 1;
  InstructionOperand inputs[] = {a, b, c, d, e, f, g, h};
  size_t input_count = arraysize(inputs);
  return Emit(opcode, output_count, &output, input_count, inputs, temp_count,
              temps);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(
    InstructionCode opcode, size_t output_count, InstructionOperand* outputs,
    size_t input_count, InstructionOperand* inputs, size_t temp_count,
    InstructionOperand* temps) {
  if (output_count >= Instruction::kMaxOutputCount ||
      input_count >= Instruction::kMaxInputCount ||
      temp_count >= Instruction::kMaxTempCount) {
    set_instruction_selection_failed();
    return nullptr;
  }

  Instruction* instr =
      Instruction::New(instruction_zone(), opcode, output_count, outputs,
                       input_count, inputs, temp_count, temps);
  return Emit(instr);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::Emit(Instruction* instr) {
  instructions_.push_back(instr);
  return instr;
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::CanCover(node_t user, node_t node) const {
  // 1. Both {user} and {node} must be in the same basic block.
  if (this->block(schedule(), node) != current_block_) {
    return false;
  }

  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Operation& op = this->Get(node);
    // 2. If node does not produce anything, it can be covered.
    if (op.Effects().produces.bits() == 0) {
      return this->is_exclusive_user_of(user, node);
    }
  } else {
    // 2. Pure {node}s must be owned by the {user}.
    if (node->op()->HasProperty(Operator::kPure)) {
      return node->OwnedBy(user);
    }
  }

  // 3. Otherwise, the {node}'s effect level must match the {user}'s.
  if constexpr (Adapter::IsTurboshaft) {
    // A ProtectedLoad node itself increases effect_level by one.
    int effect_level_after_node =
        GetEffectLevel(node) + (this->IsProtectedLoad(node) ? 1 : 0);
    if (current_effect_level_ != effect_level_after_node) {
      return false;
    }
  } else {
    if (GetEffectLevel(node) != current_effect_level_) {
      return false;
    }
  }

  // 4. Only {node} must have value edges pointing to {user}.
  return this->is_exclusive_user_of(user, node);
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsOnlyUserOfNodeInSameBlock(
    node_t user, node_t node) const {
  block_t bb_user = this->block(schedule(), user);
  block_t bb_node = this->block(schedule(), node);
  if (bb_user != bb_node) return false;

  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Operation& node_op = this->turboshaft_graph()->Get(node);
    if (node_op.saturated_use_count.Get() == 1) return true;
    for (turboshaft::OpIndex use : turboshaft_uses(node)) {
      if (use == user) continue;
      if (this->block(schedule(), use) == bb_user) return false;
    }
    return true;
  } else {
    for (Edge const edge : node->use_edges()) {
      Node* from = edge.from();
      if ((from != user) && (this->block(schedule(), from) == bb_user)) {
        return false;
      }
    }
  }
  return true;
}

template <>
Node* InstructionSelectorT<TurbofanAdapter>::FindProjection(
    Node* node, size_t projection_index) {
  return NodeProperties::FindProjection(node, projection_index);
}

template <>
turboshaft::OpIndex InstructionSelectorT<TurboshaftAdapter>::FindProjection(
    turboshaft::OpIndex node, size_t projection_index) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const turboshaft::Graph* graph = this->turboshaft_graph();
  // Projections are always emitted right after the operation.
  for (OpIndex next = graph->NextIndex(node); next.valid();
       next = graph->NextIndex(next)) {
    const ProjectionOp* projection = graph->Get(next).TryCast<ProjectionOp>();
    if (projection == nullptr) break;
    DCHECK(!projection->saturated_use_count.IsZero());
    if (projection->saturated_use_count.IsOne()) {
      // If the projection has a single use, it is the following tuple, so we
      // don't return it, since there is no point in emitting it.
      DCHECK(turboshaft_uses(next).size() == 1 &&
             graph->Get(turboshaft_uses(next)[0]).Is<TupleOp>());
      continue;
    }
    if (projection->index == projection_index) return next;
  }

  // If there is no Projection with index {projection_index} following the
  // operation, then there shouldn't be any such Projection in the graph. We
  // verify this in Debug mode.
#ifdef DEBUG
  for (OpIndex use : turboshaft_uses(node)) {
    if (const ProjectionOp* projection =
            this->Get(use).TryCast<ProjectionOp>()) {
      DCHECK_EQ(projection->input(), node);
      if (projection->index == projection_index) {
        // If we found the projection, it should have a single use: a Tuple
        // (which doesn't count as a regular use since it is just an artifact of
        // the Turboshaft graph).
        DCHECK(turboshaft_uses(use).size() == 1 &&
               graph->Get(turboshaft_uses(use)[0]).Is<TupleOp>());
      }
    }
  }
#endif  // DEBUG
  return OpIndex::Invalid();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::UpdateRenames(Instruction* instruction) {
  for (size_t i = 0; i < instruction->InputCount(); i++) {
    TryRename(instruction->InputAt(i));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::UpdateRenamesInPhi(PhiInstruction* phi) {
  for (size_t i = 0; i < phi->operands().size(); i++) {
    int vreg = phi->operands()[i];
    int renamed = GetRename(vreg);
    if (vreg != renamed) {
      phi->RenameInput(i, renamed);
    }
  }
}

template <typename Adapter>
int InstructionSelectorT<Adapter>::GetRename(int virtual_register) {
  int rename = virtual_register;
  while (true) {
    if (static_cast<size_t>(rename) >= virtual_register_rename_.size()) break;
    int next = virtual_register_rename_[rename];
    if (next == InstructionOperand::kInvalidVirtualRegister) {
      break;
    }
    rename = next;
  }
  return rename;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::TryRename(InstructionOperand* op) {
  if (!op->IsUnallocated()) return;
  UnallocatedOperand* unalloc = UnallocatedOperand::cast(op);
  int vreg = unalloc->virtual_register();
  int rename = GetRename(vreg);
  if (rename != vreg) {
    *unalloc = UnallocatedOperand(*unalloc, rename);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::SetRename(node_t node, node_t rename) {
  int vreg = GetVirtualRegister(node);
  if (static_cast<size_t>(vreg) >= virtual_register_rename_.size()) {
    int invalid = InstructionOperand::kInvalidVirtualRegister;
    virtual_register_rename_.resize(vreg + 1, invalid);
  }
  virtual_register_rename_[vreg] = GetVirtualRegister(rename);
}

template <typename Adapter>
int InstructionSelectorT<Adapter>::GetVirtualRegister(node_t node) {
  DCHECK(this->valid(node));
  size_t const id = this->id(node);
  DCHECK_LT(id, virtual_registers_.size());
  int virtual_register = virtual_registers_[id];
  if (virtual_register == InstructionOperand::kInvalidVirtualRegister) {
    virtual_register = sequence()->NextVirtualRegister();
    virtual_registers_[id] = virtual_register;
  }
  return virtual_register;
}

template <typename Adapter>
const std::map<typename Adapter::id_t, int>
InstructionSelectorT<Adapter>::GetVirtualRegistersForTesting() const {
  std::map<typename Adapter::id_t, int> virtual_registers;
  for (size_t n = 0; n < virtual_registers_.size(); ++n) {
    if (virtual_registers_[n] != InstructionOperand::kInvalidVirtualRegister) {
      typename Adapter::id_t const id = static_cast<typename Adapter::id_t>(n);
      virtual_registers.insert(std::make_pair(id, virtual_registers_[n]));
    }
  }
  return virtual_registers;
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsDefined(node_t node) const {
  DCHECK(this->valid(node));
  return defined_.Contains(this->id(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::MarkAsDefined(node_t node) {
  DCHECK(this->valid(node));
  defined_.Add(this->id(node));
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsUsed(node_t node) const {
  DCHECK(this->valid(node));
  if constexpr (Adapter::IsTurbofan) {
    // TODO(bmeurer): This is a terrible monster hack, but we have to make sure
    // that the Retain is actually emitted, otherwise the GC will mess up.
    if (this->IsRetain(node)) return true;
  } else {
    static_assert(Adapter::IsTurboshaft);
    if (!turboshaft::ShouldSkipOptimizationStep() &&
        turboshaft::ShouldSkipOperation(this->Get(node))) {
      return false;
    }
  }
  if (this->IsRequiredWhenUnused(node)) return true;
  return used_.Contains(this->id(node));
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsReallyUsed(node_t node) const {
  DCHECK(this->valid(node));
  if constexpr (Adapter::IsTurbofan) {
    // TODO(bmeurer): This is a terrible monster hack, but we have to make sure
    // that the Retain is actually emitted, otherwise the GC will mess up.
    if (this->IsRetain(node)) return true;
  } else {
    static_assert(Adapter::IsTurboshaft);
    if (!turboshaft::ShouldSkipOptimizationStep() &&
        turboshaft::ShouldSkipOperation(this->Get(node))) {
      return false;
    }
  }
  return used_.Contains(this->id(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::MarkAsUsed(node_t node) {
  DCHECK(this->valid(node));
  used_.Add(this->id(node));
}

template <typename Adapter>
int InstructionSelectorT<Adapter>::GetEffectLevel(node_t node) const {
  DCHECK(this->valid(node));
  size_t const id = this->id(node);
  DCHECK_LT(id, effect_level_.size());
  return effect_level_[id];
}

template <typename Adapter>
int InstructionSelectorT<Adapter>::GetEffectLevel(
    node_t node, FlagsContinuation* cont) const {
  return cont->IsBranch() ? GetEffectLevel(this->block_terminator(
                                this->PredecessorAt(cont->true_block(), 0)))
                          : GetEffectLevel(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::SetEffectLevel(node_t node,
                                                   int effect_level) {
  DCHECK(this->valid(node));
  size_t const id = this->id(node);
  DCHECK_LT(id, effect_level_.size());
  effect_level_[id] = effect_level;
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::CanAddressRelativeToRootsRegister(
    const ExternalReference& reference) const {
  // There are three things to consider here:
  // 1. CanUseRootsRegister: Is kRootRegister initialized?
  const bool root_register_is_available_and_initialized = CanUseRootsRegister();
  if (!root_register_is_available_and_initialized) return false;

  // 2. enable_roots_relative_addressing_: Can we address everything on the heap
  //    through the root register, i.e. are root-relative addresses to arbitrary
  //    addresses guaranteed not to change between code generation and
  //    execution?
  const bool all_root_relative_offsets_are_constant =
      (enable_roots_relative_addressing_ ==
       InstructionSelector::kEnableRootsRelativeAddressing);
  if (all_root_relative_offsets_are_constant) return true;

  // 3. IsAddressableThroughRootRegister: Is the target address guaranteed to
  //    have a fixed root-relative offset? If so, we can ignore 2.
  const bool this_root_relative_offset_is_constant =
      MacroAssemblerBase::IsAddressableThroughRootRegister(isolate(),
                                                           reference);
  return this_root_relative_offset_is_constant;
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::CanUseRootsRegister() const {
  return linkage()->GetIncomingDescriptor()->flags() &
         CallDescriptor::kCanUseRoots;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::MarkAsRepresentation(
    MachineRepresentation rep, const InstructionOperand& op) {
  UnallocatedOperand unalloc = UnallocatedOperand::cast(op);
  sequence()->MarkAsRepresentation(rep, unalloc.virtual_register());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::MarkAsRepresentation(
    MachineRepresentation rep, node_t node) {
  sequence()->MarkAsRepresentation(rep, GetVirtualRegister(node));
}

namespace {

InstructionOperand OperandForDeopt(Isolate* isolate,
                                   OperandGeneratorT<TurboshaftAdapter>* g,
                                   turboshaft::OpIndex input,
                                   FrameStateInputKind kind,
                                   MachineRepresentation rep) {
  if (rep == MachineRepresentation::kNone) {
    return g->TempImmediate(FrameStateDescriptor::kImpossibleValue);
  }

  const turboshaft::Operation& op = g->turboshaft_graph()->Get(input);
  if (const turboshaft::ConstantOp* constant =
          op.TryCast<turboshaft::ConstantOp>()) {
    using Kind = turboshaft::ConstantOp::Kind;
    switch (constant->kind) {
      case Kind::kWord32:
      case Kind::kWord64:
      case Kind::kSmi:
      case Kind::kFloat32:
      case Kind::kFloat64:
        return g->UseImmediate(input);
      case Kind::kNumber:
        if (rep == MachineRepresentation::kWord32) {
          const double d = constant->number().get_scalar();
          Tagged<Smi> smi = Smi::FromInt(static_cast<int32_t>(d));
          CHECK_EQ(smi.value(), d);
          return g->UseImmediate(static_cast<int32_t>(smi.ptr()));
        }
        return g->UseImmediate(input);
      case turboshaft::ConstantOp::Kind::kHeapObject:
      case turboshaft::ConstantOp::Kind::kCompressedHeapObject:
      case turboshaft::ConstantOp::Kind::kTrustedHeapObject: {
        if (!CanBeTaggedOrCompressedPointer(rep)) {
          // If we have inconsistent static and dynamic types, e.g. if we
          // smi-check a string, we can get here with a heap object that
          // says it is a smi. In that case, we return an invalid instruction
          // operand, which will be interpreted as an optimized-out value.

          // TODO(jarin) Ideally, we should turn the current instruction
          // into an abort (we should never execute it).
          return InstructionOperand();
        }

        Handle<HeapObject> object = constant->handle();
        RootIndex root_index;
        if (isolate->roots_table().IsRootHandle(object, &root_index) &&
            root_index == RootIndex::kOptimizedOut) {
          // For an optimized-out object we return an invalid instruction
          // operand, so that we take the fast path for optimized-out values.
          return InstructionOperand();
        }

        return g->UseImmediate(input);
      }
      default:
        UNIMPLEMENTED();
    }
  } else if (const turboshaft::TaggedBitcastOp* bitcast =
                 op.TryCast<turboshaft::Opmask::kTaggedBitcastSmi>()) {
    const turboshaft::Operation& input = g->Get(bitcast->input());
    if (const turboshaft::ConstantOp* cst =
            input.TryCast<turboshaft::Opmask::kWord32Constant>()) {
      if constexpr (Is64()) {
        return g->UseImmediate64(cst->word32());
      } else {
        return g->UseImmediate(cst->word32());
      }
    } else if (Is64() && input.Is<turboshaft::Opmask::kWord64Constant>()) {
      if (rep == MachineRepresentation::kWord32) {
        return g->UseImmediate(input.Cast<turboshaft::ConstantOp>().word32());
      } else {
        return g->UseImmediate64(input.Cast<turboshaft::ConstantOp>().word64());
      }
    }
  }

  switch (kind) {
    case FrameStateInputKind::kStackSlot:
      return g->UseUniqueSlot(input);
    case FrameStateInputKind::kAny:
      // Currently deopts "wrap" other operations, so the deopt's inputs
      // are potentially needed until the end of the deoptimising code.
      return g->UseAnyAtEnd(input);
  }
}

InstructionOperand OperandForDeopt(Isolate* isolate,
                                   OperandGeneratorT<TurbofanAdapter>* g,
                                   Node* input, FrameStateInputKind kind,
                                   MachineRepresentation rep) {
  if (rep == MachineRepresentation::kNone) {
    return g->TempImmediate(FrameStateDescriptor::kImpossibleValue);
  }

  switch (input->opcode()) {
    case IrOpcode::kInt32Constant:
    case IrOpcode::kInt64Constant:
    case IrOpcode::kFloat32Constant:
    case IrOpcode::kFloat64Constant:
      return g->UseImmediate(input);
    case IrOpcode::kNumberConstant:
      if (rep == MachineRepresentation::kWord32) {
        Tagged<Smi> smi = NumberConstantToSmi(input);
        return g->UseImmediate(static_cast<int32_t>(smi.ptr()));
      } else {
        return g->UseImmediate(input);
      }
    case IrOpcode::kHeapConstant:
    case IrOpcode::kCompressedHeapConstant:
    case IrOpcode::kTrustedHeapConstant: {
      if (!CanBeTaggedOrCompressedPointer(rep)) {
        // If we have inconsistent static and dynamic types, e.g. if we
        // smi-check a string, we can get here with a heap object that
        // says it is a smi. In that case, we return an invalid instruction
        // operand, which will be interpreted as an optimized-out value.

        // TODO(jarin) Ideally, we should turn the current instruction
        // into an abort (we should never execute it).
        return InstructionOperand();
      }

      Handle<HeapObject> constant = HeapConstantOf(input->op());
      RootIndex root_index;
      if (isolate->roots_table().IsRootHandle(constant, &root_index) &&
          root_index == RootIndex::kOptimizedOut) {
        // For an optimized-out object we return an invalid instruction
        // operand, so that we take the fast path for optimized-out values.
        return InstructionOperand();
      }

      return g->UseImmediate(input);
    }
    case IrOpcode::kArgumentsElementsState:
    case IrOpcode::kArgumentsLengthState:
    case IrOpcode::kObjectState:
    case IrOpcode::kTypedObjectState:
      UNREACHABLE();
    case IrOpcode::kBitcastWordToTaggedSigned: {
      if (input->InputAt(0)->opcode() == IrOpcode::kInt32Constant) {
        int32_t value = OpParameter<int32_t>(input->InputAt(0)->op());
        if constexpr (Is64()) {
          return g->UseImmediate64(value);
        } else {
          return g->UseImmediate(value);
        }
      } else if (Is64() &&
                 input->InputAt(0)->opcode() == IrOpcode::kInt64Constant) {
        int64_t value = OpParameter<int64_t>(input->InputAt(0)->op());
        if (rep == MachineRepresentation::kWord32) {
          return g->UseImmediate(static_cast<int>(value));
        } else {
          return g->UseImmediate64(value);
        }
      }
    }
      [[fallthrough]];
    default:
      switch (kind) {
        case FrameStateInputKind::kStackSlot:
          return g->UseUniqueSlot(input);
        case FrameStateInputKind::kAny:
          // Currently deopts "wrap" other operations, so the deopt's inputs
          // are potentially needed until the end of the deoptimising code.
          return g->UseAnyAtEnd(input);
      }
  }
  UNREACHABLE();
}

}  // namespace

class TurbofanStateObjectDeduplicator {
 public:
  explicit TurbofanStateObjectDeduplicator(Zone* zone) : objects_(zone) {}
  static const size_t kNotDuplicated = SIZE_MAX;

  size_t GetObjectId(Node* node) {
    DCHECK(node->opcode() == IrOpcode::kTypedObjectState ||
           node->opcode() == IrOpcode::kObjectId ||
           node->opcode() == IrOpcode::kArgumentsElementsState);
    for (size_t i = 0; i < objects_.size(); ++i) {
      if (objects_[i] == node) return i;
      // ObjectId nodes are the Turbofan way to express objects with the same
      // identity in the deopt info. So they should always be mapped to
      // previously appearing TypedObjectState nodes.
      if (HasObjectId(objects_[i]) && HasObjectId(node) &&
          ObjectIdOf(objects_[i]->op()) == ObjectIdOf(node->op())) {
        return i;
      }
    }
    DCHECK(node->opcode() == IrOpcode::kTypedObjectState ||
           node->opcode() == IrOpcode::kArgumentsElementsState);
    return kNotDuplicated;
  }

  size_t InsertObject(Node* node) {
    DCHECK(node->opcode() == IrOpcode::kTypedObjectState ||
           node->opcode() == IrOpcode::kObjectId ||
           node->opcode() == IrOpcode::kArgumentsElementsState);
    size_t id = objects_.size();
    objects_.push_back(node);
    return id;
  }

  size_t size() const { return objects_.size(); }

 private:
  static bool HasObjectId(Node* node) {
    return node->opcode() == IrOpcode::kTypedObjectState ||
           node->opcode() == IrOpcode::kObjectId;
  }

  ZoneVector<Node*> objects_;
};

class TurboshaftStateObjectDeduplicator {
 public:
  explicit TurboshaftStateObjectDeduplicator(Zone* zone) : object_ids_(zone) {}
  static constexpr uint32_t kArgumentsElementsDummy =
      std::numeric_limits<uint32_t>::max();
  static constexpr size_t kNotDuplicated = std::numeric_limits<size_t>::max();

  size_t GetObjectId(uint32_t object) {
    for (size_t i = 0; i < object_ids_.size(); ++i) {
      if (object_ids_[i] == object) return i;
    }
    return kNotDuplicated;
  }

  size_t InsertObject(uint32_t object) {
    object_ids_.push_back(object);
    return object_ids_.size() - 1;
  }

  void InsertDummyForArgumentsElements() {
    object_ids_.push_back(kArgumentsElementsDummy);
  }

  size_t size() const { return object_ids_.size(); }

 private:
  ZoneVector<uint32_t> object_ids_;
};

// Returns the number of instruction operands added to inputs.
template <>
size_t InstructionSelectorT<TurbofanAdapter>::AddOperandToStateValueDescriptor(
    StateValueList* values, InstructionOperandVector* inputs,
    OperandGeneratorT<TurbofanAdapter>* g,
    StateObjectDeduplicator* deduplicator, Node* input, MachineType type,
    FrameStateInputKind kind, Zone* zone) {
  DCHECK_NOT_NULL(input);
  switch (input->opcode()) {
    case IrOpcode::kArgumentsElementsState: {
      values->PushArgumentsElements(ArgumentsStateTypeOf(input->op()));
      // The elements backing store of an arguments object participates in the
      // duplicate object counting, but can itself never appear duplicated.
      DCHECK_EQ(StateObjectDeduplicator::kNotDuplicated,
                deduplicator->GetObjectId(input));
      deduplicator->InsertObject(input);
      return 0;
    }
    case IrOpcode::kArgumentsLengthState: {
      values->PushArgumentsLength();
      return 0;
    }
    case IrOpcode::kObjectState:
      UNREACHABLE();
    case IrOpcode::kTypedObjectState:
    case IrOpcode::kObjectId: {
      size_t id = deduplicator->GetObjectId(input);
      if (id == StateObjectDeduplicator::kNotDuplicated) {
        DCHECK_EQ(IrOpcode::kTypedObjectState, input->opcode());
        size_t entries = 0;
        id = deduplicator->InsertObject(input);
        StateValueList* nested = values->PushRecursiveField(zone, id);
        int const input_count = input->op()->ValueInputCount();
        ZoneVector<MachineType> const* types = MachineTypesOf(input->op());
        for (int i = 0; i < input_count; ++i) {
          entries += AddOperandToStateValueDescriptor(
              nested, inputs, g, deduplicator, input->InputAt(i), types->at(i),
              kind, zone);
        }
        return entries;
      } else {
        // Deoptimizer counts duplicate objects for the running id, so we have
        // to push the input again.
        deduplicator->InsertObject(input);
        values->PushDuplicate(id);
        return 0;
      }
    }
    default: {
      InstructionOperand op =
          OperandForDeopt(isolate(), g, input, kind, type.representation());
      if (op.kind() == InstructionOperand::INVALID) {
        // Invalid operand means the value is impossible or optimized-out.
        values->PushOptimizedOut();
        return 0;
      } else {
        inputs->push_back(op);
        values->PushPlain(type);
        return 1;
      }
    }
  }
}

template <typename Adapter>
struct InstructionSelectorT<Adapter>::CachedStateValues : public ZoneObject {
 public:
  CachedStateValues(Zone* zone, StateValueList* values, size_t values_start,
                    InstructionOperandVector* inputs, size_t inputs_start)
      : inputs_(inputs->begin() + inputs_start, inputs->end(), zone),
        values_(values->MakeSlice(values_start)) {}

  size_t Emit(InstructionOperandVector* inputs, StateValueList* values) {
    inputs->insert(inputs->end(), inputs_.begin(), inputs_.end());
    values->PushCachedSlice(values_);
    return inputs_.size();
  }

 private:
  InstructionOperandVector inputs_;
  StateValueList::Slice values_;
};

template <typename Adapter>
class InstructionSelectorT<Adapter>::CachedStateValuesBuilder {
 public:
  explicit CachedStateValuesBuilder(StateValueList* values,
                                    InstructionOperandVector* inputs,
                                    StateObjectDeduplicator* deduplicator)
      : values_(values),
        inputs_(inputs),
        deduplicator_(deduplicator),
        values_start_(values->size()),
        nested_start_(values->nested_count()),
        inputs_start_(inputs->size()),
        deduplicator_start_(deduplicator->size()) {}

  // We can only build a CachedStateValues for a StateValue if it didn't update
  // any of the ids in the deduplicator.
  bool CanCache() const { return deduplicator_->size() == deduplicator_start_; }

  InstructionSelectorT<Adapter>::CachedStateValues* Build(Zone* zone) {
    DCHECK(CanCache());
    DCHECK(values_->nested_count() == nested_start_);
    return zone->New<InstructionSelectorT<Adapter>::CachedStateValues>(
        zone, values_, values_start_, inputs_, inputs_start_);
  }

 private:
  StateValueList* values_;
  InstructionOperandVector* inputs_;
  StateObjectDeduplicator* deduplicator_;
  size_t values_start_;
  size_t nested_start_;
  size_t inputs_start_;
  size_t deduplicator_start_;
};

template <>
size_t InstructionSelectorT<TurbofanAdapter>::AddInputsToFrameStateDescriptor(
    StateValueList* values, InstructionOperandVector* inputs,
    OperandGeneratorT<TurbofanAdapter>* g,
    StateObjectDeduplicator* deduplicator, node_t node,
    FrameStateInputKind kind, Zone* zone) {
  // StateValues are often shared across different nodes, and processing them
  // is expensive, so cache the result of processing a StateValue so that we
  // can quickly copy the result if we see it again.
  FrameStateInput key(node, kind);
  auto cache_entry = state_values_cache_.find(key);
  if (cache_entry != state_values_cache_.end()) {
    // Entry found in cache, emit cached version.
    return cache_entry->second->Emit(inputs, values);
  } else {
    // Not found in cache, generate and then store in cache if possible.
    size_t entries = 0;
    CachedStateValuesBuilder cache_builder(values, inputs, deduplicator);
    StateValuesAccess::iterator it = StateValuesAccess(node).begin();
    // Take advantage of sparse nature of StateValuesAccess to skip over
    // multiple empty nodes at once pushing repeated OptimizedOuts all in one
    // go.
    while (!it.done()) {
      values->PushOptimizedOut(it.AdvanceTillNotEmpty());
      if (it.done()) break;
      StateValuesAccess::TypedNode input_node = *it;
      entries += AddOperandToStateValueDescriptor(values, inputs, g,
                                                  deduplicator, input_node.node,
                                                  input_node.type, kind, zone);
      ++it;
    }
    if (cache_builder.CanCache()) {
      // Use this->zone() to build the cache entry in the instruction
      // selector's zone rather than the more long-lived instruction zone.
      state_values_cache_.emplace(key, cache_builder.Build(this->zone()));
    }
    return entries;
  }
}

size_t AddOperandToStateValueDescriptor(
    InstructionSelectorT<TurboshaftAdapter>* selector, StateValueList* values,
    InstructionOperandVector* inputs, OperandGeneratorT<TurboshaftAdapter>* g,
    TurboshaftStateObjectDeduplicator* deduplicator,
    turboshaft::FrameStateData::Iterator* it, FrameStateInputKind kind,
    Zone* zone) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  switch (it->current_instr()) {
    case FrameStateData::Instr::kUnusedRegister:
      it->ConsumeUnusedRegister();
      values->PushOptimizedOut();
      return 0;
    case FrameStateData::Instr::kInput: {
      MachineType type;
      OpIndex input;
      it->ConsumeInput(&type, &input);
      const Operation& op = selector->Get(input);
      if (op.outputs_rep()[0] == RegisterRepresentation::Word64() &&
          type.representation() == MachineRepresentation::kWord32) {
        // 64 to 32-bit conversion is implicit in turboshaft.
        // TODO(nicohartmann@): Fix this once we have explicit truncations.
        UNIMPLEMENTED();
      }
      InstructionOperand instr_op = OperandForDeopt(
          selector->isolate(), g, input, kind, type.representation());
      if (instr_op.kind() == InstructionOperand::INVALID) {
        // Invalid operand means the value is impossible or optimized-out.
        values->PushOptimizedOut();
        return 0;
      } else {
        inputs->push_back(instr_op);
        values->PushPlain(type);
        return 1;
      }
    }
    case FrameStateData::Instr::kDematerializedObject: {
      uint32_t obj_id;
      uint32_t field_count;
      it->ConsumeDematerializedObject(&obj_id, &field_count);
      size_t id = deduplicator->GetObjectId(obj_id);
      if (id == TurboshaftStateObjectDeduplicator::kNotDuplicated) {
        id = deduplicator->InsertObject(obj_id);
        size_t entries = 0;
        StateValueList* nested = values->PushRecursiveField(zone, id);
        for (uint32_t i = 0; i < field_count; ++i) {
          entries += AddOperandToStateValueDescriptor(
              selector, nested, inputs, g, deduplicator, it, kind, zone);
        }
        return entries;
      } else {
        // Deoptimizer counts duplicate objects for the running id, so we have
        // to push the input again.
        deduplicator->InsertObject(obj_id);
        values->PushDuplicate(id);
        return 0;
      }
    }
    case FrameStateData::Instr::kDematerializedObjectReference: {
      uint32_t obj_id;
      it->ConsumeDematerializedObjectReference(&obj_id);
      size_t id = deduplicator->GetObjectId(obj_id);
      DCHECK_NE(id, TurboshaftStateObjectDeduplicator::kNotDuplicated);
      // Deoptimizer counts duplicate objects for the running id, so we have
      // to push the input again.
      deduplicator->InsertObject(obj_id);
      values->PushDuplicate(id);
      return 0;
    }
    case FrameStateData::Instr::kArgumentsElements: {
      CreateArgumentsType type;
      it->ConsumeArgumentsElements(&type);
      values->PushArgumentsElements(type);
      // The elements backing store of an arguments object participates in the
      // duplicate object counting, but can itself never appear duplicated.
      deduplicator->InsertDummyForArgumentsElements();
      return 0;
    }
    case FrameStateData::Instr::kArgumentsLength:
      it->ConsumeArgumentsLength();
      values->PushArgumentsLength();
      return 0;
    case FrameStateData::Instr::kRestLength:
      it->ConsumeRestLength();
      values->PushRestLength();
      return 0;
  }
  UNREACHABLE();
}

// Returns the number of instruction operands added to inputs.
template <>
size_t InstructionSelectorT<TurboshaftAdapter>::AddInputsToFrameStateDescriptor(
    FrameStateDescriptor* descriptor, node_t state_node, OperandGenerator* g,
    TurboshaftStateObjectDeduplicator* deduplicator,
    InstructionOperandVector* inputs, FrameStateInputKind kind, Zone* zone) {
  turboshaft::FrameStateOp& state =
      schedule()->Get(state_node).template Cast<turboshaft::FrameStateOp>();
  const FrameStateInfo& info = state.data->frame_state_info;
  USE(info);
  turboshaft::FrameStateData::Iterator it =
      state.data->iterator(state.state_values());

  size_t entries = 0;
  size_t initial_size = inputs->size();
  USE(initial_size);  // initial_size is only used for debug.
  if (descriptor->outer_state()) {
    entries += AddInputsToFrameStateDescriptor(
        descriptor->outer_state(), state.parent_frame_state(), g, deduplicator,
        inputs, kind, zone);
  }

  DCHECK_EQ(descriptor->parameters_count(), info.parameter_count());
  DCHECK_EQ(descriptor->locals_count(), info.local_count());
  DCHECK_EQ(descriptor->stack_count(), info.stack_count());

  StateValueList* values_descriptor = descriptor->GetStateValueDescriptors();

  DCHECK_EQ(values_descriptor->size(), 0u);
  values_descriptor->ReserveSize(descriptor->GetSize());

  // Function
  if (descriptor->HasClosure()) {
    entries += v8::internal::compiler::AddOperandToStateValueDescriptor(
        this, values_descriptor, inputs, g, deduplicator, &it,
        FrameStateInputKind::kStackSlot, zone);
  } else {
    // Advance the iterator either way.
    MachineType unused_type;
    turboshaft::OpIndex unused_input;
    it.ConsumeInput(&unused_type, &unused_input);
  }

  // Parameters
  for (size_t i = 0; i < descriptor->parameters_count(); ++i) {
    entries += v8::internal::compiler::AddOperandToStateValueDescriptor(
        this, values_descriptor, inputs, g, deduplicator, &it, kind, zone);
  }

  // Context
  if (descriptor->HasContext()) {
    entries += v8::internal::compiler::AddOperandToStateValueDescriptor(
        this, values_descriptor, inputs, g, deduplicator, &it,
        FrameStateInputKind::kStackSlot, zone);
  } else {
    // Advance the iterator either way.
    MachineType unused_type;
    turboshaft::OpIndex unused_input;
    it.ConsumeInput(&unused_type, &unused_input);
  }

  // Locals
  for (size_t i = 0; i < descriptor->locals_count(); ++i) {
    entries += v8::internal::compiler::AddOperandToStateValueDescriptor(
        this, values_descriptor, inputs, g, deduplicator, &it, kind, zone);
  }

  // Stack
  for (size_t i = 0; i < descriptor->stack_count(); ++i) {
    entries += v8::internal::compiler::AddOperandToStateValueDescriptor(
        this, values_descriptor, inputs, g, deduplicator, &it, kind, zone);
  }

  DCHECK_EQ(initial_size + entries, inputs->size());
  return entries;
}

template <>
size_t InstructionSelectorT<TurbofanAdapter>::AddInputsToFrameStateDescriptor(
    FrameStateDescriptor* descriptor, node_t state_node, OperandGenerator* g,
    StateObjectDeduplicator* deduplicator, InstructionOperandVector* inputs,
    FrameStateInputKind kind, Zone* zone) {
  FrameState state{state_node};
  size_t entries = 0;
  size_t initial_size = inputs->size();
  USE(initial_size);  // initial_size is only used for debug.

  if (descriptor->outer_state()) {
    entries += AddInputsToFrameStateDescriptor(
        descriptor->outer_state(), FrameState{state.outer_frame_state()}, g,
        deduplicator, inputs, kind, zone);
  }

  Node* parameters = state.parameters();
  Node* locals = state.locals();
  Node* stack = state.stack();
  Node* context = state.context();
  Node* function = state.function();

  DCHECK_EQ(descriptor->parameters_count(),
            StateValuesAccess(parameters).size());
  DCHECK_EQ(descriptor->locals_count(), StateValuesAccess(locals).size());
  DCHECK_EQ(descriptor->stack_count(), StateValuesAccess(stack).size());

  StateValueList* values_descriptor = descriptor->GetStateValueDescriptors();

  DCHECK_EQ(values_descriptor->size(), 0u);
  values_descriptor->ReserveSize(descriptor->GetSize());

  if (descriptor->HasClosure()) {
    DCHECK_NOT_NULL(function);
    entries += AddOperandToStateValueDescriptor(
        values_descriptor, inputs, g, deduplicator, function,
        MachineType::AnyTagged(), FrameStateInputKind::kStackSlot, zone);
  }

  entries += AddInputsToFrameStateDescriptor(
      values_descriptor, inputs, g, deduplicator, parameters, kind, zone);

  if (descriptor->HasContext()) {
    DCHECK_NOT_NULL(context);
    entries += AddOperandToStateValueDescriptor(
        values_descriptor, inputs, g, deduplicator, context,
        MachineType::AnyTagged(), FrameStateInputKind::kStackSlot, zone);
  }

  entries += AddInputsToFrameStateDescriptor(values_descriptor, inputs, g,
                                             deduplicator, locals, kind, zone);

  entries += AddInputsToFrameStateDescriptor(values_descriptor, inputs, g,
                                             deduplicator, stack, kind, zone);
  DCHECK_EQ(initial_size + entries, inputs->size());
  return entries;
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::EmitWithContinuation(
    InstructionCode opcode, InstructionOperand a, FlagsContinuation* cont) {
  return EmitWithContinuation(opcode, 0, nullptr, 1, &a, cont);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::EmitWithContinuation(
    InstructionCode opcode, InstructionOperand a, InstructionOperand b,
    FlagsContinuation* cont) {
  InstructionOperand inputs[] = {a, b};
  return EmitWithContinuation(opcode, 0, nullptr, arraysize(inputs), inputs,
                              cont);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::EmitWithContinuation(
    InstructionCode opcode, InstructionOperand a, InstructionOperand b,
    InstructionOperand c, FlagsContinuation* cont) {
  InstructionOperand inputs[] = {a, b, c};
  return EmitWithContinuation(opcode, 0, nullptr, arraysize(inputs), inputs,
                              cont);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::EmitWithContinuation(
    InstructionCode opcode, size_t output_count, InstructionOperand* outputs,
    size_t input_count, InstructionOperand* inputs, FlagsContinuation* cont) {
  return EmitWithContinuation(opcode, output_count, outputs, input_count,
                              inputs, 0, nullptr, cont);
}

template <typename Adapter>
Instruction* InstructionSelectorT<Adapter>::EmitWithContinuation(
    InstructionCode opcode, size_t output_count, InstructionOperand* outputs,
    size_t input_count, InstructionOperand* inputs, size_t temp_count,
    InstructionOperand* temps, FlagsContinuation* cont) {
  OperandGenerator g(this);

  opcode = cont->Encode(opcode);

  continuation_inputs_.resize(0);
  for (size_t i = 0; i < input_count; i++) {
    continuation_inputs_.push_back(inputs[i]);
  }

  continuation_outputs_.resize(0);
  for (size_t i = 0; i < output_count; i++) {
    continuation_outputs_.push_back(outputs[i]);
  }

  continuation_temps_.resize(0);
  for (size_t i = 0; i < temp_count; i++) {
    continuation_temps_.push_back(temps[i]);
  }

  if (cont->IsBranch() || cont->IsConditionalBranch()) {
    continuation_inputs_.push_back(g.Label(cont->true_block()));
    continuation_inputs_.push_back(g.Label(cont->false_block()));
  } else if (cont->IsDeoptimize()) {
    int immediate_args_count = 0;
    opcode |= DeoptImmedArgsCountField::encode(immediate_args_count) |
              DeoptFrameStateOffsetField::encode(static_cast<int>(input_count));
    AppendDeoptimizeArguments(&continuation_inputs_, cont->reason(),
                              cont->node_id(), cont->feedback(),
                              cont->frame_state());
  } else if (cont->IsSet() || cont->IsConditionalSet()) {
    continuation_outputs_.push_back(g.DefineAsRegister(cont->result()));
  } else if (cont->IsSelect()) {
    // The {Select} should put one of two values into the output register,
    // depending on the result of the condition. The two result values are in
    // the last two input slots, the {false_value} in {input_count - 2}, and the
    // true_value in {input_count - 1}. The other inputs are used for the
    // condition.
    AddOutputToSelectContinuation(&g, static_cast<int>(input_count) - 2,
                                  cont->result());
  } else if (cont->IsTrap()) {
    int trap_id = static_cast<int>(cont->trap_id());
    continuation_inputs_.push_back(g.UseImmediate(trap_id));
  } else {
    DCHECK(cont->IsNone());
  }

  size_t const emit_inputs_size = continuation_inputs_.size();
  auto* emit_inputs =
      emit_inputs_size ? &continuation_inputs_.front() : nullptr;
  size_t const emit_outputs_size = continuation_outputs_.size();
  auto* emit_outputs =
      emit_outputs_size ? &continuation_outputs_.front() : nullptr;
  size_t const emit_temps_size = continuation_temps_.size();
  auto* emit_temps = emit_temps_size ? &continuation_temps_.front() : nullptr;
  return Emit(opcode, emit_outputs_size, emit_outputs, emit_inputs_size,
              emit_inputs, emit_temps_size, emit_temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::AppendDeoptimizeArguments(
    InstructionOperandVector* args, DeoptimizeReason reason, id_t node_id,
    FeedbackSource const& feedback, node_t frame_state, DeoptimizeKind kind) {
  OperandGenerator g(this);
  FrameStateDescriptor* const descriptor = GetFrameStateDescriptor(frame_state);
  int const state_id = sequence()->AddDeoptimizationEntry(
      descriptor, kind, reason, node_id, feedback);
  args->push_back(g.TempImmediate(state_id));
  StateObjectDeduplicator deduplicator(instruction_zone());
  AddInputsToFrameStateDescriptor(descriptor, frame_state, &g, &deduplicator,
                                  args, FrameStateInputKind::kAny,
                                  instruction_zone());
}

// An internal helper class for generating the operands to calls.
// TODO(bmeurer): Get rid of the CallBuffer business and make
// InstructionSelector::VisitCall platform independent instead.
template <typename Adapter>
struct CallBufferT {
  using PushParameter = PushParameterT<Adapter>;
  CallBufferT(Zone* zone, const CallDescriptor* call_descriptor,
              FrameStateDescriptor* frame_state)
      : descriptor(call_descriptor),
        frame_state_descriptor(frame_state),
        output_nodes(zone),
        outputs(zone),
        instruction_args(zone),
        pushed_nodes(zone) {
    output_nodes.reserve(call_descriptor->ReturnCount());
    outputs.reserve(call_descriptor->ReturnCount());
    pushed_nodes.reserve(input_count());
    instruction_args.reserve(input_count() + frame_state_value_count());
  }

  const CallDescriptor* descriptor;
  FrameStateDescriptor* frame_state_descriptor;
  ZoneVector<PushParameter> output_nodes;
  InstructionOperandVector outputs;
  InstructionOperandVector instruction_args;
  ZoneVector<PushParameter> pushed_nodes;

  size_t input_count() const { return descriptor->InputCount(); }

  size_t frame_state_count() const { return descriptor->FrameStateCount(); }

  size_t frame_state_value_count() const {
    return (frame_state_descriptor == nullptr)
               ? 0
               : (frame_state_descriptor->GetTotalSize() +
                  1);  // Include deopt id.
  }
};

// TODO(bmeurer): Get rid of the CallBuffer business and make
// InstructionSelector::VisitCall platform independent instead.
template <typename Adapter>
void InstructionSelectorT<Adapter>::InitializeCallBuffer(
    node_t node, CallBuffer* buffer, CallBufferFlags flags,
    int stack_param_delta) {
  OperandGenerator g(this);
  size_t ret_count = buffer->descriptor->ReturnCount();
  bool is_tail_call = (flags & kCallTail) != 0;
  auto call = this->call_view(node);
  DCHECK_LE(call.return_count(), ret_count);

  if (ret_count > 0) {
    // Collect the projections that represent multiple outputs from this call.
    if (ret_count == 1) {
      PushParameter result = {call, buffer->descriptor->GetReturnLocation(0)};
      buffer->output_nodes.push_back(result);
    } else {
      buffer->output_nodes.resize(ret_count);
      for (size_t i = 0; i < ret_count; ++i) {
        LinkageLocation location = buffer->descriptor->GetReturnLocation(i);
        buffer->output_nodes[i] = PushParameter({}, location);
      }
      if constexpr (Adapter::IsTurboshaft) {
        for (turboshaft::OpIndex call_use : turboshaft_uses(call)) {
          const turboshaft::Operation& use_op = this->Get(call_use);
          if (use_op.Is<turboshaft::DidntThrowOp>()) {
            for (turboshaft::OpIndex use : turboshaft_uses(call_use)) {
              DCHECK(this->is_projection(use));
              size_t index = this->projection_index_of(use);
              DCHECK_LT(index, buffer->output_nodes.size());
              DCHECK(!Adapter::valid(buffer->output_nodes[index].node));
              buffer->output_nodes[index].node = use;
            }
          } else {
            DCHECK(use_op.Is<turboshaft::CheckExceptionOp>());
          }
        }
      } else {
        for (Edge const edge : ((node_t)call)->use_edges()) {
          if (!NodeProperties::IsValueEdge(edge)) continue;
          Node* node = edge.from();
          DCHECK_EQ(IrOpcode::kProjection, node->opcode());
          size_t const index = ProjectionIndexOf(node->op());

          DCHECK_LT(index, buffer->output_nodes.size());
          DCHECK(!buffer->output_nodes[index].node);
          buffer->output_nodes[index].node = node;
        }
      }
      frame_->EnsureReturnSlots(
          static_cast<int>(buffer->descriptor->ReturnSlotCount()));
    }

    // Filter out the outputs that aren't live because no projection uses them.
    size_t outputs_needed_by_framestate =
        buffer->frame_state_descriptor == nullptr
            ? 0
            : buffer->frame_state_descriptor->state_combine()
                  .ConsumedOutputCount();
    for (size_t i = 0; i < buffer->output_nodes.size(); i++) {
      bool output_is_live = this->valid(buffer->output_nodes[i].node) ||
                            i < outputs_needed_by_framestate;
      if (output_is_live) {
        LinkageLocation location = buffer->output_nodes[i].location;
        MachineRepresentation rep = location.GetType().representation();

        node_t output = buffer->output_nodes[i].node;
        InstructionOperand op = !this->valid(output)
                                    ? g.TempLocation(location)
                                    : g.DefineAsLocation(output, location);
        MarkAsRepresentation(rep, op);

        if (!UnallocatedOperand::cast(op).HasFixedSlotPolicy()) {
          buffer->outputs.push_back(op);
          buffer->output_nodes[i].node = {};
        }
      }
    }
  }

  // The first argument is always the callee code.
  node_t callee = call.callee();
  bool call_code_immediate = (flags & kCallCodeImmediate) != 0;
  bool call_address_immediate = (flags & kCallAddressImmediate) != 0;
  bool call_use_fixed_target_reg = (flags & kCallFixedTargetRegister) != 0;
  switch (buffer->descriptor->kind()) {
    case CallDescriptor::kCallCodeObject:
      buffer->instruction_args.push_back(
          (call_code_immediate && this->IsHeapConstant(callee))
              ? g.UseImmediate(callee)
          : call_use_fixed_target_reg
              ? g.UseFixed(callee, kJavaScriptCallCodeStartRegister)
              : g.UseRegister(callee));
      break;
    case CallDescriptor::kCallAddress:
      buffer->instruction_args.push_back(
          (call_address_immediate && this->IsExternalConstant(callee))
              ? g.UseImmediate(callee)
          : call_use_fixed_target_reg
              ? g.UseFixed(callee, kJavaScriptCallCodeStartRegister)
              : g.UseRegister(callee));
      break;
#if V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallWasmCapiFunction:
    case CallDescriptor::kCallWasmFunction:
    case CallDescriptor::kCallWasmImportWrapper:
      buffer->instruction_args.push_back(
          (call_address_immediate && this->IsRelocatableWasmConstant(callee))
              ? g.UseImmediate(callee)
          : call_use_fixed_target_reg
              ? g.UseFixed(callee, kJavaScriptCallCodeStartRegister)
              : g.UseRegister(callee));
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallBuiltinPointer: {
      // The common case for builtin pointers is to have the target in a
      // register. If we have a constant, we use a register anyway to simplify
      // related code.
      LinkageLocation location = buffer->descriptor->GetInputLocation(0);
      bool location_is_fixed_register =
          location.IsRegister() && !location.IsAnyRegister();
      InstructionOperand op;
      // If earlier phases specified a particular register, don't override
      // their choice.
      if (location_is_fixed_register) {
        op = g.UseLocation(callee, location);
      } else if (call_use_fixed_target_reg) {
        op = g.UseFixed(callee, kJavaScriptCallCodeStartRegister);
      } else {
        op = g.UseRegister(callee);
      }
      buffer->instruction_args.push_back(op);
      break;
    }
    case CallDescriptor::kCallJSFunction:
      buffer->instruction_args.push_back(
          g.UseLocation(callee, buffer->descriptor->GetInputLocation(0)));
      break;
  }
  DCHECK_EQ(1u, buffer->instruction_args.size());

  // If the call needs a frame state, we insert the state information as
  // follows (n is the number of value inputs to the frame state):
  // arg 1               : deoptimization id.
  // arg 2 - arg (n + 2) : value inputs to the frame state.
  size_t frame_state_entries = 0;
  USE(frame_state_entries);  // frame_state_entries is only used for debug.
  if (buffer->frame_state_descriptor != nullptr) {
    node_t frame_state = call.frame_state();

    // If it was a syntactic tail call we need to drop the current frame and
    // all the frames on top of it that are either inlined extra arguments
    // or a tail caller frame.
    if (is_tail_call) {
      frame_state = this->parent_frame_state(frame_state);
      buffer->frame_state_descriptor =
          buffer->frame_state_descriptor->outer_state();
      while (buffer->frame_state_descriptor != nullptr &&
             buffer->frame_state_descriptor->type() ==
                 FrameStateType::kInlinedExtraArguments) {
        frame_state = this->parent_frame_state(frame_state);
        buffer->frame_state_descriptor =
            buffer->frame_state_descriptor->outer_state();
      }
    }

    int const state_id = sequence()->AddDeoptimizationEntry(
        buffer->frame_state_descriptor, DeoptimizeKind::kLazy,
        DeoptimizeReason::kUnknown, this->id(call), FeedbackSource());
    buffer->instruction_args.push_back(g.TempImmediate(state_id));

    StateObjectDeduplicator deduplicator(instruction_zone());

    frame_state_entries =
        1 + AddInputsToFrameStateDescriptor(
                buffer->frame_state_descriptor, frame_state, &g, &deduplicator,
                &buffer->instruction_args, FrameStateInputKind::kStackSlot,
                instruction_zone());

    DCHECK_EQ(1 + frame_state_entries, buffer->instruction_args.size());
  }

  size_t input_count = static_cast<size_t>(buffer->input_count());

  // Split the arguments into pushed_nodes and instruction_args. Pushed
  // arguments require an explicit push instruction before the call and do
  // not appear as arguments to the call. Everything else ends up
  // as an InstructionOperand argument to the call.
  auto arguments = call.arguments();
  auto iter(arguments.begin());
  // call->inputs().begin());
  size_t pushed_count = 0;
  for (size_t index = 1; index < input_count; ++iter, ++index) {
    DCHECK_NE(iter, arguments.end());

    LinkageLocation location = buffer->descriptor->GetInputLocation(index);
    if (is_tail_call) {
      location = LinkageLocation::ConvertToTailCallerLocation(
          location, stack_param_delta);
    }
    InstructionOperand op = g.UseLocation(*iter, location);
    UnallocatedOperand unallocated = UnallocatedOperand::cast(op);
    if (unallocated.HasFixedSlotPolicy() && !is_tail_call) {
      int stack_index = buffer->descriptor->GetStackIndexFromSlot(
          unallocated.fixed_slot_index());
      // This can insert empty slots before stack_index and will insert enough
      // slots after stack_index to store the parameter.
      if (static_cast<size_t>(stack_index) >= buffer->pushed_nodes.size()) {
        int num_slots = location.GetSizeInPointers();
        buffer->pushed_nodes.resize(stack_index + num_slots);
      }
      PushParameter param = {*iter, location};
      buffer->pushed_nodes[stack_index] = param;
      pushed_count++;
    } else {
      if (location.IsNullRegister()) {
        EmitMoveFPRToParam(&op, location);
      }
      buffer->instruction_args.push_back(op);
    }
  }
  DCHECK_EQ(input_count, buffer->instruction_args.size() + pushed_count -
                             frame_state_entries);
  USE(pushed_count);
  if (V8_TARGET_ARCH_STORES_RETURN_ADDRESS_ON_STACK && is_tail_call &&
      stack_param_delta != 0) {
    // For tail calls that change the size of their parameter list and keep
    // their return address on the stack, move the return address to just above
    // the parameters.
    LinkageLocation saved_return_location =
        LinkageLocation::ForSavedCallerReturnAddress();
    InstructionOperand return_address =
        g.UsePointerLocation(LinkageLocation::ConvertToTailCallerLocation(
                                 saved_return_location, stack_param_delta),
                             saved_return_location);
    buffer->instruction_args.push_back(return_address);
  }
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsSourcePositionUsed(node_t node) {
  if (source_position_mode_ == InstructionSelector::kAllSourcePositions) {
    return true;
  }
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& operation = this->Get(node);
    // DidntThrow is where the actual call is generated.
    if (operation.Is<DidntThrowOp>()) return true;
    if (const LoadOp* load = operation.TryCast<LoadOp>()) {
      return load->kind.with_trap_handler;
    }
    if (const StoreOp* store = operation.TryCast<StoreOp>()) {
      return store->kind.with_trap_handler;
    }
#if V8_ENABLE_WEBASSEMBLY
    if (operation.Is<TrapIfOp>()) return true;
    if (const AtomicRMWOp* rmw = operation.TryCast<AtomicRMWOp>()) {
      return rmw->memory_access_kind == MemoryAccessKind::kProtected;
    }
    if (const Simd128LoadTransformOp* lt =
            operation.TryCast<Simd128LoadTransformOp>()) {
      return lt->load_kind.with_trap_handler;
    }
#if V8_ENABLE_WASM_SIMD256_REVEC
    if (const Simd256LoadTransformOp* lt =
            operation.TryCast<Simd256LoadTransformOp>()) {
      return lt->load_kind.with_trap_handler;
    }
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
    if (const Simd128LaneMemoryOp* lm =
            operation.TryCast<Simd128LaneMemoryOp>()) {
      return lm->kind.with_trap_handler;
    }
#endif
    if (additional_protected_instructions_->Contains(this->id(node))) {
      return true;
    }
    return false;
  } else {
    switch (node->opcode()) {
      case IrOpcode::kCall:
      case IrOpcode::kTrapIf:
      case IrOpcode::kTrapUnless:
      case IrOpcode::kProtectedLoad:
      case IrOpcode::kProtectedStore:
      case IrOpcode::kLoadTrapOnNull:
      case IrOpcode::kStoreTrapOnNull:
#if V8_ENABLE_WEBASSEMBLY
      case IrOpcode::kLoadTransform:
      case IrOpcode::kLoadLane:
      case IrOpcode::kStoreLane:
#endif  // V8_ENABLE_WEBASSEMBLY
      case IrOpcode::kLoad:
      case IrOpcode::kStore:
      case IrOpcode::kWord32AtomicLoad:
      case IrOpcode::kWord32AtomicStore:
      case IrOpcode::kWord32AtomicAdd:
      case IrOpcode::kWord32AtomicSub:
      case IrOpcode::kWord32AtomicAnd:
      case IrOpcode::kWord32AtomicOr:
      case IrOpcode::kWord32AtomicXor:
      case IrOpcode::kWord32AtomicExchange:
      case IrOpcode::kWord32AtomicCompareExchange:
      case IrOpcode::kWord64AtomicLoad:
      case IrOpcode::kWord64AtomicStore:
      case IrOpcode::kWord64AtomicAdd:
      case IrOpcode::kWord64AtomicSub:
      case IrOpcode::kWord64AtomicAnd:
      case IrOpcode::kWord64AtomicOr:
      case IrOpcode::kWord64AtomicXor:
      case IrOpcode::kWord64AtomicExchange:
      case IrOpcode::kWord64AtomicCompareExchange:
      case IrOpcode::kUnalignedLoad:
      case IrOpcode::kUnalignedStore:
        return true;
      default:
        return false;
    }
  }
}

namespace {
bool increment_effect_level_for_node(TurbofanAdapter* adapter, Node* node) {
  const IrOpcode::Value opcode = node->opcode();
  return opcode == IrOpcode::kStore || opcode == IrOpcode::kUnalignedStore ||
         opcode == IrOpcode::kCall || opcode == IrOpcode::kProtectedStore ||
         opcode == IrOpcode::kStoreTrapOnNull ||
#if V8_ENABLE_WEBASSEMBLY
         opcode == IrOpcode::kStoreLane ||
#endif
         opcode == IrOpcode::kStorePair ||
         opcode == IrOpcode::kStoreIndirectPointer ||
#define ADD_EFFECT_FOR_ATOMIC_OP(Opcode) opcode == IrOpcode::k##Opcode ||
         MACHINE_ATOMIC_OP_LIST(ADD_EFFECT_FOR_ATOMIC_OP)
#undef ADD_EFFECT_FOR_ATOMIC_OP
                 opcode == IrOpcode::kMemoryBarrier;
}

bool increment_effect_level_for_node(TurboshaftAdapter* adapter,
                                     turboshaft::OpIndex node) {
  // We need to increment the effect level if the operation consumes any of the
  // dimensions of the {kTurboshaftEffectLevelMask}.
  const turboshaft::Operation& op = adapter->Get(node);
  if (op.Is<turboshaft::RetainOp>()) {
    // Retain has CanWrite effect so that it's not reordered before the last
    // read it protects, but it shouldn't increment the effect level, since
    // doing a Load(x) after a Retain(x) is safe as long as there is not call
    // (or something that can trigger GC) in between Retain(x) and Load(x), and
    // if there were, then this call would increment the effect level, which
    // would prevent covering in the ISEL.
    return false;
  }
  return (op.Effects().consumes.bits() & kTurboshaftEffectLevelMask.bits()) !=
             0 ||
         op.Effects().required_when_unused ||
         op.Effects().produces.control_flow;
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBlock(block_t block) {
  DCHECK(!current_block_);
  current_block_ = block;
  auto current_num_instructions = [&] {
    DCHECK_GE(kMaxInt, instructions_.size());
    return static_cast<int>(instructions_.size());
  };
  int current_block_end = current_num_instructions();

  int effect_level = 0;
  for (node_t node : this->nodes(block)) {
    SetEffectLevel(node, effect_level);
    if (increment_effect_level_for_node(this, node)) {
      ++effect_level;
    }
  }

  // We visit the control first, then the nodes in the block, so the block's
  // control input should be on the same effect level as the last node.
  if (node_t terminator = this->block_terminator(block);
      this->valid(terminator)) {
    SetEffectLevel(terminator, effect_level);
    current_effect_level_ = effect_level;
  }

  auto FinishEmittedInstructions = [&](node_t node, int instruction_start) {
    if (instruction_selection_failed()) return false;
    if (current_num_instructions() == instruction_start) return true;
    std::reverse(instructions_.begin() + instruction_start,
                 instructions_.end());
    if (!this->valid(node)) return true;
    if (!source_positions_) return true;

    SourcePosition source_position;
    if constexpr (Adapter::IsTurboshaft) {
#if V8_ENABLE_WEBASSEMBLY && V8_TARGET_ARCH_X64
      if (V8_UNLIKELY(
              this->Get(node)
                  .template Is<
                      turboshaft::Opmask::kSimd128F64x2PromoteLowF32x4>())) {
        // On x64 there exists an optimization that folds
        // `kF64x2PromoteLowF32x4` and `kS128Load64Zero` together into a single
        // instruction. If the instruction causes an out-of-bounds memory
        // access exception, then the stack trace has to show the source
        // position of the `kS128Load64Zero` and not of the
        // `kF64x2PromoteLowF32x4`.
        if (this->CanOptimizeF64x2PromoteLowF32x4(node)) {
          node = this->input_at(node, 0);
        }
      }
#endif  // V8_ENABLE_WEBASSEMBLY && V8_TARGET_ARCH_X64
      source_position = (*source_positions_)[node];
    } else {
#if V8_ENABLE_WEBASSEMBLY && V8_TARGET_ARCH_X64
      if (V8_UNLIKELY(node->opcode() == IrOpcode::kF64x2PromoteLowF32x4)) {
        // On x64 there exists an optimization that folds
        // `kF64x2PromoteLowF32x4` and `kS128Load64Zero` together into a single
        // instruction. If the instruction causes an out-of-bounds memory
        // access exception, then the stack trace has to show the source
        // position of the `kS128Load64Zero` and not of the
        // `kF64x2PromoteLowF32x4`.
        node_t input = node->InputAt(0);
        LoadTransformMatcher m(input);

        if (m.Is(LoadTransformation::kS128Load64Zero) &&
            CanCover(node, input)) {
          node = input;
        }
      }
#endif  // V8_ENABLE_WEBASSEMBLY && V8_TARGET_ARCH_X64
      source_position = source_positions_->GetSourcePosition(node);
    }
    if (source_position.IsKnown() && IsSourcePositionUsed(node)) {
      sequence()->SetSourcePosition(instructions_.back(), source_position);
    }
    return true;
  };

  // Generate code for the block control "top down", but schedule the code
  // "bottom up".
  VisitControl(block);
  if (!FinishEmittedInstructions(this->block_terminator(block),
                                 current_block_end)) {
    return;
  }

  // Visit code in reverse control flow order, because architecture-specific
  // matching may cover more than one node at a time.
  for (node_t node : base::Reversed(this->nodes(block))) {
    int current_node_end = current_num_instructions();

    if constexpr (Adapter::IsTurboshaft) {
      if (protected_loads_to_remove_->Contains(this->id(node)) &&
          !IsReallyUsed(node)) {
        MarkAsDefined(node);
      }
    }

    if (!IsUsed(node)) {
      // Skip nodes that are unused, while marking them as Defined so that it's
      // clear that these unused nodes have been visited and will not be Defined
      // later.
      MarkAsDefined(node);
    } else if (!IsDefined(node)) {
      // Generate code for this node "top down", but schedule the code "bottom
      // up".
      current_effect_level_ = GetEffectLevel(node);
      VisitNode(node);
      if (!FinishEmittedInstructions(node, current_node_end)) return;
    }
    if (trace_turbo_ == InstructionSelector::kEnableTraceTurboJson) {
      instr_origins_[this->id(node)] = {current_num_instructions(),
                                        current_node_end};
    }
  }

  // We're done with the block.
  InstructionBlock* instruction_block =
      sequence()->InstructionBlockAt(this->rpo_number(block));
  if (current_num_instructions() == current_block_end) {
    // Avoid empty block: insert a {kArchNop} instruction.
    Emit(Instruction::New(sequence()->zone(), kArchNop));
  }
  instruction_block->set_code_start(current_num_instructions());
  instruction_block->set_code_end(current_block_end);
  current_block_ = nullptr;
}

template <typename Adapter>
FlagsCondition InstructionSelectorT<Adapter>::GetComparisonFlagCondition(
    const turboshaft::ComparisonOp& op) const {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  switch (op.kind) {
    case ComparisonOp::Kind::kEqual:
      return kEqual;
    case ComparisonOp::Kind::kSignedLessThan:
      return kSignedLessThan;
    case ComparisonOp::Kind::kSignedLessThanOrEqual:
      return kSignedLessThanOrEqual;
    case ComparisonOp::Kind::kUnsignedLessThan:
      return kUnsignedLessThan;
    case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
      return kUnsignedLessThanOrEqual;
  }
}

template <>
FlagsCondition
InstructionSelectorT<TurbofanAdapter>::GetComparisonFlagCondition(
    const turboshaft::ComparisonOp& op) const {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::MarkPairProjectionsAsWord32(node_t node) {
  node_t projection0 = FindProjection(node, 0);
  if (Adapter::valid(projection0)) {
    MarkAsWord32(projection0);
  }
  node_t projection1 = FindProjection(node, 1);
  if (Adapter::valid(projection1)) {
    MarkAsWord32(projection1);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::ConsumeEqualZero(
    turboshaft::OpIndex* user, turboshaft::OpIndex* value,
    FlagsContinuation* cont) {
  // Try to combine with comparisons against 0 by simply inverting the branch.
  using namespace turboshaft;  // NOLINT(build/namespaces)
  while (const ComparisonOp* equal =
             TryCast<Opmask::kComparisonEqual>(*value)) {
    if (equal->rep == RegisterRepresentation::Word32()) {
      if (!MatchIntegralZero(equal->right())) return;
#ifdef V8_COMPRESS_POINTERS
    } else if (equal->rep == RegisterRepresentation::Tagged()) {
      static_assert(RegisterRepresentation::Tagged().MapTaggedToWord() ==
                    RegisterRepresentation::Word32());
      if (!MatchSmiZero(equal->right())) return;
#endif  // V8_COMPRESS_POINTERS
    } else {
      return;
    }
    if (!CanCover(*user, *value)) return;

    *user = *value;
    *value = equal->left();
    cont->Negate();
  }
}

#if V8_ENABLE_WEBASSEMBLY
template <>
void InstructionSelectorT<TurbofanAdapter>::VisitI8x16RelaxedSwizzle(
    node_t node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitI8x16RelaxedSwizzle(
    node_t node) {
  return VisitI8x16Swizzle(node);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackPointerGreaterThan(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kStackPointerGreaterThanCondition, node);
  VisitStackPointerGreaterThan(node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadStackCheckOffset(node_t node) {
  OperandGenerator g(this);
  Emit(kArchStackCheckOffset, g.DefineAsRegister(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadFramePointer(node_t node) {
  OperandGenerator g(this);
  Emit(kArchFramePointer, g.DefineAsRegister(node));
}

#if V8_ENABLE_WEBASSEMBLY
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadStackPointer(node_t node) {
  OperandGenerator g(this);
  Emit(kArchStackPointer, g.DefineAsRegister(node));
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadParentFramePointer(node_t node) {
  OperandGenerator g(this);
  Emit(kArchParentFramePointer, g.DefineAsRegister(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadRootRegister(node_t node) {
  // Do nothing. Following loads/stores from this operator will use kMode_Root
  // to load/store from an offset of the root register.
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Acos(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Acos);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Acosh(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Acosh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Asin(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Asin);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Asinh(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Asinh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Atan(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Atan);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Atanh(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Atanh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Atan2(node_t node) {
  VisitFloat64Ieee754Binop(node, kIeee754Float64Atan2);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Cbrt(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Cbrt);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Cos(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Cos);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Cosh(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Cosh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Exp(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Exp);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Expm1(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Expm1);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Log(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Log);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Log1p(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Log1p);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Log2(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Log2);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Log10(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Log10);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Pow(node_t node) {
  VisitFloat64Ieee754Binop(node, kIeee754Float64Pow);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sin(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Sin);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sinh(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Sinh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Tan(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Tan);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Tanh(node_t node) {
  VisitFloat64Ieee754Unop(node, kIeee754Float64Tanh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitTableSwitch(
    const SwitchInfo& sw, InstructionOperand const& index_operand) {
  OperandGenerator g(this);
  size_t input_count = 2 + sw.value_range();
  DCHECK_LE(sw.value_range(), std::numeric_limits<size_t>::max() - 2);
  auto* inputs =
      zone()->template AllocateArray<InstructionOperand>(input_count);
  inputs[0] = index_operand;
  InstructionOperand default_operand = g.Label(sw.default_branch());
  std::fill(&inputs[1], &inputs[input_count], default_operand);
  for (const CaseInfo& c : sw.CasesUnsorted()) {
    size_t value = c.value - sw.min_value();
    DCHECK_LE(0u, value);
    DCHECK_LT(value + 2, input_count);
    inputs[value + 2] = g.Label(c.branch);
  }
  Emit(kArchTableSwitch, 0, nullptr, input_count, inputs, 0, nullptr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitBinarySearchSwitch(
    const SwitchInfo& sw, InstructionOperand const& value_operand) {
  OperandGenerator g(this);
  size_t input_count = 2 + sw.case_count() * 2;
  DCHECK_LE(sw.case_count(), (std::numeric_limits<size_t>::max() - 2) / 2);
  auto* inputs =
      zone()->template AllocateArray<InstructionOperand>(input_count);
  inputs[0] = value_operand;
  inputs[1] = g.Label(sw.default_branch());
  std::vector<CaseInfo> cases = sw.CasesSortedByValue();
  for (size_t index = 0; index < cases.size(); ++index) {
    const CaseInfo& c = cases[index];
    inputs[index * 2 + 2 + 0] = g.TempImmediate(c.value);
    inputs[index * 2 + 2 + 1] = g.Label(c.branch);
  }
  Emit(kArchBinarySearchSwitch, 0, nullptr, input_count, inputs, 0, nullptr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastTaggedToWord(node_t node) {
  EmitIdentity(node);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitBitcastWordToTagged(
    node_t node) {
  OperandGenerator g(this);
  Emit(kArchNop, g.DefineSameAsFirst(node), g.Use(node->InputAt(0)));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitBitcastWordToTagged(
    node_t node) {
  OperandGenerator g(this);
  Emit(kArchNop, g.DefineSameAsFirst(node),
       g.Use(this->Get(node).Cast<turboshaft::TaggedBitcastOp>().input()));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitBitcastSmiToWord(
    node_t node) {
  // TODO(dmercadier): using EmitIdentity here is not ideal, because users of
  // {node} will then use its input, which may not have the Word32
  // representation. This might in turn lead to the register allocator wrongly
  // tracking Tagged values that are in fact just Smis. However, using
  // Emit(kArchNop) hurts performance because it inserts a gap move which cannot
  // always be eliminated because the operands may have different sizes (and the
  // move is then truncating or extending). As a temporary work-around until the
  // register allocator is fixed, we use Emit(kArchNop) in DEBUG mode to silence
  // the register allocator verifier.
#ifdef DEBUG
  OperandGenerator g(this);
  Emit(kArchNop, g.DefineSameAsFirst(node),
       g.Use(this->Get(node).Cast<turboshaft::TaggedBitcastOp>().input()));
#else
  EmitIdentity(node);
#endif
}

// 32 bit targets do not implement the following instructions.
#if V8_TARGET_ARCH_32_BIT

VISIT_UNSUPPORTED_OP(Word64And)
VISIT_UNSUPPORTED_OP(Word64Or)
VISIT_UNSUPPORTED_OP(Word64Xor)
VISIT_UNSUPPORTED_OP(Word64Shl)
VISIT_UNSUPPORTED_OP(Word64Shr)
VISIT_UNSUPPORTED_OP(Word64Sar)
VISIT_UNSUPPORTED_OP(Word64Rol)
VISIT_UNSUPPORTED_OP(Word64Ror)
VISIT_UNSUPPORTED_OP(Word64Clz)
VISIT_UNSUPPORTED_OP(Word64Ctz)
VISIT_UNSUPPORTED_OP(Word64ReverseBits)
VISIT_UNSUPPORTED_OP(Word64Popcnt)
VISIT_UNSUPPORTED_OP(Word64Equal)
VISIT_UNSUPPORTED_OP(Int64Add)
VISIT_UNSUPPORTED_OP(Int64Sub)
VISIT_UNSUPPORTED_OP(Int64Mul)
VISIT_UNSUPPORTED_OP(Int64MulHigh)
VISIT_UNSUPPORTED_OP(Uint64MulHigh)
VISIT_UNSUPPORTED_OP(Int64Div)
VISIT_UNSUPPORTED_OP(Int64Mod)
VISIT_UNSUPPORTED_OP(Uint64Div)
VISIT_UNSUPPORTED_OP(Uint64Mod)
VISIT_UNSUPPORTED_OP(Int64AddWithOverflow)
VISIT_UNSUPPORTED_OP(Int64MulWithOverflow)
VISIT_UNSUPPORTED_OP(Int64SubWithOverflow)
VISIT_UNSUPPORTED_OP(Int64LessThan)
VISIT_UNSUPPORTED_OP(Int64LessThanOrEqual)
VISIT_UNSUPPORTED_OP(Uint64LessThan)
VISIT_UNSUPPORTED_OP(Uint64LessThanOrEqual)
VISIT_UNSUPPORTED_OP(BitcastWord32ToWord64)
VISIT_UNSUPPORTED_OP(ChangeInt32ToInt64)
VISIT_UNSUPPORTED_OP(ChangeInt64ToFloat64)
VISIT_UNSUPPORTED_OP(ChangeUint32ToUint64)
VISIT_UNSUPPORTED_OP(ChangeFloat64ToInt64)
VISIT_UNSUPPORTED_OP(ChangeFloat64ToUint64)
VISIT_UNSUPPORTED_OP(TruncateFloat64ToInt64)
VISIT_UNSUPPORTED_OP(TruncateInt64ToInt32)
VISIT_UNSUPPORTED_OP(TryTruncateFloat32ToInt64)
VISIT_UNSUPPORTED_OP(TryTruncateFloat64ToInt64)
VISIT_UNSUPPORTED_OP(TryTruncateFloat32ToUint64)
VISIT_UNSUPPORTED_OP(TryTruncateFloat64ToUint64)
VISIT_UNSUPPORTED_OP(TryTruncateFloat64ToInt32)
VISIT_UNSUPPORTED_OP(TryTruncateFloat64ToUint32)
VISIT_UNSUPPORTED_OP(RoundInt64ToFloat32)
VISIT_UNSUPPORTED_OP(RoundInt64ToFloat64)
VISIT_UNSUPPORTED_OP(RoundUint64ToFloat32)
VISIT_UNSUPPORTED_OP(RoundUint64ToFloat64)
VISIT_UNSUPPORTED_OP(BitcastFloat64ToInt64)
VISIT_UNSUPPORTED_OP(BitcastInt64ToFloat64)
VISIT_UNSUPPORTED_OP(SignExtendWord8ToInt64)
VISIT_UNSUPPORTED_OP(SignExtendWord16ToInt64)
VISIT_UNSUPPORTED_OP(SignExtendWord32ToInt64)
#endif  // V8_TARGET_ARCH_32_BIT

// 64 bit targets do not implement the following instructions.
#if V8_TARGET_ARCH_64_BIT
VISIT_UNSUPPORTED_OP(Int32PairAdd)
VISIT_UNSUPPORTED_OP(Int32PairSub)
VISIT_UNSUPPORTED_OP(Int32PairMul)
VISIT_UNSUPPORTED_OP(Word32PairShl)
VISIT_UNSUPPORTED_OP(Word32PairShr)
VISIT_UNSUPPORTED_OP(Word32PairSar)
#endif  // V8_TARGET_ARCH_64_BIT

#if !V8_TARGET_ARCH_IA32 && !V8_TARGET_ARCH_ARM && !V8_TARGET_ARCH_RISCV32
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairLoad(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairStore(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairAdd(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairSub(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairAnd(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairOr(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairXor(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairExchange(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairCompareExchange(
    node_t node) {
  UNIMPLEMENTED();
}
#endif  // !V8_TARGET_ARCH_IA32 && !V8_TARGET_ARCH_ARM
        // && !V8_TARGET_ARCH_RISCV32

#if !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_ARM64 && !V8_TARGET_ARCH_MIPS64 && \
    !V8_TARGET_ARCH_S390 && !V8_TARGET_ARCH_PPC64 &&                          \
    !V8_TARGET_ARCH_RISCV64 && !V8_TARGET_ARCH_LOONG64

VISIT_UNSUPPORTED_OP(Word64AtomicLoad)
VISIT_UNSUPPORTED_OP(Word64AtomicStore)
VISIT_UNSUPPORTED_OP(Word64AtomicAdd)
VISIT_UNSUPPORTED_OP(Word64AtomicSub)
VISIT_UNSUPPORTED_OP(Word64AtomicAnd)
VISIT_UNSUPPORTED_OP(Word64AtomicOr)
VISIT_UNSUPPORTED_OP(Word64AtomicXor)
VISIT_UNSUPPORTED_OP(Word64AtomicExchange)
VISIT_UNSUPPORTED_OP(Word64AtomicCompareExchange)

#endif  // !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_ARM64 && !V8_TARGET_ARCH_PPC64
        // !V8_TARGET_ARCH_MIPS64 && !V8_TARGET_ARCH_S390 &&
        // !V8_TARGET_ARCH_RISCV64 && !V8_TARGET_ARCH_LOONG64

#if !V8_TARGET_ARCH_IA32 && !V8_TARGET_ARCH_ARM && !V8_TARGET_ARCH_RISCV32
// This is only needed on 32-bit to split the 64-bit value into two operands.
IF_WASM(VISIT_UNSUPPORTED_OP, I64x2SplatI32Pair)
IF_WASM(VISIT_UNSUPPORTED_OP, I64x2ReplaceLaneI32Pair)
#endif  // !V8_TARGET_ARCH_IA32 && !V8_TARGET_ARCH_ARM &&
        // !V8_TARGET_ARCH_RISCV32

#if !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_S390X && !V8_TARGET_ARCH_PPC64
#if !V8_TARGET_ARCH_ARM64
#if !V8_TARGET_ARCH_MIPS64 && !V8_TARGET_ARCH_LOONG64 && \
    !V8_TARGET_ARCH_RISCV32 && !V8_TARGET_ARCH_RISCV64

IF_WASM(VISIT_UNSUPPORTED_OP, I64x2Splat)
IF_WASM(VISIT_UNSUPPORTED_OP, I64x2ExtractLane)
IF_WASM(VISIT_UNSUPPORTED_OP, I64x2ReplaceLane)

#endif  // !V8_TARGET_ARCH_MIPS64 && !V8_TARGET_ARCH_LOONG64 &&
        // !V8_TARGET_ARCH_RISCV64 && !V8_TARGET_ARCH_RISCV32
#endif  // !V8_TARGET_ARCH_ARM64
#endif  // !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_S390X && !V8_TARGET_ARCH_PPC64

#if !V8_TARGET_ARCH_ARM64

IF_WASM(VISIT_UNSUPPORTED_OP, I8x16AddReduce)
IF_WASM(VISIT_UNSUPPORTED_OP, I16x8AddReduce)
IF_WASM(VISIT_UNSUPPORTED_OP, I32x4AddReduce)
IF_WASM(VISIT_UNSUPPORTED_OP, I64x2AddReduce)
IF_WASM(VISIT_UNSUPPORTED_OP, F32x4AddReduce)
IF_WASM(VISIT_UNSUPPORTED_OP, F64x2AddReduce)

#endif  // !V8_TARGET_ARCH_ARM64

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitFinishRegion(Node* node) {
  EmitIdentity(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitParameter(node_t node) {
  OperandGenerator g(this);
  int index = this->parameter_index_of(node);

  if (linkage()->GetParameterLocation(index).IsNullRegister()) {
    EmitMoveParamToFPR(node, index);
  } else {
    InstructionOperand op =
        linkage()->ParameterHasSecondaryLocation(index)
            ? g.DefineAsDualLocation(
                  node, linkage()->GetParameterLocation(index),
                  linkage()->GetParameterSecondaryLocation(index))
            : g.DefineAsLocation(node, linkage()->GetParameterLocation(index));
    Emit(kArchNop, op);
  }
}

namespace {

LinkageLocation ExceptionLocation() {
  return LinkageLocation::ForRegister(kReturnRegister0.code(),
                                      MachineType::TaggedPointer());
}

constexpr InstructionCode EncodeCallDescriptorFlags(
    InstructionCode opcode, CallDescriptor::Flags flags) {
  // Note: Not all bits of `flags` are preserved.
  static_assert(CallDescriptor::kFlagsBitsEncodedInInstructionCode ==
                MiscField::kSize);
  DCHECK(Instruction::IsCallWithDescriptorFlags(opcode));
  return opcode | MiscField::encode(flags & MiscField::kMax);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitIfException(node_t node) {
  OperandGenerator g(this);
  if constexpr (Adapter::IsTurbofan) {
    DCHECK_EQ(IrOpcode::kCall, node->InputAt(1)->opcode());
  }
  Emit(kArchNop, g.DefineAsLocation(node, ExceptionLocation()));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitOsrValue(node_t node) {
  OperandGenerator g(this);
  int index = this->osr_value_index_of(node);
  Emit(kArchNop,
       g.DefineAsLocation(node, linkage()->GetOsrValueLocation(index)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitPhi(node_t node) {
  const int input_count = this->value_input_count(node);
  DCHECK_EQ(input_count, this->PredecessorCount(current_block_));
  PhiInstruction* phi = instruction_zone()->template New<PhiInstruction>(
      instruction_zone(), GetVirtualRegister(node),
      static_cast<size_t>(input_count));
  sequence()->InstructionBlockAt(this->rpo_number(current_block_))->AddPhi(phi);
  for (int i = 0; i < input_count; ++i) {
    node_t input = this->input_at(node, i);
    MarkAsUsed(input);
    phi->SetInput(static_cast<size_t>(i), GetVirtualRegister(input));
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitProjection(
    turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const ProjectionOp& projection = this->Get(node).Cast<ProjectionOp>();
  const Operation& value_op = this->Get(projection.input());
  if (value_op.Is<OverflowCheckedBinopOp>() ||
      value_op.Is<OverflowCheckedUnaryOp>() || value_op.Is<TryChangeOp>() ||
      value_op.Is<Word32PairBinopOp>()) {
    if (projection.index == 0u) {
      EmitIdentity(node);
    } else {
      DCHECK_EQ(1u, projection.index);
      MarkAsUsed(projection.input());
    }
  } else if (value_op.Is<DidntThrowOp>()) {
    // Nothing to do here?
  } else if (value_op.Is<CallOp>()) {
    // Call projections need to be behind the call's DidntThrow.
    UNREACHABLE();
  } else if (value_op.Is<AtomicWord32PairOp>()) {
    // Nothing to do here.
  } else {
    UNIMPLEMENTED();
  }
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitProjection(Node* node) {
  OperandGenerator g(this);
  Node* value = node->InputAt(0);
  switch (value->opcode()) {
    case IrOpcode::kInt32AddWithOverflow:
    case IrOpcode::kInt32SubWithOverflow:
    case IrOpcode::kInt32MulWithOverflow:
    case IrOpcode::kInt64AddWithOverflow:
    case IrOpcode::kInt64SubWithOverflow:
    case IrOpcode::kInt64MulWithOverflow:
    case IrOpcode::kTryTruncateFloat32ToInt64:
    case IrOpcode::kTryTruncateFloat64ToInt64:
    case IrOpcode::kTryTruncateFloat32ToUint64:
    case IrOpcode::kTryTruncateFloat64ToUint64:
    case IrOpcode::kTryTruncateFloat64ToInt32:
    case IrOpcode::kTryTruncateFloat64ToUint32:
    case IrOpcode::kInt32PairAdd:
    case IrOpcode::kInt32PairSub:
    case IrOpcode::kInt32PairMul:
    case IrOpcode::kWord32PairShl:
    case IrOpcode::kWord32PairShr:
    case IrOpcode::kWord32PairSar:
    case IrOpcode::kInt32AbsWithOverflow:
    case IrOpcode::kInt64AbsWithOverflow:
      if (ProjectionIndexOf(node->op()) == 0u) {
        EmitIdentity(node);
      } else {
        DCHECK_EQ(1u, ProjectionIndexOf(node->op()));
        MarkAsUsed(value);
      }
      break;
    case IrOpcode::kCall:
    case IrOpcode::kWord32AtomicPairLoad:
    case IrOpcode::kWord32AtomicPairExchange:
    case IrOpcode::kWord32AtomicPairCompareExchange:
    case IrOpcode::kWord32AtomicPairAdd:
    case IrOpcode::kWord32AtomicPairSub:
    case IrOpcode::kWord32AtomicPairAnd:
    case IrOpcode::kWord32AtomicPairOr:
    case IrOpcode::kWord32AtomicPairXor:
      // Nothing to do for these opcodes.
      break;
    default:
      UNREACHABLE();
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitConstant(node_t node) {
  // We must emit a NOP here because every live range needs a defining
  // instruction in the register allocator.
  OperandGenerator g(this);
  Emit(kArchNop, g.DefineAsConstant(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::UpdateMaxPushedArgumentCount(size_t count) {
  *max_pushed_argument_count_ = std::max(count, *max_pushed_argument_count_);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitCall(node_t node, block_t handler) {
  OperandGenerator g(this);
  auto call = this->call_view(node);
  const CallDescriptor* call_descriptor = call.call_descriptor();
  SaveFPRegsMode mode = call_descriptor->NeedsCallerSavedFPRegisters()
                            ? SaveFPRegsMode::kSave
                            : SaveFPRegsMode::kIgnore;

  if (call_descriptor->NeedsCallerSavedRegisters()) {
    Emit(kArchSaveCallerRegisters | MiscField::encode(static_cast<int>(mode)),
         g.NoOutput());
  }

  FrameStateDescriptor* frame_state_descriptor = nullptr;
  bool needs_frame_state = false;
  if (call_descriptor->NeedsFrameState()) {
    needs_frame_state = true;
    frame_state_descriptor = GetFrameStateDescriptor(call.frame_state());
  }

  CallBuffer buffer(zone(), call_descriptor, frame_state_descriptor);
  CallDescriptor::Flags flags = call_descriptor->flags();

  // Compute InstructionOperands for inputs and outputs.
  // TODO(turbofan): on some architectures it's probably better to use
  // the code object in a register if there are multiple uses of it.
  // Improve constant pool and the heuristics in the register allocator
  // for where to emit constants.
  CallBufferFlags call_buffer_flags(kCallCodeImmediate | kCallAddressImmediate);
  if (flags & CallDescriptor::kFixedTargetRegister) {
    call_buffer_flags |= kCallFixedTargetRegister;
  }
  InitializeCallBuffer(node, &buffer, call_buffer_flags);

  EmitPrepareArguments(&buffer.pushed_nodes, call_descriptor, node);
  UpdateMaxPushedArgumentCount(buffer.pushed_nodes.size());

  if (call_descriptor->RequiresEntrypointTagForCall()) {
    buffer.instruction_args.push_back(
        g.TempImmediate(call_descriptor->shifted_tag()));
  }

  // Pass label of exception handler block.
  if (handler) {
    if constexpr (Adapter::IsTurbofan) {
      DCHECK_EQ(IrOpcode::kIfException, handler->front()->opcode());
    }
    flags |= CallDescriptor::kHasExceptionHandler;
    buffer.instruction_args.push_back(g.Label(handler));
  } else {
    if constexpr (Adapter::IsTurboshaft) {
      if (call.ts_call_descriptor()->lazy_deopt_on_throw ==
          LazyDeoptOnThrow::kYes) {
        flags |= CallDescriptor::kHasExceptionHandler;
        buffer.instruction_args.push_back(
            g.UseImmediate(kLazyDeoptOnThrowSentinel));
      }
    }
  }

  // Select the appropriate opcode based on the call type.
  InstructionCode opcode;
  switch (call_descriptor->kind()) {
    case CallDescriptor::kCallAddress: {
      int gp_param_count =
          static_cast<int>(call_descriptor->GPParameterCount());
      int fp_param_count =
          static_cast<int>(call_descriptor->FPParameterCount());
#if ABI_USES_FUNCTION_DESCRIPTORS
      // Highest fp_param_count bit is used on AIX to indicate if a CFunction
      // call has function descriptor or not.
      static_assert(FPParamField::kSize == kHasFunctionDescriptorBitShift + 1);
      if (!call_descriptor->NoFunctionDescriptor()) {
        fp_param_count |= 1 << kHasFunctionDescriptorBitShift;
      }
#endif
      opcode = needs_frame_state ? kArchCallCFunctionWithFrameState
                                 : kArchCallCFunction;
      opcode |= ParamField::encode(gp_param_count) |
                FPParamField::encode(fp_param_count);
      break;
    }
    case CallDescriptor::kCallCodeObject:
      opcode = EncodeCallDescriptorFlags(kArchCallCodeObject, flags);
      break;
    case CallDescriptor::kCallJSFunction:
      opcode = EncodeCallDescriptorFlags(kArchCallJSFunction, flags);
      break;
#if V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallWasmCapiFunction:
    case CallDescriptor::kCallWasmFunction:
    case CallDescriptor::kCallWasmImportWrapper:
      opcode = EncodeCallDescriptorFlags(kArchCallWasmFunction, flags);
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallBuiltinPointer:
      opcode = EncodeCallDescriptorFlags(kArchCallBuiltinPointer, flags);
      break;
  }

  // Emit the call instruction.
  size_t const output_count = buffer.outputs.size();
  auto* outputs = output_count ? &buffer.outputs.front() : nullptr;
  Instruction* call_instr =
      Emit(opcode, output_count, outputs, buffer.instruction_args.size(),
           &buffer.instruction_args.front());
  if (instruction_selection_failed()) return;
  call_instr->MarkAsCall();

  EmitPrepareResults(&(buffer.output_nodes), call_descriptor, node);

  if (call_descriptor->NeedsCallerSavedRegisters()) {
    Emit(
        kArchRestoreCallerRegisters | MiscField::encode(static_cast<int>(mode)),
        g.NoOutput());
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTailCall(node_t node) {
  OperandGenerator g(this);

  auto call = this->call_view(node);
  auto caller = linkage()->GetIncomingDescriptor();
  auto callee = call.call_descriptor();
  DCHECK(caller->CanTailCall(callee));
  const int stack_param_delta = callee->GetStackParameterDelta(caller);
  CallBuffer buffer(zone(), callee, nullptr);

  // Compute InstructionOperands for inputs and outputs.
  CallBufferFlags flags(kCallCodeImmediate | kCallTail);
  if (IsTailCallAddressImmediate()) {
    flags |= kCallAddressImmediate;
  }
  if (callee->flags() & CallDescriptor::kFixedTargetRegister) {
    flags |= kCallFixedTargetRegister;
  }
  InitializeCallBuffer(node, &buffer, flags, stack_param_delta);
  UpdateMaxPushedArgumentCount(stack_param_delta);

  // Select the appropriate opcode based on the call type.
  InstructionCode opcode;
  InstructionOperandVector temps(zone());
  switch (callee->kind()) {
    case CallDescriptor::kCallCodeObject:
      opcode = kArchTailCallCodeObject;
      break;
    case CallDescriptor::kCallAddress:
      DCHECK(!caller->IsJSFunctionCall());
      opcode = kArchTailCallAddress;
      break;
#if V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallWasmFunction:
      DCHECK(!caller->IsJSFunctionCall());
      opcode = kArchTailCallWasm;
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    default:
      UNREACHABLE();
  }
  opcode = EncodeCallDescriptorFlags(opcode, callee->flags());

  Emit(kArchPrepareTailCall, g.NoOutput());

  if (callee->RequiresEntrypointTagForCall()) {
    buffer.instruction_args.push_back(g.TempImmediate(callee->shifted_tag()));
  }

  // Add an immediate operand that represents the offset to the first slot
  // that is unused with respect to the stack pointer that has been updated
  // for the tail call instruction. Backends that pad arguments can write the
  // padding value at this offset from the stack.
  const int optional_padding_offset =
      callee->GetOffsetToFirstUnusedStackSlot() - 1;
  buffer.instruction_args.push_back(g.TempImmediate(optional_padding_offset));

  const int first_unused_slot_offset =
      kReturnAddressStackSlotCount + stack_param_delta;
  buffer.instruction_args.push_back(g.TempImmediate(first_unused_slot_offset));

  // Emit the tailcall instruction.
  Emit(opcode, 0, nullptr, buffer.instruction_args.size(),
       &buffer.instruction_args.front(), temps.size(),
       temps.empty() ? nullptr : &temps.front());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitGoto(block_t target) {
  // jump to the next block.
  OperandGenerator g(this);
  Emit(kArchJmp, g.NoOutput(), g.Label(target));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitReturn(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const ReturnOp& ret = schedule()->Get(node).Cast<ReturnOp>();

  OperandGenerator g(this);
  const int input_count =
      linkage()->GetIncomingDescriptor()->ReturnCount() == 0
          ? 1
          : (1 + static_cast<int>(ret.return_values().size()));
  DCHECK_GE(input_count, 1);

  auto value_locations =
      zone()->template AllocateArray<InstructionOperand>(input_count);
  const Operation& pop_count = schedule()->Get(ret.pop_count());
  if (pop_count.Is<Opmask::kWord32Constant>() ||
      pop_count.Is<Opmask::kWord64Constant>()) {
    value_locations[0] = g.UseImmediate(ret.pop_count());
  } else {
    value_locations[0] = g.UseRegister(ret.pop_count());
  }
  for (int i = 0; i < input_count - 1; ++i) {
    value_locations[i + 1] =
        g.UseLocation(ret.return_values()[i], linkage()->GetReturnLocation(i));
  }
  Emit(kArchRet, 0, nullptr, input_count, value_locations);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitReturn(node_t ret) {
  OperandGenerator g(this);
  const int input_count = linkage()->GetIncomingDescriptor()->ReturnCount() == 0
                              ? 1
                              : ret->op()->ValueInputCount();
  DCHECK_GE(input_count, 1);
  auto value_locations =
      zone()->template AllocateArray<InstructionOperand>(input_count);
  Node* pop_count = ret->InputAt(0);
  value_locations[0] = (pop_count->opcode() == IrOpcode::kInt32Constant ||
                        pop_count->opcode() == IrOpcode::kInt64Constant)
                           ? g.UseImmediate(pop_count)
                           : g.UseRegister(pop_count);
  for (int i = 1; i < input_count; ++i) {
    value_locations[i] =
        g.UseLocation(ret->InputAt(i), linkage()->GetReturnLocation(i - 1));
  }
  Emit(kArchRet, 0, nullptr, input_count, value_locations);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBranch(node_t branch_node,
                                                block_t tbranch,
                                                block_t fbranch) {
  auto branch = this->branch_view(branch_node);
  TryPrepareScheduleFirstProjection(branch.condition());

  FlagsContinuation cont =
      FlagsContinuation::ForBranch(kNotEqual, tbranch, fbranch);
  VisitWordCompareZero(branch, branch.condition(), &cont);
}

// When a DeoptimizeIf/DeoptimizeUnless/Branch depends on a BinopOverflow, the
// InstructionSelector can sometimes generate a fuse instruction covering both
// the BinopOverflow and the DeoptIf/Branch, and the final emitted code will
// look like:
//
//     r = BinopOverflow
//     jo branch_target/deopt_target
//
// When this fusing fails, the final code looks like:
//
//     r = BinopOverflow
//     o = sete  // sets overflow bit
//     cmp o, 0
//     jnz branch_target/deopt_target
//
// To be able to fuse tue BinopOverflow and the DeoptIf/Branch, the 1st
// projection (Projection[0], which contains the actual result) must already be
// scheduled (and a few other conditions must be satisfied, see
// InstructionSelectorXXX::VisitWordCompareZero).
// TryPrepareScheduleFirstProjection is thus called from
// VisitDeoptimizeIf/VisitDeoptimizeUnless/VisitBranch and detects if the 1st
// projection could be scheduled now, and, if so, defines it.
template <typename Adapter>
void InstructionSelectorT<Adapter>::TryPrepareScheduleFirstProjection(
    node_t maybe_projection) {
  // The DeoptimizeIf/DeoptimizeUnless/Branch condition is not a projection.
  if (!this->is_projection(maybe_projection)) return;

  if (this->projection_index_of(maybe_projection) != 1u) {
    // The DeoptimizeIf/DeoptimizeUnless/Branch isn't on the Projection[1]
    // (ie, not on the overflow bit of a BinopOverflow).
    return;
  }

  DCHECK_EQ(this->value_input_count(maybe_projection), 1);
  node_t node = this->input_at(maybe_projection, 0);
  if (this->block(schedule_, node) != current_block_) {
    // The projection input is not in the current block, so it shouldn't be
    // emitted now, so we don't need to eagerly schedule its Projection[0].
    return;
  }

  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    auto* binop = this->Get(node).template TryCast<OverflowCheckedBinopOp>();
    auto* unop = this->Get(node).template TryCast<OverflowCheckedUnaryOp>();
    if (binop == nullptr && unop == nullptr) return;
    if (binop) {
      DCHECK(binop->kind == OverflowCheckedBinopOp::Kind::kSignedAdd ||
             binop->kind == OverflowCheckedBinopOp::Kind::kSignedSub ||
             binop->kind == OverflowCheckedBinopOp::Kind::kSignedMul);
    } else {
      DCHECK_EQ(unop->kind, OverflowCheckedUnaryOp::Kind::kAbs);
    }
  } else {
    switch (node->opcode()) {
      case IrOpcode::kInt32AddWithOverflow:
      case IrOpcode::kInt32SubWithOverflow:
      case IrOpcode::kInt32MulWithOverflow:
      case IrOpcode::kInt64AddWithOverflow:
      case IrOpcode::kInt64SubWithOverflow:
      case IrOpcode::kInt64MulWithOverflow:
        break;
      default:
        return;
    }
  }

  node_t result = FindProjection(node, 0);
  if (!Adapter::valid(result) || IsDefined(result)) {
    // No Projection(0), or it's already defined.
    return;
  }

  if (this->block(schedule_, result) != current_block_) {
    // {result} wasn't planned to be scheduled in {current_block_}. To
    // avoid adding checks to see if it can still be scheduled now, we
    // just bail out.
    return;
  }

  // Checking if all uses of {result} that are in the current block have
  // already been Defined.
  // We also ignore Phi uses: if {result} is used in a Phi in the block in
  // which it is defined, this means that this block is a loop header, and
  // {result} back into it through the back edge. In this case, it's
  // normal to schedule {result} before the Phi that uses it.
  if constexpr (Adapter::IsTurboshaft) {
    for (turboshaft::OpIndex use : turboshaft_uses(result)) {
      // We ignore TupleOp uses, since TupleOp don't lead to emitted machine
      // instructions and are just Turboshaft "meta operations".
      if (!this->Get(use).template Is<turboshaft::TupleOp>() &&
          !IsDefined(use) && this->block(schedule_, use) == current_block_ &&
          !this->Get(use).template Is<turboshaft::PhiOp>()) {
        return;
      }
    }
  } else {
    for (Node* use : result->uses()) {
      if (!IsDefined(use) && this->block(schedule_, use) == current_block_ &&
          use->opcode() != IrOpcode::kPhi) {
        // {use} is in the current block but is not defined yet. It's
        // possible that it's not actually used, but the IsUsed(x) predicate
        // is not valid until we have visited `x`, so we overaproximate and
        // assume that {use} is itself used.
        return;
      }
    }
  }

  // Visiting the projection now. Note that this relies on the fact that
  // VisitProjection doesn't Emit something: if it did, then we could be
  // Emitting something after a Branch, which is invalid (Branch can only
  // be at the end of a block, and the end of a block must always be a
  // block terminator). (remember that we emit operation in reverse order,
  // so because we are doing TryPrepareScheduleFirstProjection before
  // actually emitting the Branch, it would be after in the final
  // instruction sequence, not before)
  VisitProjection(result);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitDeoptimizeIf(node_t node) {
  auto deopt = this->deoptimize_view(node);
  DCHECK(deopt.is_deoptimize_if());

  TryPrepareScheduleFirstProjection(deopt.condition());

  FlagsContinuation cont = FlagsContinuation::ForDeoptimize(
      kNotEqual, deopt.reason(), this->id(node), deopt.feedback(),
      deopt.frame_state());
  VisitWordCompareZero(node, deopt.condition(), &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitDeoptimizeUnless(node_t node) {
  auto deopt = this->deoptimize_view(node);
  DCHECK(deopt.is_deoptimize_unless());
  TryPrepareScheduleFirstProjection(deopt.condition());

  FlagsContinuation cont =
      FlagsContinuation::ForDeoptimize(kEqual, deopt.reason(), this->id(node),
                                       deopt.feedback(), deopt.frame_state());
  VisitWordCompareZero(node, deopt.condition(), &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSelect(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 3);
  FlagsContinuation cont = FlagsContinuation::ForSelect(
      kNotEqual, node, this->input_at(node, 1), this->input_at(node, 2));
  VisitWordCompareZero(node, this->input_at(node, 0), &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTrapIf(node_t node, TrapId trap_id) {
  // FrameStates are only used for wasm traps inlined in JS. In that case the
  // trap node will be lowered (replaced) before instruction selection.
  // Therefore any TrapIf node has only one input.
  DCHECK_EQ(this->value_input_count(node), 1);
  FlagsContinuation cont = FlagsContinuation::ForTrap(kNotEqual, trap_id);
  VisitWordCompareZero(node, this->input_at(node, 0), &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTrapUnless(node_t node,
                                                    TrapId trap_id) {
  // FrameStates are only used for wasm traps inlined in JS. In that case the
  // trap node will be lowered (replaced) before instruction selection.
  // Therefore any TrapUnless node has only one input.
  DCHECK_EQ(this->value_input_count(node), 1);
  FlagsContinuation cont = FlagsContinuation::ForTrap(kEqual, trap_id);
  VisitWordCompareZero(node, this->input_at(node, 0), &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitIdentity(node_t node) {
  MarkAsUsed(this->input_at(node, 0));
  MarkAsDefined(node);
  SetRename(node, this->input_at(node, 0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitDeoptimize(
    DeoptimizeReason reason, id_t node_id, FeedbackSource const& feedback,
    node_t frame_state) {
  InstructionOperandVector args(instruction_zone());
  AppendDeoptimizeArguments(&args, reason, node_id, feedback, frame_state);
  Emit(kArchDeoptimize, 0, nullptr, args.size(), &args.front(), 0, nullptr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitThrow(Node* node) {
  OperandGenerator g(this);
  Emit(kArchThrowTerminator, g.NoOutput());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitDebugBreak(node_t node) {
  OperandGenerator g(this);
  Emit(kArchDebugBreak, g.NoOutput());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnreachable(node_t node) {
  OperandGenerator g(this);
  Emit(kArchDebugBreak, g.NoOutput());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStaticAssert(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  node_t asserted = this->input_at(node, 0);
  UnparkedScopeIfNeeded scope(broker_);
  AllowHandleDereference allow_handle_dereference;
  if constexpr (Adapter::IsTurboshaft) {
    StdoutStream os;
    os << this->Get(asserted);
    FATAL(
        "Expected Turbofan static assert to hold, but got non-true input:\n  "
        "%s",
        this->Get(node).template Cast<turboshaft::StaticAssertOp>().source);
  } else {
    asserted->Print(4);
    FATAL(
        "Expected Turbofan static assert to hold, but got non-true input:\n  "
        "%s",
        StaticAssertSourceOf(node->op()));
  }
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitDeadValue(Node* node) {
  OperandGenerator g(this);
  MarkAsRepresentation(DeadValueRepresentationOf(node->op()), node);
  Emit(kArchDebugBreak, g.DefineAsConstant(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitComment(node_t node) {
  OperandGenerator g(this);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::CommentOp& comment =
        this->turboshaft_graph()
            ->Get(node)
            .template Cast<turboshaft::CommentOp>();
    using ptrsize_int_t =
        std::conditional<kSystemPointerSize == 8, int64_t, int32_t>::type;
    InstructionOperand operand = sequence()->AddImmediate(
        Constant{reinterpret_cast<ptrsize_int_t>(comment.message)});
    Emit(kArchComment, 0, nullptr, 1, &operand);
  } else {
    InstructionOperand operand(g.UseImmediate(node));
    Emit(kArchComment, 0, nullptr, 1, &operand);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRetain(node_t node) {
  OperandGenerator g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kArchNop, g.NoOutput(), g.UseAny(this->input_at(node, 0)));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitControl(block_t block) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
#ifdef DEBUG
  // SSA deconstruction requires targets of branches not to have phis.
  // Edge split form guarantees this property, but is more strict.
  if (auto successors =
          SuccessorBlocks(block->LastOperation(*turboshaft_graph()));
      successors.size() > 1) {
    for (Block* successor : successors) {
      if (successor->HasPhis(*turboshaft_graph())) {
        std::ostringstream str;
        str << "You might have specified merged variables for a label with "
            << "only one predecessor." << std::endl
            << "# Current Block: " << successor->index() << std::endl;
        FATAL("%s", str.str().c_str());
      }
    }
  }
#endif  // DEBUG
  const Operation& op = block->LastOperation(*schedule());
  OpIndex node = schedule()->Index(op);
  int instruction_end = static_cast<int>(instructions_.size());
  switch (op.opcode) {
    case Opcode::kGoto:
      VisitGoto(op.Cast<GotoOp>().destination);
      break;
    case Opcode::kReturn:
      VisitReturn(node);
      break;
    case Opcode::kTailCall:
      VisitTailCall(node);
      break;
    case Opcode::kDeoptimize: {
      const DeoptimizeOp& deoptimize = op.Cast<DeoptimizeOp>();
      VisitDeoptimize(deoptimize.parameters->reason(), node.id(),
                      deoptimize.parameters->feedback(),
                      deoptimize.frame_state());
      break;
    }
    case Opcode::kBranch: {
      const BranchOp& branch = op.Cast<BranchOp>();
      block_t tbranch = branch.if_true;
      block_t fbranch = branch.if_false;
      if (tbranch == fbranch) {
        VisitGoto(tbranch);
      } else {
        VisitBranch(node, tbranch, fbranch);
      }
      break;
    }
    case Opcode::kSwitch: {
      const SwitchOp& swtch = op.Cast<SwitchOp>();
      int32_t min_value = std::numeric_limits<int32_t>::max();
      int32_t max_value = std::numeric_limits<int32_t>::min();

      ZoneVector<CaseInfo> cases(swtch.cases.size(), zone());
      for (size_t i = 0; i < swtch.cases.size(); ++i) {
        const SwitchOp::Case& c = swtch.cases[i];
        cases[i] = CaseInfo{c.value, 0, c.destination};
        if (min_value > c.value) min_value = c.value;
        if (max_value < c.value) max_value = c.value;
      }
      SwitchInfo sw(std::move(cases), min_value, max_value, swtch.default_case);
      return VisitSwitch(node, sw);
    }
    case Opcode::kCheckException: {
      const CheckExceptionOp& check = op.Cast<CheckExceptionOp>();
      VisitCall(check.throwing_operation(), check.catch_block);
      VisitGoto(check.didnt_throw_block);
      return;
    }
    case Opcode::kUnreachable:
      return VisitUnreachable(node);
    case Opcode::kStaticAssert:
      return VisitStaticAssert(node);
    default: {
      const std::string op_string = op.ToString();
      PrintF("\033[31mNo ISEL support for: %s\033[m\n", op_string.c_str());
      FATAL("Unexpected operation #%d:%s", node.id(), op_string.c_str());
    }
  }

  if (trace_turbo_ == InstructionSelector::kEnableTraceTurboJson) {
    DCHECK(node.valid());
    int instruction_start = static_cast<int>(instructions_.size());
    instr_origins_[this->id(node)] = {instruction_start, instruction_end};
  }
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitControl(BasicBlock* block) {
#ifdef DEBUG
  // SSA deconstruction requires targets of branches not to have phis.
  // Edge split form guarantees this property, but is more strict.
  if (block->SuccessorCount() > 1) {
    for (BasicBlock* const successor : block->successors()) {
      for (Node* const node : *successor) {
        if (IrOpcode::IsPhiOpcode(node->opcode())) {
          std::ostringstream str;
          str << "You might have specified merged variables for a label with "
              << "only one predecessor." << std::endl
              << "# Current Block: " << *successor << std::endl
              << "#          Node: " << *node;
          FATAL("%s", str.str().c_str());
        }
      }
    }
  }
#endif

  Node* input = block->control_input();
  int instruction_end = static_cast<int>(instructions_.size());
  switch (block->control()) {
    case BasicBlock::kGoto:
      VisitGoto(block->SuccessorAt(0));
      break;
    case BasicBlock::kCall: {
      DCHECK_EQ(IrOpcode::kCall, input->opcode());
      BasicBlock* success = block->SuccessorAt(0);
      BasicBlock* exception = block->SuccessorAt(1);
      VisitCall(input, exception);
      VisitGoto(success);
      break;
    }
    case BasicBlock::kTailCall: {
      DCHECK_EQ(IrOpcode::kTailCall, input->opcode());
      VisitTailCall(input);
      break;
    }
    case BasicBlock::kBranch: {
      DCHECK_EQ(IrOpcode::kBranch, input->opcode());
      // TODO(nicohartmann@): Once all branches have explicitly specified
      // semantics, we should allow only BranchSemantics::kMachine here.
      DCHECK_NE(BranchSemantics::kJS,
                BranchParametersOf(input->op()).semantics());
      BasicBlock* tbranch = block->SuccessorAt(0);
      BasicBlock* fbranch = block->SuccessorAt(1);
      if (tbranch == fbranch) {
        VisitGoto(tbranch);
      } else {
        VisitBranch(input, tbranch, fbranch);
      }
      break;
    }
    case BasicBlock::kSwitch: {
      DCHECK_EQ(IrOpcode::kSwitch, input->opcode());
      // Last successor must be {IfDefault}.
      BasicBlock* default_branch = block->successors().back();
      DCHECK_EQ(IrOpcode::kIfDefault, default_branch->front()->opcode());
      // All other successors must be {IfValue}s.
      int32_t min_value = std::numeric_limits<int32_t>::max();
      int32_t max_value = std::numeric_limits<int32_t>::min();
      size_t case_count = block->SuccessorCount() - 1;
      ZoneVector<CaseInfo> cases(case_count, zone());
      for (size_t i = 0; i < case_count; ++i) {
        BasicBlock* branch = block->SuccessorAt(i);
        const IfValueParameters& p = IfValueParametersOf(branch->front()->op());
        cases[i] = CaseInfo{p.value(), p.comparison_order(), branch};
        if (min_value > p.value()) min_value = p.value();
        if (max_value < p.value()) max_value = p.value();
      }
      SwitchInfo sw(cases, min_value, max_value, default_branch);
      VisitSwitch(input, sw);
      break;
    }
    case BasicBlock::kReturn: {
      DCHECK_EQ(IrOpcode::kReturn, input->opcode());
      VisitReturn(input);
      break;
    }
    case BasicBlock::kDeoptimize: {
      DeoptimizeParameters p = DeoptimizeParametersOf(input->op());
      FrameState value{input->InputAt(0)};
      VisitDeoptimize(p.reason(), input->id(), p.feedback(), value);
      break;
    }
    case BasicBlock::kThrow:
      DCHECK_EQ(IrOpcode::kThrow, input->opcode());
      VisitThrow(input);
      break;
    case BasicBlock::kNone: {
      // Exit block doesn't have control.
      DCHECK_NULL(input);
      break;
    }
    default:
      UNREACHABLE();
  }
  if (trace_turbo_ == InstructionSelector::kEnableTraceTurboJson && input) {
    int instruction_start = static_cast<int>(instructions_.size());
    instr_origins_[input->id()] = {instruction_start, instruction_end};
  }
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitNode(Node* node) {
  tick_counter_->TickAndMaybeEnterSafepoint();
  DCHECK_NOT_NULL(
      this->block(schedule(), node));  // should only use scheduled nodes.
  switch (node->opcode()) {
    case IrOpcode::kTraceInstruction:
#if V8_TARGET_ARCH_X64
      return VisitTraceInstruction(node);
#else
      return;
#endif
    case IrOpcode::kStart:
    case IrOpcode::kLoop:
    case IrOpcode::kEnd:
    case IrOpcode::kBranch:
    case IrOpcode::kIfTrue:
    case IrOpcode::kIfFalse:
    case IrOpcode::kIfSuccess:
    case IrOpcode::kSwitch:
    case IrOpcode::kIfValue:
    case IrOpcode::kIfDefault:
    case IrOpcode::kEffectPhi:
    case IrOpcode::kMerge:
    case IrOpcode::kTerminate:
    case IrOpcode::kBeginRegion:
      // No code needed for these graph artifacts.
      return;
    case IrOpcode::kIfException:
      return MarkAsTagged(node), VisitIfException(node);
    case IrOpcode::kFinishRegion:
      return MarkAsTagged(node), VisitFinishRegion(node);
    case IrOpcode::kParameter: {
      // Parameters should always be scheduled to the first block.
      DCHECK_EQ(this->rpo_number(this->block(schedule(), node)).ToInt(), 0);
      MachineType type =
          linkage()->GetParameterType(ParameterIndexOf(node->op()));
      MarkAsRepresentation(type.representation(), node);
      return VisitParameter(node);
    }
    case IrOpcode::kOsrValue:
      return MarkAsTagged(node), VisitOsrValue(node);
    case IrOpcode::kPhi: {
      MachineRepresentation rep = PhiRepresentationOf(node->op());
      if (rep == MachineRepresentation::kNone) return;
      MarkAsRepresentation(rep, node);
      return VisitPhi(node);
    }
    case IrOpcode::kProjection:
      return VisitProjection(node);
    case IrOpcode::kInt32Constant:
    case IrOpcode::kInt64Constant:
    case IrOpcode::kTaggedIndexConstant:
    case IrOpcode::kExternalConstant:
    case IrOpcode::kRelocatableInt64Constant:
      return VisitConstant(node);
    case IrOpcode::kRelocatableInt32Constant:
      return MarkAsWord32(node), VisitConstant(node);
    case IrOpcode::kFloat32Constant:
      return MarkAsFloat32(node), VisitConstant(node);
    case IrOpcode::kFloat64Constant:
      return MarkAsFloat64(node), VisitConstant(node);
    case IrOpcode::kHeapConstant:
      return MarkAsTagged(node), VisitConstant(node);
    case IrOpcode::kCompressedHeapConstant:
      return MarkAsCompressed(node), VisitConstant(node);
    case IrOpcode::kTrustedHeapConstant:
      return MarkAsTagged(node), VisitConstant(node);
    case IrOpcode::kNumberConstant: {
      double value = OpParameter<double>(node->op());
      if (!IsSmiDouble(value)) MarkAsTagged(node);
      return VisitConstant(node);
    }
    case IrOpcode::kCall:
      return VisitCall(node);
    case IrOpcode::kDeoptimizeIf:
      return VisitDeoptimizeIf(node);
    case IrOpcode::kDeoptimizeUnless:
      return VisitDeoptimizeUnless(node);
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kTrapIf:
      return VisitTrapIf(node, TrapIdOf(node->op()));
    case IrOpcode::kTrapUnless:
      return VisitTrapUnless(node, TrapIdOf(node->op()));
#endif  // V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kFrameState:
    case IrOpcode::kStateValues:
    case IrOpcode::kObjectState:
      return;
    case IrOpcode::kAbortCSADcheck:
      VisitAbortCSADcheck(node);
      return;
    case IrOpcode::kDebugBreak:
      VisitDebugBreak(node);
      return;
    case IrOpcode::kUnreachable:
      VisitUnreachable(node);
      return;
    case IrOpcode::kStaticAssert:
      VisitStaticAssert(node);
      return;
    case IrOpcode::kDeadValue:
      VisitDeadValue(node);
      return;
    case IrOpcode::kComment:
      VisitComment(node);
      return;
    case IrOpcode::kRetain:
      VisitRetain(node);
      return;
    case IrOpcode::kLoad:
    case IrOpcode::kLoadImmutable: {
      LoadRepresentation type = LoadRepresentationOf(node->op());
      MarkAsRepresentation(type.representation(), node);
      return VisitLoad(node);
    }
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kLoadTransform: {
      LoadTransformParameters params = LoadTransformParametersOf(node->op());
      if (params.transformation >= LoadTransformation::kFirst256Transform) {
        MarkAsRepresentation(MachineRepresentation::kSimd256, node);
      } else {
        MarkAsRepresentation(MachineRepresentation::kSimd128, node);
      }
      return VisitLoadTransform(node);
    }
    case IrOpcode::kLoadLane: {
      MarkAsRepresentation(MachineRepresentation::kSimd128, node);
      return VisitLoadLane(node);
    }
    case IrOpcode::kStoreLane: {
      MarkAsRepresentation(MachineRepresentation::kSimd128, node);
      return VisitStoreLane(node);
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kStore:
    case IrOpcode::kStoreIndirectPointer:
      return VisitStore(node);
    case IrOpcode::kStorePair:
      return VisitStorePair(node);
    case IrOpcode::kProtectedStore:
    case IrOpcode::kStoreTrapOnNull:
      return VisitProtectedStore(node);
    case IrOpcode::kWord32And:
      return MarkAsWord32(node), VisitWord32And(node);
    case IrOpcode::kWord32Or:
      return MarkAsWord32(node), VisitWord32Or(node);
    case IrOpcode::kWord32Xor:
      return MarkAsWord32(node), VisitWord32Xor(node);
    case IrOpcode::kWord32Shl:
      return MarkAsWord32(node), VisitWord32Shl(node);
    case IrOpcode::kWord32Shr:
      return MarkAsWord32(node), VisitWord32Shr(node);
    case IrOpcode::kWord32Sar:
      return MarkAsWord32(node), VisitWord32Sar(node);
    case IrOpcode::kWord32Rol:
      return MarkAsWord32(node), VisitWord32Rol(node);
    case IrOpcode::kWord32Ror:
      return MarkAsWord32(node), VisitWord32Ror(node);
    case IrOpcode::kWord32Equal:
      return VisitWord32Equal(node);
    case IrOpcode::kWord32Clz:
      return MarkAsWord32(node), VisitWord32Clz(node);
    case IrOpcode::kWord32Ctz:
      return MarkAsWord32(node), VisitWord32Ctz(node);
    case IrOpcode::kWord32ReverseBits:
      return MarkAsWord32(node), VisitWord32ReverseBits(node);
    case IrOpcode::kWord32ReverseBytes:
      return MarkAsWord32(node), VisitWord32ReverseBytes(node);
    case IrOpcode::kInt32AbsWithOverflow:
      return MarkAsWord32(node), VisitInt32AbsWithOverflow(node);
    case IrOpcode::kWord32Popcnt:
      return MarkAsWord32(node), VisitWord32Popcnt(node);
    case IrOpcode::kWord64Popcnt:
      return MarkAsWord64(node), VisitWord64Popcnt(node);
    case IrOpcode::kWord32Select:
      return MarkAsWord32(node), VisitSelect(node);
    case IrOpcode::kWord64And:
      return MarkAsWord64(node), VisitWord64And(node);
    case IrOpcode::kWord64Or:
      return MarkAsWord64(node), VisitWord64Or(node);
    case IrOpcode::kWord64Xor:
      return MarkAsWord64(node), VisitWord64Xor(node);
    case IrOpcode::kWord64Shl:
      return MarkAsWord64(node), VisitWord64Shl(node);
    case IrOpcode::kWord64Shr:
      return MarkAsWord64(node), VisitWord64Shr(node);
    case IrOpcode::kWord64Sar:
      return MarkAsWord64(node), VisitWord64Sar(node);
    case IrOpcode::kWord64Rol:
      return MarkAsWord64(node), VisitWord64Rol(node);
    case IrOpcode::kWord64Ror:
      return MarkAsWord64(node), VisitWord64Ror(node);
    case IrOpcode::kWord64Clz:
      return MarkAsWord64(node), VisitWord64Clz(node);
    case IrOpcode::kWord64Ctz:
      return MarkAsWord64(node), VisitWord64Ctz(node);
    case IrOpcode::kWord64ReverseBits:
      return MarkAsWord64(node), VisitWord64ReverseBits(node);
    case IrOpcode::kWord64ReverseBytes:
      return MarkAsWord64(node), VisitWord64ReverseBytes(node);
    case IrOpcode::kSimd128ReverseBytes:
      return MarkAsSimd128(node), VisitSimd128ReverseBytes(node);
    case IrOpcode::kInt64AbsWithOverflow:
      return MarkAsWord64(node), VisitInt64AbsWithOverflow(node);
    case IrOpcode::kWord64Equal:
      return VisitWord64Equal(node);
    case IrOpcode::kWord64Select:
      return MarkAsWord64(node), VisitSelect(node);
    case IrOpcode::kInt32Add:
      return MarkAsWord32(node), VisitInt32Add(node);
    case IrOpcode::kInt32AddWithOverflow:
      return MarkAsWord32(node), VisitInt32AddWithOverflow(node);
    case IrOpcode::kInt32Sub:
      return MarkAsWord32(node), VisitInt32Sub(node);
    case IrOpcode::kInt32SubWithOverflow:
      return VisitInt32SubWithOverflow(node);
    case IrOpcode::kInt32Mul:
      return MarkAsWord32(node), VisitInt32Mul(node);
    case IrOpcode::kInt32MulWithOverflow:
      return MarkAsWord32(node), VisitInt32MulWithOverflow(node);
    case IrOpcode::kInt32MulHigh:
      return VisitInt32MulHigh(node);
    case IrOpcode::kInt64MulHigh:
      return VisitInt64MulHigh(node);
    case IrOpcode::kInt32Div:
      return MarkAsWord32(node), VisitInt32Div(node);
    case IrOpcode::kInt32Mod:
      return MarkAsWord32(node), VisitInt32Mod(node);
    case IrOpcode::kInt32LessThan:
      return VisitInt32LessThan(node);
    case IrOpcode::kInt32LessThanOrEqual:
      return VisitInt32LessThanOrEqual(node);
    case IrOpcode::kUint32Div:
      return MarkAsWord32(node), VisitUint32Div(node);
    case IrOpcode::kUint32LessThan:
      return VisitUint32LessThan(node);
    case IrOpcode::kUint32LessThanOrEqual:
      return VisitUint32LessThanOrEqual(node);
    case IrOpcode::kUint32Mod:
      return MarkAsWord32(node), VisitUint32Mod(node);
    case IrOpcode::kUint32MulHigh:
      return VisitUint32MulHigh(node);
    case IrOpcode::kUint64MulHigh:
      return VisitUint64MulHigh(node);
    case IrOpcode::kInt64Add:
      return MarkAsWord64(node), VisitInt64Add(node);
    case IrOpcode::kInt64AddWithOverflow:
      return MarkAsWord64(node), VisitInt64AddWithOverflow(node);
    case IrOpcode::kInt64Sub:
      return MarkAsWord64(node), VisitInt64Sub(node);
    case IrOpcode::kInt64SubWithOverflow:
      return MarkAsWord64(node), VisitInt64SubWithOverflow(node);
    case IrOpcode::kInt64Mul:
      return MarkAsWord64(node), VisitInt64Mul(node);
    case IrOpcode::kInt64MulWithOverflow:
      return MarkAsWord64(node), VisitInt64MulWithOverflow(node);
    case IrOpcode::kInt64Div:
      return MarkAsWord64(node), VisitInt64Div(node);
    case IrOpcode::kInt64Mod:
      return MarkAsWord64(node), VisitInt64Mod(node);
    case IrOpcode::kInt64LessThan:
      return VisitInt64LessThan(node);
    case IrOpcode::kInt64LessThanOrEqual:
      return VisitInt64LessThanOrEqual(node);
    case IrOpcode::kUint64Div:
      return MarkAsWord64(node), VisitUint64Div(node);
    case IrOpcode::kUint64LessThan:
      return VisitUint64LessThan(node);
    case IrOpcode::kUint64LessThanOrEqual:
      return VisitUint64LessThanOrEqual(node);
    case IrOpcode::kUint64Mod:
      return MarkAsWord64(node), VisitUint64Mod(node);
    case IrOpcode::kBitcastTaggedToWord:
    case IrOpcode::kBitcastTaggedToWordForTagAndSmiBits:
      return MarkAsRepresentation(MachineType::PointerRepresentation(), node),
             VisitBitcastTaggedToWord(node);
    case IrOpcode::kBitcastWordToTagged:
      return MarkAsTagged(node), VisitBitcastWordToTagged(node);
    case IrOpcode::kBitcastWordToTaggedSigned:
      return MarkAsRepresentation(MachineRepresentation::kTaggedSigned, node),
             EmitIdentity(node);
    case IrOpcode::kChangeFloat32ToFloat64:
      return MarkAsFloat64(node), VisitChangeFloat32ToFloat64(node);
    case IrOpcode::kChangeInt32ToFloat64:
      return MarkAsFloat64(node), VisitChangeInt32ToFloat64(node);
    case IrOpcode::kChangeInt64ToFloat64:
      return MarkAsFloat64(node), VisitChangeInt64ToFloat64(node);
    case IrOpcode::kChangeUint32ToFloat64:
      return MarkAsFloat64(node), VisitChangeUint32ToFloat64(node);
    case IrOpcode::kChangeFloat64ToInt32:
      return MarkAsWord32(node), VisitChangeFloat64ToInt32(node);
    case IrOpcode::kChangeFloat64ToInt64:
      return MarkAsWord64(node), VisitChangeFloat64ToInt64(node);
    case IrOpcode::kChangeFloat64ToUint32:
      return MarkAsWord32(node), VisitChangeFloat64ToUint32(node);
    case IrOpcode::kChangeFloat64ToUint64:
      return MarkAsWord64(node), VisitChangeFloat64ToUint64(node);
    case IrOpcode::kFloat64SilenceNaN:
      MarkAsFloat64(node);
      if (CanProduceSignalingNaN(node->InputAt(0))) {
        return VisitFloat64SilenceNaN(node);
      } else {
        return EmitIdentity(node);
      }
    case IrOpcode::kTruncateFloat64ToInt64:
      return MarkAsWord64(node), VisitTruncateFloat64ToInt64(node);
    case IrOpcode::kTruncateFloat64ToUint32:
      return MarkAsWord32(node), VisitTruncateFloat64ToUint32(node);
    case IrOpcode::kTruncateFloat32ToInt32:
      return MarkAsWord32(node), VisitTruncateFloat32ToInt32(node);
    case IrOpcode::kTruncateFloat32ToUint32:
      return MarkAsWord32(node), VisitTruncateFloat32ToUint32(node);
    case IrOpcode::kTryTruncateFloat32ToInt64:
      return MarkAsWord64(node), VisitTryTruncateFloat32ToInt64(node);
    case IrOpcode::kTryTruncateFloat64ToInt64:
      return MarkAsWord64(node), VisitTryTruncateFloat64ToInt64(node);
    case IrOpcode::kTryTruncateFloat32ToUint64:
      return MarkAsWord64(node), VisitTryTruncateFloat32ToUint64(node);
    case IrOpcode::kTryTruncateFloat64ToUint64:
      return MarkAsWord64(node), VisitTryTruncateFloat64ToUint64(node);
    case IrOpcode::kTryTruncateFloat64ToInt32:
      return MarkAsWord32(node), VisitTryTruncateFloat64ToInt32(node);
    case IrOpcode::kTryTruncateFloat64ToUint32:
      return MarkAsWord32(node), VisitTryTruncateFloat64ToUint32(node);
    case IrOpcode::kBitcastWord32ToWord64:
      MarkAsWord64(node);
      return VisitBitcastWord32ToWord64(node);
    case IrOpcode::kChangeInt32ToInt64:
      return MarkAsWord64(node), VisitChangeInt32ToInt64(node);
    case IrOpcode::kChangeUint32ToUint64:
      return MarkAsWord64(node), VisitChangeUint32ToUint64(node);
    case IrOpcode::kTruncateFloat64ToFloat32:
      return MarkAsFloat32(node), VisitTruncateFloat64ToFloat32(node);
    case IrOpcode::kTruncateFloat64ToWord32:
      return MarkAsWord32(node), VisitTruncateFloat64ToWord32(node);
    case IrOpcode::kTruncateInt64ToInt32:
      return MarkAsWord32(node), VisitTruncateInt64ToInt32(node);
    case IrOpcode::kRoundFloat64ToInt32:
      return MarkAsWord32(node), VisitRoundFloat64ToInt32(node);
    case IrOpcode::kRoundInt64ToFloat32:
      return MarkAsFloat32(node), VisitRoundInt64ToFloat32(node);
    case IrOpcode::kRoundInt32ToFloat32:
      return MarkAsFloat32(node), VisitRoundInt32ToFloat32(node);
    case IrOpcode::kRoundInt64ToFloat64:
      return MarkAsFloat64(node), VisitRoundInt64ToFloat64(node);
    case IrOpcode::kBitcastFloat32ToInt32:
      return MarkAsWord32(node), VisitBitcastFloat32ToInt32(node);
    case IrOpcode::kRoundUint32ToFloat32:
      return MarkAsFloat32(node), VisitRoundUint32ToFloat32(node);
    case IrOpcode::kRoundUint64ToFloat32:
      return MarkAsFloat32(node), VisitRoundUint64ToFloat32(node);
    case IrOpcode::kRoundUint64ToFloat64:
      return MarkAsFloat64(node), VisitRoundUint64ToFloat64(node);
    case IrOpcode::kBitcastFloat64ToInt64:
      return MarkAsWord64(node), VisitBitcastFloat64ToInt64(node);
    case IrOpcode::kBitcastInt32ToFloat32:
      return MarkAsFloat32(node), VisitBitcastInt32ToFloat32(node);
    case IrOpcode::kBitcastInt64ToFloat64:
      return MarkAsFloat64(node), VisitBitcastInt64ToFloat64(node);
    case IrOpcode::kFloat32Add:
      return MarkAsFloat32(node), VisitFloat32Add(node);
    case IrOpcode::kFloat32Sub:
      return MarkAsFloat32(node), VisitFloat32Sub(node);
    case IrOpcode::kFloat32Neg:
      return MarkAsFloat32(node), VisitFloat32Neg(node);
    case IrOpcode::kFloat32Mul:
      return MarkAsFloat32(node), VisitFloat32Mul(node);
    case IrOpcode::kFloat32Div:
      return MarkAsFloat32(node), VisitFloat32Div(node);
    case IrOpcode::kFloat32Abs:
      return MarkAsFloat32(node), VisitFloat32Abs(node);
    case IrOpcode::kFloat32Sqrt:
      return MarkAsFloat32(node), VisitFloat32Sqrt(node);
    case IrOpcode::kFloat32Equal:
      return VisitFloat32Equal(node);
    case IrOpcode::kFloat32LessThan:
      return VisitFloat32LessThan(node);
    case IrOpcode::kFloat32LessThanOrEqual:
      return VisitFloat32LessThanOrEqual(node);
    case IrOpcode::kFloat32Max:
      return MarkAsFloat32(node), VisitFloat32Max(node);
    case IrOpcode::kFloat32Min:
      return MarkAsFloat32(node), VisitFloat32Min(node);
    case IrOpcode::kFloat32Select:
      return MarkAsFloat32(node), VisitSelect(node);
    case IrOpcode::kFloat64Add:
      return MarkAsFloat64(node), VisitFloat64Add(node);
    case IrOpcode::kFloat64Sub:
      return MarkAsFloat64(node), VisitFloat64Sub(node);
    case IrOpcode::kFloat64Neg:
      return MarkAsFloat64(node), VisitFloat64Neg(node);
    case IrOpcode::kFloat64Mul:
      return MarkAsFloat64(node), VisitFloat64Mul(node);
    case IrOpcode::kFloat64Div:
      return MarkAsFloat64(node), VisitFloat64Div(node);
    case IrOpcode::kFloat64Mod:
      return MarkAsFloat64(node), VisitFloat64Mod(node);
    case IrOpcode::kFloat64Min:
      return MarkAsFloat64(node), VisitFloat64Min(node);
    case IrOpcode::kFloat64Max:
      return MarkAsFloat64(node), VisitFloat64Max(node);
    case IrOpcode::kFloat64Abs:
      return MarkAsFloat64(node), VisitFloat64Abs(node);
    case IrOpcode::kFloat64Acos:
      return MarkAsFloat64(node), VisitFloat64Acos(node);
    case IrOpcode::kFloat64Acosh:
      return MarkAsFloat64(node), VisitFloat64Acosh(node);
    case IrOpcode::kFloat64Asin:
      return MarkAsFloat64(node), VisitFloat64Asin(node);
    case IrOpcode::kFloat64Asinh:
      return MarkAsFloat64(node), VisitFloat64Asinh(node);
    case IrOpcode::kFloat64Atan:
      return MarkAsFloat64(node), VisitFloat64Atan(node);
    case IrOpcode::kFloat64Atanh:
      return MarkAsFloat64(node), VisitFloat64Atanh(node);
    case IrOpcode::kFloat64Atan2:
      return MarkAsFloat64(node), VisitFloat64Atan2(node);
    case IrOpcode::kFloat64Cbrt:
      return MarkAsFloat64(node), VisitFloat64Cbrt(node);
    case IrOpcode::kFloat64Cos:
      return MarkAsFloat64(node), VisitFloat64Cos(node);
    case IrOpcode::kFloat64Cosh:
      return MarkAsFloat64(node), VisitFloat64Cosh(node);
    case IrOpcode::kFloat64Exp:
      return MarkAsFloat64(node), VisitFloat64Exp(node);
    case IrOpcode::kFloat64Expm1:
      return MarkAsFloat64(node), VisitFloat64Expm1(node);
    case IrOpcode::kFloat64Log:
      return MarkAsFloat64(node), VisitFloat64Log(node);
    case IrOpcode::kFloat64Log1p:
      return MarkAsFloat64(node), VisitFloat64Log1p(node);
    case IrOpcode::kFloat64Log10:
      return MarkAsFloat64(node), VisitFloat64Log10(node);
    case IrOpcode::kFloat64Log2:
      return MarkAsFloat64(node), VisitFloat64Log2(node);
    case IrOpcode::kFloat64Pow:
      return MarkAsFloat64(node), VisitFloat64Pow(node);
    case IrOpcode::kFloat64Sin:
      return MarkAsFloat64(node), VisitFloat64Sin(node);
    case IrOpcode::kFloat64Sinh:
      return MarkAsFloat64(node), VisitFloat64Sinh(node);
    case IrOpcode::kFloat64Sqrt:
      return MarkAsFloat64(node), VisitFloat64Sqrt(node);
    case IrOpcode::kFloat64Tan:
      return MarkAsFloat64(node), VisitFloat64Tan(node);
    case IrOpcode::kFloat64Tanh:
      return MarkAsFloat64(node), VisitFloat64Tanh(node);
    case IrOpcode::kFloat64Equal:
      return VisitFloat64Equal(node);
    case IrOpcode::kFloat64LessThan:
      return VisitFloat64LessThan(node);
    case IrOpcode::kFloat64LessThanOrEqual:
      return VisitFloat64LessThanOrEqual(node);
    case IrOpcode::kFloat64Select:
      return MarkAsFloat64(node), VisitSelect(node);
    case IrOpcode::kFloat32RoundDown:
      return MarkAsFloat32(node), VisitFloat32RoundDown(node);
    case IrOpcode::kFloat64RoundDown:
      return MarkAsFloat64(node), VisitFloat64RoundDown(node);
    case IrOpcode::kFloat32RoundUp:
      return MarkAsFloat32(node), VisitFloat32RoundUp(node);
    case IrOpcode::kFloat64RoundUp:
      return MarkAsFloat64(node), VisitFloat64RoundUp(node);
    case IrOpcode::kFloat32RoundTruncate:
      return MarkAsFloat32(node), VisitFloat32RoundTruncate(node);
    case IrOpcode::kFloat64RoundTruncate:
      return MarkAsFloat64(node), VisitFloat64RoundTruncate(node);
    case IrOpcode::kFloat64RoundTiesAway:
      return MarkAsFloat64(node), VisitFloat64RoundTiesAway(node);
    case IrOpcode::kFloat32RoundTiesEven:
      return MarkAsFloat32(node), VisitFloat32RoundTiesEven(node);
    case IrOpcode::kFloat64RoundTiesEven:
      return MarkAsFloat64(node), VisitFloat64RoundTiesEven(node);
    case IrOpcode::kFloat64ExtractLowWord32:
      return MarkAsWord32(node), VisitFloat64ExtractLowWord32(node);
    case IrOpcode::kFloat64ExtractHighWord32:
      return MarkAsWord32(node), VisitFloat64ExtractHighWord32(node);
    case IrOpcode::kFloat64InsertLowWord32:
      return MarkAsFloat64(node), VisitFloat64InsertLowWord32(node);
    case IrOpcode::kFloat64InsertHighWord32:
      return MarkAsFloat64(node), VisitFloat64InsertHighWord32(node);
    case IrOpcode::kStackSlot:
      return VisitStackSlot(node);
    case IrOpcode::kStackPointerGreaterThan:
      return VisitStackPointerGreaterThan(node);
    case IrOpcode::kLoadStackCheckOffset:
      return VisitLoadStackCheckOffset(node);
    case IrOpcode::kLoadFramePointer:
      return VisitLoadFramePointer(node);
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kLoadStackPointer:
      return VisitLoadStackPointer(node);
    case IrOpcode::kSetStackPointer:
      return VisitSetStackPointer(node);
#endif  // V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kLoadParentFramePointer:
      return VisitLoadParentFramePointer(node);
    case IrOpcode::kLoadRootRegister:
      return VisitLoadRootRegister(node);
    case IrOpcode::kUnalignedLoad: {
      LoadRepresentation type = LoadRepresentationOf(node->op());
      MarkAsRepresentation(type.representation(), node);
      return VisitUnalignedLoad(node);
    }
    case IrOpcode::kUnalignedStore:
      return VisitUnalignedStore(node);
    case IrOpcode::kInt32PairAdd:
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      return VisitInt32PairAdd(node);
    case IrOpcode::kInt32PairSub:
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      return VisitInt32PairSub(node);
    case IrOpcode::kInt32PairMul:
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      return VisitInt32PairMul(node);
    case IrOpcode::kWord32PairShl:
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      return VisitWord32PairShl(node);
    case IrOpcode::kWord32PairShr:
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      return VisitWord32PairShr(node);
    case IrOpcode::kWord32PairSar:
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      return VisitWord32PairSar(node);
    case IrOpcode::kMemoryBarrier:
      return VisitMemoryBarrier(node);
    case IrOpcode::kWord32AtomicLoad: {
      AtomicLoadParameters params = AtomicLoadParametersOf(node->op());
      LoadRepresentation type = params.representation();
      MarkAsRepresentation(type.representation(), node);
      return VisitWord32AtomicLoad(node);
    }
    case IrOpcode::kWord64AtomicLoad: {
      AtomicLoadParameters params = AtomicLoadParametersOf(node->op());
      LoadRepresentation type = params.representation();
      MarkAsRepresentation(type.representation(), node);
      return VisitWord64AtomicLoad(node);
    }
    case IrOpcode::kWord32AtomicStore:
      return VisitWord32AtomicStore(node);
    case IrOpcode::kWord64AtomicStore:
      return VisitWord64AtomicStore(node);
    case IrOpcode::kWord32AtomicPairStore:
      return VisitWord32AtomicPairStore(node);
    case IrOpcode::kWord32AtomicPairLoad: {
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      return VisitWord32AtomicPairLoad(node);
    }
#define ATOMIC_CASE(name, rep)                         \
  case IrOpcode::k##rep##Atomic##name: {               \
    MachineType type = AtomicOpType(node->op());       \
    MarkAsRepresentation(type.representation(), node); \
    return Visit##rep##Atomic##name(node);             \
  }
      ATOMIC_CASE(Add, Word32)
      ATOMIC_CASE(Add, Word64)
      ATOMIC_CASE(Sub, Word32)
      ATOMIC_CASE(Sub, Word64)
      ATOMIC_CASE(And, Word32)
      ATOMIC_CASE(And, Word64)
      ATOMIC_CASE(Or, Word32)
      ATOMIC_CASE(Or, Word64)
      ATOMIC_CASE(Xor, Word32)
      ATOMIC_CASE(Xor, Word64)
      ATOMIC_CASE(Exchange, Word32)
      ATOMIC_CASE(Exchange, Word64)
      ATOMIC_CASE(CompareExchange, Word32)
      ATOMIC_CASE(CompareExchange, Word64)
#undef ATOMIC_CASE
#define ATOMIC_CASE(name)                     \
  case IrOpcode::kWord32AtomicPair##name: {   \
    MarkAsWord32(node);                       \
    MarkPairProjectionsAsWord32(node);        \
    return VisitWord32AtomicPair##name(node); \
  }
      ATOMIC_CASE(Add)
      ATOMIC_CASE(Sub)
      ATOMIC_CASE(And)
      ATOMIC_CASE(Or)
      ATOMIC_CASE(Xor)
      ATOMIC_CASE(Exchange)
      ATOMIC_CASE(CompareExchange)
#undef ATOMIC_CASE
    case IrOpcode::kProtectedLoad:
    case IrOpcode::kLoadTrapOnNull: {
      LoadRepresentation type = LoadRepresentationOf(node->op());
      MarkAsRepresentation(type.representation(), node);
      return VisitProtectedLoad(node);
    }
    case IrOpcode::kSignExtendWord8ToInt32:
      return MarkAsWord32(node), VisitSignExtendWord8ToInt32(node);
    case IrOpcode::kSignExtendWord16ToInt32:
      return MarkAsWord32(node), VisitSignExtendWord16ToInt32(node);
    case IrOpcode::kSignExtendWord8ToInt64:
      return MarkAsWord64(node), VisitSignExtendWord8ToInt64(node);
    case IrOpcode::kSignExtendWord16ToInt64:
      return MarkAsWord64(node), VisitSignExtendWord16ToInt64(node);
    case IrOpcode::kSignExtendWord32ToInt64:
      return MarkAsWord64(node), VisitSignExtendWord32ToInt64(node);
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kF64x2Splat:
      return MarkAsSimd128(node), VisitF64x2Splat(node);
    case IrOpcode::kF64x2ExtractLane:
      return MarkAsFloat64(node), VisitF64x2ExtractLane(node);
    case IrOpcode::kF64x2ReplaceLane:
      return MarkAsSimd128(node), VisitF64x2ReplaceLane(node);
    case IrOpcode::kF64x2Abs:
      return MarkAsSimd128(node), VisitF64x2Abs(node);
    case IrOpcode::kF64x2Neg:
      return MarkAsSimd128(node), VisitF64x2Neg(node);
    case IrOpcode::kF64x2Sqrt:
      return MarkAsSimd128(node), VisitF64x2Sqrt(node);
    case IrOpcode::kF64x2Add:
      return MarkAsSimd128(node), VisitF64x2Add(node);
    case IrOpcode::kF64x2Sub:
      return MarkAsSimd128(node), VisitF64x2Sub(node);
    case IrOpcode::kF64x2Mul:
      return MarkAsSimd128(node), VisitF64x2Mul(node);
    case IrOpcode::kF64x2Div:
      return MarkAsSimd128(node), VisitF64x2Div(node);
    case IrOpcode::kF64x2Min:
      return MarkAsSimd128(node), VisitF64x2Min(node);
    case IrOpcode::kF64x2Max:
      return MarkAsSimd128(node), VisitF64x2Max(node);
    case IrOpcode::kF64x2Eq:
      return MarkAsSimd128(node), VisitF64x2Eq(node);
    case IrOpcode::kF64x2Ne:
      return MarkAsSimd128(node), VisitF64x2Ne(node);
    case IrOpcode::kF64x2Lt:
      return MarkAsSimd128(node), VisitF64x2Lt(node);
    case IrOpcode::kF64x2Le:
      return MarkAsSimd128(node), VisitF64x2Le(node);
    case IrOpcode::kF64x2Qfma:
      return MarkAsSimd128(node), VisitF64x2Qfma(node);
    case IrOpcode::kF64x2Qfms:
      return MarkAsSimd128(node), VisitF64x2Qfms(node);
    case IrOpcode::kF64x2Pmin:
      return MarkAsSimd128(node), VisitF64x2Pmin(node);
    case IrOpcode::kF64x2Pmax:
      return MarkAsSimd128(node), VisitF64x2Pmax(node);
    case IrOpcode::kF64x2Ceil:
      return MarkAsSimd128(node), VisitF64x2Ceil(node);
    case IrOpcode::kF64x2Floor:
      return MarkAsSimd128(node), VisitF64x2Floor(node);
    case IrOpcode::kF64x2Trunc:
      return MarkAsSimd128(node), VisitF64x2Trunc(node);
    case IrOpcode::kF64x2NearestInt:
      return MarkAsSimd128(node), VisitF64x2NearestInt(node);
    case IrOpcode::kF64x2ConvertLowI32x4S:
      return MarkAsSimd128(node), VisitF64x2ConvertLowI32x4S(node);
    case IrOpcode::kF64x2ConvertLowI32x4U:
      return MarkAsSimd128(node), VisitF64x2ConvertLowI32x4U(node);
    case IrOpcode::kF64x2PromoteLowF32x4:
      return MarkAsSimd128(node), VisitF64x2PromoteLowF32x4(node);
    case IrOpcode::kF32x4Splat:
      return MarkAsSimd128(node), VisitF32x4Splat(node);
    case IrOpcode::kF32x4ExtractLane:
      return MarkAsFloat32(node), VisitF32x4ExtractLane(node);
    case IrOpcode::kF32x4ReplaceLane:
      return MarkAsSimd128(node), VisitF32x4ReplaceLane(node);
    case IrOpcode::kF32x4SConvertI32x4:
      return MarkAsSimd128(node), VisitF32x4SConvertI32x4(node);
    case IrOpcode::kF32x4UConvertI32x4:
      return MarkAsSimd128(node), VisitF32x4UConvertI32x4(node);
    case IrOpcode::kF32x4Abs:
      return MarkAsSimd128(node), VisitF32x4Abs(node);
    case IrOpcode::kF32x4Neg:
      return MarkAsSimd128(node), VisitF32x4Neg(node);
    case IrOpcode::kF32x4Sqrt:
      return MarkAsSimd128(node), VisitF32x4Sqrt(node);
    case IrOpcode::kF32x4Add:
      return MarkAsSimd128(node), VisitF32x4Add(node);
    case IrOpcode::kF32x4Sub:
      return MarkAsSimd128(node), VisitF32x4Sub(node);
    case IrOpcode::kF32x4Mul:
      return MarkAsSimd128(node), VisitF32x4Mul(node);
    case IrOpcode::kF32x4Div:
      return MarkAsSimd128(node), VisitF32x4Div(node);
    case IrOpcode::kF32x4Min:
      return MarkAsSimd128(node), VisitF32x4Min(node);
    case IrOpcode::kF32x4Max:
      return MarkAsSimd128(node), VisitF32x4Max(node);
    case IrOpcode::kF32x4Eq:
      return MarkAsSimd128(node), VisitF32x4Eq(node);
    case IrOpcode::kF32x4Ne:
      return MarkAsSimd128(node), VisitF32x4Ne(node);
    case IrOpcode::kF32x4Lt:
      return MarkAsSimd128(node), VisitF32x4Lt(node);
    case IrOpcode::kF32x4Le:
      return MarkAsSimd128(node), VisitF32x4Le(node);
    case IrOpcode::kF32x4Qfma:
      return MarkAsSimd128(node), VisitF32x4Qfma(node);
    case IrOpcode::kF32x4Qfms:
      return MarkAsSimd128(node), VisitF32x4Qfms(node);
    case IrOpcode::kF32x4Pmin:
      return MarkAsSimd128(node), VisitF32x4Pmin(node);
    case IrOpcode::kF32x4Pmax:
      return MarkAsSimd128(node), VisitF32x4Pmax(node);
    case IrOpcode::kF32x4Ceil:
      return MarkAsSimd128(node), VisitF32x4Ceil(node);
    case IrOpcode::kF32x4Floor:
      return MarkAsSimd128(node), VisitF32x4Floor(node);
    case IrOpcode::kF32x4Trunc:
      return MarkAsSimd128(node), VisitF32x4Trunc(node);
    case IrOpcode::kF32x4NearestInt:
      return MarkAsSimd128(node), VisitF32x4NearestInt(node);
    case IrOpcode::kF32x4DemoteF64x2Zero:
      return MarkAsSimd128(node), VisitF32x4DemoteF64x2Zero(node);
    case IrOpcode::kI64x2Splat:
      return MarkAsSimd128(node), VisitI64x2Splat(node);
    case IrOpcode::kI64x2SplatI32Pair:
      return MarkAsSimd128(node), VisitI64x2SplatI32Pair(node);
    case IrOpcode::kI64x2ExtractLane:
      return MarkAsWord64(node), VisitI64x2ExtractLane(node);
    case IrOpcode::kI64x2ReplaceLane:
      return MarkAsSimd128(node), VisitI64x2ReplaceLane(node);
    case IrOpcode::kI64x2ReplaceLaneI32Pair:
      return MarkAsSimd128(node), VisitI64x2ReplaceLaneI32Pair(node);
    case IrOpcode::kI64x2Abs:
      return MarkAsSimd128(node), VisitI64x2Abs(node);
    case IrOpcode::kI64x2Neg:
      return MarkAsSimd128(node), VisitI64x2Neg(node);
    case IrOpcode::kI64x2SConvertI32x4Low:
      return MarkAsSimd128(node), VisitI64x2SConvertI32x4Low(node);
    case IrOpcode::kI64x2SConvertI32x4High:
      return MarkAsSimd128(node), VisitI64x2SConvertI32x4High(node);
    case IrOpcode::kI64x2UConvertI32x4Low:
      return MarkAsSimd128(node), VisitI64x2UConvertI32x4Low(node);
    case IrOpcode::kI64x2UConvertI32x4High:
      return MarkAsSimd128(node), VisitI64x2UConvertI32x4High(node);
    case IrOpcode::kI64x2BitMask:
      return MarkAsWord32(node), VisitI64x2BitMask(node);
    case IrOpcode::kI64x2Shl:
      return MarkAsSimd128(node), VisitI64x2Shl(node);
    case IrOpcode::kI64x2ShrS:
      return MarkAsSimd128(node), VisitI64x2ShrS(node);
    case IrOpcode::kI64x2Add:
      return MarkAsSimd128(node), VisitI64x2Add(node);
    case IrOpcode::kI64x2Sub:
      return MarkAsSimd128(node), VisitI64x2Sub(node);
    case IrOpcode::kI64x2Mul:
      return MarkAsSimd128(node), VisitI64x2Mul(node);
    case IrOpcode::kI64x2Eq:
      return MarkAsSimd128(node), VisitI64x2Eq(node);
    case IrOpcode::kI64x2Ne:
      return MarkAsSimd128(node), VisitI64x2Ne(node);
    case IrOpcode::kI64x2GtS:
      return MarkAsSimd128(node), VisitI64x2GtS(node);
    case IrOpcode::kI64x2GeS:
      return MarkAsSimd128(node), VisitI64x2GeS(node);
    case IrOpcode::kI64x2ShrU:
      return MarkAsSimd128(node), VisitI64x2ShrU(node);
    case IrOpcode::kI64x2ExtMulLowI32x4S:
      return MarkAsSimd128(node), VisitI64x2ExtMulLowI32x4S(node);
    case IrOpcode::kI64x2ExtMulHighI32x4S:
      return MarkAsSimd128(node), VisitI64x2ExtMulHighI32x4S(node);
    case IrOpcode::kI64x2ExtMulLowI32x4U:
      return MarkAsSimd128(node), VisitI64x2ExtMulLowI32x4U(node);
    case IrOpcode::kI64x2ExtMulHighI32x4U:
      return MarkAsSimd128(node), VisitI64x2ExtMulHighI32x4U(node);
    case IrOpcode::kI32x4Splat:
      return MarkAsSimd128(node), VisitI32x4Splat(node);
    case IrOpcode::kI32x4ExtractLane:
      return MarkAsWord32(node), VisitI32x4ExtractLane(node);
    case IrOpcode::kI32x4ReplaceLane:
      return MarkAsSimd128(node), VisitI32x4ReplaceLane(node);
    case IrOpcode::kI32x4SConvertF32x4:
      return MarkAsSimd128(node), VisitI32x4SConvertF32x4(node);
    case IrOpcode::kI32x4SConvertI16x8Low:
      return MarkAsSimd128(node), VisitI32x4SConvertI16x8Low(node);
    case IrOpcode::kI32x4SConvertI16x8High:
      return MarkAsSimd128(node), VisitI32x4SConvertI16x8High(node);
    case IrOpcode::kI32x4Neg:
      return MarkAsSimd128(node), VisitI32x4Neg(node);
    case IrOpcode::kI32x4Shl:
      return MarkAsSimd128(node), VisitI32x4Shl(node);
    case IrOpcode::kI32x4ShrS:
      return MarkAsSimd128(node), VisitI32x4ShrS(node);
    case IrOpcode::kI32x4Add:
      return MarkAsSimd128(node), VisitI32x4Add(node);
    case IrOpcode::kI32x4Sub:
      return MarkAsSimd128(node), VisitI32x4Sub(node);
    case IrOpcode::kI32x4Mul:
      return MarkAsSimd128(node), VisitI32x4Mul(node);
    case IrOpcode::kI32x4MinS:
      return MarkAsSimd128(node), VisitI32x4MinS(node);
    case IrOpcode::kI32x4MaxS:
      return MarkAsSimd128(node), VisitI32x4MaxS(node);
    case IrOpcode::kI32x4Eq:
      return MarkAsSimd128(node), VisitI32x4Eq(node);
    case IrOpcode::kI32x4Ne:
      return MarkAsSimd128(node), VisitI32x4Ne(node);
    case IrOpcode::kI32x4GtS:
      return MarkAsSimd128(node), VisitI32x4GtS(node);
    case IrOpcode::kI32x4GeS:
      return MarkAsSimd128(node), VisitI32x4GeS(node);
    case IrOpcode::kI32x4UConvertF32x4:
      return MarkAsSimd128(node), VisitI32x4UConvertF32x4(node);
    case IrOpcode::kI32x4UConvertI16x8Low:
      return MarkAsSimd128(node), VisitI32x4UConvertI16x8Low(node);
    case IrOpcode::kI32x4UConvertI16x8High:
      return MarkAsSimd128(node), VisitI32x4UConvertI16x8High(node);
    case IrOpcode::kI32x4ShrU:
      return MarkAsSimd128(node), VisitI32x4ShrU(node);
    case IrOpcode::kI32x4MinU:
      return MarkAsSimd128(node), VisitI32x4MinU(node);
    case IrOpcode::kI32x4MaxU:
      return MarkAsSimd128(node), VisitI32x4MaxU(node);
    case IrOpcode::kI32x4GtU:
      return MarkAsSimd128(node), VisitI32x4GtU(node);
    case IrOpcode::kI32x4GeU:
      return MarkAsSimd128(node), VisitI32x4GeU(node);
    case IrOpcode::kI32x4Abs:
      return MarkAsSimd128(node), VisitI32x4Abs(node);
    case IrOpcode::kI32x4BitMask:
      return MarkAsWord32(node), VisitI32x4BitMask(node);
    case IrOpcode::kI32x4DotI16x8S:
      return MarkAsSimd128(node), VisitI32x4DotI16x8S(node);
    case IrOpcode::kI32x4ExtMulLowI16x8S:
      return MarkAsSimd128(node), VisitI32x4ExtMulLowI16x8S(node);
    case IrOpcode::kI32x4ExtMulHighI16x8S:
      return MarkAsSimd128(node), VisitI32x4ExtMulHighI16x8S(node);
    case IrOpcode::kI32x4ExtMulLowI16x8U:
      return MarkAsSimd128(node), VisitI32x4ExtMulLowI16x8U(node);
    case IrOpcode::kI32x4ExtMulHighI16x8U:
      return MarkAsSimd128(node), VisitI32x4ExtMulHighI16x8U(node);
    case IrOpcode::kI32x4ExtAddPairwiseI16x8S:
      return MarkAsSimd128(node), VisitI32x4ExtAddPairwiseI16x8S(node);
    case IrOpcode::kI32x4ExtAddPairwiseI16x8U:
      return MarkAsSimd128(node), VisitI32x4ExtAddPairwiseI16x8U(node);
    case IrOpcode::kI32x4TruncSatF64x2SZero:
      return MarkAsSimd128(node), VisitI32x4TruncSatF64x2SZero(node);
    case IrOpcode::kI32x4TruncSatF64x2UZero:
      return MarkAsSimd128(node), VisitI32x4TruncSatF64x2UZero(node);
    case IrOpcode::kI16x8Splat:
      return MarkAsSimd128(node), VisitI16x8Splat(node);
    case IrOpcode::kI16x8ExtractLaneU:
      return MarkAsWord32(node), VisitI16x8ExtractLaneU(node);
    case IrOpcode::kI16x8ExtractLaneS:
      return MarkAsWord32(node), VisitI16x8ExtractLaneS(node);
    case IrOpcode::kI16x8ReplaceLane:
      return MarkAsSimd128(node), VisitI16x8ReplaceLane(node);
    case IrOpcode::kI16x8SConvertI8x16Low:
      return MarkAsSimd128(node), VisitI16x8SConvertI8x16Low(node);
    case IrOpcode::kI16x8SConvertI8x16High:
      return MarkAsSimd128(node), VisitI16x8SConvertI8x16High(node);
    case IrOpcode::kI16x8Neg:
      return MarkAsSimd128(node), VisitI16x8Neg(node);
    case IrOpcode::kI16x8Shl:
      return MarkAsSimd128(node), VisitI16x8Shl(node);
    case IrOpcode::kI16x8ShrS:
      return MarkAsSimd128(node), VisitI16x8ShrS(node);
    case IrOpcode::kI16x8SConvertI32x4:
      return MarkAsSimd128(node), VisitI16x8SConvertI32x4(node);
    case IrOpcode::kI16x8Add:
      return MarkAsSimd128(node), VisitI16x8Add(node);
    case IrOpcode::kI16x8AddSatS:
      return MarkAsSimd128(node), VisitI16x8AddSatS(node);
    case IrOpcode::kI16x8Sub:
      return MarkAsSimd128(node), VisitI16x8Sub(node);
    case IrOpcode::kI16x8SubSatS:
      return MarkAsSimd128(node), VisitI16x8SubSatS(node);
    case IrOpcode::kI16x8Mul:
      return MarkAsSimd128(node), VisitI16x8Mul(node);
    case IrOpcode::kI16x8MinS:
      return MarkAsSimd128(node), VisitI16x8MinS(node);
    case IrOpcode::kI16x8MaxS:
      return MarkAsSimd128(node), VisitI16x8MaxS(node);
    case IrOpcode::kI16x8Eq:
      return MarkAsSimd128(node), VisitI16x8Eq(node);
    case IrOpcode::kI16x8Ne:
      return MarkAsSimd128(node), VisitI16x8Ne(node);
    case IrOpcode::kI16x8GtS:
      return MarkAsSimd128(node), VisitI16x8GtS(node);
    case IrOpcode::kI16x8GeS:
      return MarkAsSimd128(node), VisitI16x8GeS(node);
    case IrOpcode::kI16x8UConvertI8x16Low:
      return MarkAsSimd128(node), VisitI16x8UConvertI8x16Low(node);
    case IrOpcode::kI16x8UConvertI8x16High:
      return MarkAsSimd128(node), VisitI16x8UConvertI8x16High(node);
    case IrOpcode::kI16x8ShrU:
      return MarkAsSimd128(node), VisitI16x8ShrU(node);
    case IrOpcode::kI16x8UConvertI32x4:
      return MarkAsSimd128(node), VisitI16x8UConvertI32x4(node);
    case IrOpcode::kI16x8AddSatU:
      return MarkAsSimd128(node), VisitI16x8AddSatU(node);
    case IrOpcode::kI16x8SubSatU:
      return MarkAsSimd128(node), VisitI16x8SubSatU(node);
    case IrOpcode::kI16x8MinU:
      return MarkAsSimd128(node), VisitI16x8MinU(node);
    case IrOpcode::kI16x8MaxU:
      return MarkAsSimd128(node), VisitI16x8MaxU(node);
    case IrOpcode::kI16x8GtU:
      return MarkAsSimd128(node), VisitI16x8GtU(node);
    case IrOpcode::kI16x8GeU:
      return MarkAsSimd128(node), VisitI16x8GeU(node);
    case IrOpcode::kI16x8RoundingAverageU:
      return MarkAsSimd128(node), VisitI16x8RoundingAverageU(node);
    case IrOpcode::kI16x8Q15MulRSatS:
      return MarkAsSimd128(node), VisitI16x8Q15MulRSatS(node);
    case IrOpcode::kI16x8Abs:
      return MarkAsSimd128(node), VisitI16x8Abs(node);
    case IrOpcode::kI16x8BitMask:
      return MarkAsWord32(node), VisitI16x8BitMask(node);
    case IrOpcode::kI16x8ExtMulLowI8x16S:
      return MarkAsSimd128(node), VisitI16x8ExtMulLowI8x16S(node);
    case IrOpcode::kI16x8ExtMulHighI8x16S:
      return MarkAsSimd128(node), VisitI16x8ExtMulHighI8x16S(node);
    case IrOpcode::kI16x8ExtMulLowI8x16U:
      return MarkAsSimd128(node), VisitI16x8ExtMulLowI8x16U(node);
    case IrOpcode::kI16x8ExtMulHighI8x16U:
      return MarkAsSimd128(node), VisitI16x8ExtMulHighI8x16U(node);
    case IrOpcode::kI16x8ExtAddPairwiseI8x16S:
      return MarkAsSimd128(node), VisitI16x8ExtAddPairwiseI8x16S(node);
    case IrOpcode::kI16x8ExtAddPairwiseI8x16U:
      return MarkAsSimd128(node), VisitI16x8ExtAddPairwiseI8x16U(node);
    case IrOpcode::kI8x16Splat:
      return MarkAsSimd128(node), VisitI8x16Splat(node);
    case IrOpcode::kI8x16ExtractLaneU:
      return MarkAsWord32(node), VisitI8x16ExtractLaneU(node);
    case IrOpcode::kI8x16ExtractLaneS:
      return MarkAsWord32(node), VisitI8x16ExtractLaneS(node);
    case IrOpcode::kI8x16ReplaceLane:
      return MarkAsSimd128(node), VisitI8x16ReplaceLane(node);
    case IrOpcode::kI8x16Neg:
      return MarkAsSimd128(node), VisitI8x16Neg(node);
    case IrOpcode::kI8x16Shl:
      return MarkAsSimd128(node), VisitI8x16Shl(node);
    case IrOpcode::kI8x16ShrS:
      return MarkAsSimd128(node), VisitI8x16ShrS(node);
    case IrOpcode::kI8x16SConvertI16x8:
      return MarkAsSimd128(node), VisitI8x16SConvertI16x8(node);
    case IrOpcode::kI8x16Add:
      return MarkAsSimd128(node), VisitI8x16Add(node);
    case IrOpcode::kI8x16AddSatS:
      return MarkAsSimd128(node), VisitI8x16AddSatS(node);
    case IrOpcode::kI8x16Sub:
      return MarkAsSimd128(node), VisitI8x16Sub(node);
    case IrOpcode::kI8x16SubSatS:
      return MarkAsSimd128(node), VisitI8x16SubSatS(node);
    case IrOpcode::kI8x16MinS:
      return MarkAsSimd128(node), VisitI8x16MinS(node);
    case IrOpcode::kI8x16MaxS:
      return MarkAsSimd128(node), VisitI8x16MaxS(node);
    case IrOpcode::kI8x16Eq:
      return MarkAsSimd128(node), VisitI8x16Eq(node);
    case IrOpcode::kI8x16Ne:
      return MarkAsSimd128(node), VisitI8x16Ne(node);
    case IrOpcode::kI8x16GtS:
      return MarkAsSimd128(node), VisitI8x16GtS(node);
    case IrOpcode::kI8x16GeS:
      return MarkAsSimd128(node), VisitI8x16GeS(node);
    case IrOpcode::kI8x16ShrU:
      return MarkAsSimd128(node), VisitI8x16ShrU(node);
    case IrOpcode::kI8x16UConvertI16x8:
      return MarkAsSimd128(node), VisitI8x16UConvertI16x8(node);
    case IrOpcode::kI8x16AddSatU:
      return MarkAsSimd128(node), VisitI8x16AddSatU(node);
    case IrOpcode::kI8x16SubSatU:
      return MarkAsSimd128(node), VisitI8x16SubSatU(node);
    case IrOpcode::kI8x16MinU:
      return MarkAsSimd128(node), VisitI8x16MinU(node);
    case IrOpcode::kI8x16MaxU:
      return MarkAsSimd128(node), VisitI8x16MaxU(node);
    case IrOpcode::kI8x16GtU:
      return MarkAsSimd128(node), VisitI8x16GtU(node);
    case IrOpcode::kI8x16GeU:
      return MarkAsSimd128(node), VisitI8x16GeU(node);
    case IrOpcode::kI8x16RoundingAverageU:
      return MarkAsSimd128(node), VisitI8x16RoundingAverageU(node);
    case IrOpcode::kI8x16Popcnt:
      return MarkAsSimd128(node), VisitI8x16Popcnt(node);
    case IrOpcode::kI8x16Abs:
      return MarkAsSimd128(node), VisitI8x16Abs(node);
    case IrOpcode::kI8x16BitMask:
      return MarkAsWord32(node), VisitI8x16BitMask(node);
    case IrOpcode::kS128Const:
      return MarkAsSimd128(node), VisitS128Const(node);
    case IrOpcode::kS128Zero:
      return MarkAsSimd128(node), VisitS128Zero(node);
    case IrOpcode::kS128And:
      return MarkAsSimd128(node), VisitS128And(node);
    case IrOpcode::kS128Or:
      return MarkAsSimd128(node), VisitS128Or(node);
    case IrOpcode::kS128Xor:
      return MarkAsSimd128(node), VisitS128Xor(node);
    case IrOpcode::kS128Not:
      return MarkAsSimd128(node), VisitS128Not(node);
    case IrOpcode::kS128Select:
      return MarkAsSimd128(node), VisitS128Select(node);
    case IrOpcode::kS128AndNot:
      return MarkAsSimd128(node), VisitS128AndNot(node);
    case IrOpcode::kI8x16Swizzle:
      return MarkAsSimd128(node), VisitI8x16Swizzle(node);
    case IrOpcode::kI8x16Shuffle:
      return MarkAsSimd128(node), VisitI8x16Shuffle(node);
    case IrOpcode::kV128AnyTrue:
      return MarkAsWord32(node), VisitV128AnyTrue(node);
    case IrOpcode::kI64x2AllTrue:
      return MarkAsWord32(node), VisitI64x2AllTrue(node);
    case IrOpcode::kI32x4AllTrue:
      return MarkAsWord32(node), VisitI32x4AllTrue(node);
    case IrOpcode::kI16x8AllTrue:
      return MarkAsWord32(node), VisitI16x8AllTrue(node);
    case IrOpcode::kI8x16AllTrue:
      return MarkAsWord32(node), VisitI8x16AllTrue(node);
    case IrOpcode::kI8x16RelaxedLaneSelect:
      return MarkAsSimd128(node), VisitI8x16RelaxedLaneSelect(node);
    case IrOpcode::kI16x8RelaxedLaneSelect:
      return MarkAsSimd128(node), VisitI16x8RelaxedLaneSelect(node);
    case IrOpcode::kI32x4RelaxedLaneSelect:
      return MarkAsSimd128(node), VisitI32x4RelaxedLaneSelect(node);
    case IrOpcode::kI64x2RelaxedLaneSelect:
      return MarkAsSimd128(node), VisitI64x2RelaxedLaneSelect(node);
    case IrOpcode::kF32x4RelaxedMin:
      return MarkAsSimd128(node), VisitF32x4RelaxedMin(node);
    case IrOpcode::kF32x4RelaxedMax:
      return MarkAsSimd128(node), VisitF32x4RelaxedMax(node);
    case IrOpcode::kF64x2RelaxedMin:
      return MarkAsSimd128(node), VisitF64x2RelaxedMin(node);
    case IrOpcode::kF64x2RelaxedMax:
      return MarkAsSimd128(node), VisitF64x2RelaxedMax(node);
    case IrOpcode::kI32x4RelaxedTruncF64x2SZero:
      return MarkAsSimd128(node), VisitI32x4RelaxedTruncF64x2SZero(node);
    case IrOpcode::kI32x4RelaxedTruncF64x2UZero:
      return MarkAsSimd128(node), VisitI32x4RelaxedTruncF64x2UZero(node);
    case IrOpcode::kI32x4RelaxedTruncF32x4S:
      return MarkAsSimd128(node), VisitI32x4RelaxedTruncF32x4S(node);
    case IrOpcode::kI32x4RelaxedTruncF32x4U:
      return MarkAsSimd128(node), VisitI32x4RelaxedTruncF32x4U(node);
    case IrOpcode::kI16x8RelaxedQ15MulRS:
      return MarkAsSimd128(node), VisitI16x8RelaxedQ15MulRS(node);
    case IrOpcode::kI16x8DotI8x16I7x16S:
      return MarkAsSimd128(node), VisitI16x8DotI8x16I7x16S(node);
    case IrOpcode::kI32x4DotI8x16I7x16AddS:
      return MarkAsSimd128(node), VisitI32x4DotI8x16I7x16AddS(node);
    case IrOpcode::kF16x8Splat:
      return MarkAsSimd128(node), VisitF16x8Splat(node);
    case IrOpcode::kF16x8ExtractLane:
      return MarkAsFloat32(node), VisitF16x8ExtractLane(node);
    case IrOpcode::kF16x8ReplaceLane:
      return MarkAsSimd128(node), VisitF16x8ReplaceLane(node);
    case IrOpcode::kF16x8Abs:
      return MarkAsSimd128(node), VisitF16x8Abs(node);
    case IrOpcode::kF16x8Neg:
      return MarkAsSimd128(node), VisitF16x8Neg(node);
    case IrOpcode::kF16x8Sqrt:
      return MarkAsSimd128(node), VisitF16x8Sqrt(node);
    case IrOpcode::kF16x8Ceil:
      return MarkAsSimd128(node), VisitF16x8Ceil(node);
    case IrOpcode::kF16x8Floor:
      return MarkAsSimd128(node), VisitF16x8Floor(node);
    case IrOpcode::kF16x8Trunc:
      return MarkAsSimd128(node), VisitF16x8Trunc(node);
    case IrOpcode::kF16x8NearestInt:
      return MarkAsSimd128(node), VisitF16x8NearestInt(node);
    case IrOpcode::kF16x8Add:
      return MarkAsSimd128(node), VisitF16x8Add(node);
    case IrOpcode::kF16x8Sub:
      return MarkAsSimd128(node), VisitF16x8Sub(node);
    case IrOpcode::kF16x8Mul:
      return MarkAsSimd128(node), VisitF16x8Mul(node);
    case IrOpcode::kF16x8Div:
      return MarkAsSimd128(node), VisitF16x8Div(node);
    case IrOpcode::kF16x8Min:
      return MarkAsSimd128(node), VisitF16x8Min(node);
    case IrOpcode::kF16x8Max:
      return MarkAsSimd128(node), VisitF16x8Max(node);
    case IrOpcode::kF16x8Pmin:
      return MarkAsSimd128(node), VisitF16x8Pmin(node);
    case IrOpcode::kF16x8Pmax:
      return MarkAsSimd128(node), VisitF16x8Pmax(node);
    case IrOpcode::kF16x8Eq:
      return MarkAsSimd128(node), VisitF16x8Eq(node);
    case IrOpcode::kF16x8Ne:
      return MarkAsSimd128(node), VisitF16x8Ne(node);
    case IrOpcode::kF16x8Lt:
      return MarkAsSimd128(node), VisitF16x8Lt(node);
    case IrOpcode::kF16x8Le:
      return MarkAsSimd128(node), VisitF16x8Le(node);
    case IrOpcode::kF16x8SConvertI16x8:
      return MarkAsSimd128(node), VisitF16x8SConvertI16x8(node);
    case IrOpcode::kF16x8UConvertI16x8:
      return MarkAsSimd128(node), VisitF16x8UConvertI16x8(node);
    case IrOpcode::kI16x8UConvertF16x8:
      return MarkAsSimd128(node), VisitI16x8UConvertF16x8(node);
    case IrOpcode::kI16x8SConvertF16x8:
      return MarkAsSimd128(node), VisitI16x8SConvertF16x8(node);
    case IrOpcode::kF16x8DemoteF32x4Zero:
      return MarkAsSimd128(node), VisitF16x8DemoteF32x4Zero(node);
    case IrOpcode::kF16x8DemoteF64x2Zero:
      return MarkAsSimd128(node), VisitF16x8DemoteF64x2Zero(node);
    case IrOpcode::kF32x4PromoteLowF16x8:
      return MarkAsSimd128(node), VisitF32x4PromoteLowF16x8(node);
    case IrOpcode::kF16x8Qfma:
      return MarkAsSimd128(node), VisitF16x8Qfma(node);
    case IrOpcode::kF16x8Qfms:
      return MarkAsSimd128(node), VisitF16x8Qfms(node);

      // SIMD256
#if defined(V8_TARGET_ARCH_X64) && defined(V8_ENABLE_WASM_SIMD256_REVEC)
    case IrOpcode::kF64x4Min:
      return MarkAsSimd256(node), VisitF64x4Min(node);
    case IrOpcode::kF64x4Max:
      return MarkAsSimd256(node), VisitF64x4Max(node);
    case IrOpcode::kF64x4Add:
      return MarkAsSimd256(node), VisitF64x4Add(node);
    case IrOpcode::kF32x8Add:
      return MarkAsSimd256(node), VisitF32x8Add(node);
    case IrOpcode::kI64x4Add:
      return MarkAsSimd256(node), VisitI64x4Add(node);
    case IrOpcode::kI32x8Add:
      return MarkAsSimd256(node), VisitI32x8Add(node);
    case IrOpcode::kI16x16Add:
      return MarkAsSimd256(node), VisitI16x16Add(node);
    case IrOpcode::kI8x32Add:
      return MarkAsSimd256(node), VisitI8x32Add(node);
    case IrOpcode::kF64x4Sub:
      return MarkAsSimd256(node), VisitF64x4Sub(node);
    case IrOpcode::kF32x8Sub:
      return MarkAsSimd256(node), VisitF32x8Sub(node);
    case IrOpcode::kF32x8Min:
      return MarkAsSimd256(node), VisitF32x8Min(node);
    case IrOpcode::kF32x8Max:
      return MarkAsSimd256(node), VisitF32x8Max(node);
    case IrOpcode::kI64x4Ne:
      return MarkAsSimd256(node), VisitI64x4Ne(node);
    case IrOpcode::kI64x4GeS:
      return MarkAsSimd256(node), VisitI64x4GeS(node);
    case IrOpcode::kI32x8Ne:
      return MarkAsSimd256(node), VisitI32x8Ne(node);
    case IrOpcode::kI32x8GtU:
      return MarkAsSimd256(node), VisitI32x8GtU(node);
    case IrOpcode::kI32x8GeS:
      return MarkAsSimd256(node), VisitI32x8GeS(node);
    case IrOpcode::kI32x8GeU:
      return MarkAsSimd256(node), VisitI32x8GeU(node);
    case IrOpcode::kI16x16Ne:
      return MarkAsSimd256(node), VisitI16x16Ne(node);
    case IrOpcode::kI16x16GtU:
      return MarkAsSimd256(node), VisitI16x16GtU(node);
    case IrOpcode::kI16x16GeS:
      return MarkAsSimd256(node), VisitI16x16GeS(node);
    case IrOpcode::kI16x16GeU:
      return MarkAsSimd256(node), VisitI16x16GeU(node);
    case IrOpcode::kI8x32Ne:
      return MarkAsSimd256(node), VisitI8x32Ne(node);
    case IrOpcode::kI8x32GtU:
      return MarkAsSimd256(node), VisitI8x32GtU(node);
    case IrOpcode::kI8x32GeS:
      return MarkAsSimd256(node), VisitI8x32GeS(node);
    case IrOpcode::kI8x32GeU:
      return MarkAsSimd256(node), VisitI8x32GeU(node);
    case IrOpcode::kI64x4Sub:
      return MarkAsSimd256(node), VisitI64x4Sub(node);
    case IrOpcode::kI32x8Sub:
      return MarkAsSimd256(node), VisitI32x8Sub(node);
    case IrOpcode::kI16x16Sub:
      return MarkAsSimd256(node), VisitI16x16Sub(node);
    case IrOpcode::kI8x32Sub:
      return MarkAsSimd256(node), VisitI8x32Sub(node);
    case IrOpcode::kF64x4Mul:
      return MarkAsSimd256(node), VisitF64x4Mul(node);
    case IrOpcode::kF32x8Mul:
      return MarkAsSimd256(node), VisitF32x8Mul(node);
    case IrOpcode::kI64x4Mul:
      return MarkAsSimd256(node), VisitI64x4Mul(node);
    case IrOpcode::kI32x8Mul:
      return MarkAsSimd256(node), VisitI32x8Mul(node);
    case IrOpcode::kI16x16Mul:
      return MarkAsSimd256(node), VisitI16x16Mul(node);
    case IrOpcode::kF32x8Div:
      return MarkAsSimd256(node), VisitF32x8Div(node);
    case IrOpcode::kF64x4Div:
      return MarkAsSimd256(node), VisitF64x4Div(node);
    case IrOpcode::kI16x16AddSatS:
      return MarkAsSimd256(node), VisitI16x16AddSatS(node);
    case IrOpcode::kI8x32AddSatS:
      return MarkAsSimd256(node), VisitI8x32AddSatS(node);
    case IrOpcode::kI16x16AddSatU:
      return MarkAsSimd256(node), VisitI16x16AddSatU(node);
    case IrOpcode::kI8x32AddSatU:
      return MarkAsSimd256(node), VisitI8x32AddSatU(node);
    case IrOpcode::kI16x16SubSatS:
      return MarkAsSimd256(node), VisitI16x16SubSatS(node);
    case IrOpcode::kI8x32SubSatS:
      return MarkAsSimd256(node), VisitI8x32SubSatS(node);
    case IrOpcode::kI16x16SubSatU:
      return MarkAsSimd256(node), VisitI16x16SubSatU(node);
    case IrOpcode::kI8x32SubSatU:
      return MarkAsSimd256(node), VisitI8x32SubSatU(node);
    case IrOpcode::kI32x8SConvertF32x8:
      return MarkAsSimd256(node), VisitI32x8SConvertF32x8(node);
    case IrOpcode::kI32x8UConvertF32x8:
      return MarkAsSimd256(node), VisitI32x8UConvertF32x8(node);
    case IrOpcode::kF64x4ConvertI32x4S:
      return MarkAsSimd256(node), VisitF64x4ConvertI32x4S(node);
    case IrOpcode::kF32x8SConvertI32x8:
      return MarkAsSimd256(node), VisitF32x8SConvertI32x8(node);
    case IrOpcode::kF32x8UConvertI32x8:
      return MarkAsSimd256(node), VisitF32x8UConvertI32x8(node);
    case IrOpcode::kF32x4DemoteF64x4:
      return MarkAsSimd256(node), VisitF32x4DemoteF64x4(node);
    case IrOpcode::kI64x4SConvertI32x4:
      return MarkAsSimd256(node), VisitI64x4SConvertI32x4(node);
    case IrOpcode::kI64x4UConvertI32x4:
      return MarkAsSimd256(node), VisitI64x4UConvertI32x4(node);
    case IrOpcode::kI32x8SConvertI16x8:
      return MarkAsSimd256(node), VisitI32x8SConvertI16x8(node);
    case IrOpcode::kI32x8UConvertI16x8:
      return MarkAsSimd256(node), VisitI32x8UConvertI16x8(node);
    case IrOpcode::kI16x16SConvertI8x16:
      return MarkAsSimd256(node), VisitI16x16SConvertI8x16(node);
    case IrOpcode::kI16x16UConvertI8x16:
      return MarkAsSimd256(node), VisitI16x16UConvertI8x16(node);
    case IrOpcode::kI16x16SConvertI32x8:
      return MarkAsSimd256(node), VisitI16x16SConvertI32x8(node);
    case IrOpcode::kI16x16UConvertI32x8:
      return MarkAsSimd256(node), VisitI16x16UConvertI32x8(node);
    case IrOpcode::kI8x32SConvertI16x16:
      return MarkAsSimd256(node), VisitI8x32SConvertI16x16(node);
    case IrOpcode::kI8x32UConvertI16x16:
      return MarkAsSimd256(node), VisitI8x32UConvertI16x16(node);
    case IrOpcode::kF32x8Abs:
      return MarkAsSimd256(node), VisitF32x8Abs(node);
    case IrOpcode::kF32x8Neg:
      return MarkAsSimd256(node), VisitF32x8Neg(node);
    case IrOpcode::kF32x8Sqrt:
      return MarkAsSimd256(node), VisitF32x8Sqrt(node);
    case IrOpcode::kF64x4Sqrt:
      return MarkAsSimd256(node), VisitF64x4Sqrt(node);
    case IrOpcode::kI32x8Abs:
      return MarkAsSimd256(node), VisitI32x8Abs(node);
    case IrOpcode::kI32x8Neg:
      return MarkAsSimd256(node), VisitI32x8Neg(node);
    case IrOpcode::kI16x16Abs:
      return MarkAsSimd256(node), VisitI16x16Abs(node);
    case IrOpcode::kI16x16Neg:
      return MarkAsSimd256(node), VisitI16x16Neg(node);
    case IrOpcode::kI8x32Abs:
      return MarkAsSimd256(node), VisitI8x32Abs(node);
    case IrOpcode::kI8x32Neg:
      return MarkAsSimd256(node), VisitI8x32Neg(node);
    case IrOpcode::kI64x4Shl:
      return MarkAsSimd256(node), VisitI64x4Shl(node);
    case IrOpcode::kI64x4ShrU:
      return MarkAsSimd256(node), VisitI64x4ShrU(node);
    case IrOpcode::kI32x8Shl:
      return MarkAsSimd256(node), VisitI32x8Shl(node);
    case IrOpcode::kI32x8ShrS:
      return MarkAsSimd256(node), VisitI32x8ShrS(node);
    case IrOpcode::kI32x8ShrU:
      return MarkAsSimd256(node), VisitI32x8ShrU(node);
    case IrOpcode::kI16x16Shl:
      return MarkAsSimd256(node), VisitI16x16Shl(node);
    case IrOpcode::kI16x16ShrS:
      return MarkAsSimd256(node), VisitI16x16ShrS(node);
    case IrOpcode::kI16x16ShrU:
      return MarkAsSimd256(node), VisitI16x16ShrU(node);
    case IrOpcode::kI32x8DotI16x16S:
      return MarkAsSimd256(node), VisitI32x8DotI16x16S(node);
    case IrOpcode::kI16x16RoundingAverageU:
      return MarkAsSimd256(node), VisitI16x16RoundingAverageU(node);
    case IrOpcode::kI8x32RoundingAverageU:
      return MarkAsSimd256(node), VisitI8x32RoundingAverageU(node);
    case IrOpcode::kS256Const:
      return MarkAsSimd256(node), VisitS256Const(node);
    case IrOpcode::kS256Zero:
      return MarkAsSimd256(node), VisitS256Zero(node);
    case IrOpcode::kS256And:
      return MarkAsSimd256(node), VisitS256And(node);
    case IrOpcode::kS256Or:
      return MarkAsSimd256(node), VisitS256Or(node);
    case IrOpcode::kS256Xor:
      return MarkAsSimd256(node), VisitS256Xor(node);
    case IrOpcode::kS256Not:
      return MarkAsSimd256(node), VisitS256Not(node);
    case IrOpcode::kS256Select:
      return MarkAsSimd256(node), VisitS256Select(node);
    case IrOpcode::kS256AndNot:
      return MarkAsSimd256(node), VisitS256AndNot(node);
    case IrOpcode::kF32x8Eq:
      return MarkAsSimd256(node), VisitF32x8Eq(node);
    case IrOpcode::kF64x4Eq:
      return MarkAsSimd256(node), VisitF64x4Eq(node);
    case IrOpcode::kI64x4Eq:
      return MarkAsSimd256(node), VisitI64x4Eq(node);
    case IrOpcode::kI32x8Eq:
      return MarkAsSimd256(node), VisitI32x8Eq(node);
    case IrOpcode::kI16x16Eq:
      return MarkAsSimd256(node), VisitI16x16Eq(node);
    case IrOpcode::kI8x32Eq:
      return MarkAsSimd256(node), VisitI8x32Eq(node);
    case IrOpcode::kF32x8Ne:
      return MarkAsSimd256(node), VisitF32x8Ne(node);
    case IrOpcode::kF64x4Ne:
      return MarkAsSimd256(node), VisitF64x4Ne(node);
    case IrOpcode::kI64x4GtS:
      return MarkAsSimd256(node), VisitI64x4GtS(node);
    case IrOpcode::kI32x8GtS:
      return MarkAsSimd256(node), VisitI32x8GtS(node);
    case IrOpcode::kI16x16GtS:
      return MarkAsSimd256(node), VisitI16x16GtS(node);
    case IrOpcode::kI8x32GtS:
      return MarkAsSimd256(node), VisitI8x32GtS(node);
    case IrOpcode::kF64x4Lt:
      return MarkAsSimd256(node), VisitF64x4Lt(node);
    case IrOpcode::kF32x8Lt:
      return MarkAsSimd256(node), VisitF32x8Lt(node);
    case IrOpcode::kF64x4Le:
      return MarkAsSimd256(node), VisitF64x4Le(node);
    case IrOpcode::kF32x8Le:
      return MarkAsSimd256(node), VisitF32x8Le(node);
    case IrOpcode::kI32x8MinS:
      return MarkAsSimd256(node), VisitI32x8MinS(node);
    case IrOpcode::kI16x16MinS:
      return MarkAsSimd256(node), VisitI16x16MinS(node);
    case IrOpcode::kI8x32MinS:
      return MarkAsSimd256(node), VisitI8x32MinS(node);
    case IrOpcode::kI32x8MinU:
      return MarkAsSimd256(node), VisitI32x8MinU(node);
    case IrOpcode::kI16x16MinU:
      return MarkAsSimd256(node), VisitI16x16MinU(node);
    case IrOpcode::kI8x32MinU:
      return MarkAsSimd256(node), VisitI8x32MinU(node);
    case IrOpcode::kI32x8MaxS:
      return MarkAsSimd256(node), VisitI32x8MaxS(node);
    case IrOpcode::kI16x16MaxS:
      return MarkAsSimd256(node), VisitI16x16MaxS(node);
    case IrOpcode::kI8x32MaxS:
      return MarkAsSimd256(node), VisitI8x32MaxS(node);
    case IrOpcode::kI32x8MaxU:
      return MarkAsSimd256(node), VisitI32x8MaxU(node);
    case IrOpcode::kI16x16MaxU:
      return MarkAsSimd256(node), VisitI16x16MaxU(node);
    case IrOpcode::kI8x32MaxU:
      return MarkAsSimd256(node), VisitI8x32MaxU(node);
    case IrOpcode::kI64x4Splat:
      return MarkAsSimd256(node), VisitI64x4Splat(node);
    case IrOpcode::kI32x8Splat:
      return MarkAsSimd256(node), VisitI32x8Splat(node);
    case IrOpcode::kI16x16Splat:
      return MarkAsSimd256(node), VisitI16x16Splat(node);
    case IrOpcode::kI8x32Splat:
      return MarkAsSimd256(node), VisitI8x32Splat(node);
    case IrOpcode::kF32x8Splat:
      return MarkAsSimd256(node), VisitF32x8Splat(node);
    case IrOpcode::kF64x4Splat:
      return MarkAsSimd256(node), VisitF64x4Splat(node);
    case IrOpcode::kI64x4ExtMulI32x4S:
      return MarkAsSimd256(node), VisitI64x4ExtMulI32x4S(node);
    case IrOpcode::kI64x4ExtMulI32x4U:
      return MarkAsSimd256(node), VisitI64x4ExtMulI32x4U(node);
    case IrOpcode::kI32x8ExtMulI16x8S:
      return MarkAsSimd256(node), VisitI32x8ExtMulI16x8S(node);
    case IrOpcode::kI32x8ExtMulI16x8U:
      return MarkAsSimd256(node), VisitI32x8ExtMulI16x8U(node);
    case IrOpcode::kI16x16ExtMulI8x16S:
      return MarkAsSimd256(node), VisitI16x16ExtMulI8x16S(node);
    case IrOpcode::kI16x16ExtMulI8x16U:
      return MarkAsSimd256(node), VisitI16x16ExtMulI8x16U(node);
    case IrOpcode::kI32x8ExtAddPairwiseI16x16S:
      return MarkAsSimd256(node), VisitI32x8ExtAddPairwiseI16x16S(node);
    case IrOpcode::kI32x8ExtAddPairwiseI16x16U:
      return MarkAsSimd256(node), VisitI32x8ExtAddPairwiseI16x16U(node);
    case IrOpcode::kI16x16ExtAddPairwiseI8x32S:
      return MarkAsSimd256(node), VisitI16x16ExtAddPairwiseI8x32S(node);
    case IrOpcode::kI16x16ExtAddPairwiseI8x32U:
      return MarkAsSimd256(node), VisitI16x16ExtAddPairwiseI8x32U(node);
    case IrOpcode::kF32x8Pmin:
      return MarkAsSimd256(node), VisitF32x8Pmin(node);
    case IrOpcode::kF32x8Pmax:
      return MarkAsSimd256(node), VisitF32x8Pmax(node);
    case IrOpcode::kF64x4Pmin:
      return MarkAsSimd256(node), VisitF64x4Pmin(node);
    case IrOpcode::kF64x4Pmax:
      return MarkAsSimd256(node), VisitF64x4Pmax(node);
    case IrOpcode::kI8x32Shuffle:
      return MarkAsSimd256(node), VisitI8x32Shuffle(node);
    case IrOpcode::kExtractF128:
      return MarkAsSimd128(node), VisitExtractF128(node);
    case IrOpcode::kF32x8Qfma:
      return MarkAsSimd256(node), VisitF32x8Qfma(node);
    case IrOpcode::kF32x8Qfms:
      return MarkAsSimd256(node), VisitF32x8Qfms(node);
    case IrOpcode::kF64x4Qfma:
      return MarkAsSimd256(node), VisitF64x4Qfma(node);
    case IrOpcode::kF64x4Qfms:
      return MarkAsSimd256(node), VisitF64x4Qfms(node);
    case IrOpcode::kI64x4RelaxedLaneSelect:
      return MarkAsSimd256(node), VisitI64x4RelaxedLaneSelect(node);
    case IrOpcode::kI32x8RelaxedLaneSelect:
      return MarkAsSimd256(node), VisitI32x8RelaxedLaneSelect(node);
    case IrOpcode::kI16x16RelaxedLaneSelect:
      return MarkAsSimd256(node), VisitI16x16RelaxedLaneSelect(node);
    case IrOpcode::kI8x32RelaxedLaneSelect:
      return MarkAsSimd256(node), VisitI8x32RelaxedLaneSelect(node);
    case IrOpcode::kI32x8DotI8x32I7x32AddS:
      return MarkAsSimd256(node), VisitI32x8DotI8x32I7x32AddS(node);
    case IrOpcode::kI16x16DotI8x32I7x32S:
      return MarkAsSimd256(node), VisitI16x16DotI8x32I7x32S(node);
    case IrOpcode::kF32x8RelaxedMin:
      return MarkAsSimd256(node), VisitF32x8RelaxedMin(node);
    case IrOpcode::kF32x8RelaxedMax:
      return MarkAsSimd256(node), VisitF32x8RelaxedMax(node);
    case IrOpcode::kF64x4RelaxedMin:
      return MarkAsSimd256(node), VisitF64x4RelaxedMin(node);
    case IrOpcode::kF64x4RelaxedMax:
      return MarkAsSimd256(node), VisitF64x4RelaxedMax(node);
#endif  // V8_TARGET_ARCH_X64 && V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY
    default:
      FATAL("Unexpected operator #%d:%s @ node #%d", node->opcode(),
            node->op()->mnemonic(), node->id());
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitNode(
    turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  tick_counter_->TickAndMaybeEnterSafepoint();
  const turboshaft::Operation& op = this->Get(node);
  using Opcode = turboshaft::Opcode;
  using Rep = turboshaft::RegisterRepresentation;
  switch (op.opcode) {
    case Opcode::kBranch:
    case Opcode::kGoto:
    case Opcode::kReturn:
    case Opcode::kTailCall:
    case Opcode::kUnreachable:
    case Opcode::kDeoptimize:
    case Opcode::kSwitch:
    case Opcode::kCheckException:
      // Those are already handled in VisitControl.
      DCHECK(op.IsBlockTerminator());
      break;
    case Opcode::kParameter: {
      // Parameters should always be scheduled to the first block.
      DCHECK_EQ(this->rpo_number(this->block(schedule(), node)).ToInt(), 0);
      MachineType type = linkage()->GetParameterType(
          op.Cast<turboshaft::ParameterOp>().parameter_index);
      MarkAsRepresentation(type.representation(), node);
      return VisitParameter(node);
    }
    case Opcode::kChange: {
      const turboshaft::ChangeOp& change = op.Cast<turboshaft::ChangeOp>();
      MarkAsRepresentation(change.to.machine_representation(), node);
      switch (change.kind) {
        case ChangeOp::Kind::kFloatConversion:
          if (change.from == Rep::Float64()) {
            DCHECK_EQ(change.to, Rep::Float32());
            return VisitTruncateFloat64ToFloat32(node);
          } else {
            DCHECK_EQ(change.from, Rep::Float32());
            DCHECK_EQ(change.to, Rep::Float64());
            return VisitChangeFloat32ToFloat64(node);
          }
        case ChangeOp::Kind::kSignedFloatTruncateOverflowToMin:
        case ChangeOp::Kind::kUnsignedFloatTruncateOverflowToMin: {
          using A = ChangeOp::Assumption;
          bool is_signed =
              change.kind == ChangeOp::Kind::kSignedFloatTruncateOverflowToMin;
          switch (multi(change.from, change.to, is_signed, change.assumption)) {
            case multi(Rep::Float32(), Rep::Word32(), true, A::kNoOverflow):
            case multi(Rep::Float32(), Rep::Word32(), true, A::kNoAssumption):
              return VisitTruncateFloat32ToInt32(node);
            case multi(Rep::Float32(), Rep::Word32(), false, A::kNoOverflow):
            case multi(Rep::Float32(), Rep::Word32(), false, A::kNoAssumption):
              return VisitTruncateFloat32ToUint32(node);
            case multi(Rep::Float64(), Rep::Word32(), true, A::kReversible):
              return VisitChangeFloat64ToInt32(node);
            case multi(Rep::Float64(), Rep::Word32(), false, A::kReversible):
              return VisitChangeFloat64ToUint32(node);
            case multi(Rep::Float64(), Rep::Word32(), true, A::kNoOverflow):
              return VisitRoundFloat64ToInt32(node);
            case multi(Rep::Float64(), Rep::Word32(), false, A::kNoAssumption):
            case multi(Rep::Float64(), Rep::Word32(), false, A::kNoOverflow):
              return VisitTruncateFloat64ToUint32(node);
            case multi(Rep::Float64(), Rep::Word64(), true, A::kReversible):
              return VisitChangeFloat64ToInt64(node);
            case multi(Rep::Float64(), Rep::Word64(), false, A::kReversible):
              return VisitChangeFloat64ToUint64(node);
            case multi(Rep::Float64(), Rep::Word64(), true, A::kNoOverflow):
            case multi(Rep::Float64(), Rep::Word64(), true, A::kNoAssumption):
              return VisitTruncateFloat64ToInt64(node);
            default:
              // Invalid combination.
              UNREACHABLE();
          }

          UNREACHABLE();
        }
        case ChangeOp::Kind::kJSFloatTruncate:
          DCHECK_EQ(change.from, Rep::Float64());
          DCHECK_EQ(change.to, Rep::Word32());
          return VisitTruncateFloat64ToWord32(node);
        case ChangeOp::Kind::kSignedToFloat:
          if (change.from == Rep::Word32()) {
            if (change.to == Rep::Float32()) {
              return VisitRoundInt32ToFloat32(node);
            } else {
              DCHECK_EQ(change.to, Rep::Float64());
              DCHECK_EQ(change.assumption, ChangeOp::Assumption::kNoAssumption);
              return VisitChangeInt32ToFloat64(node);
            }
          } else {
            DCHECK_EQ(change.from, Rep::Word64());
            if (change.to == Rep::Float32()) {
              return VisitRoundInt64ToFloat32(node);
            } else {
              DCHECK_EQ(change.to, Rep::Float64());
              if (change.assumption == ChangeOp::Assumption::kReversible) {
                return VisitChangeInt64ToFloat64(node);
              } else {
                return VisitRoundInt64ToFloat64(node);
              }
            }
          }
          UNREACHABLE();
        case ChangeOp::Kind::kUnsignedToFloat:
          switch (multi(change.from, change.to)) {
            case multi(Rep::Word32(), Rep::Float32()):
              return VisitRoundUint32ToFloat32(node);
            case multi(Rep::Word32(), Rep::Float64()):
              return VisitChangeUint32ToFloat64(node);
            case multi(Rep::Word64(), Rep::Float32()):
              return VisitRoundUint64ToFloat32(node);
            case multi(Rep::Word64(), Rep::Float64()):
              return VisitRoundUint64ToFloat64(node);
            default:
              UNREACHABLE();
          }
        case ChangeOp::Kind::kExtractHighHalf:
          DCHECK_EQ(change.from, Rep::Float64());
          DCHECK_EQ(change.to, Rep::Word32());
          return VisitFloat64ExtractHighWord32(node);
        case ChangeOp::Kind::kExtractLowHalf:
          DCHECK_EQ(change.from, Rep::Float64());
          DCHECK_EQ(change.to, Rep::Word32());
          return VisitFloat64ExtractLowWord32(node);
        case ChangeOp::Kind::kZeroExtend:
          DCHECK_EQ(change.from, Rep::Word32());
          DCHECK_EQ(change.to, Rep::Word64());
          return VisitChangeUint32ToUint64(node);
        case ChangeOp::Kind::kSignExtend:
          DCHECK_EQ(change.from, Rep::Word32());
          DCHECK_EQ(change.to, Rep::Word64());
          return VisitChangeInt32ToInt64(node);
        case ChangeOp::Kind::kTruncate:
          DCHECK_EQ(change.from, Rep::Word64());
          DCHECK_EQ(change.to, Rep::Word32());
          MarkAsWord32(node);
          return VisitTruncateInt64ToInt32(node);
        case ChangeOp::Kind::kBitcast:
          switch (multi(change.from, change.to)) {
            case multi(Rep::Word32(), Rep::Word64()):
              return VisitBitcastWord32ToWord64(node);
            case multi(Rep::Word32(), Rep::Float32()):
              return VisitBitcastInt32ToFloat32(node);
            case multi(Rep::Word64(), Rep::Float64()):
              return VisitBitcastInt64ToFloat64(node);
            case multi(Rep::Float32(), Rep::Word32()):
              return VisitBitcastFloat32ToInt32(node);
            case multi(Rep::Float64(), Rep::Word64()):
              return VisitBitcastFloat64ToInt64(node);
            default:
              UNREACHABLE();
          }
      }
      UNREACHABLE();
    }
    case Opcode::kTryChange: {
      const TryChangeOp& try_change = op.Cast<TryChangeOp>();
      MarkAsRepresentation(try_change.to.machine_representation(), node);
      DCHECK(try_change.kind ==
                 TryChangeOp::Kind::kSignedFloatTruncateOverflowUndefined ||
             try_change.kind ==
                 TryChangeOp::Kind::kUnsignedFloatTruncateOverflowUndefined);
      const bool is_signed =
          try_change.kind ==
          TryChangeOp::Kind::kSignedFloatTruncateOverflowUndefined;
      switch (multi(try_change.from, try_change.to, is_signed)) {
        case multi(Rep::Float64(), Rep::Word64(), true):
          return VisitTryTruncateFloat64ToInt64(node);
        case multi(Rep::Float64(), Rep::Word64(), false):
          return VisitTryTruncateFloat64ToUint64(node);
        case multi(Rep::Float64(), Rep::Word32(), true):
          return VisitTryTruncateFloat64ToInt32(node);
        case multi(Rep::Float64(), Rep::Word32(), false):
          return VisitTryTruncateFloat64ToUint32(node);
        case multi(Rep::Float32(), Rep::Word64(), true):
          return VisitTryTruncateFloat32ToInt64(node);
        case multi(Rep::Float32(), Rep::Word64(), false):
          return VisitTryTruncateFloat32ToUint64(node);
        default:
          UNREACHABLE();
      }
      UNREACHABLE();
    }
    case Opcode::kConstant: {
      const ConstantOp& constant = op.Cast<ConstantOp>();
      switch (constant.kind) {
        case ConstantOp::Kind::kWord32:
        case ConstantOp::Kind::kWord64:
        case ConstantOp::Kind::kSmi:
        case ConstantOp::Kind::kTaggedIndex:
        case ConstantOp::Kind::kExternal:
          break;
        case ConstantOp::Kind::kFloat32:
          MarkAsFloat32(node);
          break;
        case ConstantOp::Kind::kFloat64:
          MarkAsFloat64(node);
          break;
        case ConstantOp::Kind::kHeapObject:
        case ConstantOp::Kind::kTrustedHeapObject:
          MarkAsTagged(node);
          break;
        case ConstantOp::Kind::kCompressedHeapObject:
          MarkAsCompressed(node);
          break;
        case ConstantOp::Kind::kNumber:
          if (!IsSmiDouble(constant.number().get_scalar())) MarkAsTagged(node);
          break;
        case ConstantOp::Kind::kRelocatableWasmCall:
        case ConstantOp::Kind::kRelocatableWasmStubCall:
        case ConstantOp::Kind::kRelocatableWasmCanonicalSignatureId:
          break;
      }
      VisitConstant(node);
      break;
    }
    case Opcode::kWordUnary: {
      const WordUnaryOp& unop = op.Cast<WordUnaryOp>();
      if (unop.rep == WordRepresentation::Word32()) {
        MarkAsWord32(node);
        switch (unop.kind) {
          case WordUnaryOp::Kind::kReverseBytes:
            return VisitWord32ReverseBytes(node);
          case WordUnaryOp::Kind::kCountLeadingZeros:
            return VisitWord32Clz(node);
          case WordUnaryOp::Kind::kCountTrailingZeros:
            return VisitWord32Ctz(node);
          case WordUnaryOp::Kind::kPopCount:
            return VisitWord32Popcnt(node);
          case WordUnaryOp::Kind::kSignExtend8:
            return VisitSignExtendWord8ToInt32(node);
          case WordUnaryOp::Kind::kSignExtend16:
            return VisitSignExtendWord16ToInt32(node);
        }
      } else {
        DCHECK_EQ(unop.rep, WordRepresentation::Word64());
        MarkAsWord64(node);
        switch (unop.kind) {
          case WordUnaryOp::Kind::kReverseBytes:
            return VisitWord64ReverseBytes(node);
          case WordUnaryOp::Kind::kCountLeadingZeros:
            return VisitWord64Clz(node);
          case WordUnaryOp::Kind::kCountTrailingZeros:
            return VisitWord64Ctz(node);
          case WordUnaryOp::Kind::kPopCount:
            return VisitWord64Popcnt(node);
          case WordUnaryOp::Kind::kSignExtend8:
            return VisitSignExtendWord8ToInt64(node);
          case WordUnaryOp::Kind::kSignExtend16:
            return VisitSignExtendWord16ToInt64(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kWordBinop: {
      const WordBinopOp& binop = op.Cast<WordBinopOp>();
      if (binop.rep == WordRepresentation::Word32()) {
        MarkAsWord32(node);
        switch (binop.kind) {
          case WordBinopOp::Kind::kAdd:
            return VisitInt32Add(node);
          case WordBinopOp::Kind::kMul:
            return VisitInt32Mul(node);
          case WordBinopOp::Kind::kSignedMulOverflownBits:
            return VisitInt32MulHigh(node);
          case WordBinopOp::Kind::kUnsignedMulOverflownBits:
            return VisitUint32MulHigh(node);
          case WordBinopOp::Kind::kBitwiseAnd:
            return VisitWord32And(node);
          case WordBinopOp::Kind::kBitwiseOr:
            return VisitWord32Or(node);
          case WordBinopOp::Kind::kBitwiseXor:
            return VisitWord32Xor(node);
          case WordBinopOp::Kind::kSub:
            return VisitInt32Sub(node);
          case WordBinopOp::Kind::kSignedDiv:
            return VisitInt32Div(node);
          case WordBinopOp::Kind::kUnsignedDiv:
            return VisitUint32Div(node);
          case WordBinopOp::Kind::kSignedMod:
            return VisitInt32Mod(node);
          case WordBinopOp::Kind::kUnsignedMod:
            return VisitUint32Mod(node);
        }
      } else {
        DCHECK_EQ(binop.rep, WordRepresentation::Word64());
        MarkAsWord64(node);
        switch (binop.kind) {
          case WordBinopOp::Kind::kAdd:
            return VisitInt64Add(node);
          case WordBinopOp::Kind::kMul:
            return VisitInt64Mul(node);
          case WordBinopOp::Kind::kSignedMulOverflownBits:
            return VisitInt64MulHigh(node);
          case WordBinopOp::Kind::kUnsignedMulOverflownBits:
            return VisitUint64MulHigh(node);
          case WordBinopOp::Kind::kBitwiseAnd:
            return VisitWord64And(node);
          case WordBinopOp::Kind::kBitwiseOr:
            return VisitWord64Or(node);
          case WordBinopOp::Kind::kBitwiseXor:
            return VisitWord64Xor(node);
          case WordBinopOp::Kind::kSub:
            return VisitInt64Sub(node);
          case WordBinopOp::Kind::kSignedDiv:
            return VisitInt64Div(node);
          case WordBinopOp::Kind::kUnsignedDiv:
            return VisitUint64Div(node);
          case WordBinopOp::Kind::kSignedMod:
            return VisitInt64Mod(node);
          case WordBinopOp::Kind::kUnsignedMod:
            return VisitUint64Mod(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kFloatUnary: {
      const auto& unop = op.Cast<FloatUnaryOp>();
      if (unop.rep == Rep::Float32()) {
        MarkAsFloat32(node);
        switch (unop.kind) {
          case FloatUnaryOp::Kind::kAbs:
            return VisitFloat32Abs(node);
          case FloatUnaryOp::Kind::kNegate:
            return VisitFloat32Neg(node);
          case FloatUnaryOp::Kind::kRoundDown:
            return VisitFloat32RoundDown(node);
          case FloatUnaryOp::Kind::kRoundUp:
            return VisitFloat32RoundUp(node);
          case FloatUnaryOp::Kind::kRoundToZero:
            return VisitFloat32RoundTruncate(node);
          case FloatUnaryOp::Kind::kRoundTiesEven:
            return VisitFloat32RoundTiesEven(node);
          case FloatUnaryOp::Kind::kSqrt:
            return VisitFloat32Sqrt(node);
          // Those operations are only supported on 64 bit.
          case FloatUnaryOp::Kind::kSilenceNaN:
          case FloatUnaryOp::Kind::kLog:
          case FloatUnaryOp::Kind::kLog2:
          case FloatUnaryOp::Kind::kLog10:
          case FloatUnaryOp::Kind::kLog1p:
          case FloatUnaryOp::Kind::kCbrt:
          case FloatUnaryOp::Kind::kExp:
          case FloatUnaryOp::Kind::kExpm1:
          case FloatUnaryOp::Kind::kSin:
          case FloatUnaryOp::Kind::kCos:
          case FloatUnaryOp::Kind::kSinh:
          case FloatUnaryOp::Kind::kCosh:
          case FloatUnaryOp::Kind::kAcos:
          case FloatUnaryOp::Kind::kAsin:
          case FloatUnaryOp::Kind::kAsinh:
          case FloatUnaryOp::Kind::kAcosh:
          case FloatUnaryOp::Kind::kTan:
          case FloatUnaryOp::Kind::kTanh:
          case FloatUnaryOp::Kind::kAtan:
          case FloatUnaryOp::Kind::kAtanh:
            UNREACHABLE();
        }
      } else {
        DCHECK_EQ(unop.rep, Rep::Float64());
        MarkAsFloat64(node);
        switch (unop.kind) {
          case FloatUnaryOp::Kind::kAbs:
            return VisitFloat64Abs(node);
          case FloatUnaryOp::Kind::kNegate:
            return VisitFloat64Neg(node);
          case FloatUnaryOp::Kind::kSilenceNaN:
            return VisitFloat64SilenceNaN(node);
          case FloatUnaryOp::Kind::kRoundDown:
            return VisitFloat64RoundDown(node);
          case FloatUnaryOp::Kind::kRoundUp:
            return VisitFloat64RoundUp(node);
          case FloatUnaryOp::Kind::kRoundToZero:
            return VisitFloat64RoundTruncate(node);
          case FloatUnaryOp::Kind::kRoundTiesEven:
            return VisitFloat64RoundTiesEven(node);
          case FloatUnaryOp::Kind::kLog:
            return VisitFloat64Log(node);
          case FloatUnaryOp::Kind::kLog2:
            return VisitFloat64Log2(node);
          case FloatUnaryOp::Kind::kLog10:
            return VisitFloat64Log10(node);
          case FloatUnaryOp::Kind::kLog1p:
            return VisitFloat64Log1p(node);
          case FloatUnaryOp::Kind::kSqrt:
            return VisitFloat64Sqrt(node);
          case FloatUnaryOp::Kind::kCbrt:
            return VisitFloat64Cbrt(node);
          case FloatUnaryOp::Kind::kExp:
            return VisitFloat64Exp(node);
          case FloatUnaryOp::Kind::kExpm1:
            return VisitFloat64Expm1(node);
          case FloatUnaryOp::Kind::kSin:
            return VisitFloat64Sin(node);
          case FloatUnaryOp::Kind::kCos:
            return VisitFloat64Cos(node);
          case FloatUnaryOp::Kind::kSinh:
            return VisitFloat64Sinh(node);
          case FloatUnaryOp::Kind::kCosh:
            return VisitFloat64Cosh(node);
          case FloatUnaryOp::Kind::kAcos:
            return VisitFloat64Acos(node);
          case FloatUnaryOp::Kind::kAsin:
            return VisitFloat64Asin(node);
          case FloatUnaryOp::Kind::kAsinh:
            return VisitFloat64Asinh(node);
          case FloatUnaryOp::Kind::kAcosh:
            return VisitFloat64Acosh(node);
          case FloatUnaryOp::Kind::kTan:
            return VisitFloat64Tan(node);
          case FloatUnaryOp::Kind::kTanh:
            return VisitFloat64Tanh(node);
          case FloatUnaryOp::Kind::kAtan:
            return VisitFloat64Atan(node);
          case FloatUnaryOp::Kind::kAtanh:
            return VisitFloat64Atanh(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kFloatBinop: {
      const auto& binop = op.Cast<FloatBinopOp>();
      if (binop.rep == Rep::Float32()) {
        MarkAsFloat32(node);
        switch (binop.kind) {
          case FloatBinopOp::Kind::kAdd:
            return VisitFloat32Add(node);
          case FloatBinopOp::Kind::kSub:
            return VisitFloat32Sub(node);
          case FloatBinopOp::Kind::kMul:
            return VisitFloat32Mul(node);
          case FloatBinopOp::Kind::kDiv:
            return VisitFloat32Div(node);
          case FloatBinopOp::Kind::kMin:
            return VisitFloat32Min(node);
          case FloatBinopOp::Kind::kMax:
            return VisitFloat32Max(node);
          case FloatBinopOp::Kind::kMod:
          case FloatBinopOp::Kind::kPower:
          case FloatBinopOp::Kind::kAtan2:
            UNREACHABLE();
        }
      } else {
        DCHECK_EQ(binop.rep, Rep::Float64());
        MarkAsFloat64(node);
        switch (binop.kind) {
          case FloatBinopOp::Kind::kAdd:
            return VisitFloat64Add(node);
          case FloatBinopOp::Kind::kSub:
            return VisitFloat64Sub(node);
          case FloatBinopOp::Kind::kMul:
            return VisitFloat64Mul(node);
          case FloatBinopOp::Kind::kDiv:
            return VisitFloat64Div(node);
          case FloatBinopOp::Kind::kMod:
            return VisitFloat64Mod(node);
          case FloatBinopOp::Kind::kMin:
            return VisitFloat64Min(node);
          case FloatBinopOp::Kind::kMax:
            return VisitFloat64Max(node);
          case FloatBinopOp::Kind::kPower:
            return VisitFloat64Pow(node);
          case FloatBinopOp::Kind::kAtan2:
            return VisitFloat64Atan2(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kOverflowCheckedBinop: {
      const auto& binop = op.Cast<OverflowCheckedBinopOp>();
      if (binop.rep == WordRepresentation::Word32()) {
        MarkAsWord32(node);
        switch (binop.kind) {
          case OverflowCheckedBinopOp::Kind::kSignedAdd:
            return VisitInt32AddWithOverflow(node);
          case OverflowCheckedBinopOp::Kind::kSignedMul:
            return VisitInt32MulWithOverflow(node);
          case OverflowCheckedBinopOp::Kind::kSignedSub:
            return VisitInt32SubWithOverflow(node);
        }
      } else {
        DCHECK_EQ(binop.rep, WordRepresentation::Word64());
        MarkAsWord64(node);
        switch (binop.kind) {
          case OverflowCheckedBinopOp::Kind::kSignedAdd:
            return VisitInt64AddWithOverflow(node);
          case OverflowCheckedBinopOp::Kind::kSignedMul:
            return VisitInt64MulWithOverflow(node);
          case OverflowCheckedBinopOp::Kind::kSignedSub:
            return VisitInt64SubWithOverflow(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kOverflowCheckedUnary: {
      const auto& unop = op.Cast<OverflowCheckedUnaryOp>();
      if (unop.rep == WordRepresentation::Word32()) {
        MarkAsWord32(node);
        switch (unop.kind) {
          case OverflowCheckedUnaryOp::Kind::kAbs:
            return VisitInt32AbsWithOverflow(node);
        }
      } else {
        DCHECK_EQ(unop.rep, WordRepresentation::Word64());
        MarkAsWord64(node);
        switch (unop.kind) {
          case OverflowCheckedUnaryOp::Kind::kAbs:
            return VisitInt64AbsWithOverflow(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kShift: {
      const auto& shift = op.Cast<ShiftOp>();
      if (shift.rep == RegisterRepresentation::Word32()) {
        MarkAsWord32(node);
        switch (shift.kind) {
          case ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros:
          case ShiftOp::Kind::kShiftRightArithmetic:
            return VisitWord32Sar(node);
          case ShiftOp::Kind::kShiftRightLogical:
            return VisitWord32Shr(node);
          case ShiftOp::Kind::kShiftLeft:
            return VisitWord32Shl(node);
          case ShiftOp::Kind::kRotateRight:
            return VisitWord32Ror(node);
          case ShiftOp::Kind::kRotateLeft:
            return VisitWord32Rol(node);
        }
      } else {
        DCHECK_EQ(shift.rep, RegisterRepresentation::Word64());
        MarkAsWord64(node);
        switch (shift.kind) {
          case ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros:
          case ShiftOp::Kind::kShiftRightArithmetic:
            return VisitWord64Sar(node);
          case ShiftOp::Kind::kShiftRightLogical:
            return VisitWord64Shr(node);
          case ShiftOp::Kind::kShiftLeft:
            return VisitWord64Shl(node);
          case ShiftOp::Kind::kRotateRight:
            return VisitWord64Ror(node);
          case ShiftOp::Kind::kRotateLeft:
            return VisitWord64Rol(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kCall:
      // Process the call at `DidntThrow`, when we know if exceptions are caught
      // or not.
      break;
    case Opcode::kDidntThrow:
      if (current_block_->begin() == node) {
        DCHECK_EQ(current_block_->PredecessorCount(), 1);
        DCHECK(current_block_->LastPredecessor()
                   ->LastOperation(*this->turboshaft_graph())
                   .Is<CheckExceptionOp>());
        // In this case, the Call has been generated at the `CheckException`
        // already.
      } else {
        VisitCall(op.Cast<DidntThrowOp>().throwing_operation());
      }
      EmitIdentity(node);
      break;
    case Opcode::kFrameConstant: {
      const auto& constant = op.Cast<turboshaft::FrameConstantOp>();
      using Kind = turboshaft::FrameConstantOp::Kind;
      OperandGenerator g(this);
      switch (constant.kind) {
        case Kind::kStackCheckOffset:
          Emit(kArchStackCheckOffset, g.DefineAsRegister(node));
          break;
        case Kind::kFramePointer:
          Emit(kArchFramePointer, g.DefineAsRegister(node));
          break;
        case Kind::kParentFramePointer:
          Emit(kArchParentFramePointer, g.DefineAsRegister(node));
          break;
      }
      break;
    }
    case Opcode::kStackPointerGreaterThan:
      return VisitStackPointerGreaterThan(node);
    case Opcode::kComparison: {
      const ComparisonOp& comparison = op.Cast<ComparisonOp>();
      using Kind = ComparisonOp::Kind;
      switch (multi(comparison.kind, comparison.rep)) {
        case multi(Kind::kEqual, Rep::Word32()):
          return VisitWord32Equal(node);
        case multi(Kind::kEqual, Rep::Word64()):
          return VisitWord64Equal(node);
        case multi(Kind::kEqual, Rep::Float32()):
          return VisitFloat32Equal(node);
        case multi(Kind::kEqual, Rep::Float64()):
          return VisitFloat64Equal(node);
        case multi(Kind::kEqual, Rep::Tagged()):
          if constexpr (Is64() && !COMPRESS_POINTERS_BOOL) {
            return VisitWord64Equal(node);
          }
          return VisitWord32Equal(node);
        case multi(Kind::kSignedLessThan, Rep::Word32()):
          return VisitInt32LessThan(node);
        case multi(Kind::kSignedLessThan, Rep::Word64()):
          return VisitInt64LessThan(node);
        case multi(Kind::kSignedLessThan, Rep::Float32()):
          return VisitFloat32LessThan(node);
        case multi(Kind::kSignedLessThan, Rep::Float64()):
          return VisitFloat64LessThan(node);
        case multi(Kind::kSignedLessThanOrEqual, Rep::Word32()):
          return VisitInt32LessThanOrEqual(node);
        case multi(Kind::kSignedLessThanOrEqual, Rep::Word64()):
          return VisitInt64LessThanOrEqual(node);
        case multi(Kind::kSignedLessThanOrEqual, Rep::Float32()):
          return VisitFloat32LessThanOrEqual(node);
        case multi(Kind::kSignedLessThanOrEqual, Rep::Float64()):
          return VisitFloat64LessThanOrEqual(node);
        case multi(Kind::kUnsignedLessThan, Rep::Word32()):
          return VisitUint32LessThan(node);
        case multi(Kind::kUnsignedLessThan, Rep::Word64()):
          return VisitUint64LessThan(node);
        case multi(Kind::kUnsignedLessThanOrEqual, Rep::Word32()):
          return VisitUint32LessThanOrEqual(node);
        case multi(Kind::kUnsignedLessThanOrEqual, Rep::Word64()):
          return VisitUint64LessThanOrEqual(node);
        default:
          UNREACHABLE();
      }
      UNREACHABLE();
    }
    case Opcode::kLoad: {
      const LoadOp& load = op.Cast<LoadOp>();
      MachineType loaded_type = load.machine_type();
      MarkAsRepresentation(loaded_type.representation(), node);
      if (load.kind.maybe_unaligned) {
        DCHECK(!load.kind.with_trap_handler);
        if (loaded_type.representation() == MachineRepresentation::kWord8 ||
            InstructionSelector::AlignmentRequirements()
                .IsUnalignedLoadSupported(loaded_type.representation())) {
          return VisitLoad(node);
        } else {
          return VisitUnalignedLoad(node);
        }
      } else if (load.kind.is_atomic) {
        if (load.result_rep == Rep::Word32()) {
          return VisitWord32AtomicLoad(node);
        } else {
          DCHECK_EQ(load.result_rep, Rep::Word64());
          return VisitWord64AtomicLoad(node);
        }
      } else if (load.kind.with_trap_handler) {
        DCHECK(!load.kind.maybe_unaligned);
        return VisitProtectedLoad(node);
      } else {
        return VisitLoad(node);
      }
      UNREACHABLE();
    }
    case Opcode::kStore: {
      const StoreOp& store = op.Cast<StoreOp>();
      MachineRepresentation rep =
          store.stored_rep.ToMachineType().representation();
      if (store.kind.maybe_unaligned) {
        DCHECK(!store.kind.with_trap_handler);
        DCHECK_EQ(store.write_barrier, WriteBarrierKind::kNoWriteBarrier);
        if (rep == MachineRepresentation::kWord8 ||
            InstructionSelector::AlignmentRequirements()
                .IsUnalignedStoreSupported(rep)) {
          return VisitStore(node);
        } else {
          return VisitUnalignedStore(node);
        }
      } else if (store.kind.is_atomic) {
        if (store.stored_rep == MemoryRepresentation::Int64() ||
            store.stored_rep == MemoryRepresentation::Uint64()) {
          return VisitWord64AtomicStore(node);
        } else {
          return VisitWord32AtomicStore(node);
        }
      } else if (store.kind.with_trap_handler) {
        DCHECK(!store.kind.maybe_unaligned);
        return VisitProtectedStore(node);
      } else {
        return VisitStore(node);
      }
      UNREACHABLE();
    }
    case Opcode::kTaggedBitcast: {
      const TaggedBitcastOp& cast = op.Cast<TaggedBitcastOp>();
      switch (multi(cast.from, cast.to)) {
        case multi(Rep::Tagged(), Rep::Word32()):
          MarkAsWord32(node);
          if constexpr (Is64()) {
            DCHECK_EQ(cast.kind, TaggedBitcastOp::Kind::kSmi);
            DCHECK(SmiValuesAre31Bits());
            return VisitBitcastSmiToWord(node);
          } else {
            return VisitBitcastTaggedToWord(node);
          }
        case multi(Rep::Tagged(), Rep::Word64()):
          MarkAsWord64(node);
          return VisitBitcastTaggedToWord(node);
        case multi(Rep::Word32(), Rep::Tagged()):
        case multi(Rep::Word64(), Rep::Tagged()):
          if (cast.kind == TaggedBitcastOp::Kind::kSmi) {
            MarkAsRepresentation(MachineRepresentation::kTaggedSigned, node);
            return EmitIdentity(node);
          } else {
            MarkAsTagged(node);
            return VisitBitcastWordToTagged(node);
          }
        case multi(Rep::Compressed(), Rep::Word32()):
          MarkAsWord32(node);
          if (cast.kind == TaggedBitcastOp::Kind::kSmi) {
            return VisitBitcastSmiToWord(node);
          } else {
            return VisitBitcastTaggedToWord(node);
          }
        default:
          UNIMPLEMENTED();
      }
    }
    case Opcode::kPhi:
      MarkAsRepresentation(op.Cast<PhiOp>().rep, node);
      return VisitPhi(node);
    case Opcode::kProjection:
      return VisitProjection(node);
    case Opcode::kDeoptimizeIf:
      if (Get(node).Cast<DeoptimizeIfOp>().negated) {
        return VisitDeoptimizeUnless(node);
      }
      return VisitDeoptimizeIf(node);
#if V8_ENABLE_WEBASSEMBLY
    case Opcode::kTrapIf: {
      const TrapIfOp& trap_if = op.Cast<TrapIfOp>();
      if (trap_if.negated) {
        return VisitTrapUnless(node, trap_if.trap_id);
      }
      return VisitTrapIf(node, trap_if.trap_id);
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case Opcode::kCatchBlockBegin:
      MarkAsTagged(node);
      return VisitIfException(node);
    case Opcode::kRetain:
      return VisitRetain(node);
    case Opcode::kOsrValue:
      MarkAsTagged(node);
      return VisitOsrValue(node);
    case Opcode::kStackSlot:
      return VisitStackSlot(node);
    case Opcode::kFrameState:
      // FrameState is covered as part of calls.
      UNREACHABLE();
    case Opcode::kLoadRootRegister:
      return VisitLoadRootRegister(node);
    case Opcode::kAssumeMap:
      // AssumeMap is used as a hint for optimization phases but does not
      // produce any code.
      return;
    case Opcode::kDebugBreak:
      return VisitDebugBreak(node);
    case Opcode::kAbortCSADcheck:
      return VisitAbortCSADcheck(node);
    case Opcode::kSelect: {
      const SelectOp& select = op.Cast<SelectOp>();
      // If there is a Select, then it should only be one that is supported by
      // the machine, and it should be meant to be implementation with cmove.
      DCHECK_EQ(select.implem, SelectOp::Implementation::kCMove);
      MarkAsRepresentation(select.rep, node);
      return VisitSelect(node);
    }
    case Opcode::kWord32PairBinop: {
      const Word32PairBinopOp& binop = op.Cast<Word32PairBinopOp>();
      MarkAsWord32(node);
      MarkPairProjectionsAsWord32(node);
      switch (binop.kind) {
        case Word32PairBinopOp::Kind::kAdd:
          return VisitInt32PairAdd(node);
        case Word32PairBinopOp::Kind::kSub:
          return VisitInt32PairSub(node);
        case Word32PairBinopOp::Kind::kMul:
          return VisitInt32PairMul(node);
        case Word32PairBinopOp::Kind::kShiftLeft:
          return VisitWord32PairShl(node);
        case Word32PairBinopOp::Kind::kShiftRightLogical:
          return VisitWord32PairShr(node);
        case Word32PairBinopOp::Kind::kShiftRightArithmetic:
          return VisitWord32PairSar(node);
      }
      UNREACHABLE();
    }
    case Opcode::kAtomicWord32Pair: {
      const AtomicWord32PairOp& atomic_op = op.Cast<AtomicWord32PairOp>();
      if (atomic_op.kind != AtomicWord32PairOp::Kind::kStore) {
        MarkAsWord32(node);
        MarkPairProjectionsAsWord32(node);
      }
      switch (atomic_op.kind) {
        case AtomicWord32PairOp::Kind::kAdd:
          return VisitWord32AtomicPairAdd(node);
        case AtomicWord32PairOp::Kind::kAnd:
          return VisitWord32AtomicPairAnd(node);
        case AtomicWord32PairOp::Kind::kCompareExchange:
          return VisitWord32AtomicPairCompareExchange(node);
        case AtomicWord32PairOp::Kind::kExchange:
          return VisitWord32AtomicPairExchange(node);
        case AtomicWord32PairOp::Kind::kLoad:
          return VisitWord32AtomicPairLoad(node);
        case AtomicWord32PairOp::Kind::kOr:
          return VisitWord32AtomicPairOr(node);
        case AtomicWord32PairOp::Kind::kSub:
          return VisitWord32AtomicPairSub(node);
        case AtomicWord32PairOp::Kind::kXor:
          return VisitWord32AtomicPairXor(node);
        case AtomicWord32PairOp::Kind::kStore:
          return VisitWord32AtomicPairStore(node);
      }
    }
    case Opcode::kBitcastWord32PairToFloat64:
      return MarkAsFloat64(node), VisitBitcastWord32PairToFloat64(node);
    case Opcode::kAtomicRMW: {
      const AtomicRMWOp& atomic_op = op.Cast<AtomicRMWOp>();
      MarkAsRepresentation(atomic_op.memory_rep.ToRegisterRepresentation(),
                           node);
      if (atomic_op.in_out_rep == Rep::Word32()) {
        switch (atomic_op.bin_op) {
          case AtomicRMWOp::BinOp::kAdd:
            return VisitWord32AtomicAdd(node);
          case AtomicRMWOp::BinOp::kSub:
            return VisitWord32AtomicSub(node);
          case AtomicRMWOp::BinOp::kAnd:
            return VisitWord32AtomicAnd(node);
          case AtomicRMWOp::BinOp::kOr:
            return VisitWord32AtomicOr(node);
          case AtomicRMWOp::BinOp::kXor:
            return VisitWord32AtomicXor(node);
          case AtomicRMWOp::BinOp::kExchange:
            return VisitWord32AtomicExchange(node);
          case AtomicRMWOp::BinOp::kCompareExchange:
            return VisitWord32AtomicCompareExchange(node);
        }
      } else {
        DCHECK_EQ(atomic_op.in_out_rep, Rep::Word64());
        switch (atomic_op.bin_op) {
          case AtomicRMWOp::BinOp::kAdd:
            return VisitWord64AtomicAdd(node);
          case AtomicRMWOp::BinOp::kSub:
            return VisitWord64AtomicSub(node);
          case AtomicRMWOp::BinOp::kAnd:
            return VisitWord64AtomicAnd(node);
          case AtomicRMWOp::BinOp::kOr:
            return VisitWord64AtomicOr(node);
          case AtomicRMWOp::BinOp::kXor:
            return VisitWord64AtomicXor(node);
          case AtomicRMWOp::BinOp::kExchange:
            return VisitWord64AtomicExchange(node);
          case AtomicRMWOp::BinOp::kCompareExchange:
            return VisitWord64AtomicCompareExchange(node);
        }
      }
      UNREACHABLE();
    }
    case Opcode::kMemoryBarrier:
      return VisitMemoryBarrier(node);

    case Opcode::kComment:
      return VisitComment(node);

#ifdef V8_ENABLE_WEBASSEMBLY
    case Opcode::kSimd128Constant: {
      const Simd128ConstantOp& constant = op.Cast<Simd128ConstantOp>();
      MarkAsSimd128(node);
      if (constant.IsZero()) return VisitS128Zero(node);
      return VisitS128Const(node);
    }
    case Opcode::kSimd128Unary: {
      const Simd128UnaryOp& unary = op.Cast<Simd128UnaryOp>();
      MarkAsSimd128(node);
      switch (unary.kind) {
#define VISIT_SIMD_UNARY(kind)        \
  case Simd128UnaryOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_128_UNARY_OPCODE(VISIT_SIMD_UNARY)
#undef VISIT_SIMD_UNARY
      }
    }
    case Opcode::kSimd128Reduce: {
      const Simd128ReduceOp& reduce = op.Cast<Simd128ReduceOp>();
      MarkAsSimd128(node);
      switch (reduce.kind) {
        case Simd128ReduceOp::Kind::kI8x16AddReduce:
          return VisitI8x16AddReduce(node);
        case Simd128ReduceOp::Kind::kI16x8AddReduce:
          return VisitI16x8AddReduce(node);
        case Simd128ReduceOp::Kind::kI32x4AddReduce:
          return VisitI32x4AddReduce(node);
        case Simd128ReduceOp::Kind::kI64x2AddReduce:
          return VisitI64x2AddReduce(node);
        case Simd128ReduceOp::Kind::kF32x4AddReduce:
          return VisitF32x4AddReduce(node);
        case Simd128ReduceOp::Kind::kF64x2AddReduce:
          return VisitF64x2AddReduce(node);
      }
    }
    case Opcode::kSimd128Binop: {
      const Simd128BinopOp& binop = op.Cast<Simd128BinopOp>();
      MarkAsSimd128(node);
      switch (binop.kind) {
#define VISIT_SIMD_BINOP(kind)        \
  case Simd128BinopOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_128_BINARY_OPCODE(VISIT_SIMD_BINOP)
#undef VISIT_SIMD_BINOP
      }
    }
    case Opcode::kSimd128Shift: {
      const Simd128ShiftOp& shift = op.Cast<Simd128ShiftOp>();
      MarkAsSimd128(node);
      switch (shift.kind) {
#define VISIT_SIMD_SHIFT(kind)        \
  case Simd128ShiftOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_128_SHIFT_OPCODE(VISIT_SIMD_SHIFT)
#undef VISIT_SIMD_SHIFT
      }
    }
    case Opcode::kSimd128Test: {
      const Simd128TestOp& test = op.Cast<Simd128TestOp>();
      MarkAsWord32(node);
      switch (test.kind) {
#define VISIT_SIMD_TEST(kind)        \
  case Simd128TestOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_128_TEST_OPCODE(VISIT_SIMD_TEST)
#undef VISIT_SIMD_TEST
      }
    }
    case Opcode::kSimd128Splat: {
      const Simd128SplatOp& splat = op.Cast<Simd128SplatOp>();
      MarkAsSimd128(node);
      switch (splat.kind) {
#define VISIT_SIMD_SPLAT(kind)        \
  case Simd128SplatOp::Kind::k##kind: \
    return Visit##kind##Splat(node);
        FOREACH_SIMD_128_SPLAT_OPCODE(VISIT_SIMD_SPLAT)
#undef VISIT_SIMD_SPLAT
      }
    }
    case Opcode::kSimd128Shuffle:
      MarkAsSimd128(node);
      return VisitI8x16Shuffle(node);
    case Opcode::kSimd128ReplaceLane: {
      const Simd128ReplaceLaneOp& replace = op.Cast<Simd128ReplaceLaneOp>();
      MarkAsSimd128(node);
      switch (replace.kind) {
        case Simd128ReplaceLaneOp::Kind::kI8x16:
          return VisitI8x16ReplaceLane(node);
        case Simd128ReplaceLaneOp::Kind::kI16x8:
          return VisitI16x8ReplaceLane(node);
        case Simd128ReplaceLaneOp::Kind::kI32x4:
          return VisitI32x4ReplaceLane(node);
        case Simd128ReplaceLaneOp::Kind::kI64x2:
          return VisitI64x2ReplaceLane(node);
        case Simd128ReplaceLaneOp::Kind::kF16x8:
          return VisitF16x8ReplaceLane(node);
        case Simd128ReplaceLaneOp::Kind::kF32x4:
          return VisitF32x4ReplaceLane(node);
        case Simd128ReplaceLaneOp::Kind::kF64x2:
          return VisitF64x2ReplaceLane(node);
      }
    }
    case Opcode::kSimd128ExtractLane: {
      const Simd128ExtractLaneOp& extract = op.Cast<Simd128ExtractLaneOp>();
      switch (extract.kind) {
        case Simd128ExtractLaneOp::Kind::kI8x16S:
          MarkAsWord32(node);
          return VisitI8x16ExtractLaneS(node);
        case Simd128ExtractLaneOp::Kind::kI8x16U:
          MarkAsWord32(node);
          return VisitI8x16ExtractLaneU(node);
        case Simd128ExtractLaneOp::Kind::kI16x8S:
          MarkAsWord32(node);
          return VisitI16x8ExtractLaneS(node);
        case Simd128ExtractLaneOp::Kind::kI16x8U:
          MarkAsWord32(node);
          return VisitI16x8ExtractLaneU(node);
        case Simd128ExtractLaneOp::Kind::kI32x4:
          MarkAsWord32(node);
          return VisitI32x4ExtractLane(node);
        case Simd128ExtractLaneOp::Kind::kI64x2:
          MarkAsWord64(node);
          return VisitI64x2ExtractLane(node);
        case Simd128ExtractLaneOp::Kind::kF16x8:
          MarkAsFloat32(node);
          return VisitF16x8ExtractLane(node);
        case Simd128ExtractLaneOp::Kind::kF32x4:
          MarkAsFloat32(node);
          return VisitF32x4ExtractLane(node);
        case Simd128ExtractLaneOp::Kind::kF64x2:
          MarkAsFloat64(node);
          return VisitF64x2ExtractLane(node);
      }
    }
    case Opcode::kSimd128LoadTransform:
      MarkAsSimd128(node);
      return VisitLoadTransform(node);
    case Opcode::kSimd128LaneMemory: {
      const Simd128LaneMemoryOp& memory = op.Cast<Simd128LaneMemoryOp>();
      MarkAsSimd128(node);
      if (memory.mode == Simd128LaneMemoryOp::Mode::kLoad) {
        return VisitLoadLane(node);
      } else {
        DCHECK_EQ(memory.mode, Simd128LaneMemoryOp::Mode::kStore);
        return VisitStoreLane(node);
      }
    }
    case Opcode::kSimd128Ternary: {
      const Simd128TernaryOp& ternary = op.Cast<Simd128TernaryOp>();
      MarkAsSimd128(node);
      switch (ternary.kind) {
#define VISIT_SIMD_TERNARY(kind)        \
  case Simd128TernaryOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_128_TERNARY_OPCODE(VISIT_SIMD_TERNARY)
#undef VISIT_SIMD_TERNARY
      }
    }

    // SIMD256
#if V8_ENABLE_WASM_SIMD256_REVEC
    case Opcode::kSimd256Constant: {
      const Simd256ConstantOp& constant = op.Cast<Simd256ConstantOp>();
      MarkAsSimd256(node);
      if (constant.IsZero()) return VisitS256Zero(node);
      return VisitS256Const(node);
    }
    case Opcode::kSimd256Extract128Lane: {
      MarkAsSimd128(node);
      return VisitExtractF128(node);
    }
    case Opcode::kSimd256LoadTransform: {
      MarkAsSimd256(node);
      return VisitSimd256LoadTransform(node);
    }
    case Opcode::kSimd256Unary: {
      const Simd256UnaryOp& unary = op.Cast<Simd256UnaryOp>();
      MarkAsSimd256(node);
      switch (unary.kind) {
#define VISIT_SIMD_256_UNARY(kind)    \
  case Simd256UnaryOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_256_UNARY_OPCODE(VISIT_SIMD_256_UNARY)
#undef VISIT_SIMD_256_UNARY
      }
    }
    case Opcode::kSimd256Binop: {
      const Simd256BinopOp& binop = op.Cast<Simd256BinopOp>();
      MarkAsSimd256(node);
      switch (binop.kind) {
#define VISIT_SIMD_BINOP(kind)        \
  case Simd256BinopOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_256_BINARY_OPCODE(VISIT_SIMD_BINOP)
#undef VISIT_SIMD_BINOP
      }
    }
    case Opcode::kSimd256Shift: {
      const Simd256ShiftOp& shift = op.Cast<Simd256ShiftOp>();
      MarkAsSimd256(node);
      switch (shift.kind) {
#define VISIT_SIMD_SHIFT(kind)        \
  case Simd256ShiftOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_256_SHIFT_OPCODE(VISIT_SIMD_SHIFT)
#undef VISIT_SIMD_SHIFT
      }
    }
    case Opcode::kSimd256Ternary: {
      const Simd256TernaryOp& ternary = op.Cast<Simd256TernaryOp>();
      MarkAsSimd256(node);
      switch (ternary.kind) {
#define VISIT_SIMD_256_TERNARY(kind)    \
  case Simd256TernaryOp::Kind::k##kind: \
    return Visit##kind(node);
        FOREACH_SIMD_256_TERNARY_OPCODE(VISIT_SIMD_256_TERNARY)
#undef VISIT_SIMD_256_UNARY
      }
    }
    case Opcode::kSimd256Splat: {
      const Simd256SplatOp& splat = op.Cast<Simd256SplatOp>();
      MarkAsSimd256(node);
      switch (splat.kind) {
#define VISIT_SIMD_SPLAT(kind)        \
  case Simd256SplatOp::Kind::k##kind: \
    return Visit##kind##Splat(node);
        FOREACH_SIMD_256_SPLAT_OPCODE(VISIT_SIMD_SPLAT)
#undef VISIT_SIMD_SPLAT
      }
    }
#ifdef V8_TARGET_ARCH_X64
    case Opcode::kSimd256Shufd: {
      MarkAsSimd256(node);
      return VisitSimd256Shufd(node);
    }
    case Opcode::kSimd256Shufps: {
      MarkAsSimd256(node);
      return VisitSimd256Shufps(node);
    }
    case Opcode::kSimd256Unpack: {
      MarkAsSimd256(node);
      return VisitSimd256Unpack(node);
    }
    case Opcode::kSimdPack128To256: {
      MarkAsSimd256(node);
      return VisitSimdPack128To256(node);
    }
#endif  // V8_TARGET_ARCH_X64
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

    case Opcode::kLoadStackPointer:
      return VisitLoadStackPointer(node);

    case Opcode::kSetStackPointer:
      return VisitSetStackPointer(node);

#endif  // V8_ENABLE_WEBASSEMBLY

#define UNREACHABLE_CASE(op) case Opcode::k##op:
      TURBOSHAFT_JS_OPERATION_LIST(UNREACHABLE_CASE)
      TURBOSHAFT_SIMPLIFIED_OPERATION_LIST(UNREACHABLE_CASE)
      TURBOSHAFT_WASM_OPERATION_LIST(UNREACHABLE_CASE)
      TURBOSHAFT_OTHER_OPERATION_LIST(UNREACHABLE_CASE)
      UNREACHABLE_CASE(PendingLoopPhi)
      UNREACHABLE_CASE(Tuple)
      UNREACHABLE_CASE(Dead)
      UNREACHABLE();
#undef UNREACHABLE_CASE
  }
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::CanProduceSignalingNaN(Node* node) {
  // TODO(jarin) Improve the heuristic here.
  if (node->opcode() == IrOpcode::kFloat64Add ||
      node->opcode() == IrOpcode::kFloat64Sub ||
      node->opcode() == IrOpcode::kFloat64Mul) {
    return false;
  }
  return true;
}

#if V8_TARGET_ARCH_64_BIT
template <typename Adapter>
bool InstructionSelectorT<Adapter>::ZeroExtendsWord32ToWord64(
    node_t node, int recursion_depth) {
  // To compute whether a Node sets its upper 32 bits to zero, there are three
  // cases.
  // 1. Phi node, with a computed result already available in phi_states_:
  //    Read the value from phi_states_.
  // 2. Phi node, with no result available in phi_states_ yet:
  //    Recursively check its inputs, and store the result in phi_states_.
  // 3. Anything else:
  //    Call the architecture-specific ZeroExtendsWord32ToWord64NoPhis.

  // Limit recursion depth to avoid the possibility of stack overflow on very
  // large functions.
  const int kMaxRecursionDepth = 100;

  if (this->IsPhi(node)) {
    // Intermediate results from previous calls are not necessarily correct.
    if (recursion_depth == 0) {
      static_assert(sizeof(Upper32BitsState) == 1);
      memset(phi_states_.data(),
             static_cast<int>(Upper32BitsState::kNotYetChecked),
             phi_states_.size());
    }

    Upper32BitsState current = phi_states_[this->id(node)];
    if (current != Upper32BitsState::kNotYetChecked) {
      return current == Upper32BitsState::kUpperBitsGuaranteedZero;
    }

    // If further recursion is prevented, we can't make any assumptions about
    // the output of this phi node.
    if (recursion_depth >= kMaxRecursionDepth) {
      return false;
    }

    // Mark the current node so that we skip it if we recursively visit it
    // again. Or, said differently, we compute a largest fixed-point so we can
    // be optimistic when we hit cycles.
    phi_states_[this->id(node)] = Upper32BitsState::kUpperBitsGuaranteedZero;

    int input_count = this->value_input_count(node);
    for (int i = 0; i < input_count; ++i) {
      node_t input = this->input_at(node, i);
      if (!ZeroExtendsWord32ToWord64(input, recursion_depth + 1)) {
        phi_states_[this->id(node)] = Upper32BitsState::kNoGuarantee;
        return false;
      }
    }

    return true;
  }
  return ZeroExtendsWord32ToWord64NoPhis(node);
}
#endif  // V8_TARGET_ARCH_64_BIT

namespace {

FrameStateDescriptor* GetFrameStateDescriptorInternal(
    Zone* zone, turboshaft::Graph* graph,
    const turboshaft::FrameStateOp& state) {
  const FrameStateInfo& state_info = state.data->frame_state_info;
  uint16_t parameters = state_info.parameter_count();
  uint16_t max_arguments = state_info.max_arguments();
  int locals = state_info.local_count();
  int stack = state_info.stack_count();

  FrameStateDescriptor* outer_state = nullptr;
  if (state.inlined) {
    outer_state = GetFrameStateDescriptorInternal(
        zone, graph,
        graph->Get(state.parent_frame_state())
            .template Cast<turboshaft::FrameStateOp>());
  }

#if V8_ENABLE_WEBASSEMBLY
  if (state_info.type() == FrameStateType::kJSToWasmBuiltinContinuation) {
    auto function_info = static_cast<const JSToWasmFrameStateFunctionInfo*>(
        state_info.function_info());
    return zone->New<JSToWasmFrameStateDescriptor>(
        zone, state_info.type(), state_info.bailout_id(),
        state_info.state_combine(), parameters, locals, stack,
        state_info.shared_info(), outer_state, function_info->signature());
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  return zone->New<FrameStateDescriptor>(
      zone, state_info.type(), state_info.bailout_id(),
      state_info.state_combine(), parameters, max_arguments, locals, stack,
      state_info.shared_info(), outer_state,
      state_info.function_info()->wasm_liftoff_frame_size(),
      state_info.function_info()->wasm_function_index());
}

FrameStateDescriptor* GetFrameStateDescriptorInternal(Zone* zone,
                                                      FrameState state) {
  DCHECK_EQ(IrOpcode::kFrameState, state->opcode());
  DCHECK_EQ(FrameState::kFrameStateInputCount, state->InputCount());
  const FrameStateInfo& state_info = FrameStateInfoOf(state->op());
  uint16_t parameters = state_info.parameter_count();
  uint16_t max_arguments = state_info.max_arguments();
  int locals = state_info.local_count();
  int stack = state_info.stack_count();

  FrameStateDescriptor* outer_state = nullptr;
  if (state.outer_frame_state()->opcode() == IrOpcode::kFrameState) {
    outer_state = GetFrameStateDescriptorInternal(
        zone, FrameState{state.outer_frame_state()});
  }

#if V8_ENABLE_WEBASSEMBLY
  if (state_info.type() == FrameStateType::kJSToWasmBuiltinContinuation) {
    auto function_info = static_cast<const JSToWasmFrameStateFunctionInfo*>(
        state_info.function_info());
    return zone->New<JSToWasmFrameStateDescriptor>(
        zone, state_info.type(), state_info.bailout_id(),
        state_info.state_combine(), parameters, locals, stack,
        state_info.shared_info(), outer_state, function_info->signature());
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  return zone->New<FrameStateDescriptor>(
      zone, state_info.type(), state_info.bailout_id(),
      state_info.state_combine(), parameters, max_arguments, locals, stack,
      state_info.shared_info(), outer_state,
      state_info.function_info()->wasm_liftoff_frame_size(),
      state_info.function_info()->wasm_function_index());
}

}  // namespace

template <>
FrameStateDescriptor*
InstructionSelectorT<TurboshaftAdapter>::GetFrameStateDescriptor(node_t node) {
  const turboshaft::FrameStateOp& state =
      this->turboshaft_graph()
          ->Get(node)
          .template Cast<turboshaft::FrameStateOp>();
  auto* desc = GetFrameStateDescriptorInternal(instruction_zone(),
                                               this->turboshaft_graph(), state);
  *max_unoptimized_frame_height_ =
      std::max(*max_unoptimized_frame_height_,
               desc->total_conservative_frame_size_in_bytes() +
                   (desc->max_arguments() * kSystemPointerSize));
  return desc;
}

template <>
FrameStateDescriptor*
InstructionSelectorT<TurbofanAdapter>::GetFrameStateDescriptor(node_t node) {
  FrameState state{node};
  auto* desc = GetFrameStateDescriptorInternal(instruction_zone(), state);
  *max_unoptimized_frame_height_ =
      std::max(*max_unoptimized_frame_height_,
               desc->total_conservative_frame_size_in_bytes() +
                   (desc->max_arguments() * kSystemPointerSize));
  return desc;
}

#if V8_ENABLE_WEBASSEMBLY
// static
template <typename Adapter>
void InstructionSelectorT<Adapter>::SwapShuffleInputs(
    typename Adapter::SimdShuffleView& view) {
  view.SwapInputs();
}
#endif  // V8_ENABLE_WEBASSEMBLY

template class InstructionSelectorT<TurbofanAdapter>;
template class InstructionSelectorT<TurboshaftAdapter>;

// static
InstructionSelector InstructionSelector::ForTurbofan(
    Zone* zone, size_t node_count, Linkage* linkage,
    InstructionSequence* sequence, Schedule* schedule,
    SourcePositionTable* source_positions, Frame* frame,
    EnableSwitchJumpTable enable_switch_jump_table, TickCounter* tick_counter,
    JSHeapBroker* broker, size_t* max_unoptimized_frame_height,
    size_t* max_pushed_argument_count, SourcePositionMode source_position_mode,
    Features features, EnableScheduling enable_scheduling,
    EnableRootsRelativeAddressing enable_roots_relative_addressing,
    EnableTraceTurboJson trace_turbo) {
  return InstructionSelector(
      new InstructionSelectorT<TurbofanAdapter>(
          zone, node_count, linkage, sequence, schedule, source_positions,
          frame, enable_switch_jump_table, tick_counter, broker,
          max_unoptimized_frame_height, max_pushed_argument_count,
          source_position_mode, features, enable_scheduling,
          enable_roots_relative_addressing, trace_turbo),
      nullptr);
}

InstructionSelector InstructionSelector::ForTurboshaft(
    Zone* zone, size_t node_count, Linkage* linkage,
    InstructionSequence* sequence, turboshaft::Graph* graph, Frame* frame,
    EnableSwitchJumpTable enable_switch_jump_table, TickCounter* tick_counter,
    JSHeapBroker* broker, size_t* max_unoptimized_frame_height,
    size_t* max_pushed_argument_count, SourcePositionMode source_position_mode,
    Features features, EnableScheduling enable_scheduling,
    EnableRootsRelativeAddressing enable_roots_relative_addressing,
    EnableTraceTurboJson trace_turbo) {
  return InstructionSelector(
      nullptr,
      new InstructionSelectorT<TurboshaftAdapter>(
          zone, node_count, linkage, sequence, graph,
          &graph->source_positions(), frame, enable_switch_jump_table,
          tick_counter, broker, max_unoptimized_frame_height,
          max_pushed_argument_count, source_position_mode, features,
          enable_scheduling, enable_roots_relative_addressing, trace_turbo));
}

InstructionSelector::InstructionSelector(
    InstructionSelectorT<TurbofanAdapter>* turbofan_impl,
    InstructionSelectorT<TurboshaftAdapter>* turboshaft_impl)
    : turbofan_impl_(turbofan_impl), turboshaft_impl_(turboshaft_impl) {
  DCHECK_NE(!turbofan_impl_, !turboshaft_impl_);
}

InstructionSelector::~InstructionSelector() {
  DCHECK_NE(!turbofan_impl_, !turboshaft_impl_);
  delete turbofan_impl_;
  delete turboshaft_impl_;
}

#define DISPATCH_TO_IMPL(...)                    \
  DCHECK_NE(!turbofan_impl_, !turboshaft_impl_); \
  if (turbofan_impl_) {                          \
    return turbofan_impl_->__VA_ARGS__;          \
  } else {                                       \
    return turboshaft_impl_->__VA_ARGS__;        \
  }

std::optional<BailoutReason> InstructionSelector::SelectInstructions() {
  DISPATCH_TO_IMPL(SelectInstructions())
}

bool InstructionSelector::IsSupported(CpuFeature feature) const {
  DISPATCH_TO_IMPL(IsSupported(feature))
}

const ZoneVector<std::pair<int, int>>& InstructionSelector::instr_origins()
    const {
  DISPATCH_TO_IMPL(instr_origins())
}

const std::map<NodeId, int> InstructionSelector::GetVirtualRegistersForTesting()
    const {
  DISPATCH_TO_IMPL(GetVirtualRegistersForTesting());
}

#undef DISPATCH_TO_IMPL
#undef VISIT_UNSUPPORTED_OP

}  // namespace compiler
}  // namespace internal
}  // namespace v8
