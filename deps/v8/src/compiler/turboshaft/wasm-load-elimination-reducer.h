// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_LOAD_ELIMINATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_LOAD_ELIMINATION_REDUCER_H_

#include "src/base/doubly-threaded-list.h"
#include "src/compiler/turboshaft/analyzer-iterator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/loop-finder.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/snapshot-table-opindex.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// WLE is short for Wasm Load Elimination.
// We need the namespace because we reuse class names below that also exist
// in the LateLoadEliminationReducer, and in the same namespace that'd be
// an ODR violation, i.e. Undefined Behavior.
// TODO(jkummerow): Refactor the two Load Elimination implementations to
// reuse commonalities.
namespace wle {

// We model array length and string canonicalization as fields at negative
// indices.
static constexpr int kArrayLengthFieldIndex = -1;
static constexpr int kStringPrepareForGetCodeunitIndex = -2;
static constexpr int kStringAsWtf16Index = -3;
static constexpr int kAnyConvertExternIndex = -4;
static constexpr int kAssertNotNullIndex = -5;

// All "load-like" special cases use the same fake size and type. The specific
// values we use don't matter; for accurate alias analysis, the type should
// be "unrelated" to any struct type.
static constexpr uint32_t kLoadLikeType = wasm::HeapType::kExtern;
static constexpr int kLoadLikeSize = 4;  // Chosen by fair dice roll.

struct WasmMemoryAddress {
  OpIndex base;
  int32_t offset;
  uint32_t type_index;
  uint8_t size;
  bool mutability;

