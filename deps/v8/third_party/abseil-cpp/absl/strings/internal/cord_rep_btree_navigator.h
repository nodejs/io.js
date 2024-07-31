// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_NAVIGATOR_H_
#define ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_NAVIGATOR_H_

#include <cassert>
#include <iostream>

#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// CordRepBtreeNavigator is a bi-directional navigator allowing callers to
// navigate all the (leaf) data edges in a CordRepBtree instance.
//
// A CordRepBtreeNavigator instance is by default empty. Callers initialize a
// navigator instance by calling one of `InitFirst()`, `InitLast()` or
// `InitOffset()`, which establishes a current position. Callers can then
// navigate using the `Next`, `Previous`, `Skip` and `Seek` methods.
//
// The navigator instance does not take or adopt a reference on the provided
// `tree` on any of the initialization calls. Callers are responsible for
// guaranteeing the lifecycle of the provided tree. A navigator instance can
// be reset to the empty state by calling `Reset`.
//
// A navigator only keeps positional state on the 'current data edge', it does
// explicitly not keep any 'offset' state. The class does accept and return
// offsets in the `Read()`, `Skip()` and 'Seek()` methods as these would
// otherwise put a big burden on callers. Callers are expected to maintain
// (returned) offset info if they require such granular state.
class CordRepBtreeNavigator {
 public:
  // The logical position as returned by the Seek() and Skip() functions.
  // Returns the current leaf edge for the desired seek or skip position and
  // the offset of that position inside that edge.
  struct Position {
    CordRep* edge;
    size_t offset;
  };

  // The read result as returned by the Read() function.
  // `tree` contains the resulting tree which is identical to the result
  // of calling CordRepBtree::SubTree(...) on the tree being navigated.
  // `n` contains the number of bytes used from the last navigated to
  // edge of the tree.
  struct ReadResult {
    CordRep* tree;
    size_t n;
  };

  // Returns true if this instance is not empty.
  explicit operator bool() const;

  // Returns the tree for this instance or nullptr if empty.
  CordRepBtree* btree() const;

  // Returns the data edge of the current position.
  // Requires this instance to not be empty.
  CordRep* Current() const;

  // Resets this navigator to `tree`, returning the first data edge in the tree.
  CordRep* InitFirst(CordRepBtree* tree);

  // Resets this navigator to `tree`, returning the last data edge in the tree.
  CordRep* InitLast(CordRepBtree* tree);

  // Resets this navigator to `tree` returning the data edge at position
  // `offset` and the relative offset of `offset` into that data edge.
  // Returns `Position.edge = nullptr` if the provided offset is greater
  // than or equal to the length of the tree, in which case the state of
  // the navigator instance remains unchanged.
  Position InitOffset(CordRepBtree* tree, size_t offset);

  // Navigates to the next data edge.
  // Returns the next data edge or nullptr if there is no next data edge, in
  // which case the current position remains unchanged.
  CordRep* Next();

  // Navigates to the previous data edge.
  // Returns the previous data edge or nullptr if there is no previous data
  // edge, in which case the current position remains unchanged.
  CordRep* Previous();

  // Navigates to the data edge at position `offset`. Returns the navigated to
  // data edge in `Position.edge` and the relative offset of `offset` into that
  // data edge in `Position.offset`. Returns `Position.edge = nullptr` if the
  // provide offset is greater than or equal to the tree's length.
  Position Seek(size_t offset);

  // Reads `n` bytes of data starting at offset `edge_offset` of the current
  // data edge, and returns the result in `ReadResult.tree`. `ReadResult.n`
  // contains the 'bytes used` from the last / current data edge in the tree.
  // This allows users that mix regular navigation (using string views) and
  // 'read into cord' navigation to keep track of the current state, and which
  // bytes have been consumed from a navigator.
  // This function returns `ReadResult.tree = nullptr` if the requested length
  // exceeds the length of the tree starting at the current data edge.
  ReadResult Read(size_t edge_offset, size_t n);

  // Skips `n` bytes forward from the current data edge, returning the navigated
  // to data edge in `Position.edge` and `Position.offset` containing the offset
  // inside that data edge. Note that the state of the navigator is left
  // unchanged if `n` is smaller than the length of the current data edge.
  Position Skip(size_t n);

  // Resets this instance to the default / empty state.
  void Reset();

 private:
  // Slow path for Next() if Next() reached the end of a leaf node. Backtracks
  // up the stack until it finds a node that has a 'next' position available,
  // and then does a 'front dive' towards the next leaf node.
  CordRep* NextUp();

  // Slow path for Previous() if Previous() reached the beginning of a leaf
  // node. Backtracks up the stack until it finds a node that has a 'previous'
  // position available, and then does a 'back dive' towards the previous leaf
  // node.
  CordRep* PreviousUp();

