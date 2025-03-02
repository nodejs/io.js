//
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

#include "absl/log/internal/structured_proto.h"

#include <cstdint>

#include "absl/base/config.h"
#include "absl/log/internal/proto.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

namespace {

// Handles protobuf-encoding a type contained inside
// `StructuredProtoField::Varint`.
struct VarintEncoderVisitor final {
  template <typename T>
  bool operator()(T value) const {
    return EncodeVarint(field_number, value, &buf);
  }

  uint64_t field_number;
  absl::Span<char>& buf;
};

// Handles protobuf-encoding a type contained inside
// `StructuredProtoField::I64`.
struct I64EncoderVisitor final {
  bool operator()(uint64_t value) const {
    return Encode64Bit(field_number, value, &buf);
  }

  bool operator()(int64_t value) const {
    return Encode64Bit(field_number, value, &buf);
  }

  bool operator()(double value) const {
    return EncodeDouble(field_number, value, &buf);
  }

  uint64_t field_number;
  absl::Span<char>& buf;
};

// Handles protobuf-encoding a type contained inside
// `StructuredProtoField::I32`.
struct I32EncoderVisitor final {
  bool operator()(uint32_t value) const {
    return Encode32Bit(field_number, value, &buf);
  }

  bool operator()(int32_t value) const {
    return Encode32Bit(field_number, value, &buf);
  }

  bool operator()(float value) const {
    return EncodeFloat(field_number, value, &buf);
  }

  uint64_t field_number;
  absl::Span<char>& buf;
};

// Handles protobuf-encoding a type contained inside `StructuredProtoField`.
struct EncoderVisitor final {
  bool operator()(StructuredProtoField::Varint varint) {
    return absl::visit(VarintEncoderVisitor{field_number, buf}, varint);
  }

  bool operator()(StructuredProtoField::I64 i64) {
    return absl::visit(I64EncoderVisitor{field_number, buf}, i64);
  }

  bool operator()(StructuredProtoField::LengthDelimited length_delimited) {
    // No need for a visitor, since `StructuredProtoField::LengthDelimited` is
    // just `absl::Span<const char>`.
    return EncodeBytes(field_number, length_delimited, &buf);
  }

  bool operator()(StructuredProtoField::I32 i32) {
    return absl::visit(I32EncoderVisitor{field_number, buf}, i32);
  }

  uint64_t field_number;
  absl::Span<char>& buf;
};

}  // namespace

bool EncodeStructuredProtoField(StructuredProtoField field,
                                absl::Span<char>& buf) {
  return absl::visit(EncoderVisitor{field.field_number, buf}, field.value);
}

}  // namespace log_internal

ABSL_NAMESPACE_END
}  // namespace absl
