// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-interpreter-frame-state.h"

#include "include/v8-internal.h"
#include "src/base/logging.h"
#include "src/handles/handles-inl.h"
#include "src/interpreter/bytecode-register.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/objects/function-kind.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace maglev {

namespace {

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
        DCHECK_NE(lhs.effect_epoch, kEffectEpochOverflow);
        DCHECK_EQ(Node::needs_epoch_check(lhs.node->opcode()),
                  lhs.effect_epoch != kEffectEpochForPureInstructions);

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
  if (may_have_aliasing_contexts != other.may_have_aliasing_contexts) {
    if (may_have_aliasing_contexts == ContextSlotLoadsAlias::None) {
      may_have_aliasing_contexts = other.may_have_aliasing_contexts;
    } else if (other.may_have_aliasing_contexts !=
               ContextSlotLoadsAlias::None) {
      may_have_aliasing_contexts = ContextSlotLoadsAlias::Yes;
    }
  }
  DestructivelyIntersect(loaded_context_slots, other.loaded_context_slots);
}

namespace {

template <typename Key>
bool NextInIgnoreList(typename ZoneSet<Key>::const_iterator& ignore,
                      typename ZoneSet<Key>::const_iterator& ignore_end,
                      const Key& cur) {
  while (ignore != ignore_end && *ignore < cur) {
    ++ignore;
  }
  return ignore != ignore_end && *ignore == cur;
}

}  // namespace

void KnownNodeAspects::ClearUnstableNodeAspects() {
  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "  ! Clearing unstable node aspects" << std::endl;
  }
  ClearUnstableMaps();
  // Side-effects can change object contents, so we have to clear
  // our known loaded properties -- however, constant properties are known
  // to not change (and we added a dependency on this), so we don't have to
  // clear those.
  loaded_properties.clear();
  loaded_context_slots.clear();
  may_have_aliasing_contexts = KnownNodeAspects::ContextSlotLoadsAlias::None;
}

KnownNodeAspects* KnownNodeAspects::CloneForLoopHeader(
    Zone* zone, bool optimistic, LoopEffects* loop_effects) const {
  KnownNodeAspects* clone = zone->New<KnownNodeAspects>(zone);
  if (!any_map_for_any_node_is_unstable) {
    clone->node_infos = node_infos;
#ifdef DEBUG
    for (const auto& it : node_infos) {
      DCHECK(!it.second.any_map_is_unstable());
    }
#endif
  } else if (optimistic && !loop_effects->unstable_aspects_cleared) {
    clone->node_infos = node_infos;
    clone->any_map_for_any_node_is_unstable = any_map_for_any_node_is_unstable;
  } else {
    for (const auto& it : node_infos) {
      clone->node_infos.emplace(it.first,
                                NodeInfo::ClearUnstableMapsOnCopy{it.second});
    }
  }
  if (optimistic && !loop_effects->unstable_aspects_cleared) {
    // IMPORTANT: Whatever we clone here needs to be checked for consistency
    // in when we try to terminate the loop in `IsCompatibleWithLoopHeader`.
    if (loop_effects->objects_written.empty() &&
        loop_effects->keys_cleared.empty()) {
      clone->loaded_properties = loaded_properties;
    } else {
      auto cleared_key = loop_effects->keys_cleared.begin();
      auto cleared_keys_end = loop_effects->keys_cleared.end();
      auto cleared_obj = loop_effects->objects_written.begin();
      auto cleared_objs_end = loop_effects->objects_written.end();
      for (auto loaded_key : loaded_properties) {
        if (NextInIgnoreList(cleared_key, cleared_keys_end, loaded_key.first)) {
          continue;
        }
        auto& props_for_key =
            clone->loaded_properties.try_emplace(loaded_key.first, zone)
                .first->second;
        for (auto loaded_obj : loaded_key.second) {
          if (!NextInIgnoreList(cleared_obj, cleared_objs_end,
                                loaded_obj.first)) {
            props_for_key.emplace(loaded_obj);
          }
        }
      }
    }
    if (loop_effects->context_slot_written.empty()) {
      clone->loaded_context_slots = loaded_context_slots;
    } else {
      auto slot_written = loop_effects->context_slot_written.begin();
      auto slot_written_end = loop_effects->context_slot_written.end();
      for (auto loaded : loaded_context_slots) {
        if (!NextInIgnoreList(slot_written, slot_written_end, loaded.first)) {
          clone->loaded_context_slots.emplace(loaded);
        }
      }
    }
  }
  clone->loaded_constant_properties = loaded_constant_properties;
  clone->loaded_context_constants = loaded_context_constants;

  clone->effect_epoch_ = effect_epoch_;
  // To account for the back-jump we must not allow effects to be reshuffled
  // across loop headers.
  // TODO(olivf): Only do this if the loop contains write effects.
  clone->increment_effect_epoch();
  for (const auto& e : available_expressions) {
    if (e.second.effect_epoch >= clone->effect_epoch()) {
      clone->available_expressions.emplace(e);
    }
  }
  return clone;
}

