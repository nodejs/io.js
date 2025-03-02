// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LATE_LOAD_ELIMINATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LATE_LOAD_ELIMINATION_REDUCER_H_

#include <optional>

#include "src/base/doubly-threaded-list.h"
#include "src/compiler/turboshaft/analyzer-iterator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/loop-finder.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table-opindex.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

#ifdef DEBUG
#define TRACE(x)                                    \
  do {                                              \
    if (v8_flags.turboshaft_trace_load_elimination) \
      StdoutStream() << x << std::endl;             \
  } while (false)
#else
#define TRACE(x)
#endif

// Design doc:
// https://docs.google.com/document/d/1AEl4dATNLu8GlLyUBQFXJoCxoAT5BeG7RCWxoEtIBJE/edit?usp=sharing

// Load Elimination removes redundant loads. Loads can be redundant because:
//
//   - they follow a store to the same address. For instance:
//
//       x.a = 42;
//       y = x.a;
//
//   - or, they follow the same load. For instance:
//
//       y = x.a;
//       z = x.a;
//
// The "annoying" part of load elimination is that object can alias, and stores
// to dynamically computed indices tend to invalidate the whole state. For
// instance, if we don't know anything about aliasing regarding `a` and `b`,
// then, in this situation:
//
//     x.a = 42
//     y.a = 25
//     z = x.a
//
// We can't load-eliminate `z = x.a`, since `y` could alias with `x`, and `y.a =
// 25` could have overwritten `x.a`. Similarly, if we have something like:
//
//     x[0] = 42
//     y[i] = 25
//     z = x[0]
//
// We can't load-eliminate `z = x[0]`, since `y` could alias with `x`, and
// `y[i]` thus have overwritten `x[0]`.
//
//
// Implementation:
//
//   - In a `MemoryContentTable` (a SnapshotTable), we keep track of known
//     memory values.
//     * When we visit a Store:
//       + if it's to a constant offset, we invalidate all of the known values
//         at the same offset (for all bases).
//       + if it's to a dynamic index, we invalidate everything (because things
//         could alias).
//       We then update the known value at the address of the store.
//     * When we visit a Call, we invalidate everything (since the function
//       called could change any memory through aliases).
//     * When we visit a Load:
//       + if there is a known value at the address, we replace the Load by this
//         value.
//       + otherwise, the result of the Load becomes the known value at the load
//         address.
//
//   - We keep track (using a SparseOpIndexSnapshotTable) of some objects that
//     are known to not alias with anything: freshly allocated objects, until
//     they are passed to a function call, stored in an object, or flow in a
//     Phi. When storing in a fresh object, we only need to invalidate things in
//     the same object, leaving the rest of the state untouched. When storing in
//     a non-fresh object, we don't invalidate the state for fresh objects.
//
//   - We keep track (using a SparseOpIndexSnapshotTable) of the maps of some
//     objects (which we get from AssumeMap operations, which are inserted when
//     lowering CheckMaps). We use them to know if some objects can alias or
//     not: 2 objects with different maps cannot alias.
//
//   - When a loop contains a Store or a Call, it could invalidate previously
//     eliminated loads in the beginning of the loop. Thus, once we reach the
//     end of a loop, we recompute the header's snapshot using {header,
//     backedge} as predecessors, and if anything is invalidated by the
//     backedge, we revisit the loop.
//
// How we "keep track" of objects:
//
// We need the following operation:
//     1. Load the value for a {base, index, offset}.
//     2. Store that {base, index, offset} = value
//     3. Invalidate everything at a given offset + everything at an index (for
//        when storing to a base that could alias with other things).
//     4. Invalidate everything in a base (for when said base is passed to a
//        function, or when there is an indexed store in this base).
//     5. Invalidate everything (for an indexed store into an arbitrary base)
//
// To have 1. in constant time, we maintain a global hashmap (`all_keys`) from
// MemoryAddress (= {base, index, offset, element_size_log2, size}) to Keys, and
// from these Keys, we have constant-time lookup in the SnapshotTable.
// To have 3. efficiently, we maintain a Map from offsets to lists of every
// MemoryAddress at this offset (`offset_keys_`).
// To have 4. efficiently, we have a similar map from bases to lists of every
// MemoryAddress at this base (`base_keys_`).
// For 5., we can use either `offset_keys_` or `base_keys_`. In practice, we use
// the latter because it allows us to efficiently skip bases that are known to
// have no aliases.

