// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/string-util.h"

#include <cinttypes>
#include <cmath>
#include <cstddef>

#include "src/base/platform/platform.h"
#include "src/inspector/protocol/Protocol.h"
#include "src/numbers/conversions.h"

namespace v8_inspector {

namespace protocol {
namespace {
std::pair<uint8_t, uint8_t> SplitByte(uint8_t byte, uint8_t split) {
  return {byte >> split, (byte & ((1 << split) - 1)) << (6 - split)};
}

v8::Maybe<uint8_t> DecodeByte(char byte) {
  if ('A' <= byte && byte <= 'Z') return v8::Just<uint8_t>(byte - 'A');
  if ('a' <= byte && byte <= 'z') return v8::Just<uint8_t>(byte - 'a' + 26);
  if ('0' <= byte && byte <= '9')
    return v8::Just<uint8_t>(byte - '0' + 26 + 26);
  if (byte == '+') return v8::Just<uint8_t>(62);
  if (byte == '/') return v8::Just<uint8_t>(63);
  return v8::Nothing<uint8_t>();
}
}  // namespace

String Binary::toBase64() const {
  const char* table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (size() == 0) return {};
  std::basic_string<UChar> result;
  result.reserve(4 * ((size() + 2) / 3));
  uint8_t last = 0;
  for (size_t n = 0; n < size();) {
    auto split = SplitByte((*bytes_)[n], 2 + 2 * (n % 3));
    result.push_back(table[split.first | last]);

    ++n;
    if (n < size() && n % 3 == 0) {
      result.push_back(table[split.second]);
      last = 0;
    } else {
      last = split.second;
    }
  }
  result.push_back(table[last]);
  while (result.size() % 4 > 0) result.push_back('=');
  return String16(std::move(result));
}

/* static */
Binary Binary::fromBase64(const String& base64, bool* success) {
  if (base64.isEmpty()) {
    *success = true;
    return {};
  }

  *success = false;
  // Fail if the length is invalid or decoding would overflow.
  if (base64.length() % 4 != 0 || base64.length() + 4 < base64.length()) {
    return {};
  }

  std::vector<uint8_t> result;
  result.reserve(3 * base64.length() / 4);
  char pad = '=';
  // Iterate groups of four
  for (size_t i = 0; i < base64.length(); i += 4) {
    uint8_t a = 0, b = 0, c = 0, d = 0;
    if (!DecodeByte(base64[i + 0]).To(&a)) return {};
    if (!DecodeByte(base64[i + 1]).To(&b)) return {};
    if (!DecodeByte(base64[i + 2]).To(&c)) {
      // Padding is allowed only in the group on the last two positions
      if (i + 4 < base64.length() || base64[i + 2] != pad ||
          base64[i + 3] != pad) {
        return {};
      }
    }
    if (!DecodeByte(base64[i + 3]).To(&d)) {
      // Padding is allowed only in the group on the last two positions
      if (i + 4 < base64.length() || base64[i + 3] != pad) {
        return {};
      }
    }

    result.push_back((a << 2) | (b >> 4));
    if (base64[i + 2] != '=') result.push_back((0xFF & (b << 4)) | (c >> 2));
    if (base64[i + 3] != '=') result.push_back((0xFF & (c << 6)) | d);
  }
  *success = true;
  return Binary(std::make_shared<std::vector<uint8_t>>(std::move(result)));
}
}  // namespace protocol

v8::Local<v8::String> toV8String(v8::Isolate* isolate, const String16& string) {
  if (string.isEmpty()) return v8::String::Empty(isolate);
  DCHECK_GT(v8::String::kMaxLength, string.length());
  return v8::String::NewFromTwoByte(
             isolate, reinterpret_cast<const uint16_t*>(string.characters16()),
             v8::NewStringType::kNormal, static_cast<int>(string.length()))
      .ToLocalChecked();
}

v8::Local<v8::String> toV8StringInternalized(v8::Isolate* isolate,
                                             const String16& string) {
  if (string.isEmpty()) return v8::String::Empty(isolate);
  DCHECK_GT(v8::String::kMaxLength, string.length());
  return v8::String::NewFromTwoByte(
             isolate, reinterpret_cast<const uint16_t*>(string.characters16()),
             v8::NewStringType::kInternalized,
             static_cast<int>(string.length()))
      .ToLocalChecked();
}

v8::Local<v8::String> toV8StringInternalized(v8::Isolate* isolate,
                                             const char* str) {
  return v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kInternalized)
      .ToLocalChecked();
}

v8::Local<v8::String> toV8String(v8::Isolate* isolate,
                                 const StringView& string) {
  if (!string.length()) return v8::String::Empty(isolate);
  DCHECK_GT(v8::String::kMaxLength, string.length());
  if (string.is8Bit())
    return v8::String::NewFromOneByte(
               isolate, reinterpret_cast<const uint8_t*>(string.characters8()),
               v8::NewStringType::kNormal, static_cast<int>(string.length()))
        .ToLocalChecked();
  return v8::String::NewFromTwoByte(
             isolate, reinterpret_cast<const uint16_t*>(string.characters16()),
             v8::NewStringType::kNormal, static_cast<int>(string.length()))
      .ToLocalChecked();
}

String16 toProtocolString(v8::Isolate* isolate, v8::Local<v8::String> value) {
  if (value.IsEmpty() || value->IsNullOrUndefined()) return String16();
  uint32_t length = value->Length();
  std::unique_ptr<UChar[]> buffer(new UChar[length]);
  value->WriteV2(isolate, 0, length, reinterpret_cast<uint16_t*>(buffer.get()));
  return String16(buffer.get(), length);
}