namespace {

// Takes two ordered maps and ensures that every element in `as` is
//  * also present in `bs` and
//  * `Compare(a, b)` holds for each value.
template <typename As, typename Bs, typename CompareFunction,
          typename IsEmptyFunction = std::nullptr_t>
bool AspectIncludes(const As& as, const Bs& bs, const CompareFunction& Compare,
                    const IsEmptyFunction IsEmpty = nullptr) {
  typename As::const_iterator a = as.begin();
  typename Bs::const_iterator b = bs.begin();
  while (a != as.end()) {
    if constexpr (!std::is_same<IsEmptyFunction, std::nullptr_t>::value) {
      if (IsEmpty(a->second)) {
        ++a;
        continue;
      }
    }
    if (b == bs.end()) return false;
    while (b->first < a->first) {
      ++b;
      if (b == bs.end()) return false;
    }
    if (!(a->first == b->first)) return false;
    if (!Compare(a->second, b->second)) {
      return false;
    }
    ++a;
    ++b;
  }
  return true;
}

// Same as above but allows `as` to contain empty collections as values, which
// do not need to be present in `bs`.
template <typename As, typename Bs, typename Function>
bool MaybeEmptyAspectIncludes(const As& as, const Bs& bs,
                              const Function& Compare) {
  return AspectIncludes<As, Bs, Function>(as, bs, Compare,
                                          [](auto x) { return x.empty(); });
}

bool NodeInfoIncludes(const NodeInfo& before, const NodeInfo& after) {
  if (!NodeTypeIs(after.type(), before.type())) {
    return false;
  }
  if (before.possible_maps_are_known() && before.any_map_is_unstable()) {
    if (!after.possible_maps_are_known()) {
      return false;
    }
    if (!before.possible_maps().contains(after.possible_maps())) {
      return false;
    }
  }
  return true;
}

bool NodeInfoIsEmpty(const NodeInfo& info) {
  return info.type() == NodeType::kUnknown && !info.possible_maps_are_known();
}

bool NodeInfoTypeIs(const NodeInfo& before, const NodeInfo& after) {
  return NodeTypeIs(after.type(), before.type());
}

bool SameValue(ValueNode* before, ValueNode* after) { return before == after; }

}  // namespace

