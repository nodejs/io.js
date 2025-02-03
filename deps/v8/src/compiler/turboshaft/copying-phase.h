// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_COPYING_PHASE_H_
#define V8_COMPILER_TURBOSHAFT_COPYING_PHASE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/source-position.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/reducer-traits.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

using MaybeVariable = std::optional<Variable>;

V8_EXPORT_PRIVATE int CountDecimalDigits(uint32_t value);
struct PaddingSpace {
  int spaces;
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           PaddingSpace padding);

template <typename Next>
class ReducerBaseForwarder;
template <typename Next>
class WasmRevecReducer;

template <typename Derived, typename Base>
class OutputGraphAssembler : public Base {
#define FRIEND(op) friend struct op##Op;
  TURBOSHAFT_OPERATION_LIST(FRIEND)
#undef FRIEND
  template <size_t I, class D>
  friend struct FixedArityOperationT;

  OpIndex Map(OpIndex index) { return derived_this()->MapToNewGraph(index); }

  OptionalOpIndex Map(OptionalOpIndex index) {
    return derived_this()->MapToNewGraph(index);
  }

  template <size_t N>
  base::SmallVector<OpIndex, N> Map(base::Vector<const OpIndex> indices) {
    return derived_this()->template MapToNewGraph<N>(indices);
  }

 public:
#define ASSEMBLE(operation)                                         \
  OpIndex AssembleOutputGraph##operation(const operation##Op& op) { \
    return op.Explode(                                              \
        [a = assembler()](auto... args) {                           \
          return a->Reduce##operation(args...);                     \
        },                                                          \
        *this);                                                     \
  }
  TURBOSHAFT_OPERATION_LIST(ASSEMBLE)
#undef ASSEMBLE

 private:
  Derived* derived_this() { return static_cast<Derived*>(this); }
  Assembler<typename Base::ReducerList>* assembler() {
    return &derived_this()->Asm();
  }
};

