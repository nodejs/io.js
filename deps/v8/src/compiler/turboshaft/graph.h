// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_GRAPH_H_
#define V8_COMPILER_TURBOSHAFT_GRAPH_H_

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/codegen/source-position.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/types.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

template <class Reducers>
class Assembler;

class LoopUnrollingAnalyzer;

// `OperationBuffer` is a growable, Zone-allocated buffer to store Turboshaft
// operations. It is part of a `Graph`.
// The buffer can be seen as an array of 8-byte `OperationStorageSlot` values.
// The structure is append-only, that is, we only add operations at the end.
// There are rare cases (i.e., loop phis) where we overwrite an existing
// operation, but only if we can guarantee that the new operation is not bigger
// than the operation we overwrite.
class OperationBuffer {
 public:
  // A `ReplaceScope` is to overwrite an existing operation.
  // It moves the end-pointer temporarily so that the next emitted operation
  // overwrites an old one.
  class ReplaceScope {
   public:
    ReplaceScope(OperationBuffer* buffer, OpIndex replaced)
        : buffer_(buffer),
          replaced_(replaced),
          old_end_(buffer->end_),
          old_slot_count_(buffer->SlotCount(replaced)) {
      buffer_->end_ = buffer_->Get(replaced);
    }
    ~ReplaceScope() {
      DCHECK_LE(buffer_->SlotCount(replaced_), old_slot_count_);
      buffer_->end_ = old_end_;
      // Preserve the original operation size in case it has become smaller.
      buffer_->operation_sizes_[replaced_.id()] = old_slot_count_;
      buffer_->operation_sizes_[OpIndex(replaced_.offset() +
                                        static_cast<uint32_t>(old_slot_count_) *
                                            sizeof(OperationStorageSlot))
                                    .id() -
                                1] = old_slot_count_;
    }

    ReplaceScope(const ReplaceScope&) = delete;
    ReplaceScope& operator=(const ReplaceScope&) = delete;

   private:
    OperationBuffer* buffer_;
    OpIndex replaced_;
    OperationStorageSlot* old_end_;
    uint16_t old_slot_count_;
  };

  explicit OperationBuffer(Zone* zone, size_t initial_capacity) : zone_(zone) {
    DCHECK_NE(initial_capacity, 0);
    begin_ = end_ =
        zone_->AllocateArray<OperationStorageSlot>(initial_capacity);
    operation_sizes_ =
        zone_->AllocateArray<uint16_t>((initial_capacity + 1) / kSlotsPerId);
    end_cap_ = begin_ + initial_capacity;
  }

  OperationStorageSlot* Allocate(size_t slot_count) {
    if (V8_UNLIKELY(static_cast<size_t>(end_cap_ - end_) < slot_count)) {
      Grow(capacity() + slot_count);
      DCHECK(slot_count <= static_cast<size_t>(end_cap_ - end_));
    }
    OperationStorageSlot* result = end_;
    end_ += slot_count;
    OpIndex idx = Index(result);
    // Store the size in both for the first and last id corresponding to the new
    // operation. This enables iteration in both directions. The two id's are
    // the same if the operation is small.
    operation_sizes_[idx.id()] = slot_count;
    operation_sizes_[OpIndex(idx.offset() + static_cast<uint32_t>(slot_count) *
                                                sizeof(OperationStorageSlot))
                         .id() -
                     1] = slot_count;
    return result;
  }

  void RemoveLast() {
    size_t slot_count = operation_sizes_[EndIndex().id() - 1];
    end_ -= slot_count;
    DCHECK_GE(end_, begin_);
  }

  OpIndex Index(const Operation& op) const {
    return Index(reinterpret_cast<const OperationStorageSlot*>(&op));
  }
  OpIndex Index(const OperationStorageSlot* ptr) const {
    DCHECK(begin_ <= ptr && ptr <= end_);
    return OpIndex(static_cast<uint32_t>(reinterpret_cast<Address>(ptr) -
                                         reinterpret_cast<Address>(begin_)));
  }

  OperationStorageSlot* Get(OpIndex idx) {
    DCHECK_LT(idx.offset() / sizeof(OperationStorageSlot), size());
    return reinterpret_cast<OperationStorageSlot*>(
        reinterpret_cast<Address>(begin_) + idx.offset());
  }
  uint16_t SlotCount(OpIndex idx) {
    DCHECK_LT(idx.offset() / sizeof(OperationStorageSlot), size());
    return operation_sizes_[idx.id()];
  }

  const OperationStorageSlot* Get(OpIndex idx) const {
    DCHECK_LT(idx.offset(), capacity() * sizeof(OperationStorageSlot));
    return reinterpret_cast<const OperationStorageSlot*>(
        reinterpret_cast<Address>(begin_) + idx.offset());
  }

  OpIndex Next(OpIndex idx) const {
    DCHECK_GT(operation_sizes_[idx.id()], 0);
    OpIndex result = OpIndex(idx.offset() + operation_sizes_[idx.id()] *
                                                sizeof(OperationStorageSlot));
    DCHECK_LT(0, result.offset());
    DCHECK_LE(result.offset(), capacity() * sizeof(OperationStorageSlot));
    return result;
  }
  OpIndex Previous(OpIndex idx) const {
    DCHECK_GT(idx.id(), 0);
    DCHECK_GT(operation_sizes_[idx.id() - 1], 0);
    OpIndex result = OpIndex(idx.offset() - operation_sizes_[idx.id() - 1] *
                                                sizeof(OperationStorageSlot));
    DCHECK_LE(0, result.offset());
    DCHECK_LT(result.offset(), capacity() * sizeof(OperationStorageSlot));
    return result;
  }

  // Offset of the first operation.
  OpIndex BeginIndex() const { return OpIndex(0); }
  // One-past-the-end offset.
  OpIndex EndIndex() const { return Index(end_); }

  uint32_t size() const { return static_cast<uint32_t>(end_ - begin_); }
  uint32_t capacity() const { return static_cast<uint32_t>(end_cap_ - begin_); }

