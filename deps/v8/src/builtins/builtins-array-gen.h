// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_ARRAY_GEN_H_
#define V8_BUILTINS_BUILTINS_ARRAY_GEN_H_

#include "src/codegen/code-factory.h"  // for enum AllocationSiteOverrideMode
#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

class ArrayBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit ArrayBuiltinsAssembler(compiler::CodeAssemblerState* state);

  using BuiltinResultGenerator =
      std::function<void(ArrayBuiltinsAssembler* masm)>;

  using CallResultProcessor = std::function<TNode<Object>(
      ArrayBuiltinsAssembler* masm, TNode<Object> k_value, TNode<UintPtrT> k)>;

  void TypedArrayMapResultGenerator();

  // See tc39.github.io/ecma262/#sec-%typedarray%.prototype.map.
  TNode<Object> TypedArrayMapProcessor(TNode<Object> k_value,
                                       TNode<UintPtrT> k);

  TNode<String> CallJSArrayArrayJoinConcatToSequentialString(
      TNode<FixedArray> fixed_array, TNode<IntPtrT> length, TNode<String> sep,
      TNode<String> dest) {
    TNode<ExternalReference> func = ExternalConstant(
        ExternalReference::jsarray_array_join_concat_to_sequential_string());
    TNode<ExternalReference> isolate_ptr =
        ExternalConstant(ExternalReference::isolate_address(isolate()));
    return UncheckedCast<String>(
        CallCFunction(func,
                      MachineType::AnyTagged(),  // <return> String
                      std::make_pair(MachineType::Pointer(), isolate_ptr),
                      std::make_pair(MachineType::AnyTagged(), fixed_array),
                      std::make_pair(MachineType::IntPtr(), length),
                      std::make_pair(MachineType::AnyTagged(), sep),
                      std::make_pair(MachineType::AnyTagged(), dest)));
  }

 protected:
  TNode<Context> context() { return context_; }
  TNode<Object> receiver() { return receiver_; }
  TNode<IntPtrT> argc() { return argc_; }
  TNode<JSReceiver> o() { return o_; }
  TNode<UintPtrT> len() { return len_; }
  TNode<Object> callbackfn() { return callbackfn_; }
  TNode<Object> this_arg() { return this_arg_; }
  TNode<UintPtrT> k() { return k_.value(); }
  TNode<Object> a() { return a_.value(); }

  void ReturnFromBuiltin(TNode<Object> value);

  void InitIteratingArrayBuiltinBody(TNode<Context> context,
                                     TNode<Object> receiver,
                                     TNode<Object> callbackfn,
                                     TNode<Object> this_arg,
                                     TNode<IntPtrT> argc);

  void GenerateIteratingTypedArrayBuiltinBody(
      const char* name, const BuiltinResultGenerator& generator,
      const CallResultProcessor& processor,
      ForEachDirection direction = ForEachDirection::kForward);

  void TailCallArrayConstructorStub(
      const Callable& callable, TNode<Context> context,
      TNode<JSFunction> target, TNode<HeapObject> allocation_site_or_undefined,
      TNode<Int32T> argc);

  void GenerateDispatchToArrayStub(
      TNode<Context> context, TNode<JSFunction> target, TNode<Int32T> argc,
      AllocationSiteOverrideMode mode,
      base::Optional<TNode<AllocationSite>> allocation_site = base::nullopt);

  void CreateArrayDispatchNoArgument(
      TNode<Context> context, TNode<JSFunction> target, TNode<Int32T> argc,
      AllocationSiteOverrideMode mode,
      base::Optional<TNode<AllocationSite>> allocation_site);

  void CreateArrayDispatchSingleArgument(
      TNode<Context> context, TNode<JSFunction> target, TNode<Int32T> argc,
      AllocationSiteOverrideMode mode,
      base::Optional<TNode<AllocationSite>> allocation_site);

  void GenerateConstructor(TNode<Context> context,
                           TNode<HeapObject> array_function,
                           TNode<Map> array_map, TNode<Object> array_size,
                           TNode<HeapObject> allocation_site,
                           ElementsKind elements_kind, AllocationSiteMode mode);
  void GenerateArrayNoArgumentConstructor(ElementsKind kind,
                                          AllocationSiteOverrideMode mode);
  void GenerateArraySingleArgumentConstructor(ElementsKind kind,
                                              AllocationSiteOverrideMode mode);
  void GenerateArrayNArgumentsConstructor(
      TNode<Context> context, TNode<JSFunction> target,
      TNode<Object> new_target, TNode<Int32T> argc,
      TNode<HeapObject> maybe_allocation_site);

 private:
  void VisitAllTypedArrayElements(TNode<JSArrayBuffer> array_buffer,
                                  const CallResultProcessor& processor,
                                  ForEachDirection direction,
                                  TNode<JSTypedArray> typed_array);

  TNode<Object> callbackfn_;
  TNode<JSReceiver> o_;
  TNode<Object> this_arg_;
  TNode<UintPtrT> len_;
  TNode<Context> context_;
  TNode<Object> receiver_;
  TNode<IntPtrT> argc_;
  TNode<BoolT> fast_typed_array_target_;
  const char* name_ = nullptr;
  TVariable<UintPtrT> k_;
  TVariable<Object> a_;
  Label fully_spec_compliant_;
  ElementsKind source_elements_kind_ = ElementsKind::NO_ELEMENTS;
};

