// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STORE_STORE_ELIMINATION_REDUCER_INL_H_
#define V8_COMPILER_TURBOSHAFT_STORE_STORE_ELIMINATION_REDUCER_INL_H_

#include <optional>

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"
#include "src/heap/heap-layout-inl.h"
#include "src/objects/heap-object-inl.h"

namespace v8::internal::compiler::turboshaft {

// 1. StoreStoreEliminationReducer tries to identify and remove redundant
// stores. E.g. for an input like
//
//   let o = {};
//   o.x = 2;
//   o.y = 3;
//   o.x = 4;
//   use(o.x);
//
// we don't need the first store to `o.x` because the value is overwritten
// before it can be observed.
//
// The analysis considers loads and stores as corresponding pairs of `base` (the
// OpIndex that defines `o` in the above example) and `offset` (that is the
// static offset at which the field is stored inside the object). We run the
// analysis backwards and track potentially redundant stores in a
// `MaybeRedundantStoresTable` roughly as follows:
//
//   1. When we see a `base`+`offset` store and
//     a. `base`+`offset` is observable in the table, we keep the store, but
//        track following `base`+`offset` stores as unobservable in the table.
//     b. `base`+`offset` is unobservable in the table, we can eliminate the
//        store and keep the table unchanged.
//     c. `base`+`offset` is gc-observable in the table, we eliminate it only
//        if it is not an initializing store, otherwise, we keep it and update
//        the table accordingly.
//   2. When we see a `base`+`offset` load, we mark all stores to this `offset`
//      as observable in the table. Notice that we do this regardless of the
//      `base`, because they might alias potentially.
//   3. When we see an allocation, we mark all stores that are currently
//      unobservable, as gc-observable. The idea behind gc-observability is
//      that we do not observe the actual value stored, but we need to make
//      sure that the fields are written at least once, so that the GC does not
//      see uninitialized fields.
//   4. When we see another operation that can observe memory, we mark all
//      stores as observable.
//
// Notice that the table also tracks the `size` of the store, such that if
// fields are partially written, we don't incorrectly treat them as redundant to
// the full store (this can happen in strings for example).
//
// When the analysis reaches a branch, we combine values using traditional least
// upper bound operation on the `StoreObservability` lattice. After processing a
// loop header, we revisit the loop if the resulting state has changed until we
// reach a fixpoint.
//
//
// 2. StoreStoreEliminationReducer tries to merge 2 continuous 32-bits stores
// into a 64-bits one.
// When v8 create a new js object, it will initialize it's in object fields to
// some constant value after allocation, like `undefined`. When pointer
// compression is enabled, they are continuous 32-bits stores, and the store
// values are usually constants (heap object). This reducer will try to merge 2
// continuous 32-bits stores into a 64-bits one.

#include "src/compiler/turboshaft/define-assembler-macros.inc"

enum class StoreObservability {
  kUnobservable = 0,
  kGCObservable = 1,
  kObservable = 2,
};

inline std::ostream& operator<<(std::ostream& os,
                                StoreObservability observability) {
  switch (observability) {
    case StoreObservability::kUnobservable:
      return os << "Unobservable";
    case StoreObservability::kGCObservable:
      return os << "GCObservable";
    case StoreObservability::kObservable:
      return os << "Observable";
  }
}

struct MaybeRedundantStoresKeyData {
  OpIndex base;
  int32_t offset;
  uint8_t size;
  IntrusiveSetIndex active_keys_index = {};
};

class MaybeRedundantStoresTable
    : public ChangeTrackingSnapshotTable<MaybeRedundantStoresTable,
                                         StoreObservability,
                                         MaybeRedundantStoresKeyData> {
  using super =
      ChangeTrackingSnapshotTable<MaybeRedundantStoresTable, StoreObservability,
                                  MaybeRedundantStoresKeyData>;

 public:
  explicit MaybeRedundantStoresTable(const Graph& graph, Zone* zone)
      : ChangeTrackingSnapshotTable(zone),
        graph_(graph),
        block_to_snapshot_mapping_(zone),
        key_mapping_(zone),
        active_keys_(zone),
        successor_snapshots_(zone) {}

  void OnNewKey(Key key, StoreObservability value) {
    DCHECK_EQ(value, StoreObservability::kObservable);
    DCHECK(!active_keys_.Contains(key));
  }

  void OnValueChange(Key key, StoreObservability old_value,
                     StoreObservability new_value) {
    DCHECK_NE(old_value, new_value);
    if (new_value == StoreObservability::kObservable) {
      active_keys_.Remove(key);
    } else if (old_value == StoreObservability::kObservable) {
      active_keys_.Add(key);
    }
  }

  void BeginBlock(const Block* block) {
    // Seal the current block first.
    if (IsSealed()) {
      DCHECK_NULL(current_block_);
    } else {
      // If we bind a new block while the previous one is still unsealed, we
      // finalize it.
      Seal();
    }

    // Collect the snapshots of all successors.
    {
      auto successors = SuccessorBlocks(block->LastOperation(graph_));
      successor_snapshots_.clear();
      for (const Block* s : successors) {
        std::optional<Snapshot> s_snapshot =
            block_to_snapshot_mapping_[s->index()];
        // When we visit the loop for the first time, the loop header hasn't
        // been visited yet, so we ignore it.
        DCHECK_IMPLIES(!s_snapshot.has_value(), s->IsLoop());
        if (!s_snapshot.has_value()) continue;
        successor_snapshots_.push_back(*s_snapshot);
      }
    }

    // Start a new snapshot for this block by merging information from
    // successors.
    StartNewSnapshot(
        base::VectorOf(successor_snapshots_),
        [](Key, base::Vector<const StoreObservability> successors) {
          return static_cast<StoreObservability>(
              *std::max_element(successors.begin(), successors.end()));
        });

    current_block_ = block;
  }

  StoreObservability GetObservability(OpIndex base, int32_t offset,
                                      uint8_t size) {
    Key key = map_to_key(base, offset, size);
    if (key.data().size < size) return StoreObservability::kObservable;
    return Get(key);
  }

  void MarkStoreAsUnobservable(OpIndex base, int32_t offset, uint8_t size) {
    // We can only shadow stores to the exact same `base`+`offset` and keep
    // everything else because they might or might not alias.
    Key key = map_to_key(base, offset, size);
    // If the `size` we want to mark unobservable here is less than the size we
    // have seen for this key before, we do not overwrite the entire field, so
    // preceeding stores are not (fully) unobservable.
    if (size < key.data().size) return;
    Set(key, StoreObservability::kUnobservable);
  }

  void MarkPotentiallyAliasingStoresAsObservable(OpIndex base, int32_t offset) {
    // For now, we consider all stores to the same offset as potentially
    // aliasing. We might improve this to eliminate more precisely, if we have
    // some sort of aliasing information.
    for (Key key : active_keys_) {
      if (key.data().offset == offset)
        Set(key, StoreObservability::kObservable);
    }
  }

  void MarkAllStoresAsObservable() {
    for (Key key : active_keys_) {
      Set(key, StoreObservability::kObservable);
    }
  }

  void MarkAllStoresAsGCObservable() {
    for (Key key : active_keys_) {
      auto current = Get(key);
      DCHECK_NE(current, StoreObservability::kObservable);
      if (current == StoreObservability::kUnobservable) {
        Set(key, StoreObservability::kGCObservable);
      }
    }
  }

  void Seal(bool* snapshot_has_changed = nullptr) {
    DCHECK(!IsSealed());
    DCHECK_NOT_NULL(current_block_);
    DCHECK(current_block_->index().valid());
    auto& snapshot = block_to_snapshot_mapping_[current_block_->index()];
    if (!snapshot_has_changed) {
      snapshot = super::Seal();
    } else if (!snapshot.has_value()) {
      *snapshot_has_changed = true;
      snapshot = super::Seal();
    } else {
      auto new_snapshot = super::Seal();
      *snapshot_has_changed = false;
      StartNewSnapshot(
          base::VectorOf({snapshot.value(), new_snapshot}),
          [&](Key key, base::Vector<const StoreObservability> successors) {
            DCHECK_LE(successors[0], successors[1]);
            if (successors[0] != successors[1]) *snapshot_has_changed = true;
            return static_cast<StoreObservability>(
                *std::max_element(successors.begin(), successors.end()));
          });
      snapshot = super::Seal();
    }
    current_block_ = nullptr;
  }

  void Print(std::ostream& os, const char* sep = "\n") const {
    bool first = true;
    for (Key key : active_keys_) {
      os << (first ? "" : sep) << key.data().base.id() << "@"
         << key.data().offset << ": " << Get(key);
      first = false;
    }
  }

 private:
  Key map_to_key(OpIndex base, int32_t offset, uint8_t size) {
    std::pair p{base, offset};
    auto it = key_mapping_.find(p);
    if (it != key_mapping_.end()) return it->second;
    Key new_key = NewKey(MaybeRedundantStoresKeyData{base, offset, size},
                         StoreObservability::kObservable);
    key_mapping_.emplace(p, new_key);
    return new_key;
  }
  struct GetActiveKeysIndex {
    IntrusiveSetIndex& operator()(Key key) const {
      return key.data().active_keys_index;
    }
  };

  const Graph& graph_;
  GrowingBlockSidetable<std::optional<Snapshot>> block_to_snapshot_mapping_;
  ZoneAbslFlatHashMap<std::pair<OpIndex, int32_t>, Key> key_mapping_;
  // In `active_keys_`, we track the keys of all stores that arge gc-observable
  // or unobservable. Keys that are mapped to the default value (observable) are
  // removed from the `active_keys_`.
  ZoneIntrusiveSet<Key, GetActiveKeysIndex> active_keys_;
  const Block* current_block_ = nullptr;
  // {successor_snapshots_} and {temp_key_vector_} are used as temporary vectors
  // inside functions. We store them as members to avoid reallocation.
  ZoneVector<Snapshot> successor_snapshots_;
};

class RedundantStoreAnalysis {
 public:
  RedundantStoreAnalysis(const Graph& graph, Zone* phase_zone)
      : graph_(graph), table_(graph, phase_zone) {}