  void Grow(size_t min_capacity) {
    size_t size = this->size();
    size_t capacity = this->capacity();
    size_t new_capacity = 2 * capacity;
    while (new_capacity < min_capacity) new_capacity *= 2;
    CHECK_LT(new_capacity, std::numeric_limits<uint32_t>::max() /
                               sizeof(OperationStorageSlot));

    OperationStorageSlot* new_buffer =
        zone_->AllocateArray<OperationStorageSlot>(new_capacity);
    memcpy(new_buffer, begin_, size * sizeof(OperationStorageSlot));

    uint16_t* new_operation_sizes =
        zone_->AllocateArray<uint16_t>(new_capacity / kSlotsPerId);
    memcpy(new_operation_sizes, operation_sizes_,
           size / kSlotsPerId * sizeof(uint16_t));

    begin_ = new_buffer;
    end_ = new_buffer + size;
    end_cap_ = new_buffer + new_capacity;
    operation_sizes_ = new_operation_sizes;
  }

  void Reset() { end_ = begin_; }

 private:
  Zone* zone_;
  OperationStorageSlot* begin_;
  OperationStorageSlot* end_;
  OperationStorageSlot* end_cap_;
  uint16_t* operation_sizes_;
};

template <class Derived>
class DominatorForwardTreeNode;
template <class Derived>
class RandomAccessStackDominatorNode;

template <class Derived>
class DominatorForwardTreeNode {
  // A class storing a forward representation of the dominator tree, since the
  // regular dominator tree is represented as pointers from the children to
  // parents rather than parents to children.
 public:
  void AddChild(Derived* next) {
    DCHECK_EQ(static_cast<Derived*>(this)->len_ + 1, next->len_);
    next->neighboring_child_ = last_child_;
    last_child_ = next;
  }

  Derived* LastChild() const { return last_child_; }
  Derived* NeighboringChild() const { return neighboring_child_; }
  bool HasChildren() const { return last_child_ != nullptr; }

  base::SmallVector<Derived*, 8> Children() const {
    base::SmallVector<Derived*, 8> result;
    for (Derived* child = last_child_; child != nullptr;
         child = child->neighboring_child_) {
      result.push_back(child);
    }
    std::reverse(result.begin(), result.end());
    return result;
  }

 private:
#ifdef DEBUG
  friend class RandomAccessStackDominatorNode<Derived>;
#endif
  Derived* neighboring_child_ = nullptr;
  Derived* last_child_ = nullptr;
};

template <class Derived>
class RandomAccessStackDominatorNode
    : public DominatorForwardTreeNode<Derived> {
  // This class represents a node of a dominator tree implemented using Myers'
  // Random-Access Stack (see
  // https://publications.mpi-cbg.de/Myers_1983_6328.pdf). This datastructure
  // enables searching for a predecessor of a node in log(h) time, where h is
  // the height of the dominator tree.
 public:
  void SetDominator(Derived* dominator);
  void SetAsDominatorRoot();
  Derived* GetDominator() const { return nxt_; }

  // Returns the lowest common dominator of {this} and {other}.
  Derived* GetCommonDominator(
      RandomAccessStackDominatorNode<Derived>* other) const;

  bool IsDominatedBy(const Derived* other) const {
    // TODO(dmercadier): we don't have to call GetCommonDominator and could
    // determine quicker that {this} isn't dominated by {other}.
    return GetCommonDominator(other) == other;
  }

  int Depth() const { return len_; }

 private:
  friend class DominatorForwardTreeNode<Derived>;
#ifdef DEBUG
  friend class Block;
#endif

  // Myers' original datastructure requires to often check jmp_->len_, which is
  // not so great on modern computers (memory access, caches & co). To speed up
  // things a bit, we store here jmp_len_.
  int jmp_len_ = 0;

  int len_ = 0;
  Derived* nxt_ = nullptr;
  Derived* jmp_ = nullptr;
};

// A simple iterator to walk over the predecessors of a block. Note that the
// iteration order is reversed.
class PredecessorIterator {
 public:
  explicit PredecessorIterator(const Block* block) : current_(block) {}

  PredecessorIterator& operator++();
  constexpr bool operator==(const PredecessorIterator& other) const {
    return current_ == other.current_;
  }
  constexpr bool operator!=(const PredecessorIterator& other) const {
    return !(*this == other);
  }

  const Block* operator*() const { return current_; }

 private:
  const Block* current_;
};

// An iterable wrapper for the predecessors of a block.
class NeighboringPredecessorIterable {
 public:
  explicit NeighboringPredecessorIterable(const Block* begin) : begin_(begin) {}

  PredecessorIterator begin() const { return PredecessorIterator(begin_); }
  PredecessorIterator end() const { return PredecessorIterator(nullptr); }

 private:
  const Block* begin_;
};

// A basic block
class Block : public RandomAccessStackDominatorNode<Block> {
 public:
  enum class Kind : uint8_t { kMerge, kLoopHeader, kBranchTarget };

  explicit Block(Kind kind) : kind_(kind) {}

  bool IsLoopOrMerge() const { return IsLoop() || IsMerge(); }
  bool IsLoop() const { return kind_ == Kind::kLoopHeader; }
  bool IsMerge() const { return kind_ == Kind::kMerge; }
  bool IsBranchTarget() const { return kind_ == Kind::kBranchTarget; }

  Kind kind() const { return kind_; }
  void SetKind(Kind kind) { kind_ = kind; }

  BlockIndex index() const { return index_; }

  bool Contains(OpIndex op_idx) const {
    return begin_ <= op_idx && op_idx < end_;
  }

  bool IsBound() const { return index_ != BlockIndex::Invalid(); }

  base::SmallVector<Block*, 8> Predecessors() const {
    base::SmallVector<Block*, 8> result;
    for (Block* pred = last_predecessor_; pred != nullptr;
         pred = pred->neighboring_predecessor_) {
      result.push_back(pred);
    }
    std::reverse(result.begin(), result.end());
    return result;
  }

  // Returns an iterable object (defining begin() and end()) to iterate over the
  // block's predecessors.
  NeighboringPredecessorIterable PredecessorsIterable() const {
    return NeighboringPredecessorIterable(last_predecessor_);
  }

  int PredecessorCount() const {
#ifdef DEBUG
    CheckPredecessorCount();
#endif
    return predecessor_count_;
  }

#ifdef DEBUG
  // Checks that the {predecessor_count_} is equal to the number of predecessors
  // reachable through {last_predecessor_}.
  void CheckPredecessorCount() const {
    int count = 0;
    for (Block* pred = last_predecessor_; pred != nullptr;
         pred = pred->neighboring_predecessor_) {
      count++;
    }
    DCHECK_EQ(count, predecessor_count_);
  }
#endif

  static constexpr int kInvalidPredecessorIndex = -1;

