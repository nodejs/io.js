// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/hash-table-inl.h"

namespace v8 {
namespace internal {

Tagged<Object> RegisteredSymbolTable::SlowReverseLookup(Tagged<Object> value) {
  ReadOnlyRoots roots = GetReadOnlyRoots();
  for (InternalIndex i : this->IterateEntries()) {
    Tagged<Object> k;
    if (!this->ToKey(roots, i, &k)) continue;
    Tagged<Object> e = this->ValueAt(i);
    if (e == value) return k;
  }
  return roots.undefined_value();
}

}  // namespace internal
}  // namespace v8
