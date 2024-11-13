// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ASMJS_ASM_JS_H_
#define V8_ASMJS_ASM_JS_H_

// Clients of this interface shouldn't depend on lots of asmjs internals.
// Do not include anything from src/asmjs here!
#include <memory>

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class AccountingAllocator;
class AsmWasmData;
class FunctionLiteral;
class JSArrayBuffer;
class ParseInfo;
class SharedFunctionInfo;
class UnoptimizedCompilationJob;

// Interface to compile and instantiate for asm.js modules.
class AsmJs {
 public:
  static std::unique_ptr<UnoptimizedCompilationJob> NewCompilationJob(
      ParseInfo* parse_info, FunctionLiteral* literal,
      AccountingAllocator* allocator);
  static MaybeHandle<Object> InstantiateAsmWasm(
      Isolate* isolate, DirectHandle<SharedFunctionInfo>,
      DirectHandle<AsmWasmData> wasm_data, Handle<JSReceiver> stdlib,
      Handle<JSReceiver> foreign, Handle<JSArrayBuffer> memory);

  // Special export name used to indicate that the module exports a single
  // function instead of a JavaScript object holding multiple functions.
  static const char* const kSingleFunctionName;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_ASMJS_ASM_JS_H_