  // Returns the index of {target} in the predecessors of the current Block.
  // If {target} is not a direct predecessor, returns -1.
  int GetPredecessorIndex(const Block* target) const {
    int pred_count = 0;
    int pred_reverse_index = -1;
    for (Block* pred = last_predecessor_; pred != nullptr;
         pred = pred->neighboring_predecessor_) {
      if (pred == target) {
        DCHECK_EQ(pred_reverse_index, -1);
        pred_reverse_index = pred_count;
      }
      pred_count++;
    }
    if (pred_reverse_index == -1) {
      return kInvalidPredecessorIndex;
    }
    return pred_count - pred_reverse_index - 1;
  }

  Block* LastPredecessor() const { return last_predecessor_; }
  Block* NeighboringPredecessor() const { return neighboring_predecessor_; }
  bool HasPredecessors() const {
    DCHECK_EQ(predecessor_count_ == 0, last_predecessor_ == nullptr);
    return last_predecessor_ != nullptr;
  }
  void ResetLastPredecessor() {
    last_predecessor_ = nullptr;
    predecessor_count_ = 0;
  }
  void ResetAllPredecessors() {
    Block* pred = last_predecessor_;
    last_predecessor_ = nullptr;
    while (pred->neighboring_predecessor_) {
      Block* tmp = pred->neighboring_predecessor_;
      pred->neighboring_predecessor_ = nullptr;
      pred = tmp;
    }
    predecessor_count_ = 0;
  }

  Block* single_loop_predecessor() const {
    DCHECK(IsLoop());
    return single_loop_predecessor_;
  }
  void SetSingleLoopPredecessor(Block* single_loop_predecessor) {
    DCHECK(IsLoop());
    DCHECK_NULL(single_loop_predecessor_);
    DCHECK_NOT_NULL(single_loop_predecessor);
    single_loop_predecessor_ = single_loop_predecessor;
  }

  // The block from the previous graph which produced the current block. This
  // has to be updated to be the last block that contributed operations to the
  // current block to ensure that phi nodes are created correctly.
  void SetOrigin(const Block* origin) {
    DCHECK_IMPLIES(origin != nullptr,
                   origin->graph_generation_ + 1 == graph_generation_);
    origin_ = origin;
  }
  // The block from the input graph that is equivalent as a predecessor. It is
  // only available for bound blocks and it does *not* refer to an equivalent
  // block as a branch destination.
  const Block* OriginForBlockEnd() const {
    DCHECK(IsBound());
    return origin_;
  }
  const Block* OriginForLoopHeader() const {
    DCHECK(IsLoop());
    return origin_;
  }

  bool IsComplete() const { return end_.valid(); }
  OpIndex begin() const {
    DCHECK(begin_.valid());
    return begin_;
  }
  OpIndex end() const {
    DCHECK(end_.valid());
    return end_;
  }

  // Returns an approximation of the number of operations contained in this
  // block, by counting how many slots it contains. Depending on the size of the
  // operations it contains, this could be exactly how many operations it
  // contains, or it could be less.
  int OpCountUpperBound() const { return end().id() - begin().id(); }

  const Operation& FirstOperation(const Graph& graph) const;
  const Operation& LastOperation(const Graph& graph) const;
  Operation& LastOperation(Graph& graph) const;

  bool EndsWithBranchingOp(const Graph& graph) const {
    switch (LastOperation(graph).opcode) {
      case Opcode::kBranch:
      case Opcode::kSwitch:
      case Opcode::kCheckException:
        return true;
      default:
        DCHECK_LE(SuccessorBlocks(*this, graph).size(), 1);
        return false;
    }
  }

  bool HasPhis(const Graph& graph) const;

  bool HasBackedge(const Graph& graph) const {
    if (const GotoOp* gto = LastOperation(graph).TryCast<GotoOp>()) {
      return gto->destination->index().id() <= index().id();
    }
    return false;
  }

#ifdef DEBUG
  // {has_peeled_iteration_} is currently only updated for loops peeled in
  // Turboshaft (it is true only for loop headers of loops that have had their
  // first iteration peeled). So be aware that while Turbofan loop peeling is
  // enabled, this is not a reliable way to check if a loop has a peeled
  // iteration.
  bool has_peeled_iteration() const {
    DCHECK(IsLoop());
    return has_peeled_iteration_;
  }
  void set_has_peeled_iteration() {
    DCHECK(IsLoop());
    has_peeled_iteration_ = true;
  }
#endif

  // Computes the dominators of the this block, assuming that the dominators of
  // its predecessors are already computed. Returns the depth of the current
  // block in the dominator tree.
  uint32_t ComputeDominator();

  void PrintDominatorTree(
      std::vector<const char*> tree_symbols = std::vector<const char*>(),
      bool has_next = false) const;

  enum class CustomDataKind {
    kUnset,  // No custom data has been set for this block.
    kPhiInputIndex,
    kDeferredInSchedule,
  };

  void set_custom_data(uint32_t data, CustomDataKind kind_for_debug_check) {
    custom_data_ = data;
#ifdef DEBUG
    custom_data_kind_for_debug_check_ = kind_for_debug_check;
#endif
  }

  uint32_t get_custom_data(CustomDataKind kind_for_debug_check) const {
    DCHECK_EQ(custom_data_kind_for_debug_check_, kind_for_debug_check);
    return custom_data_;
  }

  void clear_custom_data() {
    custom_data_ = 0;
#ifdef DEBUG
    custom_data_kind_for_debug_check_ = CustomDataKind::kUnset;
#endif
  }

 private:
  // AddPredecessor should never be called directly except from Assembler's
  // AddPredecessor and SplitEdge methods, which takes care of maintaining
  // split-edge form.
  void AddPredecessor(Block* predecessor) {
    DCHECK(!IsBound() ||
           (Predecessors().size() == 1 && kind_ == Kind::kLoopHeader));
    DCHECK_EQ(predecessor->neighboring_predecessor_, nullptr);
    predecessor->neighboring_predecessor_ = last_predecessor_;
    last_predecessor_ = predecessor;
    predecessor_count_++;
  }


