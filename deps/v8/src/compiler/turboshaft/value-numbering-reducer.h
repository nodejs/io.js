// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_VALUE_NUMBERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_VALUE_NUMBERING_REDUCER_H_

#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/fast-hash.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/reducer-traits.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

// Value numbering removes redundant nodes from the graph. A simple example
// could be:
//
//   x = a + b
//   y = a + b
//   z = x * y
//
// Is simplified to
//
//   x = a + b
//   z = x * x
//
// It works by storing previously seen nodes in a hashmap, and when visiting a
// new node, we check to see if it's already in the hashmap. If yes, then we
// return the old node. If not, then we keep the new one (and add it into the
// hashmap). A high-level pseudo-code would be:
//
//   def VisitOp(op):
//     if op in hashmap:
//        return hashmap.get(op)
//     else:
//        hashmap.add(op)
//        return op
//
// We implemented our own hashmap (to have more control, it should become
// clearer why by the end of this explanation). When there is a collision, we
// look at the next index (and the next one if there is yet another collision,
// etc). While not the fastest approach, it has the advantage of not requiring
// any dynamic memory allocation (besides the initial table, and the resizing).
//
// For the approach describe above (the pseudocode and the paragraph before it)
// to be correct, a node should only be replaced by a node defined in blocks
// that dominate the current block. Thus, this reducer relies on the fact that
// OptimizationPhases that iterate the graph dominator order. Then, when going
// down the dominator tree, we add nodes to the hashmap, and when going back up
// the dominator tree, we remove nodes from the hashmap.
//
// In order to efficiently remove all the nodes of a given block from the
// hashmap, we maintain a linked-list of hashmap entries per block (this way, we
// don't have to iterate the wole hashmap). Note that, in practice, we think in
// terms of "depth" rather than "block", and we thus have one linked-list per
// depth of the dominator tree. The heads of those linked lists are stored in
// the vector {depths_heads_}. The linked lists are then implemented in-place in
// the hashtable entries, thanks to the `depth_neighboring_entry` field of the
// `Entry` structure.
// To remove all of the entries from a given linked list, we iterate the entries
// in the linked list, setting all of their `hash` field to 0 (we prevent hashes
// from being equal to 0, in order to detect empty entries: their hash is 0).

template <class Next>
class TypeInferenceReducer;

class ScopeCounter {
 public:
  void enter() { scopes_++; }
  void leave() { scopes_--; }
  bool is_active() { return scopes_ > 0; }

 private:
  int scopes_{0};
};

// In rare cases of intentional duplication of instructions, we need to disable
// value numbering. This scope manages that.
class DisableValueNumbering {
 public:
  template <class Reducer>
  explicit DisableValueNumbering(Reducer* reducer) {
    if constexpr (reducer_list_contains<typename Reducer::ReducerList,
                                        ValueNumberingReducer>::value) {
      scopes_ = reducer->gvn_disabled_scope();
      scopes_->enter();
    }
  }

  ~DisableValueNumbering() {
    if (scopes_ != nullptr) scopes_->leave();
  }

 private:
  ScopeCounter* scopes_{nullptr};
};

template <class Next>
class ValueNumberingReducer : public Next {
#if defined(__clang__)
  static_assert(next_is_bottom_of_assembler_stack<Next>::value ||
                next_reducer_is<Next, TypeInferenceReducer>::value);
#endif

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(ValueNumbering)

  template <typename Op>
  static constexpr bool CanBeGVNed() {
    constexpr Opcode opcode = operation_to_opcode_v<Op>;
    /* Throwing operations have a non-trivial lowering, so they don't work
     * with value numbering. */
    if constexpr (MayThrow(opcode)) return false;
    if constexpr (opcode == Opcode::kCatchBlockBegin) {
      /* CatchBlockBegin are never interesting to GVN, but additionally
       * split-edge can transform CatchBlockBeginOp into PhiOp, which means
       * that there is no guarantee here than {result} is indeed a
       * CatchBlockBegin. */
      return false;
    }
    if constexpr (opcode == Opcode::kComment) {
      /* We don't want to GVN comments. */
      return false;
    }
    return true;
  }

#define EMIT_OP(Name)                                                 \
  template <class... Args>                                            \
  OpIndex Reduce##Name(Args... args) {                                \
    OpIndex next_index = Asm().output_graph().next_operation_index(); \
    USE(next_index);                                                  \
    OpIndex result = Next::Reduce##Name(args...);                     \
    if (ShouldSkipOptimizationStep()) return result;                  \
    if constexpr (!CanBeGVNed<Name##Op>()) return result;             \
    DCHECK_EQ(next_index, result);                                    \
    return AddOrFind<Name##Op>(result);                               \
  }
  TURBOSHAFT_OPERATION_LIST(EMIT_OP)
#undef EMIT_OP

