// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/maglev-graph-building-phase.h"

#include <limits>
#include <memory>
#include <optional>
#include <type_traits>

#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/compiler/frame-states.h"
#include "src/compiler/globals.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/maglev-early-lowering-reducer-inl.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/handles/global-handles-inl.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-register.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph-verifier.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-phi-representation-selector.h"
#include "src/maglev/maglev-post-hoc-optimizations-processors.h"
#include "src/objects/elements-kind.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/objects.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

namespace {

MachineType MachineTypeFor(maglev::ValueRepresentation repr) {
  switch (repr) {
    case maglev::ValueRepresentation::kTagged:
      return MachineType::AnyTagged();
    case maglev::ValueRepresentation::kInt32:
      return MachineType::Int32();
    case maglev::ValueRepresentation::kUint32:
      return MachineType::Uint32();
    case maglev::ValueRepresentation::kIntPtr:
      return MachineType::IntPtr();
    case maglev::ValueRepresentation::kFloat64:
      return MachineType::Float64();
    case maglev::ValueRepresentation::kHoleyFloat64:
      return MachineType::HoleyFloat64();
  }
}

// TODO(dmercadier): use simply .contains once we have access to C++20.
template <typename K, typename V>
bool MapContains(ZoneUnorderedMap<K, V> map, K key) {
  return map.find(key) != map.end();
}

int ElementsKindSize(ElementsKind element_kind) {
  switch (element_kind) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) \
  case TYPE##_ELEMENTS:                           \
    DCHECK_LE(sizeof(ctype), 8);                  \
    return sizeof(ctype);
    TYPED_ARRAYS(TYPED_ARRAY_CASE)
    default:
      UNREACHABLE();
#undef TYPED_ARRAY_CASE
  }
}

}  // namespace

// This reducer tracks the Maglev origin of the Turboshaft blocks that we build
// during the translation. This is then used when reordering Phi inputs.
template <class Next>
class BlockOriginTrackingReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(BlockOriginTracking)
  void SetMaglevInputBlock(const maglev::BasicBlock* block) {
    maglev_input_block_ = block;
  }
  const maglev::BasicBlock* maglev_input_block() const {
    return maglev_input_block_;
  }
  void Bind(Block* block) {
    Next::Bind(block);
    // The 1st block we bind doesn't exist in Maglev and is meant to hold
    // Constants (which in Maglev are not in any block), and thus
    // {maglev_input_block_} should still be nullptr. In all other cases,
    // {maglev_input_block_} should not be nullptr.
    DCHECK_EQ(maglev_input_block_ == nullptr,
              block == &__ output_graph().StartBlock());
    turboshaft_block_origins_[block->index()] = maglev_input_block_;
  }

  const maglev::BasicBlock* GetMaglevOrigin(const Block* block) {
    DCHECK_NOT_NULL(turboshaft_block_origins_[block->index()]);
    return turboshaft_block_origins_[block->index()];
  }

 private:
  const maglev::BasicBlock* maglev_input_block_ = nullptr;
  GrowingBlockSidetable<const maglev::BasicBlock*> turboshaft_block_origins_{
      __ phase_zone()};
};

class GeneratorAnalyzer {
  // A document explaning how generators are handled by the translation can be
  // found here:
  //
  //     https://docs.google.com/document/d/1-iFoVuvpIEjA9dtSsOjmKL5vAzzvf0cKI6f4zaObiV8/edit?usp=sharing
  //
  //
  // Because of generator resumes, Maglev graphs can have edges that bypass loop
  // headers. This actually happens everytime a loop contains a `yield`.
  // In Turboshaft, however, the loop header must always dominate every block in
  // the loop, and thus does not allow such edges that bypass the loop header.
  // For instance,
  //
  //     function* foo() {
  //       for (let i = 0; i < 10; i++) {
  //         if (i % 2 == 0) {
  //           yield i;
  //         }
  //       }
  //     }
  //
  // The corresponding Maglev graph will look something like (this is a little
  // bit simplified since details don't matter much for this high level
  // explanation; the drawing in FindLoopHeaderBypasses below gives a more
  // precise view of what the Maglev graph looks like):
  //
  //                       + 1 ------+
  //                       | Switch  |
  //                       +---------+
  //                        /      \
  //                      /          \      |----------------------|
  //                    /              \    |                      |
  //                  /                 v   v                      |
  //                /              + 2 --------+                   |
  //              /                | Loop      |                   |
  //             |                 +-----------+                   |
  //             |                      |                          |
  //             |                      |                          |
  //             v                      v                          |
  //        + 4 ------+             + 3 --------------+            |
  //        | Resume  |             | Branch(i%2==0)  |            |
  //        +---------+             +-----------------+            |
  //            |                     /        \                   |
  //            |                    /          \                  |
  //            |                   /            \                 |
  //            |             + 5 -------+        |                |
  //            |             | yield i  |        |                |
  //            |             +----------+        |                |
  //            |                                 |                |
  //            |----------------------------|    |                |
  //                                         |    |                |
  //                                         v    v                |
  //                                    + 6 ----------+            |
  //                                    | i++         |            |
  //                                    | backedge    |            |
  //                                    +-------------+            |
  //                                           |                   |
  //                                           |-------------------|
  //
  // In this graph, the edge from block 4 to block 6 bypasses the loop header.
  //
  //
  // Note that it's even possible that the graph contains no forward path from
  // the loop header to the backedge. This happens for instance when the loop
  // body always unconditionally yields. In such cases, the backedge is always
  // reached through the main resume switch. For instance:
  //
  //     function* foo() {
  //       for (let i = 0; i < 10; i++) {
  //         yield i;
  //       }
  //     }
  //
  // Will produce the following graph:
  //
  //                       + 1 ------+
  //                       | Switch  |
  //                       +---------+
  //                        /      \
  //                      /          \      |-------------|
  //                    /              \    |             |
  //                  /                 v   v             |
  //                /              + 2 --------+          |
  //              /                | Loop      |          |
  //             |                 +-----------+          |
  //             |                      |                 |
  //             |                      |                 |
  //             v                      v                 |
  //        + 4 ------+             + 3 -------+          |
  //        | Resume  |             | yield i  |          |
  //        +---------+             +----------+          |
  //             |                                        |
  //             |                                        |
  //             |----------------------------------------|
  //
  //
  // GeneratorAnalyzer finds the loop in the Maglev graph, and finds the
  // generator resume edges that bypass loops headers. The GraphBuilder then
  // uses this information to re-route such edges to loop headers and insert
  // secondary switches. For instance, the graph right above will be transformed
  // to something like this:
  //
  //                       + 1 ------+
  //                       | Switch  |
  //                       +---------+
  //                          |  |
  //                          |  |
  //                          v  v
  //                     + 2 --------+
  //                     | p1 = phi  |
  //                     +-----------+
  //                          |
  //                          |    |-----------------------|
  //                          |    |                       |
  //                          v    v                       |
  //                     + 3 -----------------+            |
  //                     | Loop               |            |
  //                     | p2 = phi(p1,...)   |            |
  //                     +--------------------+            |
  //                           |                           |
  //                           |                           |
  //                           v                           |
  //                     + 4 -----------+                  |
  //                     | Switch(p2)   |                  |
  //                     +--------------+                  |
  //                       /       \                       |
  //                     /           \                     |
  //                   /               \                   |
  //                 v                   v                 |
  //           + 5 --------+        + 6 --------+          |
  //           | Resume    |        | yield i   |          |
  //           +-----------+        +-----------+          |
  //                 |                                     |
  //                 |                                     |
  //                 |-------------------------------------|

 public:
  explicit GeneratorAnalyzer(Zone* phase_zone,
                             maglev::MaglevGraphLabeller* labeller)
      : labeller_(labeller),
        block_to_header_(phase_zone),
        visit_queue_(phase_zone) {
    USE(labeller_);
  }

  void Analyze(maglev::Graph* graph) {
    for (auto it = graph->rbegin(); it != graph->rend(); ++it) {
      if ((*it)->is_loop()) {
        FindLoopBody(it);
      }
    }

    FindLoopHeaderBypasses(graph);
  }

  bool JumpBypassesHeader(const maglev::BasicBlock* target) {
    return block_to_innermost_bypassed_header_.contains(target);
  }

  const maglev::BasicBlock* GetInnermostBypassedHeader(
      const maglev::BasicBlock* target) {
    DCHECK(JumpBypassesHeader(target));
    return block_to_innermost_bypassed_header_[target];
  }

  bool HeaderIsBypassed(const maglev::BasicBlock* header) {
    DCHECK(header->is_loop());
    return bypassed_headers_.contains(header);
  }

  const maglev::BasicBlock* GetLoopHeader(const maglev::BasicBlock* node) {
    if (block_to_header_.contains(node)) {
      return block_to_header_[node];
    }
    return nullptr;
  }

  bool has_header_bypasses() const { return !bypassed_headers_.empty(); }

 private:
  // We consider that every block in between the loop header and the backedge
  // belongs to the loop. This is a little bit more conservative than necessary
  // and might include blocks that in fact cannot reach the backedge, but it
  // makes dealing with exception blocks easier (because they have no explicit
  // predecessors in Maglev).
  void FindLoopBody(maglev::BlockConstReverseIterator it) {
    const maglev::BasicBlock* header = *it;
    DCHECK(header->is_loop());

    --it;  // Skipping the header, since we consider its loop header to be the
           // header of their outer loop (if any).

    const maglev::BasicBlock* backedge_block = header->backedge_predecessor();
    if (backedge_block == header) {
      // This is a 1-block loop. Since headers are part of the outer loop, we
      // have nothing to mark.
      return;
    }

    block_to_header_[backedge_block] = header;

    for (; *it != backedge_block; --it) {
      const maglev::BasicBlock* curr = *it;
      if (block_to_header_.contains(curr)) {
        // {curr} is part of an inner loop.
        continue;
      }
      block_to_header_[curr] = header;
    }
  }

  void FindLoopHeaderBypasses(maglev::Graph* graph) {
    // As mentioned earlier, Maglev graphs for resumable generator functions
    // always start with a main dispatch switch in the 3rd block:
    //
    //
    //                       + 1 -----------------+
    //                       | InitialValues...   |
    //                       | Jump               |
    //                       +--------------------+
    //                                  |
    //                                  |
    //                                  v
    //                       + 2 --------------------+
    //                       | BranchIfRootConstant  |
    //                       +-----------------------+
    //                          /                  \
    //                         /                     \
    //                        /                        \
    //                       /                           \
    //                      v                              v
    //              + 3 ----------+                  + 4 --------------+
    //              | Load state  |                  | Initial setup   |
    //              | Switch      |                  | return          |
    //              +-------------+                  +-----------------+
    //                /    |    \
    //               /     |     \
    //              v      v      v
    //          Resuming in various places
    //
    //
    //
    // In order to find loop header bypasses, we are looking for cases where
    // the destination of the dispatch switch (= the successors of block 3) are
    // inside a loop.

    constexpr int kGeneratorSwitchBLockIndex = 2;
    maglev::BasicBlock* generator_switch_block =
        graph->blocks()[kGeneratorSwitchBLockIndex];
    DCHECK(generator_switch_block->control_node()->Is<maglev::Switch>());

    for (maglev::BasicBlock* target : generator_switch_block->successors()) {
      const maglev::BasicBlock* innermost_header = GetLoopHeader(target);

      if (innermost_header) {
        // This case bypasses a loop header.
        RecordHeadersForBypass(target, innermost_header);
      }
    }
  }

  void RecordHeadersForBypass(maglev::BasicBlock* initial_target,
                              const maglev::BasicBlock* innermost_header) {
    block_to_innermost_bypassed_header_[initial_target] = innermost_header;
    bypassed_headers_.insert(innermost_header);

    for (const maglev::BasicBlock* outer_header =
             GetLoopHeader(innermost_header);
         outer_header; outer_header = GetLoopHeader(outer_header)) {
      bypassed_headers_.insert(outer_header);
    }
  }

  maglev::MaglevGraphLabeller* labeller_;

  // Map from blocks inside loops to the header of said loops.
  ZoneAbslFlatHashMap<const maglev::BasicBlock*, const maglev::BasicBlock*>
      block_to_header_;

  // Map from jump target to the innermost header they bypass.
  std::unordered_map<const maglev::BasicBlock*, const maglev::BasicBlock*>
      block_to_innermost_bypassed_header_;
  // Set of headers that are bypassed because of generator resumes.
  std::unordered_set<const maglev::BasicBlock*> bypassed_headers_;

  // {visit_queue_} is used in FindLoopBody to store nodes that still need to be
  // visited. It is a instance variable in order to reuse its memory more
  // efficiently.
  ZoneVector<const maglev::BasicBlock*> visit_queue_;
};

#define GET_FRAME_STATE_MAYBE_ABORT(name, deopt_info)                       \
  V<FrameState> name;                                                       \
  {                                                                         \
    OptionalV<FrameState> _maybe_frame_state = BuildFrameState(deopt_info); \
    if (!_maybe_frame_state.has_value()) {                                  \
      DCHECK(bailout_->has_value());                                        \
      return maglev::ProcessResult::kAbort;                                 \
    }                                                                       \
    name = _maybe_frame_state.value();                                      \
  }

// Turboshaft's MachineOptimizationReducer will sometimes detect that the
// condition for a DeoptimizeIf is always true, and replace it with an
// unconditional Deoptimize. When this happens, the assembler doesn't emit
// anything until the next reachable block is bound, which can lead to some
// Variable or OpIndex being Invalid, which can break some assumptions. To avoid
// this, the RETURN_IF_UNREACHABLE macro can be used to early-return.
#define RETURN_IF_UNREACHABLE()                 \
  if (__ generating_unreachable_operations()) { \
    return maglev::ProcessResult::kContinue;    \
  }

// TODO(dmercadier): LazyDeoptOnThrow is currently not very cleanly dealt with.
// In Maglev, it is a property of the ExceptionHandlerInfo, which is use by all
// throwing nodes and is created in a single place
// (MaglevGraphBuilder::AttachExceptionHandlerInfo). However, during the
// translation, we create different kind of calls from different places (Call,
// CallBuiltin_XXX, CallRuntime_XXX), and non-call nodes can also
// LazyDeoptOnThrow (such as GenericBinop) and we always have to manually
// remember to pass ShouldLazyDeoptOnThrow, which is easy to forget, which can
// then easily lead to bugs. A few ideas come to mind:
//
//  - Make ShouldLazyDeoptOnThrow non-optional on all throwing nodes. This is a
//    bit verbose, but at least we won't forget it.
//
//  - Make ThrowingScope automatically annotate all throwing nodes that are
//    emitted while the scope is active. The Assembler would be doing most of
//    the work: it would have a "LazyDeoptOnThrowScope" or something similar,
//    and any throwing node emitted during this scope would have the
//    LazyDeoptOnThrow property added as needed. All throwing nodes have a
//    {lazy_deopt_on_throw} field defined by THROWING_OP_BOILERPLATE (except
//    calls, but we could add it), so it shouldn't be very hard for the
//    Assembler to deal with this in a unified way.
//    The downside of this approach is that the interaction between this and
//    {current_catch_block} (in particular with nested scopes) might introduce
//    even more complexity and magic in the assembler.

class GraphBuilder {
 public:
  using AssemblerT =
      TSAssembler<BlockOriginTrackingReducer, MaglevEarlyLoweringReducer,
                  MachineOptimizationReducer, VariableReducer,
                  RequiredOptimizationReducer, ValueNumberingReducer>;

  GraphBuilder(PipelineData* data, Graph& graph, Zone* temp_zone,
               maglev::MaglevCompilationUnit* maglev_compilation_unit,
               std::optional<BailoutReason>* bailout)
      : data_(data),
        temp_zone_(temp_zone),
        assembler_(data, graph, graph, temp_zone),
        maglev_compilation_unit_(maglev_compilation_unit),
        node_mapping_(temp_zone),
        block_mapping_(temp_zone),
        regs_to_vars_(temp_zone),
        loop_single_edge_predecessors_(temp_zone),
        maglev_representations_(temp_zone),
        generator_analyzer_(temp_zone,
                            maglev_compilation_unit_->graph_labeller()),
        bailout_(bailout) {}

  void PreProcessGraph(maglev::Graph* graph) {
    for (maglev::BasicBlock* block : *graph) {
      block_mapping_[block] =
          block->is_loop() ? __ NewLoopHeader() : __ NewBlock();
    }
    // Constants are not in a block in Maglev but are in Turboshaft. We bind a
    // block now, so that Constants can then be emitted.
    __ Bind(__ NewBlock());

    if (maglev_compilation_unit_->bytecode()
            .incoming_new_target_or_generator_register()
            .is_valid()) {
      // The Maglev graph might contain a RegisterInput for
      // kJavaScriptCallNewTargetRegister later in the graph, which in
      // Turboshaft is represented as a Parameter. We create this Parameter
      // here, because the Instruction Selector tends to be unhappy when
      // Parameters are defined late in the graph.
      int new_target_index = Linkage::GetJSCallNewTargetParamIndex(
          maglev_compilation_unit_->parameter_count());
      new_target_param_ = __ Parameter(
          new_target_index, RegisterRepresentation::Tagged(), "%new.target");
    }

    if (graph->has_resumable_generator()) {
      generator_analyzer_.Analyze(graph);

      dummy_object_input_ = __ SmiConstant(0);
      dummy_word32_input_ = __ Word32Constant(0);
      dummy_float64_input_ = __ Float64Constant(0);

      header_switch_input_ = __ NewVariable(RegisterRepresentation::Word32());
      loop_default_generator_value_ = __ Word32Constant(kDefaultSwitchVarValue);
      generator_context_ =
          __ NewLoopInvariantVariable(RegisterRepresentation::Tagged());
      __ SetVariable(generator_context_, __ NoContextConstant());
    }

    // Maglev nodes often don't have the NativeContext as input, but instead
    // rely on the MaglevAssembler to provide it during code generation, unlike
    // Turboshaft nodes, which need the NativeContext as an explicit input if
    // they use it. We thus emit a single NativeContext constant here, which we
    // reuse later to construct Turboshaft nodes.
    native_context_ =
        __ HeapConstant(broker_->target_native_context().object());
  }

  void PostProcessGraph(maglev::Graph* graph) {
    // It can happen that some Maglev loops don't actually loop (the backedge
    // isn't actually reachable). We can't know this when emitting the header in
    // Turboshaft, which means that we still emit the header, but then we never
    // come around to calling FixLoopPhis on it. So, once we've generated the
    // whole Turboshaft graph, we go over all loop headers, and if some turn out
    // to not be headers, we turn them into regular merge blocks (and patch
    // their PendingLoopPhis).
    for (Block& block : __ output_graph().blocks()) {
      if (block.IsLoop() && block.PredecessorCount() == 1) {
        __ output_graph().TurnLoopIntoMerge(&block);
      }
    }
  }

  // The Maglev graph for resumable generator functions always has the main
  // dispatch Switch in its 3rd block.
  bool IsMaglevMainGeneratorSwitchBlock(
      const maglev::BasicBlock* maglev_block) {
    if (!generator_analyzer_.has_header_bypasses()) return false;
    constexpr int kMainSwitchBlockId = 3;
    bool is_main_switch_block =
        maglev_compilation_unit_->graph_labeller()->BlockId(maglev_block) ==
        kMainSwitchBlockId;
    DCHECK_IMPLIES(is_main_switch_block,
                   maglev_block->control_node()->Is<maglev::Switch>());
    return is_main_switch_block;
  }

  maglev::BlockProcessResult PreProcessBasicBlock(
      maglev::BasicBlock* maglev_block) {
    // Note that it's important to call SetMaglevInputBlock before calling Bind,
    // so that BlockOriginTrackingReducer::Bind records the correct predecessor
    // for the current block.
    __ SetMaglevInputBlock(maglev_block);

    is_visiting_generator_main_switch_ =
        IsMaglevMainGeneratorSwitchBlock(maglev_block);

    Block* turboshaft_block = Map(maglev_block);

    if (__ current_block() != nullptr) {
      // The first block for Constants doesn't end with a Jump, so we add one
      // now.
      __ Goto(turboshaft_block);
    }

#ifdef DEBUG
    loop_phis_first_input_.clear();
    loop_phis_first_input_index_ = -1;
    catch_block_begin_ = V<Object>::Invalid();
#endif

    if (maglev_block->is_loop() &&
        (loop_single_edge_predecessors_.contains(maglev_block) ||
         pre_loop_generator_blocks_.contains(maglev_block))) {
      EmitLoopSinglePredecessorBlock(maglev_block);
    }

    if (maglev_block->is_exception_handler_block()) {
      StartExceptionBlock(maglev_block);
      return maglev::BlockProcessResult::kContinue;
    }

    // SetMaglevInputBlock should have been called before calling Bind, and the
    // current `maglev_input_block` should thus already be `maglev_block`.
    DCHECK_EQ(__ maglev_input_block(), maglev_block);
    if (!__ Bind(turboshaft_block)) {
      // The current block is not reachable.
      return maglev::BlockProcessResult::kContinue;
    }

    if (maglev_block->is_loop()) {
      // The "permutation" stuff that comes afterwards in this function doesn't
      // apply to loops, since loops always have 2 predecessors in Turboshaft,
      // and in both Turboshaft and Maglev, the backedge is always the last
      // predecessors, so we never need to reorder phi inputs.
      return maglev::BlockProcessResult::kContinue;
    } else if (maglev_block->is_exception_handler_block()) {
      // We need to emit the CatchBlockBegin at the begining of this block. Note
      // that if this block has multiple predecessors (because multiple throwing
      // operations are caught by the same catch handler), then edge splitting
      // will have already created CatchBlockBegin operations in the
      // predecessors, and calling `__ CatchBlockBegin` now will actually only
      // emit a Phi of the CatchBlockBegin of the predecessors (which is exactly
      // what we want). See the comment above CatchBlockBegin in
      // TurboshaftAssemblerOpInterface.
      catch_block_begin_ = __ CatchBlockBegin();
    }

    // Because of edge splitting in Maglev (which happens on Bind rather than on
    // Goto), predecessors in the Maglev graph are not always ordered by their
    // position in the graph (ie, block 4 could be the second predecessor and
    // block 5 the first one). However, since we're processing the graph "in
    // order" (because that's how the maglev GraphProcessor works), predecessors
    // in the Turboshaft graph will be ordered by their position in the graph.
    // Additionally, optimizations during the translation (like constant folding
    // by MachineOptimizationReducer) could change control flow and remove
    // predecessors (by changing a Branch into a Goto for instance).
    // We thus compute in {predecessor_permutation_} a map from Maglev
    // predecessor index to Turboshaft predecessor index, and we'll use this
    // later when emitting Phis to reorder their inputs.
    predecessor_permutation_.clear();
    if (maglev_block->has_phi() &&
        // We ignore this for exception phis since they have no inputs in Maglev
        // anyways, and in Turboshaft we rely on {regs_to_vars_} to populate
        // their inputs (and also, Maglev exception blocks have no
        // predecessors).
        !maglev_block->is_exception_handler_block()) {
      ComputePredecessorPermutations(maglev_block, turboshaft_block, false,
                                     false);
    }
    return maglev::BlockProcessResult::kContinue;
  }

  void ComputePredecessorPermutations(maglev::BasicBlock* maglev_block,
                                      Block* turboshaft_block,
                                      bool skip_backedge,
                                      bool ignore_last_predecessor) {
    // This function is only called for loops that need a "single block
    // predecessor" (from EmitLoopSinglePredecessorBlock). The backedge should
    // always be skipped in thus cases. Additionally, this means that when
    // even when {maglev_block} is a loop, {turboshaft_block} shouldn't and
    // should instead be the new single forward predecessor of the loop.
    DCHECK_EQ(skip_backedge, maglev_block->is_loop());
    DCHECK(!turboshaft_block->IsLoop());

    DCHECK(maglev_block->has_phi());
    DCHECK(turboshaft_block->IsBound());
    DCHECK_EQ(__ current_block(), turboshaft_block);

    // Collecting the Maglev predecessors.
    base::SmallVector<const maglev::BasicBlock*, 16> maglev_predecessors;
    maglev_predecessors.resize_no_init(maglev_block->predecessor_count());
    for (int i = 0; i < maglev_block->predecessor_count() - skip_backedge;
         ++i) {
      maglev_predecessors[i] = maglev_block->predecessor_at(i);
    }

    predecessor_permutation_.clear();
    predecessor_permutation_.resize_and_init(maglev_block->predecessor_count(),
                                             Block::kInvalidPredecessorIndex);
    int index = turboshaft_block->PredecessorCount() - 1;
    // Iterating predecessors from the end (because it's simpler and more
    // efficient in Turboshaft).
    for (const Block* pred : turboshaft_block->PredecessorsIterable()) {
      if (ignore_last_predecessor &&
          index == turboshaft_block->PredecessorCount() - 1) {
        // When generator resumes bypass loop headers, we add an additional
        // predecessor to the header's predecessor (called {pred_for_generator}
        // in EmitLoopSinglePredecessorBlock). This block doesn't have Maglev
        // origin, we thus have to skip it here. To compensate,
        // MakePhiMaybePermuteInputs will take an additional input for these
        // cases.
        index--;
        continue;
      }
      // Finding out to which Maglev predecessor {pred} corresponds.
      const maglev::BasicBlock* orig = __ GetMaglevOrigin(pred);
      auto orig_index = *base::index_of(maglev_predecessors, orig);

      predecessor_permutation_[orig_index] = index;
      index--;
    }
    DCHECK_EQ(index, -1);
  }