  Kind kind_;
  OpIndex begin_ = OpIndex::Invalid();
  OpIndex end_ = OpIndex::Invalid();
  BlockIndex index_ = BlockIndex::Invalid();
  Block* last_predecessor_ = nullptr;
  Block* neighboring_predecessor_ = nullptr;
  Block* single_loop_predecessor_ = nullptr;
  uint32_t predecessor_count_ = 0;
  const Block* origin_ = nullptr;
  // The {custom_data_} field can be used by algorithms to temporarily store
  // block-specific data. This field is not preserved when constructing a new
  // output graph and algorithms cannot rely on this field being properly reset
  // after previous uses.
  uint32_t custom_data_ = 0;
#ifdef DEBUG
  CustomDataKind custom_data_kind_for_debug_check_ = CustomDataKind::kUnset;
  size_t graph_generation_ = 0;
  // True if this is a loop header of a loop with a peeled iteration.
  bool has_peeled_iteration_ = false;
#endif

  friend class Graph;
  template <class Reducers>
  friend class Assembler;
  template <class Assembler>
  friend class GraphVisitor;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, const Block* b);

inline PredecessorIterator& PredecessorIterator::operator++() {
  DCHECK_NE(current_, nullptr);
  current_ = current_->NeighboringPredecessor();
  return *this;
}

class Graph {
 public:
  // A big initial capacity prevents many growing steps. It also makes sense
  // because the graph and its memory is recycled for following phases.
  explicit Graph(Zone* graph_zone, size_t initial_capacity = 2048)
      : operations_(graph_zone, initial_capacity),
        bound_blocks_(graph_zone),
        all_blocks_(),
        op_to_block_(graph_zone, this),
        block_permutation_(graph_zone),
        graph_zone_(graph_zone),
        source_positions_(graph_zone, this),
        operation_origins_(graph_zone, this),
        operation_types_(graph_zone, this),
#ifdef DEBUG
        block_type_refinement_(graph_zone),
#endif
        stack_checks_to_remove_(graph_zone) {
  }

  // Reset the graph to recycle its memory.
  void Reset() {
    operations_.Reset();
    bound_blocks_.clear();
    // No need to explicitly reset `all_blocks_`, since we will placement-new
    // new blocks into it, reusing the already allocated backing storage.
    next_block_ = 0;
    op_to_block_.Reset();
    block_permutation_.clear();
    source_positions_.Reset();
    operation_origins_.Reset();
    operation_types_.Reset();
    dominator_tree_depth_ = 0;
#ifdef DEBUG
    block_type_refinement_.Reset();
    // Do not reset of graph_created_from_turbofan_ as it is propagated along
    // the phases.
#endif
  }

  V8_INLINE const Operation& Get(OpIndex i) const {
    DCHECK(i.valid());
    DCHECK(BelongsToThisGraph(i));
    // `Operation` contains const fields and can be overwritten with placement
    // new. Therefore, std::launder is necessary to avoid undefined behavior.
    const Operation* ptr =
        std::launder(reinterpret_cast<const Operation*>(operations_.Get(i)));
    // Detect invalid memory by checking if opcode is valid.
    DCHECK_LT(OpcodeIndex(ptr->opcode), kNumberOfOpcodes);
    return *ptr;
  }
  V8_INLINE Operation& Get(OpIndex i) {
    DCHECK(i.valid());
    DCHECK(BelongsToThisGraph(i));
    // `Operation` contains const fields and can be overwritten with placement
    // new. Therefore, std::launder is necessary to avoid undefined behavior.
    Operation* ptr =
        std::launder(reinterpret_cast<Operation*>(operations_.Get(i)));
    // Detect invalid memory by checking if opcode is valid.
    DCHECK_LT(OpcodeIndex(ptr->opcode), kNumberOfOpcodes);
    return *ptr;
  }

  void KillOperation(OpIndex i) { Replace<DeadOp>(i); }

  Block& StartBlock() { return Get(BlockIndex(0)); }
  const Block& StartBlock() const { return Get(BlockIndex(0)); }

  Block& Get(BlockIndex i) {
    DCHECK_LT(i.id(), bound_blocks_.size());
    return *bound_blocks_[i.id()];
  }
  const Block& Get(BlockIndex i) const {
    DCHECK_LT(i.id(), bound_blocks_.size());
    return *bound_blocks_[i.id()];
  }

  OpIndex Index(const Operation& op) const {
    OpIndex result = operations_.Index(op);
#ifdef DEBUG
    result.set_generation_mod2(generation_mod2());
#endif
    return result;
  }
  BlockIndex BlockOf(OpIndex index) const {
    ZoneVector<Block*>::const_iterator it;
    if (block_permutation_.empty()) {
      it = std::upper_bound(
          bound_blocks_.begin(), bound_blocks_.end(), index,
          [](OpIndex value, const Block* b) { return value < b->begin_; });
      DCHECK_NE(it, bound_blocks_.begin());
    } else {
      it = std::upper_bound(
          block_permutation_.begin(), block_permutation_.end(), index,
          [](OpIndex value, const Block* b) { return value < b->begin_; });
      DCHECK_NE(it, block_permutation_.begin());
    }
    it = std::prev(it);
    DCHECK((*it)->Contains(index));
    return (*it)->index();
  }

  void SetBlockOf(BlockIndex block, OpIndex op) { op_to_block_[op] = block; }

  BlockIndex BlockIndexOf(OpIndex op) const { return op_to_block_[op]; }

  BlockIndex BlockIndexOf(const Operation& op) const {
    return op_to_block_[Index(op)];
  }

  OpIndex NextIndex(const OpIndex idx) const {
    OpIndex next = operations_.Next(idx);
#ifdef DEBUG
    next.set_generation_mod2(generation_mod2());
#endif
    return next;
  }
  OpIndex PreviousIndex(const OpIndex idx) const {
    OpIndex prev = operations_.Previous(idx);
#ifdef DEBUG
    prev.set_generation_mod2(generation_mod2());
#endif
    return prev;
  }
  OpIndex LastOperation() const {
    return PreviousIndex(next_operation_index());
  }

  OperationStorageSlot* Allocate(size_t slot_count) {
    return operations_.Allocate(slot_count);
  }

  void RemoveLast() {
    DecrementInputUses(*AllOperations().rbegin());
    operations_.RemoveLast();
#ifdef DEBUG
    if (v8_flags.turboshaft_trace_emitted) {
      std::cout << "/!\\ Removed last emitted operation /!\\\n";
    }
#endif
  }

