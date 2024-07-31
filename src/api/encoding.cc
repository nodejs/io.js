#include "node.h"
#include "string_bytes.h"
#include "util-inl.h"
#include "v8.h"

namespace node {

using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Value;

enum encoding ParseEncoding(const char* encoding,
                            enum encoding default_encoding) {
  switch (encoding[0]) {
    case 'u':
    case 'U':
      // Note: the two first conditions are needed for performance reasons
      // as "utf8"/"utf-8" is a common case.
      // (same for other cases below)

      // utf8, utf16le
      if (encoding[1] == 't' && encoding[2] == 'f') {
        // Skip `-`
        const size_t skip = encoding[3] == '-' ? 4 : 3;
        if (encoding[skip] == '8' && encoding[skip + 1] == '\0')
          return UTF8;
        if (strncmp(encoding + skip, "16le", 5) == 0)
          return UCS2;
      // ucs2
      } else if (encoding[1] == 'c' && encoding[2] == 's') {
        const size_t skip = encoding[3] == '-' ? 4 : 3;
        if (encoding[skip] == '2' && encoding[skip + 1] == '\0')
          return UCS2;
      }
      if (StringEqualNoCase(encoding, "utf8"))
        return UTF8;
      if (StringEqualNoCase(encoding, "utf-8"))
        return UTF8;
      if (StringEqualNoCase(encoding, "ucs2"))
        return UCS2;
      if (StringEqualNoCase(encoding, "ucs-2"))
        return UCS2;
      if (StringEqualNoCase(encoding, "utf16le"))
        return UCS2;
      if (StringEqualNoCase(encoding, "utf-16le"))
        return UCS2;
      break;

    case 'l':
    case 'L':
      // latin1
      if (encoding[1] == 'a') {
        if (strncmp(encoding + 2, "tin1", 5) == 0)
          return LATIN1;
      }
      if (StringEqualNoCase(encoding, "latin1"))
        return LATIN1;
      break;

    case 'b':
    case 'B':
      // binary is a deprecated alias of latin1
      if (encoding[1] == 'i') {
        if (strncmp(encoding + 2, "nary", 5) == 0)
          return LATIN1;
      // buffer
      } else if (encoding[1] == 'u') {
        if (strncmp(encoding + 2, "ffer", 5) == 0)
          return BUFFER;
      // base64
      } else if (encoding[1] == 'a') {
        if (strncmp(encoding + 2, "se64", 5) == 0)
          return BASE64;
        if (strncmp(encoding + 2, "se64url", 8) == 0)
          return BASE64URL;
      }
      if (StringEqualNoCase(encoding, "binary"))
        return LATIN1;  // BINARY is a deprecated alias of LATIN1.
      if (StringEqualNoCase(encoding, "buffer"))
        return BUFFER;
      if (StringEqualNoCase(encoding, "base64"))
        return BASE64;
      if (StringEqualNoCase(encoding, "base64url"))
        return BASE64URL;
      break;

    case 'a':
    case 'A':
      // ascii
      if (encoding[1] == 's') {
        if (strncmp(encoding + 2, "cii", 4) == 0)
          return ASCII;
      }
      if (StringEqualNoCase(encoding, "ascii"))
        return ASCII;
      break;

    case 'h':
    case 'H':
      // hex
      if (encoding[1] == 'e')
        if (encoding[2] == 'x' && encoding[3] == '\0')
          return HEX;
      if (StringEqualNoCase(encoding, "hex"))
        return HEX;
      break;
  }
  return default_encoding;
}

enum encoding ParseEncoding(Isolate* isolate,
                            Local<Value> encoding_v,
                            Local<Value> encoding_id,
                            enum encoding default_encoding) {
  if (encoding_id->IsUint32()) {
    return static_cast<enum encoding>(encoding_id.As<v8::Uint32>()->Value());
  }

  return ParseEncoding(isolate, encoding_v, default_encoding);
}

enum encoding ParseEncoding(Isolate* isolate,
                            Local<Value> encoding_v,
                            enum encoding default_encoding) {
  CHECK(!encoding_v.IsEmpty());

  if (!encoding_v->IsString())
    return default_encoding;

  Utf8Value encoding(isolate, encoding_v);

  return ParseEncoding(*encoding, default_encoding);
}

Local<Value> Encode(Isolate* isolate,
                    const char* buf,
                    size_t len,
                    enum encoding encoding) {
  CHECK_NE(encoding, UCS2);
  Local<Value> error;
  return StringBytes::Encode(isolate, buf, len, encoding, &error)
      .ToLocalChecked();
}

Local<Value> Encode(Isolate* isolate, const uint16_t* buf, size_t len) {
  Local<Value> error;
  return StringBytes::Encode(isolate, buf, len, &error)
      .ToLocalChecked();
}

// Returns -1 if the handle was not valid for decoding
ssize_t DecodeBytes(Isolate* isolate,
                    Local<Value> val,
                    enum encoding encoding) {
  HandleScope scope(isolate);

  return StringBytes::Size(isolate, val, encoding).FromMaybe(-1);
}

// Returns number of bytes written.
ssize_t DecodeWrite(Isolate* isolate,
                    char* buf,
                    size_t buflen,
                    Local<Value> val,
                    enum encoding encoding) {
  return StringBytes::Write(isolate, buf, buflen, val, encoding);
}

}  // namespace node
