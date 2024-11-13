// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_ASYNC_GEN_H_
#define V8_BUILTINS_BUILTINS_ASYNC_GEN_H_

#include "src/builtins/builtins-promise-gen.h"
#include "src/objects/js-generator.h"

namespace v8 {
namespace internal {

class AsyncBuiltinsAssembler : public PromiseBuiltinsAssembler {
 public:
  explicit AsyncBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : PromiseBuiltinsAssembler(state) {}

 protected:
  // Perform steps to resume generator after `value` is resolved.
  // `on_reject` is the SharedFunctioninfo instance used to create the reject
  // closure. `on_resolve` is the SharedFunctioninfo instance used to create the
  // resolve closure. Returns the Promise-wrapped `value`.
  TNode<Object> Await(TNode<Context> context,
                      TNode<JSGeneratorObject> generator, TNode<Object> value,
                      TNode<JSPromise> outer_promise,
                      TNode<SharedFunctionInfo> on_resolve_sfi,
                      TNode<SharedFunctionInfo> on_reject_sfi);

  // Return a new built-in function object as defined in
  // Async Iterator Value Unwrap Functions
  TNode<JSFunction> CreateUnwrapClosure(TNode<NativeContext> native_context,
                                        TNode<Boolean> done);

 private:
  void InitializeNativeClosure(TNode<Context> context,
                               TNode<NativeContext> native_context,
                               TNode<HeapObject> function,
                               TNode<SharedFunctionInfo> shared_info);
  TNode<Context> AllocateAsyncIteratorValueUnwrapContext(
      TNode<NativeContext> native_context, TNode<Boolean> done);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_ASYNC_GEN_H_