  void Run(ZoneSet<OpIndex>& eliminable_stores,
           ZoneMap<OpIndex, uint64_t>& mergeable_store_pairs) {
    eliminable_stores_ = &eliminable_stores;
    mergeable_store_pairs_ = &mergeable_store_pairs;
    for (uint32_t processed = graph_.block_count(); processed > 0;
         --processed) {
      BlockIndex block_index = static_cast<BlockIndex>(processed - 1);

      const Block& block = graph_.Get(block_index);
      ProcessBlock(block);

      // If this block is a loop header, check if this loop needs to be
      // revisited.
      if (block.IsLoop()) {
        DCHECK(!table_.IsSealed());
        bool needs_revisit = false;
        table_.Seal(&needs_revisit);
        if (needs_revisit) {
          Block* back_edge = block.LastPredecessor();
          DCHECK_GE(back_edge->index(), block_index);
          processed = back_edge->index().id() + 1;
        }
      }
    }
    eliminable_stores_ = nullptr;
    mergeable_store_pairs_ = nullptr;
  }

  void ProcessBlock(const Block& block) {
    table_.BeginBlock(&block);

    auto op_range = graph_.OperationIndices(block);
    for (auto it = op_range.end(); it != op_range.begin();) {
      --it;
      OpIndex index = *it;
      const Operation& op = graph_.Get(index);

      switch (op.opcode) {
        case Opcode::kStore: {
          const StoreOp& store = op.Cast<StoreOp>();
          // TODO(nicohartmann@): Use the new effect flags to distinguish heap
          // access once available.
          const bool is_on_heap_store = store.kind.tagged_base;
          const bool is_field_store = !store.index().valid();
          const uint8_t size = store.stored_rep.SizeInBytes();
          // For now we consider only stores of fields of objects on the heap.
          if (is_on_heap_store && is_field_store) {
            bool is_eliminable_store = false;
            switch (table_.GetObservability(store.base(), store.offset, size)) {
              case StoreObservability::kUnobservable:
                eliminable_stores_->insert(index);
                last_field_initialization_store_ = OpIndex::Invalid();
                is_eliminable_store = true;
                break;
              case StoreObservability::kGCObservable:
                if (store.maybe_initializing_or_transitioning) {
                  // We cannot eliminate this store, but we mark all following
                  // stores to the same `base+offset` as unobservable.
                  table_.MarkStoreAsUnobservable(store.base(), store.offset,
                                                 size);
                } else {
                  eliminable_stores_->insert(index);
                  last_field_initialization_store_ = OpIndex::Invalid();
                  is_eliminable_store = true;
                }
                break;
              case StoreObservability::kObservable:
                // We cannot eliminate this store, but we mark all following
                // stores to the same `base+offset` as unobservable.
                table_.MarkStoreAsUnobservable(store.base(), store.offset,
                                               size);
                break;
            }

            // Try to merge 2 consecutive 32-bit stores into a single 64-bit
            // one.
            if (COMPRESS_POINTERS_BOOL && !is_eliminable_store &&
                store.maybe_initializing_or_transitioning &&
                store.kind == StoreOp::Kind::TaggedBase() &&
                store.write_barrier == WriteBarrierKind::kNoWriteBarrier &&
                store.stored_rep.IsCompressibleTagged()) {
              if (last_field_initialization_store_.valid() &&
                  graph_.NextIndex(index) == last_field_initialization_store_) {
                const StoreOp& store0 = store;
                const StoreOp& store1 =
                    graph_.Get(last_field_initialization_store_)
                        .Cast<StoreOp>();

                DCHECK(!store0.index().valid());
                DCHECK(!store1.index().valid());

                const ConstantOp* c0 =
                    graph_.Get(store0.value()).TryCast<ConstantOp>();
                const ConstantOp* c1 =
                    graph_.Get(store1.value()).TryCast<ConstantOp>();

                // TODO(dmercadier): for now, we only apply this optimization
                // when storing read-only values, because otherwise the GC will
                // lose track of Handles when we convert them to a raw Word64.
                // However, if we were to keep the reloc info up-to-date, then
                // this might work for any object. To do this, we might need to
                // delay this optimization to later (instruction selector for
                // instance).
                if (c0 && c1 && c0->kind == ConstantOp::Kind::kHeapObject &&
                    c1->kind == ConstantOp::Kind::kHeapObject &&
                    store1.offset - store0.offset == 4 &&
                    HeapLayout::InReadOnlySpace(*c0->handle()) &&
                    HeapLayout::InReadOnlySpace(*c1->handle())) {
                  uint32_t high = static_cast<uint32_t>(c1->handle()->ptr());
                  uint32_t low = static_cast<uint32_t>(c0->handle()->ptr());
#if V8_TARGET_BIG_ENDIAN
                  uint64_t merged = make_uint64(low, high);
#else
                  uint64_t merged = make_uint64(high, low);
#endif
                  mergeable_store_pairs_->insert({index, merged});

                  eliminable_stores_->insert(last_field_initialization_store_);
                  last_field_initialization_store_ = OpIndex::Invalid();
                }

              } else {
                last_field_initialization_store_ = index;
              }
            }
          }
          break;
        }
        case Opcode::kLoad: {
          const LoadOp& load = op.Cast<LoadOp>();
          // TODO(nicohartmann@): Use the new effect flags to distinguish heap
          // access once available.
          const bool is_on_heap_load = load.kind.tagged_base;
          const bool is_field_load = !load.index().valid();
          // For now we consider only loads of fields of objects on the heap.
          if (is_on_heap_load && is_field_load) {
            table_.MarkPotentiallyAliasingStoresAsObservable(load.base(),
                                                             load.offset);
          }
          break;
        }
        default: {
          OpEffects effects = op.Effects();
          if (effects.can_read_mutable_memory()) {
            table_.MarkAllStoresAsObservable();
          } else if (effects.requires_consistent_heap()) {
            table_.MarkAllStoresAsGCObservable();
          }
        } break;
      }
    }
  }