// MapMask and related functions are an attempt to avoid having to store sets of
// maps for each AssumeMap that we encounter by compressing all of the maps into
// a single uint64_t.
//
// For each object, we keep in a MapMaskAndOr the "minimum" and "maximum" of
// all of its potential maps, where
//   - "or_" is computed using the union (logical or) of all of its potential
//     maps.
//   - "and_" is computed using the intersection (logical and) of all of its
//     potential maps.
//
// Then, given two objects A and B, if A.and_ has a bit set that isn't set in
// B.or_, it means that all of the maps of A have a bit that none of the maps of
// B have, ie, A and B are guaranteed to not have a map in common.
using MapMask = uint64_t;
struct MapMaskAndOr {
  MapMask or_ = 0;
  MapMask and_ = -1ull;

  bool operator==(const MapMaskAndOr& other) const {
    return or_ == other.or_ && and_ == other.and_;
  }

  bool operator!=(const MapMaskAndOr& other) const { return !(*this == other); }
};
inline bool is_empty(MapMaskAndOr minmax) {
  return minmax.or_ == 0 && minmax.and_ == -1ull;
}
inline MapMask ComputeMapHash(MapRef map) {
  // `map.hash_value()` is probably not a good enough hash, since most user maps
  // will have the same upper bits, so we re-hash. We're using xorshift64* (from
  // "An experimental exploration of Marsaglia’s xorshift generators, scrambled"
  // by Vigna in ACM Transactions on Mathematical Software, Volume 42).
  MapMask hash = map.hash_value();
  hash ^= hash >> 12;
  hash ^= hash << 25;
  hash ^= hash >> 27;
  return hash * 0x2545f4914f6cdd1d;
}
inline MapMaskAndOr ComputeMinMaxHash(ZoneRefSet<Map> maps) {
  MapMaskAndOr minmax;
  for (MapRef map : maps) {
    MapMask hash = ComputeMapHash(map);
    minmax.or_ |= hash;
    minmax.and_ &= hash;
  }
  return minmax;
}
inline MapMaskAndOr CombineMinMax(MapMaskAndOr a, MapMaskAndOr b) {
  return {a.or_ | b.or_, a.and_ & b.and_};
}
// Returns true if {a} and {b} could have a map in common.
inline bool CouldHaveSameMap(MapMaskAndOr a, MapMaskAndOr b) {
  return ((a.and_ & b.or_) == a.and_) || ((b.and_ & a.or_) == b.and_);
}

struct MemoryAddress {
  OpIndex base;
  OptionalOpIndex index;
  int32_t offset;
  uint8_t element_size_log2;
  uint8_t size;

  bool operator==(const MemoryAddress& other) const {
    return base == other.base && index == other.index &&
           offset == other.offset &&
           element_size_log2 == other.element_size_log2 && size == other.size;
  }

  template <typename H>
  friend H AbslHashValue(H h, const MemoryAddress& mem) {
    return H::combine(std::move(h), mem.base, mem.index, mem.offset,
                      mem.element_size_log2, mem.size);
  }
};
std::ostream& operator<<(std::ostream& os, const MemoryAddress& mem);

inline size_t hash_value(MemoryAddress const& mem) {
  return fast_hash_combine(mem.base, mem.index, mem.offset,
                           mem.element_size_log2, mem.size);
}

struct KeyData {
  using Key = SnapshotTableKey<OpIndex, KeyData>;
  MemoryAddress mem;
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
  // List of every value at this base that has a valid index.
  v8::base::DoublyThreadedList<Key, BaseListTraits> with_indices;
};

