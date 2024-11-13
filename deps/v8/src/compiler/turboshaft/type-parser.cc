// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/type-parser.h"

#include <optional>

namespace v8::internal::compiler::turboshaft {

std::optional<Type> TypeParser::ParseType() {
  if (ConsumeIf("Word32")) {
    if (IsNext("{")) return ParseSet<Word32Type>();
    if (IsNext("[")) return ParseRange<Word32Type>();
    return Word32Type::Any();
  } else if (ConsumeIf("Word64")) {
    if (IsNext("{")) return ParseSet<Word64Type>();
    if (IsNext("[")) return ParseRange<Word64Type>();
    return Word64Type::Any();
  } else if (ConsumeIf("Float32")) {
    // TODO(nicohartmann@): Handle NaN.
    if (IsNext("{")) return ParseSet<Float32Type>();
    if (IsNext("[")) return ParseRange<Float32Type>();
    return Float64Type::Any();
  } else if (ConsumeIf("Float64")) {
    // TODO(nicohartmann@): Handle NaN.
    if (IsNext("{")) return ParseSet<Float64Type>();
    if (IsNext("[")) return ParseRange<Float64Type>();
    return Float64Type::Any();
  } else {
    return std::nullopt;
  }
}

}  // namespace v8::internal::compiler::turboshaft
