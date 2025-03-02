// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_H_
#define V8_COMPILER_NODE_H_

#include "src/common/globals.h"
#include "src/compiler/graph-zone-traits.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/turbofan-types.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class Edge;
class Graph;


// Marks are used during traversal of the graph to distinguish states of nodes.
// Each node has a mark which is a monotonically increasing integer, and a
// {NodeMarker} has a range of values that indicate states of a node.
using Mark = uint32_t;

// NodeIds are identifying numbers for nodes that can be used to index auxiliary
// out-of-line data associated with each node.
using NodeId = uint32_t;

// A Node is the basic primitive of graphs. Nodes are chained together by
// input/use chains but by default otherwise contain only an identifying number
// which specific applications of graphs and nodes can use to index auxiliary
// out-of-line data, especially transient data.
//
// In addition Nodes only contain a mutable Operator that may change during
// compilation, e.g. during lowering passes. Other information that needs to be
// associated with Nodes during compilation must be stored out-of-line indexed
// by the Node's id.
class V8_EXPORT_PRIVATE Node final {
 public:
  static Node* New(Zone* zone, NodeId id, const Operator* op, int input_count,
                   Node* const* inputs, bool has_extensible_inputs);
  static Node* Clone(Zone* zone, NodeId id, const Node* node);

  inline bool IsDead() const;
  void Kill();

  const Operator* op() const { return op_; }

  constexpr IrOpcode::Value opcode() const {
    DCHECK_GE(IrOpcode::kLast, op_->opcode());
    return static_cast<IrOpcode::Value>(op_->opcode());
  }

  NodeId id() const { return IdField::decode(bit_field_); }

  int InputCount() const {
    return has_inline_inputs() ? InlineCountField::decode(bit_field_)
                               : outline_inputs()->count_;
  }

#ifdef DEBUG
  void Verify();
#else
  inline void Verify() {}
#endif

  Node* InputAt(int index) const {
    DCHECK_LE(0, index);
    DCHECK_LT(index, InputCount());
    return *GetInputPtrConst(index);
  }

  void ReplaceInput(int index, Node* new_to) {
    DCHECK_LE(0, index);
    DCHECK_LT(index, InputCount());
    ZoneNodePtr* input_ptr = GetInputPtr(index);
    Node* old_to = *input_ptr;
    if (old_to != new_to) {
      Use* use = GetUsePtr(index);
      if (old_to) old_to->RemoveUse(use);
      *input_ptr = new_to;
      if (new_to) new_to->AppendUse(use);
    }
  }

  void AppendInput(Zone* zone, Node* new_to);
  void InsertInput(Zone* zone, int index, Node* new_to);
  void InsertInputs(Zone* zone, int index, int count);
  // Returns the removed input.
  Node* RemoveInput(int index);
  void NullAllInputs();
  void TrimInputCount(int new_input_count);
  // Can trim, extend by appending new inputs, or do nothing.
  void EnsureInputCount(Zone* zone, int new_input_count);

  int UseCount() const;
  int BranchUseCount() const;
  void ReplaceUses(Node* replace_to);

  class InputEdges;
  inline InputEdges input_edges();

  class Inputs;
  inline Inputs inputs() const;
  inline base::Vector<Node*> inputs_vector() const;

  class UseEdges final {
   public:
    using value_type = Edge;

    class iterator;
    inline iterator begin() const;
    inline iterator end() const;

    bool empty() const;

    explicit UseEdges(Node* node) : node_(node) {}

   private:
    Node* node_;
  };

  UseEdges use_edges() { return UseEdges(this); }

  class V8_EXPORT_PRIVATE Uses final {
   public:
    using value_type = Node*;

    class const_iterator;
    inline const_iterator begin() const;
    inline const_iterator end() const;

    bool empty() const;

    explicit Uses(Node* node) : node_(node) {}

   private:
    Node* node_;
  };

  Uses uses() { return Uses(this); }

  // Returns true if {owner} is the only user of {this} node.
  bool OwnedBy(Node const* owner) const;

  // Returns true if {owner1} and {owner2} are the only users of {this} node.
  bool OwnedBy(Node const* owner1, Node const* owner2) const;

  void Print() const { Print(1); }
  void Print(int depth) const;
  void Print(std::ostream&, int depth = 1) const;