  // Generic implementation of InitFirst() and InitLast().
  template <CordRepBtree::EdgeType edge_type>
  CordRep* Init(CordRepBtree* tree);

  // `height_` contains the height of the current tree, or -1 if empty.
  int height_ = -1;

  // `index_` and `node_` contain the navigation state as the 'path' to the
  // current data edge which is at `node_[0]->Edge(index_[0])`. The contents
  // of these are undefined until the instance is initialized (`height_ >= 0`).
  uint8_t index_[CordRepBtree::kMaxDepth];
  CordRepBtree* node_[CordRepBtree::kMaxDepth];
};

// Returns true if this instance is not empty.
inline CordRepBtreeNavigator::operator bool() const { return height_ >= 0; }

inline CordRepBtree* CordRepBtreeNavigator::btree() const {
  return height_ >= 0 ? node_[height_] : nullptr;
}

inline CordRep* CordRepBtreeNavigator::Current() const {
  assert(height_ >= 0);
  return node_[0]->Edge(index_[0]);
}

inline void CordRepBtreeNavigator::Reset() { height_ = -1; }

inline CordRep* CordRepBtreeNavigator::InitFirst(CordRepBtree* tree) {
  return Init<CordRepBtree::kFront>(tree);
}

inline CordRep* CordRepBtreeNavigator::InitLast(CordRepBtree* tree) {
  return Init<CordRepBtree::kBack>(tree);
}

template <CordRepBtree::EdgeType edge_type>
inline CordRep* CordRepBtreeNavigator::Init(CordRepBtree* tree) {
  assert(tree != nullptr);
  assert(tree->size() > 0);
  assert(tree->height() <= CordRepBtree::kMaxHeight);
  int height = height_ = tree->height();
  size_t index = tree->index(edge_type);
  node_[height] = tree;
  index_[height] = static_cast<uint8_t>(index);
  while (--height >= 0) {
    tree = tree->Edge(index)->btree();
    node_[height] = tree;
    index = tree->index(edge_type);
    index_[height] = static_cast<uint8_t>(index);
  }
  return node_[0]->Edge(index);
}

inline CordRepBtreeNavigator::Position CordRepBtreeNavigator::Seek(
    size_t offset) {
  assert(btree() != nullptr);
  int height = height_;
  CordRepBtree* edge = node_[height];
  if (ABSL_PREDICT_FALSE(offset >= edge->length)) return {nullptr, 0};
  CordRepBtree::Position index = edge->IndexOf(offset);
  index_[height] = static_cast<uint8_t>(index.index);
  while (--height >= 0) {
    edge = edge->Edge(index.index)->btree();
    node_[height] = edge;
    index = edge->IndexOf(index.n);
    index_[height] = static_cast<uint8_t>(index.index);
  }
  return {edge->Edge(index.index), index.n};
}

inline CordRepBtreeNavigator::Position CordRepBtreeNavigator::InitOffset(
    CordRepBtree* tree, size_t offset) {
  assert(tree != nullptr);
  assert(tree->height() <= CordRepBtree::kMaxHeight);
  if (ABSL_PREDICT_FALSE(offset >= tree->length)) return {nullptr, 0};
  height_ = tree->height();
  node_[height_] = tree;
  return Seek(offset);
}

inline CordRep* CordRepBtreeNavigator::Next() {
  CordRepBtree* edge = node_[0];
  return index_[0] == edge->back() ? NextUp() : edge->Edge(++index_[0]);
}

inline CordRep* CordRepBtreeNavigator::Previous() {
  CordRepBtree* edge = node_[0];
  return index_[0] == edge->begin() ? PreviousUp() : edge->Edge(--index_[0]);
}

inline CordRep* CordRepBtreeNavigator::NextUp() {
  assert(index_[0] == node_[0]->back());
  CordRepBtree* edge;
  size_t index;
  int height = 0;
  do {
    if (++height > height_) return nullptr;
    edge = node_[height];
    index = index_[height] + 1;
  } while (index == edge->end());
  index_[height] = static_cast<uint8_t>(index);
  do {
    node_[--height] = edge = edge->Edge(index)->btree();
    index_[height] = static_cast<uint8_t>(index = edge->begin());
  } while (height > 0);
  return edge->Edge(index);
}

inline CordRep* CordRepBtreeNavigator::PreviousUp() {
  assert(index_[0] == node_[0]->begin());
  CordRepBtree* edge;
  size_t index;
  int height = 0;
  do {
    if (++height > height_) return nullptr;
    edge = node_[height];
    index = index_[height];
  } while (index == edge->begin());
  index_[height] = static_cast<uint8_t>(--index);
  do {
    node_[--height] = edge = edge->Edge(index)->btree();
    index_[height] = static_cast<uint8_t>(index = edge->back());
  } while (height > 0);
  return edge->Edge(index);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_NAVIGATOR_H_