class LoadEliminationReplacement {
 public:
  enum class Kind {
    kNone,             // We don't replace the operation
    kLoadElimination,  // We load eliminate a load operation
    // The following replacements are used for the special case optimization:
    // TruncateWord64ToWord32(
    //     BitcastTaggedToWordPtrForTagAndSmiBits(Load(x, Tagged)))
    // =>
    // Load(x, Int32)
    //
    kTaggedLoadToInt32Load,     // Turn a tagged load into a direct int32 load.
    kTaggedBitcastElimination,  // Remove this (now unused) bitcast.
    kInt32TruncationElimination,  // Replace truncation by the updated load.
  };

  LoadEliminationReplacement() : kind_(Kind::kNone), replacement_() {}

  static LoadEliminationReplacement None() {
    return LoadEliminationReplacement{};
  }
  static LoadEliminationReplacement LoadElimination(OpIndex replacement) {
    DCHECK(replacement.valid());
    return LoadEliminationReplacement{Kind::kLoadElimination, replacement};
  }
  static LoadEliminationReplacement TaggedLoadToInt32Load() {
    return LoadEliminationReplacement{Kind::kTaggedLoadToInt32Load, {}};
  }
  static LoadEliminationReplacement TaggedBitcastElimination() {
    return LoadEliminationReplacement{Kind::kTaggedBitcastElimination, {}};
  }
  static LoadEliminationReplacement Int32TruncationElimination(
      OpIndex replacement) {
    return LoadEliminationReplacement{Kind::kInt32TruncationElimination,
                                      replacement};
  }

  bool IsNone() const { return kind_ == Kind::kNone; }
  bool IsLoadElimination() const { return kind_ == Kind::kLoadElimination; }
  bool IsTaggedLoadToInt32Load() const {
    return kind_ == Kind::kTaggedLoadToInt32Load;
  }
  bool IsTaggedBitcastElimination() const {
    return kind_ == Kind::kTaggedBitcastElimination;
  }
  bool IsInt32TruncationElimination() const {
    return kind_ == Kind::kInt32TruncationElimination;
  }
  OpIndex replacement() const { return replacement_; }

 private:
  LoadEliminationReplacement(Kind kind, OpIndex replacement)
      : kind_(kind), replacement_(replacement) {}

  Kind kind_;
  OpIndex replacement_;
};

V8_EXPORT_PRIVATE bool IsInt32TruncatedLoadPattern(
    const Graph& graph, OpIndex change_idx, const ChangeOp& change,
    OpIndex* bitcast_idx = nullptr, OpIndex* load_idx = nullptr);

