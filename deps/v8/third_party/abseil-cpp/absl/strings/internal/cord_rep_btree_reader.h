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

#ifndef ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_READER_H_
#define ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_READER_H_

#include <cassert>

#include "absl/base/config.h"
#include "absl/strings/internal/cord_data_edge.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_btree_navigator.h"
#include "absl/strings/internal/cord_rep_flat.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// CordRepBtreeReader implements logic to iterate over cord btrees.
// References to the underlying data are returned as absl::string_view values.
// The most typical use case is a forward only iteration over tree data.
// The class also provides `Skip()`, `Seek()` and `Read()` methods similar to
// CordRepBtreeNavigator that allow more advanced navigation.
//
// Example: iterate over all data inside a cord btree:
//
//   CordRepBtreeReader reader;
//   for (string_view sv = reader.Init(tree); !sv.Empty(); sv = sv.Next()) {
//     DoSomethingWithDataIn(sv);
//   }
//
// All navigation methods always return the next 'chunk' of data. The class
// assumes that all data is directly 'consumed' by the caller. For example:
// invoking `Skip()` will skip the desired number of bytes, and directly
// read and return the next chunk of data directly after the skipped bytes.
//
// Example: iterate over all data inside a btree skipping the first 100 bytes:
//
//   CordRepBtreeReader reader;
//   absl::string_view sv = reader.Init(tree);
//   if (sv.length() > 100) {
//     sv.RemovePrefix(100);
//   } else {
//     sv = reader.Skip(100 - sv.length());
//   }
//   while (!sv.empty()) {
//     DoSomethingWithDataIn(sv);
//     absl::string_view sv = reader.Next();
//   }
//
// It is important to notice that `remaining` is based on the end position of
// the last data edge returned to the caller, not the cumulative data returned
// to the caller which can be less in cases of skipping or seeking over data.
//
// For example, consider a cord btree with five data edges: "abc", "def", "ghi",
// "jkl" and "mno":
//
//   absl::string_view sv;
//   CordRepBtreeReader reader;
//
//   sv = reader.Init(tree); // sv = "abc", remaining = 12
//   sv = reader.Skip(4);    // sv = "hi",  remaining = 6
//   sv = reader.Skip(2);    // sv = "l",   remaining = 3
//   sv = reader.Next();     // sv = "mno", remaining = 0
//   sv = reader.Seek(1);    // sv = "bc", remaining = 12
//
class CordRepBtreeReader {
 public:
  using ReadResult = CordRepBtreeNavigator::ReadResult;
  using Position = CordRepBtreeNavigator::Position;

  // Returns true if this instance is not empty.
  explicit operator bool() const { return navigator_.btree() != nullptr; }

  // Returns the tree referenced by this instance or nullptr if empty.
  CordRepBtree* btree() const { return navigator_.btree(); }

  // Returns the current data edge inside the referenced btree.
  // Requires that the current instance is not empty.
  CordRep* node() const { return navigator_.Current(); }

  // Returns the length of the referenced tree.
  // Requires that the current instance is not empty.
  size_t length() const;

  // Returns the number of remaining bytes available for iteration, which is the
  // number of bytes directly following the end of the last chunk returned.
  // This value will be zero if we iterated over the last edge in the bound
  // tree, in which case any call to Next() or Skip() will return an empty
  // string_view reflecting the EOF state.
  // Note that a call to `Seek()` resets `remaining` to a value based on the
  // end position of the chunk returned by that call.
  size_t remaining() const { return remaining_; }

  // Resets this instance to an empty value.
  void Reset() { navigator_.Reset(); }

  // Initializes this instance with `tree`. `tree` must not be null.
  // Returns a reference to the first data edge of the provided tree.
  absl::string_view Init(CordRepBtree* tree);

  // Navigates to and returns the next data edge of the referenced tree.
  // Returns an empty string_view if an attempt is made to read beyond the end
  // of the tree, i.e.: if `remaining()` is zero indicating an EOF condition.
  // Requires that the current instance is not empty.
  absl::string_view Next();

