// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXTENSIONS_GC_EXTENSION_H_
#define V8_EXTENSIONS_GC_EXTENSION_H_

#include "include/v8-extension.h"
#include "include/v8-local-handle.h"
#include "src/base/strings.h"

namespace v8 {

template <typename T>
class FunctionCallbackInfo;

namespace internal {

// Provides garbage collection on invoking |fun_name|(options), where
// - options is a dictionary like object. See supported properties below.
// - no parameter defaults to options:
//   {type: 'major', execution: 'sync'}.
// - truthy parameter that is not setting any options:
//   {type: 'minor', execution: 'sync'}.
//
// Exceptions that occur during parsing the options bag are preserved and result
// in skipping the GC call.
//
// Supported options:
// - type:
//     - 'major': Full GC.
//     - 'minor': Young generation GC.
//     - 'major-snapshot': Full GC with taking a heap snapshot at the same time.
// - execution: 'sync' or 'async' for synchronous and asynchronous
//   execution, respectively.
// - flavor:
//     - 'regular': A regular GC.
//     - 'last-resort': A last resort GC.
// - filename: Filename for the snapshot in case the type was
//   'major-snapshot'.
//
// Returns a Promise that resolves when GC is done when asynchronous execution
// is requested, and undefined otherwise.
//
// Frequent use cases (assuming --expose-gc):
// 1. Just perform a GC to check whether things improve: `gc()`
// 2. Test that certain objects indeed are reclaimed:
//   `await gc({type:'major', execution:'async'})`
// 3. Same as 2. but with checking why things did go wrong in a snapshot:
//   `await gc({type:'major-snapshot', execution:'async'})`
// 4. Synchronous last resort GC:
//   `gc({type:'major',execution:'sync',flavor:'last-resort'})`
class GCExtension : public v8::Extension {
 public:
  explicit GCExtension(const char* fun_name)
      : v8::Extension("v8/gc",
                      BuildSource(buffer_, sizeof(buffer_), fun_name)) {}
  v8::Local<v8::FunctionTemplate> GetNativeFunctionTemplate(
      v8::Isolate* isolate, v8::Local<v8::String> name) override;
  static void GC(const v8::FunctionCallbackInfo<v8::Value>& info);

 private:
  static const char* BuildSource(char* buf, size_t size, const char* fun_name) {
    base::SNPrintF(base::VectorOf(buf, size), "native function %s();",
                   fun_name);
    return buf;
  }

  char buffer_[50];
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXTENSIONS_GC_EXTENSION_H_
