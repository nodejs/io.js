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

#include "absl/strings/internal/cord_rep_btree_reader.h"

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

absl::string_view CordRepBtreeReader::Read(size_t n, size_t chunk_size,
                                           CordRep*& tree) {
  assert(chunk_size <= navigator_.Current()->length);

  // If chunk_size is non-zero, we need to start inside last returned edge.
  // Else we start reading at the next data edge of the tree.
  CordRep* edge = chunk_size ? navigator_.Current() : navigator_.Next();
  const size_t offset = chunk_size ? edge->length - chunk_size : 0;

  // Read the sub tree and verify we got what we wanted.
  ReadResult result = navigator_.Read(offset, n);
  tree = result.tree;

  // If the data returned in `tree` was covered entirely by `chunk_size`, i.e.,
  // read from the 'previous' edge, we did not consume any additional data, and
  // can directly return the substring into the current data edge as the next
  // chunk. We can easily establish from the above code that `navigator_.Next()`
  // has not been called as that requires `chunk_size` to be zero.
  if (n < chunk_size) return EdgeData(edge).substr(result.n);

  // The amount of data taken from the last edge is `chunk_size` and `result.n`
  // contains the offset into the current edge trailing the read data (which can
  // be 0). As the call to `navigator_.Read()` could have consumed all remaining
  // data, calling `navigator_.Current()` is not safe before checking if we
  // already consumed all remaining data.
  const size_t consumed_by_read = n - chunk_size - result.n;
  if (consumed_by_read >= remaining_) {
    remaining_ = 0;
    return {};
  }

  // We did not read all data, return remaining data from current edge.
  edge = navigator_.Current();
  remaining_ -= consumed_by_read + edge->length;
  return EdgeData(edge).substr(result.n);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