bool KnownNodeAspects::IsCompatibleWithLoopHeader(
    const KnownNodeAspects& loop_header) const {
  // Needs to be in sync with `CloneForLoopHeader(zone, true)`.

  bool had_effects = effect_epoch() != loop_header.effect_epoch();

  if (!had_effects) {
    if (!AspectIncludes(loop_header.node_infos, node_infos, NodeInfoTypeIs,
                        NodeInfoIsEmpty)) {
      if (V8_UNLIKELY(v8_flags.trace_maglev_loop_speeling)) {
        std::cout << "KNA after effectless loop has incompatible node_infos\n";
      }
      return false;
    }
    // In debug builds we do a full comparison to ensure that without an effect
    // epoch change all unstable properties still hold.
#ifndef DEBUG
    return true;
#endif
  }

  if (!AspectIncludes(loop_header.node_infos, node_infos, NodeInfoIncludes,
                      NodeInfoIsEmpty)) {
    if (V8_UNLIKELY(v8_flags.trace_maglev_loop_speeling)) {
      std::cout << "KNA after loop has incompatible node_infos\n";
    }
    DCHECK(had_effects);
    return false;
  }

  if (!MaybeEmptyAspectIncludes(
          loop_header.loaded_properties, loaded_properties,
          [](auto a, auto b) { return AspectIncludes(a, b, SameValue); })) {
    if (V8_UNLIKELY(v8_flags.trace_maglev_loop_speeling)) {
      std::cout << "KNA after loop has incompatible loaded_properties\n";
    }
    DCHECK(had_effects);
    return false;
  }
  if (!AspectIncludes(loop_header.loaded_context_slots, loaded_context_slots,
                      SameValue)) {
    if (V8_UNLIKELY(v8_flags.trace_maglev_loop_speeling)) {
      std::cout << "KNA after loop has incompatible loaded_context_slots\n";
    }
    DCHECK(had_effects);
    return false;
  }

  return true;
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
  state.virtual_objects().Snapshot();
  merge_state->set_virtual_objects(state.virtual_objects());
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
  state->loop_metadata_ = LoopMetadata{loop_info, nullptr};
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
    bool was_used, interpreter::Register context_register, Graph* graph) {
  Zone* const zone = unit.zone();
  MergePointInterpreterFrameState* state =
      zone->New<MergePointInterpreterFrameState>(
          unit, handler_offset, 0, 0, nullptr,
          was_used ? BasicBlockType::kExceptionHandlerStart
                   : BasicBlockType::kUnusedExceptionHandlerStart,
          liveness);
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

void MergePointInterpreterFrameState::MergePhis(
    MaglevGraphBuilder* builder, MaglevCompilationUnit& compilation_unit,
    InterpreterFrameState& unmerged, BasicBlock* predecessor,
    bool optimistic_loop_phis) {
  int i = 0;
  frame_state_.ForEachValue(
      compilation_unit, [&](ValueNode*& value, interpreter::Register reg) {
        PrintBeforeMerge(compilation_unit, value, unmerged.get(reg), reg,
                         known_node_aspects_);
        value = MergeValue(builder, reg, *unmerged.known_node_aspects(), value,
                           unmerged.get(reg), &per_predecessor_alternatives_[i],
                           optimistic_loop_phis);
        PrintAfterMerge(compilation_unit, value, known_node_aspects_);
        ++i;
      });
}

void MergePointInterpreterFrameState::MergeVirtualObject(
    MaglevGraphBuilder* builder, const VirtualObject::List unmerged_vos,
    const KnownNodeAspects& unmerged_aspects, VirtualObject* merged,
    VirtualObject* unmerged) {
  if (merged == unmerged) {
    // No need to merge.
    return;
  }
  // Currently, the graph builder will never change the VO map.
  DCHECK(unmerged->map().equals(merged->map()));
  DCHECK_EQ(merged->slot_count(), unmerged->slot_count());
  DCHECK_EQ(merged->allocation(), unmerged->allocation());

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << " - Merging VOS: "
              << PrintNodeLabel(builder->compilation_unit()->graph_labeller(),
                                merged)
              << "(merged) and "
              << PrintNodeLabel(builder->compilation_unit()->graph_labeller(),
                                unmerged)
              << "(unmerged)" << std::endl;
  }

  VirtualObject* result = builder->CreateVirtualObjectForMerge(
      unmerged->map(), unmerged->slot_count());
  for (uint32_t i = 0; i < merged->slot_count(); i++) {
    std::optional<ValueNode*> merged_value_opt = MergeVirtualObjectValue(
        builder, unmerged_aspects, merged->get_by_index(i),
        unmerged->get_by_index(i));
    if (!merged_value_opt.has_value()) {
      // Merge failed, we should escape the allocation instead.
      unmerged->allocation()->ForceEscaping();
      return;
    }
    result->set_by_index(i, merged_value_opt.value());
  }
  result->set_allocation(unmerged->allocation());
  result->Snapshot();
  unmerged->allocation()->UpdateObject(result);
  frame_state_.virtual_objects().Add(result);
}

