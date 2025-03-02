// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LOAD_ELIMINATION_H_
#define V8_COMPILER_LOAD_ELIMINATION_H_

#include "src/base/compiler-specific.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/simplified-operator.h"
#include "src/handles/maybe-handles.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Factory;

namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
struct FieldAccess;
class Graph;
class JSGraph;

class V8_EXPORT_PRIVATE LoadElimination final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  LoadElimination(Editor* editor, JSHeapBroker* broker, JSGraph* jsgraph,
                  Zone* zone)
      : AdvancedReducer(editor),
        broker_(broker),
        node_states_(zone),
        jsgraph_(jsgraph) {}
  ~LoadElimination() final = default;
  LoadElimination(const LoadElimination&) = delete;
  LoadElimination& operator=(const LoadElimination&) = delete;

  const char* reducer_name() const override { return "LoadElimination"; }

  Reduction Reduce(Node* node) final;

 private:
  static const size_t kMaxTrackedElements = 8;

  // Abstract state to approximate the current state of an element along the
  // effect paths through the graph.
  class AbstractElements final : public ZoneObject {
   public:
    explicit AbstractElements(Zone* zone) {
      for (size_t i = 0; i < arraysize(elements_); ++i) {
        elements_[i] = Element();
      }
    }
    AbstractElements(Node* object, Node* index, Node* value,
                     MachineRepresentation representation, Zone* zone)
        : AbstractElements(zone) {
      elements_[next_index_++] = Element(object, index, value, representation);
    }

    AbstractElements const* Extend(Node* object, Node* index, Node* value,
                                   MachineRepresentation representation,
                                   Zone* zone) const {
      AbstractElements* that = zone->New<AbstractElements>(*this);
      that->elements_[that->next_index_] =
          Element(object, index, value, representation);
      that->next_index_ = (that->next_index_ + 1) % arraysize(elements_);
      return that;
    }
    Node* Lookup(Node* object, Node* index,
                 MachineRepresentation representation) const;
    AbstractElements const* Kill(Node* object, Node* index, Zone* zone) const;
    bool Equals(AbstractElements const* that) const;
    AbstractElements const* Merge(AbstractElements const* that,
                                  Zone* zone) const;

    void Print() const;

   private:
    struct Element {
      Element() = default;
      Element(Node* object, Node* index, Node* value,
              MachineRepresentation representation)
          : object(object),
            index(index),
            value(value),
            representation(representation) {}

      Node* object = nullptr;
      Node* index = nullptr;
      Node* value = nullptr;
      MachineRepresentation representation = MachineRepresentation::kNone;
    };

    Element elements_[kMaxTrackedElements];
    size_t next_index_ = 0;
  };

  // Information we use to resolve object aliasing. Currently, we consider
  // object not aliased if they have different maps or if the nodes may
  // not alias.
  class AliasStateInfo;

  struct FieldInfo {
    FieldInfo() = default;
    FieldInfo(Node* value, MachineRepresentation representation,
              MaybeHandle<Name> name = {},
              ConstFieldInfo const_field_info = ConstFieldInfo::None())
        : value(value),
          representation(representation),
          name(name),
          const_field_info(const_field_info) {}

    bool operator==(const FieldInfo& other) const {
      return value == other.value && representation == other.representation &&
             name.address() == other.name.address() &&
             const_field_info == other.const_field_info;
    }
    bool operator!=(const FieldInfo& other) const { return !(*this == other); }

    Node* value = nullptr;
    MachineRepresentation representation = MachineRepresentation::kNone;
    MaybeHandle<Name> name;
    ConstFieldInfo const_field_info;
  };

  // Abstract state to approximate the current state of a certain field along
  // the effect paths through the graph.
  class AbstractField final : public ZoneObject {
   public:
    explicit AbstractField(Zone* zone) : info_for_node_(zone) {}
    AbstractField(Node* object, FieldInfo info, Zone* zone)
        : info_for_node_(zone) {
      info_for_node_.insert(std::make_pair(object, info));
    }

    AbstractField const* Extend(Node* object, FieldInfo info, Zone* zone,
                                int current_field_count) const {
      AbstractField* that = zone->New<AbstractField>(*this);
      if ((current_field_count >= kMaxTrackedFields &&
           that->info_for_node_.size() > 0) ||
          that->info_for_node_.size() >= kMaxTrackedObjects) {
        // We are tracking too many objects, which leads to bad performance.
        // Delete one to avoid the map from becoming bigger.
        that->info_for_node_.erase(that->info_for_node_.begin());
      }
      that->info_for_node_[object] = info;
      return that;
    }
    FieldInfo const* Lookup(Node* object) const;
    AbstractField const* KillConst(Node* object, Zone* zone) const;
    AbstractField const* Kill(const AliasStateInfo& alias_info,
                              MaybeHandle<Name> name, Zone* zone) const;
    bool Equals(AbstractField const* that) const {
      return this == that || this->info_for_node_ == that->info_for_node_;
    }
    AbstractField const* Merge(AbstractField const* that, Zone* zone,
                               int* count) const {
      if (this->Equals(that)) return this;
      AbstractField* copy = zone->New<AbstractField>(zone);
      for (auto this_it : this->info_for_node_) {
        Node* this_object = this_it.first;
        FieldInfo this_second = this_it.second;
        if (this_object->IsDead()) continue;
        auto that_it = that->info_for_node_.find(this_object);
        if (that_it != that->info_for_node_.end() &&
            that_it->second == this_second) {
          copy->info_for_node_.insert(this_it);
          (*count)++;
        }
      }
      return copy;
    }

    void Print() const;

    int count() const { return static_cast<int>(info_for_node_.size()); }

   private:
    ZoneMap<Node*, FieldInfo> info_for_node_;
  };

  static size_t const kMaxTrackedFieldsPerObject = 32;
  static size_t const kMaxTrackedObjects = 100;
  static int const kMaxTrackedFields = 300;

  // Abstract state to approximate the current map of an object along the
  // effect paths through the graph.
  class AbstractMaps final : public ZoneObject {
   public:
    explicit AbstractMaps(Zone* zone);
    AbstractMaps(Node* object, ZoneRefSet<Map> maps, Zone* zone);

    AbstractMaps const* Extend(Node* object, ZoneRefSet<Map> maps,
                               Zone* zone) const;
    bool Lookup(Node* object, ZoneRefSet<Map>* object_maps) const;
    AbstractMaps const* Kill(const AliasStateInfo& alias_info,
                             Zone* zone) const;
    bool Equals(AbstractMaps const* that) const {
      return this == that || this->info_for_node_ == that->info_for_node_;
    }
    AbstractMaps const* Merge(AbstractMaps const* that, Zone* zone) const;

    void Print() const;

   private:
    ZoneMap<Node*, ZoneRefSet<Map>> info_for_node_;
  };

  class IndexRange {
   public:
    IndexRange(int begin, int size) : begin_(begin), end_(begin + size) {
      DCHECK_LE(0, begin);
      DCHECK_LE(1, size);
      if (end_ > static_cast<int>(kMaxTrackedFieldsPerObject)) {
        *this = IndexRange::Invalid();
      }
    }
    static IndexRange Invalid() { return IndexRange(); }

    bool operator==(const IndexRange& other) const {
      return begin_ == other.begin_ && end_ == other.end_;
    }
    bool operator!=(const IndexRange& other) const { return !(*this == other); }

    struct Iterator {
      int i;
      int operator*() { return i; }
      void operator++() { ++i; }
      bool operator!=(Iterator other) { return i != other.i; }
    };

    Iterator begin() { return {begin_}; }
    Iterator end() { return {end_}; }

   private:
    int begin_;
    int end_;

    IndexRange() : begin_(-1), end_(-1) {}
  };

  class AbstractState final : public ZoneObject {
   public:
    bool Equals(AbstractState const* that) const;
    void Merge(AbstractState const* that, Zone* zone);

    AbstractState const* SetMaps(Node* object, ZoneRefSet<Map> maps,
                                 Zone* zone) const;
    AbstractState const* KillMaps(Node* object, Zone* zone) const;
    AbstractState const* KillMaps(const AliasStateInfo& alias_info,
                                  Zone* zone) const;
    bool LookupMaps(Node* object, ZoneRefSet<Map>* object_maps) const;

    AbstractState const* AddField(Node* object, IndexRange index,
                                  FieldInfo info, Zone* zone) const;
    AbstractState const* KillConstField(Node* object, IndexRange index_range,
                                        Zone* zone) const;
    AbstractState const* KillField(const AliasStateInfo& alias_info,
                                   IndexRange index, MaybeHandle<Name> name,
                                   Zone* zone) const;
    AbstractState const* KillField(Node* object, IndexRange index,
                                   MaybeHandle<Name> name, Zone* zone) const;
    AbstractState const* KillFields(Node* object, MaybeHandle<Name> name,
                                    Zone* zone) const;
    AbstractState const* KillAll(Zone* zone) const;
    FieldInfo const* LookupField(Node* object, IndexRange index,
                                 ConstFieldInfo const_field_info) const;

    AbstractState const* AddElement(Node* object, Node* index, Node* value,
                                    MachineRepresentation representation,
                                    Zone* zone) const;
    AbstractState const* KillElement(Node* object, Node* index,
                                     Zone* zone) const;
    Node* LookupElement(Node* object, Node* index,
                        MachineRepresentation representation) const;

    void Print() const;

    static AbstractState const* empty_state() { return &empty_state_; }

   private:
    static AbstractState const empty_state_;

    using AbstractFields =
        std::array<AbstractField const*, kMaxTrackedFieldsPerObject>;

    bool FieldsEquals(AbstractFields const& this_fields,
                      AbstractFields const& that_fields) const;
    void FieldsMerge(AbstractFields* this_fields,
                     AbstractFields const& that_fields, Zone* zone);

    AbstractElements const* elements_ = nullptr;
    AbstractFields fields_{};
    AbstractFields const_fields_{};
    AbstractMaps const* maps_ = nullptr;
    int const_fields_count_ = 0;
    // Note that fields_count_ includes both const_fields and non-const fields.
    // To get the number of non-const fields, use `fields_count_ -
    // const_fields_count_`.
    int fields_count_ = 0;
  };

  class AbstractStateForEffectNodes final : public ZoneObject {
   public:
    explicit AbstractStateForEffectNodes(Zone* zone) : info_for_node_(zone) {}
    AbstractState const* Get(Node* node) const;
    void Set(Node* node, AbstractState const* state);

    Zone* zone() const { return info_for_node_.zone(); }

   private:
    ZoneVector<AbstractState const*> info_for_node_;
  };

  Reduction ReduceCheckMaps(Node* node);
  Reduction ReduceCompareMaps(Node* node);
  Reduction ReduceMapGuard(Node* node);
  Reduction ReduceEnsureWritableFastElements(Node* node);
  Reduction ReduceMaybeGrowFastElements(Node* node);
  Reduction ReduceTransitionElementsKind(Node* node);
  Reduction ReduceTransitionElementsKindOrCheckMap(Node* node);
  Reduction ReduceLoadField(Node* node, FieldAccess const& access);
  Reduction ReduceStoreField(Node* node, FieldAccess const& access);
  Reduction ReduceLoadElement(Node* node);
  Reduction ReduceStoreElement(Node* node);
  Reduction ReduceTransitionAndStoreElement(Node* node);
  Reduction ReduceStoreTypedElement(Node* node);
  Reduction ReduceEffectPhi(Node* node);
  Reduction ReduceStart(Node* node);
  Reduction ReduceOtherNode(Node* node);

  Reduction UpdateState(Node* node, AbstractState const* state);

  AbstractState const* ComputeLoopState(Node* node,
                                        AbstractState const* state) const;
  AbstractState const* ComputeLoopStateForStoreField(
      Node* current, LoadElimination::AbstractState const* state,
      FieldAccess const& access) const;
  AbstractState const* UpdateStateForPhi(AbstractState const* state,
                                         Node* effect_phi, Node* phi);

  static IndexRange FieldIndexOf(int offset, int representation_size);
  static IndexRange FieldIndexOf(FieldAccess const& access);

  static AbstractState const* empty_state() {
    return AbstractState::empty_state();
  }

  CommonOperatorBuilder* common() const;
  Isolate* isolate() const;
  Factory* factory() const;
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  JSHeapBroker* broker() const { return broker_; }
  Zone* zone() const { return node_states_.zone(); }

  JSHeapBroker* broker_;
  AbstractStateForEffectNodes node_states_;
  JSGraph* const jsgraph_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LOAD_ELIMINATION_H_