  bool operator==(const WasmMemoryAddress& other) const {
    return base == other.base && offset == other.offset &&
           type_index == other.type_index && size == other.size &&
           mutability == other.mutability;
  }
};

inline size_t hash_value(WasmMemoryAddress const& mem) {
  return fast_hash_combine(mem.base, mem.offset, mem.type_index, mem.size,
                           mem.mutability);
}

struct KeyData {
  using Key = SnapshotTableKey<OpIndex, KeyData>;
  WasmMemoryAddress mem = {};
  // Pointers to the previous and the next Keys at the same base.
  Key* prev_same_base = nullptr;
  Key next_same_base = {};
  // Pointers to either the next/previous Keys at the same offset.
  Key* prev_same_offset = nullptr;
  Key next_same_offset = {};
};

struct OffsetListTraits {
  using T = SnapshotTable<OpIndex, KeyData>::Key;
  static T** prev(T t) { return &(t.data().prev_same_offset); }
  static T* next(T t) { return &(t.data().next_same_offset); }
  static bool non_empty(T t) { return t.valid(); }
};

struct BaseListTraits {
  using T = SnapshotTable<OpIndex, KeyData>::Key;
  static T** prev(T t) { return &(t.data().prev_same_base); }
  static T* next(T t) { return &(t.data().next_same_base); }
  static bool non_empty(T t) { return t.valid(); }
};

struct BaseData {
  using Key = SnapshotTable<OpIndex, KeyData>::Key;
  // List of every value at this base that has an offset rather than an index.
  v8::base::DoublyThreadedList<Key, BaseListTraits> with_offsets;
};

class WasmMemoryContentTable
    : public ChangeTrackingSnapshotTable<WasmMemoryContentTable, OpIndex,
                                         KeyData> {
 public:
  using MemoryAddress = WasmMemoryAddress;

  explicit WasmMemoryContentTable(
      PipelineData* data, Zone* zone,
      SparseOpIndexSnapshotTable<bool>& non_aliasing_objects,
      FixedOpIndexSidetable<OpIndex>& replacements, Graph& graph)
      : ChangeTrackingSnapshotTable(zone),
        non_aliasing_objects_(non_aliasing_objects),
        replacements_(replacements),
        data_(data),
        graph_(graph),
        all_keys_(zone),
        base_keys_(zone),
        offset_keys_(zone) {}

  void OnNewKey(Key key, OpIndex value) {
    if (value.valid()) {
      AddKeyInBaseOffsetMaps(key);
    }
  }

  void OnValueChange(Key key, OpIndex old_value, OpIndex new_value) {
    DCHECK_NE(old_value, new_value);
    if (old_value.valid() && !new_value.valid()) {
      RemoveKeyFromBaseOffsetMaps(key);
    } else if (new_value.valid() && !old_value.valid()) {
      AddKeyInBaseOffsetMaps(key);
    } else {
      DCHECK_EQ(new_value.valid(), old_value.valid());
    }
  }

  bool TypesUnrelated(uint32_t type1, uint32_t type2) {
    return wasm::TypesUnrelated(wasm::ValueType::Ref(type1),
                                wasm::ValueType::Ref(type2), module_, module_);
  }

  void Invalidate(const StructSetOp& set) {
    // This is like LateLoadElimination's {InvalidateAtOffset}, but based
    // on Wasm types instead of tracked JS maps.
    int offset = field_offset(set.type, set.field_index);
    auto offset_keys = offset_keys_.find(offset);
    if (offset_keys == offset_keys_.end()) return;
    for (auto it = offset_keys->second.begin();
         it != offset_keys->second.end();) {
      Key key = *it;
      DCHECK_EQ(offset, key.data().mem.offset);
      OpIndex base = key.data().mem.base;

      // If the base is guaranteed non-aliasing, we don't need to clear any
      // other entries. Any existing entry for this base will be overwritten
      // by {Insert(set)}.
      if (non_aliasing_objects_.Get(base)) {
        ++it;
        continue;
      }

      if (TypesUnrelated(set.type_index, key.data().mem.type_index)) {
        ++it;
        continue;
      }

      it = offset_keys->second.RemoveAt(it);
      Set(key, OpIndex::Invalid());
    }
  }

  // Invalidates all Keys that are not known as non-aliasing.
  enum class EntriesWithOffsets { kInvalidate, kKeep };
  template <EntriesWithOffsets offsets = EntriesWithOffsets::kInvalidate>
  void InvalidateMaybeAliasing() {
    // We find current active keys through {base_keys_} so that we can bail out
    // for whole buckets non-aliasing buckets (if we had gone through
    // {offset_keys_} instead, then for each key we would've had to check
    // whether it was non-aliasing or not).
    for (auto& base_keys : base_keys_) {
      OpIndex base = base_keys.first;
      if (non_aliasing_objects_.Get(base)) continue;
      if constexpr (offsets == EntriesWithOffsets::kInvalidate) {
        for (auto it = base_keys.second.with_offsets.begin();
             it != base_keys.second.with_offsets.end();) {
          Key key = *it;
          if (key.data().mem.mutability == false) {
            ++it;
            continue;
          }
          // It's important to remove with RemoveAt before Setting the key to
          // invalid, otherwise OnKeyChange will remove {key} from {base_keys},
          // which will invalidate {it}.
          it = base_keys.second.with_offsets.RemoveAt(it);
          Set(key, OpIndex::Invalid());
        }
      }
    }
  }

  // TODO(jkummerow): Move this to the WasmStruct class?
  int field_offset(const wasm::StructType* type, int field_index) {
    return WasmStruct::kHeaderSize + type->field_offset(field_index);
  }

  OpIndex Find(const StructGetOp& get) {
    int32_t offset = field_offset(get.type, get.field_index);
    uint8_t size = get.type->field(get.field_index).value_kind_size();
    bool mutability = get.type->mutability(get.field_index);
    return FindImpl(ResolveBase(get.object()), offset, get.type_index, size,
                    mutability);
  }

  bool HasValueWithIncorrectMutability(const StructSetOp& set) {
    int32_t offset = field_offset(set.type, set.field_index);
    uint8_t size = set.type->field(set.field_index).value_kind_size();
    bool mutability = set.type->mutability(set.field_index);
    WasmMemoryAddress mem{ResolveBase(set.object()), offset, set.type_index,
                          size, !mutability};
    return all_keys_.find(mem) != all_keys_.end();
  }

  OpIndex FindLoadLike(OpIndex op_idx, int offset_sentinel) {
    static constexpr bool mutability = false;
    return FindImpl(ResolveBase(op_idx), offset_sentinel, kLoadLikeType,
                    kLoadLikeSize, mutability);
  }

  OpIndex FindImpl(OpIndex object, int offset, uint32_t type_index,
                   uint8_t size, bool mutability,
                   OptionalOpIndex index = OptionalOpIndex::Nullopt()) {
    WasmMemoryAddress mem{object, offset, type_index, size, mutability};
    auto key = all_keys_.find(mem);
    if (key == all_keys_.end()) return OpIndex::Invalid();
    return Get(key->second);
  }

  void Insert(const StructSetOp& set) {
    OpIndex base = ResolveBase(set.object());
    int32_t offset = field_offset(set.type, set.field_index);
    uint8_t size = set.type->field(set.field_index).value_kind_size();
    bool mutability = set.type->mutability(set.field_index);
    Insert(base, offset, set.type_index, size, mutability, set.value());
  }

  void Insert(const StructGetOp& get, OpIndex get_idx) {
    OpIndex base = ResolveBase(get.object());
    int32_t offset = field_offset(get.type, get.field_index);
    uint8_t size = get.type->field(get.field_index).value_kind_size();
    bool mutability = get.type->mutability(get.field_index);
    Insert(base, offset, get.type_index, size, mutability, get_idx);
  }

  void InsertLoadLike(OpIndex base_idx, int offset_sentinel,
                      OpIndex value_idx) {
    OpIndex base = ResolveBase(base_idx);
    static constexpr bool mutability = false;
    Insert(base, offset_sentinel, kLoadLikeType, kLoadLikeSize, mutability,
           value_idx);
  }

#ifdef DEBUG
  void Print() {
    std::cout << "WasmMemoryContentTable:\n";
    for (const auto& base_keys : base_keys_) {
      for (Key key : base_keys.second.with_offsets) {
        std::cout << "  * " << key.data().mem.base << " - "
                  << key.data().mem.offset << " ==> " << Get(key) << "\n";
      }
    }
  }
#endif  // DEBUG

 private:
  void Insert(OpIndex base, int32_t offset, uint32_t type_index, uint8_t size,
              bool mutability, OpIndex value) {
    DCHECK_EQ(base, ResolveBase(base));

    WasmMemoryAddress mem{base, offset, type_index, size, mutability};
    auto existing_key = all_keys_.find(mem);
    if (existing_key != all_keys_.end()) {
      if (mutability) {
        Set(existing_key->second, value);
      } else {
        SetNoNotify(existing_key->second, value);
      }
      return;
    }

    // Creating a new key.
    Key key = NewKey({mem});
    all_keys_.insert({mem, key});
    if (mutability) {
      Set(key, value);
    } else {
      // Call `SetNoNotify` to avoid calls to `OnNewKey` and `OnValueChanged`.
      SetNoNotify(key, value);
    }
  }

 public:
  OpIndex ResolveBase(OpIndex base) {
    while (true) {
      if (replacements_[base] != OpIndex::Invalid()) {
        base = replacements_[base];
        continue;
      }
      Operation& op = graph_.Get(base);
      if (AssertNotNullOp* check = op.TryCast<AssertNotNullOp>()) {
        base = check->object();
        continue;
      }
      if (WasmTypeCastOp* cast = op.TryCast<WasmTypeCastOp>()) {
        base = cast->object();
        continue;
      }
      break;  // Terminate if nothing happened.
    }
    return base;
  }

  void AddKeyInBaseOffsetMaps(Key key) {
    // Inserting in {base_keys_}.
    OpIndex base = key.data().mem.base;
    auto base_keys = base_keys_.find(base);
    if (base_keys != base_keys_.end()) {
      base_keys->second.with_offsets.PushFront(key);
    } else {
      BaseData data;
      data.with_offsets.PushFront(key);
      base_keys_.insert({base, std::move(data)});
    }

    // Inserting in {offset_keys_}.
    int offset = key.data().mem.offset;
    auto offset_keys = offset_keys_.find(offset);
    if (offset_keys != offset_keys_.end()) {
      offset_keys->second.PushFront(key);
    } else {
      v8::base::DoublyThreadedList<Key, OffsetListTraits> list;
      list.PushFront(key);
      offset_keys_.insert({offset, std::move(list)});
    }
  }

  void RemoveKeyFromBaseOffsetMaps(Key key) {
    // Removing from {base_keys_}.
    v8::base::DoublyThreadedList<Key, BaseListTraits>::Remove(key);
    v8::base::DoublyThreadedList<Key, OffsetListTraits>::Remove(key);
  }

  SparseOpIndexSnapshotTable<bool>& non_aliasing_objects_;
  FixedOpIndexSidetable<OpIndex>& replacements_;

  PipelineData* data_;
  Graph& graph_;

  const wasm::WasmModule* module_ = data_->wasm_module();

  // TODO(dmercadier): consider using a faster datastructure than
  // ZoneUnorderedMap for {all_keys_}, {base_keys_} and {offset_keys_}.

  // A map containing all of the keys, for fast lookup of a specific
  // MemoryAddress.
  ZoneUnorderedMap<WasmMemoryAddress, Key> all_keys_;
  // Map from base OpIndex to keys associated with this base.
  ZoneUnorderedMap<OpIndex, BaseData> base_keys_;
  // Map from offsets to keys associated with this offset.
  ZoneUnorderedMap<int, v8::base::DoublyThreadedList<Key, OffsetListTraits>>
      offset_keys_;
};

}  // namespace wle

