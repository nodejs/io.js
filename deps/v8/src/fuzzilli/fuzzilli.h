// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FUZZILLI_FUZZILLI_H_
#define V8_FUZZILLI_FUZZILLI_H_

#include "include/v8-extension.h"
#include "include/v8-local-handle.h"
#include "src/base/strings.h"

// REPRL = read-eval-print-reset-loop
// These file descriptors are being opened when Fuzzilli uses fork & execve to
// run V8.
#define REPRL_CRFD 100  // Control read file decriptor
#define REPRL_CWFD 101  // Control write file decriptor
#define REPRL_DRFD 102  // Data read file decriptor
#define REPRL_DWFD 103  // Data write file decriptor

namespace v8 {
namespace internal {

class FuzzilliExtension : public v8::Extension {
 public:
  explicit FuzzilliExtension(const char* fun_name)
      : v8::Extension("v8/fuzzilli",
                      BuildSource(buffer_, sizeof(buffer_), fun_name)) {}
  v8::Local<v8::FunctionTemplate> GetNativeFunctionTemplate(
      v8::Isolate* isolate, v8::Local<v8::String> name) override;
  static void Fuzzilli(const v8::FunctionCallbackInfo<v8::Value>& info);

 private:
  static const char* BuildSource(char* buf, size_t size, const char* fun_name) {
    base::SNPrintF(base::Vector<char>(buf, static_cast<int>(size)),
                   "native function %s();", fun_name);
    return buf;
  }

  char buffer_[50];
};

}  // namespace internal
}  // namespace v8

#endif  // V8_FUZZILLI_FUZZILLI_H_
