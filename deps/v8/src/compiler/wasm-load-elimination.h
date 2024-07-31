// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_WASM_LOAD_ELIMINATION_H_
#define V8_COMPILER_WASM_LOAD_ELIMINATION_H_

#include "src/base/compiler-specific.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/node-aux-data.h"
#include "src/compiler/persistent-map.h"

namespace v8::internal::compiler {

// Forward declarations.
class CommonOperatorBuilder;
class Graph;
class JSGraph;
class MachineOperatorBuilder;
struct ObjectAccess;

class V8_EXPORT_PRIVATE WasmLoadElimination final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  WasmLoadElimination(Editor* editor, JSGraph* jsgraph, Zone* zone);
  ~WasmLoadElimination() final = default;
  WasmLoadElimination(const WasmLoadElimination&) = delete;
  WasmLoadElimination& operator=(const WasmLoadElimination&) = delete;

  const char* reducer_name() const override { return "WasmLoadElimination"; }

  Reduction Reduce(Node* node) final;

 private:
  struct FieldOrElementValue {
    FieldOrElementValue() = default;
    explicit FieldOrElementValue(Node* value) : value(value) {}

    bool operator==(const FieldOrElementValue& other) const {
      return value == other.value;
    }

    bool operator!=(const FieldOrElementValue& other) const {
      return !(*this == other);
    }

    bool IsEmpty() const { return value == nullptr; }

    Node* value = nullptr;
  };

  class HalfState final : public ZoneObject {
   public:
    explicit HalfState(Zone* zone)
        : zone_(zone),
          fields_(zone, InnerMap(zone)),
          elements_(zone, InnerMap(zone)) {}

    bool Equals(HalfState const* that) const {
      return fields_ == that->fields_ && elements_ == that->elements_;
    }
    bool IsEmpty() const {
      return fields_.begin() == fields_.end() &&
             elements_.begin() == elements_.end();
    }
    void IntersectWith(HalfState const* that);
    HalfState const* KillField(int field_index, Node* object) const;
    HalfState const* AddField(int field_index, Node* object, Node* value) const;
    FieldOrElementValue LookupField(int field_index, Node* object) const;
    void Print() const;

   private:
    using InnerMap = PersistentMap<Node*, FieldOrElementValue>;
    template <typename OuterKey>
    using OuterMap = PersistentMap<OuterKey, InnerMap>;
    // offset -> object -> info
    using FieldInfos = OuterMap<int>;
    // object -> offset -> info
    using ElementInfos = OuterMap<Node*>;

    // Update {map} so that {map.Get(outer_key).Get(inner_key)} returns {info}.
    template <typename OuterKey>
    static void Update(OuterMap<OuterKey>& map, OuterKey outer_key,
                       Node* inner_key, FieldOrElementValue info) {
      InnerMap map_copy(map.Get(outer_key));
      map_copy.Set(inner_key, info);
      map.Set(outer_key, map_copy);
    }

    static void Print(const FieldInfos& infos);
    static void Print(const ElementInfos& infos);

    Zone* zone_;
    FieldInfos fields_;
    ElementInfos elements_;
  };

  // An {AbstractState} consists of two {HalfState}s, representing the sets of
  // known mutable and immutable struct fields, respectively. The two
  // half-states should not overlap.
  struct AbstractState : public ZoneObject {
    explicit AbstractState(Zone* zone)
        : mutable_state(zone), immutable_state(zone) {}
    explicit AbstractState(HalfState mutable_state, HalfState immutable_state)
        : mutable_state(mutable_state), immutable_state(immutable_state) {}

    bool Equals(AbstractState const* that) const {
      return this->immutable_state.Equals(&that->immutable_state) &&
             this->mutable_state.Equals(&that->mutable_state);
    }
    void IntersectWith(AbstractState const* that) {
      mutable_state.IntersectWith(&that->mutable_state);
      immutable_state.IntersectWith(&that->immutable_state);
    }

    HalfState mutable_state;
    HalfState immutable_state;
  };

  Reduction ReduceWasmStructGet(Node* node);
  Reduction ReduceWasmStructSet(Node* node);
  Reduction ReduceWasmArrayLength(Node* node);
  Reduction ReduceWasmArrayInitializeLength(Node* node);
  Reduction ReduceStringPrepareForGetCodeunit(Node* node);
  Reduction ReduceStringAsWtf16(Node* node);
  Reduction ReduceAnyConvertExtern(Node* node);
  Reduction ReduceEffectPhi(Node* node);
  Reduction ReduceStart(Node* node);
  Reduction ReduceOtherNode(Node* node);

  // Reduce an operation that could be treated as a load from an immutable
  // object.
  Reduction ReduceLoadLikeFromImmutable(Node* node, int index);

  Reduction UpdateState(Node* node, AbstractState const* state);

  AbstractState const* ComputeLoopState(Node* node,
                                        AbstractState const* state) const;
  // Returns the replacement value and effect for a load given an initial value
  // node, after optional {TypeGuard}ing and i8/i16 adaptation to i32.
  std::tuple<Node*, Node*> TruncateAndExtendOrType(Node* value, Node* effect,
                                                   Node* control,
                                                   wasm::ValueType field_type,
                                                   bool is_signed);
  Reduction AssertUnreachable(Node* node);

  CommonOperatorBuilder* common() const;
  MachineOperatorBuilder* machine() const;
  Isolate* isolate() const;
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Node* dead() const { return dead_; }
  Zone* zone() const { return zone_; }
  AbstractState const* empty_state() const { return &empty_state_; }

  AbstractState const empty_state_;
  NodeAuxData<AbstractState const*> node_states_;
  JSGraph* const jsgraph_;
  Node* dead_;
  Zone* zone_;
};

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_WASM_LOAD_ELIMINATION_H_
