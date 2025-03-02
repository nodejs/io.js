// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node.h"

namespace v8 {
namespace internal {
namespace compiler {

Node::OutOfLineInputs* Node::OutOfLineInputs::New(Zone* zone, int capacity) {
  size_t size =
      sizeof(OutOfLineInputs) + capacity * (sizeof(Node*) + sizeof(Use));
  intptr_t raw_buffer =
      reinterpret_cast<intptr_t>(zone->Allocate<Node::OutOfLineInputs>(size));
  Node::OutOfLineInputs* outline =
      reinterpret_cast<OutOfLineInputs*>(raw_buffer + capacity * sizeof(Use));
  outline->capacity_ = capacity;
  outline->count_ = 0;
  return outline;
}

void Node::OutOfLineInputs::ExtractFrom(Use* old_use_ptr,
                                        ZoneNodePtr* old_input_ptr, int count) {
  DCHECK_GE(count, 0);
  // Extract the inputs from the old use and input pointers and copy them
  // to this out-of-line-storage.
  Use* new_use_ptr = reinterpret_cast<Use*>(this) - 1;
  ZoneNodePtr* new_input_ptr = inputs();
  CHECK_IMPLIES(count > 0, Use::InputIndexField::is_valid(count - 1));
  for (int current = 0; current < count; current++) {
    new_use_ptr->bit_field_ =
        Use::InputIndexField::encode(current) | Use::InlineField::encode(false);
    DCHECK_EQ(old_input_ptr, old_use_ptr->input_ptr());
    DCHECK_EQ(new_input_ptr, new_use_ptr->input_ptr());
    Node* old_to = *old_input_ptr;
    if (old_to) {
      *old_input_ptr = nullptr;
      old_to->RemoveUse(old_use_ptr);
      *new_input_ptr = old_to;
      old_to->AppendUse(new_use_ptr);
    } else {
      *new_input_ptr = nullptr;
    }
    old_input_ptr++;
    new_input_ptr++;
    old_use_ptr--;
    new_use_ptr--;
  }
  this->count_ = count;
}

// These structs are just type tags for Zone::Allocate<T>(size_t) calls.
struct NodeWithOutOfLineInputs {};
struct NodeWithInLineInputs {};

template <typename NodePtrT>
Node* Node::NewImpl(Zone* zone, NodeId id, const Operator* op, int input_count,
                    NodePtrT const* inputs, bool has_extensible_inputs) {
  // Node uses compressed pointers, so zone must support pointer compression.
  DCHECK_IMPLIES(kCompressGraphZone, zone->supports_compression());
  DCHECK_GE(input_count, 0);

  ZoneNodePtr* input_ptr;
  Use* use_ptr;
  Node* node;
  bool is_inline;

  // Verify that none of the inputs are {nullptr}.
  for (int i = 0; i < input_count; i++) {
    if (inputs[i] == nullptr) {
      FATAL("Node::New() Error: #%d:%s[%d] is nullptr", static_cast<int>(id),
            op->mnemonic(), i);
    }
  }

  if (input_count > kMaxInlineCapacity) {
    // Allocate out-of-line inputs.
    int capacity =
        has_extensible_inputs ? input_count + kMaxInlineCapacity : input_count;
    OutOfLineInputs* outline = OutOfLineInputs::New(zone, capacity);

    // Allocate node, with space for OutOfLineInputs pointer.
    void* node_buffer = zone->Allocate<NodeWithOutOfLineInputs>(
        sizeof(Node) + sizeof(ZoneOutOfLineInputsPtr));
    node = new (node_buffer) Node(id, op, kOutlineMarker, 0);
    node->set_outline_inputs(outline);

    outline->node_ = node;
    outline->count_ = input_count;

    input_ptr = outline->inputs();
    use_ptr = reinterpret_cast<Use*>(outline);
    is_inline = false;
  } else {
    // Allocate node with inline inputs. Capacity must be at least 1 so that
    // an OutOfLineInputs pointer can be stored when inputs are added later.
    int capacity = std::max(1, input_count);
    if (has_extensible_inputs) {
      const int max = kMaxInlineCapacity;
      capacity = std::min(input_count + 3, max);
    }

    size_t size = sizeof(Node) + capacity * (sizeof(ZoneNodePtr) + sizeof(Use));
    intptr_t raw_buffer =
        reinterpret_cast<intptr_t>(zone->Allocate<NodeWithInLineInputs>(size));
    void* node_buffer =
        reinterpret_cast<void*>(raw_buffer + capacity * sizeof(Use));

    node = new (node_buffer) Node(id, op, input_count, capacity);
    input_ptr = node->inline_inputs();
    use_ptr = reinterpret_cast<Use*>(node);
    is_inline = true;
  }

  // Initialize the input pointers and the uses.
  CHECK_IMPLIES(input_count > 0,
                Use::InputIndexField::is_valid(input_count - 1));
  for (int current = 0; current < input_count; ++current) {
    Node* to = *inputs++;
    input_ptr[current] = to;
    Use* use = use_ptr - 1 - current;
    use->bit_field_ = Use::InputIndexField::encode(current) |
                      Use::InlineField::encode(is_inline);
    to->AppendUse(use);
  }
  node->Verify();
  return node;
}

Node* Node::New(Zone* zone, NodeId id, const Operator* op, int input_count,
                Node* const* inputs, bool has_extensible_inputs) {
  return NewImpl(zone, id, op, input_count, inputs, has_extensible_inputs);
}

Node* Node::Clone(Zone* zone, NodeId id, const Node* node) {
  int const input_count = node->InputCount();
  ZoneNodePtr const* const inputs = node->has_inline_inputs()
                                        ? node->inline_inputs()
                                        : node->outline_inputs()->inputs();
  Node* const clone = NewImpl(zone, id, node->op(), input_count, inputs, false);
  clone->set_type(node->type());
  return clone;
}


void Node::Kill() {
  DCHECK_NOT_NULL(op());
  NullAllInputs();
  DCHECK(uses().empty());
}


void Node::AppendInput(Zone* zone, Node* new_to) {
  DCHECK_NOT_NULL(zone);
  DCHECK_NOT_NULL(new_to);

  int const inline_count = InlineCountField::decode(bit_field_);
  int const inline_capacity = InlineCapacityField::decode(bit_field_);
  if (inline_count < inline_capacity) {
    // Append inline input.
    bit_field_ = InlineCountField::update(bit_field_, inline_count + 1);
    *GetInputPtr(inline_count) = new_to;
    Use* use = GetUsePtr(inline_count);
    static_assert(InlineCapacityField::kMax <= Use::InputIndexField::kMax);
    use->bit_field_ = Use::InputIndexField::encode(inline_count) |
                      Use::InlineField::encode(true);
    new_to->AppendUse(use);
  } else {
    // Append out-of-line input.
    int const input_count = InputCount();
    OutOfLineInputs* outline = nullptr;
    if (inline_count != kOutlineMarker) {
      // switch to out of line inputs.
      outline = OutOfLineInputs::New(zone, input_count * 2 + 3);
      outline->node_ = this;
      outline->ExtractFrom(GetUsePtr(0), GetInputPtr(0), input_count);
      bit_field_ = InlineCountField::update(bit_field_, kOutlineMarker);
      set_outline_inputs(outline);
    } else {
      // use current out of line inputs.
      outline = outline_inputs();
      if (input_count >= outline->capacity_) {
        // out of space in out-of-line inputs.
        outline = OutOfLineInputs::New(zone, input_count * 2 + 3);
        outline->node_ = this;
        outline->ExtractFrom(GetUsePtr(0), GetInputPtr(0), input_count);
        set_outline_inputs(outline);
      }
    }
    outline->count_++;
    *GetInputPtr(input_count) = new_to;
    Use* use = GetUsePtr(input_count);
    CHECK(Use::InputIndexField::is_valid(input_count));
    use->bit_field_ = Use::InputIndexField::encode(input_count) |
                      Use::InlineField::encode(false);
    new_to->AppendUse(use);
  }
  Verify();
}


void Node::InsertInput(Zone* zone, int index, Node* new_to) {
  DCHECK_NOT_NULL(zone);
  DCHECK_LE(0, index);
  DCHECK_LT(index, InputCount());
  AppendInput(zone, InputAt(InputCount() - 1));
  for (int i = InputCount() - 1; i > index; --i) {
    ReplaceInput(i, InputAt(i - 1));
  }
  ReplaceInput(index, new_to);
  Verify();
}

void Node::InsertInputs(Zone* zone, int index, int count) {
  DCHECK_NOT_NULL(zone);
  DCHECK_LE(0, index);
  DCHECK_LT(0, count);
  DCHECK_LT(index, InputCount());
  for (int i = 0; i < count; i++) {
    AppendInput(zone, InputAt(std::max(InputCount() - count, 0)));
  }
  for (int i = InputCount() - count - 1; i >= std::max(index, count); --i) {
    ReplaceInput(i, InputAt(i - count));
  }
  for (int i = 0; i < count; i++) {
    ReplaceInput(index + i, nullptr);
  }
  Verify();
}

Node* Node::RemoveInput(int index) {
  DCHECK_LE(0, index);
  DCHECK_LT(index, InputCount());
  Node* result = InputAt(index);
  for (; index < InputCount() - 1; ++index) {
    ReplaceInput(index, InputAt(index + 1));
  }
  TrimInputCount(InputCount() - 1);
  Verify();
  return result;
}

void Node::ClearInputs(int start, int count) {
  ZoneNodePtr* input_ptr = GetInputPtr(start);
  Use* use_ptr = GetUsePtr(start);
  while (count-- > 0) {
    DCHECK_EQ(input_ptr, use_ptr->input_ptr());
    Node* input = *input_ptr;
    *input_ptr = nullptr;
    if (input) input->RemoveUse(use_ptr);
    input_ptr++;
    use_ptr--;
  }
  Verify();
}


void Node::NullAllInputs() { ClearInputs(0, InputCount()); }


void Node::TrimInputCount(int new_input_count) {
  int current_count = InputCount();
  DCHECK_LE(new_input_count, current_count);
  if (new_input_count == current_count) return;  // Nothing to do.
  ClearInputs(new_input_count, current_count - new_input_count);
  if (has_inline_inputs()) {
    bit_field_ = InlineCountField::update(bit_field_, new_input_count);
  } else {
    outline_inputs()->count_ = new_input_count;
  }
}

void Node::EnsureInputCount(Zone* zone, int new_input_count) {
  int current_count = InputCount();
  DCHECK_NE(current_count, 0);
  if (current_count > new_input_count) {
    TrimInputCount(new_input_count);
  } else if (current_count < new_input_count) {
    Node* dummy = InputAt(current_count - 1);
    do {
      AppendInput(zone, dummy);
      current_count++;
    } while (current_count < new_input_count);
  }
}

int Node::UseCount() const {
  int use_count = 0;
  for (const Use* use = first_use_; use; use = use->next) {
    ++use_count;
  }
  return use_count;
}

int Node::BranchUseCount() const {
  int use_count = 0;
  for (Use* use = first_use_; use; use = use->next) {
    if (use->from()->opcode() == IrOpcode::kBranch) {
      ++use_count;
    }
  }
  return use_count;
}

void Node::ReplaceUses(Node* that) {
  DCHECK(this->first_use_ == nullptr || this->first_use_->prev == nullptr);
  DCHECK(that->first_use_ == nullptr || that->first_use_->prev == nullptr);

  // Update the pointers to {this} to point to {that}.
  Use* last_use = nullptr;
  for (Use* use = this->first_use_; use; use = use->next) {
    *use->input_ptr() = that;
    last_use = use;
  }
  if (last_use) {
    // Concat the use list of {this} and {that}.
    last_use->next = that->first_use_;
    if (that->first_use_) that->first_use_->prev = last_use;
    that->first_use_ = this->first_use_;
  }
  first_use_ = nullptr;
}

bool Node::OwnedBy(Node const* owner) const {
  for (Use* use = first_use_; use; use = use->next) {
    if (use->from() != owner) {
      return false;
    }
  }
  return first_use_ != nullptr;
}

bool Node::OwnedBy(Node const* owner1, Node const* owner2) const {
  unsigned mask = 0;
  for (Use* use = first_use_; use; use = use->next) {
    Node* from = use->from();
    if (from == owner1) {
      mask |= 1;
    } else if (from == owner2) {
      mask |= 2;
    } else {
      return false;
    }
  }
  return mask == 3;
}

void Node::Print(int depth) const {
  StdoutStream os;
  Print(os, depth);
}

namespace {
void PrintNode(const Node* node, std::ostream& os, int depth,
               int indentation = 0) {
  for (int i = 0; i < indentation; ++i) {
    os << "  ";
  }
  if (node) {
    os << *node << std::endl;
  } else {
    os << "(NULL)" << std::endl;
    return;
  }
  if (depth <= 0) return;
  for (Node* input : node->inputs()) {
    PrintNode(input, os, depth - 1, indentation + 1);
  }
}
}  // namespace

void Node::Print(std::ostream& os, int depth) const {
  PrintNode(this, os, depth);
}

std::ostream& operator<<(std::ostream& os, const Node& n) {
  os << n.id() << ": " << *n.op();
  if (n.InputCount() > 0) {
    os << "(";
    for (int i = 0; i < n.InputCount(); ++i) {
      if (i != 0) os << ", ";
      if (n.InputAt(i)) {
        os << n.InputAt(i)->id();
      } else {
        os << "null";
      }
    }
    os << ")";
  }
  return os;
}

Node::Node(NodeId id, const Operator* op, int inline_count, int inline_capacity)
    : op_(op),
      mark_(0),
      bit_field_(IdField::encode(id) | InlineCountField::encode(inline_count) |
                 InlineCapacityField::encode(inline_capacity)),
      first_use_(nullptr) {
  // Check that the id didn't overflow.
  static_assert(IdField::kMax < std::numeric_limits<NodeId>::max());
  CHECK(IdField::is_valid(id));

  // Inputs must either be out of line or within the inline capacity.
  DCHECK(inline_count == kOutlineMarker || inline_count <= inline_capacity);
  DCHECK_LE(inline_capacity, kMaxInlineCapacity);
}

void Node::AppendUse(Use* use) {
  DCHECK(first_use_ == nullptr || first_use_->prev == nullptr);
  DCHECK_EQ(this, *use->input_ptr());
  use->next = first_use_;
  use->prev = nullptr;
  if (first_use_) first_use_->prev = use;
  first_use_ = use;
}


void Node::RemoveUse(Use* use) {
  DCHECK(first_use_ == nullptr || first_use_->prev == nullptr);
  if (use->prev) {
    DCHECK_NE(first_use_, use);
    use->prev->next = use->next;
  } else {
    DCHECK_EQ(first_use_, use);
    first_use_ = use->next;
  }
  if (use->next) {
    use->next->prev = use->prev;
  }
}


#if DEBUG
void Node::Verify() {
  // Check basic validity of input data structures.
  fflush(stdout);
  int count = this->InputCount();
  // Avoid quadratic explosion for mega nodes; only verify if the input
  // count is less than 200 or is a round number of 100s.
  if (count > 200 && count % 100) return;

  for (int i = 0; i < count; i++) {
    DCHECK_EQ(i, this->GetUsePtr(i)->input_index());
    DCHECK_EQ(this->GetInputPtr(i), this->GetUsePtr(i)->input_ptr());
    DCHECK_EQ(count, this->InputCount());
  }
  {  // Direct input iteration.
    int index = 0;
    for (Node* input : this->inputs()) {
      DCHECK_EQ(this->InputAt(index), input);
      index++;
    }
    DCHECK_EQ(count, index);
    DCHECK_EQ(this->InputCount(), index);
  }
  {  // Input edge iteration.
    int index = 0;
    for (Edge edge : this->input_edges()) {
      DCHECK_EQ(edge.from(), this);
      DCHECK_EQ(index, edge.index());
      DCHECK_EQ(this->InputAt(index), edge.to());
      index++;
    }
    DCHECK_EQ(count, index);
    DCHECK_EQ(this->InputCount(), index);
  }
}
#endif

Node::InputEdges::iterator Node::InputEdges::iterator::operator++(int n) {
  iterator result(*this);
  ++(*this);
  return result;
}


Node::Inputs::const_iterator Node::Inputs::const_iterator::operator++(int n) {
  const_iterator result(*this);
  ++(*this);
  return result;
}


Node::UseEdges::iterator Node::UseEdges::iterator::operator++(int n) {
  iterator result(*this);
  ++(*this);
  return result;
}


bool Node::UseEdges::empty() const { return begin() == end(); }


Node::Uses::const_iterator Node::Uses::const_iterator::operator++(int n) {
  const_iterator result(*this);
  ++(*this);
  return result;
}


bool Node::Uses::empty() const { return begin() == end(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Node_Print(void* object) {
  reinterpret_cast<i::compiler::Node*>(object)->Print();
}
