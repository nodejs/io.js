#include "node_string.h"
#include "node/inspector/protocol/Protocol.h"
#include "simdutf.h"
#include "util-inl.h"

namespace node {
namespace inspector {
namespace protocol {
namespace StringUtil {

size_t kNotFound = std::string::npos;

// NOLINTNEXTLINE(runtime/references) V8 API requirement
void builderAppendQuotedString(StringBuilder& builder,
                               const std::string_view string) {
  builder.put('"');
  if (!string.empty()) {
    size_t expected_utf16_length =
        simdutf::utf16_length_from_utf8(string.data(), string.length());
    MaybeStackBuffer<char16_t> buffer(expected_utf16_length);
    // simdutf::convert_utf8_to_utf16 returns zero in case of error.
    size_t utf16_length = simdutf::convert_utf8_to_utf16(
        string.data(), string.length(), buffer.out());
    // We have that utf16_length == expected_utf16_length if and only
    // if the input was a valid UTF-8 string.
    if (utf16_length != 0) {
      CHECK_EQ(expected_utf16_length, utf16_length);
      escapeWideStringForJSON(reinterpret_cast<const uint16_t*>(buffer.out()),
                              utf16_length,
                              &builder);
    }  // Otherwise, we had an invalid UTF-8 input.
  }
  builder.put('"');
}

std::unique_ptr<Value> parseJSON(const std::string_view string) {
  if (string.empty())
    return nullptr;
  size_t expected_utf16_length =
      simdutf::utf16_length_from_utf8(string.data(), string.length());
  MaybeStackBuffer<char16_t> buffer(expected_utf16_length);
  // simdutf::convert_utf8_to_utf16 returns zero in case of error.
  size_t utf16_length = simdutf::convert_utf8_to_utf16(
      string.data(), string.length(), buffer.out());
  // We have that utf16_length == expected_utf16_length if and only
  // if the input was a valid UTF-8 string.
  if (utf16_length == 0) return nullptr;  // We had an invalid UTF-8 input.
  CHECK_EQ(expected_utf16_length, utf16_length);
  return parseJSONCharacters(reinterpret_cast<const uint16_t*>(buffer.out()),
                             utf16_length);
}

std::unique_ptr<Value> parseJSON(v8_inspector::StringView string) {
  if (string.length() == 0)
    return nullptr;
  if (string.is8Bit())
    return parseJSONCharacters(string.characters8(), string.length());
  return parseJSONCharacters(string.characters16(), string.length());
}

String StringViewToUtf8(v8_inspector::StringView view) {
  if (view.length() == 0)
    return "";
  if (view.is8Bit()) {
    return std::string(reinterpret_cast<const char*>(view.characters8()),
                       view.length());
  }
  const char16_t* source =
      reinterpret_cast<const char16_t*>(view.characters16());
  size_t expected_utf8_length =
      simdutf::utf8_length_from_utf16(source, view.length());
  MaybeStackBuffer<char> buffer(expected_utf8_length);
  // convert_utf16_to_utf8 returns zero in case of error.
  size_t utf8_length =
      simdutf::convert_utf16_to_utf8(source, view.length(), buffer.out());
  // We have that utf8_length == expected_utf8_length if and only
  // if the input was a valid UTF-16 string. Otherwise, utf8_length
  // must be zero.
  CHECK(utf8_length == 0 || utf8_length == expected_utf8_length);
  // An invalid UTF-16 input will generate the empty string:
  return String(buffer.out(), utf8_length);
}

String fromDouble(double d) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());  // Ignore current locale
  stream << std::fixed << d;
  return stream.str();
}

double toDouble(const char* buffer, size_t length, bool* ok) {
  std::istringstream stream(std::string(buffer, length));
  stream.imbue(std::locale::classic());  // Ignore current locale
  double d;
  stream >> d;
  *ok = !stream.fail();
  return d;
}

ProtocolMessage jsonToMessage(String message) {
  return message;
}

ProtocolMessage binaryToMessage(std::vector<uint8_t> message) {
  return std::string(reinterpret_cast<const char*>(message.data()),
                     message.size());
}

String fromUTF8(const uint8_t* data, size_t length) {
  return std::string(reinterpret_cast<const char*>(data), length);
}

String fromUTF16(const uint16_t* data, size_t length) {
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

const uint8_t* CharactersUTF8(const std::string_view s) {
  return reinterpret_cast<const uint8_t*>(s.data());
}

size_t CharacterCount(const std::string_view s) {
  // The utf32_length_from_utf8 function calls count_utf8.
  // The count_utf8 function counts the number of code points
  // (characters) in the string, assuming that the string is valid Unicode.
  // TODO(@anonrig): Test to make sure CharacterCount returns correctly.
  return simdutf::utf32_length_from_utf8(s.data(), s.length());
}

}  // namespace StringUtil
}  // namespace protocol
}  // namespace inspector
}  // namespace node

