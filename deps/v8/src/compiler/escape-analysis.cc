// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/escape-analysis.h"

#include "src/codegen/tick-counter.h"
#include "src/compiler/frame-states.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/state-values-utils.h"
#include "src/handles/handles-inl.h"
#include "src/objects/map-inl.h"

#ifdef DEBUG
#define TRACE(...)                                        \
  do {                                                    \
    if (v8_flags.trace_turbo_escape) PrintF(__VA_ARGS__); \
  } while (false)
#else
#define TRACE(...)
#endif

namespace v8 {
namespace internal {
namespace compiler {

template <class T>
class Sidetable {
 public:
  explicit Sidetable(Zone* zone) : map_(zone) {}
  T& operator[](const Node* node) {
    NodeId id = node->id();
    if (id >= map_.size()) {
      map_.resize(id + 1);
    }
    return map_[id];
  }

 private:
  ZoneVector<T> map_;
};

template <class T>
class SparseSidetable {
 public:
  explicit SparseSidetable(Zone* zone, T def_value = T())
      : def_value_(std::move(def_value)), map_(zone) {}
  void Set(const Node* node, T value) {
    auto iter = map_.find(node->id());
    if (iter != map_.end()) {
      iter->second = std::move(value);
    } else if (value != def_value_) {
      map_.insert(iter, std::make_pair(node->id(), std::move(value)));
    }
  }
  const T& Get(const Node* node) const {
    auto iter = map_.find(node->id());
    return iter != map_.end() ? iter->second : def_value_;
  }

 private:
  T def_value_;
  ZoneUnorderedMap<NodeId, T> map_;
};

// Keeps track of the changes to the current node during reduction.
// Encapsulates the current state of the IR graph and the reducer state like
// side-tables. All access to the IR and the reducer state should happen through
// a ReduceScope to ensure that changes and dependencies are tracked and all
// necessary node revisitations happen.
class ReduceScope {
 public:
  using Reduction = EffectGraphReducer::Reduction;
  explicit ReduceScope(Node* node, Reduction* reduction)
      : current_node_(node), reduction_(reduction) {}

  void SetValueChanged() { reduction()->set_value_changed(); }

 protected:
  Node* current_node() const { return current_node_; }
  Reduction* reduction() { return reduction_; }

 private:
  Node* current_node_;
  Reduction* reduction_;
};

// A VariableTracker object keeps track of the values of variables at all points
// of the effect chain and introduces new phi nodes when necessary.
// Initially and by default, variables are mapped to nullptr, which means that
// the variable allocation point does not dominate the current point on the
// effect chain. We map variables that represent uninitialized memory to the
// Dead node to ensure it is not read.
// Unmapped values are impossible by construction, it is indistinguishable if a
// PersistentMap does not contain an element or maps it to the default element.
class VariableTracker {
 private:
  // The state of all variables at one point in the effect chain.
  class State {
   public:
    using Map = PersistentMap<Variable, Node*>;

    explicit State(Zone* zone) : map_(zone) {}
    Node* Get(Variable var) const {
      CHECK(var != Variable::Invalid());
      return map_.Get(var);
    }
    void Set(Variable var, Node* node) {
      CHECK(var != Variable::Invalid());
      return map_.Set(var, node);
    }
    Map::iterator begin() const { return map_.begin(); }
    Map::iterator end() const { return map_.end(); }
    bool operator!=(const State& other) const { return map_ != other.map_; }

   private:
    Map map_;
  };

 public:
  VariableTracker(JSGraph* graph, EffectGraphReducer* reducer, Zone* zone);
  VariableTracker(const VariableTracker&) = delete;
  VariableTracker& operator=(const VariableTracker&) = delete;

  Variable NewVariable() { return Variable(next_variable_++); }
  Node* Get(Variable var, Node* effect) { return table_.Get(effect).Get(var); }
  Zone* zone() { return zone_; }

  class V8_NODISCARD Scope : public ReduceScope {
   public:
    Scope(VariableTracker* tracker, Node* node, Reduction* reduction);
    ~Scope();
    Maybe<Node*> Get(Variable var) {
      Node* node = current_state_.Get(var);
      if (node && node->opcode() == IrOpcode::kDead) {
        // TODO(turbofan): We use {Dead} as a sentinel for uninitialized memory.
        // Reading uninitialized memory can only happen in unreachable code. In
        // this case, we have to mark the object as escaping to avoid dead nodes
        // in the graph. This is a workaround that should be removed once we can
        // handle dead nodes everywhere.
        return Nothing<Node*>();
      }
      return Just(node);
    }
    void Set(Variable var, Node* node) { current_state_.Set(var, node); }