 private:
  template <typename NodePtrT>
  inline static Node* NewImpl(Zone* zone, NodeId id, const Operator* op,
                              int input_count, NodePtrT const* inputs,
                              bool has_extensible_inputs);

  struct Use;
  using ZoneUsePtr = GraphZoneTraits::Ptr<Use>;

  // Out of line storage for inputs when the number of inputs overflowed the
  // capacity of the inline-allocated space.
  struct OutOfLineInputs {
    ZoneNodePtr node_;
    int count_;
    int capacity_;

    // Inputs are allocated right behind the OutOfLineInputs instance.
    inline ZoneNodePtr* inputs();

    static OutOfLineInputs* New(Zone* zone, int capacity);
    void ExtractFrom(Use* use_ptr, ZoneNodePtr* input_ptr, int count);
  };
  using ZoneOutOfLineInputsPtr = GraphZoneTraits::Ptr<OutOfLineInputs>;

  // A link in the use chain for a node. Every input {i} to a node {n} has an
  // associated {Use} which is linked into the use chain of the {i} node.
  struct Use {
    ZoneUsePtr next;
    ZoneUsePtr prev;
    uint32_t bit_field_;

    int input_index() const { return InputIndexField::decode(bit_field_); }
    bool is_inline_use() const { return InlineField::decode(bit_field_); }
    ZoneNodePtr* input_ptr() {
      int index = input_index();
      Use* start = this + 1 + index;
      ZoneNodePtr* inputs =
          is_inline_use() ? reinterpret_cast<Node*>(start)->inline_inputs()
                          : reinterpret_cast<OutOfLineInputs*>(start)->inputs();
      return &inputs[index];
    }

    Node* from() {
      Use* start = this + 1 + input_index();
      return is_inline_use() ? reinterpret_cast<Node*>(start)
                             : reinterpret_cast<OutOfLineInputs*>(start)->node_;
    }

    using InlineField = base::BitField<bool, 0, 1>;
    using InputIndexField = base::BitField<unsigned, 1, 31>;
  };

  //============================================================================
  //== Memory layout ===========================================================
  //============================================================================
  // Saving space for big graphs is important. We use a memory layout trick to
  // be able to map {Node} objects to {Use} objects and vice-versa in a
  // space-efficient manner.
  //
  // {Use} links are laid out in memory directly before a {Node}, followed by
  // direct pointers to input {Nodes}.
  //
  // inline case:
  // |Use #N  |Use #N-1|...|Use #1  |Use #0  |Node xxxx |I#0|I#1|...|I#N-1|I#N|
  //          ^                              ^                  ^
  //          + Use                          + Node             + Input
  //
  // Since every {Use} instance records its {input_index}, pointer arithmetic
  // can compute the {Node}.
  //
  // out-of-line case:
  //     |Node xxxx |
  //     ^       + outline ------------------+
  //     +----------------------------------------+
  //                                         |    |
  //                                         v    | node
  // |Use #N  |Use #N-1|...|Use #1  |Use #0  |OOL xxxxx |I#0|I#1|...|I#N-1|I#N|
  //          ^                                                 ^
  //          + Use                                             + Input
  //
  // Out-of-line storage of input lists is needed if appending an input to
  // a node exceeds the maximum inline capacity.

  Node(NodeId id, const Operator* op, int inline_count, int inline_capacity);
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  inline Address inputs_location() const;

  ZoneNodePtr* inline_inputs() const {
    return reinterpret_cast<ZoneNodePtr*>(inputs_location());
  }
  OutOfLineInputs* outline_inputs() const {
    return *reinterpret_cast<ZoneOutOfLineInputsPtr*>(inputs_location());
  }
  void set_outline_inputs(OutOfLineInputs* outline) {
    *reinterpret_cast<ZoneOutOfLineInputsPtr*>(inputs_location()) = outline;
  }

  ZoneNodePtr const* GetInputPtrConst(int input_index) const {
    return has_inline_inputs() ? &(inline_inputs()[input_index])
                               : &(outline_inputs()->inputs()[input_index]);
  }
  ZoneNodePtr* GetInputPtr(int input_index) {
    return has_inline_inputs() ? &(inline_inputs()[input_index])
                               : &(outline_inputs()->inputs()[input_index]);
  }
  Use* GetUsePtr(int input_index) {
    Use* ptr = has_inline_inputs() ? reinterpret_cast<Use*>(this)
                                   : reinterpret_cast<Use*>(outline_inputs());
    return &ptr[-1 - input_index];
  }