  template <class Op, class... Args>
  V8_INLINE Op& Add(Args... args) {
#ifdef DEBUG
    OpIndex result = next_operation_index();
#endif  // DEBUG
    Op& op = Op::New(this, args...);
    IncrementInputUses(op);

    DCHECK_EQ(result, Index(op));
#ifdef DEBUG
    for (OpIndex input : op.inputs()) {
      DCHECK_LT(input, result);
      DCHECK(BelongsToThisGraph(input));
    }

    if (v8_flags.turboshaft_trace_emitted) {
      std::cout << "Emitted: " << result << " => " << op << "\n";
    }

#endif  // DEBUG

    return op;
  }

  template <class Op, class... Args>
  void Replace(OpIndex replaced, Args... args) {
    static_assert((std::is_base_of<Operation, Op>::value));
    static_assert(std::is_trivially_destructible<Op>::value);

    const Operation& old_op = Get(replaced);
    DecrementInputUses(old_op);
    auto old_uses = old_op.saturated_use_count;
    Op* new_op;
    {
      OperationBuffer::ReplaceScope replace_scope(&operations_, replaced);
      new_op = &Op::New(this, args...);
    }
    if (!std::is_same_v<Op, DeadOp>) {
      new_op->saturated_use_count = old_uses;
    }
    IncrementInputUses(*new_op);
  }

  V8_INLINE Block* NewLoopHeader(const Block* origin = nullptr) {
    return NewBlock(Block::Kind::kLoopHeader, origin);
  }
  V8_INLINE Block* NewBlock(const Block* origin = nullptr) {
    return NewBlock(Block::Kind::kMerge, origin);
  }

  V8_INLINE Block* NewBlock(Block::Kind kind, const Block* origin = nullptr) {
    if (V8_UNLIKELY(next_block_ == all_blocks_.size())) {
      AllocateNewBlocks();
    }
    Block* result = all_blocks_[next_block_++];
    new (result) Block(kind);
#ifdef DEBUG
    result->graph_generation_ = generation_;
#endif
    result->SetOrigin(origin);
    return result;
  }

  V8_INLINE bool Add(Block* block) {
    DCHECK_EQ(block->graph_generation_, generation_);
    if (!bound_blocks_.empty() && !block->HasPredecessors()) return false;

    DCHECK(!block->begin_.valid());
    block->begin_ = next_operation_index();
    DCHECK_EQ(block->index_, BlockIndex::Invalid());
    block->index_ = next_block_index();
    bound_blocks_.push_back(block);
    uint32_t depth = block->ComputeDominator();
    dominator_tree_depth_ = std::max<uint32_t>(dominator_tree_depth_, depth);

#ifdef DEBUG
    if (v8_flags.turboshaft_trace_emitted) {
      std::cout << "\nBound: " << block->index() << " [predecessors: ";
      auto preds = block->Predecessors();
      if (preds.size() >= 1) std::cout << preds[0]->index();
      for (size_t i = 1; i < preds.size(); i++) {
        std::cout << ", " << preds[i]->index();
      }
      std::cout << "]\n";
    }
#endif

    return true;
  }

  void Finalize(Block* block) {
    DCHECK(!block->end_.valid());
    block->end_ = next_operation_index();
    // Upading mapping from Operations to Blocks for the Operations in {block}.
    for (const Operation& op : operations(*block)) {
      SetBlockOf(block->index(), Index(op));
    }
  }

  void TurnLoopIntoMerge(Block* loop) {
    DCHECK(loop->IsLoop());
    DCHECK_EQ(loop->PredecessorCount(), 1);
    loop->kind_ = Block::Kind::kMerge;
    for (Operation& op : operations(*loop)) {
      if (auto* pending_phi = op.TryCast<PendingLoopPhiOp>()) {
        Replace<PhiOp>(Index(*pending_phi),
                       base::VectorOf({pending_phi->first()}),
                       pending_phi->rep);
      }
    }
  }

  OpIndex next_operation_index() const { return EndIndex(); }
  BlockIndex next_block_index() const {
    return BlockIndex(static_cast<uint32_t>(bound_blocks_.size()));
  }

  Block* last_block() { return bound_blocks_.back(); }

  Zone* graph_zone() const { return graph_zone_; }
  uint32_t block_count() const {
    return static_cast<uint32_t>(bound_blocks_.size());
  }
  uint32_t op_id_count() const {
    return (operations_.size() + (kSlotsPerId - 1)) / kSlotsPerId;
  }
  uint32_t NumberOfOperationsForDebugging() const {
    uint32_t number_of_operations = 0;
    for ([[maybe_unused]] auto& op : AllOperations()) {
      ++number_of_operations;
    }
    return number_of_operations;
  }
  uint32_t op_id_capacity() const {
    return operations_.capacity() / kSlotsPerId;
  }

  OpIndex BeginIndex() const {
    OpIndex begin = operations_.BeginIndex();
#ifdef DEBUG
    begin.set_generation_mod2(generation_mod2());
#endif
    return begin;
  }
  OpIndex EndIndex() const {
    OpIndex end = operations_.EndIndex();
#ifdef DEBUG
    end.set_generation_mod2(generation_mod2());
#endif
    return end;
  }

  class OpIndexIterator
      : public base::iterator<std::bidirectional_iterator_tag, OpIndex,
                              std::ptrdiff_t, OpIndex*, OpIndex> {
   public:
    using value_type = OpIndex;

    explicit OpIndexIterator(OpIndex index, const Graph* graph)
        : index_(index), graph_(graph) {}
    value_type operator*() const { return index_; }
    OpIndexIterator& operator++() {
      index_ = graph_->NextIndex(index_);
      return *this;
    }
    OpIndexIterator& operator--() {
      index_ = graph_->PreviousIndex(index_);
      return *this;
    }
    bool operator!=(OpIndexIterator other) const {
      DCHECK_EQ(graph_, other.graph_);
      return index_ != other.index_;
    }
    bool operator==(OpIndexIterator other) const { return !(*this != other); }

   private:
    OpIndex index_;
    const Graph* const graph_;
  };

  template <class OperationT, typename GraphT>
  class OperationIterator
      : public base::iterator<std::bidirectional_iterator_tag, OperationT> {
   public:
    static_assert(std::is_same_v<std::remove_const_t<OperationT>, Operation> &&
                  std::is_same_v<std::remove_const_t<GraphT>, Graph>);
    using value_type = OperationT;

    explicit OperationIterator(OpIndex index, GraphT* graph)
        : index_(index), graph_(graph) {}
    value_type& operator*() { return graph_->Get(index_); }
    value_type* operator->() { return &graph_->Get(index_); }
    OperationIterator& operator++() {
      DCHECK_NE(index_, graph_->EndIndex());
      index_ = graph_->NextIndex(index_);
      return *this;
    }
    OperationIterator& operator--() {
      DCHECK_NE(index_, graph_->BeginIndex());
      index_ = graph_->PreviousIndex(index_);
      return *this;
    }
    bool operator!=(OperationIterator other) const {
      DCHECK_EQ(graph_, other.graph_);
      return index_ != other.index_;
    }
    bool operator==(OperationIterator other) const { return !(*this != other); }

   private:
    OpIndex index_;
    GraphT* const graph_;
  };