class WasmLoadEliminationAnalyzer {
 public:
  using AliasTable = SparseOpIndexSnapshotTable<bool>;
  using AliasKey = AliasTable::Key;
  using AliasSnapshot = AliasTable::Snapshot;

  using MemoryKey = wle::WasmMemoryContentTable::Key;
  using MemorySnapshot = wle::WasmMemoryContentTable::Snapshot;

  WasmLoadEliminationAnalyzer(PipelineData* data, Graph& graph,
                              Zone* phase_zone)
      : graph_(graph),
        phase_zone_(phase_zone),
        replacements_(graph.op_id_count(), phase_zone, &graph),
        non_aliasing_objects_(phase_zone),
        memory_(data, phase_zone, non_aliasing_objects_, replacements_, graph_),
        block_to_snapshot_mapping_(graph.block_count(), phase_zone),
        predecessor_alias_snapshots_(phase_zone),
        predecessor_memory_snapshots_(phase_zone) {}

  void Run() {
    LoopFinder loop_finder(phase_zone_, &graph_);
    AnalyzerIterator iterator(phase_zone_, graph_, loop_finder);

    bool compute_start_snapshot = true;
    while (iterator.HasNext()) {
      const Block* block = iterator.Next();

      ProcessBlock(*block, compute_start_snapshot);
      compute_start_snapshot = true;

      // Consider re-processing for loops.
      if (const GotoOp* last = block->LastOperation(graph_).TryCast<GotoOp>()) {
        if (last->destination->IsLoop() &&
            last->destination->LastPredecessor() == block) {
          const Block* loop_header = last->destination;
          // {block} is the backedge of a loop. We recompute the loop header's
          // initial snapshots, and if they differ from its original snapshot,
          // then we revisit the loop.
          if (BeginBlock<true>(loop_header)) {
            // We set the snapshot of the loop's 1st predecessor to the newly
            // computed snapshot. It's not quite correct, but this predecessor
            // is guaranteed to end with a Goto, and we are now visiting the
            // loop, which means that we don't really care about this
            // predecessor anymore.
            // The reason for saving this snapshot is to prevent infinite
            // looping, since the next time we reach this point, the backedge
            // snapshot could still invalidate things from the forward edge
            // snapshot. By restricting the forward edge snapshot, we prevent
            // this.
            const Block* loop_1st_pred =
                loop_header->LastPredecessor()->NeighboringPredecessor();
            FinishBlock(loop_1st_pred);
            // And we start a new fresh snapshot from this predecessor.
            auto pred_snapshots =
                block_to_snapshot_mapping_[loop_1st_pred->index()];
            non_aliasing_objects_.StartNewSnapshot(
                pred_snapshots->alias_snapshot);
            memory_.StartNewSnapshot(pred_snapshots->memory_snapshot);

            iterator.MarkLoopForRevisit();
            compute_start_snapshot = false;
          } else {
            SealAndDiscard();
          }
        }
      }
    }
  }

