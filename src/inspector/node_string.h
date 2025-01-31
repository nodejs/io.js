// Bridges V8 Inspector generated code with the std::string used by the Node
// Compare to V8 counterpart - deps/v8/src/inspector/string-util.h
#ifndef SRC_INSPECTOR_NODE_STRING_H_
#define SRC_INSPECTOR_NODE_STRING_H_

#include "crdtp/protocol_core.h"
#include "util.h"
#include "v8-inspector.h"

#include <cstring>
#include <sstream>
#include <string>

namespace crdtp {

template <>
struct ProtocolTypeTraits<std::string> {
  static bool Deserialize(DeserializerState* state, std::string* value);
  static void Serialize(const std::string& value, std::vector<uint8_t>* bytes);
};

}  // namespace crdtp

namespace node {
namespace inspector {
namespace protocol {

class Value;

using String = std::string;
using StringBuilder = std::ostringstream;
using ProtocolMessage = std::string;

// Implements StringUtil methods used in `inspector_protocol/lib/`.
// Refer to
// https://source.chromium.org/chromium/chromium/src/+/main:third_party/blink/renderer/core/inspector/v8_inspector_string.h;l=40;drc=2b15d6974a49d3a14d3d67ae099a649d523a828d
// for more details about the interface.
struct StringUtil {
  // Convert Utf16 in local endianness to Utf8 if needed.
  static String StringViewToUtf8(v8_inspector::StringView view);
  static String fromUTF16(const uint16_t* data, size_t length);

  static String fromUTF8(const uint8_t* data, size_t length);
  static String fromUTF16LE(const uint16_t* data, size_t length);
  static const uint8_t* CharactersUTF8(const std::string_view s);
  static size_t CharacterCount(const std::string_view s);

  // Unimplemented. The generated code will fall back to CharactersUTF8().
  inline static uint8_t* CharactersLatin1(const std::string_view s) {
    return nullptr;
  }
  inline static const uint16_t* CharactersUTF16(const std::string_view s) {
    return nullptr;
  }
};

// A read-only sequence of uninterpreted bytes with reference-counted storage.
// Though the templates for generating the protocol bindings reference
// this type, js_protocol.pdl doesn't have a field of type 'binary', so
// therefore it's unnecessary to provide an implementation here.
class Binary {
 public:
  const uint8_t* data() const { UNREACHABLE(); }
  size_t size() const { UNREACHABLE(); }
  String toBase64() const { UNREACHABLE(); }
  static Binary fromBase64(const std::string_view base64, bool* success) {
    UNREACHABLE();
  }
  static Binary fromSpan(const uint8_t* data, size_t size) { UNREACHABLE(); }
};

}  // namespace protocol
}  // namespace inspector
}  // namespace node

#endif  // SRC_INSPECTOR_NODE_STRING_H_