   private:
    VariableTracker* states_;
    State current_state_;
  };

 private:
  State MergeInputs(Node* effect_phi);
  Zone* zone_;
  JSGraph* graph_;
  SparseSidetable<State> table_;
  ZoneVector<Node*> buffer_;
  EffectGraphReducer* reducer_;
  int next_variable_ = 0;
  TickCounter* const tick_counter_;
};

// Encapsulates the current state of the escape analysis reducer to preserve
// invariants regarding changes and re-visitation.
class EscapeAnalysisTracker : public ZoneObject {
 public:
  EscapeAnalysisTracker(JSGraph* jsgraph, EffectGraphReducer* reducer,
                        Zone* zone)
      : virtual_objects_(zone),
        replacements_(zone),
        framestate_might_lazy_deopt_(zone),
        variable_states_(jsgraph, reducer, zone),
        jsgraph_(jsgraph),
        zone_(zone) {}
  EscapeAnalysisTracker(const EscapeAnalysisTracker&) = delete;
  EscapeAnalysisTracker& operator=(const EscapeAnalysisTracker&) = delete;

  class V8_NODISCARD Scope : public VariableTracker::Scope {
   public:
    Scope(EffectGraphReducer* reducer, EscapeAnalysisTracker* tracker,
          Node* node, Reduction* reduction)
        : VariableTracker::Scope(&tracker->variable_states_, node, reduction),
          tracker_(tracker),
          reducer_(reducer) {}
    const VirtualObject* GetVirtualObject(Node* node) {
      VirtualObject* vobject = tracker_->virtual_objects_.Get(node);
      if (vobject) vobject->AddDependency(current_node());
      return vobject;
    }
    // Create or retrieve a virtual object for the current node.
    const VirtualObject* InitVirtualObject(int size) {
      DCHECK_EQ(IrOpcode::kAllocate, current_node()->opcode());
      VirtualObject* vobject = tracker_->virtual_objects_.Get(current_node());
      if (vobject) {
        CHECK(vobject->size() == size);
      } else {
        vobject = tracker_->NewVirtualObject(size);
      }
      if (vobject) vobject->AddDependency(current_node());
      vobject_ = vobject;
      return vobject;
    }

    void SetVirtualObject(Node* object) {
      vobject_ = tracker_->virtual_objects_.Get(object);
    }

    void SetEscaped(Node* node) {
      if (VirtualObject* object = tracker_->virtual_objects_.Get(node)) {
        if (object->HasEscaped()) return;
        TRACE("Setting %s#%d to escaped because of use by %s#%d\n",
              node->op()->mnemonic(), node->id(),
              current_node()->op()->mnemonic(), current_node()->id());
        object->SetEscaped();
        object->RevisitDependants(reducer_);
      }
    }
    // The inputs of the current node have to be accessed through the scope to
    // ensure that they respect the node replacements.
    Node* ValueInput(int i) {
      return tracker_->ResolveReplacement(
          NodeProperties::GetValueInput(current_node(), i));
    }
    Node* ContextInput() {
      return tracker_->ResolveReplacement(
          NodeProperties::GetContextInput(current_node()));
    }
    // Accessing the current node is fine for `FrameState nodes.
    Node* CurrentNode() {
      DCHECK_EQ(current_node()->opcode(), IrOpcode::kFrameState);
      return current_node();
    }

    void SetReplacement(Node* replacement) {
      replacement_ = replacement;
      vobject_ =
          replacement ? tracker_->virtual_objects_.Get(replacement) : nullptr;
      if (replacement) {
        TRACE("Set %s#%d as replacement.\n", replacement->op()->mnemonic(),
              replacement->id());
      } else {
        TRACE("Set nullptr as replacement.\n");
      }
    }

    void MarkForDeletion() { SetReplacement(tracker_->jsgraph_->Dead()); }

