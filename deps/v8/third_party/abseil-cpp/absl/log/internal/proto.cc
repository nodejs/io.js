// Copyright 2020 The Abseil Authors.
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

#include "absl/log/internal/proto.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {
void EncodeRawVarint(uint64_t value, size_t size, absl::Span<char> *buf) {
  for (size_t s = 0; s < size; s++) {
    (*buf)[s] = static_cast<char>((value & 0x7f) | (s + 1 == size ? 0 : 0x80));
    value >>= 7;
  }
  buf->remove_prefix(size);
}
}  // namespace

bool EncodeVarint(uint64_t tag, uint64_t value, absl::Span<char> *buf) {
  const uint64_t tag_type = MakeTagType(tag, WireType::kVarint);
  const size_t tag_type_size = VarintSize(tag_type);
  const size_t value_size = VarintSize(value);
  if (tag_type_size + value_size > buf->size()) {
    buf->remove_suffix(buf->size());
    return false;
  }
  EncodeRawVarint(tag_type, tag_type_size, buf);
  EncodeRawVarint(value, value_size, buf);
  return true;
}

bool Encode64Bit(uint64_t tag, uint64_t value, absl::Span<char> *buf) {
  const uint64_t tag_type = MakeTagType(tag, WireType::k64Bit);
  const size_t tag_type_size = VarintSize(tag_type);
  if (tag_type_size + sizeof(value) > buf->size()) {
    buf->remove_suffix(buf->size());
    return false;
  }
  EncodeRawVarint(tag_type, tag_type_size, buf);
  for (size_t s = 0; s < sizeof(value); s++) {
    (*buf)[s] = static_cast<char>(value & 0xff);
    value >>= 8;
  }
  buf->remove_prefix(sizeof(value));
  return true;
}

bool Encode32Bit(uint64_t tag, uint32_t value, absl::Span<char> *buf) {
  const uint64_t tag_type = MakeTagType(tag, WireType::k32Bit);
  const size_t tag_type_size = VarintSize(tag_type);
  if (tag_type_size + sizeof(value) > buf->size()) {
    buf->remove_suffix(buf->size());
    return false;
  }
  EncodeRawVarint(tag_type, tag_type_size, buf);
  for (size_t s = 0; s < sizeof(value); s++) {
    (*buf)[s] = static_cast<char>(value & 0xff);
    value >>= 8;
  }
  buf->remove_prefix(sizeof(value));
  return true;
}

bool EncodeBytes(uint64_t tag, absl::Span<const char> value,
                 absl::Span<char> *buf) {
  const uint64_t tag_type = MakeTagType(tag, WireType::kLengthDelimited);
  const size_t tag_type_size = VarintSize(tag_type);
  uint64_t length = value.size();
  const size_t length_size = VarintSize(length);
  if (tag_type_size + length_size + value.size() > buf->size()) {
    buf->remove_suffix(buf->size());
    return false;
  }
  EncodeRawVarint(tag_type, tag_type_size, buf);
  EncodeRawVarint(length, length_size, buf);
  memcpy(buf->data(), value.data(), value.size());
  buf->remove_prefix(value.size());
  return true;
}

bool EncodeBytesTruncate(uint64_t tag, absl::Span<const char> value,
                         absl::Span<char> *buf) {
  const uint64_t tag_type = MakeTagType(tag, WireType::kLengthDelimited);
  const size_t tag_type_size = VarintSize(tag_type);
  uint64_t length = value.size();
  const size_t length_size =
      VarintSize(std::min<uint64_t>(length, buf->size()));
  if (tag_type_size + length_size <= buf->size() &&
      tag_type_size + length_size + value.size() > buf->size()) {
    value.remove_suffix(tag_type_size + length_size + value.size() -
                        buf->size());
    length = value.size();
  }
  if (tag_type_size + length_size + value.size() > buf->size()) {
    buf->remove_suffix(buf->size());
    return false;
  }
  EncodeRawVarint(tag_type, tag_type_size, buf);
  EncodeRawVarint(length, length_size, buf);
  memcpy(buf->data(), value.data(), value.size());
  buf->remove_prefix(value.size());
  return true;
}

ABSL_MUST_USE_RESULT absl::Span<char> EncodeMessageStart(
    uint64_t tag, uint64_t max_size, absl::Span<char> *buf) {
  const uint64_t tag_type = MakeTagType(tag, WireType::kLengthDelimited);
  const size_t tag_type_size = VarintSize(tag_type);
  max_size = std::min<uint64_t>(max_size, buf->size());
  const size_t length_size = VarintSize(max_size);
  if (tag_type_size + length_size > buf->size()) {
    buf->remove_suffix(buf->size());
    return absl::Span<char>();
  }
  EncodeRawVarint(tag_type, tag_type_size, buf);
  const absl::Span<char> ret = buf->subspan(0, length_size);
  EncodeRawVarint(0, length_size, buf);
  return ret;
}

void EncodeMessageLength(absl::Span<char> msg, const absl::Span<char> *buf) {
  if (!msg.data()) return;
  assert(buf->data() >= msg.data());
  if (buf->data() < msg.data()) return;
  EncodeRawVarint(
      static_cast<uint64_t>(buf->data() - (msg.data() + msg.size())),
      msg.size(), &msg);
}

namespace {
uint64_t DecodeVarint(absl::Span<const char> *buf) {
  uint64_t value = 0;
  size_t s = 0;
  while (s < buf->size()) {
    value |= static_cast<uint64_t>(static_cast<unsigned char>((*buf)[s]) & 0x7f)
             << 7 * s;
    if (!((*buf)[s++] & 0x80)) break;
  }
  buf->remove_prefix(s);
  return value;
}

uint64_t Decode64Bit(absl::Span<const char> *buf) {
  uint64_t value = 0;
  size_t s = 0;
  while (s < buf->size()) {
    value |= static_cast<uint64_t>(static_cast<unsigned char>((*buf)[s]))
             << 8 * s;
    if (++s == sizeof(value)) break;
  }
  buf->remove_prefix(s);
  return value;
}

uint32_t Decode32Bit(absl::Span<const char> *buf) {
  uint32_t value = 0;
  size_t s = 0;
  while (s < buf->size()) {
    value |= static_cast<uint32_t>(static_cast<unsigned char>((*buf)[s]))
             << 8 * s;
    if (++s == sizeof(value)) break;
  }
  buf->remove_prefix(s);
  return value;
}
}  // namespace

bool ProtoField::DecodeFrom(absl::Span<const char> *data) {
  if (data->empty()) return false;
  const uint64_t tag_type = DecodeVarint(data);
  tag_ = tag_type >> 3;
  type_ = static_cast<WireType>(tag_type & 0x07);
  switch (type_) {
    case WireType::kVarint:
      value_ = DecodeVarint(data);
      break;
    case WireType::k64Bit:
      value_ = Decode64Bit(data);
      break;
    case WireType::kLengthDelimited: {
      value_ = DecodeVarint(data);
      data_ = data->subspan(
          0, static_cast<size_t>(std::min<uint64_t>(value_, data->size())));
      data->remove_prefix(data_.size());
      break;
    }
    case WireType::k32Bit:
      value_ = Decode32Bit(data);
      break;
  }
  return true;
}

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