  // Exceptions Phis are a bit special in Maglev: they have no predecessors, and
  // get populated on Throw based on values in the FrameState, which can be raw
  // Int32/Float64. However, they are always Tagged, which means that retagging
  // happens when they are populated. This can lead to exception Phis having a
  // mix of tagged and untagged predecessors (the latter would be automatically
  // retagged). When this happens, we need to manually retag all of the
  // predecessors of the exception Phis. To do so:
  //
  //   - If {block} has a single predecessor, it means that it won't have
  //     exception "phis" per se, but just values that have to retag.
  //
  //   - If {block} has multiple predecessors, then we need to do the retagging
  //     in the predecessors. It's a bit annoying because we've already bound
  //     and finalized all of the predecessors by now. So, we create new
  //     predecessor blocks in which we insert the taggings, patch the old
  //     predecessors to point to the new ones, and update the predecessors of
  //     {block}.
  void StartExceptionBlock(maglev::BasicBlock* maglev_catch_handler) {
    Block* turboshaft_catch_handler = Map(maglev_catch_handler);
    if (turboshaft_catch_handler->PredecessorCount() == 0) {
      // Some Assembler optimizations made this catch handler not be actually
      // reachable.
      return;
    }
    if (turboshaft_catch_handler->PredecessorCount() == 1) {
      StartSinglePredecessorExceptionBlock(maglev_catch_handler,
                                           turboshaft_catch_handler);
    } else {
      StartMultiPredecessorExceptionBlock(maglev_catch_handler,
                                          turboshaft_catch_handler);
    }
  }
  void StartSinglePredecessorExceptionBlock(
      maglev::BasicBlock* maglev_catch_handler,
      Block* turboshaft_catch_handler) {
    if (!__ Bind(turboshaft_catch_handler)) return;
    catch_block_begin_ = __ CatchBlockBegin();
    if (!maglev_catch_handler->has_phi()) return;
    InsertTaggingForPhis(maglev_catch_handler);
  }
  // InsertTaggingForPhis makes sure that all of the inputs of the exception
  // phis of {maglev_catch_handler} are tagged. If some aren't tagged, it
  // inserts a tagging node in the current block and updates the corresponding
  // Variable.
  void InsertTaggingForPhis(maglev::BasicBlock* maglev_catch_handler) {
    DCHECK(maglev_catch_handler->has_phi());

    IterCatchHandlerPhis(maglev_catch_handler, [&](interpreter::Register owner,
                                                   Variable var) {
      DCHECK_NE(owner, interpreter::Register::virtual_accumulator());
      V<Any> ts_idx = __ GetVariable(var);
      DCHECK(maglev_representations_.contains(ts_idx));
      switch (maglev_representations_[ts_idx]) {
        case maglev::ValueRepresentation::kTagged:
          // Already tagged, nothing to do.
          break;
        case maglev::ValueRepresentation::kInt32:
          __ SetVariable(var, __ ConvertInt32ToNumber(V<Word32>::Cast(ts_idx)));
          break;
        case maglev::ValueRepresentation::kUint32:
          __ SetVariable(var,
                         __ ConvertUint32ToNumber(V<Word32>::Cast(ts_idx)));
          break;
        case maglev::ValueRepresentation::kFloat64:
          __ SetVariable(
              var,
              Float64ToTagged(
                  V<Float64>::Cast(ts_idx),
                  maglev::Float64ToTagged::ConversionMode::kCanonicalizeSmi));
          break;
        case maglev::ValueRepresentation::kHoleyFloat64:
          __ SetVariable(
              var, HoleyFloat64ToTagged(V<Float64>::Cast(ts_idx),
                                        maglev::HoleyFloat64ToTagged::
                                            ConversionMode::kCanonicalizeSmi));
          break;
        case maglev::ValueRepresentation::kIntPtr:
          UNREACHABLE();
      }
    });
  }
  void StartMultiPredecessorExceptionBlock(
      maglev::BasicBlock* maglev_catch_handler,
      Block* turboshaft_catch_handler) {
    if (!maglev_catch_handler->has_phi()) {
      // The very simple case: the catch handler didn't have any Phis, we don't
      // have to do anything complex.
      if (!__ Bind(turboshaft_catch_handler)) return;
      catch_block_begin_ = __ CatchBlockBegin();
      return;
    }

    // Inserting the tagging in all of the predecessors.
    auto predecessors = turboshaft_catch_handler->Predecessors();
    turboshaft_catch_handler->ResetAllPredecessors();
    base::SmallVector<V<Object>, 16> catch_block_begins;
    for (Block* predecessor : predecessors) {
      // Recording the CatchBlockBegin of this predecessor.
      V<Object> catch_begin = predecessor->begin();
      DCHECK(Asm().Get(catch_begin).template Is<CatchBlockBeginOp>());
      catch_block_begins.push_back(catch_begin);

      TagExceptionPhiInputsForBlock(predecessor, maglev_catch_handler,
                                    turboshaft_catch_handler);
    }

    // Finally binding the catch handler.
    __ Bind(turboshaft_catch_handler);

    // We now need to insert a Phi for the CatchBlockBegins of the
    // predecessors (usually, we would just call `__ CatchBlockbegin`, which
    // takes care of creating a Phi node if necessary, but this won't work here,
    // because this mechanisms expects the CatchBlockBegin to be the 1st
    // instruction of the predecessors, and it isn't the case since the
    // predecessors are now the blocks with the tagging).
    catch_block_begin_ = __ Phi(base::VectorOf(catch_block_begins));
  }
  void TagExceptionPhiInputsForBlock(Block* old_block,
                                     maglev::BasicBlock* maglev_catch_handler,
                                     Block* turboshaft_catch_handler) {
    DCHECK(maglev_catch_handler->has_phi());

    // We start by patching in-place the predecessors final Goto of {old_block}
    // to jump to a new block (in which we'll insert the tagging).
    Block* new_block = __ NewBlock();
    const GotoOp& old_goto =
        old_block->LastOperation(__ output_graph()).Cast<GotoOp>();
    DCHECK_EQ(old_goto.destination, turboshaft_catch_handler);
    __ output_graph().Replace<GotoOp>(__ output_graph().Index(old_goto),
                                      new_block, /* is_backedge */ false);
    __ AddPredecessor(old_block, new_block, false);

    // Now, we bind the new block and insert the taggings
    __ BindReachable(new_block);
    InsertTaggingForPhis(maglev_catch_handler);

    // Finally, we just go from this block to the catch handler.
    __ Goto(turboshaft_catch_handler);
  }

  void EmitLoopSinglePredecessorBlock(maglev::BasicBlock* maglev_loop_header) {
    DCHECK(maglev_loop_header->is_loop());

    bool has_special_generator_handling = false;
    V<Word32> switch_var_first_input;
    if (pre_loop_generator_blocks_.contains(maglev_loop_header)) {
      // This loop header used to be bypassed by generator resume edges. It will
      // now act as a secondary switch for the generator resumes.
      std::vector<GeneratorSplitEdge>& generator_preds =
          pre_loop_generator_blocks_[maglev_loop_header];
      // {generator_preds} contains all of the edges that were bypassing this
      // loop header. Rather than adding that many predecessors to the loop
      // header, will create a single predecessor, {pred_for_generator}, to
      // which all of the edges of {generator_preds} will go.
      Block* pred_for_generator = __ NewBlock();

      for (GeneratorSplitEdge pred : generator_preds) {
        __ Bind(pred.pre_loop_dst);
        __ SetVariable(header_switch_input_,
                       __ Word32Constant(pred.switch_value));
        __ Goto(pred_for_generator);
      }

      __ Bind(pred_for_generator);
      switch_var_first_input = __ GetVariable(header_switch_input_);
      DCHECK(switch_var_first_input.valid());

      BuildJump(maglev_loop_header);

      has_special_generator_handling = true;
      on_generator_switch_loop_ = true;
    }

    DCHECK(loop_single_edge_predecessors_.contains(maglev_loop_header));
    Block* loop_pred = loop_single_edge_predecessors_[maglev_loop_header];
    __ Bind(loop_pred);

    if (maglev_loop_header->has_phi()) {
      ComputePredecessorPermutations(maglev_loop_header, loop_pred, true,
                                     has_special_generator_handling);

      // Now we need to emit Phis (one per loop phi in {block}, which should
      // contain the same input except for the backedge).
      loop_phis_first_input_.clear();
      loop_phis_first_input_index_ = 0;
      for (maglev::Phi* phi : *maglev_loop_header->phis()) {
        constexpr int kSkipBackedge = 1;
        int input_count = phi->input_count() - kSkipBackedge;

        if (has_special_generator_handling) {
          // Adding an input to the Phis to account for the additional
          // generator-related predecessor.
          V<Any> additional_input;
          switch (phi->value_representation()) {
            case maglev::ValueRepresentation::kTagged:
              additional_input = dummy_object_input_;
              break;
            case maglev::ValueRepresentation::kInt32:
            case maglev::ValueRepresentation::kUint32:
              additional_input = dummy_word32_input_;
              break;
            case maglev::ValueRepresentation::kFloat64:
            case maglev::ValueRepresentation::kHoleyFloat64:
              additional_input = dummy_float64_input_;
              break;
            case maglev::ValueRepresentation::kIntPtr:
              // Maglev doesn't have IntPtr Phis.
              UNREACHABLE();
          }
          loop_phis_first_input_.push_back(
              MakePhiMaybePermuteInputs(phi, input_count, additional_input));
        } else {
          loop_phis_first_input_.push_back(
              MakePhiMaybePermuteInputs(phi, input_count));
        }
      }
    }

    if (has_special_generator_handling) {
      // We now emit the Phi that will be used in the loop's main switch.
      base::SmallVector<OpIndex, 16> inputs;
      constexpr int kSkipGeneratorPredecessor = 1;

      // We insert a default input for all of the non-generator predecessor.
      int input_count_without_generator =
          loop_pred->PredecessorCount() - kSkipGeneratorPredecessor;
      DCHECK(loop_default_generator_value_.valid());
      inputs.insert(inputs.begin(), input_count_without_generator,
                    loop_default_generator_value_);

      // And we insert the "true" input for the generator predecessor (which is
      // {pred_for_generator} above).
      DCHECK(switch_var_first_input.valid());
      inputs.push_back(switch_var_first_input);

      __ SetVariable(
          header_switch_input_,
          __ Phi(base::VectorOf(inputs), RegisterRepresentation::Word32()));
    }

    // Actually jumping to the loop.
    __ Goto(Map(maglev_loop_header));
  }

  void PostPhiProcessing() {
    // Loop headers that are bypassed because of generators need to be turned
    // into secondary generator switches (so as to not be bypassed anymore).
    // Concretely, we split the loop headers in half by inserting a Switch right
    // after the loop phis have been emitted. Here is a visual representation of
    // what's happening:
    //
    // Before:
    //
    //              |         ----------------------------
    //              |         |                          |
    //              |         |                          |
    //              v         v                          |
    //      +------------------------+                   |
    //      | phi_1(...)             |                   |
    //      | ...                    |                   |
    //      | phi_k(...)             |                   |
    //      | <some op 1>            |                   |
    //      | ...                    |                   |
    //      | <some op n>            |                   |
    //      | Branch                 |                   |
    //      +------------------------+                   |
    //                 |                                 |
    //                 |                                 |
    //                 v                                 |
    //
    //
    // After:
    //
    //
    //              |         -----------------------------------
    //              |         |                                 |
    //              |         |                                 |
    //              v         v                                 |
    //      +------------------------+                          |
    //      | phi_1(...)             |                          |
    //      | ...                    |                          |
    //      | phi_k(...)             |                          |
    //      | Switch                 |                          |
    //      +------------------------+                          |
    //        /   |     |      \                                |
    //       /    |     |       \                               |
    //      /     |     |        \                              |
    //     v      v     v         v                             |
    //                        +------------------+              |
    //                        | <some op 1>      |              |
    //                        | ...              |              |
    //                        | <some op n>      |              |
    //                        | Branch           |              |
    //                        +------------------+              |
    //                                 |                        |
    //                                 |                        |
    //                                 v                        |
    //
    //
    // Since `PostPhiProcessing` is called right after all phis have been
    // emitted, now is thus the time to split the loop header.

    if (on_generator_switch_loop_) {
      const maglev::BasicBlock* maglev_loop_header = __ maglev_input_block();
      DCHECK(maglev_loop_header->is_loop());
      std::vector<GeneratorSplitEdge>& generator_preds =
          pre_loop_generator_blocks_[maglev_loop_header];

      compiler::turboshaft::SwitchOp::Case* cases =
          __ output_graph().graph_zone()
              -> AllocateArray<compiler::turboshaft::SwitchOp::Case>(
                               generator_preds.size());

      for (int i = 0; static_cast<unsigned int>(i) < generator_preds.size();
           i++) {
        GeneratorSplitEdge pred = generator_preds[i];
        cases[i] = {pred.switch_value, pred.inside_loop_target,
                    BranchHint::kNone};
      }
      Block* default_block = __ NewBlock();
      __ Switch(__ GetVariable(header_switch_input_),
                base::VectorOf(cases, generator_preds.size()), default_block);

      // We now bind {default_block}. It will contain the rest of the loop
      // header. The MaglevGraphProcessor will continue to visit the header's
      // body as if nothing happened.
      __ Bind(default_block);
    }
    on_generator_switch_loop_ = false;
  }