  using MutableOperationIterator = OperationIterator<Operation, Graph>;
  using ConstOperationIterator =
      OperationIterator<const Operation, const Graph>;

  base::iterator_range<MutableOperationIterator> AllOperations() {
    return operations(BeginIndex(), EndIndex());
  }
  base::iterator_range<ConstOperationIterator> AllOperations() const {
    return operations(BeginIndex(), EndIndex());
  }

  base::iterator_range<OpIndexIterator> AllOperationIndices() const {
    return OperationIndices(BeginIndex(), EndIndex());
  }

  base::iterator_range<MutableOperationIterator> operations(
      const Block& block) {
    return operations(block.begin_, block.end_);
  }
  base::iterator_range<ConstOperationIterator> operations(
      const Block& block) const {
    return operations(block.begin_, block.end_);
  }

  base::iterator_range<OpIndexIterator> OperationIndices(
      const Block& block) const {
    return OperationIndices(block.begin_, block.end_);
  }

  base::iterator_range<ConstOperationIterator> operations(OpIndex begin,
                                                          OpIndex end) const {
    DCHECK(begin.valid());
    DCHECK(end.valid());
    return {ConstOperationIterator(begin, this),
            ConstOperationIterator(end, this)};
  }
  base::iterator_range<MutableOperationIterator> operations(OpIndex begin,
                                                            OpIndex end) {
    DCHECK(begin.valid());
    DCHECK(end.valid());
    return {MutableOperationIterator(begin, this),
            MutableOperationIterator(end, this)};
  }

  base::iterator_range<OpIndexIterator> OperationIndices(OpIndex begin,
                                                         OpIndex end) const {
    DCHECK(begin.valid());
    DCHECK(end.valid());
    return {OpIndexIterator(begin, this), OpIndexIterator(end, this)};
  }

  base::iterator_range<base::DerefPtrIterator<Block>> blocks() {
    return {base::DerefPtrIterator<Block>(bound_blocks_.data()),
            base::DerefPtrIterator<Block>(bound_blocks_.data() +
                                          bound_blocks_.size())};
  }
  base::iterator_range<base::DerefPtrIterator<const Block>> blocks() const {
    return {base::DerefPtrIterator<const Block>(bound_blocks_.data()),
            base::DerefPtrIterator<const Block>(bound_blocks_.data() +
                                                bound_blocks_.size())};
  }
  const ZoneVector<Block*>& blocks_vector() const { return bound_blocks_; }

  bool IsLoopBackedge(const GotoOp& op) const {
    DCHECK(op.destination->IsBound());
    return op.destination->begin() <= Index(op);
  }

  bool IsValid(OpIndex i) const { return i < next_operation_index(); }

  const GrowingOpIndexSidetable<SourcePosition>& source_positions() const {
    return source_positions_;
  }
  GrowingOpIndexSidetable<SourcePosition>& source_positions() {
    return source_positions_;
  }

  const GrowingOpIndexSidetable<OpIndex>& operation_origins() const {
    return operation_origins_;
  }
  GrowingOpIndexSidetable<OpIndex>& operation_origins() {
    return operation_origins_;
  }

  uint32_t DominatorTreeDepth() const { return dominator_tree_depth_; }

  const GrowingOpIndexSidetable<Type>& operation_types() const {
    return operation_types_;
  }
  GrowingOpIndexSidetable<Type>& operation_types() { return operation_types_; }
#ifdef DEBUG
  // Store refined types per block here for --trace-turbo printing.
  // TODO(nicohartmann@): Remove this once we have a proper way to print
  // type information inside the reducers.
  using TypeRefinements = std::vector<std::pair<OpIndex, Type>>;
  const GrowingBlockSidetable<TypeRefinements>& block_type_refinement() const {
    return block_type_refinement_;
  }
  GrowingBlockSidetable<TypeRefinements>& block_type_refinement() {
    return block_type_refinement_;
  }
#endif  // DEBUG

  void ReorderBlocks(base::Vector<uint32_t> permutation) {
    DCHECK_EQ(permutation.size(), bound_blocks_.size());
    block_permutation_.resize(bound_blocks_.size());
    std::swap(block_permutation_, bound_blocks_);

    for (size_t i = 0; i < permutation.size(); ++i) {
      DCHECK_LE(0, permutation[i]);
      DCHECK_LT(permutation[i], block_permutation_.size());
      bound_blocks_[i] = block_permutation_[permutation[i]];
      bound_blocks_[i]->index_ = BlockIndex(static_cast<uint32_t>(i));
    }
  }

  Graph& GetOrCreateCompanion() {
    if (!companion_) {
      companion_ = graph_zone_->New<Graph>(graph_zone_, operations_.size());
#ifdef DEBUG
      companion_->generation_ = generation_ + 1;
      if (IsCreatedFromTurbofan()) companion_->SetCreatedFromTurbofan();
#endif  // DEBUG
    }
    return *companion_;
  }

  // Swap the graph with its companion graph to turn the output of one phase
  // into the input of the next phase.
  void SwapWithCompanion() {
    Graph& companion = GetOrCreateCompanion();
    std::swap(operations_, companion.operations_);
    std::swap(bound_blocks_, companion.bound_blocks_);
    std::swap(all_blocks_, companion.all_blocks_);
    std::swap(next_block_, companion.next_block_);
    std::swap(block_permutation_, companion.block_permutation_);
    std::swap(graph_zone_, companion.graph_zone_);
    op_to_block_.SwapData(companion.op_to_block_);
    source_positions_.SwapData(companion.source_positions_);
    operation_origins_.SwapData(companion.operation_origins_);
    operation_types_.SwapData(companion.operation_types_);
#ifdef DEBUG
    std::swap(block_type_refinement_, companion.block_type_refinement_);
    // Update generation index.
    DCHECK_EQ(generation_ + 1, companion.generation_);
    generation_ = companion.generation_++;
#endif  // DEBUG
    // Reseting phase-specific fields.
    loop_unrolling_analyzer_ = nullptr;
    stack_checks_to_remove_.clear();
  }

#ifdef DEBUG
  size_t generation() const { return generation_; }
  int generation_mod2() const { return generation_ % 2; }

