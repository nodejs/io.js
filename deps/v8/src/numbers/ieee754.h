// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_IEEE754_H_
#define V8_NUMBERS_IEEE754_H_

#include "src/base/macros.h"
namespace v8::internal::math {

// Returns |x| to the power of |y|.
// The result of base ** exponent when base is 1 or -1 and exponent is
// +Infinity or -Infinity differs from IEEE 754-2008. The first edition
// of ECMAScript specified a result of NaN for this operation, whereas
// later versions of IEEE 754-2008 specified 1. The historical ECMAScript
// behaviour is preserved for compatibility reasons.
V8_EXPORT_PRIVATE double pow(double x, double y);

}  // namespace v8::internal::math

#endif  // V8_NUMBERS_IEEE754_H_