    bool FrameStateMightLazyDeopt(Node* framestate) {
      DCHECK_EQ(IrOpcode::kFrameState, framestate->opcode());
      if (auto it = tracker_->framestate_might_lazy_deopt_.find(framestate);
          it != tracker_->framestate_might_lazy_deopt_.end()) {
        return it->second;
      }
      for (Node* use : framestate->uses()) {
        switch (use->opcode()) {
          case IrOpcode::kCheckpoint:
          case IrOpcode::kDeoptimize:
          case IrOpcode::kDeoptimizeIf:
          case IrOpcode::kDeoptimizeUnless:
            // These nodes only cause eager deopts.
            break;
          default:
            if (use->opcode() == IrOpcode::kFrameState &&
                !FrameStateMightLazyDeopt(use)) {
              break;
            }
            return tracker_->framestate_might_lazy_deopt_[framestate] = true;
        }
      }
      return tracker_->framestate_might_lazy_deopt_[framestate] = false;
    }

    ~Scope() {
      if (replacement_ != tracker_->replacements_[current_node()] ||
          vobject_ != tracker_->virtual_objects_.Get(current_node())) {
        reduction()->set_value_changed();
      }
      tracker_->replacements_[current_node()] = replacement_;
      tracker_->virtual_objects_.Set(current_node(), vobject_);
    }

   private:
    EscapeAnalysisTracker* tracker_;
    EffectGraphReducer* reducer_;
    VirtualObject* vobject_ = nullptr;
    Node* replacement_ = nullptr;
  };

  Node* GetReplacementOf(Node* node) { return replacements_[node]; }
  Node* ResolveReplacement(Node* node) {
    if (Node* replacement = GetReplacementOf(node)) {
      return replacement;
    }
    return node;
  }

 private:
  friend class EscapeAnalysisResult;
  static constexpr int kTrackingBudget = 600;

  VirtualObject* NewVirtualObject(int size) {
    if (number_of_tracked_bytes_ + size >= kTrackingBudget) return nullptr;
    number_of_tracked_bytes_ += size;
    return zone_->New<VirtualObject>(&variable_states_, next_object_id_++,
                                     size);
  }