  OpIndex Replacement(OpIndex index) { return replacements_[index]; }

 private:
  void ProcessBlock(const Block& block, bool compute_start_snapshot);
  void ProcessStructGet(OpIndex op_idx, const StructGetOp& op);
  void ProcessStructSet(OpIndex op_idx, const StructSetOp& op);
  void ProcessArrayLength(OpIndex op_idx, const ArrayLengthOp& op);
  void ProcessWasmAllocateArray(OpIndex op_idx, const WasmAllocateArrayOp& op);
  void ProcessStringAsWtf16(OpIndex op_idx, const StringAsWtf16Op& op);
  void ProcessStringPrepareForGetCodeUnit(
      OpIndex op_idx, const StringPrepareForGetCodeUnitOp& op);
  void ProcessAnyConvertExtern(OpIndex op_idx, const AnyConvertExternOp& op);
  void ProcessAssertNotNull(OpIndex op_idx, const AssertNotNullOp& op);
  void ProcessAllocate(OpIndex op_idx, const AllocateOp& op);
  void ProcessCall(OpIndex op_idx, const CallOp& op);
  void ProcessPhi(OpIndex op_idx, const PhiOp& op);

  void DcheckWordBinop(OpIndex op_idx, const WordBinopOp& binop);

  // BeginBlock initializes the various SnapshotTables for {block}, and returns
  // true if {block} is a loop that should be revisited.
  template <bool for_loop_revisit = false>
  bool BeginBlock(const Block* block);
  void FinishBlock(const Block* block);
  // Seals the current snapshot, but discards it. This is used when considering
  // whether a loop should be revisited or not: we recompute the loop header's
  // snapshots, and then revisit the loop if the snapshots contain
  // modifications. If the snapshots are unchanged, we discard them and don't
  // revisit the loop.
  void SealAndDiscard();
  void StoreLoopSnapshotInForwardPredecessor(const Block& loop_header);

  // Returns true if the loop's backedge already has snapshot data (meaning that
  // it was already visited).
  bool BackedgeHasSnapshot(const Block& loop_header) const;

  void InvalidateAllNonAliasingInputs(const Operation& op);
  void InvalidateIfAlias(OpIndex op_idx);

  Graph& graph_;
  Zone* phase_zone_;

  FixedOpIndexSidetable<OpIndex> replacements_;

  AliasTable non_aliasing_objects_;
  wle::WasmMemoryContentTable memory_;

  struct Snapshot {
    AliasSnapshot alias_snapshot;
    MemorySnapshot memory_snapshot;
  };
  FixedBlockSidetable<std::optional<Snapshot>> block_to_snapshot_mapping_;

  // {predecessor_alias_napshots_}, {predecessor_maps_snapshots_} and
  // {predecessor_memory_snapshots_} are used as temporary vectors when starting
  // to process a block. We store them as members to avoid reallocation.
  ZoneVector<AliasSnapshot> predecessor_alias_snapshots_;
  ZoneVector<MemorySnapshot> predecessor_memory_snapshots_;
};

template <class Next>
class WasmLoadEliminationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmLoadElimination)

  void Analyze() {
    if (v8_flags.turboshaft_wasm_load_elimination) {
      DCHECK(AllowHandleDereference::IsAllowed());
      analyzer_.Run();
    }
    Next::Analyze();
  }

#define EMIT_OP(Name)                                                          \
  OpIndex REDUCE_INPUT_GRAPH(Name)(OpIndex ig_index, const Name##Op& op) {     \
    if (v8_flags.turboshaft_wasm_load_elimination) {                           \
      OpIndex ig_replacement_index = analyzer_.Replacement(ig_index);          \
      if (ig_replacement_index.valid()) {                                      \
        OpIndex replacement = Asm().MapToNewGraph(ig_replacement_index);       \
        return replacement;                                                    \
      }                                                                        \
    }                                                                          \
    return Next::ReduceInputGraph##Name(ig_index, op);                         \
  }

  EMIT_OP(StructGet)
  EMIT_OP(ArrayLength)
  EMIT_OP(StringAsWtf16)
  EMIT_OP(StringPrepareForGetCodeUnit)
  EMIT_OP(AnyConvertExtern)

  OpIndex REDUCE_INPUT_GRAPH(StructSet)(OpIndex ig_index,
                                        const StructSetOp& op) {
    if (v8_flags.turboshaft_wasm_load_elimination) {
      OpIndex ig_replacement_index = analyzer_.Replacement(ig_index);
      if (ig_replacement_index.valid()) {
        // For struct.set, "replace with itself" is a sentinel for
        // "unreachable", and those are the only replacements we schedule for
        // this operation.
        DCHECK_EQ(ig_replacement_index, ig_index);
        __ Unreachable();
        return OpIndex::Invalid();
      }
    }
    return Next::ReduceInputGraphStructSet(ig_index, op);
  }

 private:
  WasmLoadEliminationAnalyzer analyzer_{
      Asm().data(), Asm().modifiable_input_graph(), Asm().phase_zone()};
};

