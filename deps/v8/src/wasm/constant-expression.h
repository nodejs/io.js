// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_CONSTANT_EXPRESSION_H_
#define V8_WASM_CONSTANT_EXPRESSION_H_

#include <stdint.h>

#include <variant>

#include "src/base/bit-field.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-value.h"

namespace v8 {
namespace internal {

enum class MessageTemplate;
class WasmTrustedInstanceData;
class Zone;

namespace wasm {

class WireBytesRef;

// A representation of a constant expression. The most common expression types
// are hard-coded, while the rest are represented as a {WireBytesRef}.
class ConstantExpression {
 public:
  enum Kind {
    kEmpty,
    kI32Const,
    kRefNull,
    kRefFunc,
    kWireBytesRef,
    kLastKind = kWireBytesRef
  };

  constexpr ConstantExpression() = default;

  static constexpr ConstantExpression I32Const(int32_t value) {
    return ConstantExpression(ValueField::encode(value) |
                              KindField::encode(kI32Const));
  }
  static constexpr ConstantExpression RefFunc(uint32_t index) {
    return ConstantExpression(ValueField::encode(index) |
                              KindField::encode(kRefFunc));
  }
  static constexpr ConstantExpression RefNull(HeapType::Representation repr) {
    return ConstantExpression(ValueField::encode(repr) |
                              KindField::encode(kRefNull));
  }
  static constexpr ConstantExpression WireBytes(uint32_t offset,
                                                uint32_t length) {
    return ConstantExpression(OffsetField::encode(offset) |
                              LengthField::encode(length) |
                              KindField::encode(kWireBytesRef));
  }

  constexpr Kind kind() const { return KindField::decode(bit_field_); }

  constexpr bool is_set() const { return kind() != kEmpty; }

  constexpr uint32_t index() const {
    DCHECK_EQ(kind(), kRefFunc);
    return ValueField::decode(bit_field_);
  }

  constexpr HeapType::Representation repr() const {
    DCHECK_EQ(kind(), kRefNull);
    return static_cast<HeapType::Representation>(
        ValueField::decode(bit_field_));
  }

  constexpr int32_t i32_value() const {
    DCHECK_EQ(kind(), kI32Const);
    return ValueField::decode(bit_field_);
  }

  V8_EXPORT_PRIVATE WireBytesRef wire_bytes_ref() const;

 private:
  static constexpr int kValueBits = 32;
  static constexpr int kLengthBits = 30;
  static constexpr int kOffsetBits = 30;
  static constexpr int kKindBits = 3;

  // There are two possible combinations of fields: offset + length + kind if
  // kind = kWireBytesRef, or value + kind for anything else.
  using ValueField = base::BitField<uint32_t, 0, kValueBits, uint64_t>;
  using OffsetField = base::BitField<uint32_t, 0, kOffsetBits, uint64_t>;
  using LengthField = OffsetField::Next<uint32_t, kLengthBits>;
  using KindField = LengthField::Next<Kind, kKindBits>;

  // Make sure we reserve enough bits for a {WireBytesRef}'s length and offset.
  static_assert(kV8MaxWasmModuleSize <= LengthField::kMax + 1);
  static_assert(kV8MaxWasmModuleSize <= OffsetField::kMax + 1);
  // Make sure kind fits in kKindBits.
  static_assert(kLastKind <= KindField::kMax + 1);

  explicit constexpr ConstantExpression(uint64_t bit_field)
      : bit_field_(bit_field) {}

  uint64_t bit_field_ = 0;
};

// Verify that the default constructor initializes the {kind()} to {kEmpty}.
static_assert(ConstantExpression{}.kind() == ConstantExpression::kEmpty);

// We want to keep {ConstantExpression} small to reduce memory usage during
// compilation/instantiation.
static_assert(sizeof(ConstantExpression) <= 8);

using ValueOrError = std::variant<WasmValue, MessageTemplate>;

V8_INLINE bool is_error(ValueOrError result) {
  return std::holds_alternative<MessageTemplate>(result);
}
V8_INLINE MessageTemplate to_error(ValueOrError result) {
  return std::get<MessageTemplate>(result);
}
V8_INLINE WasmValue to_value(ValueOrError result) {
  return std::get<WasmValue>(result);
}

// Evaluates a constant expression.
// Returns a {WasmValue} if the evaluation succeeds, or an error as a
// {MessageTemplate} if it fails.
// Resets {zone} so make sure it contains no useful data.
ValueOrError EvaluateConstantExpression(
    Zone* zone, ConstantExpression expr, ValueType expected, Isolate* isolate,
    Handle<WasmTrustedInstanceData> trusted_instance_data,
    Handle<WasmTrustedInstanceData> shared_trusted_instance_data);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_CONSTANT_EXPRESSION_H_