String16 toProtocolStringWithTypeCheck(v8::Isolate* isolate,
                                       v8::Local<v8::Value> value) {
  if (value.IsEmpty() || !value->IsString()) return String16();
  return toProtocolString(isolate, value.As<v8::String>());
}

String16 toString16(const StringView& string) {
  if (!string.length()) return String16();
  if (string.is8Bit())
    return String16(reinterpret_cast<const char*>(string.characters8()),
                    string.length());
  return String16(string.characters16(), string.length());
}

StringView toStringView(const String16& string) {
  if (string.isEmpty()) return StringView();
  return StringView(string.characters16(), string.length());
}

bool stringViewStartsWith(const StringView& string, const char* prefix) {
  if (!string.length()) return !(*prefix);
  if (string.is8Bit()) {
    for (size_t i = 0, j = 0; prefix[j] && i < string.length(); ++i, ++j) {
      if (string.characters8()[i] != prefix[j]) return false;
    }
  } else {
    for (size_t i = 0, j = 0; prefix[j] && i < string.length(); ++i, ++j) {
      if (string.characters16()[i] != prefix[j]) return false;
    }
  }
  return true;
}

namespace {
// An empty string buffer doesn't own any string data; its ::string() returns a
// default-constructed StringView instance.
class EmptyStringBuffer : public StringBuffer {
 public:
  StringView string() const override { return StringView(); }
};

// Contains LATIN1 text data or CBOR encoded binary data in a vector.
class StringBuffer8 : public StringBuffer {
 public:
  explicit StringBuffer8(std::vector<uint8_t> data) : data_(std::move(data)) {}

  StringView string() const override {
    return StringView(data_.data(), data_.size());
  }

 private:
  std::vector<uint8_t> data_;
};

// Contains a 16 bit string (String16).
class StringBuffer16 : public StringBuffer {
 public:
  explicit StringBuffer16(String16 data) : data_(std::move(data)) {}

  StringView string() const override {
    return StringView(data_.characters16(), data_.length());
  }

 private:
  String16 data_;
};
}  // namespace

// static
std::unique_ptr<StringBuffer> StringBuffer::create(StringView string) {
  if (string.length() == 0) return std::make_unique<EmptyStringBuffer>();
  if (string.is8Bit()) {
    return std::make_unique<StringBuffer8>(std::vector<uint8_t>(
        string.characters8(), string.characters8() + string.length()));
  }
  return std::make_unique<StringBuffer16>(
      String16(string.characters16(), string.length()));
}

std::unique_ptr<StringBuffer> StringBufferFrom(String16 str) {
  if (str.isEmpty()) return std::make_unique<EmptyStringBuffer>();
  return std::make_unique<StringBuffer16>(std::move(str));
}

std::unique_ptr<StringBuffer> StringBufferFrom(std::vector<uint8_t> str) {
  if (str.empty()) return std::make_unique<EmptyStringBuffer>();
  return std::make_unique<StringBuffer8>(std::move(str));
}

String16 stackTraceIdToString(uintptr_t id) {
  String16Builder builder;
  builder.appendNumber(static_cast<size_t>(id));
  return builder.toString();
}

}  // namespace v8_inspector

namespace v8_crdtp {

using v8_inspector::String16;
using v8_inspector::protocol::Binary;
using v8_inspector::protocol::StringUtil;

// static
bool ProtocolTypeTraits<String16>::Deserialize(DeserializerState* state,
                                               String16* value) {
  auto* tokenizer = state->tokenizer();
  if (tokenizer->TokenTag() == cbor::CBORTokenTag::STRING8) {
    const auto str = tokenizer->GetString8();
    *value = StringUtil::fromUTF8(str.data(), str.size());
    return true;
  }
  if (tokenizer->TokenTag() == cbor::CBORTokenTag::STRING16) {
    const auto str = tokenizer->GetString16WireRep();
    *value = StringUtil::fromUTF16LE(
        reinterpret_cast<const uint16_t*>(str.data()), str.size() / 2);
    return true;
  }
  state->RegisterError(Error::BINDINGS_STRING_VALUE_EXPECTED);
  return false;
}

// static
void ProtocolTypeTraits<String16>::Serialize(const String16& value,
                                             std::vector<uint8_t>* bytes) {
  cbor::EncodeFromUTF16(
      span<uint16_t>(reinterpret_cast<const uint16_t*>(value.characters16()),
                     value.length()),
      bytes);
}

// static
bool ProtocolTypeTraits<Binary>::Deserialize(DeserializerState* state,
                                             Binary* value) {
  auto* tokenizer = state->tokenizer();
  if (tokenizer->TokenTag() == cbor::CBORTokenTag::BINARY) {
    *value = Binary::fromSpan(tokenizer->GetBinary());
    return true;
  }
  if (tokenizer->TokenTag() == cbor::CBORTokenTag::STRING8) {
    const auto str_span = tokenizer->GetString8();
    auto str = StringUtil::fromUTF8(str_span.data(), str_span.size());
    bool success = false;
    *value = Binary::fromBase64(str, &success);
    return success;
  }
  state->RegisterError(Error::BINDINGS_BINARY_VALUE_EXPECTED);
  return false;
}

// static
void ProtocolTypeTraits<Binary>::Serialize(const Binary& value,
                                           std::vector<uint8_t>* bytes) {
  cbor::EncodeBinary(span<uint8_t>(value.data(), value.size()), bytes);
}

}  // namespace v8_crdtp
