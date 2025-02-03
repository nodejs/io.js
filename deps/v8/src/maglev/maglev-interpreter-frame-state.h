// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_INTERPRETER_FRAME_STATE_H_
#define V8_MAGLEV_MAGLEV_INTERPRETER_FRAME_STATE_H_

#include <optional>

#include "src/base/threaded-list.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/interpreter/bytecode-register.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-ir.h"
#ifdef V8_ENABLE_MAGLEV
#include "src/maglev/maglev-regalloc-data.h"
#endif
#include "src/maglev/maglev-register-frame-array.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace maglev {

class BasicBlock;
class Graph;
class MaglevGraphBuilder;
class MergePointInterpreterFrameState;

// Destructively intersects the right map into the left map, such that the
// left map is mutated to become the result of the intersection. Values that
// are in both maps are passed to the merging function to be merged with each
// other -- again, the LHS here is expected to be mutated.
template <typename Key, typename Value,
          typename MergeFunc = std::equal_to<Value>>
void DestructivelyIntersect(ZoneMap<Key, Value>& lhs_map,
                            const ZoneMap<Key, Value>& rhs_map,
                            MergeFunc&& func = MergeFunc()) {
  // Walk the two maps in lock step. This relies on the fact that ZoneMaps are
  // sorted.
  typename ZoneMap<Key, Value>::iterator lhs_it = lhs_map.begin();
  typename ZoneMap<Key, Value>::const_iterator rhs_it = rhs_map.begin();
  while (lhs_it != lhs_map.end() && rhs_it != rhs_map.end()) {
    if (lhs_it->first < rhs_it->first) {
      // Remove from LHS elements that are not in RHS.
      lhs_it = lhs_map.erase(lhs_it);
    } else if (rhs_it->first < lhs_it->first) {
      // Skip over elements that are only in RHS.
      ++rhs_it;
    } else {
      // Apply the merge function to the values of the two iterators. If the
      // function returns false, remove the value.
      bool keep_value = func(lhs_it->second, rhs_it->second);
      if (keep_value) {
        ++lhs_it;
      } else {
        lhs_it = lhs_map.erase(lhs_it);
      }
      ++rhs_it;
    }
  }
  // If we haven't reached the end of LHS by now, then we have reached the end
  // of RHS, and the remaining items are therefore not in RHS. Remove them.
  if (lhs_it != lhs_map.end()) {
    lhs_map.erase(lhs_it, lhs_map.end());
  }
}

using PossibleMaps = compiler::ZoneRefSet<Map>;

class NodeInfo {
 public:
  NodeInfo() = default;

  struct ClearUnstableMapsOnCopy {
    const NodeInfo& val;
  };
  explicit NodeInfo(ClearUnstableMapsOnCopy other) V8_NOEXCEPT {
    type_ = other.val.type_;
    alternative_ = other.val.alternative_;
    if (other.val.possible_maps_are_known_ && !other.val.any_map_is_unstable_) {
      possible_maps_ = other.val.possible_maps_;
      possible_maps_are_known_ = true;
    }
  }

  NodeType type() const { return type_; }
  NodeType CombineType(NodeType other) {
    return type_ = maglev::CombineType(type_, other);
  }
  NodeType IntersectType(NodeType other) {
    return type_ = maglev::IntersectType(type_, other);
  }

  // Optional alternative nodes with the equivalent value but a different
  // representation.
  class AlternativeNodes {
   public:
    AlternativeNodes() { store_.fill(nullptr); }

#define ALTERNATIVES(V)                                \
  V(tagged, Tagged)                                    \
  V(int32, Int32)                                      \
  V(truncated_int32_to_number, TruncatedInt32ToNumber) \
  V(float64, Float64)                                  \
  V(constant, Constant)

