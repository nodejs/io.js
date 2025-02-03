// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_CONSTRUCTOR_GEN_H_
#define V8_BUILTINS_BUILTINS_CONSTRUCTOR_GEN_H_

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

class ConstructorBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit ConstructorBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  TNode<Context> FastNewFunctionContext(TNode<ScopeInfo> scope_info,
                                        TNode<Uint32T> slots,
                                        TNode<Context> context,
                                        ScopeType scope_type);

  TNode<JSRegExp> CreateRegExpLiteral(TNode<HeapObject> maybe_feedback_vector,
                                      TNode<TaggedIndex> slot,
                                      TNode<Object> pattern, TNode<Smi> flags,
                                      TNode<Context> context);

  TNode<JSArray> CreateShallowArrayLiteral(
      TNode<FeedbackVector> feedback_vector, TNode<TaggedIndex> slot,
      TNode<Context> context, AllocationSiteMode allocation_site_mode,
      Label* call_runtime);

  TNode<JSArray> CreateEmptyArrayLiteral(TNode<FeedbackVector> feedback_vector,
                                         TNode<TaggedIndex> slot,
                                         TNode<Context> context);

  TNode<HeapObject> CreateShallowObjectLiteral(
      TNode<FeedbackVector> feedback_vector, TNode<TaggedIndex> slot,
      Label* call_runtime);
  TNode<HeapObject> CreateShallowObjectLiteral(
      TNode<AllocationSite> allocation_site, TNode<JSObject> boilerplate,
      Label* call_runtime, bool bailout_if_dictionary = false);

  TNode<JSObject> CreateEmptyObjectLiteral(TNode<Context> context);

  TNode<JSObject> FastNewObject(TNode<Context> context,
                                TNode<JSFunction> target,
                                TNode<JSReceiver> new_target);

  TNode<JSObject> FastNewObject(TNode<Context> context,
                                TNode<JSFunction> target,
                                TNode<JSReceiver> new_target,
                                Label* call_runtime);

  void CopyMutableHeapNumbersInObject(TNode<HeapObject> copy,
                                      TNode<IntPtrT> start_offset,
                                      TNode<IntPtrT> instance_size);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_CONSTRUCTOR_GEN_H_
