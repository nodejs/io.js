// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_INSPECTOR_UTILS_H_
#define V8_TEST_INSPECTOR_UTILS_H_

#include <vector>

#include "include/v8-inspector.h"
#include "include/v8-local-handle.h"

namespace v8 {

class Isolate;
class String;

namespace internal {

std::vector<uint8_t> ToBytes(v8::Isolate*, v8::Local<v8::String>);

v8::Local<v8::String> ToV8String(v8::Isolate*, const char*);

v8::Local<v8::String> ToV8String(v8::Isolate*, const std::vector<uint8_t>&);

v8::Local<v8::String> ToV8String(v8::Isolate*, const std::string&);

v8::Local<v8::String> ToV8String(v8::Isolate*, const std::vector<uint16_t>&);

v8::Local<v8::String> ToV8String(v8::Isolate*, const v8_inspector::StringView&);

std::vector<uint16_t> ToVector(v8::Isolate*, v8::Local<v8::String>);

}  // namespace internal
}  // namespace v8

#endif  //  V8_TEST_INSPECTOR_UTILS_H_