template <class AfterNext>
class GraphVisitor : public OutputGraphAssembler<GraphVisitor<AfterNext>,
                                                 VariableReducer<AfterNext>> {
  template <typename N>
  friend class ReducerBaseForwarder;
  template <typename N>
  friend class WasmRevecReducer;

 public:
  using Next = VariableReducer<AfterNext>;
  TURBOSHAFT_REDUCER_BOILERPLATE(CopyingPhase)

  GraphVisitor()
      : input_graph_(Asm().modifiable_input_graph()),
        current_input_block_(nullptr),
        op_mapping_(Asm().input_graph().op_id_count(), OpIndex::Invalid(),
                    Asm().phase_zone(), &Asm().input_graph()),
        block_mapping_(Asm().input_graph().block_count(), nullptr,
                       Asm().phase_zone()),
        blocks_needing_variables_(Asm().input_graph().block_count(),
                                  Asm().phase_zone()),
        old_opindex_to_variables(Asm().input_graph().op_id_count(),
                                 Asm().phase_zone(), &Asm().input_graph()),
        blocks_to_clone_(Asm().phase_zone()) {
    Asm().output_graph().Reset();
  }

  // `trace_reduction` is a template parameter to avoid paying for tracing at
  // runtime.
  template <bool trace_reduction>
  void VisitGraph() {
    Asm().Analyze();

    // Creating initial old-to-new Block mapping.
    for (Block& input_block : Asm().modifiable_input_graph().blocks()) {
      block_mapping_[input_block.index()] = Asm().output_graph().NewBlock(
          input_block.IsLoop() ? Block::Kind::kLoopHeader : Block::Kind::kMerge,
          &input_block);
    }

    // Visiting the graph.
    VisitAllBlocks<trace_reduction>();

    Finalize();
  }

  void Bind(Block* block) {
    Next::Bind(block);
    block->SetOrigin(current_input_block());
  }

  void Finalize() {
    // Updating the source_positions.
    if (!Asm().input_graph().source_positions().empty()) {
      for (OpIndex index : Asm().output_graph().AllOperationIndices()) {
        OpIndex origin = Asm().output_graph().operation_origins()[index];
        Asm().output_graph().source_positions()[index] =
            origin.valid() ? Asm().input_graph().source_positions()[origin]
                           : SourcePosition::Unknown();
      }
    }
    // Updating the operation origins.
    NodeOriginTable* origins = Asm().data()->node_origins();
    if (origins) {
      for (OpIndex index : Asm().output_graph().AllOperationIndices()) {
        OpIndex origin = Asm().output_graph().operation_origins()[index];
        if (origin.valid()) {
          origins->SetNodeOrigin(index.id(), origin.id());
        }
      }
    }

    input_graph_.SwapWithCompanion();
  }

  const Block* current_input_block() { return current_input_block_; }

  bool* turn_loop_without_backedge_into_merge() {
    return &turn_loop_without_backedge_into_merge_;
  }

  // Emits a Goto to a cloned version of {input_block}, assuming that the only
  // predecessor of this cloned copy will be the current block. {input_block} is
  // not cloned right away (because this would recursively call VisitBlockBody,
  // which could cause stack overflows), and is instead added to the
  // {blocks_to_clone_} stack, whose blocks will be cloned once the current
  // block has been fully visited.
  void CloneBlockAndGoto(const Block* input_block) {
    Block* new_block =
        Asm().output_graph().NewBlock(input_block->kind(), input_block);

    // Computing which input of Phi operations to use when visiting
    // {input_block} (since {input_block} doesn't really have predecessors
    // anymore).
    int added_block_phi_input = input_block->GetPredecessorIndex(
        Asm().current_block()->OriginForBlockEnd());

    // There is no guarantees that {input_block} will be entirely removed just
    // because it's cloned/inlined, since it's possible that it has predecessors
    // for which this optimization didn't apply. As a result, we add it to
    // {blocks_needing_variables_}, so that if it's ever generated
    // normally, Variables are used when emitting its content, so that
    // they can later be merged when control flow merges with the current
    // version of {input_block} that we just cloned.
    blocks_needing_variables_.Add(input_block->index().id());

    Asm().Goto(new_block);

    blocks_to_clone_.push_back({input_block, added_block_phi_input, new_block});
  }

  // Visits and emits {input_block} right now (ie, in the current block). This
  // should not be called recursively in order to avoid stack overflow (ie,
  // processing {input_block} should never lead to calling CloneAndInlingBlock).
  void CloneAndInlineBlock(const Block* input_block) {
    if (Asm().generating_unreachable_operations()) return;

#ifdef DEBUG
    // Making sure that we didn't call CloneAndInlineBlock recursively.
    DCHECK(!is_in_recursive_inlining_);
    ScopedModification<bool> recursive_guard(&is_in_recursive_inlining_, true);
#endif

    // Computing which input of Phi operations to use when visiting
    // {input_block} (since {input_block} doesn't really have predecessors
    // anymore).
    int added_block_phi_input = input_block->GetPredecessorIndex(
        Asm().current_block()->OriginForBlockEnd());

    // There is no guarantees that {input_block} will be entirely removed just
    // because it's cloned/inlined, since it's possible that it has predecessors
    // for which this optimization didn't apply. As a result, we add it to
    // {blocks_needing_variables_}, so that if it's ever generated
    // normally, Variables are used when emitting its content, so that
    // they can later be merged when control flow merges with the current
    // version of {input_block} that we just cloned.
    blocks_needing_variables_.Add(input_block->index().id());

    ScopedModification<bool> set_true(&current_block_needs_variables_, true);
    VisitBlockBody<CanHavePhis::kYes, ForCloning::kYes, false>(
        input_block, added_block_phi_input);
  }

  // {InlineOp} introduces two limitations unlike {CloneAndInlineBlock}:
  // 1. The input operation must not be emitted anymore as part of its
  // regular input block;
  // 2. {InlineOp} must not be used multiple times for the same input op.
  bool InlineOp(OpIndex index, const Block* input_block) {
    return VisitOpAndUpdateMapping<false>(index, input_block);
  }

  template <bool can_be_invalid = false>
  OpIndex MapToNewGraph(OpIndex old_index, int predecessor_index = -1) {
    DCHECK(old_index.valid());
    OpIndex result = op_mapping_[old_index];

    if (!result.valid()) {
      // {op_mapping} doesn't have a mapping for {old_index}. The
      // VariableReducer should provide the mapping.
      MaybeVariable var = GetVariableFor(old_index);
      if constexpr (can_be_invalid) {
        if (!var.has_value()) {
          return OpIndex::Invalid();
        }
      }
      DCHECK(var.has_value());
      if (predecessor_index == -1) {
        result = Asm().GetVariable(var.value());
      } else {
        result = Asm().GetPredecessorValue(var.value(), predecessor_index);
      }
    }

    DCHECK_IMPLIES(!can_be_invalid, result.valid());
    return result;
  }

  template <bool can_be_invalid = false, typename T>
  V<T> MapToNewGraph(V<T> old_index, int predecessor_index = -1) {
    return V<T>::Cast(MapToNewGraph(static_cast<OpIndex>(old_index), -1));
  }

  Block* MapToNewGraph(const Block* block) const {
    Block* new_block = block_mapping_[block->index()];
    DCHECK_NOT_NULL(new_block);
    return new_block;
  }

  template <typename FunctionType>
  OpIndex ResolvePhi(const PhiOp& op, FunctionType&& map,
                     RegisterRepresentation rep) {
    if (op.input_count == 1) {
      // If, in the previous CopyingPhase, a loop header was turned into a
      // regular blocks, its PendingLoopPhis became Phis with a single input. We
      // can now just get rid of these Phis.
      return map(op.input(0), -1);
    }

    OpIndex ig_index = Asm().input_graph().Index(op);
    if (Asm().current_block()->IsLoop()) {
      DCHECK_EQ(op.input_count, 2);
      OpIndex og_index = map(op.input(0), -1);
      if (ig_index == op.input(PhiOp::kLoopPhiBackEdgeIndex)) {
        // Avoid emitting a Loop Phi which points to itself, instead
        // emit it's 0'th input.
        return og_index;
      }
      return Asm().PendingLoopPhi(og_index, rep);
    }

    base::Vector<const OpIndex> old_inputs = op.inputs();
    base::SmallVector<OpIndex, 64> new_inputs;
    int predecessor_count = Asm().current_block()->PredecessorCount();
    Block* old_pred = current_input_block_->LastPredecessor();
    Block* new_pred = Asm().current_block()->LastPredecessor();
    // Control predecessors might be missing after the optimization phase. So we
    // need to skip phi inputs that belong to control predecessors that have no
    // equivalent in the new graph.

    // We first assume that the order if the predecessors of the current block
    // did not change. If it did, {new_pred} won't be nullptr at the end of this
    // loop, and we'll instead fall back to the slower code below to compute the
    // inputs of the Phi.
    int predecessor_index = predecessor_count - 1;
    for (OpIndex input : base::Reversed(old_inputs)) {
      if (new_pred && new_pred->OriginForBlockEnd() == old_pred) {
        // Phis inputs have to come from predecessors. We thus have to
        // MapToNewGraph with {predecessor_index} so that we get an OpIndex that
        // is from a predecessor rather than one that comes from a Variable
        // merged in the current block.
        new_inputs.push_back(map(input, predecessor_index));
        new_pred = new_pred->NeighboringPredecessor();
        predecessor_index--;
      }
      old_pred = old_pred->NeighboringPredecessor();
    }
    DCHECK_IMPLIES(new_pred == nullptr, old_pred == nullptr);

    if (new_pred != nullptr) {
      // If {new_pred} is not nullptr, then the order of the predecessors
      // changed. This should only happen with blocks that were introduced in
      // the previous graph. For instance, consider this (partial) dominator
      // tree:
      //
      //     ╠ 7
      //     ║ ╠ 8
      //     ║ ╚ 10
      //     ╠ 9
      //     ╚ 11
      //
      // Where the predecessors of block 11 are blocks 9 and 10 (in that order).
      // In dominator visit order, block 10 will be visited before block 9.
      // Since blocks are added to predecessors when the predecessors are
      // visited, it means that in the new graph, the predecessors of block 11
      // are [10, 9] rather than [9, 10].
      // To account for this, we reorder the inputs of the Phi, and get rid of
      // inputs from blocks that vanished.

#ifdef DEBUG
      // To check that indices are set properly, we zap them in debug builds.
      for (auto& block : Asm().modifiable_input_graph().blocks()) {
        block.clear_custom_data();
      }
#endif
      uint32_t pos = current_input_block_->PredecessorCount() - 1;
      for (old_pred = current_input_block_->LastPredecessor();
           old_pred != nullptr; old_pred = old_pred->NeighboringPredecessor()) {
        // Store the current index of the {old_pred}.
        old_pred->set_custom_data(pos--, Block::CustomDataKind::kPhiInputIndex);
      }

      // Filling {new_inputs}: we iterate the new predecessors, and, for each
      // predecessor, we check the index of the input corresponding to the old
      // predecessor, and we put it next in {new_inputs}.
      new_inputs.clear();
      int predecessor_index = predecessor_count - 1;
      for (new_pred = Asm().current_block()->LastPredecessor();
           new_pred != nullptr; new_pred = new_pred->NeighboringPredecessor()) {
        const Block* origin = new_pred->OriginForBlockEnd();
        DCHECK_NOT_NULL(origin);
        OpIndex input = old_inputs[origin->get_custom_data(
            Block::CustomDataKind::kPhiInputIndex)];
        // Phis inputs have to come from predecessors. We thus have to
        // MapToNewGraph with {predecessor_index} so that we get an OpIndex that
        // is from a predecessor rather than one that comes from a Variable
        // merged in the current block.
        new_inputs.push_back(map(input, predecessor_index));
        predecessor_index--;
      }
    }

    DCHECK_EQ(new_inputs.size(), Asm().current_block()->PredecessorCount());

    if (new_inputs.size() == 1) {
      // This Operation used to be a Phi in a Merge, but since one (or more) of
      // the inputs of the merge have been removed, there is no need for a Phi
      // anymore.
      return new_inputs[0];
    }

    std::reverse(new_inputs.begin(), new_inputs.end());
    return Asm().ReducePhi(base::VectorOf(new_inputs), rep);
  }

  // The block from the input graph that corresponds to the current block as a
  // branch destination. Such a block might not exist, and this function uses a
  // trick to compute such a block in almost all cases, but might rarely fail
  // and return `nullptr` instead.
  const Block* OriginForBlockStart(Block* block) const {
    // Check that `block->origin_` is still valid as a block start and was not
    // changed to a semantically different block when inlining blocks.
    const Block* origin = block->origin_;
    if (origin && MapToNewGraph(origin) == block) return origin;
    return nullptr;
  }

  // Clone all of the blocks in {sub_graph} (which should be Blocks of the input
  // graph). If `keep_loop_kinds` is true, the loop headers are preserved, and
  // otherwise they are marked as Merge. An initial GotoOp jumping to the 1st
  // block of `sub_graph` is always emitted. The output Block corresponding to
  // the 1st block of `sub_graph` is returned.
  template <class Set>
  Block* CloneSubGraph(Set sub_graph, bool keep_loop_kinds,
                       bool is_loop_after_peeling = false) {
    // The BlockIndex of the blocks of `sub_graph` should be sorted so that
    // visiting them in order is correct (all of the predecessors of a block
    // should always be visited before the block itself).
    DCHECK(std::is_sorted(sub_graph.begin(), sub_graph.end(),
                          [](const Block* a, const Block* b) {
                            return a->index().id() <= b->index().id();
                          }));

    // 1. Create new blocks, and update old->new mapping. This is required to
    // emit multiple times the blocks of {sub_graph}: if a block `B1` in
    // {sub_graph} ends with a Branch/Goto to a block `B2` that is also in
    // {sub_graph}, then this Branch/Goto should go to the version of `B2` that
    // this CloneSubGraph will insert, rather than to a version inserted by a
    // previous call to CloneSubGraph or the version that the regular
    // VisitAllBlock function will emit.
    ZoneVector<Block*> old_mappings(sub_graph.size(), Asm().phase_zone());
    for (auto&& [input_block, old_mapping] :
         base::zip(sub_graph, old_mappings)) {
      old_mapping = block_mapping_[input_block->index()];
      Block::Kind kind = keep_loop_kinds && input_block->IsLoop()
                             ? Block::Kind::kLoopHeader
                             : Block::Kind::kMerge;
      block_mapping_[input_block->index()] =
          Asm().output_graph().NewBlock(kind, input_block);
    }

    // 2. Visit block in correct order (begin to end)

    // Emit a goto to 1st block.
    Block* start = block_mapping_[(*sub_graph.begin())->index()];
#ifdef DEBUG
    if (is_loop_after_peeling) start->set_has_peeled_iteration();
#endif
    Asm().Goto(start);
    // Visiting `sub_graph`.
    for (const Block* block : sub_graph) {
      blocks_needing_variables_.Add(block->index().id());
      VisitBlock<false>(block);
      ProcessWaitingCloningAndInlining<false>();
    }

    // 3. Restore initial old->new mapping
    for (auto&& [input_block, old_mapping] :
         base::zip(sub_graph, old_mappings)) {
      block_mapping_[input_block->index()] = old_mapping;
    }

    return start;
  }

  template <bool can_be_invalid = false>
  OptionalOpIndex MapToNewGraph(OptionalOpIndex old_index,
                                int predecessor_index = -1) {
    if (!old_index.has_value()) return OptionalOpIndex::Nullopt();
    return MapToNewGraph<can_be_invalid>(old_index.value(), predecessor_index);
  }

  template <size_t expected_size>
  base::SmallVector<OpIndex, expected_size> MapToNewGraph(
      base::Vector<const OpIndex> inputs) {
    base::SmallVector<OpIndex, expected_size> result;
    for (OpIndex input : inputs) {
      result.push_back(MapToNewGraph(input));
    }
    return result;
  }

 private:
  template <bool trace_reduction>
  void VisitAllBlocks() {
    base::SmallVector<const Block*, 128> visit_stack;

    visit_stack.push_back(&Asm().input_graph().StartBlock());
    while (!visit_stack.empty()) {
      const Block* block = visit_stack.back();
      visit_stack.pop_back();
      VisitBlock<trace_reduction>(block);
      ProcessWaitingCloningAndInlining<trace_reduction>();

      for (Block* child = block->LastChild(); child != nullptr;
           child = child->NeighboringChild()) {
        visit_stack.push_back(child);
      }
    }
  }

  template <bool trace_reduction>
  void VisitBlock(const Block* input_block) {
    Asm().SetCurrentOrigin(OpIndex::Invalid());
    current_block_needs_variables_ =
        blocks_needing_variables_.Contains(input_block->index().id());
    if constexpr (trace_reduction) {
      std::cout << "\nold " << PrintAsBlockHeader{*input_block} << "\n";
      std::cout << "new "
                << PrintAsBlockHeader{*MapToNewGraph(input_block),
                                      Asm().output_graph().next_block_index()}
                << "\n";
    }
    Block* new_block = MapToNewGraph(input_block);
    if (Asm().Bind(new_block)) {
      VisitBlockBody<CanHavePhis::kYes, ForCloning::kNo, trace_reduction>(
          input_block);
      if constexpr (trace_reduction) TraceBlockFinished();
    } else {
      if constexpr (trace_reduction) TraceBlockUnreachable();
    }

    // If we eliminate a loop backedge, we need to turn the loop into a
    // single-predecessor merge block.
    if (!turn_loop_without_backedge_into_merge_) return;
    const Operation& last_op = input_block->LastOperation(Asm().input_graph());
    if (auto* final_goto = last_op.TryCast<GotoOp>()) {
      if (final_goto->destination->IsLoop()) {
        if (input_block->index() >= final_goto->destination->index()) {
          Asm().FinalizeLoop(MapToNewGraph(final_goto->destination));
        } else {
          // We have a forward jump to a loop, rather than a backedge. We
          // don't need to do anything.
        }
      }
    }
  }

  enum class CanHavePhis { kNo, kYes };
  enum class ForCloning { kNo, kYes };

  template <CanHavePhis can_have_phis, ForCloning for_cloning,
            bool trace_reduction>
  void VisitBlockBody(const Block* input_block,
                      int added_block_phi_input = -1) {
    DCHECK_NOT_NULL(Asm().current_block());
    current_input_block_ = input_block;

    // Phis could be mutually recursive, for instance (in a loop header):
    //
    //     p1 = phi(a, p2)
    //     p2 = phi(b, p1)
    //
    // In this case, if we are currently unrolling the loop and visiting this
    // loop header that is now part of the loop body, then if we visit Phis
    // and emit new mapping (with CreateOldToNewMapping) as we go along, we
    // would visit p1 and emit a mapping saying "p1 = p2", and use this
    // mapping when visiting p2, then we'd map p2 to p2 instead of p1. To
    // overcome this issue, we first visit the Phis of the loop, emit the new
    // phis, and record the new mapping in a side-table ({new_phi_values}).
    // Then, we visit all of the operations of the loop and commit the new
    // mappings: phis were emitted before using the old mapping, and all of
    // the other operations will use the new mapping (as they should).
    //
    // Note that Phis are not always at the begining of blocks, but when they
    // aren't, they can't have inputs from the current block (except on their
    // backedge for loop phis, but they start as PendingLoopPhis without
    // backedge input), so visiting all Phis first is safe.

    // Visiting Phis and collecting their new OpIndices.
    base::SmallVector<OpIndex, 64> new_phi_values;
    if constexpr (can_have_phis == CanHavePhis::kYes) {
      for (OpIndex index : Asm().input_graph().OperationIndices(*input_block)) {
        if (ShouldSkipOperation(Asm().input_graph().Get(index))) continue;
        DCHECK_NOT_NULL(Asm().current_block());
        if (Asm().input_graph().Get(index).template Is<PhiOp>()) {
          OpIndex new_index;
          if constexpr (for_cloning == ForCloning::kYes) {
            // When cloning a block, it only has a single predecessor, and Phis
            // should therefore be removed and be replaced by the input
            // corresponding to this predecessor.
            DCHECK_NE(added_block_phi_input, -1);
            // This Phi has been cloned/inlined, and has thus now a single
            // predecessor, and shouldn't be a Phi anymore.
            new_index = MapToNewGraph(
                Asm().input_graph().Get(index).input(added_block_phi_input));
          } else {
            new_index =
                VisitOpNoMappingUpdate<trace_reduction>(index, input_block);
          }
          new_phi_values.push_back(new_index);
          if (!Asm().current_block()) {
            // A reducer has detected, based on the Phis of the block that were
            // visited so far, that we are in unreachable code (or, less likely,
            // decided, based on some Phis only, to jump away from this block?).
            return;
          }
        }
      }
    }
    DCHECK_NOT_NULL(Asm().current_block());

    // Visiting everything, updating Phi mappings, and emitting non-phi
    // operations.
    int phi_num = 0;
    bool stopped_early = false;
    for (OpIndex index : base::IterateWithoutLast(
             Asm().input_graph().OperationIndices(*input_block))) {
      if (ShouldSkipOperation(Asm().input_graph().Get(index))) continue;
      const Operation& op = Asm().input_graph().Get(index);
      if constexpr (can_have_phis == CanHavePhis::kYes) {
        if (op.Is<PhiOp>()) {
          CreateOldToNewMapping(index, new_phi_values[phi_num++]);
          continue;
        }
      }
      // Blocks with a single predecessor (for which CanHavePhis might be kNo)
      // can still have phis if they used to be loop header that were turned
      // into regular blocks.
      DCHECK_IMPLIES(op.Is<PhiOp>(), op.input_count == 1);

      if (!VisitOpAndUpdateMapping<trace_reduction>(index, input_block)) {
        stopped_early = true;
        break;
      }
    }
    // If the last operation of the loop above (= the one-before-last operation
    // of the block) was lowered to an unconditional deopt/trap/something like
    // that, then current_block will now be null, and there is no need visit the
    // last operation of the block.
    if (stopped_early || Asm().current_block() == nullptr) return;

    // The final operation (which should be a block terminator) of the block
    // is processed separately, because if it's a Goto to a block with a
    // single predecessor, we'll inline it. (we could have had a check `if (op
    // is a Goto)` in the loop above, but since this can only be true for the
    // last operation, we instead extracted it here to make things faster).
    const Operation& terminator =
        input_block->LastOperation(Asm().input_graph());
    DCHECK(terminator.IsBlockTerminator());
    VisitBlockTerminator<trace_reduction>(terminator, input_block);
  }

  template <bool trace_reduction>
  bool VisitOpAndUpdateMapping(OpIndex index, const Block* input_block) {
    if (Asm().current_block() == nullptr) return false;
    OpIndex new_index =
        VisitOpNoMappingUpdate<trace_reduction>(index, input_block);
    const Operation& op = Asm().input_graph().Get(index);
    if (CanBeUsedAsInput(op) && new_index.valid()) {
      CreateOldToNewMapping(index, new_index);
    }
    return true;
  }

  template <bool trace_reduction>
  OpIndex VisitOpNoMappingUpdate(OpIndex index, const Block* input_block) {
    Block* current_block = Asm().current_block();
    DCHECK_NOT_NULL(current_block);
    Asm().SetCurrentOrigin(index);
    current_block->SetOrigin(input_block);
    OpIndex first_output_index = Asm().output_graph().next_operation_index();
    USE(first_output_index);
    const Operation& op = Asm().input_graph().Get(index);
    if constexpr (trace_reduction) TraceReductionStart(index);
    if (ShouldSkipOperation(op)) {
      if constexpr (trace_reduction) TraceOperationSkipped();
      return OpIndex::Invalid();
    }
    OpIndex new_index;
    switch (op.opcode) {
#define EMIT_INSTR_CASE(Name)                                             \
  case Opcode::k##Name:                                                   \
    if (MayThrow(Opcode::k##Name)) return OpIndex::Invalid();             \
    new_index = Asm().ReduceInputGraph##Name(index, op.Cast<Name##Op>()); \
    break;
      TURBOSHAFT_OPERATION_LIST(EMIT_INSTR_CASE)
#undef EMIT_INSTR_CASE
    }
    if constexpr (trace_reduction) {
      if (CanBeUsedAsInput(op) && !new_index.valid()) {
        TraceOperationSkipped();
      } else {
        TraceReductionResult(current_block, first_output_index, new_index);
      }
    }
#ifdef DEBUG
    DCHECK_IMPLIES(new_index.valid(),
                   Asm().output_graph().BelongsToThisGraph(new_index));
    if (V8_UNLIKELY(v8_flags.turboshaft_verify_reductions)) {
      if (new_index.valid()) {
        const Operation& new_op = Asm().output_graph().Get(new_index);
        if (!new_op.Is<TupleOp>()) {
          // Checking that the outputs_rep of the new operation are the same as
          // the old operation. (except for tuples, since they don't have
          // outputs_rep)
          DCHECK_EQ(new_op.outputs_rep().size(), op.outputs_rep().size());
          for (size_t i = 0; i < new_op.outputs_rep().size(); ++i) {
            DCHECK(new_op.outputs_rep()[i].AllowImplicitRepresentationChangeTo(
                op.outputs_rep()[i],
                Asm().output_graph().IsCreatedFromTurbofan()));
          }
        }
        Asm().Verify(index, new_index);
      }
    }
#endif  // DEBUG
    return new_index;
  }

  template <bool trace_reduction>
  void VisitBlockTerminator(const Operation& terminator,
                            const Block* input_block) {
    if (Asm().CanAutoInlineBlocksWithSinglePredecessor() &&
        terminator.Is<GotoOp>()) {
      Block* destination = terminator.Cast<GotoOp>().destination;
      if (destination->PredecessorCount() == 1) {
        block_to_inline_now_ = destination;
        return;
      }
    }
    // Just going through the regular VisitOp function.
    OpIndex index = Asm().input_graph().Index(terminator);
    VisitOpAndUpdateMapping<trace_reduction>(index, input_block);
  }

  template <bool trace_reduction>
  void ProcessWaitingCloningAndInlining() {
    InlineWaitingBlock<trace_reduction>();
    while (!blocks_to_clone_.empty()) {
      BlockToClone item = blocks_to_clone_.back();
      blocks_to_clone_.pop_back();
      DoCloneBlock<trace_reduction>(
          item.input_block, item.added_block_phi_input, item.new_output_block);
      InlineWaitingBlock<trace_reduction>();
    }
  }

  template <bool trace_reduction>
  void InlineWaitingBlock() {
    while (block_to_inline_now_) {
      Block* input_block = block_to_inline_now_;
      block_to_inline_now_ = nullptr;
      ScopedModification<bool> set_true(&current_block_needs_variables_, true);
      if constexpr (trace_reduction) {
        std::cout << "Inlining " << PrintAsBlockHeader{*input_block} << "\n";
      }
      VisitBlockBody<CanHavePhis::kNo, ForCloning::kNo, trace_reduction>(
          input_block);
    }
  }

  template <bool trace_reduction>
  void DoCloneBlock(const Block* input_block, int added_block_phi_input,
                    Block* output_block) {
    DCHECK_EQ(output_block->PredecessorCount(), 1);
    if constexpr (trace_reduction) {
      std::cout << "\nCloning old " << PrintAsBlockHeader{*input_block} << "\n";
      std::cout << "As new "
                << PrintAsBlockHeader{*output_block,
                                      Asm().output_graph().next_block_index()}
                << "\n";
    }

    ScopedModification<bool> set_true(&current_block_needs_variables_, true);

    Asm().BindReachable(output_block);
    VisitBlockBody<CanHavePhis::kYes, ForCloning::kYes, trace_reduction>(
        input_block, added_block_phi_input);

    if constexpr (trace_reduction) TraceBlockFinished();
  }

  void TraceReductionStart(OpIndex index) {
    std::cout << "╭── o" << index.id() << ": "
              << PaddingSpace{5 - CountDecimalDigits(index.id())}
              << OperationPrintStyle{Asm().input_graph().Get(index), "#o"}
              << "\n";
  }
  void TraceOperationSkipped() { std::cout << "╰─> skipped\n\n"; }
  void TraceBlockUnreachable() { std::cout << "╰─> unreachable\n\n"; }
  void TraceReductionResult(Block* current_block, OpIndex first_output_index,
                            OpIndex new_index) {
    if (new_index < first_output_index) {
      // The operation was replaced with an already existing one.
      std::cout << "╰─> #n" << new_index.id() << "\n";
    }
    bool before_arrow = new_index >= first_output_index;
    for (const Operation& op : Asm().output_graph().operations(
             first_output_index, Asm().output_graph().next_operation_index())) {
      OpIndex index = Asm().output_graph().Index(op);
      const char* prefix;
      if (index == new_index) {
        prefix = "╰─>";
        before_arrow = false;
      } else if (before_arrow) {
        prefix = "│  ";
      } else {
        prefix = "   ";
      }
      std::cout << prefix << " n" << index.id() << ": "
                << PaddingSpace{5 - CountDecimalDigits(index.id())}
                << OperationPrintStyle{Asm().output_graph().Get(index), "#n"}
                << "\n";
      if (op.IsBlockTerminator() && Asm().current_block() &&
          Asm().current_block() != current_block) {
        current_block = &Asm().output_graph().Get(
            BlockIndex(current_block->index().id() + 1));
        std::cout << "new " << PrintAsBlockHeader{*current_block} << "\n";
      }
    }
    std::cout << "\n";
  }
  void TraceBlockFinished() { std::cout << "\n"; }

  // These functions take an operation from the old graph and use the assembler
  // to emit a corresponding operation in the new graph, translating inputs and
  // blocks accordingly.
  V8_INLINE OpIndex AssembleOutputGraphGoto(const GotoOp& op) {
    Block* destination = MapToNewGraph(op.destination);
    if (op.is_backedge) {
      DCHECK(destination->IsBound());
      DCHECK(destination->IsLoop());
      FixLoopPhis(op.destination);
    }
    // It is important that we first fix loop phis and then reduce the `Goto`,
    // because reducing the `Goto` can have side effects, in particular, it can
    // modify affect the SnapshotTable of `VariableReducer`, which is also used
    // by `FixLoopPhis()`.
    Asm().ReduceGoto(destination, op.is_backedge);
    return OpIndex::Invalid();
  }
  V8_INLINE OpIndex AssembleOutputGraphBranch(const BranchOp& op) {
    Block* if_true = MapToNewGraph(op.if_true);
    Block* if_false = MapToNewGraph(op.if_false);
    return Asm().ReduceBranch(MapToNewGraph(op.condition()), if_true, if_false,
                              op.hint);
  }
  OpIndex AssembleOutputGraphSwitch(const SwitchOp& op) {
    base::SmallVector<SwitchOp::Case, 16> cases;
    for (SwitchOp::Case c : op.cases) {
      cases.emplace_back(c.value, MapToNewGraph(c.destination), c.hint);
    }
    return Asm().ReduceSwitch(
        MapToNewGraph(op.input()),
        Asm().graph_zone()->CloneVector(base::VectorOf(cases)),
        MapToNewGraph(op.default_case), op.default_hint);
  }
  OpIndex AssembleOutputGraphPhi(const PhiOp& op) {
    return ResolvePhi(
        op,
        [this](OpIndex ind, int predecessor_index) {
          return MapToNewGraph(ind, predecessor_index);
        },
        op.rep);
  }
  OpIndex AssembleOutputGraphPendingLoopPhi(const PendingLoopPhiOp& op) {
    UNREACHABLE();
  }
  V8_INLINE OpIndex AssembleOutputGraphFrameState(const FrameStateOp& op) {
    auto inputs = MapToNewGraph<32>(op.inputs());
    return Asm().ReduceFrameState(base::VectorOf(inputs), op.inlined, op.data);
  }
  OpIndex AssembleOutputGraphCall(const CallOp& op) {
    OpIndex callee = MapToNewGraph(op.callee());
    OptionalOpIndex frame_state = MapToNewGraph(op.frame_state());
    auto arguments = MapToNewGraph<16>(op.arguments());
    return Asm().ReduceCall(callee, frame_state, base::VectorOf(arguments),
                            op.descriptor, op.Effects());
  }
  OpIndex AssembleOutputGraphDidntThrow(const DidntThrowOp& op) {
    const Operation& throwing_operation =
        Asm().input_graph().Get(op.throwing_operation());
    OpIndex result;
    switch (throwing_operation.opcode) {
#define CASE(Name)                                                     \
  case Opcode::k##Name:                                                \
    result = Asm().ReduceInputGraph##Name(                             \
        op.throwing_operation(), throwing_operation.Cast<Name##Op>()); \
    break;
      TURBOSHAFT_THROWING_OPERATIONS_LIST(CASE)
#undef CASE
      default:
        UNREACHABLE();
    }
    return result;
  }

  V<None> AssembleOutputGraphCheckException(const CheckExceptionOp& op) {
    Graph::OpIndexIterator it(op.didnt_throw_block->begin(),
                              &Asm().input_graph());
    Graph::OpIndexIterator end(op.didnt_throw_block->end(),
                               &Asm().input_graph());
    // To translate `CheckException` to the new graph, we reduce the throwing
    // operation (actually it's `DidntThrow` operation, but that triggers the
    // actual reduction) with a catch scope. If the reduction replaces the
    // throwing operation with other throwing operations, all of them will be
    // connected to the provided catch block. The reduction should automatically
    // bind a block that represents non-throwing control flow of the original
    // operation, so we can inline the rest of the `didnt_throw` block.
    {
      CatchScope scope(Asm(), MapToNewGraph(op.catch_block));
      DCHECK(Asm().input_graph().Get(*it).template Is<DidntThrowOp>());
      if (!Asm().InlineOp(*it, op.didnt_throw_block)) {
        return V<None>::Invalid();
      }
      ++it;
    }
    for (; it != end; ++it) {
      // Using `InlineOp` requires that the inlined operation is not emitted
      // multiple times. This is the case here because we just removed the
      // single predecessor of `didnt_throw_block`.
      if (!Asm().InlineOp(*it, op.didnt_throw_block)) {
        break;
      }
    }
    return V<None>::Invalid();
  }

  void CreateOldToNewMapping(OpIndex old_index, OpIndex new_index) {
    DCHECK(old_index.valid());
    DCHECK(Asm().input_graph().BelongsToThisGraph(old_index));
    DCHECK_IMPLIES(new_index.valid(),
                   Asm().output_graph().BelongsToThisGraph(new_index));

    if (current_block_needs_variables_) {
      MaybeVariable var = GetVariableFor(old_index);
      if (!var.has_value()) {
        MaybeRegisterRepresentation rep =
            Asm().input_graph().Get(old_index).outputs_rep().size() == 1
                ? static_cast<const MaybeRegisterRepresentation&>(
                      Asm().input_graph().Get(old_index).outputs_rep()[0])
                : MaybeRegisterRepresentation::None();
        var = Asm().NewLoopInvariantVariable(rep);
        SetVariableFor(old_index, *var);
      }
      Asm().SetVariable(*var, new_index);
      return;
    }

    DCHECK(!op_mapping_[old_index].valid());
    op_mapping_[old_index] = new_index;
  }

  MaybeVariable GetVariableFor(OpIndex old_index) const {
    return old_opindex_to_variables[old_index];
  }

  void SetVariableFor(OpIndex old_index, MaybeVariable var) {
    DCHECK(!old_opindex_to_variables[old_index].has_value());
    old_opindex_to_variables[old_index] = var;
  }

  void FixLoopPhis(Block* input_graph_loop) {
    DCHECK(input_graph_loop->IsLoop());
    Block* output_graph_loop = MapToNewGraph(input_graph_loop);
    DCHECK(output_graph_loop->IsLoop());
    for (const Operation& op : Asm().input_graph().operations(
             input_graph_loop->begin(), input_graph_loop->end())) {
      if (auto* input_phi = op.TryCast<PhiOp>()) {
        OpIndex phi_index =
            MapToNewGraph<true>(Asm().input_graph().Index(*input_phi));
        if (!phi_index.valid() || !output_graph_loop->Contains(phi_index)) {
          // Unused phis are skipped, so they are not be mapped to anything in
          // the new graph. If the phi is reduced to an operation from a
          // different block, then there is no loop phi in the current loop
          // header to take care of.
          continue;
        }
        Asm().FixLoopPhi(*input_phi, phi_index, output_graph_loop);
      }
    }
  }

  Graph& input_graph_;

  const Block* current_input_block_;

  // Mappings from old OpIndices to new OpIndices.
  FixedOpIndexSidetable<OpIndex> op_mapping_;

  // Mappings from old blocks to new blocks.
  FixedBlockSidetable<Block*> block_mapping_;

  // {current_block_needs_variables_} is set to true if the current block should
  // use Variables to map old to new OpIndex rather than just {op_mapping}. This
  // is typically the case when the block has been cloned.
  bool current_block_needs_variables_ = false;

  // When {turn_loop_without_backedge_into_merge_} is true (the default), when
  // processing an input block that ended with a loop backedge but doesn't
  // anymore, the loop header is turned into a regular merge. This can be turned
  // off when unrolling a loop for instance.
  bool turn_loop_without_backedge_into_merge_ = true;

  // Set of Blocks for which Variables should be used rather than
  // {op_mapping}.
  BitVector blocks_needing_variables_;

  // Mapping from old OpIndex to Variables.
  FixedOpIndexSidetable<MaybeVariable> old_opindex_to_variables;

  // When the last operation of a Block is a Goto to a Block with a single
  // predecessor, we always inline the destination into the current block. To
  // avoid making this process recursive (which could lead to stack overflows),
  // we set the variable {block_to_inline_now_} instead. Right after we're done
  // visiting a Block, the function ProcessWaitingCloningAndInlining will inline
  // {block_to_inline_now_} (if it's set) in a non-recursive way.
  Block* block_to_inline_now_ = nullptr;

  // When a Reducer wants to clone a block (for instance,
  // BranchEliminationReducer, in order to remove Phis or to replace a Branch by
  // a Goto), this block is not cloned right away, in order to avoid recursion
  // (which could lead to stack overflows). Instead, we add this block to
  // {blocks_to_clone_}. Right after we're done visiting a Block, the function
  // ProcessWaitingCloningAndInlining will actually clone the blocks in
  // {blocks_to_clone_} in a non-recursive way.
  struct BlockToClone {
    const Block* input_block;
    int added_block_phi_input;
    Block* new_output_block;
  };
  ZoneVector<BlockToClone> blocks_to_clone_;

#ifdef DEBUG
  // Recursively inlining blocks is still allowed (mainly for
  // LoopUnrollingReducer), but it shouldn't be actually recursive. This is
  // checked by the {is_in_recursive_inlining_}, which is set to true while
  // recursively inlining a block. Trying to inline a block while
  // {is_in_recursive_inlining_} is true will lead to a DCHECK failure.
  bool is_in_recursive_inlining_ = false;
#endif
};

template <template <class> class... Reducers>
class TSAssembler;

template <template <class> class... Reducers>
class CopyingPhaseImpl {
 public:
  static void Run(PipelineData* data, Graph& input_graph, Zone* phase_zone,
                  bool trace_reductions = false) {
    TSAssembler<GraphVisitor, Reducers...> phase(
        data, input_graph, input_graph.GetOrCreateCompanion(), phase_zone);
#ifdef DEBUG
    if (trace_reductions) {
      phase.template VisitGraph<true>();
    } else {
      phase.template VisitGraph<false>();
    }
#else
    phase.template VisitGraph<false>();
#endif  // DEBUG
  }
};

template <template <typename> typename... Reducers>
class CopyingPhase {
 public:
  static void Run(PipelineData* data, Zone* phase_zone) {
    Graph& input_graph = data->graph();
    CopyingPhaseImpl<Reducers...>::Run(
        data, input_graph, phase_zone,
        data->info()->turboshaft_trace_reduction());
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_COPYING_PHASE_H_