  void AppendUse(Use* use);
  void RemoveUse(Use* use);

  void* operator new(size_t, void* location) { return location; }

  // Only NodeProperties should manipulate the op.
  void set_op(const Operator* op) { op_ = op; }

  // Only NodeProperties should manipulate the type.
  Type type() const { return type_; }
  void set_type(Type type) { type_ = type; }

  // Only NodeMarkers should manipulate the marks on nodes.
  Mark mark() const { return mark_; }
  void set_mark(Mark mark) { mark_ = mark; }

  inline bool has_inline_inputs() const {
    return InlineCountField::decode(bit_field_) != kOutlineMarker;
  }

  void ClearInputs(int start, int count);

  using IdField = base::BitField<NodeId, 0, 24>;
  using InlineCountField = base::BitField<unsigned, 24, 4>;
  using InlineCapacityField = base::BitField<unsigned, 28, 4>;
  static const int kOutlineMarker = InlineCountField::kMax;
  static const int kMaxInlineCapacity = InlineCapacityField::kMax - 1;

  const Operator* op_;
  Type type_;
  Mark mark_;
  uint32_t bit_field_;
  ZoneUsePtr first_use_;

  friend class Edge;
  friend class NodeMarkerBase;
  friend class NodeProperties;
};

Address Node::inputs_location() const {
  return reinterpret_cast<Address>(this) + sizeof(Node);
}

ZoneNodePtr* Node::OutOfLineInputs::inputs() {
  return reinterpret_cast<ZoneNodePtr*>(reinterpret_cast<Address>(this) +
                                        sizeof(Node::OutOfLineInputs));
}

std::ostream& operator<<(std::ostream& os, const Node& n);

// Base class for node wrappers.
class NodeWrapper {
 public:
  explicit constexpr NodeWrapper(Node* node) : node_(node) {}
  operator Node*() const { return node_; }
  Node* operator->() const { return node_; }

 protected:
  Node* node() const { return node_; }
  void set_node(Node* node) {
    DCHECK_NOT_NULL(node);
    node_ = node;
  }

 private:
  Node* node_;
};

// Wrapper classes for special node/edge types (effect, control, frame states).

class Effect : public NodeWrapper {
 public:
  explicit constexpr Effect(Node* node) : NodeWrapper(node) {
    // TODO(jgruber): Remove the End special case.
    SLOW_DCHECK(node == nullptr || node->op()->opcode() == IrOpcode::kEnd ||
                node->op()->EffectOutputCount() > 0);
  }

  // Support the common `Node* x = effect = ...` pattern.
  Node* operator=(Node* value) {
    DCHECK_GT(value->op()->EffectOutputCount(), 0);
    set_node(value);
    return value;
  }
};

class Control : public NodeWrapper {
 public:
  explicit constexpr Control(Node* node) : NodeWrapper(node) {
    // TODO(jgruber): Remove the End special case.
    SLOW_DCHECK(node == nullptr || node->opcode() == IrOpcode::kEnd ||
                node->op()->ControlOutputCount() > 0);
  }

  // Support the common `Node* x = control = ...` pattern.
  Node* operator=(Node* value) {
    DCHECK_GT(value->op()->ControlOutputCount(), 0);
    set_node(value);
    return value;
  }
};

// Typedefs to shorten commonly used Node containers.
using NodeDeque = ZoneDeque<Node*>;
using NodeSet = ZoneSet<Node*>;
using NodeVector = ZoneVector<Node*>;
using NodeVectorVector = ZoneVector<NodeVector>;

class Node::InputEdges final {
 public:
  using value_type = Edge;

  class iterator;
  inline iterator begin() const;
  inline iterator end() const;

  bool empty() const { return count_ == 0; }
  int count() const { return count_; }

  inline value_type operator[](int index) const;

  InputEdges(ZoneNodePtr* input_root, Use* use_root, int count)
      : input_root_(input_root), use_root_(use_root), count_(count) {}

 private:
  ZoneNodePtr* input_root_;
  Use* use_root_;
  int count_;
};

class V8_EXPORT_PRIVATE Node::Inputs final {
 public:
  using value_type = Node*;

  class const_iterator;
  inline const_iterator begin() const;
  inline const_iterator end() const;