 private:
  const Graph& graph_;
  MaybeRedundantStoresTable table_;
  ZoneSet<OpIndex>* eliminable_stores_ = nullptr;

  ZoneMap<OpIndex, uint64_t>* mergeable_store_pairs_ = nullptr;
  OpIndex last_field_initialization_store_ = OpIndex::Invalid();
};

template <class Next>
class StoreStoreEliminationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(StoreStoreElimination)

  void Analyze() {
    analysis_.Run(eliminable_stores_, mergeable_store_pairs_);
    Next::Analyze();
  }

  OpIndex REDUCE_INPUT_GRAPH(Store)(OpIndex ig_index, const StoreOp& store) {
    if (eliminable_stores_.count(ig_index) > 0) {
      return OpIndex::Invalid();
    } else if (mergeable_store_pairs_.count(ig_index) > 0) {
      DCHECK(COMPRESS_POINTERS_BOOL);
      OpIndex value = __ Word64Constant(mergeable_store_pairs_[ig_index]);
      __ Store(__ MapToNewGraph(store.base()), value,
               StoreOp::Kind::TaggedBase(), MemoryRepresentation::Uint64(),
               WriteBarrierKind::kNoWriteBarrier, store.offset);
      return OpIndex::Invalid();
    }
    return Next::ReduceInputGraphStore(ig_index, store);
  }

 private:
  RedundantStoreAnalysis analysis_{Asm().input_graph(), Asm().phase_zone()};
  ZoneSet<OpIndex> eliminable_stores_{Asm().phase_zone()};
  ZoneMap<OpIndex, uint64_t> mergeable_store_pairs_{Asm().phase_zone()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_STORE_STORE_ELIMINATION_REDUCER_INL_H_