void WasmLoadEliminationAnalyzer::ProcessBlock(const Block& block,
                                               bool compute_start_snapshot) {
  if (compute_start_snapshot) {
    BeginBlock(&block);
  }
  if (block.IsLoop() && BackedgeHasSnapshot(block)) {
    // Update the associated snapshot for the forward edge with the merged
    // snapshot information from the forward- and backward edge.
    // This will make sure that when evaluating whether a loop needs to be
    // revisited, the inner loop compares the merged state with the backedge
    // preventing us from exponential revisits for loops where the backedge
    // invalidates loads which are eliminatable on the forward edge.
    StoreLoopSnapshotInForwardPredecessor(block);
  }

  for (OpIndex op_idx : graph_.OperationIndices(block)) {
    Operation& op = graph_.Get(op_idx);
    if (ShouldSkipOptimizationStep()) continue;
    if (ShouldSkipOperation(op)) continue;
    switch (op.opcode) {
      case Opcode::kStructGet:
        ProcessStructGet(op_idx, op.Cast<StructGetOp>());
        break;
      case Opcode::kStructSet:
        ProcessStructSet(op_idx, op.Cast<StructSetOp>());
        break;
      case Opcode::kArrayLength:
        ProcessArrayLength(op_idx, op.Cast<ArrayLengthOp>());
        break;
      case Opcode::kWasmAllocateArray:
        ProcessWasmAllocateArray(op_idx, op.Cast<WasmAllocateArrayOp>());
        break;
      case Opcode::kStringAsWtf16:
        ProcessStringAsWtf16(op_idx, op.Cast<StringAsWtf16Op>());
        break;
      case Opcode::kStringPrepareForGetCodeUnit:
        ProcessStringPrepareForGetCodeUnit(
            op_idx, op.Cast<StringPrepareForGetCodeUnitOp>());
        break;
      case Opcode::kAnyConvertExtern:
        ProcessAnyConvertExtern(op_idx, op.Cast<AnyConvertExternOp>());
        break;
      case Opcode::kAssertNotNull:
        // TODO(14108): We'll probably want to handle WasmTypeCast as
        // a "load-like" instruction too, to eliminate repeated casts.
        ProcessAssertNotNull(op_idx, op.Cast<AssertNotNullOp>());
        break;
      case Opcode::kArraySet:
        break;
      case Opcode::kAllocate:
        // Create new non-alias.
        ProcessAllocate(op_idx, op.Cast<AllocateOp>());
        break;
      case Opcode::kCall:
        // Invalidate state (+ maybe invalidate aliases).
        ProcessCall(op_idx, op.Cast<CallOp>());
        break;
      case Opcode::kPhi:
        // Invalidate aliases.
        ProcessPhi(op_idx, op.Cast<PhiOp>());
        break;
      case Opcode::kLoad:
        // Atomic loads have the "can_write" bit set, because they make
        // writes on other threads visible. At any rate, we have to
        // explicitly skip them here.
      case Opcode::kStore:
        // We rely on having no raw "Store" operations operating on Wasm
        // objects at this point in the pipeline.
        // TODO(jkummerow): Is there any way to DCHECK that?
      case Opcode::kAssumeMap:
      case Opcode::kCatchBlockBegin:
      case Opcode::kRetain:
      case Opcode::kDidntThrow:
      case Opcode::kCheckException:
      case Opcode::kAtomicRMW:
      case Opcode::kAtomicWord32Pair:
      case Opcode::kMemoryBarrier:
      case Opcode::kJSStackCheck:
      case Opcode::kWasmStackCheck:
      case Opcode::kSimd128LaneMemory:
      case Opcode::kGlobalSet:
      case Opcode::kParameter:
        // We explicitly break for those operations that have can_write effects
        // but don't actually write, or cannot interfere with load elimination.
        break;

      case Opcode::kWordBinop:
        // A WordBinop should never invalidate aliases (since the only time when
        // it should take a non-aliasing object as input is for Smi checks).
        DcheckWordBinop(op_idx, op.Cast<WordBinopOp>());
        break;

      case Opcode::kFrameState:
      case Opcode::kDeoptimizeIf:
      case Opcode::kComparison:
      case Opcode::kTrapIf:
        // We explicitly break for these opcodes so that we don't call
        // InvalidateAllNonAliasingInputs on their inputs, since they don't
        // really create aliases. (and also, they don't write so it's
        // fine to break)
        DCHECK(!op.Effects().can_write());
        break;

      case Opcode::kDeoptimize:
      case Opcode::kReturn:
        // We explicitly break for these opcodes so that we don't call
        // InvalidateAllNonAliasingInputs on their inputs, since they are block
        // terminators without successors, meaning that it's not useful for the
        // rest of the analysis to invalidate anything here.
        DCHECK(op.IsBlockTerminator() && SuccessorBlocks(op).empty());
        break;

      default:
        // Operations that `can_write` should invalidate the state. All such
        // operations should be already handled above, which means that we don't
        // need a `if (can_write) { Invalidate(); }` here.
        CHECK(!op.Effects().can_write());

        // Even if the operation doesn't write, it could create an alias to its
        // input by returning it. This happens for instance in Phis and in
        // Change (although ChangeOp is already handled earlier by calling
        // ProcessChange). We are conservative here by calling
        // InvalidateAllNonAliasingInputs for all operations even though only
        // few can actually create aliases to fresh allocations, the reason
        // being that missing such a case would be a security issue, and it
        // should be rare for fresh allocations to be used outside of
        // Call/Store/Load/Change anyways.
        InvalidateAllNonAliasingInputs(op);

        break;
    }
  }

  FinishBlock(&block);
}