class ArrayBuiltins {
 public:
  enum ArrayFromAsyncIterableResolveContextSlots {
    kArrayFromAsyncIterableResolveResumeStateStepSlot =
        Context::MIN_CONTEXT_SLOTS,
    kArrayFromAsyncIterableResolveResumeStateAwaitedValueSlot,
    kArrayFromAsyncIterableResolveResumeStateIndexSlot,
    kArrayFromAsyncIterableResolvePromiseSlot,
    kArrayFromAsyncIterableResolvePromiseFunctionSlot,
    kArrayFromAsyncIterableResolveOnFulfilledFunctionSlot,
    kArrayFromAsyncIterableResolveOnRejectedFunctionSlot,
    kArrayFromAsyncIterableResolveResultArraySlot,
    kArrayFromAsyncIterableResolveIteratorSlot,
    kArrayFromAsyncIterableResolveNextMethodSlot,
    kArrayFromAsyncIterableResolveErrorSlot,
    kArrayFromAsyncIterableResolveMapfnSlot,
    kArrayFromAsyncIterableResolveThisArgSlot,
    kArrayFromAsyncIterableResolveLength
  };

  enum ArrayFromAsyncArrayLikeResolveContextSlots {
    kArrayFromAsyncArrayLikeResolveResumeStateStepSlot =
        Context::MIN_CONTEXT_SLOTS,
    kArrayFromAsyncArrayLikeResolveResumeStateAwaitedValueSlot,
    kArrayFromAsyncArrayLikeResolveResumeStateLenSlot,
    kArrayFromAsyncArrayLikeResolveResumeStateIndexSlot,
    kArrayFromAsyncArrayLikeResolvePromiseSlot,
    kArrayFromAsyncArrayLikeResolvePromiseFunctionSlot,
    kArrayFromAsyncArrayLikeResolveOnFulfilledFunctionSlot,
    kArrayFromAsyncArrayLikeResolveOnRejectedFunctionSlot,
    kArrayFromAsyncArrayLikeResolveResultArraySlot,
    kArrayFromAsyncArrayLikeResolveArrayLikeSlot,
    kArrayFromAsyncArrayLikeResolveErrorSlot,
    kArrayFromAsyncArrayLikeResolveMapfnSlot,
    kArrayFromAsyncArrayLikeResolveThisArgSlot,
    kArrayFromAsyncArrayLikeResolveLength
  };

  enum ArrayFromAsyncLabels {
    kGetIteratorStep,
    kCheckIteratorValueAndMapping,
    kIteratorMapping,
    kGetIteratorValueWithMapping,
    kAddIteratorValueToTheArray,
    kGetArrayLikeValue,
    kCheckArrayLikeValueAndMapping,
    kGetArrayLikeValueWithMapping,
    kAddArrayLikeValueToTheArray,
    kDoneAndResolvePromise,
    kCloseAsyncIterator,
    kRejectPromise
  };
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_ARRAY_GEN_H_
