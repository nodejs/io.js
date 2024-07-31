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

#include "absl/strings/internal/cord_rep_btree_navigator.h"

#include <cassert>

#include "absl/strings/internal/cord_data_edge.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

using ReadResult = CordRepBtreeNavigator::ReadResult;

namespace {

// Returns a `CordRepSubstring` from `rep` starting at `offset` of size `n`.
// If `rep` is already a `CordRepSubstring` instance, an adjusted instance is
// created based on the old offset and new offset.
// Adopts a reference on `rep`. Rep must be a valid data edge. Returns
// nullptr if `n == 0`, `rep` if `n == rep->length`.
// Requires `offset < rep->length` and `offset + n <= rep->length`.
// TODO(192061034): move to utility library in internal and optimize for small
// substrings of larger reps.
inline CordRep* Substring(CordRep* rep, size_t offset, size_t n) {
  assert(n <= rep->length);
  assert(offset < rep->length);
  assert(offset <= rep->length - n);
  assert(IsDataEdge(rep));

  if (n == 0) return nullptr;
  if (n == rep->length) return CordRep::Ref(rep);

  if (rep->tag == SUBSTRING) {
    offset += rep->substring()->start;
    rep = rep->substring()->child;
  }

  assert(rep->IsExternal() || rep->IsFlat());
  CordRepSubstring* substring = new CordRepSubstring();
  substring->length = n;
  substring->tag = SUBSTRING;
  substring->start = offset;
  substring->child = CordRep::Ref(rep);
  return substring;
}

inline CordRep* Substring(CordRep* rep, size_t offset) {
  return Substring(rep, offset, rep->length - offset);
}

}  // namespace

CordRepBtreeNavigator::Position CordRepBtreeNavigator::Skip(size_t n) {
  int height = 0;
  size_t index = index_[0];
  CordRepBtree* node = node_[0];
  CordRep* edge = node->Edge(index);

  // Overall logic: Find an edge of at least the length we need to skip.
  // We consume all edges which are smaller (i.e., must be 100% skipped).
  // If we exhausted all edges on the current level, we move one level
  // up the tree, and repeat until we either find the edge, or until we hit
  // the top of the tree meaning the skip exceeds tree->length.
  while (n >= edge->length) {
    n -= edge->length;
    while (++index == node->end()) {
      if (++height > height_) return {nullptr, n};
      node = node_[height];
      index = index_[height];
    }
    edge = node->Edge(index);
  }

  // If we moved up the tree, descend down to the leaf level, consuming all
  // edges that must be skipped.
  while (height > 0) {
    node = edge->btree();
    index_[height] = static_cast<uint8_t>(index);
    node_[--height] = node;
    index = node->begin();
    edge = node->Edge(index);
    while (n >= edge->length) {
      n -= edge->length;
      ++index;
      assert(index != node->end());
      edge = node->Edge(index);
    }
  }
  index_[0] = static_cast<uint8_t>(index);
  return {edge, n};
}

ReadResult CordRepBtreeNavigator::Read(size_t edge_offset, size_t n) {
  int height = 0;
  size_t length = edge_offset + n;
  size_t index = index_[0];
  CordRepBtree* node = node_[0];
  CordRep* edge = node->Edge(index);
  assert(edge_offset < edge->length);

  if (length < edge->length) {
    return {Substring(edge, edge_offset, n), length};
  }

  // Similar to 'Skip', we consume all edges that are inside the 'length' of
  // data that needs to be read. If we exhaust the current level, we move one
  // level up the tree and repeat until we hit the final edge that must be
  // (partially) read. We consume all edges into `subtree`.
  CordRepBtree* subtree = CordRepBtree::New(Substring(edge, edge_offset));
  size_t subtree_end = 1;
  do {
    length -= edge->length;
    while (++index == node->end()) {
      index_[height] = static_cast<uint8_t>(index);
      if (++height > height_) {
        subtree->set_end(subtree_end);
        if (length == 0) return {subtree, 0};
        CordRep::Unref(subtree);
        return {nullptr, length};
      }
      if (length != 0) {
        subtree->set_end(subtree_end);
        subtree = CordRepBtree::New(subtree);
        subtree_end = 1;
      }
      node = node_[height];
      index = index_[height];
    }
    edge = node->Edge(index);
    if (length >= edge->length) {
      subtree->length += edge->length;
      subtree->edges_[subtree_end++] = CordRep::Ref(edge);
    }
  } while (length >= edge->length);
  CordRepBtree* tree = subtree;
  subtree->length += length;

  // If we moved up the tree, descend down to the leaf level, consuming all
  // edges that must be read, adding 'down' nodes to `subtree`.
  while (height > 0) {
    node = edge->btree();
    index_[height] = static_cast<uint8_t>(index);
    node_[--height] = node;
    index = node->begin();
    edge = node->Edge(index);

    if (length != 0) {
      CordRepBtree* right = CordRepBtree::New(height);
      right->length = length;
      subtree->edges_[subtree_end++] = right;
      subtree->set_end(subtree_end);
      subtree = right;
      subtree_end = 0;
      while (length >= edge->length) {
        subtree->edges_[subtree_end++] = CordRep::Ref(edge);
        length -= edge->length;
        edge = node->Edge(++index);
      }
    }
  }
  // Add any (partial) edge still remaining at the leaf level.
  if (length != 0) {
    subtree->edges_[subtree_end++] = Substring(edge, 0, length);
  }
  subtree->set_end(subtree_end);
  index_[0] = static_cast<uint8_t>(index);
  return {tree, length};
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