  bool BelongsToThisGraph(OpIndex idx) const {
    return idx.generation_mod2() == generation_mod2();
  }

  void SetCreatedFromTurbofan() { graph_created_from_turbofan_ = true; }
  bool IsCreatedFromTurbofan() const { return graph_created_from_turbofan_; }
#endif  // DEBUG

  void set_loop_unrolling_analyzer(
      LoopUnrollingAnalyzer* loop_unrolling_analyzer) {
    DCHECK_NULL(loop_unrolling_analyzer_);
    loop_unrolling_analyzer_ = loop_unrolling_analyzer;
  }
  void clear_loop_unrolling_analyzer() { loop_unrolling_analyzer_ = nullptr; }
  LoopUnrollingAnalyzer* loop_unrolling_analyzer() const {
    DCHECK_NOT_NULL(loop_unrolling_analyzer_);
    return loop_unrolling_analyzer_;
  }
#ifdef DEBUG
  bool has_loop_unrolling_analyzer() const {
    return loop_unrolling_analyzer_ != nullptr;
  }
#endif

  void clear_stack_checks_to_remove() { stack_checks_to_remove_.clear(); }
  ZoneAbslFlatHashSet<uint32_t>& stack_checks_to_remove() {
    return stack_checks_to_remove_;
  }
  const ZoneAbslFlatHashSet<uint32_t>& stack_checks_to_remove() const {
    return stack_checks_to_remove_;
  }

 private:
  bool InputsValid(const Operation& op) const {
    for (OpIndex i : op.inputs()) {
      if (!IsValid(i)) return false;
    }
    return true;
  }

  template <class Op>
  void IncrementInputUses(const Op& op) {
    for (OpIndex input : op.inputs()) {
      // Tuples should never be used as input, except in other tuples (which is
      // used for instance in Int64Lowering::LowerCall).
      DCHECK_IMPLIES(Get(input).Is<TupleOp>(), op.template Is<TupleOp>());
      Get(input).saturated_use_count.Incr();
    }
  }

  template <class Op>
  void DecrementInputUses(const Op& op) {
    for (OpIndex input : op.inputs()) {
      // Tuples should never be used as input, except in other tuples (which is
      // used for instance in Int64Lowering::LowerCall).
      DCHECK_IMPLIES(Get(input).Is<TupleOp>(), op.template Is<TupleOp>());
      Get(input).saturated_use_count.Decr();
    }
  }

  // Allocates pointer-stable storage for new blocks, and pushes the pointers
  // to that storage to `bound_blocks_`. Initialization of the blocks is defered
  // to when they are actually constructed in `NewBlocks`.
  V8_NOINLINE V8_PRESERVE_MOST void AllocateNewBlocks() {
    constexpr size_t kMinCapacity = 32;
    size_t next_capacity = std::max(kMinCapacity, all_blocks_.size() * 2);
    size_t new_block_count = next_capacity - all_blocks_.size();
    DCHECK_GT(new_block_count, 0);
    base::Vector<Block> block_storage =
        graph_zone_->AllocateVector<Block>(new_block_count);
    base::Vector<Block*> new_all_blocks =
        graph_zone_->AllocateVector<Block*>(next_capacity);
    DCHECK_EQ(new_all_blocks.size(), all_blocks_.size() + new_block_count);
    std::copy(all_blocks_.begin(), all_blocks_.end(), new_all_blocks.begin());
    Block** insert_begin = new_all_blocks.begin() + all_blocks_.size();
    DCHECK_EQ(insert_begin + new_block_count, new_all_blocks.end());
    for (size_t i = 0; i < new_block_count; ++i) {
      insert_begin[i] = &block_storage[i];
    }
    base::Vector<Block*> old_all_blocks = all_blocks_;
    all_blocks_ = new_all_blocks;
    if (!old_all_blocks.empty()) {
      graph_zone_->DeleteArray(old_all_blocks.data(), old_all_blocks.length());
    }

    // Eventually most new blocks will be bound anyway, so pre-allocate as well.
    DCHECK_LE(bound_blocks_.size(), all_blocks_.size());
    bound_blocks_.reserve(all_blocks_.size());
  }

  OperationBuffer operations_;
  ZoneVector<Block*> bound_blocks_;
  // The next two fields essentially form a `ZoneVector` but with pointer
  // stability for the `Block` elements. That is, `all_blocks_` contains
  // pointers to (potentially non-contiguous) Zone-allocated `Block`s.
  // Each pointer in `all_blocks_` points to already allocated space, but they
  // are only properly value-initialized up to index `next_block_`.
  base::Vector<Block*> all_blocks_;
  size_t next_block_ = 0;
  GrowingOpIndexSidetable<BlockIndex> op_to_block_;
  // When `ReorderBlocks` is called, `block_permutation_` contains the original
  // order of blocks in order to provide a proper OpIndex->Block mapping for
  // `BlockOf`. In non-reordered graphs, this vector is empty.
  ZoneVector<Block*> block_permutation_;
  Zone* graph_zone_;
  GrowingOpIndexSidetable<SourcePosition> source_positions_;
  GrowingOpIndexSidetable<OpIndex> operation_origins_;
  uint32_t dominator_tree_depth_ = 0;
  GrowingOpIndexSidetable<Type> operation_types_;
#ifdef DEBUG
  GrowingBlockSidetable<TypeRefinements> block_type_refinement_;
  bool graph_created_from_turbofan_ = false;
#endif

  Graph* companion_ = nullptr;
#ifdef DEBUG
  size_t generation_ = 1;
#endif  // DEBUG

  // Phase specific data.
  // For some reducers/phases, we use the graph to pass data around. These data
  // should always be invalidated at the end of the graph copy.

  LoopUnrollingAnalyzer* loop_unrolling_analyzer_ = nullptr;

  // {stack_checks_to_remove_} contains the BlockIndex of loop headers whose
  // stack checks should be removed.
  // TODO(dmercadier): using the Zone for a resizable structure is not great
  // (because it tends to waste memory), but using a newed/malloced structure in
  // the Graph means that we have to remember to delete/free it, which isn't
  // convenient, because Zone memory typically isn't manually deleted (and the
  // Graph thus isn't). Still, it's probably not a big deal, because
  // {stack_checks_to_remove_} should never contain more than a handful of
  // items, and thus shouldn't waste too much memory.
  ZoneAbslFlatHashSet<uint32_t> stack_checks_to_remove_;
};

