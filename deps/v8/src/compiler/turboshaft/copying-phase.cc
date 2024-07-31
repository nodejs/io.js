// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/copying-phase.h"

namespace v8::internal::compiler::turboshaft {

int CountDecimalDigits(uint32_t value) {
  int result = 1;
  while (value > 9) {
    result++;
    value = value / 10;
  }
  return result;
}

std::ostream& operator<<(std::ostream& os, PaddingSpace padding) {
  if (padding.spaces > 10000) return os;
  for (int i = 0; i < padding.spaces; ++i) {
    os << ' ';
  }
  return os;
}

}  // namespace v8::internal::compiler::turboshaft