void MergePointInterpreterFrameState::MergeVirtualObjects(
    MaglevGraphBuilder* builder, MaglevCompilationUnit& compilation_unit,
    const VirtualObject::List unmerged_vos,
    const KnownNodeAspects& unmerged_aspects) {
  if (frame_state_.virtual_objects().is_empty()) return;
  if (unmerged_vos.is_empty()) return;

  frame_state_.virtual_objects().Snapshot();

  PrintVirtualObjects(compilation_unit, unmerged_vos, "VOs before merge:");

  SmallZoneMap<InlinedAllocation*, VirtualObject*, 10> unmerged_map(
      builder->zone());
  SmallZoneMap<InlinedAllocation*, VirtualObject*, 10> merged_map(
      builder->zone());

  // We iterate both list in reversed order of ids collecting the umerged
  // objects into the map, until we find a common virtual object.
  VirtualObject::List::WalkUntilCommon(
      frame_state_.virtual_objects(), unmerged_vos,
      [&](VirtualObject* vo, VirtualObject::List vos) {
        // If we have a version in the map, it should be the most up-to-date,
        // since the list is in reverse order.
        auto& map = unmerged_vos == vos ? unmerged_map : merged_map;
        map.emplace(vo->allocation(), vo);
      });

  // Walk the merged map (values from the merged state) and merge values.
  for (auto [_, merged] : merged_map) {
    VirtualObject* unmerged = nullptr;
    auto it = unmerged_map.find(merged->allocation());
    if (it != unmerged_map.end()) {
      unmerged = it->second;
      unmerged_map.erase(it);
    } else {
      unmerged = unmerged_vos.FindAllocatedWith(merged->allocation());
    }
    if (unmerged != nullptr) {
      MergeVirtualObject(builder, unmerged_vos, unmerged_aspects, merged,
                         unmerged);
    }
  }

  // Walk the unmerged map (values from the interpreter frame state) and merge
  // values. If the value was already merged, we would have removed from the
  // unmerged_map.
  for (auto [_, unmerged] : unmerged_map) {
    VirtualObject* merged = nullptr;
    auto it = merged_map.find(unmerged->allocation());
    if (it != merged_map.end()) {
      merged = it->second;
    } else {
      merged = frame_state_.virtual_objects().FindAllocatedWith(
          unmerged->allocation());
    }
    if (merged != nullptr) {
      MergeVirtualObject(builder, unmerged_vos, unmerged_aspects, merged,
                         unmerged);
    }
  }

  PrintVirtualObjects(compilation_unit, unmerged_vos, "VOs after merge:");
}

void MergePointInterpreterFrameState::InitializeLoop(
    MaglevGraphBuilder* builder, MaglevCompilationUnit& compilation_unit,
    InterpreterFrameState& unmerged, BasicBlock* predecessor,
    bool optimistic_initial_state, LoopEffects* loop_effects) {
  DCHECK_IMPLIES(optimistic_initial_state,
                 v8_flags.maglev_optimistic_peeled_loops);
  DCHECK_GT(predecessor_count_, 1);
  DCHECK_EQ(predecessors_so_far_, 0);
  predecessors_[predecessors_so_far_] = predecessor;

  DCHECK_NULL(known_node_aspects_);
  DCHECK(is_unmerged_loop());
  DCHECK_EQ(predecessors_so_far_, 0);
  known_node_aspects_ = unmerged.known_node_aspects()->CloneForLoopHeader(
      builder->zone(), optimistic_initial_state, loop_effects);
  unmerged.virtual_objects().Snapshot();
  frame_state_.set_virtual_objects(unmerged.virtual_objects());
  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "Initializing "
              << (optimistic_initial_state ? "optimistic " : "")
              << "loop state..." << std::endl;
  }

  MergePhis(builder, compilation_unit, unmerged, predecessor,
            optimistic_initial_state);

  predecessors_so_far_ = 1;
}

void MergePointInterpreterFrameState::InitializeWithBasicBlock(
    BasicBlock* block) {
  for (Phi* phi : phis_) {
    phi->set_owner(block);
  }
}

void MergePointInterpreterFrameState::Merge(
    MaglevGraphBuilder* builder, MaglevCompilationUnit& compilation_unit,
    InterpreterFrameState& unmerged, BasicBlock* predecessor) {
  DCHECK_GT(predecessor_count_, 1);
  DCHECK_LT(predecessors_so_far_, predecessor_count_);
  predecessors_[predecessors_so_far_] = predecessor;

  if (known_node_aspects_ == nullptr) {
    return InitializeLoop(builder, compilation_unit, unmerged, predecessor);
  }

  known_node_aspects_->Merge(*unmerged.known_node_aspects(), builder->zone());
  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "Merging..." << std::endl;
  }

  MergeVirtualObjects(builder, compilation_unit, unmerged.virtual_objects(),
                      *unmerged.known_node_aspects());
  MergePhis(builder, compilation_unit, unmerged, predecessor, false);

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

  // We have to clear the LoopInfo (which is used to record more precise use
  // hints for Phis) for 2 reasons:
  //
  //  - Phi::RecordUseReprHint checks if a use is inside the loop defining the
  //    Phi by checking if the LoopInfo of the loop Phi "Contains" the current
  //    bytecode offset, but this will be wrong if the Phi is in a function that
  //    was inlined (because the LoopInfo contains the first and last bytecode
  //    offset of the loop **in its own function**).
  //
  //  - LoopInfo is obtained from the {header_to_info_} member of
  //    BytecodeAnalysis, but the BytecodeAnalysis is a member of the
  //    MaglevGraphBuilder, and thus gets destructed when the MaglevGraphBuilder
  //    created for inlining is destructed. LoopInfo would then become a stale
  //    pointer.
  ClearLoopInfo();
}

