// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Slightly adapted for inclusion in V8.
// Copyright 2014 the V8 project authors. All rights reserved.
// List of adaptations:
// - include guard names
// - wrap in v8 namespace
// - include paths

#ifndef V8_BASE_SAFE_CONVERSIONS_ARM_IMPL_H_
#define V8_BASE_SAFE_CONVERSIONS_ARM_IMPL_H_

#include <stdint.h>

#include <type_traits>

#include "src/base/safe_conversions_impl.h"

namespace v8::base::internal {

// Fast saturation to a destination type.
template <typename Dst, typename Src>
struct SaturateFastAsmOp {
  static constexpr bool is_supported =
      kEnableAsmCode && std::is_signed_v<Src> && std::is_integral_v<Dst> &&
      std::is_integral_v<Src> &&
      IntegerBitsPlusSign<Src>::value <= IntegerBitsPlusSign<int32_t>::value &&
      IntegerBitsPlusSign<Dst>::value <= IntegerBitsPlusSign<int32_t>::value &&
      !IsTypeInRangeForNumericType<Dst, Src>::value;

  __attribute__((always_inline)) static Dst Do(Src value) {
    int32_t src = value;
    typename std::conditional<std::is_signed_v<Dst>, int32_t, uint32_t>::type
        result;
    if (std::is_signed_v<Dst>) {
      asm("ssat %[dst], %[shift], %[src]"
          : [dst] "=r"(result)
          : [src] "r"(src), [shift] "n"(IntegerBitsPlusSign<Dst>::value <= 32
                                            ? IntegerBitsPlusSign<Dst>::value
                                            : 32));
    } else {
      asm("usat %[dst], %[shift], %[src]"
          : [dst] "=r"(result)
          : [src] "r"(src), [shift] "n"(IntegerBitsPlusSign<Dst>::value < 32
                                            ? IntegerBitsPlusSign<Dst>::value
                                            : 31));
    }
    return static_cast<Dst>(result);
  }
};

}  // namespace v8::base::internal

#endif  // V8_BASE_SAFE_CONVERSIONS_ARM_IMPL_H_
