// Copyright 2019 The V8 Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is V8 specific. It's not rolled from the upstream project.

#include "json_platform.h"

#include <cmath>
#include "../../../src/base/vector.h"
#include "../../../src/numbers/conversions.h"

namespace v8_crdtp {
namespace json {
namespace platform {
// Parses |str| into |result|. Returns false iff there are
// leftover characters or parsing errors.
bool StrToD(const char* str, double* result) {
  *result = v8::internal::StringToDouble(str, v8::internal::NO_CONVERSION_FLAG);
  return std::isfinite(*result);
}

// Prints |value| in a format suitable for JSON.
std::string DToStr(double value) {
  v8::base::ScopedVector<char> buffer(
      v8::internal::kDoubleToStringMinBufferSize);
  return std::string(v8::internal::DoubleToStringView(value, buffer));
}
}  // namespace platform
}  // namespace json
}  // namespace v8_crdtp
