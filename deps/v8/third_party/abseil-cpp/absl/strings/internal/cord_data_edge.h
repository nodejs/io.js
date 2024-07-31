// Copyright 2022 The Abseil Authors
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

#ifndef ABSL_STRINGS_INTERNAL_CORD_DATA_EDGE_H_
#define ABSL_STRINGS_INTERNAL_CORD_DATA_EDGE_H_

#include <cassert>
#include <cstddef>

#include "absl/base/config.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// Returns true if the provided rep is a FLAT, EXTERNAL or a SUBSTRING node
// holding a FLAT or EXTERNAL child rep. Requires `rep != nullptr`.
inline bool IsDataEdge(const CordRep* edge) {
  assert(edge != nullptr);

  // The fast path is that `edge` is an EXTERNAL or FLAT node, making the below
  // if a single, well predicted branch. We then repeat the FLAT or EXTERNAL
  // check in the slow path of the SUBSTRING check to optimize for the hot path.
  if (edge->tag == EXTERNAL || edge->tag >= FLAT) return true;
  if (edge->tag == SUBSTRING) edge = edge->substring()->child;
  return edge->tag == EXTERNAL || edge->tag >= FLAT;
}

// Returns the `absl::string_view` data reference for the provided data edge.
// Requires 'IsDataEdge(edge) == true`.
inline absl::string_view EdgeData(const CordRep* edge) {
  assert(IsDataEdge(edge));

  size_t offset = 0;
  const size_t length = edge->length;
  if (edge->IsSubstring()) {
    offset = edge->substring()->start;
    edge = edge->substring()->child;
  }
  return edge->tag >= FLAT
             ? absl::string_view{edge->flat()->Data() + offset, length}
             : absl::string_view{edge->external()->base + offset, length};
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_DATA_EDGE_H_