  // Skips the provided amount of bytes and returns a reference to the data
  // directly following the skipped bytes.
  absl::string_view Skip(size_t skip);

  // Reads `n` bytes into `tree`.
  // If `chunk_size` is zero, starts reading at the next data edge. If
  // `chunk_size` is non zero, the read starts at the last `chunk_size` bytes of
  // the last returned data edge. Effectively, this means that the read starts
  // at offset `consumed() - chunk_size`.
  // Requires that `chunk_size` is less than or equal to the length of the
  // last returned data edge. The purpose of `chunk_size` is to simplify code
  // partially consuming a returned chunk and wanting to include the remaining
  // bytes in the Read call. For example, the below code will read 1000 bytes of
  // data into a cord tree if the first chunk starts with "big:":
  //
  //   CordRepBtreeReader reader;
  //   absl::string_view sv = reader.Init(tree);
  //   if (absl::StartsWith(sv, "big:")) {
  //     CordRepBtree tree;
  //     sv = reader.Read(1000, sv.size() - 4 /* "big:" */, &tree);
  //   }
  //
  // This method will return an empty string view if all remaining data was
  // read. If `n` exceeded the amount of remaining data this function will
  // return an empty string view and `tree` will be set to nullptr.
  // In both cases, `consumed` will be set to `length`.
  absl::string_view Read(size_t n, size_t chunk_size, CordRep*& tree);

  // Navigates to the chunk at offset `offset`.
  // Returns a reference into the navigated to chunk, adjusted for the relative
  // position of `offset` into that chunk. For example, calling `Seek(13)` on a
  // cord tree containing 2 chunks of 10 and 20 bytes respectively will return
  // a string view into the second chunk starting at offset 3 with a size of 17.
  // Returns an empty string view if `offset` is equal to or greater than the
  // length of the referenced tree.
  absl::string_view Seek(size_t offset);

 private:
  size_t remaining_ = 0;
  CordRepBtreeNavigator navigator_;
};

inline size_t CordRepBtreeReader::length() const {
  assert(btree() != nullptr);
  return btree()->length;
}

inline absl::string_view CordRepBtreeReader::Init(CordRepBtree* tree) {
  assert(tree != nullptr);
  const CordRep* edge = navigator_.InitFirst(tree);
  remaining_ = tree->length - edge->length;
  return EdgeData(edge);
}

inline absl::string_view CordRepBtreeReader::Next() {
  if (remaining_ == 0) return {};
  const CordRep* edge = navigator_.Next();
  assert(edge != nullptr);
  remaining_ -= edge->length;
  return EdgeData(edge);
}

inline absl::string_view CordRepBtreeReader::Skip(size_t skip) {
  // As we are always positioned on the last 'consumed' edge, we
  // need to skip the current edge as well as `skip`.
  const size_t edge_length = navigator_.Current()->length;
  CordRepBtreeNavigator::Position pos = navigator_.Skip(skip + edge_length);
  if (ABSL_PREDICT_FALSE(pos.edge == nullptr)) {
    remaining_ = 0;
    return {};
  }
  // The combined length of all edges skipped before `pos.edge` is `skip -
  // pos.offset`, all of which are 'consumed', as well as the current edge.
  remaining_ -= skip - pos.offset + pos.edge->length;
  return EdgeData(pos.edge).substr(pos.offset);
}

inline absl::string_view CordRepBtreeReader::Seek(size_t offset) {
  const CordRepBtreeNavigator::Position pos = navigator_.Seek(offset);
  if (ABSL_PREDICT_FALSE(pos.edge == nullptr)) {
    remaining_ = 0;
    return {};
  }
  absl::string_view chunk = EdgeData(pos.edge).substr(pos.offset);
  remaining_ = length() - offset - chunk.length();
  return chunk;
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_READER_H_
