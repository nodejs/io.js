// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_SNAPSHOT_TABLE_OPINDEX_H_
#define V8_COMPILER_TURBOSHAFT_SNAPSHOT_TABLE_OPINDEX_H_

#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/snapshot-table.h"

namespace v8::internal::compiler::turboshaft {
// A Wrapper around a SnapshotTable, which takes care of mapping OpIndex to Key.
// It uses a ZoneAbslFlatHashMap to store this mapping, and is thus more
// appropriate for cases where not many OpIndex have a corresponding key.
template <class Value, class KeyData = NoKeyData>
class SparseOpIndexSnapshotTable : public SnapshotTable<Value, KeyData> {
 public:
  using Base = SnapshotTable<Value, KeyData>;
  using Key = typename SnapshotTable<Value, KeyData>::Key;

  explicit SparseOpIndexSnapshotTable(Zone* zone)
      : Base(zone), indices_to_keys_(zone) {}

  using Base::Get;
  Value Get(OpIndex idx) const {
    auto it = indices_to_keys_.find(idx);
    if (it == indices_to_keys_.end()) return Value{};
    return Base::Get(it->second);
  }

  Value GetPredecessorValue(OpIndex idx, int predecessor_index) {
    auto it = indices_to_keys_.find(idx);
    if (it == indices_to_keys_.end()) return Value{};
    return Base::GetPredecessorValue(it->second, predecessor_index);
  }

  using Base::Set;
  bool Set(OpIndex idx, Value new_value) {
    Key key = GetOrCreateKey(idx);
    return Base::Set(key, new_value);
  }

  void NewKey(OpIndex idx, KeyData data, Value initial_value = Value{}) {
    DCHECK(!indices_to_keys_[idx].has_value());
    indices_to_keys_[idx] = Base::NewKey(data, initial_value);
  }
  void NewKey(OpIndex idx, Value initial_value = Value{}) {
    NewKey(idx, KeyData{}, initial_value);
  }

  bool HasKeyFor(OpIndex idx) const {
    return indices_to_keys_.find(idx) != indices_to_keys_.end();
  }

  base::Optional<Key> TryGetKeyFor(OpIndex idx) const {
    auto it = indices_to_keys_.find(idx);
    if (it != indices_to_keys_.end()) return it->second;
    return base::nullopt;
  }

 private:
  Key GetOrCreateKey(OpIndex idx) {
    auto it = indices_to_keys_.find(idx);
    if (it != indices_to_keys_.end()) return it->second;
    Key key = Base::NewKey();
    indices_to_keys_.insert({idx, key});
    return key;
  }
  ZoneAbslFlatHashMap<OpIndex, Key> indices_to_keys_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_SNAPSHOT_TABLE_OPINDEX_H_