bool MergePointInterpreterFrameState::TryMergeLoop(
    MaglevGraphBuilder* builder, InterpreterFrameState& loop_end_state,
    const std::function<BasicBlock*()>& FinishBlock) {
  // This should be the last predecessor we try to merge.
  DCHECK_EQ(predecessors_so_far_, predecessor_count_ - 1);
  DCHECK(is_unmerged_loop());

  backedge_deopt_frame_ =
      builder->zone()->New<DeoptFrame>(builder->GetLatestCheckpointedFrame());

  auto& compilation_unit = *builder->compilation_unit();

  DCHECK_NOT_NULL(known_node_aspects_);
  DCHECK(v8_flags.maglev_optimistic_peeled_loops);

  // TODO(olivf): This could be done faster by consulting loop_effects_
  if (!loop_end_state.known_node_aspects()->IsCompatibleWithLoopHeader(
          *known_node_aspects_)) {
    if (v8_flags.trace_maglev_graph_building) {
      std::cout << "Merging failed, peeling loop instead... " << std::endl;
    }
    ClearLoopInfo();
    return false;
  }

  bool phis_can_merge = true;
  frame_state_.ForEachValue(compilation_unit, [&](ValueNode* value,
                                                  interpreter::Register reg) {
    if (!value->Is<Phi>()) return;
    Phi* phi = value->Cast<Phi>();
    if (!phi->is_loop_phi()) return;
    if (phi->merge_state() != this) return;
    NodeType old_type = GetNodeType(builder->broker(), builder->local_isolate(),
                                    *known_node_aspects_, phi);
    if (old_type != NodeType::kUnknown) {
      NodeType new_type = GetNodeType(
          builder->broker(), builder->local_isolate(),
          *loop_end_state.known_node_aspects(), loop_end_state.get(reg));
      if (!NodeTypeIs(new_type, old_type)) {
        if (v8_flags.trace_maglev_loop_speeling) {
          std::cout << "Cannot merge " << new_type << " into " << old_type
                    << " for r" << reg.index() << "\n";
        }
        phis_can_merge = false;
      }
    }
  });
  if (!phis_can_merge) {
    ClearLoopInfo();
    return false;
  }

  BasicBlock* loop_end_block = FinishBlock();
  int input = predecessor_count_ - 1;
  loop_end_block->set_predecessor_id(input);
  predecessors_[input] = loop_end_block;

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "Next peeling not needed due to compatible state" << std::endl;
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
  ClearLoopInfo();
  return true;
}

void MergePointInterpreterFrameState::set_loop_effects(
    LoopEffects* loop_effects) {
  DCHECK(is_loop());
  DCHECK(loop_metadata_.has_value());
  loop_metadata_->loop_effects = loop_effects;
}

const LoopEffects* MergePointInterpreterFrameState::loop_effects() {
  DCHECK(is_loop());
  DCHECK(loop_metadata_.has_value());
  return loop_metadata_->loop_effects;
}

