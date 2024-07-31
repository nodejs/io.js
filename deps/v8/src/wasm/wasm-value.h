// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_VALUE_H_
#define V8_WASM_WASM_VALUE_H_

#include "src/base/memory.h"
#include "src/handles/handles.h"
#include "src/utils/boxed-float.h"
#include "src/wasm/value-type.h"

namespace v8 {
namespace internal {
namespace wasm {

#define FOREACH_SIMD_TYPE(V)  \
  V(double, float2, f64x2, 2) \
  V(float, float4, f32x4, 4)  \
  V(int64_t, int2, i64x2, 2)  \
  V(int32_t, int4, i32x4, 4)  \
  V(int16_t, int8, i16x8, 8)  \
  V(int8_t, int16, i8x16, 16)

#define DEFINE_SIMD_TYPE(cType, sType, name, kSize) \
  struct sType {                                    \
    cType val[kSize];                               \
  };
FOREACH_SIMD_TYPE(DEFINE_SIMD_TYPE)
#undef DEFINE_SIMD_TYPE

class Simd128 {
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
           kSimd128Size);
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

// Macro for defining WasmValue methods for different types.
// Elements:
// - name (for to_<name>() method)
// - wasm type
// - c type
#define FOREACH_PRIMITIVE_WASMVAL_TYPE(V) \
  V(i8, kWasmI8, int8_t)                  \
  V(i16, kWasmI16, int16_t)               \
  V(i32, kWasmI32, int32_t)               \
  V(u32, kWasmI32, uint32_t)              \
  V(i64, kWasmI64, int64_t)               \
  V(u64, kWasmI64, uint64_t)              \
  V(f32, kWasmF32, float)                 \
  V(f32_boxed, kWasmF32, Float32)         \
  V(f64, kWasmF64, double)                \
  V(f64_boxed, kWasmF64, Float64)         \
  V(s128, kWasmS128, Simd128)

ASSERT_TRIVIALLY_COPYABLE(Handle<Object>);

// A wasm value with type information.
class WasmValue {
 public:
  WasmValue() : type_(kWasmVoid), bit_pattern_{} {}

#define DEFINE_TYPE_SPECIFIC_METHODS(name, localtype, ctype)                  \
  explicit WasmValue(ctype v) : type_(localtype), bit_pattern_{} {            \
    static_assert(sizeof(ctype) <= sizeof(bit_pattern_),                      \
                  "size too big for WasmValue");                              \
    base::WriteUnalignedValue<ctype>(reinterpret_cast<Address>(bit_pattern_), \
                                     v);                                      \
  }                                                                           \
  ctype to_##name() const {                                                   \
    DCHECK_EQ(localtype, type_);                                              \
    return to_##name##_unchecked();                                           \
  }                                                                           \
  ctype to_##name##_unchecked() const {                                       \
    return base::ReadUnalignedValue<ctype>(                                   \
        reinterpret_cast<Address>(bit_pattern_));                             \
  }

  FOREACH_PRIMITIVE_WASMVAL_TYPE(DEFINE_TYPE_SPECIFIC_METHODS)
#undef DEFINE_TYPE_SPECIFIC_METHODS

  WasmValue(const uint8_t* raw_bytes, ValueType type)
      : type_(type), bit_pattern_{} {
    DCHECK(type_.is_numeric());
    memcpy(bit_pattern_, raw_bytes, type.value_kind_size());
  }

  WasmValue(Handle<Object> ref, ValueType type) : type_(type), bit_pattern_{} {
    static_assert(sizeof(Handle<Object>) <= sizeof(bit_pattern_),
                  "bit_pattern_ must be large enough to fit a Handle");
    DCHECK(type.is_reference());
    base::WriteUnalignedValue<Handle<Object>>(
        reinterpret_cast<Address>(bit_pattern_), ref);
  }

  Handle<Object> to_ref() const {
    DCHECK(type_.is_reference());
    return base::ReadUnalignedValue<Handle<Object>>(
        reinterpret_cast<Address>(bit_pattern_));
  }

  ValueType type() const { return type_; }

  // Checks equality of type and bit pattern (also for float and double values).
  bool operator==(const WasmValue& other) const {
    return type_ == other.type_ &&
           !memcmp(bit_pattern_, other.bit_pattern_,
                   type_.is_reference() ? sizeof(Handle<Object>)
                                        : type_.value_kind_size());
  }

  void CopyTo(uint8_t* to) const {
    static_assert(sizeof(float) == sizeof(Float32));
    static_assert(sizeof(double) == sizeof(Float64));
    DCHECK(type_.is_numeric());
    memcpy(to, bit_pattern_, type_.value_kind_size());
  }

  // If {packed_type.is_packed()}, create a new value of {packed_type()}.
  // Otherwise, return this object.
  WasmValue Packed(ValueType packed_type) const {
    if (packed_type == kWasmI8) {
      DCHECK_EQ(type_, kWasmI32);
      return WasmValue(static_cast<int8_t>(to_i32()));
    }
    if (packed_type == kWasmI16) {
      DCHECK_EQ(type_, kWasmI32);
      return WasmValue(static_cast<int16_t>(to_i32()));
    }
    return *this;
  }

  template <typename T>
  inline T to() const;

  template <typename T>
  inline T to_unchecked() const;

  static WasmValue ForUintPtr(uintptr_t value) {
    using type =
        std::conditional<kSystemPointerSize == 8, uint64_t, uint32_t>::type;
    return WasmValue{type{value}};
  }

  inline std::string to_string() const {
    switch (type_.kind()) {
      case kI8:
        return std::to_string(to_i8());
      case kI16:
        return std::to_string(to_i16());
      case kI32:
        return std::to_string(to_i32());
      case kI64:
        return std::to_string(to_i64());
      case kF32:
        return std::to_string(to_f32());
      case kF64:
        return std::to_string(to_f64());
      case kS128: {
        std::stringstream stream;
        stream << "0x" << std::hex;
        for (int8_t uint8_t : bit_pattern_) {
          if (!(uint8_t & 0xf0)) stream << '0';
          stream << uint8_t;
        }
        return stream.str();
      }
      case kRefNull:
      case kRef:
      case kRtt:
        return "Handle [" + std::to_string(to_ref().address()) + "]";
      case kVoid:
      case kBottom:
        UNREACHABLE();
    }
  }

  bool zero_byte_representation() {
    DCHECK(type().is_numeric());
    uint32_t byte_count = type().value_kind_size();
    return static_cast<uint32_t>(std::count(
               bit_pattern_, bit_pattern_ + byte_count, 0)) == byte_count;
  }

 private:
  ValueType type_;
  uint8_t bit_pattern_[16];
};

#define DECLARE_CAST(name, localtype, ctype, ...) \
  template <>                                     \
  inline ctype WasmValue::to_unchecked() const {  \
    return to_##name##_unchecked();               \
  }                                               \
  template <>                                     \
  inline ctype WasmValue::to() const {            \
    return to_##name();                           \
  }
FOREACH_PRIMITIVE_WASMVAL_TYPE(DECLARE_CAST)
#undef DECLARE_CAST

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_VALUE_H_
