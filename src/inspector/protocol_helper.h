#ifndef SRC_INSPECTOR_PROTOCOL_HELPER_H_
#define SRC_INSPECTOR_PROTOCOL_HELPER_H_

#include "node/inspector/protocol/Protocol.h"
#include "util.h"
#include "v8-inspector.h"

namespace node::inspector {

// Convert a V8 string to v8_inspector StringBuffer, encoded in UTF16.
inline std::unique_ptr<v8_inspector::StringBuffer> ToInspectorString(
    v8::Isolate* isolate, v8::Local<v8::Value> value) {
  TwoByteValue buffer(isolate, value);
  return v8_inspector::StringBuffer::create(
      v8_inspector::StringView(*buffer, buffer.length()));
}

// Convert a V8 string to node::inspector::protocol::String, encoded in UTF8.
inline protocol::String ToProtocolString(v8::Isolate* isolate,
                                         v8::Local<v8::Value> value) {
  Utf8Value buffer(isolate, value);
  return *buffer;
}

}  // namespace node::inspector

#endif  // SRC_INSPECTOR_PROTOCOL_HELPER_H_
