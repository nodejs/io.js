// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/inspector/utils.h"

#include <vector>

#include "include/v8-inspector.h"
#include "include/v8-primitive.h"

namespace v8 {
namespace internal {

std::vector<uint8_t> ToBytes(v8::Isolate* isolate, v8::Local<v8::String> str) {
  std::vector<uint8_t> buffer(str->Length());
  str->WriteOneByte(isolate, buffer.data(), 0, str->Length());
  return buffer;
}

v8::Local<v8::String> ToV8String(v8::Isolate* isolate, const char* str) {
  return v8::String::NewFromUtf8(isolate, str).ToLocalChecked();
}

v8::Local<v8::String> ToV8String(v8::Isolate* isolate,
                                 const std::vector<uint8_t>& bytes) {
  return v8::String::NewFromOneByte(isolate, bytes.data(),
                                    v8::NewStringType::kNormal,
                                    static_cast<int>(bytes.size()))
      .ToLocalChecked();
}

v8::Local<v8::String> ToV8String(v8::Isolate* isolate,
                                 const std::string& buffer) {
  int length = static_cast<int>(buffer.size());
  return v8::String::NewFromUtf8(isolate, buffer.data(),
                                 v8::NewStringType::kNormal, length)
      .ToLocalChecked();
}

v8::Local<v8::String> ToV8String(v8::Isolate* isolate,
                                 const std::vector<uint16_t>& buffer) {
  int length = static_cast<int>(buffer.size());
  return v8::String::NewFromTwoByte(isolate, buffer.data(),
                                    v8::NewStringType::kNormal, length)
      .ToLocalChecked();
}

v8::Local<v8::String> ToV8String(v8::Isolate* isolate,
                                 const v8_inspector::StringView& string) {
  if (string.is8Bit()) {
    return v8::String::NewFromOneByte(isolate, string.characters8(),
                                      v8::NewStringType::kNormal,
                                      static_cast<int>(string.length()))
        .ToLocalChecked();
  }
  return v8::String::NewFromTwoByte(isolate, string.characters16(),
                                    v8::NewStringType::kNormal,
                                    static_cast<int>(string.length()))
      .ToLocalChecked();
}

std::vector<uint16_t> ToVector(v8::Isolate* isolate,
                               v8::Local<v8::String> str) {
  std::vector<uint16_t> buffer(str->Length());
  str->Write(isolate, buffer.data(), 0, str->Length());
  return buffer;
}

}  // namespace internal
}  // namespace v8