class MemoryContentTable
    : public ChangeTrackingSnapshotTable<MemoryContentTable, OpIndex, KeyData> {
 public:
  using Replacement = LoadEliminationReplacement;
  explicit MemoryContentTable(
      Zone* zone, SparseOpIndexSnapshotTable<bool>& non_aliasing_objects,
      SparseOpIndexSnapshotTable<MapMaskAndOr>& object_maps,
      FixedOpIndexSidetable<Replacement>& replacements)
      : ChangeTrackingSnapshotTable(zone),
        non_aliasing_objects_(non_aliasing_objects),
        object_maps_(object_maps),
        replacements_(replacements),
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

  // Invalidate all previous known memory that could alias with {store}.
  void Invalidate(const StoreOp& store) {
    Invalidate(store.base(), store.index(), store.offset);
  }

  void Invalidate(OpIndex base, OptionalOpIndex index, int32_t offset) {
    TRACE("> MemoryContentTable: Invalidating based on "
          << base << ", " << index << ", " << offset);
    base = ResolveBase(base);

    if (non_aliasing_objects_.Get(base)) {
      TRACE(">> base is non-aliasing");
      // Since {base} is non-aliasing, it's enough to just iterate the values at
      // this base.
      auto base_keys = base_keys_.find(base);
      if (base_keys == base_keys_.end()) return;
      for (auto it = base_keys->second.with_offsets.begin();
           it != base_keys->second.with_offsets.end();) {
        Key key = *it;
        DCHECK_EQ(key.data().mem.base, base);
        DCHECK(!key.data().mem.index.valid());
        if (index.valid() || offset == key.data().mem.offset) {
          // Overwrites {key}.
          it = base_keys->second.with_offsets.RemoveAt(it);
          TRACE(">>> invalidating " << key.data().mem);
          Set(key, OpIndex::Invalid());
        } else {
          ++it;
        }
      }
      // Invalidating all of the value with valid Index at base {base}.
      for (auto it = base_keys->second.with_indices.begin();
           it != base_keys->second.with_indices.end();) {
        Key key = *it;
        DCHECK(key.data().mem.index.valid());
        it = base_keys->second.with_indices.RemoveAt(it);
        Set(key, OpIndex::Invalid());
      }
    } else {
      TRACE(">> base is maybe-aliasing");
      // {base} could alias with other things, so we iterate the whole state.
      if (index.valid()) {
        // {index} could be anything, so we invalidate everything.
        TRACE(">> Invalidating everything because of valid index");
        return InvalidateMaybeAliasing();
      }

      // Invalidating all of the values with valid Index.
      // TODO(dmercadier): we could keep keys that don't alias here, but that
      // would require doing a map lookup on the base of each key. A better
      // alternative would probably be to have 2 {non_alias_index_keys_} and
      // {maybe_alias_index_keys_} tables instead of just {index_keys_}. This
      // has the downside that when a base stops being non-alias, all of its
      // indexed memory cells have to be moved. This could be worked around by
      // having these 2 tables contain BaseData.with_indices values instead of
      // Keys, so that a whole BaseData.with_indices can be removed in a single
      // operation from the global {non_alias_index_keys_}.
      for (auto it = index_keys_.begin(); it != index_keys_.end();) {
        Key key = *it;
        it = index_keys_.RemoveAt(it);
        TRACE(">>> Invalidating indexed memory " << key.data().mem);
        Set(key, OpIndex::Invalid());
      }

      TRACE(">>> Invalidating everything maybe-aliasing at offset " << offset);
      InvalidateAtOffset(offset, base);
    }
  }

  // Invalidates all Keys that are not known as non-aliasing.
  void InvalidateMaybeAliasing() {
    TRACE(">> InvalidateMaybeAliasing");
    // We find current active keys through {base_keys_} so that we can bail out
    // for whole buckets non-aliasing bases (if we had gone through
    // {offset_keys_} instead, then for each key we would've had to check
    // whether it was non-aliasing or not).
    for (auto& base_keys : base_keys_) {
      OpIndex base = base_keys.first;
      if (non_aliasing_objects_.Get(base)) continue;
      for (auto it = base_keys.second.with_offsets.begin();
           it != base_keys.second.with_offsets.end();) {
        Key key = *it;
        // It's important to remove with RemoveAt before Setting the key to
        // invalid, otherwise OnKeyChange will remove {key} from {base_keys},
        // which will invalidate {it}.
        it = base_keys.second.with_offsets.RemoveAt(it);
        TRACE(">>> Invalidating " << key.data().mem);
        Set(key, OpIndex::Invalid());
      }
      for (auto it = base_keys.second.with_indices.begin();
           it != base_keys.second.with_indices.end();) {
        Key key = *it;
        it = base_keys.second.with_indices.RemoveAt(it);
        TRACE(">>> Invalidating " << key.data().mem);
        Set(key, OpIndex::Invalid());
      }
    }
  }

  OpIndex Find(const LoadOp& load) {
    OpIndex base = ResolveBase(load.base());
    OptionalOpIndex index = load.index();
    int32_t offset = load.offset;
    uint8_t element_size_log2 = index.valid() ? load.element_size_log2 : 0;
    uint8_t size = load.loaded_rep.SizeInBytes();

    MemoryAddress mem{base, index, offset, element_size_log2, size};
    auto key = all_keys_.find(mem);
    if (key == all_keys_.end()) return OpIndex::Invalid();
    return Get(key->second);
  }

  void Insert(const StoreOp& store) {
    OpIndex base = ResolveBase(store.base());
    OptionalOpIndex index = store.index();
    int32_t offset = store.offset;
    uint8_t element_size_log2 = index.valid() ? store.element_size_log2 : 0;
    OpIndex value = store.value();
    uint8_t size = store.stored_rep.SizeInBytes();

    if (store.kind.is_immutable) {
      InsertImmutable(base, index, offset, element_size_log2, size, value);
    } else {
      Insert(base, index, offset, element_size_log2, size, value);
    }
  }

  void Insert(const LoadOp& load, OpIndex load_idx) {
    OpIndex base = ResolveBase(load.base());
    OptionalOpIndex index = load.index();
    int32_t offset = load.offset;
    uint8_t element_size_log2 = index.valid() ? load.element_size_log2 : 0;
    uint8_t size = load.loaded_rep.SizeInBytes();

    if (load.kind.is_immutable) {
      InsertImmutable(base, index, offset, element_size_log2, size, load_idx);
    } else {
      Insert(base, index, offset, element_size_log2, size, load_idx);
    }
  }

#ifdef DEBUG
  void Print() {
    std::cout << "MemoryContentTable:\n";
    for (const auto& base_keys : base_keys_) {
      for (Key key : base_keys.second.with_offsets) {
        std::cout << "  * " << key.data().mem.base << " - "
                  << key.data().mem.index << " - " << key.data().mem.offset
                  << " - " << key.data().mem.element_size_log2 << " ==> "
                  << Get(key) << "\n";
      }
      for (Key key : base_keys.second.with_indices) {
        std::cout << "  * " << key.data().mem.base << " - "
                  << key.data().mem.index << " - " << key.data().mem.offset
                  << " - " << key.data().mem.element_size_log2 << " ==> "
                  << Get(key) << "\n";
      }
    }
  }
#endif

 private:
  // To avoid pathological execution times, we cap the maximum number of
  // keys we track. This is safe, because *not* tracking objects (even
  // though we could) only makes us miss out on possible optimizations.
  // TODO(dmercadier/jkummerow): Find a more elegant solution to keep
  // execution time in check. One example of a test case can be found in
  // crbug.com/v8/14370.
  static constexpr size_t kMaxKeys = 10000;

  void Insert(OpIndex base, OptionalOpIndex index, int32_t offset,
              uint8_t element_size_log2, uint8_t size, OpIndex value) {
    DCHECK_EQ(base, ResolveBase(base));

    MemoryAddress mem{base, index, offset, element_size_log2, size};
    TRACE("> MemoryContentTable: will insert " << mem
                                               << " with value=" << value);
    auto existing_key = all_keys_.find(mem);
    if (existing_key != all_keys_.end()) {
      TRACE(">> Reusing existing key");
      Set(existing_key->second, value);
      return;
    }

    if (all_keys_.size() > kMaxKeys) {
      TRACE(">> Bailing out because too many keys");
      return;
    }

    // Creating a new key.
    Key key = NewKey({mem});
    all_keys_.insert({mem, key});
    Set(key, value);
  }

  void InsertImmutable(OpIndex base, OptionalOpIndex index, int32_t offset,
                       uint8_t element_size_log2, uint8_t size, OpIndex value) {
    DCHECK_EQ(base, ResolveBase(base));

    MemoryAddress mem{base, index, offset, element_size_log2, size};
    TRACE("> MemoryContentTable: will insert immutable "
          << mem << " with value=" << value);
    auto existing_key = all_keys_.find(mem);
    if (existing_key != all_keys_.end()) {
      TRACE(">> Reusing existing key");
      SetNoNotify(existing_key->second, value);
      return;
    }

    if (all_keys_.size() > kMaxKeys) {
      TRACE(">> Bailing out because too many keys");
      return;
    }

    // Creating a new key.
    Key key = NewKey({mem});
    all_keys_.insert({mem, key});
    // Call `SetNoNotify` to avoid calls to `OnNewKey` and `OnValueChanged`.
    SetNoNotify(key, value);
  }

  void InvalidateAtOffset(int32_t offset, OpIndex base) {
    MapMaskAndOr base_maps = object_maps_.Get(base);
    auto offset_keys = offset_keys_.find(offset);
    if (offset_keys == offset_keys_.end()) return;
    for (auto it = offset_keys->second.begin();
         it != offset_keys->second.end();) {
      Key key = *it;
      DCHECK_EQ(offset, key.data().mem.offset);
      // It can overwrite previous stores to any base (except non-aliasing
      // ones).
      if (non_aliasing_objects_.Get(key.data().mem.base)) {
        ++it;
        continue;
      }
      MapMaskAndOr this_maps = key.data().mem.base == base
                                   ? base_maps
                                   : object_maps_.Get(key.data().mem.base);
      if (!is_empty(base_maps) && !is_empty(this_maps) &&
          !CouldHaveSameMap(base_maps, this_maps)) {
        TRACE(">>>> InvalidateAtOffset: not invalidating thanks for maps: "
              << key.data().mem);
        ++it;
        continue;
      }
      it = offset_keys->second.RemoveAt(it);
      TRACE(">>>> InvalidateAtOffset: invalidating " << key.data().mem);
      Set(key, OpIndex::Invalid());
    }
  }

  OpIndex ResolveBase(OpIndex base) {
    while (replacements_[base].IsLoadElimination()) {
      base = replacements_[base].replacement();
    }
    return base;
  }

  void AddKeyInBaseOffsetMaps(Key key) {
    // Inserting in {base_keys_}.
    OpIndex base = key.data().mem.base;
    auto base_keys = base_keys_.find(base);
    if (base_keys != base_keys_.end()) {
      if (key.data().mem.index.valid()) {
        base_keys->second.with_indices.PushFront(key);
      } else {
        base_keys->second.with_offsets.PushFront(key);
      }
    } else {
      BaseData data;
      if (key.data().mem.index.valid()) {
        data.with_indices.PushFront(key);
      } else {
        data.with_offsets.PushFront(key);
      }
      base_keys_.insert({base, std::move(data)});
    }

    if (key.data().mem.index.valid()) {
      // Inserting in {index_keys_}.
      index_keys_.PushFront(key);
    } else {
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
  }

  void RemoveKeyFromBaseOffsetMaps(Key key) {
    // Removing from {base_keys_}.
    v8::base::DoublyThreadedList<Key, BaseListTraits>::Remove(key);
    v8::base::DoublyThreadedList<Key, OffsetListTraits>::Remove(key);
  }

  SparseOpIndexSnapshotTable<bool>& non_aliasing_objects_;
  SparseOpIndexSnapshotTable<MapMaskAndOr>& object_maps_;
  FixedOpIndexSidetable<Replacement>& replacements_;

  // A map containing all of the keys, for fast lookup of a specific
  // MemoryAddress.
  ZoneAbslFlatHashMap<MemoryAddress, Key> all_keys_;
  // Map from base OpIndex to keys associated with this base.
  ZoneAbslFlatHashMap<OpIndex, BaseData> base_keys_;
  // Map from offsets to keys associated with this offset.
  ZoneAbslFlatHashMap<int, v8::base::DoublyThreadedList<Key, OffsetListTraits>>
      offset_keys_;

  // List of all of the keys that have a valid index.
  v8::base::DoublyThreadedList<Key, OffsetListTraits> index_keys_;
};

class V8_EXPORT_PRIVATE LateLoadEliminationAnalyzer {
 public:
  using AliasTable = SparseOpIndexSnapshotTable<bool>;
  using AliasKey = AliasTable::Key;
  using AliasSnapshot = AliasTable::Snapshot;

  using MapTable = SparseOpIndexSnapshotTable<MapMaskAndOr>;
  using MapKey = MapTable::Key;
  using MapSnapshot = MapTable::Snapshot;

  using MemoryKey = MemoryContentTable::Key;
  using MemorySnapshot = MemoryContentTable::Snapshot;

  using Replacement = LoadEliminationReplacement;

  enum class RawBaseAssumption {
    kNoInnerPointer,
    kMaybeInnerPointer,
  };

  LateLoadEliminationAnalyzer(PipelineData* data, Graph& graph,
                              Zone* phase_zone, JSHeapBroker* broker,
                              RawBaseAssumption raw_base_assumption)
      : data_(data),
        graph_(graph),
        phase_zone_(phase_zone),
        broker_(broker),
        raw_base_assumption_(raw_base_assumption),
        replacements_(graph.op_id_count(), phase_zone, &graph),
        non_aliasing_objects_(phase_zone),
        object_maps_(phase_zone),
        memory_(phase_zone, non_aliasing_objects_, object_maps_, replacements_),
        block_to_snapshot_mapping_(graph.block_count(), phase_zone),
        predecessor_alias_snapshots_(phase_zone),
        predecessor_maps_snapshots_(phase_zone),
        predecessor_memory_snapshots_(phase_zone) {
    USE(data_);
  }

  void Run();

  Replacement GetReplacement(OpIndex index) { return replacements_[index]; }

 private:
  void ProcessBlock(const Block& block, bool compute_start_snapshot);
  void ProcessLoad(OpIndex op_idx, const LoadOp& op);
  void ProcessStore(OpIndex op_idx, const StoreOp& op);
  void ProcessAllocate(OpIndex op_idx, const AllocateOp& op);
  void ProcessCall(OpIndex op_idx, const CallOp& op);
  void ProcessAssumeMap(OpIndex op_idx, const AssumeMapOp& op);
  void ProcessChange(OpIndex op_idx, const ChangeOp& change);

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

  PipelineData* data_;
  Graph& graph_;
  Zone* phase_zone_;
  JSHeapBroker* broker_;
  RawBaseAssumption raw_base_assumption_;

#if V8_ENABLE_WEBASSEMBLY
  bool is_wasm_ = data_->is_wasm();
#endif

  FixedOpIndexSidetable<Replacement> replacements_;
  // We map: Load-index -> Change-index -> Bitcast-index
  std::map<OpIndex, base::SmallMap<std::map<OpIndex, OpIndex>, 4>>
      int32_truncated_loads_;

  // TODO(dmercadier): {non_aliasing_objects_} tends to be weak for
  // backing-stores, because they are often stored into an object right after
  // being created, and often don't have other aliases throughout their
  // lifetime. It would be more useful to have a more precise tracking of
  // aliases. Storing a non-aliasing object into a potentially-aliasing one
  // probably always means that the former becomes potentially-aliasing.
  // However, storing a non-aliasing object into another non-aliasing object
  // should be reasonably not-too-hard to track.
  AliasTable non_aliasing_objects_;
  MapTable object_maps_;
  MemoryContentTable memory_;

  struct Snapshot {
    AliasSnapshot alias_snapshot;
    MapSnapshot maps_snapshot;
    MemorySnapshot memory_snapshot;
  };
  FixedBlockSidetable<std::optional<Snapshot>> block_to_snapshot_mapping_;

  // {predecessor_alias_napshots_}, {predecessor_maps_snapshots_} and
  // {predecessor_memory_snapshots_} are used as temporary vectors when starting
  // to process a block. We store them as members to avoid reallocation.
  ZoneVector<AliasSnapshot> predecessor_alias_snapshots_;
  ZoneVector<MapSnapshot> predecessor_maps_snapshots_;
  ZoneVector<MemorySnapshot> predecessor_memory_snapshots_;
};

template <class Next>
class V8_EXPORT_PRIVATE LateLoadEliminationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(LateLoadElimination)
  using Replacement = LoadEliminationReplacement;

  void Analyze() {
    if (is_wasm_ || v8_flags.turboshaft_load_elimination) {
      DCHECK(AllowHandleDereference::IsAllowed());
      analyzer_.Run();
    }
    Next::Analyze();
  }

  OpIndex REDUCE_INPUT_GRAPH(Load)(OpIndex ig_index, const LoadOp& load) {
    if (is_wasm_ || v8_flags.turboshaft_load_elimination) {
      Replacement replacement = analyzer_.GetReplacement(ig_index);
      if (replacement.IsLoadElimination()) {
        OpIndex replacement_ig_index = replacement.replacement();
        OpIndex replacement_idx = Asm().MapToNewGraph(replacement_ig_index);
        // The replacement might itself be a load that int32-truncated.
        if (analyzer_.GetReplacement(replacement_ig_index)
                .IsTaggedLoadToInt32Load()) {
          DCHECK_EQ(Asm().output_graph().Get(replacement_idx).outputs_rep()[0],
                    RegisterRepresentation::Word32());
        } else {
          DCHECK(Asm()
                     .output_graph()
                     .Get(replacement_idx)
                     .outputs_rep()[0]
                     .AllowImplicitRepresentationChangeTo(
                         load.outputs_rep()[0],
                         Asm().output_graph().IsCreatedFromTurbofan()));
        }
        return replacement_idx;
      } else if (replacement.IsTaggedLoadToInt32Load()) {
        auto loaded_rep = load.loaded_rep;
        auto result_rep = load.result_rep;
        DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
        loaded_rep = MemoryRepresentation::Int32();
        result_rep = RegisterRepresentation::Word32();
        return Asm().Load(Asm().MapToNewGraph(load.base()),
                          Asm().MapToNewGraph(load.index()), load.kind,
                          loaded_rep, result_rep, load.offset,
                          load.element_size_log2);
      }
    }
    return Next::ReduceInputGraphLoad(ig_index, load);
  }

  OpIndex REDUCE_INPUT_GRAPH(Change)(OpIndex ig_index, const ChangeOp& change) {
    if (is_wasm_ || v8_flags.turboshaft_load_elimination) {
      Replacement replacement = analyzer_.GetReplacement(ig_index);
      if (replacement.IsInt32TruncationElimination()) {
        DCHECK(
            IsInt32TruncatedLoadPattern(Asm().input_graph(), ig_index, change));
        return Asm().MapToNewGraph(replacement.replacement());
      }
    }
    return Next::ReduceInputGraphChange(ig_index, change);
  }

  OpIndex REDUCE_INPUT_GRAPH(TaggedBitcast)(OpIndex ig_index,
                                            const TaggedBitcastOp& bitcast) {
    if (is_wasm_ || v8_flags.turboshaft_load_elimination) {
      Replacement replacement = analyzer_.GetReplacement(ig_index);
      if (replacement.IsTaggedBitcastElimination()) {
        return OpIndex::Invalid();
      }
    }
    return Next::ReduceInputGraphTaggedBitcast(ig_index, bitcast);
  }

  V<None> REDUCE(AssumeMap)(V<HeapObject>, ZoneRefSet<Map>) {
    // AssumeMaps are currently not used after Load Elimination. We thus remove
    // them now. If they ever become needed for later optimizations, we could
    // consider leaving them in the graph and just ignoring them in the
    // Instruction Selector.
    return {};
  }

 private:
  const bool is_wasm_ = __ data() -> is_wasm();
  using RawBaseAssumption = LateLoadEliminationAnalyzer::RawBaseAssumption;
  RawBaseAssumption raw_base_assumption_ =
      __ data() -> pipeline_kind() == TurboshaftPipelineKind::kCSA
          ? RawBaseAssumption::kMaybeInnerPointer
          : RawBaseAssumption::kNoInnerPointer;
  LateLoadEliminationAnalyzer analyzer_{__ data(), __ modifiable_input_graph(),
                                        __ phase_zone(), __ data()->broker(),
                                        raw_base_assumption_};
};

#undef TRACE

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LATE_LOAD_ELIMINATION_REDUCER_H_
