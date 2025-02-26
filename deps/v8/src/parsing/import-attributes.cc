// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/import-attributes.h"

#include "src/ast/ast-value-factory.h"

namespace v8 {
namespace internal {

bool ImportAttributesKeyComparer::operator()(const AstRawString* lhs,
                                             const AstRawString* rhs) const {
  return AstRawString::Compare(lhs, rhs) < 0;
}

}  // namespace internal
}  // namespace v8