  void Bind(Block* block) {
    Next::Bind(block);
    ResetToBlock(block);
    dominator_path_.push_back(block);
    depths_heads_.push_back(nullptr);
  }

  // Resets {table_} up to the first dominator of {block} that it contains.
  void ResetToBlock(Block* block) {
    Block* target = block->GetDominator();
    while (!dominator_path_.empty() && target != nullptr &&
           dominator_path_.back() != target) {
      if (dominator_path_.back()->Depth() > target->Depth()) {
        ClearCurrentDepthEntries();
      } else if (dominator_path_.back()->Depth() < target->Depth()) {
        target = target->GetDominator();
      } else {
        // {target} and {dominator_path.back} have the same depth but are not
        // equal, so we go one level up for both.
        ClearCurrentDepthEntries();
        target = target->GetDominator();
      }
    }
  }

  template <class Op>
  bool WillGVNOp(const Op& op) {
    Entry* entry = Find(op);
    return !entry->IsEmpty();
  }

  ScopeCounter* gvn_disabled_scope() { return &disabled_scope_; }

 private:
  // TODO(dmercadier): Once the mapping from Operations to Blocks has been added
  // to turboshaft, remove the `block` field from the `Entry` structure.
  struct Entry {
    OpIndex value;
    BlockIndex block;
    size_t hash = 0;
    Entry* depth_neighboring_entry = nullptr;

    bool IsEmpty() const { return hash == 0; }
  };

  template <class Op>
  OpIndex AddOrFind(OpIndex op_idx) {
    if (is_disabled()) return op_idx;

    const Op& op = Asm().output_graph().Get(op_idx).template Cast<Op>();
    if (std::is_same_v<Op, PendingLoopPhiOp> || op.IsBlockTerminator() ||
        (!op.Effects().repetition_is_eliminatable() &&
         !std::is_same_v<Op, DeoptimizeIfOp>)) {
      // GVNing DeoptimizeIf is safe, despite its lack of
      // repetition_is_eliminatable.
      return op_idx;
    }
    RehashIfNeeded();

    size_t hash;
    Entry* entry = Find(op, &hash);
    if (entry->IsEmpty()) {
      // {op} is not present in the state, inserting it.
      *entry = Entry{op_idx, Asm().current_block()->index(), hash,
                     depths_heads_.back()};
      depths_heads_.back() = entry;
      ++entry_count_;
      return op_idx;
    } else {
      // {op} is already present, removing it from the graph and returning the
      // previous one.
      Next::RemoveLast(op_idx);
      return entry->value;
    }
  }

  template <class Op>
  Entry* Find(const Op& op, size_t* hash_ret = nullptr) {
    constexpr bool same_block_only = std::is_same<Op, PhiOp>::value;
    size_t hash = ComputeHash<same_block_only>(op);
    size_t start_index = hash & mask_;
    for (size_t i = start_index;; i = NextEntryIndex(i)) {
      Entry& entry = table_[i];
      if (entry.IsEmpty()) {
        // We didn't find {op} in {table_}. Returning where it could be
        // inserted.
        if (hash_ret) *hash_ret = hash;
        return &entry;
      }
      if (entry.hash == hash) {
        const Operation& entry_op = Asm().output_graph().Get(entry.value);
        if (entry_op.Is<Op>() &&
            (!same_block_only ||
             entry.block == Asm().current_block()->index()) &&
            entry_op.Cast<Op>().EqualsForGVN(op)) {
          return &entry;
        }
      }
      // Making sure that we don't have an infinite loop.
      DCHECK_NE(start_index, NextEntryIndex(i));
    }
  }

