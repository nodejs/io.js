// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_SIDETABLE_H_
#define V8_COMPILER_TURBOSHAFT_SIDETABLE_H_

#include <algorithm>
#include <type_traits>

#include "src/compiler/turboshaft/operations.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

#ifdef DEBUG
V8_EXPORT_PRIVATE bool OpIndexBelongsToTableGraph(const Graph* graph,
                                                  OpIndex index);
#endif

namespace detail {

// This sidetable is a conceptually infinite mapping from Turboshaft operation
// indices to values. It grows automatically and default-initializes the table
// when accessed out-of-bounds.
template <class T, class Key>
class GrowingSidetable {
 public:
  static_assert(std::is_same_v<Key, OpIndex> ||
                std::is_same_v<Key, BlockIndex>);

  T& operator[](Key index) {
    DCHECK(index.valid());
    size_t i = index.id();
    if (V8_UNLIKELY(i >= table_.size())) {
      table_.resize(NextSize(i));
      // Make sure we also get access to potential over-allocation by
      // `resize()`.
      table_.resize(table_.capacity());
    }
    return table_[i];
  }

  const T& operator[](Key index) const {
    DCHECK(index.valid());
    size_t i = index.id();
    if (V8_UNLIKELY(i >= table_.size())) {
      table_.resize(NextSize(i));
      // Make sure we also get access to potential over-allocation by
      // `resize()`.
      table_.resize(table_.capacity());
    }
    return table_[i];
  }

  // Reset by filling the table with the default value instead of shrinking to
  // keep the memory for later phases.
  void Reset() { std::fill(table_.begin(), table_.end(), T{}); }

  // Returns `true` if the table never contained any values, even before
  // `Reset()`.
  bool empty() const { return table_.empty(); }

 protected:
  // Constructors are protected: use GrowingBlockSidetable or
  // GrowingOpIndexSidetable instead.
  explicit GrowingSidetable(Zone* zone) : table_(zone) {}
  GrowingSidetable(size_t size, const T& initial_value, Zone* zone)
      : table_(size, initial_value, zone) {}

  mutable ZoneVector<T> table_;

  size_t NextSize(size_t out_of_bounds_index) const {
    DCHECK_GE(out_of_bounds_index, table_.size());
    return out_of_bounds_index + out_of_bounds_index / 2 + 32;
  }
};

// A fixed-size sidetable mapping from `Key` to `T`.
// Elements are default-initialized.
template <class T, class Key>
class FixedSidetable {
 public:
  static_assert(std::is_same_v<Key, OpIndex> ||
                std::is_same_v<Key, BlockIndex>);

  T& operator[](Key op) {
    DCHECK_LT(op.id(), table_.size());
    return table_[op.id()];
  }

  const T& operator[](Key op) const {
    DCHECK_LT(op.id(), table_.size());
    return table_[op.id()];
  }

 protected:
  // Constructors are protected: use FixedBlockSidetable or
  // FixedOpIndexSidetable instead.
  explicit FixedSidetable(size_t size, Zone* zone) : table_(size, zone) {}
  FixedSidetable(size_t size, const T& default_value, Zone* zone)
      : table_(size, default_value, zone) {}

  ZoneVector<T> table_;
};

}  // namespace detail

template <typename T>
class GrowingBlockSidetable : public detail::GrowingSidetable<T, BlockIndex> {
  using Base = detail::GrowingSidetable<T, BlockIndex>;

 public:
  explicit GrowingBlockSidetable(Zone* zone) : Base(zone) {}

  GrowingBlockSidetable(size_t size, const T& initial_value, Zone* zone)
      : Base(size, initial_value, zone) {}
};

template <typename T>
class FixedBlockSidetable : public detail::FixedSidetable<T, BlockIndex> {
  using Base = detail::FixedSidetable<T, BlockIndex>;

 public:
  explicit FixedBlockSidetable(size_t size, Zone* zone) : Base(size, zone) {}

  FixedBlockSidetable(size_t size, const T& initial_value, Zone* zone)
      : Base(size, initial_value, zone) {}
};

template <class T>
class GrowingOpIndexSidetable : public detail::GrowingSidetable<T, OpIndex> {
  using Base = detail::GrowingSidetable<T, OpIndex>;

 public:
  explicit GrowingOpIndexSidetable(Zone* zone, const Graph* graph)
      : Base(zone)
#ifdef DEBUG
        ,
        graph_(graph)
#endif
  {
    USE(graph);
  }

  GrowingOpIndexSidetable(size_t size, const T& initial_value, Zone* zone,
                          const Graph* graph)
      : Base(size, initial_value, zone)
#ifdef DEBUG
        ,
        graph_(graph)
#endif
  {
    USE(graph);
  }

  T& operator[](OpIndex index) {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    return Base::operator[](index);
  }

  const T& operator[](OpIndex index) const {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    return Base::operator[](index);
  }

  void SwapData(GrowingOpIndexSidetable<T>& other) {
    std::swap(Base::table_, other.table_);
  }

 public:
#ifdef DEBUG
  const Graph* graph_;
#endif
};

template <class T>
class FixedOpIndexSidetable : public detail::FixedSidetable<T, OpIndex> {
  using Base = detail::FixedSidetable<T, OpIndex>;

 public:
  FixedOpIndexSidetable(size_t size, Zone* zone, const Graph* graph)
      : Base(size, zone)
#ifdef DEBUG
        ,
        graph_(graph)
#endif
  {
  }
  FixedOpIndexSidetable(size_t size, const T& default_value, Zone* zone,
                        const Graph* graph)
      : Base(size, default_value, zone)
#ifdef DEBUG
        ,
        graph_(graph)
#endif
  {
  }

  T& operator[](OpIndex index) {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    return Base::operator[](index);
  }

  const T& operator[](OpIndex index) const {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    return Base::operator[](index);
  }

  void SwapData(FixedOpIndexSidetable<T>& other) {
    std::swap(Base::table_, other.table_);
  }

 public:
#ifdef DEBUG
  const Graph* graph_;
#endif
};

template <class T>
class SparseOpIndexSideTable {
 public:
  SparseOpIndexSideTable(Zone* zone, const Graph* graph)
      : data_(zone)
#ifdef DEBUG
        ,
        graph_(graph)
#endif
  {
  }

  T& operator[](OpIndex index) {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    return data_[index];
  }

  const T& operator[](OpIndex index) const {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    DCHECK(data_.contains(index));
    return data_.at(index);
  }

  bool contains(OpIndex index, const T** value = nullptr) const {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    if (auto it = data_.find(index); it != data_.end()) {
      if (value) *value = &it->second;
      return true;
    }
    return false;
  }

  void remove(OpIndex index) {
    DCHECK(OpIndexBelongsToTableGraph(graph_, index));
    auto it = data_.find(index);
    if (it != data_.end()) data_.erase(it);
  }

  auto begin() { return data_.begin(); }
  auto end() { return data_.end(); }

 private:
  ZoneAbslFlatHashMap<OpIndex, T> data_;
#ifdef DEBUG
  const Graph* graph_;
#endif
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_SIDETABLE_H_