void MergePointInterpreterFrameState::MergeThrow(
    MaglevGraphBuilder* builder, const MaglevCompilationUnit* handler_unit,
    const KnownNodeAspects& known_node_aspects,
    const VirtualObject::List virtual_objects) {
  // We don't count total predecessors on exception handlers, but we do want to
  // special case the first predecessor so we do count predecessors_so_far
  DCHECK_EQ(predecessor_count_, 0);
  DCHECK(is_exception_handler());

  DCHECK_EQ(builder->compilation_unit(), handler_unit);

  const InterpreterFrameState& builder_frame =
      builder->current_interpreter_frame();

  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "- Merging into exception handler @" << this << std::endl;
    PrintVirtualObjects(*handler_unit, virtual_objects);
  }

  if (known_node_aspects_ == nullptr) {
    DCHECK_EQ(predecessors_so_far_, 0);
    known_node_aspects_ = known_node_aspects.Clone(builder->zone());
    virtual_objects.Snapshot();
    frame_state_.set_virtual_objects(virtual_objects);
  } else {
    known_node_aspects_->Merge(known_node_aspects, builder->zone());
    MergeVirtualObjects(builder, *builder->compilation_unit(), virtual_objects,
                        known_node_aspects);
  }

  frame_state_.ForEachParameter(
      *handler_unit, [&](ValueNode*& value, interpreter::Register reg) {
        PrintBeforeMerge(*handler_unit, value, builder_frame.get(reg), reg,
                         known_node_aspects_);
        value = MergeValue(builder, reg, known_node_aspects, value,
                           builder_frame.get(reg), nullptr);
        PrintAfterMerge(*handler_unit, value, known_node_aspects_);
      });
  frame_state_.ForEachLocal(
      *handler_unit, [&](ValueNode*& value, interpreter::Register reg) {
        PrintBeforeMerge(*handler_unit, value, builder_frame.get(reg), reg,
                         known_node_aspects_);
        value = MergeValue(builder, reg, known_node_aspects, value,
                           builder_frame.get(reg), nullptr);
        PrintAfterMerge(*handler_unit, value, known_node_aspects_);
      });

  // Pick out the context value from the incoming registers.
  // TODO(leszeks): This should be the same for all incoming states, but we lose
  // the identity for generator-restored context. If generator value restores
  // were handled differently, we could avoid emitting a Phi here.
  ValueNode*& context = frame_state_.context(*handler_unit);
  PrintBeforeMerge(*handler_unit, context,
                   builder_frame.get(catch_block_context_register_),
                   catch_block_context_register_, known_node_aspects_);
  context = MergeValue(
      builder, catch_block_context_register_, known_node_aspects, context,
      builder_frame.get(catch_block_context_register_), nullptr);
  PrintAfterMerge(*handler_unit, context, known_node_aspects_);

  predecessors_so_far_++;
}