  // Remove all of the Entries of the current depth.
  void ClearCurrentDepthEntries() {
    for (Entry* entry = depths_heads_.back(); entry != nullptr;) {
      entry->hash = 0;
      Entry* next_entry = entry->depth_neighboring_entry;
      entry->depth_neighboring_entry = nullptr;
      entry = next_entry;
      --entry_count_;
    }
    depths_heads_.pop_back();
    dominator_path_.pop_back();
  }

  // If the table is too full, double its size and re-insert the old entries.
  void RehashIfNeeded() {
    if (V8_LIKELY(table_.size() - (table_.size() / 4) > entry_count_)) return;
    base::Vector<Entry> new_table = table_ =
        Asm().phase_zone()->template NewVector<Entry>(table_.size() * 2);
    size_t mask = mask_ = table_.size() - 1;

    for (size_t depth_idx = 0; depth_idx < depths_heads_.size(); depth_idx++) {
      // It's important to fill the new hash by inserting data in increasing
      // depth order, in order to avoid holes when later calling
      // ClearCurrentDepthEntries. Consider for instance:
      //
      //  ---+------+------+------+----
      //     |  a1  |  a2  |  a3  |
      //  ---+------+------+------+----
      //
      // Where a1, a2 and a3 have the same hash. By construction, we know that
      // depth(a1) <= depth(a2) <= depth(a3). If, when re-hashing, we were to
      // insert them in another order, say:
      //
      //  ---+------+------+------+----
      //     |  a3  |  a1  |  a2  |
      //  ---+------+------+------+----
      //
      // Then, when we'll call ClearCurrentDepthEntries to remove entries from
      // a3's depth, we'll get this:
      //
      //  ---+------+------+------+----
      //     | null |  a1  |  a2  |
      //  ---+------+------+------+----
      //
      // And, when looking if a1 is in the hash, we'd find a "null" where we
      // expect it, and assume that it's not present. If, instead, we always
      // conserve the increasing depth order, then when removing a3, we'd get:
      //
      //  ---+------+------+------+----
      //     |  a1  |  a2  | null |
      //  ---+------+------+------+----
      //
      // Where we can still find a1 and a2.
      Entry* entry = depths_heads_[depth_idx];
      depths_heads_[depth_idx] = nullptr;

      while (entry != nullptr) {
        for (size_t i = entry->hash & mask;; i = NextEntryIndex(i)) {
          if (new_table[i].hash == 0) {
            new_table[i] = *entry;
            Entry* next_entry = entry->depth_neighboring_entry;
            new_table[i].depth_neighboring_entry = depths_heads_[depth_idx];
            depths_heads_[depth_idx] = &new_table[i];
            entry = next_entry;
            break;
          }
        }
      }
    }
  }

  template <bool same_block_only, class Op>
  size_t ComputeHash(const Op& op) {
    size_t hash = op.hash_value();
    if (same_block_only) {
      hash = fast_hash_combine(Asm().current_block()->index(), hash);
    }
    if (V8_UNLIKELY(hash == 0)) return 1;
    return hash;
  }

  size_t NextEntryIndex(size_t index) { return (index + 1) & mask_; }
  Entry* NextEntry(Entry* entry) {
    return V8_LIKELY(entry + 1 < table_.end()) ? entry + 1 : &table_[0];
  }
  Entry* PrevEntry(Entry* entry) {
    return V8_LIKELY(entry > table_.begin()) ? entry - 1 : table_.end() - 1;
  }

  bool is_disabled() { return disabled_scope_.is_active(); }

  ZoneVector<Block*> dominator_path_{Asm().phase_zone()};
  base::Vector<Entry> table_ = Asm().phase_zone()->template NewVector<Entry>(
      base::bits::RoundUpToPowerOfTwo(
          std::max<size_t>(128, Asm().input_graph().op_id_capacity() / 2)));
  size_t mask_ = table_.size() - 1;
  size_t entry_count_ = 0;
  ZoneVector<Entry*> depths_heads_{Asm().phase_zone()};
  ScopeCounter disabled_scope_;
};

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_VALUE_NUMBERING_REDUCER_H_