    enum Kind {
#define KIND(name, Name) k##Name,
      ALTERNATIVES(KIND)
#undef KIND
          kNumberOfAlternatives
    };

#define API(name, Name)                                      \
  ValueNode* name() const { return store_[Kind::k##Name]; }  \
  ValueNode* set_##name(ValueNode* val) {                    \
    return store_[Kind::k##Name] = val;                      \
  }                                                          \
  template <typename Function>                               \
  ValueNode* get_or_set_##name(Function create) {            \
    if (store_[Kind::k##Name]) return store_[Kind::k##Name]; \
    return store_[Kind::k##Name] = create();                 \
  }
    ALTERNATIVES(API)
#undef API
#undef ALTERNATIVES

    bool has_none() const { return store_ == AlternativeNodes().store_; }

    void MergeWith(const AlternativeNodes& other) {
      for (size_t i = 0; i < Kind::kNumberOfAlternatives; ++i) {
        if (store_[i] && store_[i] != other.store_[i]) {
          store_[i] = nullptr;
        }
      }
    }

   private:
    // TODO(leszeks): At least one of these is redundant for every node,
    // consider a more compressed form or even linked list.
    std::array<ValueNode*, Kind::kNumberOfAlternatives> store_;

    // Prevent callers from copying these when they try to update the
    // alternatives by making these private.
    AlternativeNodes(const AlternativeNodes&) V8_NOEXCEPT = default;
    AlternativeNodes& operator=(const AlternativeNodes&) V8_NOEXCEPT = default;
    friend class NodeInfo;
  };

  const AlternativeNodes& alternative() const { return alternative_; }
  AlternativeNodes& alternative() { return alternative_; }

  bool no_info_available() const {
    return type_ == NodeType::kUnknown && alternative_.has_none() &&
           !possible_maps_are_known_;
  }

  bool is_smi() const { return NodeTypeIsSmi(type_); }
  bool is_any_heap_object() const { return NodeTypeIsAnyHeapObject(type_); }
  bool is_string() const { return NodeTypeIsString(type_); }
  bool is_internalized_string() const {
    return NodeTypeIsInternalizedString(type_);
  }
  bool is_symbol() const { return NodeTypeIsSymbol(type_); }

  // Mutate this node info by merging in another node info, with the result
  // being a node info that is the subset of information valid in both inputs.
  void MergeWith(const NodeInfo& other, Zone* zone,
                 bool& any_merged_map_is_unstable) {
    IntersectType(other.type_);
    alternative_.MergeWith(other.alternative_);
    if (possible_maps_are_known_) {
      if (other.possible_maps_are_known_) {
        // Map sets are the set of _possible_ maps, so on a merge we need to
        // _union_ them together (i.e. intersect the set of impossible maps).
        // Remember whether _any_ of these merges observed unstable maps.
        possible_maps_.Union(other.possible_maps_, zone);
      } else {
        possible_maps_.clear();
        possible_maps_are_known_ = false;
      }
    }

    any_map_is_unstable_ = possible_maps_are_known_ &&
                           (any_map_is_unstable_ || other.any_map_is_unstable_);
    any_merged_map_is_unstable =
        any_merged_map_is_unstable || any_map_is_unstable_;
  }

  bool possible_maps_are_unstable() const { return any_map_is_unstable_; }

  void ClearUnstableMaps() {
    if (!any_map_is_unstable_) return;
    possible_maps_.clear();
    possible_maps_are_known_ = false;
    any_map_is_unstable_ = false;
  }

  template <typename Function>
  void ClearUnstableMapsIfAny(const Function& condition) {
    if (!any_map_is_unstable_) return;
    for (auto map : possible_maps_) {
      if (condition(map)) {
        ClearUnstableMaps();
        return;
      }
    }
  }

  bool possible_maps_are_known() const { return possible_maps_are_known_; }

  const PossibleMaps& possible_maps() const {
    // If !possible_maps_are_known_ then every map is possible and using the
    // (probably empty) possible_maps_ set is definetly wrong.
    CHECK(possible_maps_are_known_);
    return possible_maps_;
  }

  void SetPossibleMaps(const PossibleMaps& possible_maps,
                       bool any_map_is_unstable, NodeType possible_type,
                       compiler::JSHeapBroker* broker) {
    possible_maps_ = possible_maps;
    possible_maps_are_known_ = true;
    any_map_is_unstable_ = any_map_is_unstable;
#ifdef DEBUG
    if (possible_maps.size()) {
      NodeType expected = StaticTypeForMap(*possible_maps.begin(), broker);
      for (auto map : possible_maps) {
        expected =
            maglev::IntersectType(StaticTypeForMap(map, broker), expected);
      }
      // Ensure the claimed type is not narrower than what can be learned from
      // the map checks.
      DCHECK(NodeTypeIs(expected, possible_type));
    } else {
      DCHECK_EQ(possible_type, NodeType::kUnknown);
    }
#endif
    CombineType(possible_type);
  }

  bool any_map_is_unstable() const { return any_map_is_unstable_; }

 private:
  NodeType type_ = NodeType::kUnknown;

  bool any_map_is_unstable_ = false;

  // Maps for a node. Sets of maps that only contain stable maps are valid
  // across side-effecting calls, as long as we install a dependency, otherwise
  // they are cleared on side-effects.
  // TODO(v8:7700): Investigate a better data structure to use than ZoneMap.
  bool possible_maps_are_known_ = false;
  PossibleMaps possible_maps_;

  AlternativeNodes alternative_;
};

struct LoopEffects;

struct KnownNodeAspects {
  // Permanently valid if checked in a dominator.
  using NodeInfos = ZoneMap<ValueNode*, NodeInfo>;

  explicit KnownNodeAspects(Zone* zone)
      : any_map_for_any_node_is_unstable(false),
        loaded_constant_properties(zone),
        loaded_properties(zone),
        loaded_context_constants(zone),
        loaded_context_slots(zone),
        available_expressions(zone),
        node_infos(zone),
        effect_epoch_(0) {}

  // Copy constructor is defaulted but private so that we explicitly call the
  // Clone method.
  KnownNodeAspects& operator=(const KnownNodeAspects& other) = delete;
  KnownNodeAspects(KnownNodeAspects&& other) = delete;
  KnownNodeAspects& operator=(KnownNodeAspects&& other) = delete;

  KnownNodeAspects* Clone(Zone* zone) const {
    return zone->New<KnownNodeAspects>(*this);
  }

  // Loop headers can safely clone the node types, since those won't be
  // invalidated in the loop body, and similarly stable maps will have
  // dependencies installed. Unstable maps however might be invalidated by
  // calls, and we don't know about these until it's too late.
  KnownNodeAspects* CloneForLoopHeader(Zone* zone,
                                       bool optimistic_initial_state,
                                       LoopEffects* loop_effects) const;

  void ClearUnstableNodeAspects();

  void ClearUnstableMaps() {
    // A side effect could change existing objects' maps. For stable maps we
    // know this hasn't happened (because we added a dependency on the maps
    // staying stable and therefore not possible to transition away from), but
    // we can no longer assume that objects with unstable maps still have the
    // same map. Unstable maps can also transition to stable ones, so we have to
    // clear _all_ maps for a node if it had _any_ unstable map.
    if (!any_map_for_any_node_is_unstable) return;
    for (auto& it : node_infos) {
      it.second.ClearUnstableMaps();
    }
    any_map_for_any_node_is_unstable = false;
  }

  template <typename Function>
  void ClearUnstableMapsIfAny(const Function& condition) {
    if (!any_map_for_any_node_is_unstable) return;
    for (auto& it : node_infos) {
      it.second.ClearUnstableMapsIfAny(condition);
    }
  }

  void ClearAvailableExpressions() { available_expressions.clear(); }

  NodeInfos::iterator FindInfo(ValueNode* node) {
    return node_infos.find(node);
  }
  NodeInfos::const_iterator FindInfo(ValueNode* node) const {
    return node_infos.find(node);
  }
  bool IsValid(NodeInfos::iterator& it) { return it != node_infos.end(); }
  bool IsValid(NodeInfos::const_iterator& it) const {
    return it != node_infos.end();
  }

  const NodeInfo* TryGetInfoFor(ValueNode* node) const {
    return const_cast<KnownNodeAspects*>(this)->TryGetInfoFor(node);
  }
  NodeInfo* TryGetInfoFor(ValueNode* node) {
    auto info_it = FindInfo(node);
    if (!IsValid(info_it)) return nullptr;
    return &info_it->second;
  }
  NodeInfo* GetOrCreateInfoFor(ValueNode* node, compiler::JSHeapBroker* broker,
                               LocalIsolate* isolate) {
    auto info_it = FindInfo(node);
    if (IsValid(info_it)) return &info_it->second;
    auto res = &node_infos.emplace(node, NodeInfo()).first->second;
    res->CombineType(StaticTypeForNode(broker, isolate, node));
    return res;
  }

  NodeType NodeTypeFor(ValueNode* node) const {
    if (auto info = TryGetInfoFor(node)) {
      return info->type();
    }
    return NodeType::kUnknown;
  }

  void Merge(const KnownNodeAspects& other, Zone* zone);

  // If IsCompatibleWithLoopHeader(other) returns true, it means that
  // Merge(other) would not remove any information from `this`.
  bool IsCompatibleWithLoopHeader(const KnownNodeAspects& other) const;

  // TODO(leszeks): Store these more efficiently than with std::map -- in
  // particular, clear out entries that are no longer reachable, perhaps also
  // allow lookup by interpreter register rather than by node pointer.

  bool any_map_for_any_node_is_unstable;

  // Cached property loads.

  // Represents a key into the cache. This is either a NameRef, or an enum
  // value.
  class LoadedPropertyMapKey {
   public:
    enum Type {
      // kName must be zero so that pointers are unaffected.
      kName = 0,
      kElements,
      kTypedArrayLength,
      // TODO(leszeks): We could probably share kStringLength with
      // kTypedArrayLength if needed.
      kStringLength
    };
    static constexpr int kTypeMask = 0x3;
    static_assert((kName & ~kTypeMask) == 0);
    static_assert((kElements & ~kTypeMask) == 0);
    static_assert((kTypedArrayLength & ~kTypeMask) == 0);
    static_assert((kStringLength & ~kTypeMask) == 0);

    static LoadedPropertyMapKey Elements() {
      return LoadedPropertyMapKey(kElements);
    }

    static LoadedPropertyMapKey TypedArrayLength() {
      return LoadedPropertyMapKey(kTypedArrayLength);
    }

    static LoadedPropertyMapKey StringLength() {
      return LoadedPropertyMapKey(kStringLength);
    }

    // Allow implicit conversion from NameRef to key, so that callers in the
    // common path can use a NameRef directly.
    // NOLINTNEXTLINE
    LoadedPropertyMapKey(compiler::NameRef ref)
        : data_(reinterpret_cast<Address>(ref.data())) {
      DCHECK_EQ(data_ & kTypeMask, kName);
    }

    bool operator==(const LoadedPropertyMapKey& other) const {
      return data_ == other.data_;
    }
    bool operator<(const LoadedPropertyMapKey& other) const {
      return data_ < other.data_;
    }

    compiler::NameRef name() {
      DCHECK_EQ(type(), kName);
      return compiler::NameRef(reinterpret_cast<compiler::ObjectData*>(data_),
                               false);
    }

    Type type() { return static_cast<Type>(data_ & kTypeMask); }

   private:
    explicit LoadedPropertyMapKey(Type type) : data_(type) {
      DCHECK_NE(type, kName);
    }

    Address data_;
  };
  // Maps key->object->value, so that stores to a key can invalidate all loads
  // of that key (in case the objects are aliasing).
  using LoadedPropertyMap =
      ZoneMap<LoadedPropertyMapKey, ZoneMap<ValueNode*, ValueNode*>>;

  // Valid across side-effecting calls, as long as we install a dependency.
  LoadedPropertyMap loaded_constant_properties;
  // Flushed after side-effecting calls.
  LoadedPropertyMap loaded_properties;

  // Unconditionally valid across side-effecting calls.
  ZoneMap<std::tuple<ValueNode*, int>, ValueNode*> loaded_context_constants;
  enum class ContextSlotLoadsAlias : uint8_t {
    None,
    OnlyLoadsRelativeToCurrentContext,
    OnlyLoadsRelativeToConstant,
    Yes,
  };
  ContextSlotLoadsAlias may_have_aliasing_contexts =
      ContextSlotLoadsAlias::None;
  void UpdateMayHaveAliasingContexts(ValueNode* context) {
    if (context->Is<InitialValue>()) {
      if (may_have_aliasing_contexts == ContextSlotLoadsAlias::None) {
        may_have_aliasing_contexts =
            ContextSlotLoadsAlias::OnlyLoadsRelativeToCurrentContext;
      } else if (may_have_aliasing_contexts !=
                 ContextSlotLoadsAlias::OnlyLoadsRelativeToCurrentContext) {
        may_have_aliasing_contexts = ContextSlotLoadsAlias::Yes;
      }
    } else if (context->Is<Constant>()) {
      if (may_have_aliasing_contexts == ContextSlotLoadsAlias::None) {
        may_have_aliasing_contexts =
            ContextSlotLoadsAlias::OnlyLoadsRelativeToConstant;
      } else if (may_have_aliasing_contexts !=
                 ContextSlotLoadsAlias::OnlyLoadsRelativeToConstant) {
        may_have_aliasing_contexts = ContextSlotLoadsAlias::Yes;
      }
    } else if (!context->Is<LoadTaggedField>()) {
      may_have_aliasing_contexts = ContextSlotLoadsAlias::Yes;
    }
  }
  // Flushed after side-effecting calls.
  using LoadedContextSlotsKey = std::tuple<ValueNode*, int>;
  using LoadedContextSlots = ZoneMap<LoadedContextSlotsKey, ValueNode*>;
  LoadedContextSlots loaded_context_slots;

  struct AvailableExpression {
    NodeBase* node;
    uint32_t effect_epoch;
  };
  ZoneMap<uint32_t, AvailableExpression> available_expressions;
  uint32_t effect_epoch() const { return effect_epoch_; }
  static constexpr uint32_t kEffectEpochForPureInstructions =
      std::numeric_limits<uint32_t>::max();
  static constexpr uint32_t kEffectEpochOverflow =
      kEffectEpochForPureInstructions - 1;
  void increment_effect_epoch() {
    if (effect_epoch_ < kEffectEpochOverflow) effect_epoch_++;
  }

 private:
  NodeInfos node_infos;
  uint32_t effect_epoch_;

  friend KnownNodeAspects* Zone::New<KnownNodeAspects, const KnownNodeAspects&>(
      const KnownNodeAspects&);
  KnownNodeAspects(const KnownNodeAspects& other) V8_NOEXCEPT = default;
};

class InterpreterFrameState {
 public:
  InterpreterFrameState(const MaglevCompilationUnit& info,
                        KnownNodeAspects* known_node_aspects,
                        VirtualObject::List virtual_objects)
      : frame_(info),
        known_node_aspects_(known_node_aspects),
        virtual_objects_(virtual_objects) {
    frame_[interpreter::Register::virtual_accumulator()] = nullptr;
  }

  explicit InterpreterFrameState(const MaglevCompilationUnit& info)
      : InterpreterFrameState(info,
                              info.zone()->New<KnownNodeAspects>(info.zone()),
                              VirtualObject::List()) {}

  inline void CopyFrom(const MaglevCompilationUnit& info,
                       MergePointInterpreterFrameState& state,
                       bool preserve_known_node_aspects, Zone* zone);

  void set_accumulator(ValueNode* value) {
    // Conversions should be stored in known_node_aspects/NodeInfo.
    DCHECK(!value->properties().is_conversion());
    frame_[interpreter::Register::virtual_accumulator()] = value;
  }
  ValueNode* accumulator() const {
    return frame_[interpreter::Register::virtual_accumulator()];
  }

  void set(interpreter::Register reg, ValueNode* value) {
    DCHECK_IMPLIES(reg.is_parameter(),
                   reg == interpreter::Register::current_context() ||
                       reg == interpreter::Register::function_closure() ||
                       reg == interpreter::Register::virtual_accumulator() ||
                       reg.ToParameterIndex() >= 0);
    // Conversions should be stored in known_node_aspects/NodeInfo.
    DCHECK(!value->properties().is_conversion());
    frame_[reg] = value;
  }
  ValueNode* get(interpreter::Register reg) const {
    DCHECK_IMPLIES(reg.is_parameter(),
                   reg == interpreter::Register::current_context() ||
                       reg == interpreter::Register::function_closure() ||
                       reg == interpreter::Register::virtual_accumulator() ||
                       reg.ToParameterIndex() >= 0);
    return frame_[reg];
  }

  const RegisterFrameArray<ValueNode*>& frame() const { return frame_; }

  KnownNodeAspects* known_node_aspects() { return known_node_aspects_; }
  const KnownNodeAspects* known_node_aspects() const {
    return known_node_aspects_;
  }

  void set_known_node_aspects(KnownNodeAspects* known_node_aspects) {
    DCHECK_NOT_NULL(known_node_aspects);
    known_node_aspects_ = known_node_aspects;
  }

  void clear_known_node_aspects() { known_node_aspects_ = nullptr; }

  void add_object(VirtualObject* vobject) { virtual_objects_.Add(vobject); }
  const VirtualObject::List& virtual_objects() const {
    return virtual_objects_;
  }
  void set_virtual_objects(const VirtualObject::List& virtual_objects) {
    virtual_objects_ = virtual_objects;
  }

 private:
  RegisterFrameArray<ValueNode*> frame_;
  KnownNodeAspects* known_node_aspects_;
  VirtualObject::List virtual_objects_;
};

class CompactInterpreterFrameState {
 public:
  CompactInterpreterFrameState(const MaglevCompilationUnit& info,
                               const compiler::BytecodeLivenessState* liveness)
      : live_registers_and_accumulator_(
            info.zone()->AllocateArray<ValueNode*>(SizeFor(info, liveness))),
        liveness_(liveness),
        virtual_objects_() {}

  CompactInterpreterFrameState(const MaglevCompilationUnit& info,
                               const compiler::BytecodeLivenessState* liveness,
                               const InterpreterFrameState& state)
      : CompactInterpreterFrameState(info, liveness) {
    virtual_objects_ = state.virtual_objects();
    ForEachValue(info, [&](ValueNode*& entry, interpreter::Register reg) {
      entry = state.get(reg);
    });
  }

  CompactInterpreterFrameState(const CompactInterpreterFrameState&) = delete;
  CompactInterpreterFrameState(CompactInterpreterFrameState&&) = delete;
  CompactInterpreterFrameState& operator=(const CompactInterpreterFrameState&) =
      delete;
  CompactInterpreterFrameState& operator=(CompactInterpreterFrameState&&) =
      delete;

  template <typename Function>
  void ForEachParameter(const MaglevCompilationUnit& info, Function&& f) const {
    for (int i = 0; i < info.parameter_count(); i++) {
      interpreter::Register reg = interpreter::Register::FromParameterIndex(i);
      f(live_registers_and_accumulator_[i], reg);
    }
  }

  template <typename Function>
  void ForEachParameter(const MaglevCompilationUnit& info, Function&& f) {
    for (int i = 0; i < info.parameter_count(); i++) {
      interpreter::Register reg = interpreter::Register::FromParameterIndex(i);
      f(live_registers_and_accumulator_[i], reg);
    }
  }

  template <typename Function>
  void ForEachLocal(const MaglevCompilationUnit& info, Function&& f) const {
    int live_reg = 0;
    for (int register_index : *liveness_) {
      interpreter::Register reg = interpreter::Register(register_index);
      f(live_registers_and_accumulator_[info.parameter_count() +
                                        context_register_count_ + live_reg++],
        reg);
    }
  }

  template <typename Function>
  void ForEachLocal(const MaglevCompilationUnit& info, Function&& f) {
    int live_reg = 0;
    for (int register_index : *liveness_) {
      interpreter::Register reg = interpreter::Register(register_index);
      f(live_registers_and_accumulator_[info.parameter_count() +
                                        context_register_count_ + live_reg++],
        reg);
    }
  }

  template <typename Function>
  void ForEachRegister(const MaglevCompilationUnit& info, Function&& f) {
    ForEachParameter(info, f);
    f(context(info), interpreter::Register::current_context());
    ForEachLocal(info, f);
  }

  template <typename Function>
  void ForEachRegister(const MaglevCompilationUnit& info, Function&& f) const {
    ForEachParameter(info, f);
    f(context(info), interpreter::Register::current_context());
    ForEachLocal(info, f);
  }

  template <typename Function>
  void ForEachValue(const MaglevCompilationUnit& info, Function&& f) {
    ForEachRegister(info, f);
    if (liveness_->AccumulatorIsLive()) {
      f(accumulator(info), interpreter::Register::virtual_accumulator());
    }
  }

  template <typename Function>
  void ForEachValue(const MaglevCompilationUnit& info, Function&& f) const {
    ForEachRegister(info, f);
    if (liveness_->AccumulatorIsLive()) {
      f(accumulator(info), interpreter::Register::virtual_accumulator());
    }
  }

  const compiler::BytecodeLivenessState* liveness() const { return liveness_; }

  ValueNode*& accumulator(const MaglevCompilationUnit& info) {
    DCHECK(liveness_->AccumulatorIsLive());
    return live_registers_and_accumulator_[size(info) - 1];
  }
  ValueNode*& accumulator(const MaglevCompilationUnit& info) const {
    DCHECK(liveness_->AccumulatorIsLive());
    return live_registers_and_accumulator_[size(info) - 1];
  }

  ValueNode*& context(const MaglevCompilationUnit& info) {
    return live_registers_and_accumulator_[info.parameter_count()];
  }
  ValueNode*& context(const MaglevCompilationUnit& info) const {
    return live_registers_and_accumulator_[info.parameter_count()];
  }

  ValueNode* GetValueOf(interpreter::Register reg,
                        const MaglevCompilationUnit& info) const {
    DCHECK(reg.is_valid());
    if (reg == interpreter::Register::current_context()) {
      return context(info);
    }
    if (reg == interpreter::Register::virtual_accumulator()) {
      return accumulator(info);
    }
    if (reg.is_parameter()) {
      DCHECK_LT(reg.ToParameterIndex(), info.parameter_count());
      return live_registers_and_accumulator_[reg.ToParameterIndex()];
    }
    int live_reg = 0;
    // TODO(victorgomes): See if we can do better than a linear search here.
    for (int register_index : *liveness_) {
      if (reg == interpreter::Register(register_index)) {
        return live_registers_and_accumulator_[info.parameter_count() +
                                               context_register_count_ +
                                               live_reg];
      }
      live_reg++;
    }
    // No value in this frame state.
    return nullptr;
  }

  size_t size(const MaglevCompilationUnit& info) const {
    return SizeFor(info, liveness_);
  }

  const VirtualObject::List& virtual_objects() const {
    return virtual_objects_;
  }
  VirtualObject::List& virtual_objects() { return virtual_objects_; }
  void set_virtual_objects(const VirtualObject::List& vos) {
    virtual_objects_ = vos;
  }

 private:
  static size_t SizeFor(const MaglevCompilationUnit& info,
                        const compiler::BytecodeLivenessState* liveness) {
    return info.parameter_count() + context_register_count_ +
           liveness->live_value_count();
  }

  // TODO(leszeks): Only include the context register if there are any
  // Push/PopContext calls.
  static const int context_register_count_ = 1;
  ValueNode** const live_registers_and_accumulator_;
  const compiler::BytecodeLivenessState* const liveness_;
  VirtualObject::List virtual_objects_;
};

class MergePointRegisterState {
#ifdef V8_ENABLE_MAGLEV

 public:
  bool is_initialized() const { return values_[0].GetPayload().is_initialized; }

  template <typename Function>
  void ForEachGeneralRegister(Function&& f) {
    RegisterState* current_value = &values_[0];
    for (Register reg : MaglevAssembler::GetAllocatableRegisters()) {
      f(reg, *current_value);
      ++current_value;
    }
  }

  template <typename Function>
  void ForEachDoubleRegister(Function&& f) {
    RegisterState* current_value = &double_values_[0];
    for (DoubleRegister reg :
         MaglevAssembler::GetAllocatableDoubleRegisters()) {
      f(reg, *current_value);
      ++current_value;
    }
  }

 private:
  RegisterState values_[kAllocatableGeneralRegisterCount] = {{}};
  RegisterState double_values_[kAllocatableDoubleRegisterCount] = {{}};
#endif  // V8_ENABLE_MAGLEV
};

class MergePointInterpreterFrameState {
 public:
  enum class BasicBlockType {
    kDefault,
    kLoopHeader,
    kExceptionHandlerStart,
    kUnusedExceptionHandlerStart,
  };

  static MergePointInterpreterFrameState* New(
      const MaglevCompilationUnit& info, const InterpreterFrameState& state,
      int merge_offset, int predecessor_count, BasicBlock* predecessor,
      const compiler::BytecodeLivenessState* liveness);

  static MergePointInterpreterFrameState* NewForLoop(
      const InterpreterFrameState& start_state,
      const MaglevCompilationUnit& info, int merge_offset,
      int predecessor_count, const compiler::BytecodeLivenessState* liveness,
      const compiler::LoopInfo* loop_info, bool has_been_peeled = false);

  static MergePointInterpreterFrameState* NewForCatchBlock(
      const MaglevCompilationUnit& unit,
      const compiler::BytecodeLivenessState* liveness, int handler_offset,
      bool was_used, interpreter::Register context_register, Graph* graph);

  // Merges an unmerged framestate with a possibly merged framestate into |this|
  // framestate.
  void Merge(MaglevGraphBuilder* graph_builder, InterpreterFrameState& unmerged,
             BasicBlock* predecessor);
  void Merge(MaglevGraphBuilder* graph_builder,
             MaglevCompilationUnit& compilation_unit,
             InterpreterFrameState& unmerged, BasicBlock* predecessor);
  void InitializeLoop(MaglevGraphBuilder* graph_builder,
                      MaglevCompilationUnit& compilation_unit,
                      InterpreterFrameState& unmerged, BasicBlock* predecessor,
                      bool optimistic_initial_state = false,
                      LoopEffects* loop_effects = nullptr);
  void InitializeWithBasicBlock(BasicBlock* current_block);

  // Merges an unmerged framestate with a possibly merged framestate into |this|
  // framestate.
  void MergeLoop(MaglevGraphBuilder* graph_builder,
                 InterpreterFrameState& loop_end_state,
                 BasicBlock* loop_end_block);
  void MergeLoop(MaglevGraphBuilder* graph_builder,
                 MaglevCompilationUnit& compilation_unit,
                 InterpreterFrameState& loop_end_state,
                 BasicBlock* loop_end_block);
  void set_loop_effects(LoopEffects* loop_effects);
  const LoopEffects* loop_effects();
  // Merges a frame-state that might not be mergable, in which case we need to
  // re-compile the loop again. Calls FinishBlock only if the merge succeeded.
  bool TryMergeLoop(MaglevGraphBuilder* graph_builder,
                    InterpreterFrameState& loop_end_state,
                    const std::function<BasicBlock*()>& FinishBlock);

  // Merges an unmerged framestate into a possibly merged framestate at the
  // start of the target catchblock.
  void MergeThrow(MaglevGraphBuilder* handler_builder,
                  const MaglevCompilationUnit* handler_unit,
                  const KnownNodeAspects& known_node_aspects,
                  const VirtualObject::List virtual_objects);

  // Merges a dead framestate (e.g. one which has been early terminated with a
  // deopt).
  void MergeDead(const MaglevCompilationUnit& compilation_unit,
                 unsigned num = 1) {
    DCHECK_GE(predecessor_count_, num);
    DCHECK_LT(predecessors_so_far_, predecessor_count_);
    ReducePhiPredecessorCount(num);
    predecessor_count_ -= num;
    DCHECK_LE(predecessors_so_far_, predecessor_count_);
  }

  // Merges a dead loop framestate (e.g. one where the block containing the
  // JumpLoop has been early terminated with a deopt).
  void MergeDeadLoop(const MaglevCompilationUnit& compilation_unit) {
    // This should be the last predecessor we try to merge.
    DCHECK_EQ(predecessors_so_far_, predecessor_count_ - 1);
    DCHECK(is_unmerged_loop());
    MergeDead(compilation_unit);
    // This means that this is no longer a loop.
    bitfield_ =
        kBasicBlockTypeBits::update(bitfield_, BasicBlockType::kDefault);
  }

  // Returns and clears the known node aspects on this state. Expects to only
  // ever be called once, when starting a basic block with this state.
  KnownNodeAspects* TakeKnownNodeAspects() {
    DCHECK_NOT_NULL(known_node_aspects_);
    return std::exchange(known_node_aspects_, nullptr);
  }

  KnownNodeAspects* CloneKnownNodeAspects(Zone* zone) {
    return known_node_aspects_->Clone(zone);
  }

  const CompactInterpreterFrameState& frame_state() const {
    return frame_state_;
  }
  MergePointRegisterState& register_state() { return register_state_; }

  bool has_phi() const { return !phis_.is_empty(); }
  Phi::List* phis() { return &phis_; }

  int predecessor_count() const { return predecessor_count_; }

  int predecessors_so_far() const { return predecessors_so_far_; }

  BasicBlock* predecessor_at(int i) const {
    DCHECK_LE(predecessors_so_far_, predecessor_count_);
    DCHECK_LT(i, predecessors_so_far_);
    return predecessors_[i];
  }
  void set_predecessor_at(int i, BasicBlock* val) {
    DCHECK_LE(predecessors_so_far_, predecessor_count_);
    DCHECK_LT(i, predecessors_so_far_);
    predecessors_[i] = val;
  }

  void set_virtual_objects(const VirtualObject::List& vos) {
    frame_state_.set_virtual_objects(vos);
  }

  void PrintVirtualObjects(const MaglevCompilationUnit& info,
                           VirtualObject::List from_ifs,
                           const char* prelude = nullptr) {
    if (!v8_flags.trace_maglev_graph_building) return;
    if (prelude) {
      std::cout << prelude << std::endl;
    }
    from_ifs.Print(std::cout,
                   "* VOs (Interpreter Frame State): ", info.graph_labeller());
    frame_state_.virtual_objects().Print(
        std::cout, "* VOs (Merge Frame State): ", info.graph_labeller());
  }

  bool is_loop() const {
    return basic_block_type() == BasicBlockType::kLoopHeader;
  }

  bool exception_handler_was_used() const {
    DCHECK(is_exception_handler());
    return basic_block_type() == BasicBlockType::kExceptionHandlerStart;
  }

  bool is_exception_handler() const {
    return basic_block_type() == BasicBlockType::kExceptionHandlerStart ||
           basic_block_type() == BasicBlockType::kUnusedExceptionHandlerStart;
  }

  bool is_unmerged_loop() const {
    // If this is a loop and not all predecessors are set, then the loop isn't
    // merged yet.
    DCHECK_IMPLIES(is_loop(), predecessor_count_ > 0);
    return is_loop() && predecessors_so_far_ < predecessor_count_;
  }

  bool is_unreachable_loop() const {
    // If there is only one predecessor, and it's not set, then this is a loop
    // merge with no forward control flow entering it.
    return is_loop() && !is_resumable_loop() && predecessor_count_ == 1 &&
           predecessors_so_far_ == 0;
  }

  BasicBlockType basic_block_type() const {
    return kBasicBlockTypeBits::decode(bitfield_);
  }
  bool is_resumable_loop() const {
    return kIsResumableLoopBit::decode(bitfield_);
  }
  bool is_loop_with_peeled_iteration() const {
    return kIsLoopWithPeeledIterationBit::decode(bitfield_);
  }

  int merge_offset() const { return merge_offset_; }

  DeoptFrame* backedge_deopt_frame() const { return backedge_deopt_frame_; }

  const compiler::LoopInfo* loop_info() const {
    DCHECK(loop_metadata_.has_value());
    DCHECK_NOT_NULL(loop_metadata_->loop_info);
    return loop_metadata_->loop_info;
  }
  void ClearLoopInfo() { loop_metadata_->loop_info = nullptr; }
  bool HasLoopInfo() const {
    return loop_metadata_.has_value() && loop_metadata_->loop_info;
  }

  interpreter::Register catch_block_context_register() const {
    DCHECK(is_exception_handler());
    return catch_block_context_register_;
  }

 private:
  using kBasicBlockTypeBits = base::BitField<BasicBlockType, 0, 2>;
  using kIsResumableLoopBit = kBasicBlockTypeBits::Next<bool, 1>;
  using kIsLoopWithPeeledIterationBit = kIsResumableLoopBit::Next<bool, 1>;

  // For each non-Phi value in the frame state, store its alternative
  // representations to avoid re-converting on Phi creation.
  class Alternatives {
   public:
    using List = base::ThreadedList<Alternatives>;

    explicit Alternatives(const NodeInfo* node_info)
        : node_type_(node_info ? node_info->type() : NodeType::kUnknown),
          tagged_alternative_(node_info ? node_info->alternative().tagged()
                                        : nullptr) {}

    NodeType node_type() const { return node_type_; }
    ValueNode* tagged_alternative() const { return tagged_alternative_; }

   private:
    Alternatives** next() { return &next_; }

    // For now, Phis are tagged, so only store the tagged alternative.
    NodeType node_type_;
    ValueNode* tagged_alternative_;
    Alternatives* next_ = nullptr;
    friend base::ThreadedListTraits<Alternatives>;
  };
  NodeType AlternativeType(const Alternatives* alt);

  template <typename T, typename... Args>
  friend T* Zone::New(Args&&... args);

  MergePointInterpreterFrameState(
      const MaglevCompilationUnit& info, int merge_offset,
      int predecessor_count, int predecessors_so_far, BasicBlock** predecessors,
      BasicBlockType type, const compiler::BytecodeLivenessState* liveness);

  void MergePhis(MaglevGraphBuilder* builder,
                 MaglevCompilationUnit& compilation_unit,
                 InterpreterFrameState& unmerged, BasicBlock* predecessor,
                 bool optimistic_loop_phis);
  void MergeVirtualObjects(MaglevGraphBuilder* builder,
                           MaglevCompilationUnit& compilation_unit,
                           InterpreterFrameState& unmerged,
                           BasicBlock* predecessor);

  ValueNode* MergeValue(const MaglevGraphBuilder* graph_builder,
                        interpreter::Register owner,
                        const KnownNodeAspects& unmerged_aspects,
                        ValueNode* merged, ValueNode* unmerged,
                        Alternatives::List* per_predecessor_alternatives,
                        bool optimistic_loop_phis = false);

  void ReducePhiPredecessorCount(unsigned num);

  void MergeVirtualObjects(MaglevGraphBuilder* builder,
                           MaglevCompilationUnit& compilation_unit,
                           const VirtualObject::List unmerged_vos,
                           const KnownNodeAspects& unmerged_aspects);

  void MergeVirtualObject(MaglevGraphBuilder* builder,
                          const VirtualObject::List unmerged_vos,
                          const KnownNodeAspects& unmerged_aspects,
                          VirtualObject* merged, VirtualObject* unmerged);

  std::optional<ValueNode*> MergeVirtualObjectValue(
      const MaglevGraphBuilder* graph_builder,
      const KnownNodeAspects& unmerged_aspects, ValueNode* merged,
      ValueNode* unmerged);

  void MergeLoopValue(MaglevGraphBuilder* graph_builder,
                      interpreter::Register owner,
                      const KnownNodeAspects& unmerged_aspects,
                      ValueNode* merged, ValueNode* unmerged);

  ValueNode* NewLoopPhi(Zone* zone, interpreter::Register reg);

  ValueNode* NewExceptionPhi(Zone* zone, interpreter::Register reg) {
    DCHECK_EQ(predecessor_count_, 0);
    DCHECK_NULL(predecessors_);
    Phi* result = Node::New<Phi>(zone, 0, this, reg);
    phis_.Add(result);
    return result;
  }

  int merge_offset_;

  int predecessor_count_;
  int predecessors_so_far_;

  uint32_t bitfield_;

  BasicBlock** predecessors_;

  Phi::List phis_;

  CompactInterpreterFrameState frame_state_;
  MergePointRegisterState register_state_;
  KnownNodeAspects* known_node_aspects_ = nullptr;

  union {
    // {pre_predecessor_alternatives_} is used to keep track of the alternatives
    // of Phi inputs. Once the block has been merged, it's not used anymore.
    Alternatives::List* per_predecessor_alternatives_;
    // {backedge_deopt_frame_} is used to record the deopt frame for the
    // backedge, in case we want to insert a deopting conversion during phi
    // untagging. It is set when visiting the JumpLoop (and will only be set for
    // loop headers), when the header has already been merged and
    // {per_predecessor_alternatives_} is thus not used anymore.
    DeoptFrame* backedge_deopt_frame_;
    // For catch blocks, store the interpreter register holding the context.
    // This will be the same value for all incoming merges.
    interpreter::Register catch_block_context_register_;
  };

  struct LoopMetadata {
    const compiler::LoopInfo* loop_info;
    const LoopEffects* loop_effects;
  };
  std::optional<LoopMetadata> loop_metadata_ = std::nullopt;
};

struct LoopEffects {
  explicit LoopEffects(int loop_header, Zone* zone)
      :
#ifdef DEBUG
        loop_header(loop_header),
#endif
        context_slot_written(zone),
        objects_written(zone),
        keys_cleared(zone),
        allocations(zone) {
  }
#ifdef DEBUG
  int loop_header;
#endif
  ZoneSet<KnownNodeAspects::LoadedContextSlotsKey> context_slot_written;
  ZoneSet<ValueNode*> objects_written;
  ZoneSet<KnownNodeAspects::LoadedPropertyMapKey> keys_cleared;
  ZoneSet<InlinedAllocation*> allocations;
  bool unstable_aspects_cleared = false;
  bool may_have_aliasing_contexts = false;
  void Clear() {
    context_slot_written.clear();
    objects_written.clear();
    keys_cleared.clear();
    allocations.clear();
    unstable_aspects_cleared = false;
  }
  void Merge(const LoopEffects* other) {
    if (!unstable_aspects_cleared) {
      unstable_aspects_cleared = other->unstable_aspects_cleared;
    }
    if (!may_have_aliasing_contexts) {
      may_have_aliasing_contexts = other->may_have_aliasing_contexts;
    }
    context_slot_written.insert(other->context_slot_written.begin(),
                                other->context_slot_written.end());
    objects_written.insert(other->objects_written.begin(),
                           other->objects_written.end());
    keys_cleared.insert(other->keys_cleared.begin(), other->keys_cleared.end());
    allocations.insert(other->allocations.begin(), other->allocations.end());
  }
};

void InterpreterFrameState::CopyFrom(const MaglevCompilationUnit& info,
                                     MergePointInterpreterFrameState& state,
                                     bool preserve_known_node_aspects = false,
                                     Zone* zone = nullptr) {
  DCHECK_IMPLIES(preserve_known_node_aspects, zone);
  if (v8_flags.trace_maglev_graph_building) {
    std::cout << "- Copying frame state from merge @" << &state << std::endl;
    state.PrintVirtualObjects(info, virtual_objects());
  }
  state.frame_state().ForEachValue(
      info, [&](ValueNode* value, interpreter::Register reg) {
          frame_[reg] = value;
      });
  if (preserve_known_node_aspects) {
    known_node_aspects_ = state.CloneKnownNodeAspects(zone);
  } else {
    // Move "what we know" across without copying -- we can safely mutate it
    // now, as we won't be entering this merge point again.
    known_node_aspects_ = state.TakeKnownNodeAspects();
  }
  virtual_objects_ = state.frame_state().virtual_objects();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_INTERPRETER_FRAME_STATE_H_
