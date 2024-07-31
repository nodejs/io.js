// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_CALL_GEN_H_
#define V8_BUILTINS_BUILTINS_CALL_GEN_H_

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

class CallOrConstructBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit CallOrConstructBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  void CallOrConstructWithArrayLike(TNode<Object> target,
                                    base::Optional<TNode<Object>> new_target,
                                    TNode<Object> arguments_list,
                                    TNode<Context> context);
  void CallOrConstructDoubleVarargs(TNode<Object> target,
                                    base::Optional<TNode<Object>> new_target,
                                    TNode<FixedDoubleArray> elements,
                                    TNode<Int32T> length,
                                    TNode<Int32T> args_count,
                                    TNode<Context> context, TNode<Int32T> kind);
  void CallOrConstructWithSpread(TNode<Object> target,
                                 base::Optional<TNode<Object>> new_target,
                                 TNode<Object> spread, TNode<Int32T> args_count,
                                 TNode<Context> context);

  template <class Descriptor>
  void CallReceiver(Builtin id, base::Optional<TNode<Object>> = base::nullopt);
  template <class Descriptor>
  void CallReceiver(Builtin id, TNode<Int32T> argc, TNode<UintPtrT> slot,
                    base::Optional<TNode<Object>> = base::nullopt);

  enum class CallFunctionTemplateMode : uint8_t {
    // This version is for using from IC system and generic builtins like
    // HandleApiCallOrConstruct. It does both access and receiver compatibility
    // checks if necessary and uses CallApiCallbackGeneric for calling Api
    // function in order to support side-effects checking and make the Api
    // function show up in the stack trace in case of exception.
    kGeneric,

    // These versions are used for generating calls from optimized code with
    // respective checks and use CallApiCallbackOptimized for calling Api
    // function.
    kCheckAccess,
    kCheckCompatibleReceiver,
    kCheckAccessAndCompatibleReceiver,
  };
  constexpr static bool IsAccessCheckRequired(CallFunctionTemplateMode mode);

  void CallFunctionTemplate(CallFunctionTemplateMode mode,
                            TNode<FunctionTemplateInfo> function_template_info,
                            TNode<Int32T> argc, TNode<Context> context,
                            TNode<Object> maybe_incumbent_context);

  void BuildConstruct(TNode<Object> target, TNode<Object> new_target,
                      TNode<Int32T> argc, const LazyNode<Context>& context,
                      const LazyNode<HeapObject>& feedback_vector,
                      TNode<UintPtrT> slot, UpdateFeedbackMode mode);

  void BuildConstructWithSpread(TNode<Object> target, TNode<Object> new_target,
                                TNode<Object> spread, TNode<Int32T> argc,
                                const LazyNode<Context>& context,
                                const LazyNode<HeapObject>& feedback_vector,
                                TNode<UintPtrT> slot, UpdateFeedbackMode mode);

  void BuildConstructForwardAllArgs(TNode<Object> target,
                                    TNode<Object> new_target,
                                    const LazyNode<Context>& context,
                                    const LazyNode<HeapObject>& feedback_vector,
                                    TNode<UintPtrT> slot);

  TNode<JSReceiver> GetCompatibleReceiver(TNode<JSReceiver> receiver,
                                          TNode<HeapObject> signature,
                                          TNode<Context> context);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_CALL_GEN_H_
