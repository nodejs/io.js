// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_SIMD128_H_
#define V8_COMMON_SIMD128_H_

#include <cstdint>

#include "src/base/memory.h"
#include "src/common/globals.h"

namespace v8::internal {

#define FOREACH_SIMD_TYPE(V)     \
  V(double, float64x2, f64x2, 2) \
  V(float, float32x4, f32x4, 4)  \
  V(int64_t, int64x2, i64x2, 2)  \
  V(int32_t, int32x4, i32x4, 4)  \
  V(int16_t, int16x8, i16x8, 8)  \
  V(int8_t, int8x16, i8x16, 16)

#define DEFINE_SIMD_TYPE(cType, sType, name, kSize) \
  struct sType {                                    \
    cType val[kSize];                               \
  };
FOREACH_SIMD_TYPE(DEFINE_SIMD_TYPE)
#undef DEFINE_SIMD_TYPE

class alignas(double) Simd128 {
 public:
  Simd128() = default;

#define DEFINE_SIMD_TYPE_SPECIFIC_METHODS(cType, sType, name, size)          \
  explicit Simd128(sType val) {                                              \
    base::WriteUnalignedValue<sType>(reinterpret_cast<Address>(val_), val);  \
  }                                                                          \
  sType to_##name() const {                                                  \
    return base::ReadUnalignedValue<sType>(reinterpret_cast<Address>(val_)); \
  }
  FOREACH_SIMD_TYPE(DEFINE_SIMD_TYPE_SPECIFIC_METHODS)
#undef DEFINE_SIMD_TYPE_SPECIFIC_METHODS

  explicit Simd128(uint8_t* bytes) {
    memcpy(static_cast<void*>(val_), reinterpret_cast<void*>(bytes),
           v8::internal::kSimd128Size);
  }

  bool operator==(const Simd128& other) const noexcept {
    return memcmp(val_, other.val_, sizeof val_) == 0;
  }

  const uint8_t* bytes() { return val_; }

  template <typename T>
  inline T to() const;

 private:
  uint8_t val_[16] = {0};
};

#define DECLARE_CAST(cType, sType, name, size) \
  template <>                                  \
  inline sType Simd128::to() const {           \
    return to_##name();                        \
  }
FOREACH_SIMD_TYPE(DECLARE_CAST)
#undef DECLARE_CAST

}  // namespace v8::internal

#endif  // V8_COMMON_SIMD128_H_
