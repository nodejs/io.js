// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-interpreter-frame-state.h"

#include "src/handles/handles-inl.h"
#include "src/interpreter/bytecode-register.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph.h"
#include "src/objects/function-kind.h"

namespace v8 {
namespace internal {
namespace maglev {

void KnownNodeAspects::Merge(const KnownNodeAspects& other, Zone* zone) {
  bool any_merged_map_is_unstable = false;
  DestructivelyIntersect(node_infos, other.node_infos,
                         [&](NodeInfo& lhs, const NodeInfo& rhs) {
                           lhs.MergeWith(rhs, zone, any_merged_map_is_unstable);
                           return !lhs.no_info_available();
                         });

  if (effect_epoch_ != other.effect_epoch_) {
    effect_epoch_ = std::max(effect_epoch_, other.effect_epoch_) + 1;
  }
  DestructivelyIntersect(
      available_expressions, other.available_expressions,
      [&](const AvailableExpression& lhs, const AvailableExpression& rhs) {
        DCHECK_IMPLIES(lhs.node == rhs.node,
                       lhs.effect_epoch == rhs.effect_epoch);
        return lhs.node == rhs.node && lhs.effect_epoch >= effect_epoch_;
      });

  this->any_map_for_any_node_is_unstable = any_merged_map_is_unstable;

  auto merge_loaded_properties =
      [](ZoneMap<ValueNode*, ValueNode*>& lhs,
         const ZoneMap<ValueNode*, ValueNode*>& rhs) {
        // Loaded properties are maps of maps, so just do the destructive
        // intersection recursively.
        DestructivelyIntersect(lhs, rhs);
        return !lhs.empty();
      };
  DestructivelyIntersect(loaded_constant_properties,
                         other.loaded_constant_properties,
                         merge_loaded_properties);
  DestructivelyIntersect(loaded_properties, other.loaded_properties,
                         merge_loaded_properties);
  DestructivelyIntersect(loaded_context_constants,
                         other.loaded_context_constants);
  DestructivelyIntersect(loaded_context_slots, other.loaded_context_slots);
}

// static
MergePointInterpreterFrameState* MergePointInterpreterFrameState::New(
    const MaglevCompilationUnit& info, const InterpreterFrameState& state,
    int merge_offset, int predecessor_count, BasicBlock* predecessor,
    const compiler::BytecodeLivenessState* liveness) {
  MergePointInterpreterFrameState* merge_state =
      info.zone()->New<MergePointInterpreterFrameState>(
          info, merge_offset, predecessor_count, 1,
          info.zone()->AllocateArray<BasicBlock*>(predecessor_count),
          BasicBlockType::kDefault, liveness);
  int i = 0;
  merge_state->frame_state_.ForEachValue(
      info, [&](ValueNode*& entry, interpreter::Register reg) {
        entry = state.get(reg);
        // Initialise the alternatives list and cache the alternative
        // representations of the node.
        Alternatives::List* per_predecessor_alternatives =
            new (&merge_state->per_predecessor_alternatives_[i])
                Alternatives::List();
        per_predecessor_alternatives->Add(info.zone()->New<Alternatives>(
            state.known_node_aspects()->TryGetInfoFor(entry)));
        i++;
      });
  merge_state->predecessors_[0] = predecessor;
  merge_state->known_node_aspects_ =
      state.known_node_aspects()->Clone(info.zone());
  return merge_state;
}

// static
MergePointInterpreterFrameState* MergePointInterpreterFrameState::NewForLoop(
    const InterpreterFrameState& start_state, const MaglevCompilationUnit& info,
    int merge_offset, int predecessor_count,
    const compiler::BytecodeLivenessState* liveness,
    const compiler::LoopInfo* loop_info, bool has_been_peeled) {
  MergePointInterpreterFrameState* state =
      info.zone()->New<MergePointInterpreterFrameState>(
          info, merge_offset, predecessor_count, 0,
          info.zone()->AllocateArray<BasicBlock*>(predecessor_count),
          BasicBlockType::kLoopHeader, liveness);
  state->bitfield_ =
      kIsLoopWithPeeledIterationBit::update(state->bitfield_, has_been_peeled);
  state->loop_info_ = loop_info;
  if (loop_info->resumable()) {
    state->known_node_aspects_ =
        info.zone()->New<KnownNodeAspects>(info.zone());
    state->bitfield_ = kIsResumableLoopBit::update(state->bitfield_, true);
  }
  auto& assignments = loop_info->assignments();
  auto& frame_state = state->frame_state_;
  int i = 0;
  frame_state.ForEachParameter(
      info, [&](ValueNode*& entry, interpreter::Register reg) {
        entry = nullptr;
        if (assignments.ContainsParameter(reg.ToParameterIndex())) {
          entry = state->NewLoopPhi(info.zone(), reg);
        } else if (state->is_resumable_loop()) {
          // Copy initial values out of the start state.
          entry = start_state.get(reg);
          // Initialise the alternatives list for this value.
          new (&state->per_predecessor_alternatives_[i]) Alternatives::List();
          DCHECK(entry->Is<InitialValue>());
        }
        ++i;
      });
  frame_state.context(info) = nullptr;
  if (state->is_resumable_loop()) {
    // While contexts are always the same at specific locations, resumable loops
    // do have different nodes to set the context across resume points. Create a
    // phi for them.
    frame_state.context(info) = state->NewLoopPhi(
        info.zone(), interpreter::Register::current_context());
  }
  frame_state.ForEachLocal(
      info, [&](ValueNode*& entry, interpreter::Register reg) {
        entry = nullptr;
        if (assignments.ContainsLocal(reg.index())) {
          entry = state->NewLoopPhi(info.zone(), reg);
        }
      });
  DCHECK(!frame_state.liveness()->AccumulatorIsLive());
  return state;
}

// static
MergePointInterpreterFrameState*
MergePointInterpreterFrameState::NewForCatchBlock(
    const MaglevCompilationUnit& unit,
    const compiler::BytecodeLivenessState* liveness, int handler_offset,
    interpreter::Register context_register, Graph* graph) {
  Zone* const zone = unit.zone();
  MergePointInterpreterFrameState* state =
      zone->New<MergePointInterpreterFrameState>(
          unit, handler_offset, 0, 0, nullptr,
          BasicBlockType::kExceptionHandlerStart, liveness);
  auto& frame_state = state->frame_state_;
  // If the accumulator is live, the ExceptionPhi associated to it is the
  // first one in the block. That ensures it gets kReturnValue0 in the
  // register allocator. See
  // StraightForwardRegisterAllocator::AllocateRegisters.
  if (frame_state.liveness()->AccumulatorIsLive()) {
    frame_state.accumulator(unit) = state->NewExceptionPhi(
        zone, interpreter::Register::virtual_accumulator());
  }
  frame_state.ForEachRegister(
      unit,
      [&](ValueNode*& entry, interpreter::Register reg) { entry = nullptr; });
  state->catch_block_context_register_ = context_register;
  return state;
}

MergePointInterpreterFrameState::MergePointInterpreterFrameState(
    const MaglevCompilationUnit& info, int merge_offset, int predecessor_count,
    int predecessors_so_far, BasicBlock** predecessors, BasicBlockType type,
    const compiler::BytecodeLivenessState* liveness)
    : merge_offset_(merge_offset),
      predecessor_count_(predecessor_count),
      predecessors_so_far_(predecessors_so_far),
      bitfield_(kBasicBlockTypeBits::encode(type)),
      predecessors_(predecessors),
      frame_state_(info, liveness),
      per_predecessor_alternatives_(
          type == BasicBlockType::kExceptionHandlerStart
              ? nullptr
              : info.zone()->AllocateArray<Alternatives::List>(
                    frame_state_.size(info))) {}

namespace {
void PrintBeforeMerge(const MaglevCompilationUnit& compilation_unit,
                      ValueNode* current_value, ValueNode* unmerged_value,
                      interpreter::Register reg, KnownNodeAspects* kna) {
  if (!v8_flags.trace_maglev_graph_building) return;
  std::cout << "  " << reg.ToString() << ": "
            << PrintNodeLabel(compilation_unit.graph_labeller(), current_value)
            << "<";
  if (kna) {
    if (auto cur_info = kna->TryGetInfoFor(current_value)) {
      std::cout << cur_info->type();
      if (cur_info->possible_maps_are_known()) {
        std::cout << " " << cur_info->possible_maps().size();
      }
    }
  }
  std::cout << "> <- "
            << PrintNodeLabel(compilation_unit.graph_labeller(), unmerged_value)
            << "<";
  if (kna) {
    if (auto in_info = kna->TryGetInfoFor(unmerged_value)) {
      std::cout << in_info->type();
      if (in_info->possible_maps_are_known()) {
        std::cout << " " << in_info->possible_maps().size();
      }
    }
  }
  std::cout << ">";
}
void PrintAfterMerge(const MaglevCompilationUnit& compilation_unit,
                     ValueNode* merged_value, KnownNodeAspects* kna) {
  if (!v8_flags.trace_maglev_graph_building) return;
  std::cout << " => "
            << PrintNodeLabel(compilation_unit.graph_labeller(), merged_value)
            << ": "
            << PrintNode(compilation_unit.graph_labeller(), merged_value)
            << "<";

  if (kna) {
    if (auto out_info = kna->TryGetInfoFor(merged_value)) {
      std::cout << out_info->type();
      if (out_info->possible_maps_are_known()) {
        std::cout << " " << out_info->possible_maps().size();
      }
    }
  }

  std::cout << ">" << std::endl;
}
}  // namespace

void MergePointInterpreterFrameState::Merge(MaglevGraphBuilder* builder,
                                            InterpreterFrameState& unmerged,
                                            BasicBlock* predecessor) {
  Merge(builder, *builder->compilation_unit(), unmerged, predecessor);
}

void MergePointInterpreterFrameState::Merge(
    MaglevGraphBuilder* builder, MaglevCompilationUnit& compilation_unit,
    InterpreterFrameState& unmerged, BasicBlock* predecessor) {
  DCHECK_GT(predecessor_count_, 1);
  DCHECK_LT(predecessors_so_far_, predecessor_count_);
  predecessors_[predecessors_so_far_] = predecessor;

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "Merging..." << std::endl;
  }
  int i = 0;
  frame_state_.ForEachValue(compilation_unit, [&](ValueNode*& value,
                                                  interpreter::Register reg) {
    PrintBeforeMerge(compilation_unit, value, unmerged.get(reg), reg,
                     known_node_aspects_);
    value = MergeValue(builder, reg, *unmerged.known_node_aspects(), value,
                       unmerged.get(reg), &per_predecessor_alternatives_[i]);
    PrintAfterMerge(compilation_unit, value, known_node_aspects_);
    ++i;
  });

