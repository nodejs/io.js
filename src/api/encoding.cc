#include "node.h"
#include "string_bytes.h"
#include "util-inl.h"
#include "v8.h"

namespace node {

using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Value;

Local<Value> Encode(Isolate* isolate,
                    const char* buf,
                    size_t len,
                    ENCODING encoding) {
  CHECK_NE(encoding, UTF16LE);
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
                    ENCODING encoding) {
  HandleScope scope(isolate);

  return StringBytes::Size(isolate, val, encoding).FromMaybe(-1);
}

// Returns number of bytes written.
ssize_t DecodeWrite(Isolate* isolate,
                    char* buf,
                    size_t buflen,
                    Local<Value> val,
                    ENCODING encoding) {
  return StringBytes::Write(isolate, buf, buflen, val, encoding);
}

}  // namespace node