  maglev::ProcessResult Process(maglev::Constant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ HeapConstant(node->object().object()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::RootConstant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ HeapConstant(MakeRef(broker_, node->DoReify(local_isolate_))
                                     .AsHeapObject()
                                     .object()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32Constant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Word32Constant(node->value()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Uint32Constant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Word32Constant(node->value()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64Constant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Float64Constant(node->value()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::SmiConstant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ SmiConstant(node->value()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TaggedIndexConstant* node,
                                const maglev::ProcessingState& state) {
    // TODO(dmercadier): should this really be a SmiConstant, or rather a
    // Word32Constant?
    SetMap(node, __ SmiConstant(node->value().ptr()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TrustedConstant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ TrustedHeapConstant(node->object().object()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::InitialValue* node,
                                const maglev::ProcessingState& state) {
    // TODO(dmercadier): InitialValues are much simpler in Maglev because they
    // are mapped directly to interpreter registers, whereas Turbofan changes
    // the indices, making everything more complex. We should try to have the
    // same InitialValues in Turboshaft as in Maglev, in order to simplify
    // things.
#ifdef DEBUG
    // We cannot use strdup or something that simple for {debug_name}, because
    // it has to be zone allocated rather than heap-allocated, since it won't be
    // freed and this would thus cause a leak.
    std::string reg_string_name = node->source().ToString();
    base::Vector<char> debug_name_arr =
        graph_zone()->NewVector<char>(reg_string_name.length() + /* \n */ 1);
    strcpy(debug_name_arr.data(), reg_string_name.c_str());
    char* debug_name = debug_name_arr.data();
#else
    char* debug_name = nullptr;
#endif
    interpreter::Register source = node->source();
    V<Object> value;
    if (source.is_function_closure()) {
      // The function closure is a Parameter rather than an OsrValue even when
      // OSR-compiling.
      value = __ Parameter(Linkage::kJSCallClosureParamIndex,
                           RegisterRepresentation::Tagged(), debug_name);
    } else if (maglev_compilation_unit_->is_osr()) {
      int index;
      if (source.is_current_context()) {
        index = Linkage::kOsrContextSpillSlotIndex;
      } else if (source == interpreter::Register::virtual_accumulator()) {
        index = Linkage::kOsrAccumulatorRegisterIndex;
      } else if (source.is_parameter()) {
        index = source.ToParameterIndex();
      } else {
        // For registers, recreate the index computed by FillWithOsrValues in
        // BytecodeGraphBuilder.
        index = source.index() + InterpreterFrameConstants::kExtraSlotCount +
                maglev_compilation_unit_->parameter_count();
      }
      value = __ OsrValue(index);
    } else {
      int index = source.ToParameterIndex();
      if (source.is_current_context()) {
        index = Linkage::GetJSCallContextParamIndex(
            maglev_compilation_unit_->parameter_count());
      } else {
        index = source.ToParameterIndex();
      }
      value = __ Parameter(index, RegisterRepresentation::Tagged(), debug_name);
    }
    SetMap(node, value);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::RegisterInput* node,
                                const maglev::ProcessingState& state) {
    DCHECK(maglev_compilation_unit_->bytecode()
               .incoming_new_target_or_generator_register()
               .is_valid());
    DCHECK_EQ(node->input(), kJavaScriptCallNewTargetRegister);
    DCHECK(new_target_param_.valid());
    SetMap(node, new_target_param_);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::FunctionEntryStackCheck* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    __ JSFunctionEntryStackCheck(native_context(), frame_state);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Phi* node,
                                const maglev::ProcessingState& state) {
    int input_count = node->input_count();
    RegisterRepresentation rep =
        RegisterRepresentationFor(node->value_representation());
    if (node->is_exception_phi()) {
      if (node->owner() == interpreter::Register::virtual_accumulator()) {
        DCHECK(catch_block_begin_.valid());
        SetMap(node, catch_block_begin_);
      } else {
        Variable var = regs_to_vars_[node->owner().index()];
        SetMap(node, __ GetVariable(var));
        // {var} won't be used anymore once we've created the mapping from
        // {node} to its value. We thus reset it, in order to avoid Phis being
        // created for {var} at later merge points.
        __ SetVariable(var, V<Object>::Invalid());
      }
      return maglev::ProcessResult::kContinue;
    }
    if (__ current_block()->IsLoop()) {
      DCHECK(state.block()->is_loop());
      OpIndex first_phi_input;
      if (state.block()->predecessor_count() > 2 ||
          generator_analyzer_.HeaderIsBypassed(state.block())) {
        // This loop has multiple forward edge in Maglev, so we should have
        // created an intermediate block in Turboshaft, which will be the only
        // predecessor of the Turboshaft loop, and from which we'll find the
        // first input for this loop phi.
        DCHECK_EQ(loop_phis_first_input_.size(),
                  static_cast<size_t>(state.block()->phis()->LengthForTest()));
        DCHECK_GE(loop_phis_first_input_index_, 0);
        DCHECK_LT(loop_phis_first_input_index_, loop_phis_first_input_.size());
        DCHECK(loop_single_edge_predecessors_.contains(state.block()));
        DCHECK_EQ(loop_single_edge_predecessors_[state.block()],
                  __ current_block()->LastPredecessor());
        first_phi_input = loop_phis_first_input_[loop_phis_first_input_index_];
        loop_phis_first_input_index_++;
      } else {
        DCHECK_EQ(input_count, 2);
        DCHECK_EQ(state.block()->predecessor_count(), 2);
        DCHECK(loop_phis_first_input_.empty());
        first_phi_input = Map(node->input(0));
      }
      SetMap(node, __ PendingLoopPhi(first_phi_input, rep));
    } else {
      SetMap(node, MakePhiMaybePermuteInputs(node, input_count));
    }
    return maglev::ProcessResult::kContinue;
  }

  V<Any> MakePhiMaybePermuteInputs(
      maglev::ValueNode* maglev_node, int maglev_input_count,
      OptionalV<Any> additional_input = OptionalV<Any>::Nullopt()) {
    DCHECK(!predecessor_permutation_.empty());

    base::SmallVector<OpIndex, 16> inputs;
    // Note that it's important to use `current_block()->PredecessorCount()` as
    // the size of {inputs}, because some Maglev predecessors could have been
    // dropped by Turboshaft during the translation (and thus, `input_count`
    // might be too much).
    inputs.resize_and_init(__ current_block()->PredecessorCount());
    for (int i = 0; i < maglev_input_count; ++i) {
      if (predecessor_permutation_[i] != Block::kInvalidPredecessorIndex) {
        inputs[predecessor_permutation_[i]] = Map(maglev_node->input(i));
      }
    }

    if (additional_input.has_value()) {
      // When a loop header was bypassed by a generator resume, we insert an
      // additional predecessor to the loop, and thus need an additional input
      // for the Phis.
      inputs[inputs.size() - 1] = additional_input.value();
    }

    return __ Phi(
        base::VectorOf(inputs),
        RegisterRepresentationFor(maglev_node->value_representation()));
  }

  maglev::ProcessResult Process(maglev::Call* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    V<Object> function = Map(node->function());
    V<Context> context = Map(node->context());

    Builtin builtin;
    switch (node->target_type()) {
      case maglev::Call::TargetType::kAny:
        switch (node->receiver_mode()) {
          case ConvertReceiverMode::kNullOrUndefined:
            builtin = Builtin::kCall_ReceiverIsNullOrUndefined;
            break;
          case ConvertReceiverMode::kNotNullOrUndefined:
            builtin = Builtin::kCall_ReceiverIsNotNullOrUndefined;
            break;
          case ConvertReceiverMode::kAny:
            builtin = Builtin::kCall_ReceiverIsAny;
            break;
        }
        break;
      case maglev::Call::TargetType::kJSFunction:
        switch (node->receiver_mode()) {
          case ConvertReceiverMode::kNullOrUndefined:
            builtin = Builtin::kCallFunction_ReceiverIsNullOrUndefined;
            break;
          case ConvertReceiverMode::kNotNullOrUndefined:
            builtin = Builtin::kCallFunction_ReceiverIsNotNullOrUndefined;
            break;
          case ConvertReceiverMode::kAny:
            builtin = Builtin::kCallFunction_ReceiverIsAny;
            break;
        }
        break;
    }

    base::SmallVector<OpIndex, 16> arguments;
    arguments.push_back(function);
    arguments.push_back(__ Word32Constant(node->num_args()));
    for (auto arg : node->args()) {
      arguments.push_back(Map(arg));
    }
    arguments.push_back(context);

    SetMap(node,
           GenerateBuiltinCall(node, builtin, frame_state,
                               base::VectorOf(arguments), node->num_args()));

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CallKnownJSFunction* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    V<Object> callee = Map(node->closure());
    int actual_parameter_count = JSParameterCount(node->num_args());

    if (node->shared_function_info().HasBuiltinId()) {
      // Note that there is no need for a ThrowingScope here:
      // GenerateBuiltinCall takes care of creating one.
      base::SmallVector<OpIndex, 16> arguments;
      arguments.push_back(callee);
      arguments.push_back(Map(node->new_target()));
      arguments.push_back(__ Word32Constant(actual_parameter_count));
      arguments.push_back(Map(node->receiver()));
      for (int i = 0; i < node->num_args(); i++) {
        arguments.push_back(Map(node->arg(i)));
      }
      // Setting missing arguments to Undefined.
      for (int i = actual_parameter_count; i < node->expected_parameter_count();
           i++) {
        arguments.push_back(__ HeapConstant(local_factory_->undefined_value()));
      }
      arguments.push_back(Map(node->context()));
      SetMap(node, GenerateBuiltinCall(
                       node, node->shared_function_info().builtin_id(),
                       frame_state, base::VectorOf(arguments),
                       std::max<int>(actual_parameter_count,
                                     node->expected_parameter_count())));
    } else {
      ThrowingScope throwing_scope(this, node);
      base::SmallVector<OpIndex, 16> arguments;
      arguments.push_back(Map(node->receiver()));
      for (int i = 0; i < node->num_args(); i++) {
        arguments.push_back(Map(node->arg(i)));
      }
      // Setting missing arguments to Undefined.
      for (int i = actual_parameter_count; i < node->expected_parameter_count();
           i++) {
        arguments.push_back(__ HeapConstant(local_factory_->undefined_value()));
      }
      arguments.push_back(Map(node->new_target()));
      arguments.push_back(__ Word32Constant(actual_parameter_count));

      // Load the context from {callee}.
      OpIndex context =
          __ LoadField(callee, AccessBuilder::ForJSFunctionContext());
      arguments.push_back(context);

      const CallDescriptor* descriptor = Linkage::GetJSCallDescriptor(
          graph_zone(), false,
          std::max<int>(actual_parameter_count,
                        node->expected_parameter_count()),
          CallDescriptor::kNeedsFrameState | CallDescriptor::kCanUseRoots);

      LazyDeoptOnThrow lazy_deopt_on_throw = ShouldLazyDeoptOnThrow(node);

      SetMap(node, __ Call(V<CallTarget>::Cast(callee), frame_state,
                           base::VectorOf(arguments),
                           TSCallDescriptor::Create(descriptor, CanThrow::kYes,
                                                    lazy_deopt_on_throw,
                                                    graph_zone())));
    }

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CallKnownApiFunction* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    if (node->inline_builtin()) {
      DCHECK(v8_flags.maglev_inline_api_calls);
      // TODO(dmercadier, 40912714, 42203760): The flag maglev_inline_api_calls
      // is currently experimental, and it's not clear at this point if it will
      // even become non-experimental, so we currently don't support it in the
      // Maglev->Turboshaft translation. Note that a quick-fix would be to treat
      // kNoProfilingInlined like kNoProfiling, although this would be slower
      // than desired.
      UNIMPLEMENTED();
    }

    OpIndex api_holder;
    if (node->api_holder().has_value()) {
      api_holder = __ HeapConstant(node->api_holder().value().object());
    } else {
      api_holder = Map(node->receiver());
    }

    V<Object> target =
        __ HeapConstant(node->function_template_info().AsHeapObject().object());

    ApiFunction function(node->function_template_info().callback(broker_));
    ExternalReference function_ref = ExternalReference::Create(
        &function, ExternalReference::DIRECT_API_CALL);

    base::SmallVector<OpIndex, 16> arguments;
    arguments.push_back(__ ExternalConstant(function_ref));
    arguments.push_back(__ Word32Constant(node->num_args()));
    arguments.push_back(target);
    arguments.push_back(api_holder);
    arguments.push_back(Map(node->receiver()));
    for (maglev::Input arg : node->args()) {
      arguments.push_back(Map(arg));
    }
    arguments.push_back(Map(node->context()));

    Builtin builtin;
    switch (node->mode()) {
      case maglev::CallKnownApiFunction::Mode::kNoProfiling:
        builtin = Builtin::kCallApiCallbackOptimizedNoProfiling;
        break;
      case maglev::CallKnownApiFunction::Mode::kNoProfilingInlined:
        // Handled earlier when checking `node->inline_builtin()`.
        UNREACHABLE();
      case maglev::CallKnownApiFunction::Mode::kGeneric:
        builtin = Builtin::kCallApiCallbackOptimized;
        break;
    }

    int stack_arg_count = node->num_args() + /* implicit receiver */ 1;
    V<Any> result = GenerateBuiltinCall(
        node, builtin, frame_state, base::VectorOf(arguments), stack_arg_count);
    SetMap(node, result);

    return maglev::ProcessResult::kContinue;
  }
  V<Any> GenerateBuiltinCall(
      maglev::NodeBase* node, Builtin builtin,
      OptionalV<FrameState> frame_state, base::Vector<const OpIndex> arguments,
      std::optional<int> stack_arg_count = std::nullopt) {
    ThrowingScope throwing_scope(this, node);

    Callable callable = Builtins::CallableFor(isolate_, builtin);
    const CallInterfaceDescriptor& descriptor = callable.descriptor();
    CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
        graph_zone(), descriptor,
        stack_arg_count.has_value() ? stack_arg_count.value()
                                    : descriptor.GetStackParameterCount(),
        frame_state.valid() ? CallDescriptor::kNeedsFrameState
                            : CallDescriptor::kNoFlags);
    V<Code> stub_code = __ HeapConstant(callable.code());

    LazyDeoptOnThrow lazy_deopt_on_throw = ShouldLazyDeoptOnThrow(node);

    return __ Call(stub_code, frame_state, base::VectorOf(arguments),
                   TSCallDescriptor::Create(call_descriptor, CanThrow::kYes,
                                            lazy_deopt_on_throw, graph_zone()));
  }
  maglev::ProcessResult Process(maglev::CallBuiltin* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    base::SmallVector<OpIndex, 16> arguments;
    for (int i = 0; i < node->InputCountWithoutContext(); i++) {
      arguments.push_back(Map(node->input(i)));
    }

    if (node->has_feedback()) {
      V<Any> feedback_slot;
      switch (node->slot_type()) {
        case maglev::CallBuiltin::kTaggedIndex:
          feedback_slot = __ TaggedIndexConstant(node->feedback().index());
          break;
        case maglev::CallBuiltin::kSmi:
          feedback_slot = __ WordPtrConstant(node->feedback().index());
          break;
      }
      arguments.push_back(feedback_slot);
      arguments.push_back(__ HeapConstant(node->feedback().vector));
    }

    auto descriptor = Builtins::CallInterfaceDescriptorFor(node->builtin());
    if (descriptor.HasContextParameter()) {
      arguments.push_back(Map(node->context_input()));
    }

    int stack_arg_count =
        node->InputCountWithoutContext() - node->InputsInRegisterCount();
    if (node->has_feedback()) {
      // We might need to take the feedback slot and vector into account for
      // {stack_arg_count}. There are three possibilities:
      // 1. Feedback slot and vector are in register.
      // 2. Feedback slot is in register and vector is on stack.
      // 3. Feedback slot and vector are on stack.
      int slot_index = node->InputCountWithoutContext();
      int vector_index = slot_index + 1;
      if (vector_index < descriptor.GetRegisterParameterCount()) {
        // stack_arg_count is already correct.
      } else if (vector_index == descriptor.GetRegisterParameterCount()) {
        // feedback vector is on the stack
        stack_arg_count += 1;
      } else {
        // feedback slot and vector on the stack
        stack_arg_count += 2;
      }
    }

    V<Any> call_idx =
        GenerateBuiltinCall(node, node->builtin(), frame_state,
                            base::VectorOf(arguments), stack_arg_count);
    SetMapMaybeMultiReturn(node, call_idx);

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CallRuntime* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);
    LazyDeoptOnThrow lazy_deopt_on_throw = ShouldLazyDeoptOnThrow(node);

    auto c_entry_stub = __ CEntryStubConstant(isolate_, node->ReturnCount());

    CallDescriptor* call_descriptor = Linkage::GetRuntimeCallDescriptor(
        graph_zone(), node->function_id(), node->num_args(),
        Operator::kNoProperties, CallDescriptor::kNeedsFrameState,
        lazy_deopt_on_throw);

    base::SmallVector<OpIndex, 16> arguments;
    for (int i = 0; i < node->num_args(); i++) {
      arguments.push_back(Map(node->arg(i)));
    }

    arguments.push_back(
        __ ExternalConstant(ExternalReference::Create(node->function_id())));
    arguments.push_back(__ Word32Constant(node->num_args()));

    arguments.push_back(Map(node->context()));

    OptionalV<FrameState> frame_state = OptionalV<FrameState>::Nullopt();
    if (call_descriptor->NeedsFrameState()) {
      GET_FRAME_STATE_MAYBE_ABORT(frame_state_value, node->lazy_deopt_info());
      frame_state = frame_state_value;
    }
    DCHECK_IMPLIES(lazy_deopt_on_throw == LazyDeoptOnThrow::kYes,
                   frame_state.has_value());

    V<Any> call_idx =
        __ Call(c_entry_stub, frame_state, base::VectorOf(arguments),
                TSCallDescriptor::Create(call_descriptor, CanThrow::kYes,
                                         lazy_deopt_on_throw, graph_zone()));
    SetMapMaybeMultiReturn(node, call_idx);

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ThrowReferenceErrorIfHole* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    IF (UNLIKELY(RootEqual(node->value(), RootIndex::kTheHoleValue))) {
      GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
      __ CallRuntime_ThrowAccessedUninitializedVariable(
          isolate_, frame_state, native_context(), ShouldLazyDeoptOnThrow(node),
          __ HeapConstant(node->name().object()));
      // TODO(dmercadier): use RuntimeAbort here instead of Unreachable.
      // However, before doing so, RuntimeAbort should be changed so that 1)
      // it's a block terminator and 2) it doesn't call the runtime when
      // v8_flags.trap_on_abort is true.
      __ Unreachable();
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ThrowIfNotSuperConstructor* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    V<HeapObject> constructor = Map(node->constructor());
    V<i::Map> map = __ LoadMapField(constructor);
    static_assert(Map::kBitFieldOffsetEnd + 1 - Map::kBitFieldOffset == 1);
    V<Word32> bitfield =
        __ template LoadField<Word32>(map, AccessBuilder::ForMapBitField());
    IF_NOT (LIKELY(__ Word32BitwiseAnd(bitfield,
                                       Map::Bits1::IsConstructorBit::kMask))) {
      GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
      __ CallRuntime_ThrowNotSuperConstructor(
          isolate_, frame_state, native_context(), ShouldLazyDeoptOnThrow(node),
          constructor, Map(node->function()));
      // TODO(dmercadier): use RuntimeAbort here instead of Unreachable.
      // However, before doing so, RuntimeAbort should be changed so that 1)
      // it's a block terminator and 2) it doesn't call the runtime when
      // v8_flags.trap_on_abort is true.
      __ Unreachable();
    }

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ThrowSuperAlreadyCalledIfNotHole* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    IF_NOT (LIKELY(__ RootEqual(Map(node->value()), RootIndex::kTheHoleValue,
                                isolate_))) {
      GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
      __ CallRuntime_ThrowSuperAlreadyCalledError(isolate_, frame_state,
                                                  native_context(),
                                                  ShouldLazyDeoptOnThrow(node));
      // TODO(dmercadier): use RuntimeAbort here instead of Unreachable.
      // However, before doing so, RuntimeAbort should be changed so that 1)
      // it's a block terminator and 2) it doesn't call the runtime when
      // v8_flags.trap_on_abort is true.
      __ Unreachable();
    }

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ThrowSuperNotCalledIfHole* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    IF (UNLIKELY(__ RootEqual(Map(node->value()), RootIndex::kTheHoleValue,
                              isolate_))) {
      GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
      __ CallRuntime_ThrowSuperNotCalled(isolate_, frame_state,
                                         native_context(),
                                         ShouldLazyDeoptOnThrow(node));
      // TODO(dmercadier): use RuntimeAbort here instead of Unreachable.
      // However, before doing so, RuntimeAbort should be changed so that 1)
      // it's a block terminator and 2) it doesn't call the runtime when
      // v8_flags.trap_on_abort is true.
      __ Unreachable();
    }

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ThrowIfNotCallable* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    V<Object> value = Map(node->value());

    IF_NOT (LIKELY(__ ObjectIsCallable(value))) {
      GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
      __ CallRuntime_ThrowCalledNonCallable(
          isolate_, frame_state, native_context(), ShouldLazyDeoptOnThrow(node),
          value);
      // TODO(dmercadier): use RuntimeAbort here instead of Unreachable.
      // However, before doing so, RuntimeAbort should be changed so that 1)
      // it's a block terminator and 2) it doesn't call the runtime when
      // v8_flags.trap_on_abort is true.
      __ Unreachable();
    }

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CreateFunctionContext* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    V<Context> context = Map(node->context());
    V<ScopeInfo> scope_info = __ HeapConstant(node->scope_info().object());
    if (node->scope_type() == FUNCTION_SCOPE) {
      SetMap(node, __ CallBuiltin_FastNewFunctionContextFunction(
                       isolate_, frame_state, context, scope_info,
                       node->slot_count(), ShouldLazyDeoptOnThrow(node)));
    } else {
      DCHECK_EQ(node->scope_type(), EVAL_SCOPE);
      SetMap(node, __ CallBuiltin_FastNewFunctionContextEval(
                       isolate_, frame_state, context, scope_info,
                       node->slot_count(), ShouldLazyDeoptOnThrow(node)));
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::FastCreateClosure* node,
                                const maglev::ProcessingState& state) {
    NoThrowingScopeRequired no_throws(node);

    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    V<Context> context = Map(node->context());
    V<SharedFunctionInfo> shared_function_info =
        __ HeapConstant(node->shared_function_info().object());
    V<FeedbackCell> feedback_cell =
        __ HeapConstant(node->feedback_cell().object());

    SetMap(node,
           __ CallBuiltin_FastNewClosure(isolate_, frame_state, context,
                                         shared_function_info, feedback_cell));

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CreateClosure* node,
                                const maglev::ProcessingState& state) {
    NoThrowingScopeRequired no_throws(node);

    V<Context> context = Map(node->context());
    V<SharedFunctionInfo> shared_function_info =
        __ HeapConstant(node->shared_function_info().object());
    V<FeedbackCell> feedback_cell =
        __ HeapConstant(node->feedback_cell().object());

    V<JSFunction> closure;
    if (node->pretenured()) {
      closure = __ CallRuntime_NewClosure_Tenured(
          isolate_, context, shared_function_info, feedback_cell);
    } else {
      closure = __ CallRuntime_NewClosure(isolate_, context,
                                          shared_function_info, feedback_cell);
    }

    SetMap(node, closure);

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CallWithArrayLike* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    V<Context> context = Map(node->context());
    V<Object> function = Map(node->function());
    V<Object> receiver = Map(node->receiver());
    V<Object> arguments_list = Map(node->arguments_list());

    SetMap(node, __ CallBuiltin_CallWithArrayLike(
                     isolate_, graph_zone(), frame_state, context, receiver,
                     function, arguments_list, ShouldLazyDeoptOnThrow(node)));

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CallWithSpread* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    V<Context> context = Map(node->context());
    V<Object> function = Map(node->function());
    V<Object> spread = Map(node->spread());

    base::SmallVector<V<Object>, 16> arguments_no_spread;
    for (auto arg : node->args_no_spread()) {
      arguments_no_spread.push_back(Map(arg));
    }

    SetMap(node, __ CallBuiltin_CallWithSpread(
                     isolate_, graph_zone(), frame_state, context, function,
                     node->num_args_no_spread(), spread,
                     base::VectorOf(arguments_no_spread),
                     ShouldLazyDeoptOnThrow(node)));

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CallForwardVarargs* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    V<JSFunction> function = Map(node->function());
    V<Context> context = Map(node->context());

    base::SmallVector<V<Object>, 16> arguments;
    for (auto arg : node->args()) {
      arguments.push_back(Map(arg));
    }
    DCHECK_EQ(node->num_args(), arguments.size());

    Builtin builtin;
    switch (node->target_type()) {
      case maglev::Call::TargetType::kJSFunction:
        builtin = Builtin::kCallFunctionForwardVarargs;
        break;
      case maglev::Call::TargetType::kAny:
        builtin = Builtin::kCallForwardVarargs;
        break;
    }
    V<Object> call = __ CallBuiltin_CallForwardVarargs(
        isolate_, graph_zone(), builtin, frame_state, context, function,
        node->num_args(), node->start_index(), base::VectorOf(arguments),
        ShouldLazyDeoptOnThrow(node));

    SetMap(node, call);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Construct* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    base::SmallVector<OpIndex, 16> arguments;

    arguments.push_back(Map(node->function()));
    arguments.push_back(Map(node->new_target()));
    arguments.push_back(__ Word32Constant(node->num_args()));

    for (auto arg : node->args()) {
      arguments.push_back(Map(arg));
    }

    arguments.push_back(Map(node->context()));

    SetMap(node,
           GenerateBuiltinCall(node, Builtin::kConstruct, frame_state,
                               base::VectorOf(arguments), node->num_args()));

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ConstructWithSpread* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    base::SmallVector<OpIndex, 16> arguments;
    arguments.push_back(Map(node->function()));
    arguments.push_back(Map(node->new_target()));
    arguments.push_back(__ Word32Constant(node->num_args_no_spread()));
    arguments.push_back(Map(node->spread()));

    for (auto arg : node->args_no_spread()) {
      arguments.push_back(Map(arg));
    }

    arguments.push_back(Map(node->context()));

    SetMap(node, GenerateBuiltinCall(node, Builtin::kConstructWithSpread,
                                     frame_state, base::VectorOf(arguments),
                                     node->num_args_no_spread()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckConstructResult* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ CheckConstructResult(Map(node->construct_result_input()),
                                         Map(node->implicit_receiver_input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckDerivedConstructResult* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);
    V<Object> construct_result = Map(node->construct_result_input());
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    __ CheckDerivedConstructResult(construct_result, frame_state,
                                   native_context(),
                                   ShouldLazyDeoptOnThrow(node));
    SetMap(node, construct_result);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::SetKeyedGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->object_input()),
                           Map(node->key_input()),
                           Map(node->value_input()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kKeyedStoreIC, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::GetKeyedGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->object_input()), Map(node->key_input()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kKeyedLoadIC, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::SetNamedGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->object_input()),
                           __ HeapConstant(node->name().object()),
                           Map(node->value_input()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kStoreIC, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadNamedGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {
        Map(node->object_input()), __ HeapConstant(node->name().object()),
        __ TaggedIndexConstant(node->feedback().index()),
        __ HeapConstant(node->feedback().vector), Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kLoadIC, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LoadNamedFromSuperGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->receiver()),
                           Map(node->lookup_start_object()),
                           __ HeapConstant(node->name().object()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kLoadSuperIC, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LoadGlobal* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {__ HeapConstant(node->name().object()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    Builtin builtin;
    switch (node->typeof_mode()) {
      case TypeofMode::kInside:
        builtin = Builtin::kLoadGlobalICInsideTypeof;
        break;
      case TypeofMode::kNotInside:
        builtin = Builtin::kLoadGlobalIC;
        break;
    }

    SetMap(node, GenerateBuiltinCall(node, builtin, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StoreGlobal* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {
        __ HeapConstant(node->name().object()), Map(node->value()),
        __ TaggedIndexConstant(node->feedback().index()),
        __ HeapConstant(node->feedback().vector), Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kStoreGlobalIC, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::DefineKeyedOwnGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->object_input()),
                           Map(node->key_input()),
                           Map(node->value_input()),
                           Map(node->flags_input()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kDefineKeyedOwnIC,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::DefineNamedOwnGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->object_input()),
                           __ HeapConstant(node->name().object()),
                           Map(node->value_input()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kDefineNamedOwnIC,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::GetIterator* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {
        Map(node->receiver()), __ TaggedIndexConstant(node->load_slot()),
        __ TaggedIndexConstant(node->call_slot()),
        __ HeapConstant(node->feedback()), Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kGetIteratorWithFeedback,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CreateShallowObjectLiteral* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {
        __ HeapConstant(node->feedback().vector),
        __ TaggedIndexConstant(node->feedback().index()),
        __ HeapConstant(node->boilerplate_descriptor().object()),
        __ SmiConstant(Smi::FromInt(node->flags())), native_context()};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kCreateShallowObjectLiteral,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CreateShallowArrayLiteral* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {__ HeapConstant(node->feedback().vector),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->constant_elements().object()),
                           __ SmiConstant(Smi::FromInt(node->flags())),
                           native_context()};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kCreateShallowArrayLiteral,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StoreInArrayLiteralGeneric* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->object_input()),
                           Map(node->name_input()),
                           Map(node->value_input()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           native_context()};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kStoreInArrayLiteralIC,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::TestInstanceOf* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->object()), Map(node->callable()),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kInstanceOf, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::DeleteProperty* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {
        Map(node->object()), Map(node->key()),
        __ SmiConstant(Smi::FromInt(static_cast<int>(node->mode()))),
        Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kDeleteProperty,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ToName* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {Map(node->value_input()), Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kToName, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CreateRegExpLiteral* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {__ HeapConstant(node->feedback().vector),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->pattern().object()),
                           __ SmiConstant(Smi::FromInt(node->flags())),
                           native_context()};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kCreateRegExpLiteral,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::GetTemplateObject* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {
        __ HeapConstant(node->shared_function_info().object()),
        Map(node->description()), __ WordPtrConstant(node->feedback().index()),
        __ HeapConstant(node->feedback().vector), native_context()};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kGetTemplateObject,
                                     frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CreateObjectLiteral* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {
        __ HeapConstant(node->feedback().vector),
        __ TaggedIndexConstant(node->feedback().index()),
        __ HeapConstant(node->boilerplate_descriptor().object()),
        __ SmiConstant(Smi::FromInt(node->flags())), native_context()};

    SetMap(node,
           GenerateBuiltinCall(node, Builtin::kCreateObjectFromSlowBoilerplate,
                               frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CreateArrayLiteral* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {__ HeapConstant(node->feedback().vector),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->constant_elements().object()),
                           __ SmiConstant(Smi::FromInt(node->flags())),
                           native_context()};

    SetMap(node,
           GenerateBuiltinCall(node, Builtin::kCreateArrayFromSlowBoilerplate,
                               frame_state, base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ForInPrepare* node,
                                const maglev::ProcessingState& state) {
    OpIndex arguments[] = {Map(node->enumerator()),
                           __ TaggedIndexConstant(node->feedback().index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    V<Any> call =
        GenerateBuiltinCall(node, Builtin::kForInPrepare,
                            OptionalV<turboshaft::FrameState>::Nullopt(),
                            base::VectorOf(arguments));
    SetMap(node, __ Projection(call, 0, RegisterRepresentation::Tagged()));
    second_return_value_ = V<Object>::Cast(
        __ Projection(call, 1, RegisterRepresentation::Tagged()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ForInNext* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    OpIndex arguments[] = {__ WordPtrConstant(node->feedback().index()),
                           Map(node->receiver()),
                           Map(node->cache_array()),
                           Map(node->cache_type()),
                           Map(node->cache_index()),
                           __ HeapConstant(node->feedback().vector),
                           Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kForInNext, frame_state,
                                     base::VectorOf(arguments)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckSmi* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIfNot(__ ObjectIsSmi(Map(node->receiver_input())), frame_state,
                       DeoptimizeReason::kNotASmi,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckInt32IsSmi* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    DeoptIfInt32IsNotSmi(node->input(), frame_state,
                         node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckHoleyFloat64IsSmi* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    V<Word32> w32 = __ ChangeFloat64ToInt32OrDeopt(
        Map(node->input()), frame_state,
        CheckForMinusZeroMode::kCheckForMinusZero,
        node->eager_deopt_info()->feedback_to_update());
    if (!SmiValuesAre32Bits()) {
      DeoptIfInt32IsNotSmi(w32, frame_state,
                           node->eager_deopt_info()->feedback_to_update());
    }
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckNumber* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    V<Object> input = Map(node->receiver_input());
    V<Word32> check;
    if (node->mode() == Object::Conversion::kToNumeric) {
      check = __ ObjectIsNumberOrBigInt(input);
    } else {
      DCHECK_EQ(node->mode(), Object::Conversion::kToNumber);
      check = __ ObjectIsNumber(input);
    }
    __ DeoptimizeIfNot(check, frame_state, DeoptimizeReason::kNotANumber,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckHeapObject* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIf(__ ObjectIsSmi(Map(node->receiver_input())), frame_state,
                    DeoptimizeReason::kSmi,
                    node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckFloat64IsNan* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIfNot(__ Float64IsNaN(Map(node->target_input())), frame_state,
                       DeoptimizeReason::kWrongValue,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  void CheckMaps(V<Object> receiver_input, V<FrameState> frame_state,
                 const FeedbackSource& feedback,
                 const compiler::ZoneRefSet<Map>& maps, bool check_heap_object,
                 bool try_migrate) {
    Label<> done(this);
    if (check_heap_object) {
      OpIndex is_smi = __ IsSmi(receiver_input);
      if (AnyMapIsHeapNumber(maps)) {
        // Smis count as matching the HeapNumber map, so we're done.
        GOTO_IF(is_smi, done);
      } else {
        __ DeoptimizeIf(is_smi, frame_state, DeoptimizeReason::kWrongMap,
                        feedback);
      }
    }

    bool has_migration_targets = false;
    if (try_migrate) {
      for (MapRef map : maps) {
        if (map.object()->is_migration_target()) {
          has_migration_targets = true;
          break;
        }
      }
    }

    __ CheckMaps(V<HeapObject>::Cast(receiver_input), frame_state, maps,
                 has_migration_targets ? CheckMapsFlag::kTryMigrateInstance
                                       : CheckMapsFlag::kNone,
                 feedback);

    if (done.has_incoming_jump()) {
      GOTO(done);
      BIND(done);
    }
  }
  maglev::ProcessResult Process(maglev::CheckMaps* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    CheckMaps(Map(node->receiver_input()), frame_state,
              node->eager_deopt_info()->feedback_to_update(),
              node->maps().Clone(graph_zone()),
              node->check_type() == maglev::CheckType::kCheckHeapObject,
              /* try_migrate */ false);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckMapsWithMigration* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    CheckMaps(Map(node->receiver_input()), frame_state,
              node->eager_deopt_info()->feedback_to_update(),
              node->maps().Clone(graph_zone()),
              node->check_type() == maglev::CheckType::kCheckHeapObject,
              /* try_migrate */ true);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::MigrateMapIfNeeded* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(node,
           __ MigrateMapIfNeeded(
               Map(node->object_input()), Map(node->map_input()), frame_state,
               node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckValue* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIfNot(__ TaggedEqual(Map(node->target_input()),
                                      __ HeapConstant(node->value().object())),
                       frame_state, DeoptimizeReason::kWrongValue,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckValueEqualsInt32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIfNot(__ Word32Equal(Map(node->target_input()), node->value()),
                       frame_state, DeoptimizeReason::kWrongValue,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckValueEqualsFloat64* node,
                                const maglev::ProcessingState& state) {
    DCHECK(!std::isnan(node->value()));
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIfNot(
        __ Float64Equal(Map(node->target_input()), node->value()), frame_state,
        DeoptimizeReason::kWrongValue,
        node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckString* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    ObjectIsOp::InputAssumptions input_assumptions =
        node->check_type() == maglev::CheckType::kCheckHeapObject
            ? ObjectIsOp::InputAssumptions::kNone
            : ObjectIsOp::InputAssumptions::kHeapObject;
    V<Word32> check = __ ObjectIs(Map(node->receiver_input()),
                                  ObjectIsOp::Kind::kString, input_assumptions);
    __ DeoptimizeIfNot(check, frame_state, DeoptimizeReason::kNotAString,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckSymbol* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    ObjectIsOp::InputAssumptions input_assumptions =
        node->check_type() == maglev::CheckType::kCheckHeapObject
            ? ObjectIsOp::InputAssumptions::kNone
            : ObjectIsOp::InputAssumptions::kHeapObject;
    V<Word32> check = __ ObjectIs(Map(node->receiver_input()),
                                  ObjectIsOp::Kind::kSymbol, input_assumptions);
    __ DeoptimizeIfNot(check, frame_state, DeoptimizeReason::kNotASymbol,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckInstanceType* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ CheckInstanceType(
        Map(node->receiver_input()), frame_state,
        node->eager_deopt_info()->feedback_to_update(),
        node->first_instance_type(), node->last_instance_type(),
        node->check_type() != maglev::CheckType::kOmitHeapObjectCheck);

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckDynamicValue* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIfNot(
        __ TaggedEqual(Map(node->first_input()), Map(node->second_input())),
        frame_state, DeoptimizeReason::kWrongValue,
        node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedSmiSizedInt32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    DeoptIfInt32IsNotSmi(node->input(), frame_state,
                         node->eager_deopt_info()->feedback_to_update());
    SetMap(node, Map(node->input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckNotHole* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIf(RootEqual(node->object_input(), RootIndex::kTheHoleValue),
                    frame_state, DeoptimizeReason::kHole,
                    node->eager_deopt_info()->feedback_to_update());
    SetMap(node, Map(node->object_input()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckConstTrackingLetCellTagged* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ CheckConstTrackingLetCellTagged(
        Map(node->context_input()), Map(node->value_input()), node->index(),
        frame_state, node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckConstTrackingLetCell* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ CheckConstTrackingLetCell(
        Map(node->context_input()), node->index(), frame_state,
        node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckInt32Condition* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    bool negate_result = false;
    V<Word32> cmp = ConvertInt32Compare(node->left_input(), node->right_input(),
                                        node->condition(), &negate_result);
    if (negate_result) {
      __ DeoptimizeIf(cmp, frame_state, node->reason(),
                      node->eager_deopt_info()->feedback_to_update());
    } else {
      __ DeoptimizeIfNot(cmp, frame_state, node->reason(),
                         node->eager_deopt_info()->feedback_to_update());
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::AllocationBlock* node,
                                const maglev::ProcessingState& state) {
    DCHECK(
        node->is_used());  // Should have been dead-code eliminated otherwise.
    int size = 0;
    for (auto alloc : node->allocation_list()) {
      if (!alloc->HasBeenAnalysed() || alloc->HasEscaped()) {
        alloc->set_offset(size);
        size += alloc->size();
      }
    }
    node->set_size(size);
    SetMap(node, __ FinishInitialization(
                     __ Allocate<HeapObject>(size, node->allocation_type())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::InlinedAllocation* node,
                                const maglev::ProcessingState& state) {
    DCHECK(node->HasBeenAnalysed() &&
           node->HasEscaped());  // Would have been removed otherwise.
    V<HeapObject> alloc = Map(node->allocation_block());
    SetMap(node, __ BitcastWordPtrToHeapObject(__ WordPtrAdd(
                     __ BitcastHeapObjectToWordPtr(alloc), node->offset())));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::EnsureWritableFastElements* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ EnsureWritableFastElements(Map(node->object_input()),
                                               Map(node->elements_input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::MaybeGrowFastElements* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    GrowFastElementsMode mode =
        IsDoubleElementsKind(node->elements_kind())
            ? GrowFastElementsMode::kDoubleElements
            : GrowFastElementsMode::kSmiOrObjectElements;
    SetMap(node, __ MaybeGrowFastElements(
                     Map(node->object_input()), Map(node->elements_input()),
                     Map(node->index_input()),
                     Map(node->elements_length_input()), frame_state, mode,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ExtendPropertiesBackingStore* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(node, __ ExtendPropertiesBackingStore(
                     Map(node->property_array_input()),
                     Map(node->object_input()), node->old_length(), frame_state,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::TransitionElementsKindOrCheckMap* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    bool check_heap_object;
    switch (node->check_type()) {
      case maglev::CheckType::kCheckHeapObject:
        check_heap_object = true;
        break;
      case maglev::CheckType::kOmitHeapObjectCheck:
        check_heap_object = false;
        break;
    }
    __ TransitionElementsKindOrCheckMap(
        Map(node->object_input()), frame_state, check_heap_object,
        node->transition_sources(), node->transition_target(),
        node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TransitionElementsKind* node,
                                const maglev::ProcessingState& state) {
    __ TransitionMultipleElementsKind(Map(node->object_input()),
                                      node->transition_sources(),
                                      node->transition_target());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::HasInPrototypeChain* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    SetMap(node, __ HasInPrototypeChain(Map(node->object()), node->prototype(),
                                        frame_state, native_context(),
                                        ShouldLazyDeoptOnThrow(node)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::UpdateJSArrayLength* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ UpdateJSArrayLength(Map(node->length_input()),
                                        Map(node->object_input()),
                                        Map(node->index_input())));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::AllocateElementsArray* node,
                                const maglev::ProcessingState& state) {
    V<Word32> length = Map(node->length_input());
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    // Note that {length} cannot be negative (Maglev inserts a check before
    // AllocateElementsArray to ensure this).
    __ DeoptimizeIfNot(
        __ Uint32LessThan(length, JSArray::kInitialMaxFastElementArray),
        frame_state, DeoptimizeReason::kGreaterThanMaxFastElementArray,
        node->eager_deopt_info()->feedback_to_update());
    RETURN_IF_UNREACHABLE();

    SetMap(node,
           __ NewArray(__ ChangeUint32ToUintPtr(length),
                       NewArrayOp::Kind::kObject, node->allocation_type()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StringConcat* node,
                                const maglev::ProcessingState& state) {
    // When coming from Turbofan, StringConcat is always guarded by a check that
    // the length is less than String::kMaxLength, which prevents StringConcat
    // from ever throwing (and as a consequence of this, it does not need a
    // Context input). This is not the case for Maglev. To mimic Turbofan's
    // behavior, we thus insert here a length check.
    // TODO(dmercadier): I'm not convinced that these checks make a lot of
    // sense, since they make the graph bigger, and throwing before the builtin
    // call to StringConcat isn't super important since throwing is not supposed
    // to be fast. We should consider just calling the builtin and letting it
    // throw. With LazyDeopOnThrow, this is currently a bit verbose to
    // implement, so we should first find a way to have this LazyDeoptOnThrow
    // without adding a member to all throwing operations (like adding
    // LazyDeoptOnThrow in FrameStateOp).
    ThrowingScope throwing_scope(this, node);

    V<String> left = Map(node->lhs());
    V<String> right = Map(node->rhs());

    V<Word32> left_len = __ StringLength(left);
    V<Word32> right_len = __ StringLength(right);

    V<Tuple<Word32, Word32>> len_and_ovf =
        __ Int32AddCheckOverflow(left_len, right_len);
    V<Word32> len = __ Projection<0>(len_and_ovf);
    V<Word32> ovf = __ Projection<1>(len_and_ovf);

    Label<> throw_invalid_length(this);
    Label<> done(this);

    GOTO_IF(UNLIKELY(ovf), throw_invalid_length);
    GOTO_IF(LIKELY(__ Uint32LessThanOrEqual(len, String::kMaxLength)), done);

    GOTO(throw_invalid_length);
    BIND(throw_invalid_length);
    {
      GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
      __ CallRuntime_ThrowInvalidStringLength(isolate_, frame_state,
                                              native_context(),
                                              ShouldLazyDeoptOnThrow(node));
      // We should not return from Throw.
      __ Unreachable();
    }

    BIND(done);

    SetMap(node, __ StringConcat(Map(node->lhs()), Map(node->rhs())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StringEqual* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ StringEqual(Map(node->lhs()), Map(node->rhs())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StringLength* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ StringLength(Map(node->object_input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StringAt* node,
                                const maglev::ProcessingState& state) {
    V<Word32> char_code =
        __ StringCharCodeAt(Map(node->string_input()),
                            __ ChangeUint32ToUintPtr(Map(node->index_input())));
    SetMap(node, __ ConvertCharCodeToString(char_code));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedInternalizedString* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(node, __ CheckedInternalizedString(
                     Map(node->object_input()), frame_state,
                     node->check_type() == maglev::CheckType::kCheckHeapObject,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckValueEqualsString* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ CheckValueEqualsString(Map(node->target_input()), node->value(),
                              frame_state,
                              node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BuiltinStringFromCharCode* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ConvertCharCodeToString(Map(node->code_input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::BuiltinStringPrototypeCharCodeOrCodePointAt* node,
      const maglev::ProcessingState& state) {
    if (node->mode() == maglev::BuiltinStringPrototypeCharCodeOrCodePointAt::
                            Mode::kCharCodeAt) {
      SetMap(node, __ StringCharCodeAt(
                       Map(node->string_input()),
                       __ ChangeUint32ToUintPtr(Map(node->index_input()))));
    } else {
      DCHECK_EQ(node->mode(),
                maglev::BuiltinStringPrototypeCharCodeOrCodePointAt::Mode::
                    kCodePointAt);
      SetMap(node, __ StringCodePointAt(
                       Map(node->string_input()),
                       __ ChangeUint32ToUintPtr(Map(node->index_input()))));
    }
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ToString* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    Label<String> done(this);

    V<Object> value = Map(node->value_input());
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());

    GOTO_IF(__ ObjectIsString(value), done, V<String>::Cast(value));

    IF_NOT (__ IsSmi(value)) {
      if (node->mode() == maglev::ToString::ConversionMode::kConvertSymbol) {
        V<i::Map> map = __ LoadMapField(value);
        V<Word32> instance_type = __ LoadInstanceTypeField(map);
        IF (__ Word32Equal(instance_type, SYMBOL_TYPE)) {
          GOTO(done, __ CallRuntime_SymbolDescriptiveString(
                         isolate_, frame_state, Map(node->context()),
                         V<Symbol>::Cast(value), ShouldLazyDeoptOnThrow(node)));
        }
      }
    }

    GOTO(done,
         __ CallBuiltin_ToString(isolate_, frame_state, Map(node->context()),
                                 value, ShouldLazyDeoptOnThrow(node)));

    BIND(done, result);
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::NumberToString* node,
                                const maglev::ProcessingState& state) {
    NoThrowingScopeRequired no_throws(node);

    SetMap(node,
           __ CallBuiltin_NumberToString(isolate_, Map(node->value_input())));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ArgumentsLength* node,
                                const maglev::ProcessingState& state) {
    // TODO(dmercadier): ArgumentsLength in Maglev returns a raw Word32, while
    // in Turboshaft, it returns a Smi. We thus untag this Smi here to match
    // Maglev's behavior, but it would be more efficient to change Turboshaft's
    // ArgumentsLength operation to return a raw Word32 as well.
    SetMap(node, __ UntagSmi(__ ArgumentsLength()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ArgumentsElements* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ NewArgumentsElements(Map(node->arguments_count_input()),
                                         node->type(),
                                         node->formal_parameter_count()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::RestLength* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ RestLength(node->formal_parameter_count()));
    return maglev::ProcessResult::kContinue;
  }

  template <typename T>
  maglev::ProcessResult Process(maglev::AbstractLoadTaggedField<T>* node,
                                const maglev::ProcessingState& state) {
    V<Object> value =
        __ LoadTaggedField(Map(node->object_input()), node->offset());
    SetMap(node, value);

    if (generator_analyzer_.has_header_bypasses() &&
        maglev_generator_context_node_ == nullptr &&
        node->object_input().node()->template Is<maglev::RegisterInput>() &&
        node->offset() == JSGeneratorObject::kContextOffset) {
      // This is loading the context of a generator for the 1st time. We save it
      // in {generator_context_} for later use.
      __ SetVariable(generator_context_, value);
      maglev_generator_context_node_ = node;
    }

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadDoubleField* node,
                                const maglev::ProcessingState& state) {
    V<HeapNumber> field = __ LoadTaggedField<HeapNumber>(
        Map(node->object_input()), node->offset());
    SetMap(node, __ LoadHeapNumberValue(field));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadFixedArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadFixedArrayElement(
                     Map(node->elements_input()),
                     __ ChangeInt32ToIntPtr(Map(node->index_input()))));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadFixedDoubleArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadFixedDoubleArrayElement(
                     Map(node->elements_input()),
                     __ ChangeInt32ToIntPtr(Map(node->index_input()))));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadHoleyFixedDoubleArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadFixedDoubleArrayElement(
                     Map(node->elements_input()),
                     __ ChangeInt32ToIntPtr(Map(node->index_input()))));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::LoadHoleyFixedDoubleArrayElementCheckedNotHole* node,
      const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    V<Float64> result = __ LoadFixedDoubleArrayElement(
        Map(node->elements_input()),
        __ ChangeInt32ToIntPtr(Map(node->index_input())));
    __ DeoptimizeIf(__ Float64IsHole(result), frame_state,
                    DeoptimizeReason::kHole,
                    node->eager_deopt_info()->feedback_to_update());
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StoreTaggedFieldNoWriteBarrier* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), Map(node->value_input()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::AnyTagged(),
             WriteBarrierKind::kNoWriteBarrier, node->offset(),
             node->initializing_or_transitioning());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreTaggedFieldWithWriteBarrier* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), Map(node->value_input()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::AnyTagged(),
             WriteBarrierKind::kFullWriteBarrier, node->offset(),
             node->initializing_or_transitioning());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreDoubleField* node,
                                const maglev::ProcessingState& state) {
    V<HeapNumber> field = __ LoadTaggedField<HeapNumber>(
        Map(node->object_input()), node->offset());
    __ StoreField(field, AccessBuilder::ForHeapNumberValue(),
                  Map(node->value_input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::StoreTrustedPointerFieldWithWriteBarrier* node,
      const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), Map(node->value_input()),
             StoreOp::Kind::TaggedBase(),
             MemoryRepresentation::IndirectPointer(),
             WriteBarrierKind::kIndirectPointerWriteBarrier, node->offset(),
             node->initializing_or_transitioning());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::StoreFixedArrayElementNoWriteBarrier* node,
      const maglev::ProcessingState& state) {
    __ StoreFixedArrayElement(Map(node->elements_input()),
                              __ ChangeInt32ToIntPtr(Map(node->index_input())),
                              Map(node->value_input()),
                              WriteBarrierKind::kNoWriteBarrier);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::StoreFixedArrayElementWithWriteBarrier* node,
      const maglev::ProcessingState& state) {
    __ StoreFixedArrayElement(Map(node->elements_input()),
                              __ ChangeInt32ToIntPtr(Map(node->index_input())),
                              Map(node->value_input()),
                              WriteBarrierKind::kFullWriteBarrier);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreFixedDoubleArrayElement* node,
                                const maglev::ProcessingState& state) {
    __ StoreFixedDoubleArrayElement(
        Map(node->elements_input()),
        __ ChangeInt32ToIntPtr(Map(node->index_input())),
        Map(node->value_input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreMap* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), __ HeapConstant(node->map().object()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::TaggedPointer(),
             WriteBarrierKind::kMapWriteBarrier, HeapObject::kMapOffset,
             /*maybe_initializing_or_transitioning*/ true);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreFloat64* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), Map(node->value_input()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::Float64(),
             WriteBarrierKind::kNoWriteBarrier, node->offset());
    return maglev::ProcessResult::kContinue;
  }

  // For-in specific operations.
  maglev::ProcessResult Process(maglev::LoadEnumCacheLength* node,
                                const maglev::ProcessingState& state) {
    V<Word32> bitfield3 =
        __ LoadField<Word32>(V<i::Map>::Cast(Map(node->map_input())),
                             AccessBuilder::ForMapBitField3());
    V<Word32> length = __ Word32ShiftRightLogical(
        __ Word32BitwiseAnd(bitfield3, Map::Bits3::EnumLengthBits::kMask),
        Map::Bits3::EnumLengthBits::kShift);
    SetMap(node, length);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckCacheIndicesNotCleared* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    // If the cache length is zero, we don't have any indices, so we know this
    // is ok even though the indices are the empty array.
    IF_NOT (__ Word32Equal(Map(node->length_input()), 0)) {
      // Otherwise, an empty array with non-zero required length is not valid.
      V<Word32> condition =
          RootEqual(node->indices_input(), RootIndex::kEmptyFixedArray);
      __ DeoptimizeIf(condition, frame_state,
                      DeoptimizeReason::kWrongEnumIndices,
                      node->eager_deopt_info()->feedback_to_update());
    }
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadTaggedFieldByFieldIndex* node,
                                const maglev::ProcessingState& state) {
    SetMap(node,
           __ LoadFieldByIndex(Map(node->object_input()),
                               __ UntagSmi(Map<Smi>(node->index_input()))));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LoadTypedArrayLength* node,
                                const maglev::ProcessingState& state) {
    // TODO(dmercadier): consider loading the raw length instead of the byte
    // length. This is not currently done because the raw length field might be
    // removed soon.
    V<WordPtr> length =
        __ LoadField<WordPtr>(Map<JSTypedArray>(node->receiver_input()),
                              AccessBuilder::ForJSTypedArrayByteLength());

    int element_size = ElementsKindSize(node->elements_kind());
    if (element_size > 1) {
      DCHECK(element_size == 2 || element_size == 4 || element_size == 8);
      length = __ WordPtrShiftRightLogical(
          length, base::bits::CountTrailingZeros(element_size));
    }
    SetMap(node, length);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckTypedArrayBounds* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIfNot(
        __ UintPtrLessThan(__ ChangeUint32ToUintPtr(Map(node->index_input())),
                           Map(node->length_input())),
        frame_state, DeoptimizeReason::kOutOfBounds,
        node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LoadUnsignedIntTypedArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, BuildTypedArrayLoad(Map<JSTypedArray>(node->object_input()),
                                     Map<Word32>(node->index_input()),
                                     node->elements_kind()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadSignedIntTypedArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, BuildTypedArrayLoad(Map<JSTypedArray>(node->object_input()),
                                     Map<Word32>(node->index_input()),
                                     node->elements_kind()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadDoubleTypedArrayElement* node,
                                const maglev::ProcessingState& state) {
    DCHECK_EQ(node->elements_kind(),
              any_of(FLOAT32_ELEMENTS, FLOAT64_ELEMENTS));
    V<Float> value = V<Float>::Cast(BuildTypedArrayLoad(
        Map<JSTypedArray>(node->object_input()),
        Map<Word32>(node->index_input()), node->elements_kind()));
    if (node->elements_kind() == FLOAT32_ELEMENTS) {
      value = __ ChangeFloat32ToFloat64(V<Float32>::Cast(value));
    }
    SetMap(node, value);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StoreIntTypedArrayElement* node,
                                const maglev::ProcessingState& state) {
    BuildTypedArrayStore(Map<JSTypedArray>(node->object_input()),
                         Map<Word32>(node->index_input()),
                         Map<Untagged>(node->value_input()),
                         node->elements_kind());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreDoubleTypedArrayElement* node,
                                const maglev::ProcessingState& state) {
    DCHECK_EQ(node->elements_kind(),
              any_of(FLOAT32_ELEMENTS, FLOAT64_ELEMENTS));
    V<Float> value = Map<Float>(node->value_input());
    if (node->elements_kind() == FLOAT32_ELEMENTS) {
      value = __ TruncateFloat64ToFloat32(Map(node->value_input()));
    }
    BuildTypedArrayStore(Map<JSTypedArray>(node->object_input()),
                         Map<Word32>(node->index_input()), value,
                         node->elements_kind());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckJSDataViewBounds* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    // Normal DataView (backed by AB / SAB) or non-length tracking backed by
    // GSAB.
    V<WordPtr> byte_length =
        __ LoadField<WordPtr>(Map<JSTypedArray>(node->receiver_input()),
                              AccessBuilder::ForJSDataViewByteLength());

    int element_size = ExternalArrayElementSize(node->element_type());
    if (element_size > 1) {
      // For element_size larger than 1, we need to make sure that {index} is
      // less than {byte_length}, but also that {index+element_size} is less
      // than {byte_length}. We do this by subtracting {element_size-1} from
      // {byte_length}: if the resulting length is greater than 0, then we can
      // just treat {element_size} as 1 and check if {index} is less than this
      // new {byte_length}.
      DCHECK(element_size == 2 || element_size == 4 || element_size == 8);
      byte_length = __ WordPtrSub(byte_length, element_size - 1);
      __ DeoptimizeIf(__ IntPtrLessThan(byte_length, 0), frame_state,
                      DeoptimizeReason::kOutOfBounds,
                      node->eager_deopt_info()->feedback_to_update());
    }
    __ DeoptimizeIfNot(
        __ Uint32LessThan(Map<Word32>(node->index_input()),
                          __ TruncateWordPtrToWord32(byte_length)),
        frame_state, DeoptimizeReason::kOutOfBounds,
        node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LoadSignedIntDataViewElement* node,
                                const maglev::ProcessingState& state) {
    V<JSDataView> data_view = Map<JSDataView>(node->object_input());
    V<WordPtr> storage = __ LoadField<WordPtr>(
        data_view, AccessBuilder::ForJSDataViewDataPointer());
    V<Word32> is_little_endian =
        ToBit(node->is_little_endian_input(),
              TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject);
    SetMap(node, __ LoadDataViewElement(
                     data_view, storage,
                     __ ChangeUint32ToUintPtr(Map<Word32>(node->index_input())),
                     is_little_endian, node->type()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadDoubleDataViewElement* node,
                                const maglev::ProcessingState& state) {
    V<JSDataView> data_view = Map<JSDataView>(node->object_input());
    V<WordPtr> storage = __ LoadField<WordPtr>(
        data_view, AccessBuilder::ForJSDataViewDataPointer());
    V<Word32> is_little_endian =
        ToBit(node->is_little_endian_input(),
              TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject);
    SetMap(node,
           __ LoadDataViewElement(
               data_view, storage,
               __ ChangeUint32ToUintPtr(Map<Word32>(node->index_input())),
               is_little_endian, ExternalArrayType::kExternalFloat64Array));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StoreSignedIntDataViewElement* node,
                                const maglev::ProcessingState& state) {
    V<JSDataView> data_view = Map<JSDataView>(node->object_input());
    V<WordPtr> storage = __ LoadField<WordPtr>(
        data_view, AccessBuilder::ForJSDataViewDataPointer());
    V<Word32> is_little_endian =
        ToBit(node->is_little_endian_input(),
              TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject);
    __ StoreDataViewElement(
        data_view, storage,
        __ ChangeUint32ToUintPtr(Map<Word32>(node->index_input())),
        Map<Word32>(node->value_input()), is_little_endian, node->type());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreDoubleDataViewElement* node,
                                const maglev::ProcessingState& state) {
    V<JSDataView> data_view = Map<JSDataView>(node->object_input());
    V<WordPtr> storage = __ LoadField<WordPtr>(
        data_view, AccessBuilder::ForJSDataViewDataPointer());
    V<Word32> is_little_endian =
        ToBit(node->is_little_endian_input(),
              TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject);
    __ StoreDataViewElement(
        data_view, storage,
        __ ChangeUint32ToUintPtr(Map<Word32>(node->index_input())),
        Map<Float64>(node->value_input()), is_little_endian,
        ExternalArrayType::kExternalFloat64Array);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckTypedArrayNotDetached* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIf(
        __ ArrayBufferIsDetached(Map<JSArrayBufferView>(node->object_input())),
        frame_state, DeoptimizeReason::kArrayBufferWasDetached,
        node->eager_deopt_info()->feedback_to_update());

    return maglev::ProcessResult::kContinue;
  }

  void BuildJump(maglev::BasicBlock* target) {
    Block* destination = Map(target);
    if (target->is_loop() && (target->predecessor_count() > 2 ||
                              generator_analyzer_.HeaderIsBypassed(target))) {
      // This loop has multiple forward edge in Maglev, so we'll create an extra
      // block in Turboshaft that will be the only predecessor.
      auto it = loop_single_edge_predecessors_.find(target);
      if (it != loop_single_edge_predecessors_.end()) {
        destination = it->second;
      } else {
        Block* loop_only_pred = __ NewBlock();
        loop_single_edge_predecessors_[target] = loop_only_pred;
        destination = loop_only_pred;
      }
    }
    __ Goto(destination);
  }

  maglev::ProcessResult Process(maglev::Jump* node,
                                const maglev::ProcessingState& state) {
    BuildJump(node->target());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckpointedJump* node,
                                const maglev::ProcessingState& state) {
    BuildJump(node->target());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::JumpLoop* node,
                                const maglev::ProcessingState& state) {
    if (header_switch_input_.valid()) {
      __ SetVariable(header_switch_input_, loop_default_generator_value_);
    }
    __ Goto(Map(node->target()));
    FixLoopPhis(node->target());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Int32Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> bool_res =
        ConvertCompare<Word32>(node->left_input(), node->right_input(),
                               node->operation(), Sign::kSigned);
    SetMap(node, ConvertWord32ToJSBool(bool_res));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> bool_res =
        ConvertCompare<Float64>(node->left_input(), node->right_input(),
                                node->operation(), Sign::kSigned);
    SetMap(node, ConvertWord32ToJSBool(bool_res));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TaggedEqual* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, ConvertWord32ToJSBool(
                     __ TaggedEqual(Map(node->lhs()), Map(node->rhs()))));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TaggedNotEqual* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, ConvertWord32ToJSBool(
                     __ TaggedEqual(Map(node->lhs()), Map(node->rhs())),
                     /*flip*/ true));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TestUndetectable* node,
                                const maglev::ProcessingState& state) {
    ObjectIsOp::InputAssumptions assumption;
    switch (node->check_type()) {
      case maglev::CheckType::kCheckHeapObject:
        assumption = ObjectIsOp::InputAssumptions::kNone;
        break;
      case maglev::CheckType::kOmitHeapObjectCheck:
        assumption = ObjectIsOp::InputAssumptions::kHeapObject;
        break;
    }
    SetMap(node, ConvertWord32ToJSBool(
                     __ ObjectIs(Map(node->value()),
                                 ObjectIsOp::Kind::kUndetectable, assumption)));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TestTypeOf* node,
                                const maglev::ProcessingState& state) {
    V<Object> input = Map(node->value());
    V<Boolean> result;
    switch (node->literal()) {
      case interpreter::TestTypeOfFlags::LiteralFlag::kNumber:
        result = ConvertWord32ToJSBool(
            __ ObjectIs(input, ObjectIsOp::Kind::kNumber,
                        ObjectIsOp::InputAssumptions::kNone));
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kString:
        result = ConvertWord32ToJSBool(
            __ ObjectIs(input, ObjectIsOp::Kind::kString,
                        ObjectIsOp::InputAssumptions::kNone));
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kSymbol:
        result = ConvertWord32ToJSBool(
            __ ObjectIs(input, ObjectIsOp::Kind::kSymbol,
                        ObjectIsOp::InputAssumptions::kNone));
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kBigInt:
        result = ConvertWord32ToJSBool(
            __ ObjectIs(input, ObjectIsOp::Kind::kBigInt,
                        ObjectIsOp::InputAssumptions::kNone));
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kFunction:
        result = ConvertWord32ToJSBool(
            __ ObjectIs(input, ObjectIsOp::Kind::kDetectableCallable,
                        ObjectIsOp::InputAssumptions::kNone));
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kBoolean:
        result = __ Select(__ RootEqual(input, RootIndex::kTrueValue, isolate_),
                           __ HeapConstant(local_factory_->true_value()),
                           ConvertWord32ToJSBool(__ RootEqual(
                               input, RootIndex::kFalseValue, isolate_)),
                           RegisterRepresentation::Tagged(), BranchHint::kNone,
                           SelectOp::Implementation::kBranch);
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kUndefined:
        result = __ Select(__ RootEqual(input, RootIndex::kNullValue, isolate_),
                           __ HeapConstant(local_factory_->false_value()),
                           ConvertWord32ToJSBool(__ ObjectIs(
                               input, ObjectIsOp::Kind::kUndetectable,
                               ObjectIsOp::InputAssumptions::kNone)),
                           RegisterRepresentation::Tagged(), BranchHint::kNone,
                           SelectOp::Implementation::kBranch);
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kObject:
        result = __ Select(__ ObjectIs(input, ObjectIsOp::Kind::kNonCallable,
                                       ObjectIsOp::InputAssumptions::kNone),
                           __ HeapConstant(local_factory_->true_value()),
                           ConvertWord32ToJSBool(__ RootEqual(
                               input, RootIndex::kNullValue, isolate_)),
                           RegisterRepresentation::Tagged(), BranchHint::kNone,
                           SelectOp::Implementation::kBranch);
        break;
      case interpreter::TestTypeOfFlags::LiteralFlag::kOther:
        UNREACHABLE();  // Should never be emitted.
    }

    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckDetectableCallable* node,
                                const maglev::ProcessingState& state) {
    V<Object> receiver = Map(node->receiver_input());
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());

    ObjectIsOp::InputAssumptions assumptions;
    switch (node->check_type()) {
      case maglev::CheckType::kCheckHeapObject:
        assumptions = ObjectIsOp::InputAssumptions::kNone;
        break;
      case maglev::CheckType::kOmitHeapObjectCheck:
        assumptions = ObjectIsOp::InputAssumptions::kHeapObject;
        break;
    }

    __ DeoptimizeIfNot(
        __ ObjectIs(receiver, ObjectIsOp::Kind::kDetectableCallable,
                    assumptions),
        frame_state, DeoptimizeReason::kNotDetectableReceiver,
        node->eager_deopt_info()->feedback_to_update());

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::BranchIfToBooleanTrue* node,
                                const maglev::ProcessingState& state) {
    TruncateJSPrimitiveToUntaggedOp::InputAssumptions assumption =
        node->check_type() == maglev::CheckType::kCheckHeapObject
            ? TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject
            : TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kHeapObject;
    V<Word32> condition = ToBit(node->condition_input(), assumption);
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfInt32Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition =
        ConvertCompare<Word32>(node->left_input(), node->right_input(),
                               node->operation(), Sign::kSigned);
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfUint32Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition =
        ConvertCompare<Word32>(node->left_input(), node->right_input(),
                               node->operation(), Sign::kUnsigned);
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfFloat64Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition =
        ConvertCompare<Float64>(node->left_input(), node->right_input(),
                                node->operation(), Sign::kSigned);
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfInt32ToBooleanTrue* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = Map(node->condition_input());
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfFloat64ToBooleanTrue* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = Float64ToBit(Map(node->condition_input()));
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfFloat64IsHole* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = __ Float64IsHole(Map(node->condition_input()));
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfReferenceEqual* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition =
        __ TaggedEqual(Map(node->left_input()), Map(node->right_input()));
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfRootConstant* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition =
        RootEqual(node->condition_input(), node->root_index());
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfUndefinedOrNull* node,
                                const maglev::ProcessingState& state) {
    __ GotoIf(RootEqual(node->condition_input(), RootIndex::kUndefinedValue),
              Map(node->if_true()));
    __ Branch(RootEqual(node->condition_input(), RootIndex::kNullValue),
              Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfUndetectable* node,
                                const maglev::ProcessingState& state) {
    ObjectIsOp::InputAssumptions assumption;
    switch (node->check_type()) {
      case maglev::CheckType::kCheckHeapObject:
        assumption = ObjectIsOp::InputAssumptions::kNone;
        break;
      case maglev::CheckType::kOmitHeapObjectCheck:
        assumption = ObjectIsOp::InputAssumptions::kHeapObject;
        break;
    }
    __ Branch(__ ObjectIs(Map(node->condition_input()),
                          ObjectIsOp::Kind::kUndetectable, assumption),
              Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfSmi* node,
                                const maglev::ProcessingState& state) {
    __ Branch(__ IsSmi(Map(node->condition_input())), Map(node->if_true()),
              Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfJSReceiver* node,
                                const maglev::ProcessingState& state) {
    __ GotoIf(__ IsSmi(Map(node->condition_input())), Map(node->if_false()));
    __ Branch(__ JSAnyIsNotPrimitive(Map(node->condition_input())),
              Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Switch* node,
                                const maglev::ProcessingState& state) {
    if (is_visiting_generator_main_switch_) {
      // This is the main resume-switch for a generator, and some of its target
      // bypass loop headers. We need to re-route the destinations to the
      // bypassed loop headers, where secondary switches will be inserted.

      compiler::turboshaft::SwitchOp::Case* cases =
          __ output_graph().graph_zone()
              -> AllocateArray<compiler::turboshaft::SwitchOp::Case>(
                               node->size());

      DCHECK_EQ(0, node->value_base());

      for (int i = 0; i < node->size(); i++) {
        maglev::BasicBlock* target = node->targets()[i].block_ptr();
        if (generator_analyzer_.JumpBypassesHeader(target)) {
          Block* new_dst = __ NewBlock();

          const maglev::BasicBlock* innermost_bypassed_header =
              generator_analyzer_.GetInnermostBypassedHeader(target);

          pre_loop_generator_blocks_[innermost_bypassed_header].push_back(
              {new_dst, Map(target), i});

          // {innermost_bypassed_header} is only the innermost bypassed header.
          // We also need to record bypasses of outer headers. In the end, we
          // want this main Switch to go to before the outermost header, which
          // will dispatch to the next inner loop, and so on until the innermost
          // loop header and then to the initial destination.
          for (const maglev::BasicBlock* bypassed_header =
                   generator_analyzer_.GetLoopHeader(innermost_bypassed_header);
               bypassed_header != nullptr;
               bypassed_header =
                   generator_analyzer_.GetLoopHeader(bypassed_header)) {
            Block* prev_loop_dst = __ NewBlock();
            pre_loop_generator_blocks_[bypassed_header].push_back(
                {prev_loop_dst, new_dst, i});
            new_dst = prev_loop_dst;
          }

          cases[i] = {i, new_dst, BranchHint::kNone};

        } else {
          cases[i] = {i, Map(target), BranchHint::kNone};
        }
      }

      Block* default_block = __ NewBlock();
      __ Switch(Map(node->value()), base::VectorOf(cases, node->size()),
                default_block);
      __ Bind(default_block);
      __ Unreachable();

      return maglev::ProcessResult::kContinue;
    }

    compiler::turboshaft::SwitchOp::Case* cases =
        __ output_graph().graph_zone()
            -> AllocateArray<compiler::turboshaft::SwitchOp::Case>(
                             node->size());
    int case_value_base = node->value_base();
    for (int i = 0; i < node->size(); i++) {
      cases[i] = {i + case_value_base, Map(node->targets()[i].block_ptr()),
                  BranchHint::kNone};
    }
    Block* default_block;
    bool emit_default_block = false;
    if (node->has_fallthrough()) {
      default_block = Map(state.next_block());
    } else {
      default_block = __ NewBlock();
      emit_default_block = true;
    }
    __ Switch(Map(node->value()), base::VectorOf(cases, node->size()),
              default_block);
    if (emit_default_block) {
      __ Bind(default_block);
      __ Unreachable();
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckedSmiUntag* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(node,
           __ CheckedSmiUntag(Map(node->input()), frame_state,
                              node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::UnsafeSmiUntag* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ UntagSmi(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedSmiTagInt32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(
        node,
        __ ConvertUntaggedToJSPrimitiveOrDeopt(
            Map(node->input()), frame_state,
            ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind::kSmi,
            RegisterRepresentation::Word32(),
            ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::kSigned,
            node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedSmiTagUint32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(node,
           __ ConvertUntaggedToJSPrimitiveOrDeopt(
               Map(node->input()), frame_state,
               ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind::kSmi,
               RegisterRepresentation::Word32(),
               ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::
                   kUnsigned,
               node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedSmiTagFloat64* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    V<Word32> as_int32 = __ ChangeFloat64ToInt32OrDeopt(
        Map(node->input()), frame_state,
        CheckForMinusZeroMode::kCheckForMinusZero,
        node->eager_deopt_info()->feedback_to_update());
    SetMap(
        node,
        __ ConvertUntaggedToJSPrimitiveOrDeopt(
            as_int32, frame_state,
            ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind::kSmi,
            RegisterRepresentation::Word32(),
            ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::kSigned,
            node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::UnsafeSmiTagInt32* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ TagSmi(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::UnsafeSmiTagUint32* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ TagSmi(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }

#define PROCESS_BINOP_WITH_OVERFLOW(MaglevName, TurboshaftName,                \
                                    minus_zero_mode)                           \
  maglev::ProcessResult Process(maglev::Int32##MaglevName##WithOverflow* node, \
                                const maglev::ProcessingState& state) {        \
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());        \
    SetMap(node,                                                               \
           __ Word32##TurboshaftName##DeoptOnOverflow(                         \
               Map(node->left_input()), Map(node->right_input()), frame_state, \
               node->eager_deopt_info()->feedback_to_update(),                 \
               CheckForMinusZeroMode::k##minus_zero_mode));                    \
    return maglev::ProcessResult::kContinue;                                   \
  }
  PROCESS_BINOP_WITH_OVERFLOW(Add, SignedAdd, DontCheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Subtract, SignedSub, DontCheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Multiply, SignedMul, CheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Divide, SignedDiv, CheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Modulus, SignedMod, CheckForMinusZero)
#undef PROCESS_BINOP_WITH_OVERFLOW
  maglev::ProcessResult Process(maglev::Int32IncrementWithOverflow* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    // Turboshaft doesn't have a dedicated Increment operation; we use a regular
    // addition instead.
    SetMap(node, __ Word32SignedAddDeoptOnOverflow(
                     Map(node->value_input()), 1, frame_state,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32DecrementWithOverflow* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    // Turboshaft doesn't have a dedicated Decrement operation; we use a regular
    // addition instead.
    SetMap(node, __ Word32SignedSubDeoptOnOverflow(
                     Map(node->value_input()), 1, frame_state,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32NegateWithOverflow* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    // Turboshaft doesn't have a Int32NegateWithOverflow operation, but Turbofan
    // emits mutliplications by -1 for this, so using this as well here.
    SetMap(node, __ Word32SignedMulDeoptOnOverflow(
                     Map(node->value_input()), -1, frame_state,
                     node->eager_deopt_info()->feedback_to_update(),
                     CheckForMinusZeroMode::kCheckForMinusZero));
    return maglev::ProcessResult::kContinue;
  }

#define PROCESS_FLOAT64_BINOP(MaglevName, TurboshaftName)               \
  maglev::ProcessResult Process(maglev::Float64##MaglevName* node,      \
                                const maglev::ProcessingState& state) { \
    SetMap(node, __ Float64##TurboshaftName(Map(node->left_input()),    \
                                            Map(node->right_input()))); \
    return maglev::ProcessResult::kContinue;                            \
  }
  PROCESS_FLOAT64_BINOP(Add, Add)
  PROCESS_FLOAT64_BINOP(Subtract, Sub)
  PROCESS_FLOAT64_BINOP(Multiply, Mul)
  PROCESS_FLOAT64_BINOP(Divide, Div)
  PROCESS_FLOAT64_BINOP(Modulus, Mod)
  PROCESS_FLOAT64_BINOP(Exponentiate, Power)
#undef PROCESS_FLOAT64_BINOP

#define PROCESS_INT32_BITWISE_BINOP(Name)                               \
  maglev::ProcessResult Process(maglev::Int32Bitwise##Name* node,       \
                                const maglev::ProcessingState& state) { \
    SetMap(node, __ Word32Bitwise##Name(Map(node->left_input()),        \
                                        Map(node->right_input())));     \
    return maglev::ProcessResult::kContinue;                            \
  }
  PROCESS_INT32_BITWISE_BINOP(And)
  PROCESS_INT32_BITWISE_BINOP(Or)
  PROCESS_INT32_BITWISE_BINOP(Xor)
#undef PROCESS_INT32_BITWISE_BINOP

#define PROCESS_INT32_SHIFT(MaglevName, TurboshaftName)                        \
  maglev::ProcessResult Process(maglev::Int32##MaglevName* node,               \
                                const maglev::ProcessingState& state) {        \
    V<Word32> right = Map(node->right_input());                                \
    if (!SupportedOperations::word32_shift_is_safe()) {                        \
      /* JavaScript spec says that the right-hand side of a shift should be    \
       * taken modulo 32. Some architectures do this automatically, some       \
       * don't. For those that don't, which do this modulo 32 with a `& 0x1f`. \
       */                                                                      \
      right = __ Word32BitwiseAnd(right, 0x1f);                                \
    }                                                                          \
    SetMap(node, __ Word32##TurboshaftName(Map(node->left_input()), right));   \
    return maglev::ProcessResult::kContinue;                                   \
  }
  PROCESS_INT32_SHIFT(ShiftLeft, ShiftLeft)
  PROCESS_INT32_SHIFT(ShiftRight, ShiftRightArithmetic)
  PROCESS_INT32_SHIFT(ShiftRightLogical, ShiftRightLogical)
#undef PROCESS_INT32_SHIFT

  maglev::ProcessResult Process(maglev::Int32BitwiseNot* node,
                                const maglev::ProcessingState& state) {
    // Turboshaft doesn't have a bitwise Not operator; we instead use "^ -1".
    SetMap(node, __ Word32BitwiseXor(Map(node->value_input()),
                                     __ Word32Constant(-1)));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32AbsWithOverflow* node,
                                const maglev::ProcessingState& state) {
    V<Word32> input = Map(node->input());
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    ScopedVariable<Word32, AssemblerT> result(this, input);

    IF (__ Int32LessThan(input, 0)) {
      V<Tuple<Word32, Word32>> result_with_ovf =
          __ Int32MulCheckOverflow(input, -1);
      __ DeoptimizeIf(__ Projection<1>(result_with_ovf), frame_state,
                      DeoptimizeReason::kOverflow,
                      node->eager_deopt_info()->feedback_to_update());
      result = __ Projection<0>(result_with_ovf);
    }

    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Float64Negate* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Float64Negate(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64Abs* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Float64Abs(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64Round* node,
                                const maglev::ProcessingState& state) {
    if (node->kind() == maglev::Float64Round::Kind::kFloor) {
      SetMap(node, __ Float64RoundDown(Map(node->input())));
    } else if (node->kind() == maglev::Float64Round::Kind::kCeil) {
      SetMap(node, __ Float64RoundUp(Map(node->input())));
    } else {
      DCHECK_EQ(node->kind(), maglev::Float64Round::Kind::kNearest);
      // Nearest rounds to +infinity on ties. We emulate this by rounding up and
      // adjusting if the difference exceeds 0.5 (like SimplifiedLowering does
      // for lower Float64Round).
      OpIndex input = Map(node->input());
      ScopedVariable<Float64, AssemblerT> result(this,
                                                 __ Float64RoundUp(input));
      IF_NOT (__ Float64LessThanOrEqual(__ Float64Sub(result, 0.5), input)) {
        result = __ Float64Sub(result, 1.0);
      }

      SetMap(node, result);
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Float64Ieee754Unary* node,
                                const maglev::ProcessingState& state) {
    FloatUnaryOp::Kind kind;
    switch (node->ieee_function()) {
#define CASE(MathName, ExpName, EnumName)                         \
  case maglev::Float64Ieee754Unary::Ieee754Function::k##EnumName: \
    kind = FloatUnaryOp::Kind::k##EnumName;                       \
    break;
      IEEE_754_UNARY_LIST(CASE)
#undef CASE
    }
    SetMap(node, __ Float64Unary(Map(node->input()), kind));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckedSmiIncrement* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    V<Smi> result;
    if constexpr (SmiValuesAre31Bits()) {
      result = __ BitcastWord32ToSmi(__ Word32SignedAddDeoptOnOverflow(
          __ BitcastSmiToWord32(Map(node->value_input())),
          Smi::FromInt(1).ptr(), frame_state,
          node->eager_deopt_info()->feedback_to_update()));
    } else {
      // Remember that 32-bit Smis are stored in the upper 32 bits of 64-bit
      // qwords. We thus perform a 64-bit addition rather than a 32-bit one,
      // despite Smis being only 32 bits.
      result = __ BitcastWordPtrToSmi(__ WordPtrSignedAddDeoptOnOverflow(
          __ BitcastSmiToWordPtr(Map(node->value_input())),
          Smi::FromInt(1).ptr(), frame_state,
          node->eager_deopt_info()->feedback_to_update()));
    }
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedSmiDecrement* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    V<Smi> result;
    if constexpr (SmiValuesAre31Bits()) {
      result = __ BitcastWord32ToSmi(__ Word32SignedSubDeoptOnOverflow(
          __ BitcastSmiToWord32(Map(node->value_input())),
          Smi::FromInt(1).ptr(), frame_state,
          node->eager_deopt_info()->feedback_to_update()));
    } else {
      result = __ BitcastWordPtrToSmi(__ WordPtrSignedSubDeoptOnOverflow(
          __ BitcastSmiToWordPtr(Map(node->value_input())),
          Smi::FromInt(1).ptr(), frame_state,
          node->eager_deopt_info()->feedback_to_update()));
    }
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }

// Note that Maglev collects feedback in the generic binops and unops, so that
// Turbofan has chance to get better feedback. However, once we reach Turbofan,
// we stop collecting feedback, since we've tried multiple times to keep
// collecting feedback in Turbofan, but it never seemed worth it. The latest
// occurence of this was ended by this CL: https://crrev.com/c/4110858.
#define PROCESS_GENERIC_BINOP(Name)                                            \
  maglev::ProcessResult Process(maglev::Generic##Name* node,                   \
                                const maglev::ProcessingState& state) {        \
    ThrowingScope throwing_scope(this, node);                                  \
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());         \
    SetMap(node,                                                               \
           __ Generic##Name(Map(node->left_input()), Map(node->right_input()), \
                            frame_state, native_context(),                     \
                            ShouldLazyDeoptOnThrow(node)));                    \
    return maglev::ProcessResult::kContinue;                                   \
  }
  GENERIC_BINOP_LIST(PROCESS_GENERIC_BINOP)
#undef PROCESS_GENERIC_BINOP

#define PROCESS_GENERIC_UNOP(Name)                                            \
  maglev::ProcessResult Process(maglev::Generic##Name* node,                  \
                                const maglev::ProcessingState& state) {       \
    ThrowingScope throwing_scope(this, node);                                 \
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());        \
    SetMap(node,                                                              \
           __ Generic##Name(Map(node->operand_input()), frame_state,          \
                            native_context(), ShouldLazyDeoptOnThrow(node))); \
    return maglev::ProcessResult::kContinue;                                  \
  }
  GENERIC_UNOP_LIST(PROCESS_GENERIC_UNOP)
#undef PROCESS_GENERIC_UNOP

  maglev::ProcessResult Process(maglev::ToNumberOrNumeric* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    SetMap(node, __ ToNumberOrNumeric(Map(node->value_input()), frame_state,
                                      native_context(), node->mode(),
                                      ShouldLazyDeoptOnThrow(node)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LogicalNot* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = __ TaggedEqual(
        Map(node->value()), __ HeapConstant(local_factory_->true_value()));
    SetMap(node, ConvertWord32ToJSBool(condition, /*flip*/ true));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ToBooleanLogicalNot* node,
                                const maglev::ProcessingState& state) {
    TruncateJSPrimitiveToUntaggedOp::InputAssumptions assumption =
        node->check_type() == maglev::CheckType::kCheckHeapObject
            ? TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject
            : TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kHeapObject;
    V<Word32> condition = ToBit(node->value(), assumption);
    SetMap(node, ConvertWord32ToJSBool(condition, /*flip*/ true));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ToBoolean* node,
                                const maglev::ProcessingState& state) {
    TruncateJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions;
    switch (node->check_type()) {
      case maglev::CheckType::kCheckHeapObject:
        input_assumptions =
            TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject;
        break;
      case maglev::CheckType::kOmitHeapObjectCheck:
        input_assumptions =
            TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kHeapObject;
        break;
    }
    SetMap(node,
           ConvertWord32ToJSBool(ToBit(node->value(), input_assumptions)));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32ToBoolean* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, ConvertWord32ToJSBool(Map(node->value()), node->flip()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64ToBoolean* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = Float64ToBit(Map(node->value()));
    SetMap(node, ConvertWord32ToJSBool(condition, node->flip()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32ToNumber* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ConvertInt32ToNumber(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Uint32ToNumber* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ConvertUint32ToNumber(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64ToTagged* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, Float64ToTagged(Map(node->input()), node->conversion_mode()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::HoleyFloat64ToTagged* node,
                                const maglev::ProcessingState& state) {
    SetMap(node,
           HoleyFloat64ToTagged(Map(node->input()), node->conversion_mode()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64ToHeapNumberForField* node,
                                const maglev::ProcessingState& state) {
    // We don't use ConvertUntaggedToJSPrimitive but instead the lower level
    // AllocateHeapNumberWithValue helper, because ConvertUntaggedToJSPrimitive
    // can be GVNed, which we don't want for Float64ToHeapNumberForField, since
    // it creates a mutable HeapNumber, that will then be owned by an object
    // field.
    SetMap(node, __ AllocateHeapNumberWithValue(Map(node->input()),
                                                isolate_->factory()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::HoleyFloat64IsHole* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, ConvertWord32ToJSBool(__ Float64IsHole(Map(node->input()))));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckedNumberOrOddballToFloat64* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind kind;
    switch (node->conversion_type()) {
      case maglev::TaggedToFloat64ConversionType::kOnlyNumber:
        kind = ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber;
        break;
      case maglev::TaggedToFloat64ConversionType::kNumberOrBoolean:
        kind = ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
            kNumberOrBoolean;
        break;
      case maglev::TaggedToFloat64ConversionType::kNumberOrOddball:
        kind = ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
            kNumberOrOddball;
        break;
    }
    SetMap(node,
           __ ConvertJSPrimitiveToUntaggedOrDeopt(
               Map(node->input()), frame_state, kind,
               ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kFloat64,
               CheckForMinusZeroMode::kCheckForMinusZero,
               node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::UncheckedNumberOrOddballToFloat64* node,
                                const maglev::ProcessingState& state) {
    // `node->conversion_type()` doesn't matter here, since for both HeapNumbers
    // and Oddballs, the Float64 value is at the same index (and this node never
    // deopts, regardless of its input).
    SetMap(node, __ ConvertJSPrimitiveToUntagged(
                     Map(node->input()),
                     ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kFloat64,
                     ConvertJSPrimitiveToUntaggedOp::InputAssumptions::
                         kNumberOrOddball));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TruncateUint32ToInt32* node,
                                const maglev::ProcessingState& state) {
    // This doesn't matter in Turboshaft: both Uint32 and Int32 are Word32.
    SetMap(node, Map(node->input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedInt32ToUint32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIf(__ Int32LessThan(Map(node->input()), 0), frame_state,
                    DeoptimizeReason::kNotUint32,
                    node->eager_deopt_info()->feedback_to_update());
    SetMap(node, Map(node->input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedUint32ToInt32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ DeoptimizeIf(__ Int32LessThan(Map(node->input()), 0), frame_state,
                    DeoptimizeReason::kNotInt32,
                    node->eager_deopt_info()->feedback_to_update());
    SetMap(node, Map(node->input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::UnsafeInt32ToUint32* node,
                                const maglev::ProcessingState& state) {
    // This is a no-op in Maglev, and also in Turboshaft where both Uint32 and
    // Int32 are Word32.
    SetMap(node, Map(node->input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedObjectToIndex* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    const FeedbackSource& feedback =
        node->eager_deopt_info()->feedback_to_update();
    OpIndex result = __ ConvertJSPrimitiveToUntaggedOrDeopt(
        Map(node->object_input()), frame_state,
        ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumberOrString,
        ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kArrayIndex,
        CheckForMinusZeroMode::kCheckForMinusZero, feedback);
    if constexpr (Is64()) {
      // ArrayIndex is 32-bit in Maglev, but 64 in Turboshaft. This means that
      // we have to convert it to 32-bit before the following `SetMap`, and we
      // thus have to check that it actually fits in a Uint32.
      __ DeoptimizeIfNot(__ Uint64LessThanOrEqual(
                             result, std::numeric_limits<uint32_t>::max()),
                         frame_state, DeoptimizeReason::kNotInt32, feedback);
      RETURN_IF_UNREACHABLE();
    }
    SetMap(node, Is64() ? __ TruncateWord64ToWord32(result) : result);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ChangeInt32ToFloat64* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ChangeInt32ToFloat64(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ChangeUint32ToFloat64* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ChangeUint32ToFloat64(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedTruncateFloat64ToInt32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(node, __ ChangeFloat64ToInt32OrDeopt(
                     Map(node->input()), frame_state,
                     CheckForMinusZeroMode::kCheckForMinusZero,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedTruncateFloat64ToUint32* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(node, __ ChangeFloat64ToUint32OrDeopt(
                     Map(node->input()), frame_state,
                     CheckForMinusZeroMode::kCheckForMinusZero,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::CheckedTruncateNumberOrOddballToInt32* node,
      const maglev::ProcessingState& state) {
    TruncateJSPrimitiveToUntaggedOrDeoptOp::InputRequirement input_requirement;
    switch (node->conversion_type()) {
      case maglev::TaggedToFloat64ConversionType::kOnlyNumber:
        input_requirement =
            TruncateJSPrimitiveToUntaggedOrDeoptOp::InputRequirement::kNumber;
        break;
      case maglev::TaggedToFloat64ConversionType::kNumberOrBoolean:
        input_requirement = TruncateJSPrimitiveToUntaggedOrDeoptOp::
            InputRequirement::kNumberOrBoolean;
        break;
      case maglev::TaggedToFloat64ConversionType::kNumberOrOddball:
        input_requirement = TruncateJSPrimitiveToUntaggedOrDeoptOp::
            InputRequirement::kNumberOrOddball;
        break;
    }
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    SetMap(
        node,
        __ TruncateJSPrimitiveToUntaggedOrDeopt(
            Map(node->input()), frame_state,
            TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32,
            input_requirement, node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TruncateNumberOrOddballToInt32* node,
                                const maglev::ProcessingState& state) {
    // In Maglev, TruncateNumberOrOddballToInt32 does the same thing for both
    // NumberOrOddball and Number; except when debug_code is enabled: then,
    // Maglev inserts runtime checks ensuring that the input is indeed a Number
    // or NumberOrOddball. Turboshaft doesn't typically introduce such runtime
    // checks, so we instead just lower both Number and NumberOrOddball to the
    // NumberOrOddball variant.
    SetMap(node, __ TruncateJSPrimitiveToUntagged(
                     Map(node->input()),
                     TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kInt32,
                     TruncateJSPrimitiveToUntaggedOp::InputAssumptions::
                         kNumberOrOddball));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TruncateFloat64ToInt32* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ JSTruncateFloat64ToWord32(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::HoleyFloat64ToMaybeNanFloat64* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Float64SilenceNaN(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedHoleyFloat64ToFloat64* node,
                                const maglev::ProcessingState& state) {
    V<Float64> input = Map(node->input());
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());

    __ DeoptimizeIf(__ Float64IsHole(input), frame_state,
                    DeoptimizeReason::kHole,
                    node->eager_deopt_info()->feedback_to_update());

    SetMap(node, input);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ConvertHoleToUndefined* node,
                                const maglev::ProcessingState& state) {
    V<Word32> cond = RootEqual(node->object_input(), RootIndex::kTheHoleValue);
    SetMap(node,
           __ Select(cond, __ HeapConstant(local_factory_->undefined_value()),
                     Map<Object>(node->object_input()),
                     RegisterRepresentation::Tagged(), BranchHint::kNone,
                     SelectOp::Implementation::kBranch));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ConvertReceiver* node,
                                const maglev::ProcessingState& state) {
    NoThrowingScopeRequired no_throws(node);

    Label<Object> done(this);
    Label<> non_js_receiver(this);
    V<Object> receiver = Map(node->receiver_input());

    GOTO_IF(__ IsSmi(receiver), non_js_receiver);

    GOTO_IF(__ JSAnyIsNotPrimitive(V<HeapObject>::Cast(receiver)), done,
            receiver);

    if (node->mode() != ConvertReceiverMode::kNotNullOrUndefined) {
      Label<> convert_global_proxy(this);
      GOTO_IF(__ RootEqual(receiver, RootIndex::kUndefinedValue, isolate_),
              convert_global_proxy);
      GOTO_IF_NOT(__ RootEqual(receiver, RootIndex::kNullValue, isolate_),
                  non_js_receiver);
      GOTO(convert_global_proxy);
      BIND(convert_global_proxy);
      GOTO(done,
           __ HeapConstant(
               node->native_context().global_proxy_object(broker_).object()));
    } else {
      GOTO(non_js_receiver);
    }

    BIND(non_js_receiver);
    GOTO(done, __ CallBuiltin_ToObject(
                   isolate_, __ HeapConstant(node->native_context().object()),
                   V<JSPrimitive>::Cast(receiver)));

    BIND(done, result);
    SetMap(node, result);

    return maglev::ProcessResult::kContinue;
  }

  static constexpr int kMinClampedUint8 = 0;
  static constexpr int kMaxClampedUint8 = 255;
  V<Word32> Int32ToUint8Clamped(V<Word32> value) {
    ScopedVariable<Word32, AssemblerT> result(this);
    IF (__ Int32LessThan(value, kMinClampedUint8)) {
      result = __ Word32Constant(kMinClampedUint8);
    } ELSE IF (__ Int32LessThan(value, kMaxClampedUint8)) {
      result = value;
    } ELSE {
      result = __ Word32Constant(kMaxClampedUint8);
    }
    return result;
  }
  V<Word32> Float64ToUint8Clamped(V<Float64> value) {
    ScopedVariable<Word32, AssemblerT> result(this);
    IF (__ Float64LessThan(value, kMinClampedUint8)) {
      result = __ Word32Constant(kMinClampedUint8);
    } ELSE IF (__ Float64LessThan(kMaxClampedUint8, value)) {
      result = __ Word32Constant(kMaxClampedUint8);
    } ELSE {
      // Note that this case handles values that are in range of Clamped Uint8
      // and NaN. The order of the IF/ELSE-IF/ELSE in this function is so that
      // we do indeed end up here for NaN.
      result = __ JSTruncateFloat64ToWord32(__ Float64RoundTiesEven(value));
    }
    return result;
  }

  maglev::ProcessResult Process(maglev::Int32ToUint8Clamped* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, Int32ToUint8Clamped(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Uint32ToUint8Clamped* node,
                                const maglev::ProcessingState& state) {
    ScopedVariable<Word32, AssemblerT> result(this);
    V<Word32> value = Map(node->input());
    IF (__ Uint32LessThan(value, kMaxClampedUint8)) {
      result = value;
    } ELSE {
      result = __ Word32Constant(kMaxClampedUint8);
    }
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64ToUint8Clamped* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, Float64ToUint8Clamped(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedNumberToUint8Clamped* node,
                                const maglev::ProcessingState& state) {
    ScopedVariable<Word32, AssemblerT> result(this);
    V<Object> value = Map(node->input());
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    IF (__ IsSmi(value)) {
      result = Int32ToUint8Clamped(__ UntagSmi(V<Smi>::Cast(value)));
    } ELSE {
      V<i::Map> map = __ LoadMapField(value);
      __ DeoptimizeIfNot(
          __ TaggedEqual(map,
                         __ HeapConstant(local_factory_->heap_number_map())),
          frame_state, DeoptimizeReason::kNotAHeapNumber,
          node->eager_deopt_info()->feedback_to_update());
      result = Float64ToUint8Clamped(
          __ LoadHeapNumberValue(V<HeapNumber>::Cast(value)));
    }
    RETURN_IF_UNREACHABLE();
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ToObject* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    OpIndex arguments[] = {Map(node->value_input()), Map(node->context())};

    SetMap(node, GenerateBuiltinCall(node, Builtin::kToObject, frame_state,
                                     base::VectorOf(arguments)));

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Return* node,
                                const maglev::ProcessingState& state) {
    __ Return(Map(node->value_input()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Deopt* node,
                                const maglev::ProcessingState& state) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->eager_deopt_info());
    __ Deoptimize(frame_state, node->reason(),
                  node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::SetPendingMessage* node,
                                const maglev::ProcessingState& state) {
    V<WordPtr> message_address = __ ExternalConstant(
        ExternalReference::address_of_pending_message(isolate_));
    V<Object> old_message = __ LoadMessage(message_address);
    __ StoreMessage(message_address, Map(node->value()));
    SetMap(node, old_message);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::GeneratorStore* node,
                                const maglev::ProcessingState& state) {
    base::SmallVector<OpIndex, 32> parameters_and_registers;
    int num_parameters_and_registers = node->num_parameters_and_registers();
    for (int i = 0; i < num_parameters_and_registers; i++) {
      parameters_and_registers.push_back(
          Map(node->parameters_and_registers(i)));
    }
    __ GeneratorStore(Map(node->context_input()), Map(node->generator_input()),
                      parameters_and_registers, node->suspend_id(),
                      node->bytecode_offset());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::GeneratorRestoreRegister* node,
                                const maglev::ProcessingState& state) {
    V<FixedArray> array = Map(node->array_input());
    V<Object> result =
        __ LoadTaggedField(array, FixedArray::OffsetOfElementAt(node->index()));
    __ Store(array, Map(node->stale_input()), StoreOp::Kind::TaggedBase(),
             MemoryRepresentation::TaggedSigned(),
             WriteBarrierKind::kNoWriteBarrier,
             FixedArray::OffsetOfElementAt(node->index()));

    SetMap(node, result);

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Abort* node,
                                const maglev::ProcessingState& state) {
    __ RuntimeAbort(node->reason());
    // TODO(dmercadier): remove this `Unreachable` once RuntimeAbort is marked
    // as a block terminator.
    __ Unreachable();
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Identity* node,
                                const maglev::ProcessingState&) {
    SetMap(node, Map(node->input(0)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Dead*, const maglev::ProcessingState&) {
    // Nothing to do; `Dead` is in Maglev to kill a node when removing it
    // directly from the graph is not possible.
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::DebugBreak*,
                                const maglev::ProcessingState&) {
    __ DebugBreak();
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::GapMove*,
                                const maglev::ProcessingState&) {
    // GapMove nodes are created by Maglev's register allocator, which
    // doesn't run when using Maglev as a frontend for Turboshaft.
    UNREACHABLE();
  }
  maglev::ProcessResult Process(maglev::ConstantGapMove*,
                                const maglev::ProcessingState&) {
    // ConstantGapMove nodes are created by Maglev's register allocator, which
    // doesn't run when using Maglev as a frontend for Turboshaft.
    UNREACHABLE();
  }

  maglev::ProcessResult Process(maglev::VirtualObject*,
                                const maglev::ProcessingState&) {
    // VirtualObjects should never be part of the Maglev graph.
    UNREACHABLE();
  }

  maglev::ProcessResult Process(maglev::GetSecondReturnedValue* node,
                                const maglev::ProcessingState& state) {
    DCHECK(second_return_value_.valid());
    SetMap(node, second_return_value_);

#ifdef DEBUG
    second_return_value_ = V<Object>::Invalid();
#endif

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::TryOnStackReplacement*,
                                const maglev::ProcessingState&) {
    // Turboshaft is the top tier compiler, so we never need to OSR from it.
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ReduceInterruptBudgetForReturn*,
                                const maglev::ProcessingState&) {
    // No need to update the interrupt budget once we reach Turboshaft.
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ReduceInterruptBudgetForLoop* node,
                                const maglev::ProcessingState&) {
    // ReduceInterruptBudgetForLoop nodes are not emitted by Maglev when it is
    // used as a frontend for Turboshaft.
    UNREACHABLE();
  }
  maglev::ProcessResult Process(maglev::HandleNoHeapWritesInterrupt* node,
                                const maglev::ProcessingState&) {
    GET_FRAME_STATE_MAYBE_ABORT(frame_state, node->lazy_deopt_info());
    __ JSLoopStackCheck(native_context(), frame_state);
    return maglev::ProcessResult::kContinue;
  }

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  maglev::ProcessResult Process(
      maglev::GetContinuationPreservedEmbedderData* node,
      const maglev::ProcessingState&) {
    V<Object> data = __ GetContinuationPreservedEmbedderData();
    SetMap(node, data);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(
      maglev::SetContinuationPreservedEmbedderData* node,
      const maglev::ProcessingState&) {
    V<Object> data = Map(node->input(0));
    __ SetContinuationPreservedEmbedderData(data);
    return maglev::ProcessResult::kContinue;
  }
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

  maglev::ProcessResult Process(maglev::AssertInt32* node,
                                const maglev::ProcessingState&) {
    bool negate_result = false;
    V<Word32> cmp = ConvertInt32Compare(node->left_input(), node->right_input(),
                                        node->condition(), &negate_result);
    Label<> abort(this);
    Label<> end(this);
    if (negate_result) {
      GOTO_IF(cmp, abort);
    } else {
      GOTO_IF_NOT(cmp, abort);
    }
    GOTO(end);

    BIND(abort);
    __ RuntimeAbort(node->reason());
    __ Unreachable();

    BIND(end);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CallSelf*,
                                const maglev::ProcessingState&) {
    // CallSelf nodes are only created when Maglev is the top-tier compiler
    // (which can't be the case here, since we're currently compiling for
    // Turboshaft).
    UNREACHABLE();
  }

  // Nodes unused by maglev but still existing.
  maglev::ProcessResult Process(maglev::ExternalConstant*,
                                const maglev::ProcessingState&) {
    UNREACHABLE();
  }
  maglev::ProcessResult Process(maglev::CheckUint32IsSmi*,
                                const maglev::ProcessingState&) {
    UNREACHABLE();
  }
  maglev::ProcessResult Process(maglev::CallCPPBuiltin*,
                                const maglev::ProcessingState&) {
    UNREACHABLE();
  }
  maglev::ProcessResult Process(maglev::UnsafeTruncateUint32ToInt32*,
                                const maglev::ProcessingState&) {
    UNREACHABLE();
  }
  maglev::ProcessResult Process(maglev::UnsafeTruncateFloat64ToInt32*,
                                const maglev::ProcessingState&) {
    UNREACHABLE();
  }
  maglev::ProcessResult Process(maglev::BranchIfTypeOf*,
                                const maglev::ProcessingState&) {
    UNREACHABLE();
  }

  AssemblerT& Asm() { return assembler_; }
  Zone* temp_zone() { return temp_zone_; }
  Zone* graph_zone() { return Asm().output_graph().graph_zone(); }

 private:
  OptionalV<FrameState> BuildFrameState(
      maglev::EagerDeoptInfo* eager_deopt_info) {
    deduplicator_.Reset();
    // Eager deopts don't have a result location/size.
    const interpreter::Register result_location =
        interpreter::Register::invalid_value();
    const int result_size = 0;

    switch (eager_deopt_info->top_frame().type()) {
      case maglev::DeoptFrame::FrameType::kInterpretedFrame:
        return BuildFrameState(eager_deopt_info->top_frame().as_interpreted(),
                               result_location, result_size);
      case maglev::DeoptFrame::FrameType::kBuiltinContinuationFrame:
        return BuildFrameState(
            eager_deopt_info->top_frame().as_builtin_continuation());
      case maglev::DeoptFrame::FrameType::kInlinedArgumentsFrame:
      case maglev::DeoptFrame::FrameType::kConstructInvokeStubFrame:
        UNIMPLEMENTED();
    }
  }

  OptionalV<FrameState> BuildFrameState(
      maglev::LazyDeoptInfo* lazy_deopt_info) {
    deduplicator_.Reset();
    switch (lazy_deopt_info->top_frame().type()) {
      case maglev::DeoptFrame::FrameType::kInterpretedFrame:
        return BuildFrameState(lazy_deopt_info->top_frame().as_interpreted(),
                               lazy_deopt_info->result_location(),
                               lazy_deopt_info->result_size());
      case maglev::DeoptFrame::FrameType::kConstructInvokeStubFrame:
        return BuildFrameState(
            lazy_deopt_info->top_frame().as_construct_stub());

      case maglev::DeoptFrame::FrameType::kBuiltinContinuationFrame:
        return BuildFrameState(
            lazy_deopt_info->top_frame().as_builtin_continuation());

      case maglev::DeoptFrame::FrameType::kInlinedArgumentsFrame:
        UNIMPLEMENTED();
    }
  }

  OptionalV<FrameState> BuildParentFrameState(maglev::DeoptFrame& frame) {
    // Only the topmost frame should have a valid result_location and
    // result_size. One reason for this is that, in Maglev, the PokeAt is not an
    // attribute of the DeoptFrame but rather of the LazyDeoptInfo (to which the
    // topmost frame is attached).
    const interpreter::Register result_location =
        interpreter::Register::invalid_value();
    const int result_size = 0;

    switch (frame.type()) {
      case maglev::DeoptFrame::FrameType::kInterpretedFrame:
        return BuildFrameState(frame.as_interpreted(), result_location,
                               result_size);
      case maglev::DeoptFrame::FrameType::kConstructInvokeStubFrame:
        return BuildFrameState(frame.as_construct_stub());
      case maglev::DeoptFrame::FrameType::kInlinedArgumentsFrame:
        return BuildFrameState(frame.as_inlined_arguments());
      case maglev::DeoptFrame::FrameType::kBuiltinContinuationFrame:
        return BuildFrameState(frame.as_builtin_continuation());
    }
  }

  OptionalV<FrameState> BuildFrameState(
      maglev::ConstructInvokeStubDeoptFrame& frame) {
    FrameStateData::Builder builder;
    if (frame.parent() != nullptr) {
      OptionalV<FrameState> parent_frame =
          BuildParentFrameState(*frame.parent());
      if (!parent_frame.has_value()) return OptionalV<FrameState>::Nullopt();
      builder.AddParentFrameState(parent_frame.value());
    }

    const maglev::VirtualObject::List& virtual_objects =
        maglev::GetVirtualObjects(frame);

    // Closure
    // TODO(dmercadier): ConstructInvokeStub frames don't have a Closure input,
    // but the instruction selector assumes that they do and that it should be
    // skipped. We thus use SmiConstant(0) as a fake Closure input here, but it
    // would be nicer to fix the instruction selector to not require this input
    // at all for such frames.
    V<Any> fake_closure_input = __ SmiConstant(0);
    builder.AddInput(MachineType::AnyTagged(), fake_closure_input);

    // Parameters
    AddDeoptInput(builder, virtual_objects, frame.receiver());

    // Context
    AddDeoptInput(builder, virtual_objects, frame.context());

    if (builder.Inputs().size() >
        std::numeric_limits<decltype(Operation::input_count)>::max() - 1) {
      *bailout_ = BailoutReason::kTooManyArguments;
      return OptionalV<FrameState>::Nullopt();
    }

    const FrameStateInfo* frame_state_info = MakeFrameStateInfo(frame);
    return __ FrameState(
        builder.Inputs(), builder.inlined(),
        builder.AllocateFrameStateData(*frame_state_info, graph_zone()));
  }

  OptionalV<FrameState> BuildFrameState(
      maglev::InlinedArgumentsDeoptFrame& frame) {
    FrameStateData::Builder builder;
    if (frame.parent() != nullptr) {
      OptionalV<FrameState> parent_frame =
          BuildParentFrameState(*frame.parent());
      if (!parent_frame.has_value()) return OptionalV<FrameState>::Nullopt();
      builder.AddParentFrameState(parent_frame.value());
    }

    const maglev::VirtualObject::List& virtual_objects =
        maglev::GetVirtualObjects(frame);

    // Closure
    AddDeoptInput(builder, virtual_objects, frame.closure());

    // Parameters
    for (const maglev::ValueNode* arg : frame.arguments()) {
      AddDeoptInput(builder, virtual_objects, arg);
    }

    // Context
    // TODO(dmercadier): InlinedExtraArguments frames don't have a Context
    // input, but the instruction selector assumes that they do and that it
    // should be skipped. We thus use SmiConstant(0) as a fake Context input
    // here, but it would be nicer to fix the instruction selector to not
    // require this input at all for such frames.
    V<Any> fake_context_input = __ SmiConstant(0);
    builder.AddInput(MachineType::AnyTagged(), fake_context_input);

    if (builder.Inputs().size() >
        std::numeric_limits<decltype(Operation::input_count)>::max() - 1) {
      *bailout_ = BailoutReason::kTooManyArguments;
      return OptionalV<FrameState>::Nullopt();
    }

    const FrameStateInfo* frame_state_info = MakeFrameStateInfo(frame);
    return __ FrameState(
        builder.Inputs(), builder.inlined(),
        builder.AllocateFrameStateData(*frame_state_info, graph_zone()));
  }

  OptionalV<FrameState> BuildFrameState(
      maglev::BuiltinContinuationDeoptFrame& frame) {
    FrameStateData::Builder builder;
    if (frame.parent() != nullptr) {
      OptionalV<FrameState> parent_frame =
          BuildParentFrameState(*frame.parent());
      if (!parent_frame.has_value()) return OptionalV<FrameState>::Nullopt();
      builder.AddParentFrameState(parent_frame.value());
    }

    const maglev::VirtualObject::List& virtual_objects =
        maglev::GetVirtualObjects(frame);

    // Closure
    if (frame.is_javascript()) {
      builder.AddInput(MachineType::AnyTagged(),
                       __ HeapConstant(frame.javascript_target().object()));
    } else {
      builder.AddUnusedRegister();
    }

    // Parameters
    for (maglev::ValueNode* param : frame.parameters()) {
      AddDeoptInput(builder, virtual_objects, param);
    }

    // Extra fixed JS frame parameters. These are at the end since JS builtins
    // push their parameters in reverse order.
    constexpr int kExtraFixedJSFrameParameters = 3;
    if (frame.is_javascript()) {
      static_assert(kExtraFixedJSFrameParameters == 3);
      // kJavaScriptCallTargetRegister
      builder.AddInput(MachineType::AnyTagged(),
                       __ HeapConstant(frame.javascript_target().object()));
      // kJavaScriptCallNewTargetRegister
      builder.AddInput(MachineType::AnyTagged(),
                       __ HeapConstant(local_factory_->undefined_value()));
      // kJavaScriptCallArgCountRegister
      builder.AddInput(
          MachineType::AnyTagged(),
          __ SmiConstant(Smi::FromInt(
              Builtins::GetStackParameterCount(frame.builtin_id()))));
    }

    // Context
    AddDeoptInput(builder, virtual_objects, frame.context());

    if (builder.Inputs().size() >
        std::numeric_limits<decltype(Operation::input_count)>::max() - 1) {
      *bailout_ = BailoutReason::kTooManyArguments;
      return OptionalV<FrameState>::Nullopt();
    }

    const FrameStateInfo* frame_state_info = MakeFrameStateInfo(frame);
    return __ FrameState(
        builder.Inputs(), builder.inlined(),
        builder.AllocateFrameStateData(*frame_state_info, graph_zone()));
  }

  OptionalV<FrameState> BuildFrameState(maglev::InterpretedDeoptFrame& frame,
                                        interpreter::Register result_location,
                                        int result_size) {
    DCHECK_EQ(result_size != 0, result_location.is_valid());
    FrameStateData::Builder builder;

    if (frame.parent() != nullptr) {
      OptionalV<FrameState> parent_frame =
          BuildParentFrameState(*frame.parent());
      if (!parent_frame.has_value()) return OptionalV<FrameState>::Nullopt();
      builder.AddParentFrameState(parent_frame.value());
    }

    const maglev::VirtualObject::List& virtual_objects =
        frame.frame_state()->virtual_objects();

    // Closure
    AddDeoptInput(builder, virtual_objects, frame.closure());

    // Parameters
    frame.frame_state()->ForEachParameter(
        frame.unit(), [&](maglev::ValueNode* value, interpreter::Register reg) {
          AddDeoptInput(builder, virtual_objects, value, reg, result_location,
                        result_size);
        });

    // Context
    AddDeoptInput(builder, virtual_objects,
                  frame.frame_state()->context(frame.unit()));

    // Locals
    // ForEachLocal in Maglev skips over dead registers, but we still need to
    // call AddUnusedRegister on the Turboshaft FrameStateData Builder.
    // {local_index} is used to keep track of such unused registers.
    // Among the variables not included in ForEachLocal is the Accumulator (but
    // this is fine since there is an entry in the state specifically for the
    // accumulator later).
    int local_index = 0;
    frame.frame_state()->ForEachLocal(
        frame.unit(), [&](maglev::ValueNode* value, interpreter::Register reg) {
          while (local_index < reg.index()) {
            builder.AddUnusedRegister();
            local_index++;
          }
          AddDeoptInput(builder, virtual_objects, value, reg, result_location,
                        result_size);
          local_index++;
        });
    for (; local_index < frame.unit().register_count(); local_index++) {
      builder.AddUnusedRegister();
    }

    // Accumulator
    if (frame.frame_state()->liveness()->AccumulatorIsLive()) {
      AddDeoptInput(builder, virtual_objects,
                    frame.frame_state()->accumulator(frame.unit()),
                    interpreter::Register::virtual_accumulator(),
                    result_location, result_size);
    } else {
      builder.AddUnusedRegister();
    }

    OutputFrameStateCombine combine =
        ComputeCombine(frame, result_location, result_size);

    if (builder.Inputs().size() >
        std::numeric_limits<decltype(Operation::input_count)>::max() - 1) {
      *bailout_ = BailoutReason::kTooManyArguments;
      return OptionalV<FrameState>::Nullopt();
    }

    const FrameStateInfo* frame_state_info = MakeFrameStateInfo(frame, combine);
    return __ FrameState(
        builder.Inputs(), builder.inlined(),
        builder.AllocateFrameStateData(*frame_state_info, graph_zone()));
  }

  void AddDeoptInput(FrameStateData::Builder& builder,
                     const maglev::VirtualObject::List& virtual_objects,
                     const maglev::ValueNode* node) {
    if (const maglev::InlinedAllocation* alloc =
            node->TryCast<maglev::InlinedAllocation>()) {
      DCHECK(alloc->HasBeenAnalysed());
      if (alloc->HasBeenElided()) {
        AddVirtualObjectInput(builder, virtual_objects,
                              virtual_objects.FindAllocatedWith(alloc));
        return;
      }
    }
    if (const maglev::Identity* ident_obj = node->TryCast<maglev::Identity>()) {
      // The value_representation of Identity nodes is always Tagged rather than
      // the actual value_representation of their input. We thus bypass identity
      // nodes manually here to get to correct value_representation and thus the
      // correct MachineType.
      node = ident_obj->input(0).node();
      // Identity nodes should not have Identity as input.
      DCHECK(!node->Is<maglev::Identity>());
    }
    builder.AddInput(MachineTypeFor(node->value_representation()), Map(node));
  }

  void AddDeoptInput(FrameStateData::Builder& builder,
                     const maglev::VirtualObject::List& virtual_objects,
                     const maglev::ValueNode* node, interpreter::Register reg,
                     interpreter::Register result_location, int result_size) {
    if (result_location.is_valid() && maglev::LazyDeoptInfo::InReturnValues(
                                          reg, result_location, result_size)) {
      builder.AddUnusedRegister();
    } else {
      AddDeoptInput(builder, virtual_objects, node);
    }
  }

  void AddVirtualObjectInput(FrameStateData::Builder& builder,
                             const maglev::VirtualObject::List& virtual_objects,
                             const maglev::VirtualObject* vobj) {
    if (vobj->type() == maglev::VirtualObject::kHeapNumber) {
      // We need to add HeapNumbers as dematerialized HeapNumbers (rather than
      // simply NumberConstant), because they could be mutable HeapNumber
      // fields, in which case we don't want GVN to merge them.
      constexpr int kNumberOfField = 2;  // map + value
      builder.AddDematerializedObject(deduplicator_.CreateFreshId().id,
                                      kNumberOfField);
      builder.AddInput(MachineType::AnyTagged(),
                       __ HeapConstant(local_factory_->heap_number_map()));
      builder.AddInput(MachineType::Float64(),
                       __ Float64Constant(vobj->number()));
      return;
    }

    Deduplicator::DuplicatedId dup_id = deduplicator_.GetDuplicatedId(vobj);
    if (dup_id.duplicated) {
      builder.AddDematerializedObjectReference(dup_id.id);
      return;
    }
    if (vobj->type() == maglev::VirtualObject::kFixedDoubleArray) {
      constexpr int kMapAndLengthFieldCount = 2;
      uint32_t length = vobj->double_elements_length();
      uint32_t field_count = length + kMapAndLengthFieldCount;
      builder.AddDematerializedObject(dup_id.id, field_count);
      builder.AddInput(
          MachineType::AnyTagged(),
          __ HeapConstantNoHole(local_factory_->fixed_double_array_map()));
      builder.AddInput(MachineType::AnyTagged(),
                       __ SmiConstant(Smi::FromInt(length)));
      FixedDoubleArrayRef elements = vobj->double_elements();
      for (uint32_t i = 0; i < length; i++) {
        i::Float64 value = elements.GetFromImmutableFixedDoubleArray(i);
        if (value.is_hole_nan()) {
          builder.AddInput(
              MachineType::AnyTagged(),
              __ HeapConstantHole(local_factory_->the_hole_value()));
        } else {
          builder.AddInput(MachineType::AnyTagged(),
                           __ NumberConstant(value.get_scalar()));
        }
      }
      return;
    }

    DCHECK_EQ(vobj->type(), maglev::VirtualObject::kDefault);
    constexpr int kMapFieldCount = 1;
    uint32_t field_count = vobj->slot_count() + kMapFieldCount;
    builder.AddDematerializedObject(dup_id.id, field_count);
    builder.AddInput(MachineType::AnyTagged(),
                     __ HeapConstantNoHole(vobj->map().object()));
    for (uint32_t i = 0; i < vobj->slot_count(); i++) {
      AddVirtualObjectNestedValue(builder, virtual_objects,
                                  vobj->get_by_index(i));
    }
  }

  void AddVirtualObjectNestedValue(
      FrameStateData::Builder& builder,
      const maglev::VirtualObject::List& virtual_objects,
      const maglev::ValueNode* value) {
    if (maglev::IsConstantNode(value->opcode())) {
      switch (value->opcode()) {
        case maglev::Opcode::kConstant:
          builder.AddInput(
              MachineType::AnyTagged(),
              __ HeapConstant(value->Cast<maglev::Constant>()->ref().object()));
          break;

        case maglev::Opcode::kFloat64Constant:
          builder.AddInput(
              MachineType::AnyTagged(),
              __ NumberConstant(value->Cast<maglev::Float64Constant>()
                                    ->value()
                                    .get_scalar()));
          break;

        case maglev::Opcode::kInt32Constant:
          builder.AddInput(
              MachineType::AnyTagged(),
              __ NumberConstant(value->Cast<maglev::Int32Constant>()->value()));
          break;

        case maglev::Opcode::kUint32Constant:
          builder.AddInput(MachineType::AnyTagged(),
                           __ NumberConstant(
                               value->Cast<maglev::Uint32Constant>()->value()));
          break;

        case maglev::Opcode::kRootConstant:
          builder.AddInput(
              MachineType::AnyTagged(),
              (__ HeapConstant(Cast<HeapObject>(isolate_->root_handle(
                  value->Cast<maglev::RootConstant>()->index())))));
          break;

        case maglev::Opcode::kSmiConstant:
          builder.AddInput(
              MachineType::AnyTagged(),
              __ SmiConstant(value->Cast<maglev::SmiConstant>()->value()));
          break;

        case maglev::Opcode::kTrustedConstant:
          builder.AddInput(
              MachineType::AnyTagged(),
              __ TrustedHeapConstant(
                  value->Cast<maglev::TrustedConstant>()->object().object()));
          break;

        case maglev::Opcode::kTaggedIndexConstant:
        case maglev::Opcode::kExternalConstant:
        default:
          UNREACHABLE();
      }
      return;
    }

    // Special nodes.
    switch (value->opcode()) {
      case maglev::Opcode::kArgumentsElements:
        builder.AddArgumentsElements(
            value->Cast<maglev::ArgumentsElements>()->type());
        break;
      case maglev::Opcode::kArgumentsLength:
        builder.AddArgumentsLength();
        break;
      case maglev::Opcode::kRestLength:
        builder.AddRestLength();
        break;
      case maglev::Opcode::kVirtualObject:
        UNREACHABLE();
      default:
        AddDeoptInput(builder, virtual_objects, value);
        break;
    }
  }

  class Deduplicator {
   public:
    struct DuplicatedId {
      uint32_t id;
      bool duplicated;
    };
    DuplicatedId GetDuplicatedId(const maglev::VirtualObject* object) {
      // TODO(dmercadier): do better than a linear search here.
      for (uint32_t idx = 0; idx < object_ids_.size(); idx++) {
        if (object_ids_[idx] == object) {
          return {idx, true};
        }
      }
      object_ids_.push_back(object);
      return {next_id_++, false};
    }

    DuplicatedId CreateFreshId() { return {next_id_++, false}; }

    void Reset() {
      object_ids_.clear();
      next_id_ = 0;
    }

    static const uint32_t kNotDuplicated = -1;

   private:
    std::vector<const maglev::VirtualObject*> object_ids_{10};
    uint32_t next_id_ = 0;
  };

  OutputFrameStateCombine ComputeCombine(maglev::InterpretedDeoptFrame& frame,
                                         interpreter::Register result_location,
                                         int result_size) {
    if (result_size == 0) {
      return OutputFrameStateCombine::Ignore();
    }
    return OutputFrameStateCombine::PokeAt(
        frame.ComputeReturnOffset(result_location, result_size));
  }

  const FrameStateInfo* MakeFrameStateInfo(
      maglev::InterpretedDeoptFrame& maglev_frame,
      OutputFrameStateCombine combine) {
    FrameStateType type = FrameStateType::kUnoptimizedFunction;
    uint16_t parameter_count = maglev_frame.unit().parameter_count();
    uint16_t max_arguments = maglev_frame.unit().max_arguments();
    int local_count = maglev_frame.unit().register_count();
    Handle<SharedFunctionInfo> shared_info =
        maglev_frame.unit().shared_function_info().object();
    FrameStateFunctionInfo* info = graph_zone()->New<FrameStateFunctionInfo>(
        type, parameter_count, max_arguments, local_count, shared_info);

    return graph_zone()->New<FrameStateInfo>(maglev_frame.bytecode_position(),
                                             combine, info);
  }

  const FrameStateInfo* MakeFrameStateInfo(
      maglev::InlinedArgumentsDeoptFrame& maglev_frame) {
    FrameStateType type = FrameStateType::kInlinedExtraArguments;
    uint16_t parameter_count =
        static_cast<uint16_t>(maglev_frame.arguments().size());
    uint16_t max_arguments = 0;
    int local_count = 0;
    Handle<SharedFunctionInfo> shared_info =
        maglev_frame.unit().shared_function_info().object();
    FrameStateFunctionInfo* info = graph_zone()->New<FrameStateFunctionInfo>(
        type, parameter_count, max_arguments, local_count, shared_info);

    return graph_zone()->New<FrameStateInfo>(maglev_frame.bytecode_position(),
                                             OutputFrameStateCombine::Ignore(),
                                             info);
  }

  const FrameStateInfo* MakeFrameStateInfo(
      maglev::ConstructInvokeStubDeoptFrame& maglev_frame) {
    FrameStateType type = FrameStateType::kConstructInvokeStub;
    Handle<SharedFunctionInfo> shared_info =
        maglev_frame.unit().shared_function_info().object();
    constexpr uint16_t kParameterCount = 1;  // Only 1 parameter: the receiver.
    constexpr uint16_t kMaxArguments = 0;
    constexpr int kLocalCount = 0;
    FrameStateFunctionInfo* info = graph_zone()->New<FrameStateFunctionInfo>(
        type, kParameterCount, kMaxArguments, kLocalCount, shared_info);

    return graph_zone()->New<FrameStateInfo>(
        BytecodeOffset::None(), OutputFrameStateCombine::Ignore(), info);
  }

  const FrameStateInfo* MakeFrameStateInfo(
      maglev::BuiltinContinuationDeoptFrame& maglev_frame) {
    FrameStateType type = maglev_frame.is_javascript()
                              ? FrameStateType::kJavaScriptBuiltinContinuation
                              : FrameStateType::kBuiltinContinuation;
    uint16_t parameter_count =
        static_cast<uint16_t>(maglev_frame.parameters().length());
    constexpr int kExtraFixedJSFrameParameters = 3;
    if (maglev_frame.is_javascript()) {
      parameter_count += kExtraFixedJSFrameParameters;
    }
    Handle<SharedFunctionInfo> shared_info =
        GetSharedFunctionInfo(maglev_frame).object();
    constexpr int kLocalCount = 0;
    constexpr uint16_t kMaxArguments = 0;
    FrameStateFunctionInfo* info = graph_zone()->New<FrameStateFunctionInfo>(
        type, parameter_count, kMaxArguments, kLocalCount, shared_info);

    return graph_zone()->New<FrameStateInfo>(
        Builtins::GetContinuationBytecodeOffset(maglev_frame.builtin_id()),
        OutputFrameStateCombine::Ignore(), info);
  }

  SharedFunctionInfoRef GetSharedFunctionInfo(
      const maglev::DeoptFrame& deopt_frame) {
    switch (deopt_frame.type()) {
      case maglev::DeoptFrame::FrameType::kInterpretedFrame:
        return deopt_frame.as_interpreted().unit().shared_function_info();
      case maglev::DeoptFrame::FrameType::kInlinedArgumentsFrame:
        return deopt_frame.as_inlined_arguments().unit().shared_function_info();
      case maglev::DeoptFrame::FrameType::kConstructInvokeStubFrame:
        return deopt_frame.as_construct_stub().unit().shared_function_info();
      case maglev::DeoptFrame::FrameType::kBuiltinContinuationFrame:
        return GetSharedFunctionInfo(*deopt_frame.parent());
    }
  }

  enum class Sign { kSigned, kUnsigned };
  template <typename rep>
  V<Word32> ConvertCompare(maglev::Input left_input, maglev::Input right_input,
                           ::Operation operation, Sign sign) {
    DCHECK_IMPLIES(
        (std::is_same_v<rep, Float64> || std::is_same_v<rep, Float32>),
        sign == Sign::kSigned);
    ComparisonOp::Kind kind;
    bool swap_inputs = false;
    switch (operation) {
      case ::Operation::kEqual:
      case ::Operation::kStrictEqual:
        kind = ComparisonOp::Kind::kEqual;
        break;
      case ::Operation::kLessThan:
        kind = sign == Sign::kSigned ? ComparisonOp::Kind::kSignedLessThan
                                     : ComparisonOp::Kind::kUnsignedLessThan;
        break;
      case ::Operation::kLessThanOrEqual:
        kind = sign == Sign::kSigned
                   ? ComparisonOp::Kind::kSignedLessThanOrEqual
                   : ComparisonOp::Kind::kUnsignedLessThanOrEqual;
        break;
      case ::Operation::kGreaterThan:
        kind = sign == Sign::kSigned ? ComparisonOp::Kind::kSignedLessThan
                                     : ComparisonOp::Kind::kUnsignedLessThan;
        swap_inputs = true;
        break;
      case ::Operation::kGreaterThanOrEqual:
        kind = sign == Sign::kSigned
                   ? ComparisonOp::Kind::kSignedLessThanOrEqual
                   : ComparisonOp::Kind::kUnsignedLessThanOrEqual;
        swap_inputs = true;
        break;
      default:
        UNREACHABLE();
    }
    V<rep> left = Map(left_input);
    V<rep> right = Map(right_input);
    if (swap_inputs) std::swap(left, right);
    return __ Comparison(left, right, kind, V<rep>::rep);
  }

  V<Word32> ConvertInt32Compare(maglev::Input left_input,
                                maglev::Input right_input,
                                maglev::AssertCondition condition,
                                bool* negate_result) {
    ComparisonOp::Kind kind;
    bool swap_inputs = false;
    switch (condition) {
      case maglev::AssertCondition::kEqual:
        kind = ComparisonOp::Kind::kEqual;
        break;
      case maglev::AssertCondition::kNotEqual:
        kind = ComparisonOp::Kind::kEqual;
        *negate_result = true;
        break;
      case maglev::AssertCondition::kLessThan:
        kind = ComparisonOp::Kind::kSignedLessThan;
        break;
      case maglev::AssertCondition::kLessThanEqual:
        kind = ComparisonOp::Kind::kSignedLessThanOrEqual;
        break;
      case maglev::AssertCondition::kGreaterThan:
        kind = ComparisonOp::Kind::kSignedLessThan;
        swap_inputs = true;
        break;
      case maglev::AssertCondition::kGreaterThanEqual:
        kind = ComparisonOp::Kind::kSignedLessThanOrEqual;
        swap_inputs = true;
        break;
      case maglev::AssertCondition::kUnsignedLessThan:
        kind = ComparisonOp::Kind::kUnsignedLessThan;
        break;
      case maglev::AssertCondition::kUnsignedLessThanEqual:
        kind = ComparisonOp::Kind::kUnsignedLessThanOrEqual;
        break;
      case maglev::AssertCondition::kUnsignedGreaterThan:
        kind = ComparisonOp::Kind::kUnsignedLessThan;
        swap_inputs = true;
        break;
      case maglev::AssertCondition::kUnsignedGreaterThanEqual:
        kind = ComparisonOp::Kind::kUnsignedLessThanOrEqual;
        swap_inputs = true;
        break;
    }
    V<Word32> left = Map(left_input);
    V<Word32> right = Map(right_input);
    if (swap_inputs) std::swap(left, right);
    return __ Comparison(left, right, kind, WordRepresentation::Word32());
  }

  V<Word32> RootEqual(maglev::Input input, RootIndex root) {
    return __ RootEqual(Map(input), root, isolate_);
  }

  void DeoptIfInt32IsNotSmi(maglev::Input maglev_input,
                            V<FrameState> frame_state,
                            const compiler::FeedbackSource& feedback) {
    return DeoptIfInt32IsNotSmi(Map<Word32>(maglev_input), frame_state,
                                feedback);
  }
  void DeoptIfInt32IsNotSmi(V<Word32> input, V<FrameState> frame_state,
                            const compiler::FeedbackSource& feedback) {
    // TODO(dmercadier): is there no higher level way of doing this?
    V<Tuple<Word32, Word32>> add = __ Int32AddCheckOverflow(input, input);
    V<Word32> check = __ template Projection<1>(add);
    __ DeoptimizeIf(check, frame_state, DeoptimizeReason::kNotASmi, feedback);
  }

  std::pair<V<WordPtr>, V<Object>> GetTypedArrayDataAndBasePointers(
      V<JSTypedArray> typed_array) {
    V<WordPtr> data_pointer = __ LoadField<WordPtr>(
        typed_array, AccessBuilder::ForJSTypedArrayExternalPointer());
    V<Object> base_pointer = __ LoadField<Object>(
        typed_array, AccessBuilder::ForJSTypedArrayBasePointer());
    return {data_pointer, base_pointer};
  }
  V<Untagged> BuildTypedArrayLoad(V<JSTypedArray> typed_array, V<Word32> index,
                                  ElementsKind kind) {
    auto [data_pointer, base_pointer] =
        GetTypedArrayDataAndBasePointers(typed_array);
    return __ LoadTypedElement(typed_array, base_pointer, data_pointer,
                               __ ChangeUint32ToUintPtr(index),
                               GetArrayTypeFromElementsKind(kind));
  }
  void BuildTypedArrayStore(V<JSTypedArray> typed_array, V<Word32> index,
                            V<Untagged> value, ElementsKind kind) {
    auto [data_pointer, base_pointer] =
        GetTypedArrayDataAndBasePointers(typed_array);
    __ StoreTypedElement(typed_array, base_pointer, data_pointer,
                         __ ChangeUint32ToUintPtr(index), value,
                         GetArrayTypeFromElementsKind(kind));
  }

  V<Number> Float64ToTagged(
      V<Float64> input,
      maglev::Float64ToTagged::ConversionMode conversion_mode) {
    // Float64ToTagged's conversion mode is used to control whether integer
    // floats should be converted to Smis or to HeapNumbers: kCanonicalizeSmi
    // means that they can be converted to Smis, and otherwise they should
    // remain HeapNumbers.
    ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind kind =
        conversion_mode ==
                maglev::Float64ToTagged::ConversionMode::kCanonicalizeSmi
            ? ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber
            : ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kHeapNumber;
    return V<Number>::Cast(__ ConvertUntaggedToJSPrimitive(
        input, kind, RegisterRepresentation::Float64(),
        ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
        CheckForMinusZeroMode::kCheckForMinusZero));
  }

  V<NumberOrUndefined> HoleyFloat64ToTagged(
      V<Float64> input,
      maglev::HoleyFloat64ToTagged::ConversionMode conversion_mode) {
    Label<NumberOrUndefined> done(this);
    if (conversion_mode ==
        maglev::HoleyFloat64ToTagged::ConversionMode::kCanonicalizeSmi) {
      // ConvertUntaggedToJSPrimitive cannot at the same time canonicalize smis
      // and handle holes. We thus manually insert a smi check when the
      // conversion_mode is CanonicalizeSmi.
      IF (__ Float64IsSmi(input)) {
        V<Word32> as_int32 = __ TruncateFloat64ToInt32OverflowUndefined(input);
        V<Smi> as_smi = V<Smi>::Cast(__ ConvertUntaggedToJSPrimitive(
            as_int32, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kSmi,
            RegisterRepresentation::Word32(),
            ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
            CheckForMinusZeroMode::kDontCheckForMinusZero));
        GOTO(done, as_smi);
      }
    }
    V<NumberOrUndefined> as_obj =
        V<NumberOrUndefined>::Cast(__ ConvertUntaggedToJSPrimitive(
            input,
            ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::
                kHeapNumberOrUndefined,
            RegisterRepresentation::Float64(),
            ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
            CheckForMinusZeroMode::kCheckForMinusZero));
    if (done.has_incoming_jump()) {
      GOTO(done, as_obj);
      BIND(done, result);
      return result;
    } else {
      // Avoid creating a new block if {as_obj} is the only possible return
      // value.
      return as_obj;
    }
  }

  void FixLoopPhis(maglev::BasicBlock* loop) {
    DCHECK(loop->is_loop());
    if (!loop->has_phi()) return;
    for (maglev::Phi* maglev_phi : *loop->phis()) {
      OpIndex phi_index = Map(maglev_phi);
      PendingLoopPhiOp& pending_phi =
          __ output_graph().Get(phi_index).Cast<PendingLoopPhiOp>();
      __ output_graph().Replace<PhiOp>(
          phi_index,
          base::VectorOf(
              {pending_phi.first(), Map(maglev_phi -> backedge_input())}),
          pending_phi.rep);
    }
  }

  RegisterRepresentation RegisterRepresentationFor(
      maglev::ValueRepresentation value_rep) {
    switch (value_rep) {
      case maglev::ValueRepresentation::kTagged:
        return RegisterRepresentation::Tagged();
      case maglev::ValueRepresentation::kInt32:
      case maglev::ValueRepresentation::kUint32:
        return RegisterRepresentation::Word32();
      case maglev::ValueRepresentation::kFloat64:
      case maglev::ValueRepresentation::kHoleyFloat64:
        return RegisterRepresentation::Float64();
      case maglev::ValueRepresentation::kIntPtr:
        return RegisterRepresentation::WordPtr();
    }
  }

  // TODO(dmercadier): Using a Branch would open more optimization opportunities
  // for BranchElimination compared to using a Select. However, in most cases,
  // Maglev should avoid materializing JS booleans, so there is a good chance
  // that it we actually need to do it, it's because we have to, and
  // BranchElimination probably cannot help. Thus, using a Select rather than a
  // Branch leads to smaller graphs, which is generally beneficial. Still, once
  // the graph builder is finished, we should evaluate whether Select or Branch
  // is the best choice here.
  V<Boolean> ConvertWord32ToJSBool(V<Word32> b, bool flip = false) {
    V<Boolean> true_idx = __ HeapConstant(local_factory_->true_value());
    V<Boolean> false_idx = __ HeapConstant(local_factory_->false_value());
    if (flip) std::swap(true_idx, false_idx);
    return __ Select(b, true_idx, false_idx, RegisterRepresentation::Tagged(),
                     BranchHint::kNone, SelectOp::Implementation::kBranch);
  }

  // This function corresponds to MaglevAssembler::ToBoolean.
  V<Word32> ToBit(
      maglev::Input input,
      TruncateJSPrimitiveToUntaggedOp::InputAssumptions assumptions) {
    // TODO(dmercadier): {input} in Maglev is of type Object (like, any
    // HeapObject or Smi). However, the implementation of ToBoolean in Maglev is
    // identical to the lowering of TruncateJSPrimitiveToUntaggedOp(kBit) in
    // Turboshaft (which is done in MachineLoweringReducer), so we're using
    // TruncateJSPrimitiveToUntaggedOp with a non-JSPrimitive input (but it
    // still works). We should avoid doing this to avoid any confusion. Renaming
    // TruncateJSPrimitiveToUntagged to TruncateObjectToUntagged might be the
    // proper fix, in particular because it seems that the Turbofan input to
    // this operation is indeed an Object rather than a JSPrimitive (since
    // we use this operation in the regular TF->TS graph builder to translate
    // TruncateTaggedToBit and TruncateTaggedPointerToBit).
    return V<Word32>::Cast(__ TruncateJSPrimitiveToUntagged(
        Map(input.node()), TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kBit,
        assumptions));
  }

  // Converts a Float64 to a Word32 boolean, correctly producing 0 for NaN, by
  // relying on the fact that "0.0 < abs(x)" is only false for NaN and 0.
  V<Word32> Float64ToBit(V<Float64> input) {
    return __ Float64LessThan(0.0, __ Float64Abs(input));
  }

  LazyDeoptOnThrow ShouldLazyDeoptOnThrow(maglev::NodeBase* node) {
    if (!node->properties().can_throw()) return LazyDeoptOnThrow::kNo;
    const maglev::ExceptionHandlerInfo* info = node->exception_handler_info();
    if (info->ShouldLazyDeopt()) return LazyDeoptOnThrow::kYes;
    return LazyDeoptOnThrow::kNo;
  }

  class ThrowingScope {
    // In Maglev, exception handlers have no predecessors, and their Phis are a
    // bit special: they all correspond to interpreter registers, and get
    // eventually initialized with the value that their predecessors have for
    // the corresponding interpreter registers.

    // In Turboshaft, exception handlers have predecessors and contain regular
    // phis. Creating a ThrowingScope takes care of recording in Variables
    // the current value of interpreter registers (right before emitting a node
    // that can throw), and sets the current_catch_block of the Assembler.
    // Throwing operations that are emitted while the scope is active will
    // automatically be wired to the catch handler. Then, when calling
    // Process(Phi) on exception phis (= when processing the catch handler),
    // these Phis will be mapped to the Variable corresponding to their owning
    // intepreter register.

   public:
    ThrowingScope(GraphBuilder* builder, maglev::NodeBase* throwing_node)
        : builder_(*builder) {
      DCHECK_EQ(__ current_catch_block(), nullptr);
      if (!throwing_node->properties().can_throw()) return;
      const maglev::ExceptionHandlerInfo* handler_info =
          throwing_node->exception_handler_info();
      if (!handler_info->HasExceptionHandler() ||
          handler_info->ShouldLazyDeopt()) {
        return;
      }

      catch_block_ = handler_info->catch_block.block_ptr();

      __ set_current_catch_block(builder_.Map(catch_block_));

      // We now need to prepare recording the inputs for the exception phis of
      // the catch handler.

      if (!catch_block_->has_phi()) {
        // Catch handler doesn't have any Phis, no need to do anything else.
        return;
      }

      const maglev::InterpretedDeoptFrame& interpreted_frame =
          throwing_node->lazy_deopt_info()->GetFrameForExceptionHandler(
              handler_info);
      const maglev::CompactInterpreterFrameState* compact_frame =
          interpreted_frame.frame_state();
      const maglev::MaglevCompilationUnit& maglev_unit =
          interpreted_frame.unit();

      builder_.IterCatchHandlerPhis(
          catch_block_, [this, compact_frame, maglev_unit](
                            interpreter::Register owner, Variable var) {
            DCHECK_NE(owner, interpreter::Register::virtual_accumulator());

            const maglev::ValueNode* maglev_value =
                compact_frame->GetValueOf(owner, maglev_unit);
            DCHECK_NOT_NULL(maglev_value);

            if (const maglev::VirtualObject* vobj =
                    maglev_value->TryCast<maglev::VirtualObject>()) {
              maglev_value = vobj->allocation();
            }

            V<Any> ts_value = builder_.Map(maglev_value);
            __ SetVariable(var, ts_value);
            builder_.RecordRepresentation(ts_value,
                                          maglev_value->value_representation());
          });
    }

    ~ThrowingScope() {
      // Resetting the catch handler. It is always set on a case-by-case basis
      // before emitting a throwing node, so there is no need to "reset the
      // previous catch handler" or something like that, since there is no
      // previous handler (there is a DCHECK in the ThrowingScope constructor
      // checking that the current_catch_block is indeed nullptr when the scope
      // is created).
      __ set_current_catch_block(nullptr);

      if (catch_block_ == nullptr) return;
      if (!catch_block_->has_phi()) return;

      // We clear the Variables that we've set when initializing the scope, in
      // order to avoid creating Phis for such Variables. These are really only
      // meant to be used when translating the Phis in the catch handler, and
      // when the scope is destroyed, we shouldn't be in the Catch handler yet.
      builder_.IterCatchHandlerPhis(
          catch_block_, [this](interpreter::Register, Variable var) {
            __ SetVariable(var, V<Object>::Invalid());
          });
    }

   private:
    GraphBuilder::AssemblerT& Asm() { return builder_.Asm(); }
    GraphBuilder& builder_;
    const maglev::BasicBlock* catch_block_ = nullptr;
  };

  class NoThrowingScopeRequired {
   public:
    explicit NoThrowingScopeRequired(maglev::NodeBase* node) {
      // If this DCHECK fails, then the caller should instead use a
      // ThrowingScope. Additionally, all of the calls it contains should
      // explicitely pass LazyDeoptOnThrow.
      DCHECK(!node->properties().can_throw());
    }
  };

  template <typename Function>
  void IterCatchHandlerPhis(const maglev::BasicBlock* catch_block,
                            Function&& callback) {
    DCHECK_NOT_NULL(catch_block);
    DCHECK(catch_block->has_phi());
    for (auto phi : *catch_block->phis()) {
      DCHECK(phi->is_exception_phi());
      interpreter::Register owner = phi->owner();
      if (owner == interpreter::Register::virtual_accumulator()) {
        // The accumulator exception phi corresponds to the exception object
        // rather than whatever value the accumulator contained before the
        // throwing operation. We don't need to iterate here, since there is
        // special handling when processing Phis to use `catch_block_begin_`
        // for it instead of a Variable.
        continue;
      }

      auto it = regs_to_vars_.find(owner.index());
      Variable var;
      if (it == regs_to_vars_.end()) {
        // We use a LoopInvariantVariable: if loop phis were needed, then the
        // Maglev value would already be a loop Phi, and we wouldn't need
        // Turboshaft to automatically insert a loop phi.
        var = __ NewLoopInvariantVariable(RegisterRepresentation::Tagged());
        regs_to_vars_.insert({owner.index(), var});
      } else {
        var = it->second;
      }

      callback(owner, var);
    }
  }

  template <typename T>
  V<T> Map(const maglev::Input input) {
    return V<T>::Cast(Map(input.node()));
  }
  OpIndex Map(const maglev::Input input) { return Map(input.node()); }
  OpIndex Map(const maglev::NodeBase* node) {
    if (V8_UNLIKELY(node == maglev_generator_context_node_)) {
      return __ GetVariable(generator_context_);
    }
    DCHECK(node_mapping_[node].valid());
    return node_mapping_[node];
  }
  Block* Map(const maglev::BasicBlock* block) { return block_mapping_[block]; }

  void SetMap(maglev::NodeBase* node, V<Any> idx) {
    DCHECK(idx.valid());
    DCHECK_EQ(__ output_graph().Get(idx).outputs_rep().size(), 1);
    node_mapping_[node] = idx;
  }

  void SetMapMaybeMultiReturn(maglev::NodeBase* node, V<Any> idx) {
    const Operation& op = __ output_graph().Get(idx);
    if (const TupleOp* tuple = op.TryCast<TupleOp>()) {
      // If the call returned multiple values, then in Maglev, {node} is
      // used as the 1st returned value, and a GetSecondReturnedValue node is
      // used to access the 2nd value. We thus call `SetMap` with the 1st
      // projection of the call, and record the 2nd projection in
      // {second_return_value_}, which we'll use when translating
      // GetSecondReturnedValue.
      DCHECK_EQ(tuple->input_count, 2);
      SetMap(node, tuple->input(0));
      second_return_value_ = tuple->input<Object>(1);
    } else {
      SetMap(node, idx);
    }
  }

  void RecordRepresentation(OpIndex idx, maglev::ValueRepresentation repr) {
    DCHECK_IMPLIES(maglev_representations_.contains(idx),
                   maglev_representations_[idx] == repr);
    maglev_representations_[idx] = repr;
  }

  V<NativeContext> native_context() {
    DCHECK(native_context_.valid());
    return native_context_;
  }

  PipelineData* data_;
  Zone* temp_zone_;
  Isolate* isolate_ = data_->isolate();
  LocalIsolate* local_isolate_ = isolate_->AsLocalIsolate();
  JSHeapBroker* broker_ = data_->broker();
  LocalFactory* local_factory_ = local_isolate_->factory();
  AssemblerT assembler_;
  maglev::MaglevCompilationUnit* maglev_compilation_unit_;
  ZoneUnorderedMap<const maglev::NodeBase*, OpIndex> node_mapping_;
  ZoneUnorderedMap<const maglev::BasicBlock*, Block*> block_mapping_;
  ZoneUnorderedMap<int, Variable> regs_to_vars_;

  // The {deduplicator_} is used when building frame states containing escaped
  // objects. It could be a local object in `BuildFrameState`, but it's instead
  // defined here to recycle its memory.
  Deduplicator deduplicator_;

  // In Turboshaft, exception blocks start with a CatchBlockBegin. In Maglev,
  // there is no such operation, and the exception is instead populated into the
  // accumulator by the throwing code, and is then loaded in Maglev through an
  // exception phi. When emitting a Turboshaft exception block, we thus store
  // the CatchBlockBegin in {catch_block_begin_}, which we then use when trying
  // to map the exception phi corresponding to the accumulator.
  V<Object> catch_block_begin_ = V<Object>::Invalid();

  // Maglev loops can have multiple forward edges, while Turboshaft should only
  // have a single one. When a Maglev loop has multiple forward edge, we create
  // an additional Turboshaft block before (which we record in
  // {loop_single_edge_predecessors_}), and jumps to the loop will instead go to
  // this additional block, which will become the only forward predecessor of
  // the loop.
  ZoneUnorderedMap<const maglev::BasicBlock*, Block*>
      loop_single_edge_predecessors_;
  // When we create an additional loop predecessor for loops that have multiple
  // forward predecessors, we store the newly created phis in
  // {loop_phis_first_input_}, so that we can then use them as the first input
  // of the original loop phis. {loop_phis_first_input_index_} is used as an
  // index in {loop_phis_first_input_} in VisitPhi so that we know where to find
  // the first input for the current loop phi.
  base::SmallVector<OpIndex, 16> loop_phis_first_input_;
  int loop_phis_first_input_index_ = -1;

  // Magle doesn't have projections. Instead, after nodes that return multiple
  // values (currently, only maglev::ForInPrepare and maglev::CallBuiltin for
  // some builtins), Maglev inserts a GetSecondReturnedValue node, which
  // basically just binds kReturnRegister1 to a ValueNode. In the
  // Maglev->Turboshaft translation, when we emit a builtin call with multiple
  // return values, we set {second_return_value_} to the 2nd projection, and
  // then use it when translating GetSecondReturnedValue.
  V<Object> second_return_value_ = V<Object>::Invalid();

  // {maglev_representations_} contains a map from Turboshaft OpIndex to
  // ValueRepresentation of the corresponding Maglev node. This is used when
  // translating exception phis: they might need to be re-tagged, and we need to
  // know the Maglev ValueRepresentation to distinguish between Float64 and
  // HoleyFloat64 (both of which would have Float64 RegisterRepresentation in
  // Turboshaft, but they need to be tagged differently).
  ZoneAbslFlatHashMap<OpIndex, maglev::ValueRepresentation>
      maglev_representations_;

  GeneratorAnalyzer generator_analyzer_;
  static constexpr int kDefaultSwitchVarValue = -1;
  // {is_visiting_generator_main_switch_} is true if the function is a resumable
  // generator, and the current input block is the main dispatch switch for
  // resuming the generator.
  bool is_visiting_generator_main_switch_ = false;
  // {on_generator_switch_loop_} is true if the current input block is a loop
  // that used to be bypassed by generator resumes, and thus that needs a
  // secondary generator dispatch switch.
  bool on_generator_switch_loop_ = false;
  // {header_switch_input_} is the value on which secondary generator switches
  // should switch.
  Variable header_switch_input_;
  // When secondary dispatch switches for generators are created,
  // {loop_default_generator_value_} is used as the default inputs for
  // {header_switch_input_} for edges that weren't manually inserted in the
  // translation for generators.
  V<Word32> loop_default_generator_value_ = V<Word32>::Invalid();
  // If the main generator switch bypasses some loop headers, we'll need to
  // add an additional predecessor to these loop headers to get rid of the
  // bypass. If we do so, we'll need a dummy input for the loop Phis, which
  // we create here.
  V<Object> dummy_object_input_ = V<Object>::Invalid();
  V<Word32> dummy_word32_input_ = V<Word32>::Invalid();
  V<Float64> dummy_float64_input_ = V<Float64>::Invalid();
  // {maglev_generator_context_node_} is the 1st Maglev node that load the
  // context from the generator. Because of the removal of loop header bypasses,
  // we can end up using this node in place that's not dominated by the block
  // defining this node. To fix this problem, when loading the context from the
  // generator for the 1st time, we set {generator_context_}, and in `Map`, we
  // always check whether we're trying to get the generator context (=
  // {maglev_generator_context_node_}): if so, then we get the value from
  // {generator_context_} instead. Note that {generator_context_} is initialized
  // with a dummy value (NoContextConstant) so that valid Phis get inserted
  // where needed, but by construction, we'll never actually use this dummy
  // value.
  maglev::NodeBase* maglev_generator_context_node_ = nullptr;
  Variable generator_context_;

  struct GeneratorSplitEdge {
    Block* pre_loop_dst;
    Block* inside_loop_target;
    int switch_value;
  };
  std::unordered_map<const maglev::BasicBlock*, std::vector<GeneratorSplitEdge>>
      pre_loop_generator_blocks_;

  V<NativeContext> native_context_ = V<NativeContext>::Invalid();
  V<Object> new_target_param_ = V<Object>::Invalid();
  base::SmallVector<int, 16> predecessor_permutation_;

  std::optional<BailoutReason>* bailout_;
};

// A NodeProcessor wrapper around GraphBuilder that takes care of
//  - skipping nodes when we are in Unreachable code.
//  - recording source positions.
class NodeProcessorBase : public GraphBuilder {
 public:
  using GraphBuilder::GraphBuilder;

  NodeProcessorBase(PipelineData* data, Graph& graph, Zone* temp_zone,
                    maglev::MaglevCompilationUnit* maglev_compilation_unit,
                    std::optional<BailoutReason>* bailout)
      : GraphBuilder::GraphBuilder(data, graph, temp_zone,
                                   maglev_compilation_unit, bailout),
        graph_(graph),
        labeller_(maglev_compilation_unit->graph_labeller()) {}

  template <typename NodeT>
  maglev::ProcessResult Process(NodeT* node,
                                const maglev::ProcessingState& state) {
    if (GraphBuilder::Asm().generating_unreachable_operations()) {
      // It doesn't matter much whether we return kRemove or kContinue here,
      // since anyways we'll be done with the Maglev graph once this phase is
      // over. Maglev currently doesn't support kRemove for control nodes, so we
      // just return kContinue for simplicity.
      return maglev::ProcessResult::kContinue;
    } else {
      OpIndex end_index_before = graph_.EndIndex();
      maglev::ProcessResult result = GraphBuilder::Process(node, state);

      // Recording the SourcePositions of the OpIndex that were just created.
      SourcePosition source = labeller_->GetNodeProvenance(node).position;
      for (OpIndex idx = end_index_before; idx != graph_.EndIndex();
           idx = graph_.NextIndex(idx)) {
        graph_.source_positions()[idx] = source;
      }

      return result;
    }
  }

 private:
  Graph& graph_;
  maglev::MaglevGraphLabeller* labeller_;
};

void PrintBytecode(PipelineData& data,
                   maglev::MaglevCompilationInfo* compilation_info) {
  DCHECK(data.info()->trace_turbo_graph());
  maglev::MaglevCompilationUnit* top_level_unit =
      compilation_info->toplevel_compilation_unit();
  CodeTracer* code_tracer = data.GetCodeTracer();
  CodeTracer::StreamScope tracing_scope(code_tracer);
  tracing_scope.stream()
      << "\n----- Bytecode before MaglevGraphBuilding -----\n"
      << std::endl;
  tracing_scope.stream() << "Function: "
                         << Brief(*compilation_info->toplevel_function())
                         << std::endl;
  BytecodeArray::Disassemble(top_level_unit->bytecode().object(),
                             tracing_scope.stream());
  Print(*top_level_unit->feedback().object(), tracing_scope.stream());
}

void PrintMaglevGraph(PipelineData& data,
                      maglev::MaglevCompilationInfo* compilation_info,
                      maglev::Graph* maglev_graph, const char* msg) {
  CodeTracer* code_tracer = data.GetCodeTracer();
  CodeTracer::StreamScope tracing_scope(code_tracer);
  tracing_scope.stream() << "\n----- " << msg << " -----" << std::endl;
  maglev::PrintGraph(tracing_scope.stream(), compilation_info, maglev_graph);
}

// TODO(dmercadier, nicohartmann): consider doing some of these optimizations on
// the Turboshaft graph after the Maglev->Turboshaft translation. For instance,
// MaglevPhiRepresentationSelector is the Maglev equivalent of Turbofan's
// SimplifiedLowering, but is much less powerful (doesn't take truncations into
// account, doesn't do proper range analysis, doesn't run a fixpoint
// analysis...).
void RunMaglevOptimizations(PipelineData* data,
                            maglev::MaglevCompilationInfo* compilation_info,
                            maglev::MaglevGraphBuilder& maglev_graph_builder,
                            maglev::Graph* maglev_graph) {
  // Phi untagging.
  {
    maglev::GraphProcessor<maglev::MaglevPhiRepresentationSelector> processor(
        &maglev_graph_builder);
    processor.ProcessGraph(maglev_graph);
  }

  if (V8_UNLIKELY(data->info()->trace_turbo_graph())) {
    PrintMaglevGraph(*data, compilation_info, maglev_graph,
                     "After phi untagging");
  }

  // Escape analysis.
  {
    maglev::GraphMultiProcessor<maglev::AnyUseMarkingProcessor> processor;
    processor.ProcessGraph(maglev_graph);
  }

#ifdef DEBUG
  maglev::GraphProcessor<maglev::MaglevGraphVerifier> verifier(
      compilation_info);
  verifier.ProcessGraph(maglev_graph);
#endif

  // Dead nodes elimination (which, amongst other things, cleans up the left
  // overs of escape analysis).
  {
    maglev::GraphMultiProcessor<maglev::DeadNodeSweepingProcessor> processor(
        maglev::DeadNodeSweepingProcessor{compilation_info});
    processor.ProcessGraph(maglev_graph);
  }

  if (V8_UNLIKELY(data->info()->trace_turbo_graph())) {
    PrintMaglevGraph(*data, compilation_info, maglev_graph,
                     "After escape analysis and dead node sweeping");
  }
}

std::optional<BailoutReason> MaglevGraphBuildingPhase::Run(PipelineData* data,
                                                           Zone* temp_zone) {
  JSHeapBroker* broker = data->broker();
  UnparkedScopeIfNeeded unparked_scope(broker);

  std::unique_ptr<maglev::MaglevCompilationInfo> compilation_info =
      maglev::MaglevCompilationInfo::NewForTurboshaft(
          data->isolate(), broker, data->info()->closure(),
          data->info()->osr_offset(),
          data->info()->function_context_specializing());

  if (V8_UNLIKELY(data->info()->trace_turbo_graph())) {
    PrintBytecode(*data, compilation_info.get());
  }

  LocalIsolate* local_isolate = broker->local_isolate()
                                    ? broker->local_isolate()
                                    : broker->isolate()->AsLocalIsolate();
  maglev::Graph* maglev_graph =
      maglev::Graph::New(temp_zone, data->info()->is_osr());

  // We always create a MaglevGraphLabeller in order to record source positions.
  compilation_info->set_graph_labeller(new maglev::MaglevGraphLabeller());

  maglev::MaglevGraphBuilder maglev_graph_builder(
      local_isolate, compilation_info->toplevel_compilation_unit(),
      maglev_graph);
  maglev_graph_builder.Build();

  if (V8_UNLIKELY(data->info()->trace_turbo_graph())) {
    PrintMaglevGraph(*data, compilation_info.get(), maglev_graph,
                     "After graph building");
  }

  RunMaglevOptimizations(data, compilation_info.get(), maglev_graph_builder,
                         maglev_graph);

  // TODO(nicohartmann): Should we have source positions here?
  data->InitializeGraphComponent(nullptr);

  std::optional<BailoutReason> bailout;
  maglev::GraphProcessor<NodeProcessorBase, true> builder(
      data, data->graph(), temp_zone,
      compilation_info->toplevel_compilation_unit(), &bailout);
  builder.ProcessGraph(maglev_graph);

  // Copying {inlined_functions} from Maglev to Turboshaft.
  for (OptimizedCompilationInfo::InlinedFunctionHolder holder :
       maglev_graph->inlined_functions()) {
    data->info()->inlined_functions().push_back(holder);
  }

  if (V8_UNLIKELY(bailout.has_value() &&
                  (v8_flags.trace_turbo || v8_flags.trace_turbo_graph))) {
    // If we've bailed out, then we've probably left the graph in some kind of
    // invalid state. We Reset it now, so that --trace-turbo doesn't try to
    // print an invalid graph.
    data->graph().Reset();
  }

  return bailout;
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