  if (known_node_aspects_ == nullptr) {
    DCHECK(is_unmerged_loop());
    DCHECK_EQ(predecessors_so_far_, 0);
    known_node_aspects_ =
        unmerged.known_node_aspects()->CloneForLoopHeader(builder->zone());
  } else {
    known_node_aspects_->Merge(*unmerged.known_node_aspects(), builder->zone());
  }

  predecessors_so_far_++;
  DCHECK_LE(predecessors_so_far_, predecessor_count_);
}

void MergePointInterpreterFrameState::MergeLoop(
    MaglevGraphBuilder* builder, InterpreterFrameState& loop_end_state,
    BasicBlock* loop_end_block) {
  MergeLoop(builder, *builder->compilation_unit(), loop_end_state,
            loop_end_block);
}

void MergePointInterpreterFrameState::MergeLoop(
    MaglevGraphBuilder* builder, MaglevCompilationUnit& compilation_unit,
    InterpreterFrameState& loop_end_state, BasicBlock* loop_end_block) {
  // This should be the last predecessor we try to merge.
  DCHECK_EQ(predecessors_so_far_, predecessor_count_ - 1);
  DCHECK(is_unmerged_loop());
  predecessors_[predecessor_count_ - 1] = loop_end_block;

  backedge_deopt_frame_ =
      builder->zone()->New<DeoptFrame>(builder->GetLatestCheckpointedFrame());

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "Merging loop backedge..." << std::endl;
  }
  frame_state_.ForEachValue(
      compilation_unit, [&](ValueNode* value, interpreter::Register reg) {
        PrintBeforeMerge(compilation_unit, value, loop_end_state.get(reg), reg,
                         known_node_aspects_);
        MergeLoopValue(builder, reg, *loop_end_state.known_node_aspects(),
                       value, loop_end_state.get(reg));
        PrintAfterMerge(compilation_unit, value, known_node_aspects_);
      });
  predecessors_so_far_++;
  DCHECK_EQ(predecessors_so_far_, predecessor_count_);
}

void MergePointInterpreterFrameState::MergeThrow(
    MaglevGraphBuilder* builder, const MaglevCompilationUnit* handler_unit,
    InterpreterFrameState& unmerged) {
  // We don't count total predecessors on exception handlers, but we do want to
  // special case the first predecessor so we do count predecessors_so_far
  DCHECK_EQ(predecessor_count_, 0);
  DCHECK(is_exception_handler());

  // Find the graph builder that contains the actual handler, so that we merge
  // in the right function's interpreter frame state.
  const MaglevGraphBuilder* handler_builder = builder;
  while (handler_builder->compilation_unit() != handler_unit) {
    handler_builder = handler_builder->parent();
  }
  const InterpreterFrameState& handler_builder_frame =
      handler_builder->current_interpreter_frame();

  if (v8_flags.trace_maglev_graph_building) {
    if (handler_builder == builder) {
      std::cout << "Merging into exception handler..." << std::endl;
    } else {
      std::cout << "Merging into parent exception handler..." << std::endl;
    }
  }

  frame_state_.ForEachParameter(*handler_unit, [&](ValueNode*& value,
                                                   interpreter::Register reg) {
    PrintBeforeMerge(*handler_unit, value, handler_builder_frame.get(reg), reg,
                     known_node_aspects_);
    value = MergeValue(builder, reg, *unmerged.known_node_aspects(), value,
                       handler_builder_frame.get(reg), nullptr);
    PrintAfterMerge(*handler_unit, value, known_node_aspects_);
  });
  frame_state_.ForEachLocal(*handler_unit, [&](ValueNode*& value,
                                               interpreter::Register reg) {
    PrintBeforeMerge(*handler_unit, value, handler_builder_frame.get(reg), reg,
                     known_node_aspects_);
    value = MergeValue(builder, reg, *unmerged.known_node_aspects(), value,
                       handler_builder_frame.get(reg), nullptr);
    PrintAfterMerge(*handler_unit, value, known_node_aspects_);
  });

  // Pick out the context value from the incoming registers.
  // TODO(leszeks): This should be the same for all incoming states, but we lose
  // the identity for generator-restored context. If generator value restores
  // were handled differently, we could avoid emitting a Phi here.
  ValueNode*& context = frame_state_.context(*handler_unit);
  PrintBeforeMerge(*handler_unit, context,
                   handler_builder_frame.get(catch_block_context_register_),
                   catch_block_context_register_, known_node_aspects_);
  context = MergeValue(builder, catch_block_context_register_,
                       *unmerged.known_node_aspects(), context,
                       handler_builder_frame.get(catch_block_context_register_),
                       nullptr);
  PrintAfterMerge(*handler_unit, context, known_node_aspects_);

  if (known_node_aspects_ == nullptr) {
    DCHECK_EQ(predecessors_so_far_, 0);
    known_node_aspects_ = unmerged.known_node_aspects()->Clone(builder->zone());
  } else {
    known_node_aspects_->Merge(*unmerged.known_node_aspects(), builder->zone());
  }
  predecessors_so_far_++;
}

namespace {

ValueNode* FromInt32ToTagged(MaglevGraphBuilder* builder, NodeType node_type,
                             ValueNode* value, BasicBlock* predecessor) {
  DCHECK_EQ(value->properties().value_representation(),
            ValueRepresentation::kInt32);
  DCHECK(!value->properties().is_conversion());

  ValueNode* tagged;
  if (value->Is<Int32Constant>()) {
    int32_t constant = value->Cast<Int32Constant>()->value();
    return builder->GetSmiConstant(constant);
  } else if (value->Is<StringLength>() ||
             value->Is<BuiltinStringPrototypeCharCodeOrCodePointAt>()) {
    static_assert(String::kMaxLength <= kSmiMaxValue,
                  "String length must fit into a Smi");
    tagged = Node::New<UnsafeSmiTag>(builder->zone(), {value});
  } else if (NodeTypeIsSmi(node_type)) {
    // For known Smis, we can tag without a check.
    tagged = Node::New<UnsafeSmiTag>(builder->zone(), {value});
  } else {
    tagged = Node::New<Int32ToNumber>(builder->zone(), {value});
  }

  predecessor->nodes().Add(tagged);
  builder->compilation_unit()->RegisterNodeInGraphLabeller(tagged);
  return tagged;
}

ValueNode* FromUint32ToTagged(MaglevGraphBuilder* builder, NodeType node_type,
                              ValueNode* value, BasicBlock* predecessor) {
  DCHECK_EQ(value->properties().value_representation(),
            ValueRepresentation::kUint32);
  DCHECK(!value->properties().is_conversion());

  ValueNode* tagged;
  if (NodeTypeIsSmi(node_type)) {
    tagged = Node::New<UnsafeSmiTag>(builder->zone(), {value});
  } else {
    tagged = Node::New<Uint32ToNumber>(builder->zone(), {value});
  }

  predecessor->nodes().Add(tagged);
  builder->compilation_unit()->RegisterNodeInGraphLabeller(tagged);
  return tagged;
}

ValueNode* FromFloat64ToTagged(MaglevGraphBuilder* builder, NodeType node_type,
                               ValueNode* value, BasicBlock* predecessor) {
  DCHECK_EQ(value->properties().value_representation(),
            ValueRepresentation::kFloat64);
  DCHECK(!value->properties().is_conversion());

  // Create a tagged version, and insert it at the end of the predecessor.
  ValueNode* tagged = Node::New<Float64ToTagged>(
      builder->zone(), {value},
      Float64ToTagged::ConversionMode::kCanonicalizeSmi);

  predecessor->nodes().Add(tagged);
  builder->compilation_unit()->RegisterNodeInGraphLabeller(tagged);
  return tagged;
}

ValueNode* FromHoleyFloat64ToTagged(MaglevGraphBuilder* builder,
                                    NodeType node_type, ValueNode* value,
                                    BasicBlock* predecessor) {
  DCHECK_EQ(value->properties().value_representation(),
            ValueRepresentation::kHoleyFloat64);
  DCHECK(!value->properties().is_conversion());

  // Create a tagged version, and insert it at the end of the predecessor.
  ValueNode* tagged = Node::New<HoleyFloat64ToTagged>(
      builder->zone(), {value},
      HoleyFloat64ToTagged::ConversionMode::kCanonicalizeSmi);

  predecessor->nodes().Add(tagged);
  builder->compilation_unit()->RegisterNodeInGraphLabeller(tagged);
  return tagged;
}

ValueNode* NonTaggedToTagged(MaglevGraphBuilder* builder, NodeType node_type,
                             ValueNode* value, BasicBlock* predecessor) {
  switch (value->properties().value_representation()) {
    case ValueRepresentation::kIntPtr:
    case ValueRepresentation::kTagged:
      UNREACHABLE();
    case ValueRepresentation::kInt32:
      return FromInt32ToTagged(builder, node_type, value, predecessor);
    case ValueRepresentation::kUint32:
      return FromUint32ToTagged(builder, node_type, value, predecessor);
    case ValueRepresentation::kFloat64:
      return FromFloat64ToTagged(builder, node_type, value, predecessor);
    case ValueRepresentation::kHoleyFloat64:
      return FromHoleyFloat64ToTagged(builder, node_type, value, predecessor);
  }
}
ValueNode* EnsureTagged(MaglevGraphBuilder* builder,
                        const KnownNodeAspects& known_node_aspects,
                        ValueNode* value, BasicBlock* predecessor) {
  if (value->properties().value_representation() ==
      ValueRepresentation::kTagged) {
    return value;
  }

  auto info_it = known_node_aspects.FindInfo(value);
  const NodeInfo* info =
      known_node_aspects.IsValid(info_it) ? &info_it->second : nullptr;
  if (info) {
    if (auto alt = info->alternative().tagged()) {
      return alt;
    }
  }
  return NonTaggedToTagged(builder, info ? info->type() : NodeType::kUnknown,
                           value, predecessor);
}

NodeType GetNodeType(compiler::JSHeapBroker* broker, LocalIsolate* isolate,
                     const KnownNodeAspects& aspects, ValueNode* node) {
  // We first check the KnownNodeAspects in order to return the most precise
  // type possible.
  NodeType type = aspects.NodeTypeFor(node);
  if (type != NodeType::kUnknown) {
    return type;
  }
  // If this node has no NodeInfo (or not known type in its NodeInfo), we fall
  // back to its static type.
  return StaticTypeForNode(broker, isolate, node);
}

}  // namespace

NodeType MergePointInterpreterFrameState::AlternativeType(
    const Alternatives* alt) {
  if (!alt) return NodeType::kUnknown;
  return alt->node_type();
}

ValueNode* MergePointInterpreterFrameState::MergeValue(
    MaglevGraphBuilder* builder, interpreter::Register owner,
    const KnownNodeAspects& unmerged_aspects, ValueNode* merged,
    ValueNode* unmerged, Alternatives::List* per_predecessor_alternatives) {
  // If the merged node is null, this is a pre-created loop header merge
  // frame will null values for anything that isn't a loop Phi.
  if (merged == nullptr) {
    DCHECK(is_exception_handler() || is_unmerged_loop());
    DCHECK_EQ(predecessors_so_far_, 0);
    // Initialise the alternatives list and cache the alternative
    // representations of the node.
    if (per_predecessor_alternatives) {
      new (per_predecessor_alternatives) Alternatives::List();
      per_predecessor_alternatives->Add(builder->zone()->New<Alternatives>(
          unmerged_aspects.TryGetInfoFor(unmerged)));
    } else {
      DCHECK(is_exception_handler());
    }
    return unmerged;
  }

  Phi* result = merged->TryCast<Phi>();
  if (result != nullptr && result->merge_state() == this) {
    // It's possible that merged == unmerged at this point since loop-phis are
    // not dropped if they are only assigned to themselves in the loop.
    DCHECK_EQ(result->owner(), owner);
    // Don't set inputs on exception phis.
    DCHECK_EQ(result->is_exception_phi(), is_exception_handler());
    if (is_exception_handler()) {
      return result;
    }
    NodeType unmerged_type =
        GetNodeType(builder->broker(), builder->local_isolate(),
                    unmerged_aspects, unmerged);
    unmerged = EnsureTagged(builder, unmerged_aspects, unmerged,
                            predecessors_[predecessors_so_far_]);
    result->set_input(predecessors_so_far_, unmerged);

    if (predecessors_so_far_ == 0) {
      DCHECK(result->is_loop_phi());
      // For loop Phis, `type` is always Unknown until the backedge has been
      // bound, so there is no point in updating it here.
      result->set_post_loop_type(unmerged_type);
    } else {
      result->merge_type(unmerged_type);
      result->merge_post_loop_type(unmerged_type);
    }

    return result;
  }

  if (merged == unmerged) {
    // Cache the alternative representations of the unmerged node.
    if (per_predecessor_alternatives) {
      DCHECK_EQ(per_predecessor_alternatives->LengthForTest(),
                predecessors_so_far_);
      per_predecessor_alternatives->Add(builder->zone()->New<Alternatives>(
          unmerged_aspects.TryGetInfoFor(unmerged)));
    } else {
      DCHECK(is_exception_handler());
    }
    return merged;
  }

  // We should always statically know what the context is, so we should never
  // create Phis for it. The exception is resumable functions and OSR, where the
  // context should be statically known but we lose that static information
  // across the resume / OSR entry.
  DCHECK_IMPLIES(
      owner == interpreter::Register::current_context() ||
          (is_exception_handler() && owner == catch_block_context_register()),
      IsResumableFunction(builder->compilation_unit()
                              ->info()
                              ->toplevel_compilation_unit()
                              ->shared_function_info()
                              .kind()) ||
          builder->compilation_unit()->info()->toplevel_is_osr());

  // Up to this point all predecessors had the same value for this interpreter
  // frame slot. Now that we find a distinct value, insert a copy of the first
  // value for each predecessor seen so far, in addition to the new value.
  // TODO(verwaest): Unclear whether we want this for Maglev: Instead of
  // letting the register allocator remove phis, we could always merge through
  // the frame slot. In that case we only need the inputs for representation
  // selection, and hence could remove duplicate inputs. We'd likely need to
  // attach the interpreter register to the phi in that case?

  // For exception phis, just allocate exception handlers.
  if (is_exception_handler()) {
    return NewExceptionPhi(builder->zone(), owner);
  }

  result = Node::New<Phi>(builder->zone(), predecessor_count_, this, owner);
  if (v8_flags.trace_maglev_graph_building) {
    for (int i = 0; i < predecessor_count_; i++) {
      result->initialize_input_null(i);
    }
  }

  NodeType merged_type =
      StaticTypeForNode(builder->broker(), builder->local_isolate(), merged);

  bool is_tagged = merged->properties().value_representation() ==
                   ValueRepresentation::kTagged;
  NodeType type = merged_type != NodeType::kUnknown
                      ? merged_type
                      : AlternativeType(per_predecessor_alternatives->first());
  int i = 0;
  for (const Alternatives* alt : *per_predecessor_alternatives) {
    ValueNode* tagged = is_tagged ? merged : alt->tagged_alternative();
    if (tagged == nullptr) {
      DCHECK_NOT_NULL(alt);
      tagged = NonTaggedToTagged(builder, alt->node_type(), merged,
                                 predecessors_[i]);
    }
    result->set_input(i, tagged);
    type = IntersectType(type, merged_type != NodeType::kUnknown
                                   ? merged_type
                                   : AlternativeType(alt));
    i++;
  }
  DCHECK_EQ(i, predecessors_so_far_);

  // Note: it's better to call GetNodeType on {unmerged} before updating it with
  // EnsureTagged, since untagged nodes have a higher chance of having a
  // StaticType.
  NodeType unmerged_type = GetNodeType(
      builder->broker(), builder->local_isolate(), unmerged_aspects, unmerged);
  unmerged = EnsureTagged(builder, unmerged_aspects, unmerged,
                          predecessors_[predecessors_so_far_]);
  result->set_input(predecessors_so_far_, unmerged);

  result->set_type(IntersectType(type, unmerged_type));

  phis_.Add(result);
  return result;
}
void MergePointInterpreterFrameState::MergeLoopValue(
    MaglevGraphBuilder* builder, interpreter::Register owner,
    KnownNodeAspects& unmerged_aspects, ValueNode* merged,
    ValueNode* unmerged) {
  Phi* result = merged->TryCast<Phi>();
  if (result == nullptr || result->merge_state() != this) {
    // Not a loop phi, we don't have to do anything.
    return;
  }
  DCHECK_EQ(result->owner(), owner);
  unmerged = EnsureTagged(builder, unmerged_aspects, unmerged,
                          predecessors_[predecessors_so_far_]);
  result->set_input(predecessor_count_ - 1, unmerged);

  NodeType type = GetNodeType(builder->broker(), builder->local_isolate(),
                              unmerged_aspects, unmerged);
  result->merge_post_loop_type(type);
  // We've just merged the backedge, which means that future uses of this Phi
  // will be after the loop, so we can now promote `post_loop_type` to the
  // regular `type`.
  DCHECK_EQ(predecessors_so_far_, predecessor_count_ - 1);
  result->promote_post_loop_type();

  if (Phi* unmerged_phi = unmerged->TryCast<Phi>()) {
    // Propagating the `uses_repr` from {result} to {unmerged_phi}.
    builder->RecordUseReprHint(unmerged_phi, result->get_uses_repr_hints());
  }
}

ValueNode* MergePointInterpreterFrameState::NewLoopPhi(
    Zone* zone, interpreter::Register reg) {
  DCHECK_EQ(predecessors_so_far_, 0);
  // Create a new loop phi, which for now is empty.
  Phi* result = Node::New<Phi>(zone, predecessor_count_, this, reg);

  if (v8_flags.trace_maglev_graph_building) {
    for (int i = 0; i < predecessor_count_; i++) {
      result->initialize_input_null(i);
    }
  }
  phis_.Add(result);
  return result;
}

void MergePointInterpreterFrameState::ReducePhiPredecessorCount(
    interpreter::Register owner, ValueNode* merged, unsigned num) {
  // If the merged node is null, this is a pre-created loop header merge
  // frame with null values for anything that isn't a loop Phi.
  if (merged == nullptr) {
    DCHECK(is_unmerged_loop());
    DCHECK_EQ(predecessors_so_far_, 0);
    return;
  }

  Phi* result = merged->TryCast<Phi>();
  if (result != nullptr && result->merge_state() == this) {
    // It's possible that merged == unmerged at this point since loop-phis are
    // not dropped if they are only assigned to themselves in the loop.
    DCHECK_EQ(result->owner(), owner);
    result->reduce_input_count(num);
  }
}
}  // namespace maglev
}  // namespace internal
}  // namespace v8