namespace {

ValueNode* FromInt32ToTagged(const MaglevGraphBuilder* builder,
                             NodeType node_type, ValueNode* value,
                             BasicBlock* predecessor) {
  DCHECK_EQ(value->properties().value_representation(),
            ValueRepresentation::kInt32);
  DCHECK(!value->properties().is_conversion());

  ValueNode* tagged;
  if (value->Is<Int32Constant>()) {
    int32_t constant = value->Cast<Int32Constant>()->value();
    if (Smi::IsValid(constant)) {
      return builder->GetSmiConstant(constant);
    }
  }

  if (value->Is<StringLength>() ||
      value->Is<BuiltinStringPrototypeCharCodeOrCodePointAt>()) {
    static_assert(String::kMaxLength <= kSmiMaxValue,
                  "String length must fit into a Smi");
    tagged = Node::New<UnsafeSmiTagInt32>(builder->zone(), {value});
  } else if (NodeTypeIsSmi(node_type)) {
    // For known Smis, we can tag without a check.
    tagged = Node::New<UnsafeSmiTagInt32>(builder->zone(), {value});
  } else {
    tagged = Node::New<Int32ToNumber>(builder->zone(), {value});
  }

  predecessor->nodes().Add(tagged);
  builder->compilation_unit()->RegisterNodeInGraphLabeller(tagged);
  return tagged;
}

ValueNode* FromUint32ToTagged(const MaglevGraphBuilder* builder,
                              NodeType node_type, ValueNode* value,
                              BasicBlock* predecessor) {
  DCHECK_EQ(value->properties().value_representation(),
            ValueRepresentation::kUint32);
  DCHECK(!value->properties().is_conversion());

  ValueNode* tagged;
  if (NodeTypeIsSmi(node_type)) {
    tagged = Node::New<UnsafeSmiTagUint32>(builder->zone(), {value});
  } else {
    tagged = Node::New<Uint32ToNumber>(builder->zone(), {value});
  }

  predecessor->nodes().Add(tagged);
  builder->compilation_unit()->RegisterNodeInGraphLabeller(tagged);
  return tagged;
}

ValueNode* FromFloat64ToTagged(const MaglevGraphBuilder* builder,
                               NodeType node_type, ValueNode* value,
                               BasicBlock* predecessor) {
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

ValueNode* FromHoleyFloat64ToTagged(const MaglevGraphBuilder* builder,
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

ValueNode* NonTaggedToTagged(const MaglevGraphBuilder* builder,
                             NodeType node_type, ValueNode* value,
                             BasicBlock* predecessor) {
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
ValueNode* EnsureTagged(const MaglevGraphBuilder* builder,
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

}  // namespace

NodeType MergePointInterpreterFrameState::AlternativeType(
    const Alternatives* alt) {
  if (!alt) return NodeType::kUnknown;
  return alt->node_type();
}

ValueNode* MergePointInterpreterFrameState::MergeValue(
    const MaglevGraphBuilder* builder, interpreter::Register owner,
    const KnownNodeAspects& unmerged_aspects, ValueNode* merged,
    ValueNode* unmerged, Alternatives::List* per_predecessor_alternatives,
    bool optimistic_loop_phis) {
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

  auto UpdateLoopPhiType = [&](Phi* result, NodeType unmerged_type) {
    DCHECK(result->is_loop_phi());
    if (predecessors_so_far_ == 0) {
      // For loop Phis, `type` is always Unknown until the backedge has been
      // bound, so there is no point in updating it here.
      result->set_post_loop_type(unmerged_type);
      if (optimistic_loop_phis) {
        // In the case of optimistic loop headers we try to speculatively use
        // the type of the incomming argument as the phi type. We verify if that
        // happened to be true before allowing the loop to conclude in
        // `TryMergeLoop`. Some types which are known to cause issues are
        // generalized here.
        NodeType initial_optimistic_type =
            (unmerged_type == NodeType::kInternalizedString) ? NodeType::kString
                                                             : unmerged_type;
        result->set_type(initial_optimistic_type);
      }
    } else {
      if (optimistic_loop_phis) {
        if (NodeInfo* node_info = known_node_aspects_->TryGetInfoFor(result)) {
          node_info->IntersectType(unmerged_type);
        }
        result->merge_type(unmerged_type);
      }
      result->merge_post_loop_type(unmerged_type);
    }
  };

  Phi* result = merged->TryCast<Phi>();
  if (result != nullptr && result->merge_state() == this) {
    // It's possible that merged == unmerged at this point since loop-phis are
    // not dropped if they are only assigned to themselves in the loop.
    DCHECK_EQ(result->owner(), owner);
    // Don't set inputs on exception phis.
    DCHECK_EQ(result->is_exception_phi(), is_exception_handler());
    if (is_exception_handler()) {
      // If an inlined allocation flows to an exception phi, we should consider
      // as an use.
      if (unmerged->Is<InlinedAllocation>()) {
        unmerged->add_use();
      }
      return result;
    }

    NodeType unmerged_type =
        GetNodeType(builder->broker(), builder->local_isolate(),
                    unmerged_aspects, unmerged);
    if (result->is_loop_phi()) {
      UpdateLoopPhiType(result, unmerged_type);
    } else {
      result->merge_type(unmerged_type);
    }
    unmerged = EnsureTagged(builder, unmerged_aspects, unmerged,
                            predecessors_[predecessors_so_far_]);
    result->set_input(predecessors_so_far_, unmerged);

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
    // ... and add an use if inputs are inlined allocation.
    if (merged->Is<InlinedAllocation>()) {
      merged->add_use();
    }
    if (unmerged->Is<InlinedAllocation>()) {
      unmerged->add_use();
    }
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

  if (result->is_loop_phi()) {
    DCHECK(result->is_unmerged_loop_phi());
    UpdateLoopPhiType(result, type);
  } else {
    result->set_type(IntersectType(type, unmerged_type));
  }

  phis_.Add(result);
  return result;
}

std::optional<ValueNode*>
MergePointInterpreterFrameState::MergeVirtualObjectValue(
    const MaglevGraphBuilder* builder, const KnownNodeAspects& unmerged_aspects,
    ValueNode* merged, ValueNode* unmerged) {
  DCHECK_NOT_NULL(merged);
  DCHECK_NOT_NULL(unmerged);

  Phi* result = merged->TryCast<Phi>();
  if (result != nullptr && result->merge_state() == this) {
    NodeType unmerged_type =
        GetNodeType(builder->broker(), builder->local_isolate(),
                    unmerged_aspects, unmerged);
    unmerged = EnsureTagged(builder, unmerged_aspects, unmerged,
                            predecessors_[predecessors_so_far_]);
    result->set_input(predecessors_so_far_, unmerged);
    DCHECK_GT(predecessors_so_far_, 0);
    result->merge_type(unmerged_type);
    result->merge_post_loop_type(unmerged_type);
    return result;
  }

  if (merged == unmerged) {
    return merged;
  }

  if (InlinedAllocation* merged_nested_alloc =
          merged->TryCast<InlinedAllocation>()) {
    if (InlinedAllocation* unmerged_nested_alloc =
            unmerged->TryCast<InlinedAllocation>()) {
      // If a nested allocation doesn't point to the same object in both
      // objects, then we currently give up merging them and escape the
      // allocation.
      if (merged_nested_alloc != unmerged_nested_alloc) {
        return {};
      }
    }
  }

  // We don't support exception phis inside a virtual object.
  if (is_exception_handler()) {
    return {};
  }

  result = Node::New<Phi>(builder->zone(), predecessor_count_, this,
                          interpreter::Register::invalid_value());
  if (v8_flags.trace_maglev_graph_building) {
    for (int i = 0; i < predecessor_count_; i++) {
      result->initialize_input_null(i);
    }
  }

  NodeType merged_type =
      StaticTypeForNode(builder->broker(), builder->local_isolate(), merged);

  // We must have seen the same value so far.
  DCHECK_NOT_NULL(known_node_aspects_);
  for (int i = 0; i < predecessors_so_far_; i++) {
    ValueNode* tagged_merged =
        EnsureTagged(builder, *known_node_aspects_, merged, predecessors_[i]);
    result->set_input(i, tagged_merged);
  }

  NodeType unmerged_type = GetNodeType(
      builder->broker(), builder->local_isolate(), unmerged_aspects, unmerged);
  unmerged = EnsureTagged(builder, unmerged_aspects, unmerged,
                          predecessors_[predecessors_so_far_]);
  result->set_input(predecessors_so_far_, unmerged);

  result->set_type(IntersectType(merged_type, unmerged_type));

  phis_.Add(result);
  return result;
}

void MergePointInterpreterFrameState::MergeLoopValue(
    MaglevGraphBuilder* builder, interpreter::Register owner,
    const KnownNodeAspects& unmerged_aspects, ValueNode* merged,
    ValueNode* unmerged) {
  Phi* result = merged->TryCast<Phi>();
  if (result == nullptr || result->merge_state() != this) {
    // Not a loop phi, we don't have to do anything.
    return;
  }
  DCHECK_EQ(result->owner(), owner);
  NodeType type = GetNodeType(builder->broker(), builder->local_isolate(),
                              unmerged_aspects, unmerged);
  unmerged = EnsureTagged(builder, unmerged_aspects, unmerged,
                          predecessors_[predecessors_so_far_]);
  result->set_input(predecessor_count_ - 1, unmerged);

  result->merge_post_loop_type(type);
  // We've just merged the backedge, which means that future uses of this Phi
  // will be after the loop, so we can now promote `post_loop_type` to the
  // regular `type`.
  DCHECK_EQ(predecessors_so_far_, predecessor_count_ - 1);
  result->promote_post_loop_type();

  if (Phi* unmerged_phi = unmerged->TryCast<Phi>()) {
    // Propagating the `uses_repr` from {result} to {unmerged_phi}.
    builder->RecordUseReprHint(unmerged_phi, result->get_uses_repr_hints());

    // Soundness of the loop phi Smi type relies on the back-edge static types
    // sminess.
    if (result->uses_require_31_bit_value()) {
      unmerged_phi->SetUseRequires31BitValue();
    }
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

void MergePointInterpreterFrameState::ReducePhiPredecessorCount(unsigned num) {
  for (Phi* phi : phis_) {
    phi->reduce_input_count(num);
    if (predecessors_so_far_ == predecessor_count_ - 1 &&
        predecessor_count_ > 1 && phi->is_loop_phi()) {
      phi->promote_post_loop_type();
    }
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
