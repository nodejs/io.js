// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LAYERED_HASH_MAP_H_
#define V8_COMPILER_TURBOSHAFT_LAYERED_HASH_MAP_H_

#include <cstddef>
#include <iostream>
#include <limits>
#include <optional>

#include "src/base/bits.h"
#include "src/compiler/turboshaft/fast-hash.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

// LayeredHashMap is a hash map whose elements are groupped into layers, such
// that it's efficient to remove all of the items from the last inserted layer.
// In addition to the regular Insert/Get/Contains functions of hash maps, it
// thus provides two additional functions: StartLayer to indicate that future
// insertions are part of a new layer, and DropLastLayer to remove all of the
// items of the last layer.
//
// LayeredHashMap does not support inserting multiple values with the same key,
// and does not support updating already-inserted items in the map. If you need
// to update an existing key, you'll need to remove it (by calling DropLastLayer
// as many times as needed), and then re-insert it.
//
// The implementation uses a regular ZoneVector for the main hash table, while
// keeping a linked list of items per layer. When inserting an item in the
// LayeredHashMap, we insert it into the ZoneVector and link it to the linked
// list of the current (=latest) layer. In order to remove all of the items from
// the last layer, we iterate its linked list, and remove the items one by one
// from the ZoneVector, after which we drop the linked list alltogether.

template <class Key, class Value>
class LayeredHashMap {
 public:
  explicit LayeredHashMap(Zone* zone, uint32_t initial_capacity = 64);

  void StartLayer();
  void DropLastLayer();

  void InsertNewKey(Key key, Value value);
  bool Contains(Key key);
  std::optional<Value> Get(Key key);

 private:
  struct Entry {
    size_t hash = 0;
    Key key = Key();
    Value value = Value();
    Entry* depth_neighboring_entry = nullptr;
  };
  void ResizeIfNeeded();
  size_t NextEntryIndex(size_t index) { return (index + 1) & mask_; }
  Entry* FindEntryForKey(Key key, size_t hash = 0);
  Entry* InsertEntry(Entry entry);

  size_t ComputeHash(Key key) {
    size_t hash = fast_hash<Key>()(key);
    return V8_UNLIKELY(hash == 0) ? 1 : hash;
  }

  size_t mask_;
  size_t entry_count_;
  base::Vector<Entry> table_;
  ZoneVector<Entry*> depths_heads_;
  Zone* zone_;

  static constexpr double kNeedResizePercentage = 0.75;
  static constexpr int kGrowthFactor = 2;
};

template <class Key, class Value>
LayeredHashMap<Key, Value>::LayeredHashMap(Zone* zone,
                                           uint32_t initial_capacity)
    : entry_count_(0), depths_heads_(zone), zone_(zone) {
  // Setting the minimal capacity to 16
  initial_capacity = std::max<uint32_t>(initial_capacity, 16);
  // {initial_capacity} should be a power of 2, so that we can compute offset
  // in {table_} with a mask rather than a modulo.
  initial_capacity = base::bits::RoundUpToPowerOfTwo32(initial_capacity);
  mask_ = initial_capacity - 1;
  // Allocating the table_
  table_ = zone_->NewVector<Entry>(initial_capacity);
}

template <class Key, class Value>
void LayeredHashMap<Key, Value>::StartLayer() {
  depths_heads_.push_back(nullptr);
}

template <class Key, class Value>
void LayeredHashMap<Key, Value>::DropLastLayer() {
  DCHECK_GT(depths_heads_.size(), 0);
  for (Entry* entry = depths_heads_.back(); entry != nullptr;) {
    entry_count_--;
    Entry* next = entry->depth_neighboring_entry;
    *entry = Entry();
    entry = next;
  }
  depths_heads_.pop_back();
}

template <class Key, class Value>
typename LayeredHashMap<Key, Value>::Entry*
LayeredHashMap<Key, Value>::FindEntryForKey(Key key, size_t hash) {
  for (size_t i = hash & mask_;; i = NextEntryIndex(i)) {
    if (table_[i].hash == 0) return &table_[i];
    if (table_[i].hash == hash && table_[i].key == key) return &table_[i];
  }
}

template <class Key, class Value>
void LayeredHashMap<Key, Value>::InsertNewKey(Key key, Value value) {
  ResizeIfNeeded();
  size_t hash = ComputeHash(key);
  Entry* destination = FindEntryForKey(key, hash);
  DCHECK_EQ(destination->hash, 0);
  *destination = Entry{hash, key, value, depths_heads_.back()};
  depths_heads_.back() = destination;
  entry_count_++;
}

template <class Key, class Value>
std::optional<Value> LayeredHashMap<Key, Value>::Get(Key key) {
  Entry* destination = FindEntryForKey(key, ComputeHash(key));
  if (destination->hash == 0) return std::nullopt;
  return destination->value;
}

template <class Key, class Value>
bool LayeredHashMap<Key, Value>::Contains(Key key) {
  return Get(key).has_value();
}

template <class Key, class Value>
void LayeredHashMap<Key, Value>::ResizeIfNeeded() {
  if (table_.size() * kNeedResizePercentage > entry_count_) return;
  CHECK_LE(table_.size(), std::numeric_limits<size_t>::max() / kGrowthFactor);
  table_ = zone_->NewVector<Entry>(table_.size() * kGrowthFactor);
  mask_ = table_.size() - 1;
  DCHECK_EQ(base::bits::CountPopulation(mask_),
            sizeof(mask_) * 8 - base::bits::CountLeadingZeros(mask_));
  for (size_t depth_idx = 0; depth_idx < depths_heads_.size(); depth_idx++) {
    // It's important to fill the new hash by inserting data in increasing
    // depth order, in order to avoid holes when later calling DropLastLayer.
    // Consider for instance:
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
    // Then, when we'll call DropLastLayer to remove entries from a3's depth,
    // we'll get this:
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
      Entry* new_entry_loc = FindEntryForKey(entry->key, entry->hash);
      *new_entry_loc = *entry;
      Entry* next_entry = entry->depth_neighboring_entry;
      new_entry_loc->depth_neighboring_entry = depths_heads_[depth_idx];
      depths_heads_[depth_idx] = new_entry_loc;
      entry = next_entry;
    }
  }
}

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LAYERED_HASH_MAP_H_