// Returns true if replacing a load with a RegisterRepresentation
// {expected_reg_rep} and size {in_memory_size} with an
// operation with RegisterRepresentation {actual} is valid. For instance,
// replacing an operation that returns a Float64 by one that returns a Word64 is
// not valid. Similarly, replacing a Tagged with an untagged value is probably
// not valid because of the GC.

bool RepIsCompatible(RegisterRepresentation actual,
                     RegisterRepresentation expected_reg_repr,
                     uint8_t in_memory_size) {
  if (in_memory_size !=
      MemoryRepresentation::FromRegisterRepresentation(actual, true)
          .SizeInBytes()) {
    // The replacement was truncated when being stored or should be truncated
    // (or sign-extended) during the load. Since we don't have enough
    // truncations operators in Turboshaft (eg, we don't have Int32 to Int8
    // truncation), we just prevent load elimination in this case.

    // TODO(jkummerow): support eliminating repeated loads of the same i8/i16
    // field.
    return false;
  }

  return expected_reg_repr == actual;
}

void WasmLoadEliminationAnalyzer::ProcessStructGet(OpIndex op_idx,
                                                   const StructGetOp& get) {
  OpIndex existing = memory_.Find(get);
  if (existing.valid()) {
    const Operation& replacement = graph_.Get(existing);
    DCHECK_EQ(replacement.outputs_rep().size(), 1);
    DCHECK_EQ(get.outputs_rep().size(), 1);
    uint8_t size = get.type->field(get.field_index).value_kind_size();
    if (RepIsCompatible(replacement.outputs_rep()[0], get.outputs_rep()[0],
                        size)) {
      replacements_[op_idx] = existing;
      return;
    }
  }
  replacements_[op_idx] = OpIndex::Invalid();
  memory_.Insert(get, op_idx);
}

void WasmLoadEliminationAnalyzer::ProcessStructSet(OpIndex op_idx,
                                                   const StructSetOp& set) {
  if (memory_.HasValueWithIncorrectMutability(set)) {
    // This struct.set is unreachable. We don't have a good way to annotate
    // it as such, so we use "replace with itself" as a sentinel.
    // TODO(jkummerow): Check how often this case is triggered in practice.
    replacements_[op_idx] = op_idx;
    return;
  }

  memory_.Invalidate(set);
  memory_.Insert(set);

  // Updating aliases if the value stored was known as non-aliasing.
  OpIndex value = set.value();
  if (non_aliasing_objects_.HasKeyFor(value)) {
    non_aliasing_objects_.Set(value, false);
  }
}

void WasmLoadEliminationAnalyzer::ProcessArrayLength(
    OpIndex op_idx, const ArrayLengthOp& length) {
  static constexpr int offset = wle::kArrayLengthFieldIndex;
  OpIndex existing = memory_.FindLoadLike(length.array(), offset);
  if (existing.valid()) {
#if DEBUG
    const Operation& replacement = graph_.Get(existing);
    DCHECK_EQ(replacement.outputs_rep().size(), 1);
    DCHECK_EQ(length.outputs_rep().size(), 1);
    DCHECK_EQ(replacement.outputs_rep()[0], length.outputs_rep()[0]);
#endif
    replacements_[op_idx] = existing;
    return;
  }
  replacements_[op_idx] = OpIndex::Invalid();
  memory_.InsertLoadLike(length.array(), offset, op_idx);
}

void WasmLoadEliminationAnalyzer::ProcessWasmAllocateArray(
    OpIndex op_idx, const WasmAllocateArrayOp& alloc) {
  non_aliasing_objects_.Set(op_idx, true);
  static constexpr int offset = wle::kArrayLengthFieldIndex;
  memory_.InsertLoadLike(op_idx, offset, alloc.length());
}

void WasmLoadEliminationAnalyzer::ProcessStringAsWtf16(
    OpIndex op_idx, const StringAsWtf16Op& op) {
  static constexpr int offset = wle::kStringAsWtf16Index;
  OpIndex existing = memory_.FindLoadLike(op.string(), offset);
  if (existing.valid()) {
    DCHECK_EQ(Opcode::kStringAsWtf16, graph_.Get(existing).opcode);
    replacements_[op_idx] = existing;
    return;
  }
  replacements_[op_idx] = OpIndex::Invalid();
  memory_.InsertLoadLike(op.string(), offset, op_idx);
}

void WasmLoadEliminationAnalyzer::ProcessStringPrepareForGetCodeUnit(
    OpIndex op_idx, const StringPrepareForGetCodeUnitOp& prep) {
  static constexpr int offset = wle::kStringPrepareForGetCodeunitIndex;
  OpIndex existing = memory_.FindLoadLike(prep.string(), offset);
  if (existing.valid()) {
    DCHECK_EQ(Opcode::kStringPrepareForGetCodeUnit,
              graph_.Get(existing).opcode);
    replacements_[op_idx] = existing;
    return;
  }
  replacements_[op_idx] = OpIndex::Invalid();
  memory_.InsertLoadLike(prep.string(), offset, op_idx);
}