  bool empty() const { return count_ == 0; }
  int count() const { return count_; }

  inline value_type operator[](int index) const;

  explicit Inputs(ZoneNodePtr const* input_root, int count)
      : input_root_(input_root), count_(count) {}

 private:
  ZoneNodePtr const* input_root_;
  int count_;
};

// An encapsulation for information associated with a single use of a node as an
// input from another node, allowing access to both the defining node and
// the node having the input.
class Edge final {
 public:
  Node* from() const { return use_->from(); }
  Node* to() const { return *input_ptr_; }
  int index() const {
    int const index = use_->input_index();
    DCHECK_LT(index, use_->from()->InputCount());
    return index;
  }

  bool operator==(const Edge& other) { return input_ptr_ == other.input_ptr_; }
  bool operator!=(const Edge& other) { return !(*this == other); }

  void UpdateTo(Node* new_to) {
    Node* old_to = *input_ptr_;
    if (old_to != new_to) {
      if (old_to) old_to->RemoveUse(use_);
      *input_ptr_ = new_to;
      if (new_to) new_to->AppendUse(use_);
    }
  }

 private:
  friend class Node::UseEdges::iterator;
  friend class Node::InputEdges;
  friend class Node::InputEdges::iterator;

  Edge(Node::Use* use, ZoneNodePtr* input_ptr)
      : use_(use), input_ptr_(input_ptr) {
    DCHECK_NOT_NULL(use);
    DCHECK_NOT_NULL(input_ptr);
    DCHECK_EQ(input_ptr, use->input_ptr());
  }

  Node::Use* use_;
  ZoneNodePtr* input_ptr_;
};

bool Node::IsDead() const {
  Node::Inputs inputs = this->inputs();
  return inputs.count() > 0 && inputs[0] == nullptr;
}

Node::InputEdges Node::input_edges() {
  int inline_count = InlineCountField::decode(bit_field_);
  if (inline_count != kOutlineMarker) {
    return InputEdges(inline_inputs(), reinterpret_cast<Use*>(this) - 1,
                      inline_count);
  } else {
    return InputEdges(outline_inputs()->inputs(),
                      reinterpret_cast<Use*>(outline_inputs()) - 1,
                      outline_inputs()->count_);
  }
}

Node::Inputs Node::inputs() const {
  int inline_count = InlineCountField::decode(bit_field_);
  if (inline_count != kOutlineMarker) {
    return Inputs(inline_inputs(), inline_count);
  } else {
    return Inputs(outline_inputs()->inputs(), outline_inputs()->count_);
  }
}

base::Vector<Node*> Node::inputs_vector() const {
  int inline_count = InlineCountField::decode(bit_field_);
  if (inline_count != kOutlineMarker) {
    return base::VectorOf<Node*>(inline_inputs(), inline_count);
  } else {
    return base::VectorOf<Node*>(outline_inputs()->inputs(),
                                 outline_inputs()->count_);
  }
}

// A forward iterator to visit the edges for the input dependencies of a node.
class Node::InputEdges::iterator final {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = Edge;
  using pointer = Edge*;
  using reference = Edge&;

  iterator() : use_(nullptr), input_ptr_(nullptr) {}
  iterator(const iterator& other) = default;

  Edge operator*() const { return Edge(use_, input_ptr_); }
  bool operator==(const iterator& other) const {
    return input_ptr_ == other.input_ptr_;
  }
  bool operator!=(const iterator& other) const { return !(*this == other); }
  iterator& operator++() {
    input_ptr_++;
    use_--;
    return *this;
  }
  iterator operator++(int);
  iterator& operator+=(difference_type offset) {
    input_ptr_ += offset;
    use_ -= offset;
    return *this;
  }
  iterator operator+(difference_type offset) const {
    return iterator(use_ - offset, input_ptr_ + offset);
  }
  difference_type operator-(const iterator& other) const {
    return input_ptr_ - other.input_ptr_;
  }

 private:
  friend class Node;

  explicit iterator(Use* use, ZoneNodePtr* input_ptr)
      : use_(use), input_ptr_(input_ptr) {}

  Use* use_;
  ZoneNodePtr* input_ptr_;
};


Node::InputEdges::iterator Node::InputEdges::begin() const {
  return Node::InputEdges::iterator(use_root_, input_root_);
}