V8_INLINE OperationStorageSlot* AllocateOpStorage(Graph* graph,
                                                  size_t slot_count) {
  return graph->Allocate(slot_count);
}

V8_INLINE const Operation& Get(const Graph& graph, OpIndex index) {
  return graph.Get(index);
}

V8_INLINE const Operation& Block::FirstOperation(const Graph& graph) const {
  DCHECK_EQ(graph_generation_, graph.generation());
  DCHECK(begin_.valid());
  DCHECK(end_.valid());
  return graph.Get(begin_);
}

V8_INLINE const Operation& Block::LastOperation(const Graph& graph) const {
  DCHECK_EQ(graph_generation_, graph.generation());
  return graph.Get(graph.PreviousIndex(end()));
}

V8_INLINE Operation& Block::LastOperation(Graph& graph) const {
  DCHECK_EQ(graph_generation_, graph.generation());
  return graph.Get(graph.PreviousIndex(end()));
}

V8_INLINE bool Block::HasPhis(const Graph& graph) const {
  // TODO(dmercadier): consider re-introducing the invariant that Phis are
  // always at the begining of a block to speed up such functions. Currently,
  // in practice, Phis do not appear after the first non-FrameState non-Constant
  // operation, but this is not enforced.
  DCHECK_EQ(graph_generation_, graph.generation());
  for (const auto& op : graph.operations(*this)) {
    if (op.Is<PhiOp>()) return true;
  }
  return false;
}

struct PrintAsBlockHeader {
  const Block& block;
  BlockIndex block_id;

  explicit PrintAsBlockHeader(const Block& block)
      : block(block), block_id(block.index()) {}
  PrintAsBlockHeader(const Block& block, BlockIndex block_id)
      : block(block), block_id(block_id) {}
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           PrintAsBlockHeader block);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const Graph& graph);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const Block::Kind& kind);

inline uint32_t Block::ComputeDominator() {
  if (V8_UNLIKELY(LastPredecessor() == nullptr)) {
    // If the block has no predecessors, then it's the start block. We create a
    // jmp_ edge to itself, so that the SetDominator algorithm does not need a
    // special case for when the start block is reached.
    SetAsDominatorRoot();
  } else {
    // If the block has one or more predecessors, the dominator is the lowest
    // common ancestor (LCA) of all of the predecessors.

    // Note that for BranchTarget, there is a single predecessor. This doesn't
    // change the logic: the loop won't be entered, and the first (and only)
    // predecessor is set as the dominator.
    // Similarly, since we compute dominators on the fly, when we reach a
    // kLoopHeader, we haven't visited its body yet, and it should only have one
    // predecessor (the backedge is not here yet), which is its dominator.
    DCHECK_IMPLIES(kind_ == Block::Kind::kLoopHeader, PredecessorCount() == 1);

    Block* dominator = LastPredecessor();
    for (Block* pred = dominator->NeighboringPredecessor(); pred != nullptr;
         pred = pred->NeighboringPredecessor()) {
      dominator = dominator->GetCommonDominator(pred);
    }
    SetDominator(dominator);
  }
  DCHECK_NE(jmp_, nullptr);
  DCHECK_IMPLIES(nxt_ == nullptr, LastPredecessor() == nullptr);
  DCHECK_IMPLIES(len_ == 0, LastPredecessor() == nullptr);
  return Depth();
}

template <class Derived>
inline void RandomAccessStackDominatorNode<Derived>::SetAsDominatorRoot() {
  jmp_ = static_cast<Derived*>(this);
  nxt_ = nullptr;
  len_ = 0;
  jmp_len_ = 0;
}

template <class Derived>
inline void RandomAccessStackDominatorNode<Derived>::SetDominator(
    Derived* dominator) {
  DCHECK_NOT_NULL(dominator);
  DCHECK_NULL(static_cast<Block*>(this)->neighboring_child_);
  DCHECK_NULL(static_cast<Block*>(this)->last_child_);
  // Determining the jmp pointer
  Derived* t = dominator->jmp_;
  if (dominator->len_ - t->len_ == t->len_ - t->jmp_len_) {
    t = t->jmp_;
  } else {
    t = dominator;
  }
  // Initializing fields
  nxt_ = dominator;
  jmp_ = t;
  len_ = dominator->len_ + 1;
  jmp_len_ = jmp_->len_;
  dominator->AddChild(static_cast<Derived*>(this));
}

template <class Derived>
inline Derived* RandomAccessStackDominatorNode<Derived>::GetCommonDominator(
    RandomAccessStackDominatorNode<Derived>* other) const {
  const RandomAccessStackDominatorNode* a = this;
  const RandomAccessStackDominatorNode* b = other;
  if (b->len_ > a->len_) {
    // Swapping |a| and |b| so that |a| always has a greater length.
    std::swap(a, b);
  }
  DCHECK_GE(a->len_, 0);
  DCHECK_GE(b->len_, 0);

  // Going up the dominators of |a| in order to reach the level of |b|.
  while (a->len_ != b->len_) {
    DCHECK_GE(a->len_, 0);
    if (a->jmp_len_ >= b->len_) {
      a = a->jmp_;
    } else {
      a = a->nxt_;
    }
  }

  // Going up the dominators of |a| and |b| simultaneously until |a| == |b|
  while (a != b) {
    DCHECK_EQ(a->len_, b->len_);
    DCHECK_GE(a->len_, 0);
    if (a->jmp_ == b->jmp_) {
      // We found a common dominator, but we actually want to find the smallest
      // one, so we go down in the current subtree.
      a = a->nxt_;
      b = b->nxt_;
    } else {
      a = a->jmp_;
      b = b->jmp_;
    }
  }

  return static_cast<Derived*>(
      const_cast<RandomAccessStackDominatorNode<Derived>*>(a));
}

}  // namespace v8::internal::compiler::turboshaft

// MSVC needs this definition to know how to deal with the PredecessorIterator.
template <>
class std::iterator_traits<
    v8::internal::compiler::turboshaft::PredecessorIterator> {
 public:
  using iterator_category = std::forward_iterator_tag;
};

#endif  // V8_COMPILER_TURBOSHAFT_GRAPH_H_