void WasmLoadEliminationAnalyzer::ProcessAnyConvertExtern(
    OpIndex op_idx, const AnyConvertExternOp& convert) {
  static constexpr int offset = wle::kAnyConvertExternIndex;
  OpIndex existing = memory_.FindLoadLike(convert.object(), offset);
  if (existing.valid()) {
    DCHECK_EQ(Opcode::kAnyConvertExtern, graph_.Get(existing).opcode);
    replacements_[op_idx] = existing;
    return;
  }
  replacements_[op_idx] = OpIndex::Invalid();
  memory_.InsertLoadLike(convert.object(), offset, op_idx);
}

void WasmLoadEliminationAnalyzer::ProcessAssertNotNull(
    OpIndex op_idx, const AssertNotNullOp& assert) {
  static constexpr int offset = wle::kAssertNotNullIndex;
  OpIndex existing = memory_.FindLoadLike(assert.object(), offset);
  if (existing.valid()) {
    DCHECK_EQ(Opcode::kAssertNotNull, graph_.Get(existing).opcode);
    replacements_[op_idx] = existing;
    return;
  }
  replacements_[op_idx] = OpIndex::Invalid();
  memory_.InsertLoadLike(assert.object(), offset, op_idx);
}

// Since we only loosely keep track of what can or can't alias, we assume that
// anything that was guaranteed to not alias with anything (because it's in
// {non_aliasing_objects_}) can alias with anything when coming back from the
// call if it was an argument of the call.
void WasmLoadEliminationAnalyzer::ProcessCall(OpIndex op_idx,
                                              const CallOp& op) {
  // Some builtins do not create aliases and do not invalidate existing
  // memory, and some even return fresh objects. For such cases, we don't
  // invalidate the state, and record the non-alias if any.
  if (!op.Effects().can_write()) {
    return;
  }
  // TODO(jkummerow): Add special handling to builtins that are known not to
  // have relevant side effects. Alternatively, specify their effects to not
  // include `CanWriteMemory()`.
#if 0
  if (auto builtin_id = TryGetBuiltinId(
          graph_.Get(op.callee()).TryCast<ConstantOp>(), broker_)) {
    switch (*builtin_id) {
      case Builtin::kExample:
        // This builtin touches no Wasm objects, and calls no other functions.
        return;
      default:
        break;
    }
  }
#endif
  // Not a builtin call, or not a builtin that we know doesn't invalidate
  // memory.

  InvalidateAllNonAliasingInputs(op);

  // The call could modify arbitrary memory, so we invalidate every
  // potentially-aliasing object.
  memory_.InvalidateMaybeAliasing();
}

void WasmLoadEliminationAnalyzer::InvalidateAllNonAliasingInputs(
    const Operation& op) {
  for (OpIndex input : op.inputs()) {
    InvalidateIfAlias(input);
  }
}

void WasmLoadEliminationAnalyzer::InvalidateIfAlias(OpIndex op_idx) {
  if (auto key = non_aliasing_objects_.TryGetKeyFor(op_idx);
      key.has_value() && non_aliasing_objects_.Get(*key)) {
    // An known non-aliasing object was passed as input to the Call; the Call
    // could create aliases, so we have to consider going forward that this
    // object could actually have aliases.
    non_aliasing_objects_.Set(*key, false);
  }
}

// The only time an Allocate should flow into a WordBinop is for Smi checks
// (which, by the way, should be removed by MachineOptimizationReducer (since
// Allocate never returns a Smi), but there is no guarantee that this happens
// before load elimination). So, there is no need to invalidate non-aliases, and
// we just DCHECK in this function that indeed, nothing else than a Smi check
// happens on non-aliasing objects.
void WasmLoadEliminationAnalyzer::DcheckWordBinop(OpIndex op_idx,
                                                  const WordBinopOp& binop) {
#ifdef DEBUG
  auto check = [&](V<Word> left, V<Word> right) {
    if (auto key = non_aliasing_objects_.TryGetKeyFor(left);
        key.has_value() && non_aliasing_objects_.Get(*key)) {
      int64_t cst;
      DCHECK_EQ(binop.kind, WordBinopOp::Kind::kBitwiseAnd);
      DCHECK(OperationMatcher(graph_).MatchSignedIntegralConstant(right, &cst));
      DCHECK_EQ(cst, kSmiTagMask);
    }
  };
  check(binop.left(), binop.right());
  check(binop.right(), binop.left());
#endif
}

void WasmLoadEliminationAnalyzer::ProcessAllocate(OpIndex op_idx,
                                                  const AllocateOp&) {
  // In particular, this handles {struct.new}.
  non_aliasing_objects_.Set(op_idx, true);
}