Node::InputEdges::iterator Node::InputEdges::end() const {
  return Node::InputEdges::iterator(use_root_ - count_, input_root_ + count_);
}

Edge Node::InputEdges::operator[](int index) const {
  return Edge(use_root_ + index, input_root_ + index);
}

// A forward iterator to visit the inputs of a node.
class Node::Inputs::const_iterator final {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = Node*;
  using pointer = const value_type*;
  using reference = value_type&;

  const_iterator(const const_iterator& other) = default;

  Node* operator*() const { return *input_ptr_; }
  bool operator==(const const_iterator& other) const {
    return input_ptr_ == other.input_ptr_;
  }
  bool operator!=(const const_iterator& other) const {
    return !(*this == other);
  }
  const_iterator& operator++() {
    ++input_ptr_;
    return *this;
  }
  const_iterator operator++(int);
  const_iterator& operator+=(difference_type offset) {
    input_ptr_ += offset;
    return *this;
  }
  const_iterator operator+(difference_type offset) const {
    return const_iterator(input_ptr_ + offset);
  }
  difference_type operator-(const const_iterator& other) const {
    return input_ptr_ - other.input_ptr_;
  }

 private:
  friend class Node::Inputs;

  explicit const_iterator(ZoneNodePtr const* input_ptr)
      : input_ptr_(input_ptr) {}

  ZoneNodePtr const* input_ptr_;
};


Node::Inputs::const_iterator Node::Inputs::begin() const {
  return const_iterator(input_root_);
}


Node::Inputs::const_iterator Node::Inputs::end() const {
  return const_iterator(input_root_ + count_);
}

Node* Node::Inputs::operator[](int index) const { return input_root_[index]; }

// A forward iterator to visit the uses edges of a node.
class Node::UseEdges::iterator final {
 public:
  iterator(const iterator& other) = default;

  Edge operator*() const { return Edge(current_, current_->input_ptr()); }
  bool operator==(const iterator& other) const {
    return current_ == other.current_;
  }
  bool operator!=(const iterator& other) const { return !(*this == other); }
  iterator& operator++() {
    DCHECK_NOT_NULL(current_);
    current_ = next_;
    next_ = current_ ? static_cast<Node::Use*>(current_->next) : nullptr;
    return *this;
  }
  iterator operator++(int);

 private:
  friend class Node::UseEdges;

  iterator() : current_(nullptr), next_(nullptr) {}
  explicit iterator(Node* node)
      : current_(node->first_use_),
        next_(current_ ? static_cast<Node::Use*>(current_->next) : nullptr) {}

  Node::Use* current_;
  Node::Use* next_;
};


Node::UseEdges::iterator Node::UseEdges::begin() const {
  return Node::UseEdges::iterator(this->node_);
}


Node::UseEdges::iterator Node::UseEdges::end() const {
  return Node::UseEdges::iterator();
}


// A forward iterator to visit the uses of a node.
class Node::Uses::const_iterator final {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = int;
  using value_type = Node*;
  using pointer = Node**;
  using reference = Node*&;

  Node* operator*() const { return current_->from(); }
  bool operator==(const const_iterator& other) const {
    return other.current_ == current_;
  }
  bool operator!=(const const_iterator& other) const {
    return other.current_ != current_;
  }
  const_iterator& operator++() {
    DCHECK_NOT_NULL(current_);
    // Checking no use gets mutated while iterating through them, a potential
    // very tricky cause of bug.
    current_ = current_->next;
#ifdef DEBUG
    DCHECK_EQ(current_, next_);
    next_ = current_ ? current_->next : nullptr;
#endif
    return *this;
  }
  const_iterator operator++(int);

 private:
  friend class Node::Uses;

  const_iterator() : current_(nullptr) {}
  explicit const_iterator(Node* node)
      : current_(node->first_use_)
#ifdef DEBUG
        ,
        next_(current_ ? current_->next : nullptr)
#endif
  {
  }

  Node::Use* current_;
#ifdef DEBUG
  Node::Use* next_;
#endif
};


Node::Uses::const_iterator Node::Uses::begin() const {
  return const_iterator(this->node_);
}


Node::Uses::const_iterator Node::Uses::end() const { return const_iterator(); }

inline Node::Uses::const_iterator begin(const Node::Uses& uses) {
  return uses.begin();
}
inline Node::Uses::const_iterator end(const Node::Uses& uses) {
  return uses.end();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_H_
