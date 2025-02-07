#include "node_string.h"
#include "crdtp/json.h"
#include "node/inspector/protocol/Protocol.h"
#include "simdutf.h"
#include "util-inl.h"

namespace crdtp {

bool ProtocolTypeTraits<std::string>::Deserialize(DeserializerState* state,
                                                  std::string* value) {
  if (state->tokenizer()->TokenTag() == cbor::CBORTokenTag::STRING8) {
    span<uint8_t> cbor_span = state->tokenizer()->GetString8();
    value->assign(reinterpret_cast<const char*>(cbor_span.data()),
                  cbor_span.size());
    return true;
  }
  CHECK(state->tokenizer()->TokenTag() == cbor::CBORTokenTag::STRING16);
  span<uint8_t> utf16le = state->tokenizer()->GetString16WireRep();

  value->assign(node::inspector::protocol::StringUtil::fromUTF16LE(
      reinterpret_cast<const uint16_t*>(utf16le.data()),
      utf16le.size() / sizeof(uint16_t)));
  return true;
}

void ProtocolTypeTraits<std::string>::Serialize(const std::string& value,
                                                std::vector<uint8_t>* bytes) {
  cbor::EncodeString8(SpanFrom(value), bytes);
}

}  // namespace crdtp

namespace node {
namespace inspector {
namespace protocol {

String StringUtil::StringViewToUtf8(v8_inspector::StringView view) {
  if (view.length() == 0)
    return "";
  if (view.is8Bit()) {
    return std::string(reinterpret_cast<const char*>(view.characters8()),
                       view.length());
  }
  return fromUTF16(view.characters16(), view.length());
}

String StringUtil::fromUTF16(const uint16_t* data, size_t length) {
  auto casted_data = reinterpret_cast<const char16_t*>(data);
  size_t expected_utf8_length =
      simdutf::utf8_length_from_utf16(casted_data, length);
  MaybeStackBuffer<char> buffer(expected_utf8_length);
  // simdutf::convert_utf16_to_utf8 returns zero in case of error.
  size_t utf8_length =
      simdutf::convert_utf16_to_utf8(casted_data, length, buffer.out());
  // We have that utf8_length == expected_utf8_length if and only
  // if the input was a valid UTF-16 string. Otherwise, utf8_length
  // must be zero.
  CHECK(utf8_length == 0 || utf8_length == expected_utf8_length);
  // An invalid UTF-16 input will generate the empty string:
  return String(buffer.out(), utf8_length);
}

String StringUtil::fromUTF8(const uint8_t* data, size_t length) {
  return std::string(reinterpret_cast<const char*>(data), length);
}

String StringUtil::fromUTF16LE(const uint16_t* data, size_t length) {
  auto casted_data = reinterpret_cast<const char16_t*>(data);
  size_t expected_utf8_length =
      simdutf::utf8_length_from_utf16le(casted_data, length);
  MaybeStackBuffer<char> buffer(expected_utf8_length);
  // simdutf::convert_utf16le_to_utf8 returns zero in case of error.
  size_t utf8_length =
      simdutf::convert_utf16le_to_utf8(casted_data, length, buffer.out());
  // We have that utf8_length == expected_utf8_length if and only
  // if the input was a valid UTF-16 string. Otherwise, utf8_length
  // must be zero.
  CHECK(utf8_length == 0 || utf8_length == expected_utf8_length);
  // An invalid UTF-16 input will generate the empty string:
  return String(buffer.out(), utf8_length);
}

const uint8_t* StringUtil::CharactersUTF8(const std::string_view s) {
  return reinterpret_cast<const uint8_t*>(s.data());
}

size_t StringUtil::CharacterCount(const std::string_view s) {
  // Return the length of underlying representation storage.
  // E.g. for std::basic_string_view<char>, return its byte length.
  // If we adopt a variant underlying store string type, like
  // `v8_inspector::StringView`, for UTF16, return the length of the
  // underlying uint16_t store.
  return s.length();
}

}  // namespace protocol
}  // namespace inspector
}  // namespace node