void WasmLoadEliminationAnalyzer::ProcessPhi(OpIndex op_idx, const PhiOp& phi) {
  InvalidateAllNonAliasingInputs(phi);

  base::Vector<const OpIndex> inputs = phi.inputs();
  // This copies some of the functionality of {RequiredOptimizationReducer}:
  // Phis whose inputs are all the same value can be replaced by that value.
  // We need to have this logic here because interleaving it with other cases
  // of load elimination can unlock further optimizations: simplifying Phis
  // can allow elimination of more loads, which can then allow simplification
  // of even more Phis.
  if (inputs.size() > 0) {
    bool same_inputs = true;
    OpIndex first = memory_.ResolveBase(inputs.first());
    for (const OpIndex& input : inputs.SubVectorFrom(1)) {
      if (memory_.ResolveBase(input) != first) {
        same_inputs = false;
        break;
      }
    }
    if (same_inputs) {
      replacements_[op_idx] = first;
    }
  }
}

void WasmLoadEliminationAnalyzer::FinishBlock(const Block* block) {
  block_to_snapshot_mapping_[block->index()] =
      Snapshot{non_aliasing_objects_.Seal(), memory_.Seal()};
}

void WasmLoadEliminationAnalyzer::SealAndDiscard() {
  non_aliasing_objects_.Seal();
  memory_.Seal();
}

void WasmLoadEliminationAnalyzer::StoreLoopSnapshotInForwardPredecessor(
    const Block& loop_header) {
  auto non_aliasing_snapshot = non_aliasing_objects_.Seal();
  auto memory_snapshot = memory_.Seal();

  block_to_snapshot_mapping_
      [loop_header.LastPredecessor()->NeighboringPredecessor()->index()] =
          Snapshot{non_aliasing_snapshot, memory_snapshot};

  non_aliasing_objects_.StartNewSnapshot(non_aliasing_snapshot);
  memory_.StartNewSnapshot(memory_snapshot);
}

bool WasmLoadEliminationAnalyzer::BackedgeHasSnapshot(
    const Block& loop_header) const {
  DCHECK(loop_header.IsLoop());
  return block_to_snapshot_mapping_[loop_header.LastPredecessor()->index()]
      .has_value();
}

template <bool for_loop_revisit>
bool WasmLoadEliminationAnalyzer::BeginBlock(const Block* block) {
  DCHECK_IMPLIES(
      for_loop_revisit,
      block->IsLoop() &&
          block_to_snapshot_mapping_[block->LastPredecessor()->index()]
              .has_value());

  // Collect the snapshots of all predecessors.
  {
    predecessor_alias_snapshots_.clear();
    predecessor_memory_snapshots_.clear();
    for (const Block* p : block->PredecessorsIterable()) {
      auto pred_snapshots = block_to_snapshot_mapping_[p->index()];
      // When we visit the loop for the first time, the loop header hasn't
      // been visited yet, so we ignore it.
      DCHECK_IMPLIES(!pred_snapshots.has_value(),
                     block->IsLoop() && block->LastPredecessor() == p);
      if (!pred_snapshots.has_value()) {
        DCHECK(!for_loop_revisit);
        continue;
      }
      // Note that the backedge snapshot of an inner loop in kFirstVisit will
      // also be taken into account if we are in the kSecondVisit of an outer
      // loop. The data in the backedge snapshot could be out-dated, but if it
      // is, then it's fine: if the backedge of the outer-loop was more
      // restrictive than its forward incoming edge, then the forward incoming
      // edge of the inner loop should reflect this restriction.
      predecessor_alias_snapshots_.push_back(pred_snapshots->alias_snapshot);
      predecessor_memory_snapshots_.push_back(pred_snapshots->memory_snapshot);
    }
  }

  // Note that predecessors are in reverse order, which means that the backedge
  // is at offset 0.
  constexpr int kBackedgeOffset = 0;
  constexpr int kForwardEdgeOffset = 1;

  bool loop_needs_revisit = false;
  // Start a new snapshot for this block by merging information from
  // predecessors.
  auto merge_aliases = [&](AliasKey key,
                           base::Vector<const bool> predecessors) -> bool {
    if (for_loop_revisit && predecessors[kForwardEdgeOffset] &&
        !predecessors[kBackedgeOffset]) {
      // The backedge doesn't think that {key} is no-alias, but the loop
      // header previously thought it was --> need to revisit.
      loop_needs_revisit = true;
    }
    return base::all_of(predecessors);
  };
  non_aliasing_objects_.StartNewSnapshot(
      base::VectorOf(predecessor_alias_snapshots_), merge_aliases);

  // Merging for {memory_} means setting values to Invalid unless all
  // predecessors have the same value.
  // TODO(dmercadier): we could insert of Phis during the pass to merge existing
  // information. This is a bit hard, because we are currently in an analyzer
  // rather than a reducer. Still, we could "prepare" the insertion now and then
  // really insert them during the Reduce phase of the CopyingPhase.
  auto merge_memory = [&](MemoryKey key,
                          base::Vector<const OpIndex> predecessors) -> OpIndex {
    if (for_loop_revisit && predecessors[kForwardEdgeOffset].valid() &&
        predecessors[kBackedgeOffset] != predecessors[kForwardEdgeOffset]) {
      // {key} had a value in the loop header, but the backedge and the forward
      // edge don't agree on its value, which means that the loop invalidated
      // some memory data, and thus needs to be revisited.
      loop_needs_revisit = true;
    }
    return base::all_equal(predecessors) ? predecessors[0] : OpIndex::Invalid();
  };
  memory_.StartNewSnapshot(base::VectorOf(predecessor_memory_snapshots_),
                           merge_memory);

  if (block->IsLoop()) return loop_needs_revisit;
  return false;
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_LOAD_ELIMINATION_REDUCER_H_
