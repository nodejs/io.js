// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ESCAPE_ANALYSIS_H_
#define V8_COMPILER_ESCAPE_ANALYSIS_H_

#include "src/base/hashing.h"
#include "src/common/globals.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/persistent-map.h"
#include "src/objects/name.h"

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

class CommonOperatorBuilder;
class VariableTracker;
class EscapeAnalysisTracker;

// {EffectGraphReducer} reduces up to a fixed point. It distinguishes changes to
// the effect output of a node from changes to the value output to reduce the
// number of revisitations.
class EffectGraphReducer {
 public:
  class Reduction {
   public:
    bool value_changed() const { return value_changed_; }
    void set_value_changed() { value_changed_ = true; }
    bool effect_changed() const { return effect_changed_; }
    void set_effect_changed() { effect_changed_ = true; }

   private:
    bool value_changed_ = false;
    bool effect_changed_ = false;
  };

  EffectGraphReducer(Graph* graph,
                     std::function<void(Node*, Reduction*)> reduce,
                     TickCounter* tick_counter, Zone* zone);

  void ReduceGraph() { ReduceFrom(graph_->end()); }

  // Mark node for revisitation.
  void Revisit(Node* node);

  // Add a new root node to start reduction from. This is useful if the reducer
  // adds nodes that are not yet reachable, but should already be considered
  // part of the graph.
  void AddRoot(Node* node) {
    DCHECK_EQ(State::kUnvisited, state_.Get(node));
    state_.Set(node, State::kRevisit);
    revisit_.push(node);
  }

  bool Complete() { return stack_.empty() && revisit_.empty(); }

  TickCounter* tick_counter() const { return tick_counter_; }

 private:
  struct NodeState {
    Node* node;
    int input_index;
  };
  void ReduceFrom(Node* node);
  enum class State : uint8_t { kUnvisited = 0, kRevisit, kOnStack, kVisited };
  const uint8_t kNumStates = static_cast<uint8_t>(State::kVisited) + 1;
  Graph* graph_;
  NodeMarker<State> state_;
  ZoneStack<Node*> revisit_;
  ZoneStack<NodeState> stack_;
  std::function<void(Node*, Reduction*)> reduce_;
  TickCounter* const tick_counter_;
};

// A variable is an abstract storage location, which is lowered to SSA values
// and phi nodes by {VariableTracker}.
class Variable {
 public:
  Variable() : id_(kInvalid) {}
  bool operator==(Variable other) const { return id_ == other.id_; }
  bool operator!=(Variable other) const { return id_ != other.id_; }
  bool operator<(Variable other) const { return id_ < other.id_; }
  static Variable Invalid() { return Variable(kInvalid); }
  friend V8_INLINE size_t hash_value(Variable v) {
    return base::hash_value(v.id_);
  }
  friend std::ostream& operator<<(std::ostream& os, Variable var) {
    return os << var.id_;
  }

 private:
  using Id = int;
  explicit Variable(Id id) : id_(id) {}
  Id id_;
  static const Id kInvalid = -1;

  friend class VariableTracker;
};

// An object that can track the nodes in the graph whose current reduction
// depends on the value of the object.
class Dependable : public ZoneObject {
 public:
  explicit Dependable(Zone* zone) : dependants_(zone) {}
  void AddDependency(Node* node) { dependants_.push_back(node); }
  void RevisitDependants(EffectGraphReducer* reducer) {
    for (Node* node : dependants_) {
      reducer->Revisit(node);
    }
    dependants_.clear();
  }

 private:
  ZoneVector<Node*> dependants_;
};

// A virtual object represents an allocation site and tracks the Variables
// associated with its fields as well as its global escape status.
class VirtualObject : public Dependable {
 public:
  using Id = uint32_t;
  using const_iterator = ZoneVector<Variable>::const_iterator;
  VirtualObject(VariableTracker* var_states, Id id, int size);
  Maybe<Variable> FieldAt(int offset) const {
    CHECK(IsAligned(offset, kTaggedSize));
    CHECK(!HasEscaped());
    if (offset >= size()) {
      // TODO(turbofan): Reading out-of-bounds can only happen in unreachable
      // code. In this case, we have to mark the object as escaping to avoid
      // dead nodes in the graph. This is a workaround that should be removed
      // once we can handle dead nodes everywhere.
      return Nothing<Variable>();
    }
    return Just(fields_.at(offset / kTaggedSize));
  }
  Maybe<Variable> FieldAt(Maybe<int> maybe_offset) const {
    int offset;
    if (!maybe_offset.To(&offset)) return Nothing<Variable>();
    return FieldAt(offset);
  }
  Id id() const { return id_; }
  int size() const { return static_cast<int>(kTaggedSize * fields_.size()); }
  // Escaped might mean that the object escaped to untracked memory or that it
  // is used in an operation that requires materialization.
  void SetEscaped() { escaped_ = true; }
  bool HasEscaped() const { return escaped_; }
  const_iterator begin() const { return fields_.begin(); }
  const_iterator end() const { return fields_.end(); }

 private:
  bool escaped_ = false;
  Id id_;
  ZoneVector<Variable> fields_;
};

class EscapeAnalysisResult {
 public:
  explicit EscapeAnalysisResult(EscapeAnalysisTracker* tracker)
      : tracker_(tracker) {}

  const VirtualObject* GetVirtualObject(Node* node);
  Node* GetVirtualObjectField(const VirtualObject* vobject, int field,
                              Node* effect);
  Node* GetReplacementOf(Node* node);

 private:
  EscapeAnalysisTracker* tracker_;
};

class V8_EXPORT_PRIVATE EscapeAnalysis final
    : public NON_EXPORTED_BASE(EffectGraphReducer) {
 public:
  EscapeAnalysis(JSGraph* jsgraph, TickCounter* tick_counter, Zone* zone);

  EscapeAnalysisResult analysis_result() {
    DCHECK(Complete());
    return EscapeAnalysisResult(tracker_);
  }

 private:
  void Reduce(Node* node, Reduction* reduction);
  JSGraph* jsgraph() { return jsgraph_; }
  Isolate* isolate() const { return jsgraph_->isolate(); }
  EscapeAnalysisTracker* tracker_;
  JSGraph* jsgraph_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ESCAPE_ANALYSIS_H_
