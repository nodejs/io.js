// Copyright 2024 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: log/internal/structured_proto.h
// -----------------------------------------------------------------------------

#ifndef ABSL_LOG_INTERNAL_STRUCTURED_PROTO_H_
#define ABSL_LOG_INTERNAL_STRUCTURED_PROTO_H_

#include <cstddef>
#include <cstdint>

#include "absl/base/config.h"
#include "absl/log/internal/proto.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

// Sum type holding a single valid protobuf field suitable for encoding.
struct StructuredProtoField final {
  // Numeric type encoded with varint encoding:
  // https://protobuf.dev/programming-guides/encoding/#varints
  using Varint = absl::variant<uint64_t, int64_t, uint32_t, int32_t, bool>;

  // Fixed-length 64-bit integer encoding:
  // https://protobuf.dev/programming-guides/encoding/#non-varints
  using I64 = absl::variant<uint64_t, int64_t, double>;

  // Length-delimited record type (string, sub-message):
  // https://protobuf.dev/programming-guides/encoding/#length-types
  using LengthDelimited = absl::Span<const char>;

  // Fixed-length 32-bit integer encoding:
  // https://protobuf.dev/programming-guides/encoding/#non-varints
  using I32 = absl::variant<uint32_t, int32_t, float>;

  // Valid record type:
  // https://protobuf.dev/programming-guides/encoding/#structure
  using Value = absl::variant<Varint, I64, LengthDelimited, I32>;

  // Field number for the protobuf value.
  uint64_t field_number;

  // Value to encode.
  Value value;
};

// Estimates the number of bytes needed to encode `field` using
// protobuf encoding.
//
// The returned value might be larger than the actual number of bytes needed.
inline size_t BufferSizeForStructuredProtoField(StructuredProtoField field) {
  // Visitor to estimate the number of bytes of one of the types contained
  // inside `StructuredProtoField`.
  struct BufferSizeVisitor final {
    size_t operator()(StructuredProtoField::Varint /*unused*/) {
      return BufferSizeFor(field_number, WireType::kVarint);
    }

    size_t operator()(StructuredProtoField::I64 /*unused*/) {
      return BufferSizeFor(field_number, WireType::k64Bit);
    }

    size_t operator()(StructuredProtoField::LengthDelimited length_delimited) {
      return BufferSizeFor(field_number, WireType::kLengthDelimited) +
             length_delimited.size();
    }

    size_t operator()(StructuredProtoField::I32 /*unused*/) {
      return BufferSizeFor(field_number, WireType::k32Bit);
    }

    uint64_t field_number;
  };

  return absl::visit(BufferSizeVisitor{field.field_number}, field.value);
}

// Encodes `field` into `buf` using protobuf encoding.
//
// On success, returns `true` and advances `buf` to the end of
// the bytes consumed.
//
// On failure (if `buf` was too small), returns `false`.
bool EncodeStructuredProtoField(StructuredProtoField field,
                                absl::Span<char>& buf);

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_STRUCTURED_PROTO_H_