  SparseSidetable<VirtualObject*> virtual_objects_;
  Sidetable<Node*> replacements_;
  ZoneUnorderedMap<Node*, bool> framestate_might_lazy_deopt_;
  VariableTracker variable_states_;
  VirtualObject::Id next_object_id_ = 0;
  int number_of_tracked_bytes_ = 0;
  JSGraph* const jsgraph_;
  Zone* const zone_;
};

EffectGraphReducer::EffectGraphReducer(
    Graph* graph, std::function<void(Node*, Reduction*)> reduce,
    TickCounter* tick_counter, Zone* zone)
    : graph_(graph),
      state_(graph, kNumStates),
      revisit_(zone),
      stack_(zone),
      reduce_(std::move(reduce)),
      tick_counter_(tick_counter) {}

void EffectGraphReducer::ReduceFrom(Node* node) {
  // Perform DFS and eagerly trigger revisitation as soon as possible.
  // A stack element {node, i} indicates that input i of node should be visited
  // next.
  DCHECK(stack_.empty());
  stack_.push({node, 0});
  while (!stack_.empty()) {
    tick_counter_->TickAndMaybeEnterSafepoint();
    Node* current = stack_.top().node;
    int& input_index = stack_.top().input_index;
    if (input_index < current->InputCount()) {
      Node* input = current->InputAt(input_index);
      input_index++;
      switch (state_.Get(input)) {
        case State::kVisited:
          // The input is already reduced.
          break;
        case State::kOnStack:
          // The input is on the DFS stack right now, so it will be revisited
          // later anyway.
          break;
        case State::kUnvisited:
        case State::kRevisit: {
          state_.Set(input, State::kOnStack);
          stack_.push({input, 0});
          break;
        }
      }
    } else {
      stack_.pop();
      Reduction reduction;
      reduce_(current, &reduction);
      for (Edge edge : current->use_edges()) {
        // Mark uses for revisitation.
        Node* use = edge.from();
        if (NodeProperties::IsEffectEdge(edge)) {
          if (reduction.effect_changed()) Revisit(use);
        } else {
          if (reduction.value_changed()) Revisit(use);
        }
      }
      state_.Set(current, State::kVisited);
      // Process the revisitation buffer immediately. This improves performance
      // of escape analysis. Using a stack for {revisit_} reverses the order in
      // which the revisitation happens. This also seems to improve performance.
      while (!revisit_.empty()) {
        Node* revisit = revisit_.top();
        if (state_.Get(revisit) == State::kRevisit) {
          state_.Set(revisit, State::kOnStack);
          stack_.push({revisit, 0});
        }
        revisit_.pop();
      }
    }
  }
}

void EffectGraphReducer::Revisit(Node* node) {
  if (state_.Get(node) == State::kVisited) {
    TRACE("  Queueing for revisit: %s#%d\n", node->op()->mnemonic(),
          node->id());
    state_.Set(node, State::kRevisit);
    revisit_.push(node);
  }
}

VariableTracker::VariableTracker(JSGraph* graph, EffectGraphReducer* reducer,
                                 Zone* zone)
    : zone_(zone),
      graph_(graph),
      table_(zone, State(zone)),
      buffer_(zone),
      reducer_(reducer),
      tick_counter_(reducer->tick_counter()) {}

VariableTracker::Scope::Scope(VariableTracker* states, Node* node,
                              Reduction* reduction)
    : ReduceScope(node, reduction),
      states_(states),
      current_state_(states->zone_) {
  switch (node->opcode()) {
    case IrOpcode::kEffectPhi:
      current_state_ = states_->MergeInputs(node);
      break;
    default:
      int effect_inputs = node->op()->EffectInputCount();
      if (effect_inputs == 1) {
        current_state_ =
            states_->table_.Get(NodeProperties::GetEffectInput(node, 0));
      } else {
        DCHECK_EQ(0, effect_inputs);
      }
  }
}

VariableTracker::Scope::~Scope() {
  if (!reduction()->effect_changed() &&
      states_->table_.Get(current_node()) != current_state_) {
    reduction()->set_effect_changed();
  }
  states_->table_.Set(current_node(), current_state_);
}

VariableTracker::State VariableTracker::MergeInputs(Node* effect_phi) {
  // A variable that is mapped to [nullptr] was not assigned a value on every
  // execution path to the current effect phi. Relying on the invariant that
  // every variable is initialized (at least with a sentinel like the Dead
  // node), this means that the variable initialization does not dominate the
  // current point. So for loop effect phis, we can keep nullptr for a variable
  // as long as the first input of the loop has nullptr for this variable. For
  // non-loop effect phis, we can even keep it nullptr as long as any input has
  // nullptr.
  DCHECK_EQ(IrOpcode::kEffectPhi, effect_phi->opcode());
  int arity = effect_phi->op()->EffectInputCount();
  Node* control = NodeProperties::GetControlInput(effect_phi, 0);
  TRACE("control: %s#%d\n", control->op()->mnemonic(), control->id());
  bool is_loop = control->opcode() == IrOpcode::kLoop;
  buffer_.reserve(arity + 1);

  State first_input = table_.Get(NodeProperties::GetEffectInput(effect_phi, 0));
  State result = first_input;
  for (std::pair<Variable, Node*> var_value : first_input) {
    tick_counter_->TickAndMaybeEnterSafepoint();
    if (Node* value = var_value.second) {
      Variable var = var_value.first;
      TRACE("var %i:\n", var.id_);
      buffer_.clear();
      buffer_.push_back(value);
      bool identical_inputs = true;
      int num_defined_inputs = 1;
      TRACE("  input 0: %s#%d\n", value->op()->mnemonic(), value->id());
      for (int i = 1; i < arity; ++i) {
        Node* next_value =
            table_.Get(NodeProperties::GetEffectInput(effect_phi, i)).Get(var);
        if (next_value != value) identical_inputs = false;
        if (next_value != nullptr) {
          num_defined_inputs++;
          TRACE("  input %i: %s#%d\n", i, next_value->op()->mnemonic(),
                next_value->id());
        } else {
          TRACE("  input %i: nullptr\n", i);
        }
        buffer_.push_back(next_value);
      }

      Node* old_value = table_.Get(effect_phi).Get(var);
      if (old_value) {
        TRACE("  old: %s#%d\n", old_value->op()->mnemonic(), old_value->id());
      } else {
        TRACE("  old: nullptr\n");
      }
      // Reuse a previously created phi node if possible.
      if (old_value && old_value->opcode() == IrOpcode::kPhi &&
          NodeProperties::GetControlInput(old_value, 0) == control) {
        // Since a phi node can never dominate its control node,
        // [old_value] cannot originate from the inputs. Thus [old_value]
        // must have been created by a previous reduction of this [effect_phi].
        for (int i = 0; i < arity; ++i) {
          Node* old_input = NodeProperties::GetValueInput(old_value, i);
          Node* new_input = buffer_[i] ? buffer_[i] : graph_->Dead();
          if (old_input != new_input) {
            NodeProperties::ReplaceValueInput(old_value, new_input, i);
            reducer_->Revisit(old_value);
          }
        }
        result.Set(var, old_value);
      } else {
        if (num_defined_inputs == 1 && is_loop) {
          // For loop effect phis, the variable initialization dominates iff it
          // dominates the first input.
          DCHECK_EQ(2, arity);
          DCHECK_EQ(value, buffer_[0]);
          result.Set(var, value);
        } else if (num_defined_inputs < arity) {
          // If the variable is undefined on some input of this non-loop effect
          // phi, then its initialization does not dominate this point.
          result.Set(var, nullptr);
        } else {
          DCHECK_EQ(num_defined_inputs, arity);
          // We only create a phi if the values are different.
          if (identical_inputs) {
            result.Set(var, value);
          } else {
            TRACE("Creating new phi\n");
            buffer_.push_back(control);
            Node* phi = graph_->graph()->NewNode(
                graph_->common()->Phi(MachineRepresentation::kTagged, arity),
                arity + 1, &buffer_.front());
            // TODO(turbofan): Computing precise types here is tricky, because
            // of the necessary revisitations. If we really need this, we should
            // probably do it afterwards.
            NodeProperties::SetType(phi, Type::Any());
            reducer_->AddRoot(phi);
            result.Set(var, phi);
          }
        }
      }
#ifdef DEBUG
      if (Node* result_node = result.Get(var)) {
        TRACE("  result: %s#%d\n", result_node->op()->mnemonic(),
              result_node->id());
      } else {
        TRACE("  result: nullptr\n");
      }
#endif
    }
  }
  return result;
}

namespace {

int OffsetOfFieldAccess(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kLoadField ||
         op->opcode() == IrOpcode::kStoreField);
  FieldAccess access = FieldAccessOf(op);
  return access.offset;
}

Maybe<int> OffsetOfElementAt(ElementAccess const& access, int index) {
  MachineRepresentation representation = access.machine_type.representation();
  // Double elements accesses are not yet supported. See chromium:1237821.
  if (representation == MachineRepresentation::kFloat64) return Nothing<int>();

  DCHECK_GE(index, 0);
  DCHECK_GE(ElementSizeLog2Of(representation), kTaggedSizeLog2);
  return Just(access.header_size +
              (index << ElementSizeLog2Of(representation)));
}

Maybe<int> OffsetOfElementsAccess(const Operator* op, Node* index_node) {
  DCHECK(op->opcode() == IrOpcode::kLoadElement ||
         op->opcode() == IrOpcode::kStoreElement);
  Type index_type = NodeProperties::GetType(index_node);
  if (!index_type.Is(Type::OrderedNumber())) return Nothing<int>();
  double max = index_type.Max();
  double min = index_type.Min();
  int index = static_cast<int>(min);
  if (index < 0 || index != min || index != max) return Nothing<int>();
  return OffsetOfElementAt(ElementAccessOf(op), index);
}

Node* LowerCompareMapsWithoutLoad(Node* checked_map,
                                  ZoneRefSet<Map> const& checked_against,
                                  JSGraph* jsgraph) {
  Node* true_node = jsgraph->TrueConstant();
  Node* false_node = jsgraph->FalseConstant();
  Node* replacement = false_node;
  for (MapRef map : checked_against) {
    // We are using HeapConstantMaybeHole here instead of HeapConstantNoHole
    // as we cannot do the CHECK(object is hole) here as the compile thread is
    // parked during EscapeAnalysis for performance reasons, see pipeline.cc.
    // TODO(cffsmith): do manual checking against hole values here.
    Node* map_node = jsgraph->HeapConstantMaybeHole(map.object());
    // We cannot create a HeapConstant type here as we are off-thread.
    NodeProperties::SetType(map_node, Type::Internal());
    Node* comparison = jsgraph->graph()->NewNode(
        jsgraph->simplified()->ReferenceEqual(), checked_map, map_node);
    NodeProperties::SetType(comparison, Type::Boolean());
    if (replacement == false_node) {
      replacement = comparison;
    } else {
      replacement = jsgraph->graph()->NewNode(
          jsgraph->common()->Select(MachineRepresentation::kTaggedPointer),
          comparison, true_node, replacement);
      NodeProperties::SetType(replacement, Type::Boolean());
    }
  }
  return replacement;
}

void ReduceNode(const Operator* op, EscapeAnalysisTracker::Scope* current,
                JSGraph* jsgraph) {
  switch (op->opcode()) {
    case IrOpcode::kAllocate: {
      NumberMatcher size(current->ValueInput(0));
      if (!size.HasResolvedValue()) break;
      int size_int = static_cast<int>(size.ResolvedValue());
      if (size_int != size.ResolvedValue()) break;
      if (const VirtualObject* vobject = current->InitVirtualObject(size_int)) {
        // Initialize with dead nodes as a sentinel for uninitialized memory.
        for (Variable field : *vobject) {
          current->Set(field, jsgraph->Dead());
        }
      }
      break;
    }
    case IrOpcode::kFinishRegion:
      current->SetVirtualObject(current->ValueInput(0));
      break;
    case IrOpcode::kStoreField: {
      Node* object = current->ValueInput(0);
      Node* value = current->ValueInput(1);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      Variable var;
      // BoundedSize fields cannot currently be materialized by the deoptimizer,
      // so we must not dematerialze them.
      if (vobject && !vobject->HasEscaped() &&
          vobject->FieldAt(OffsetOfFieldAccess(op)).To(&var) &&
          !FieldAccessOf(op).is_bounded_size_access) {
        current->Set(var, value);
        current->MarkForDeletion();
      } else {
        current->SetEscaped(object);
        current->SetEscaped(value);
      }
      break;
    }
    case IrOpcode::kStoreElement: {
      Node* object = current->ValueInput(0);
      Node* index = current->ValueInput(1);
      Node* value = current->ValueInput(2);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      int offset;
      Variable var;
      if (vobject && !vobject->HasEscaped() &&
          OffsetOfElementsAccess(op, index).To(&offset) &&
          vobject->FieldAt(offset).To(&var)) {
        current->Set(var, value);
        current->MarkForDeletion();
      } else {
        current->SetEscaped(value);
        current->SetEscaped(object);
      }
      break;
    }
    case IrOpcode::kLoadField: {
      Node* object = current->ValueInput(0);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      Variable var;
      Node* value;
      if (vobject && !vobject->HasEscaped() &&
          vobject->FieldAt(OffsetOfFieldAccess(op)).To(&var) &&
          current->Get(var).To(&value)) {
        current->SetReplacement(value);
      } else {
        current->SetEscaped(object);
      }
      break;
    }
    case IrOpcode::kLoadElement: {
      Node* object = current->ValueInput(0);
      Node* index = current->ValueInput(1);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      int offset;
      Variable var;
      Node* value;
      if (vobject && !vobject->HasEscaped() &&
          OffsetOfElementsAccess(op, index).To(&offset) &&
          vobject->FieldAt(offset).To(&var) && current->Get(var).To(&value)) {
        current->SetReplacement(value);
        break;
      } else if (vobject && !vobject->HasEscaped()) {
        // Compute the known length (aka the number of elements) of {object}
        // based on the virtual object information.
        ElementAccess const& access = ElementAccessOf(op);
        int const length =
            (vobject->size() - access.header_size) >>
            ElementSizeLog2Of(access.machine_type.representation());
        Variable var0, var1;
        Node* value0;
        Node* value1;
        if (length == 1 &&
            vobject->FieldAt(OffsetOfElementAt(access, 0)).To(&var) &&
            current->Get(var).To(&value) &&
            (value == nullptr ||
             NodeProperties::GetType(value).Is(access.type))) {
          // The {object} has no elements, and we know that the LoadElement
          // {index} must be within bounds, thus it must always yield this
          // one element of {object}.
          current->SetReplacement(value);
          break;
        } else if (length == 2 &&
                   vobject->FieldAt(OffsetOfElementAt(access, 0)).To(&var0) &&
                   current->Get(var0).To(&value0) &&
                   (value0 == nullptr ||
                    NodeProperties::GetType(value0).Is(access.type)) &&
                   vobject->FieldAt(OffsetOfElementAt(access, 1)).To(&var1) &&
                   current->Get(var1).To(&value1) &&
                   (value1 == nullptr ||
                    NodeProperties::GetType(value1).Is(access.type))) {
          if (value0 && value1) {
            // The {object} has exactly two elements, so the LoadElement
            // must return one of them (i.e. either the element at index
            // 0 or the one at index 1). So we can turn the LoadElement
            // into a Select operation instead (still allowing the {object}
            // to be scalar replaced). We must however mark the elements
            // of the {object} itself as escaping.
            Node* check =
                jsgraph->graph()->NewNode(jsgraph->simplified()->NumberEqual(),
                                          index, jsgraph->ZeroConstant());
            NodeProperties::SetType(check, Type::Boolean());
            Node* select = jsgraph->graph()->NewNode(
                jsgraph->common()->Select(access.machine_type.representation()),
                check, value0, value1);
            NodeProperties::SetType(select, access.type);
            current->SetReplacement(select);
            current->SetEscaped(value0);
            current->SetEscaped(value1);
            break;
          } else {
            // If the variables have no values, we have
            // not reached the fixed-point yet.
            break;
          }
        }
      }
      current->SetEscaped(object);
      break;
    }
    case IrOpcode::kTypeGuard: {
      current->SetVirtualObject(current->ValueInput(0));
      break;
    }
    case IrOpcode::kReferenceEqual: {
      Node* left = current->ValueInput(0);
      Node* right = current->ValueInput(1);
      const VirtualObject* left_object = current->GetVirtualObject(left);
      const VirtualObject* right_object = current->GetVirtualObject(right);
      Node* replacement = nullptr;
      if (left_object && !left_object->HasEscaped()) {
        if (right_object && !right_object->HasEscaped() &&
            left_object->id() == right_object->id()) {
          replacement = jsgraph->TrueConstant();
        } else {
          replacement = jsgraph->FalseConstant();
        }
      } else if (right_object && !right_object->HasEscaped()) {
        replacement = jsgraph->FalseConstant();
      }
      // TODO(turbofan) This is a workaround for uninhabited types. If we
      // replaced a value of uninhabited type with a constant, we would
      // widen the type of the node. This could produce inconsistent
      // types (which might confuse representation selection). We get
      // around this by refusing to constant-fold and escape-analyze
      // if the type is not inhabited.
      if (replacement && !NodeProperties::GetType(left).IsNone() &&
          !NodeProperties::GetType(right).IsNone()) {
        current->SetReplacement(replacement);
        break;
      }
      current->SetEscaped(left);
      current->SetEscaped(right);
      break;
    }
    case IrOpcode::kCheckMaps: {
      CheckMapsParameters params = CheckMapsParametersOf(op);
      Node* checked = current->ValueInput(0);
      const VirtualObject* vobject = current->GetVirtualObject(checked);
      Variable map_field;
      Node* map;
      if (vobject && !vobject->HasEscaped() &&
          vobject->FieldAt(HeapObject::kMapOffset).To(&map_field) &&
          current->Get(map_field).To(&map)) {
        if (map) {
          Type const map_type = NodeProperties::GetType(map);
          if (map_type.IsHeapConstant() &&
              params.maps().contains(
                  map_type.AsHeapConstant()->Ref().AsMap())) {
            current->MarkForDeletion();
            break;
          }
        } else {
          // If the variable has no value, we have not reached the fixed-point
          // yet.
          break;
        }
      }
      current->SetEscaped(checked);
      break;
    }
    case IrOpcode::kCompareMaps: {
      Node* object = current->ValueInput(0);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      Variable map_field;
      Node* object_map;
      if (vobject && !vobject->HasEscaped() &&
          vobject->FieldAt(HeapObject::kMapOffset).To(&map_field) &&
          current->Get(map_field).To(&object_map)) {
        if (object_map) {
          current->SetReplacement(LowerCompareMapsWithoutLoad(
              object_map, CompareMapsParametersOf(op), jsgraph));
          break;
        } else {
          // If the variable has no value, we have not reached the fixed-point
          // yet.
          break;
        }
      }
      current->SetEscaped(object);
      break;
    }
    case IrOpcode::kCheckHeapObject: {
      Node* checked = current->ValueInput(0);
      switch (checked->opcode()) {
        case IrOpcode::kAllocate:
        case IrOpcode::kFinishRegion:
        case IrOpcode::kHeapConstant:
          current->SetReplacement(checked);
          break;
        default:
          current->SetEscaped(checked);
          break;
      }
      break;
    }
    case IrOpcode::kMapGuard: {
      Node* object = current->ValueInput(0);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      if (vobject && !vobject->HasEscaped()) {
        current->MarkForDeletion();
      }
      break;
    }
    case IrOpcode::kStateValues:
      // We visit StateValue nodes through their correpsonding FrameState node,
      // so we need to make sure we revisit the FrameState.
      current->SetValueChanged();
      break;
    case IrOpcode::kFrameState: {
      // We mark the receiver as escaping due to the non-standard `.getThis`
      // API.
      FrameState frame_state{current->CurrentNode()};
      FrameStateType type = frame_state.frame_state_info().type();
      // This needs to be kept in sync with the frame types supported in
      // `OptimizedFrame::Summarize`.
      if (type != FrameStateType::kUnoptimizedFunction &&
          type != FrameStateType::kJavaScriptBuiltinContinuation &&
          type != FrameStateType::kJavaScriptBuiltinContinuationWithCatch) {
        break;
      }
      if (!current->FrameStateMightLazyDeopt(current->CurrentNode())) {
        // Only lazy deopt frame states are used to generate stack traces.
        break;
      }
      StateValuesAccess::iterator it =
          StateValuesAccess(frame_state.parameters()).begin();
      if (!it.done()) {
        if (Node* receiver = it.node()) {
          current->SetEscaped(receiver);
        }
        current->SetEscaped(frame_state.function());
      }
      break;
    }
    default: {
      // For unknown nodes, treat all value inputs as escaping.
      int value_input_count = op->ValueInputCount();
      for (int i = 0; i < value_input_count; ++i) {
        Node* input = current->ValueInput(i);
        current->SetEscaped(input);
      }
      if (OperatorProperties::HasContextInput(op)) {
        current->SetEscaped(current->ContextInput());
      }
      break;
    }
  }
}

}  // namespace

void EscapeAnalysis::Reduce(Node* node, Reduction* reduction) {
  const Operator* op = node->op();
  TRACE("Reducing %s#%d\n", op->mnemonic(), node->id());

  EscapeAnalysisTracker::Scope current(this, tracker_, node, reduction);
  ReduceNode(op, &current, jsgraph());
}

EscapeAnalysis::EscapeAnalysis(JSGraph* jsgraph, TickCounter* tick_counter,
                               Zone* zone)
    : EffectGraphReducer(
          jsgraph->graph(),
          [this](Node* node, Reduction* reduction) { Reduce(node, reduction); },
          tick_counter, zone),
      tracker_(zone->New<EscapeAnalysisTracker>(jsgraph, this, zone)),
      jsgraph_(jsgraph) {}

Node* EscapeAnalysisResult::GetReplacementOf(Node* node) {
  Node* replacement = tracker_->GetReplacementOf(node);
  // Replacements cannot have replacements. This is important to ensure
  // re-visitation: If a replacement is replaced, then all nodes accessing
  // the replacement have to be updated.
  if (replacement) DCHECK_NULL(tracker_->GetReplacementOf(replacement));
  return replacement;
}

Node* EscapeAnalysisResult::GetVirtualObjectField(const VirtualObject* vobject,
                                                  int field, Node* effect) {
  return tracker_->variable_states_.Get(vobject->FieldAt(field).FromJust(),
                                        effect);
}

const VirtualObject* EscapeAnalysisResult::GetVirtualObject(Node* node) {
  return tracker_->virtual_objects_.Get(node);
}

VirtualObject::VirtualObject(VariableTracker* var_states, VirtualObject::Id id,
                             int size)
    : Dependable(var_states->zone()), id_(id), fields_(var_states->zone()) {
  DCHECK(IsAligned(size, kTaggedSize));
  TRACE("Creating VirtualObject id:%d size:%d\n", id, size);
  int num_fields = size / kTaggedSize;
  fields_.reserve(num_fields);
  for (int i = 0; i < num_fields; ++i) {
    fields_.push_back(var_states->NewVariable());
  }
}

#undef TRACE

}  // namespace compiler
}  // namespace internal
}  // namespace v8
