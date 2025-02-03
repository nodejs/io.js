// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_FLOAT16_H_
#define V8_WASM_FLOAT16_H_

#include "src/base/memory.h"
#include "third_party/fp16/src/include/fp16.h"

namespace v8 {
namespace internal {

class Float16 {
 public:
  Float16() : bits_(0) {}

  static Float16 Read(base::Address source) {
    return Float16(base::ReadUnalignedValue<uint16_t>(source));
  }

  void Write(base::Address destination) {
    return base::WriteUnalignedValue<uint16_t>(destination, bits_);
  }

  static Float16 FromFloat32(float f32) {
    return Float16(fp16_ieee_from_fp32_value(f32));
  }

  float ToFloat32() const { return fp16_ieee_to_fp32_value(bits_); }

 private:
  explicit Float16(uint16_t raw_bits) : bits_(raw_bits) {}

  uint16_t bits_;
};

static_assert(sizeof(Float16) == sizeof(uint16_t));

}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_FLOAT16_H_
