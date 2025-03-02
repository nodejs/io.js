// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXTENSIONS_CPUTRACEMARK_EXTENSION_H_
#define V8_EXTENSIONS_CPUTRACEMARK_EXTENSION_H_

#include "include/v8-extension.h"
#include "src/base/strings.h"

namespace v8 {

template <typename T>
class FunctionCallbackInfo;

namespace internal {

class CpuTraceMarkExtension : public v8::Extension {
 public:
  explicit CpuTraceMarkExtension(const char* fun_name)
      : v8::Extension("v8/cpumark",
                      BuildSource(buffer_, sizeof(buffer_), fun_name)) {}

  v8::Local<v8::FunctionTemplate> GetNativeFunctionTemplate(
      v8::Isolate* isolate, v8::Local<v8::String> name) override;

 private:
  static void Mark(const v8::FunctionCallbackInfo<v8::Value>& info);

  static const char* BuildSource(char* buf, size_t size, const char* fun_name) {
    base::SNPrintF(base::VectorOf(buf, size), "native function %s();",
                   fun_name);
    return buf;
  }

  char buffer_[50];
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXTENSIONS_CPUTRACEMARK_EXTENSION_H_
