// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASE_CACHED_UNORDERED_MAP_H_
#define V8_HEAP_BASE_CACHED_UNORDERED_MAP_H_

#include <unordered_map>

#include "absl/container/flat_hash_map.h"
#include "src/base/hashing.h"

namespace heap::base {

// A cached map that speeds up `operator[]` if used in LRU fashion.
template <typename _Key, typename _Value, typename _Hash = v8::base::hash<_Key>>
class CachedUnorderedMap final {
  using MapT = absl::flat_hash_map<_Key, _Value, _Hash>;

 public:
  using Key = typename MapT::key_type;
  using Mapped = typename MapT::mapped_type;

  Mapped& operator[](const Key& key) {
    // nullptr value is used to indicate absence of a last key.
    DCHECK_NOT_NULL(key);

    if (key == last_key_) {
      return *last_mapped_;
    }

    auto it = map_.find(key);
    if (it == map_.end()) {
      auto result = map_.emplace(key, Mapped());
      DCHECK(result.second);
      it = result.first;
    }

    last_key_ = key;
    last_mapped_ = &it->second;

    return it->second;
  }

  typename MapT::size_type erase(const Key& key) {
    if (key == last_key_) {
      last_key_ = nullptr;
      last_mapped_ = nullptr;
    }
    return map_.erase(key);
  }

  // No iterator is cached in this class so an actual find() has to be executed
  // everytime.
  typename MapT::iterator find(const Key& key) { return map_.find(key); }

  typename MapT::iterator begin() { return map_.begin(); }
  typename MapT::iterator end() { return map_.end(); }
  typename MapT::const_iterator begin() const { return map_.begin(); }
  typename MapT::const_iterator end() const { return map_.begin(); }

  bool contains(const Key& key) const { return map_.contains(key); }

  void clear() {
    last_key_ = nullptr;
    last_mapped_ = nullptr;
    map_.clear();
  }

  bool empty() const { return map_.empty(); }

  MapT Take() {
    last_key_ = nullptr;
    last_mapped_ = nullptr;

    MapT tmp(std::move(map_));
    map_.clear();
    return tmp;
  }

 private:
  Key last_key_ = nullptr;
  Mapped* last_mapped_ = nullptr;
  MapT map_;
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_CACHED_UNORDERED_MAP_H_
