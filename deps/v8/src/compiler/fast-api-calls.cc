// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/fast-api-calls.h"

#include "src/codegen/cpu-features.h"
#include "src/compiler/globals.h"

namespace v8 {

// Local handles should be trivially copyable so that the contained value can be
// efficiently passed by value in a register. This is important for two
// reasons: better performance and a simpler ABI for generated code and fast
// API calls.
ASSERT_TRIVIALLY_COPYABLE(api_internal::IndirectHandleBase);
#ifdef V8_ENABLE_DIRECT_LOCAL
ASSERT_TRIVIALLY_COPYABLE(api_internal::DirectHandleBase);
#endif
ASSERT_TRIVIALLY_COPYABLE(LocalBase<Object>);

#if !(defined(V8_ENABLE_LOCAL_OFF_STACK_CHECK) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI)
// Direct local handles should be trivially copyable, for the same reasons as
// above. In debug builds, however, where we want to check that such handles are
// stack-allocated, we define a non-default copy constructor and destructor.
// This makes them non-trivially copyable. We only do it in builds where we can
// declare them as "trivial ABI", which guarantees that they can be efficiently
// passed by value in a register.
ASSERT_TRIVIALLY_COPYABLE(Local<Object>);
ASSERT_TRIVIALLY_COPYABLE(internal::LocalUnchecked<Object>);
ASSERT_TRIVIALLY_COPYABLE(MaybeLocal<Object>);
#endif

namespace internal {
namespace compiler {
namespace fast_api_call {

ElementsKind GetTypedArrayElementsKind(CTypeInfo::Type type) {
  switch (type) {
    case CTypeInfo::Type::kUint8:
      return UINT8_ELEMENTS;
    case CTypeInfo::Type::kInt32:
      return INT32_ELEMENTS;
    case CTypeInfo::Type::kUint32:
      return UINT32_ELEMENTS;
    case CTypeInfo::Type::kInt64:
      return BIGINT64_ELEMENTS;
    case CTypeInfo::Type::kUint64:
      return BIGUINT64_ELEMENTS;
    case CTypeInfo::Type::kFloat32:
      return FLOAT32_ELEMENTS;
    case CTypeInfo::Type::kFloat64:
      return FLOAT64_ELEMENTS;
    case CTypeInfo::Type::kVoid:
    case CTypeInfo::Type::kSeqOneByteString:
    case CTypeInfo::Type::kBool:
    case CTypeInfo::Type::kPointer:
    case CTypeInfo::Type::kV8Value:
    case CTypeInfo::Type::kApiObject:
    case CTypeInfo::Type::kAny:
      UNREACHABLE();
  }
}

OverloadsResolutionResult ResolveOverloads(
    const FastApiCallFunctionVector& candidates, unsigned int arg_count) {
  DCHECK_GT(arg_count, 0);

  static constexpr int kReceiver = 1;

  // Only the case of the overload resolution of two functions, one with a
  // JSArray param and the other with a typed array param is currently
  // supported.
  DCHECK_EQ(candidates.size(), 2);

  for (unsigned int arg_index = kReceiver; arg_index < arg_count; arg_index++) {
    int index_of_func_with_js_array_arg = -1;
    int index_of_func_with_typed_array_arg = -1;
    CTypeInfo::Type element_type = CTypeInfo::Type::kVoid;

    for (size_t i = 0; i < candidates.size(); i++) {
      const CTypeInfo& type_info =
          candidates[i].signature->ArgumentInfo(arg_index);
      CTypeInfo::SequenceType sequence_type = type_info.GetSequenceType();

      if (sequence_type == CTypeInfo::SequenceType::kIsSequence) {
        DCHECK_LT(index_of_func_with_js_array_arg, 0);
        index_of_func_with_js_array_arg = static_cast<int>(i);
      } else if (sequence_type == CTypeInfo::SequenceType::kIsTypedArray) {
        DCHECK_LT(index_of_func_with_typed_array_arg, 0);
        index_of_func_with_typed_array_arg = static_cast<int>(i);
        element_type = type_info.GetType();
      } else {
        DCHECK_LT(index_of_func_with_js_array_arg, 0);
        DCHECK_LT(index_of_func_with_typed_array_arg, 0);
      }
    }

    if (index_of_func_with_js_array_arg >= 0 &&
        index_of_func_with_typed_array_arg >= 0) {
      return {static_cast<int>(arg_index), element_type};
    }
  }

  // No overload found with a JSArray and a typed array as i-th argument.
  return OverloadsResolutionResult::Invalid();
}

bool CanOptimizeFastSignature(const CFunctionInfo* c_signature) {
  USE(c_signature);

#if defined(V8_OS_MACOS) && defined(V8_TARGET_ARCH_ARM64)
  // On MacArm64 hardware we don't support passing of arguments on the stack.
  if (c_signature->ArgumentCount() > 8) {
    return false;
  }
#endif  // defined(V8_OS_MACOS) && defined(V8_TARGET_ARCH_ARM64)

#ifndef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
  if (c_signature->ReturnInfo().GetType() == CTypeInfo::Type::kFloat32 ||
      c_signature->ReturnInfo().GetType() == CTypeInfo::Type::kFloat64) {
    return false;
  }
#endif

#ifndef V8_TARGET_ARCH_64_BIT
  if (c_signature->ReturnInfo().GetType() == CTypeInfo::Type::kInt64 ||
      c_signature->ReturnInfo().GetType() == CTypeInfo::Type::kUint64) {
    return false;
  }
#endif

  for (unsigned int i = 0; i < c_signature->ArgumentCount(); ++i) {
    USE(i);

#ifdef V8_TARGET_ARCH_X64
    // Clamp lowering in EffectControlLinearizer uses rounding.
    uint8_t flags = uint8_t(c_signature->ArgumentInfo(i).GetFlags());
    if (flags & uint8_t(CTypeInfo::Flags::kClampBit)) {
      return CpuFeatures::IsSupported(SSE4_2);
    }
#endif  // V8_TARGET_ARCH_X64

#ifndef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
    if (c_signature->ArgumentInfo(i).GetType() == CTypeInfo::Type::kFloat32 ||
        c_signature->ArgumentInfo(i).GetType() == CTypeInfo::Type::kFloat64) {
      return false;
    }
#endif

#ifndef V8_TARGET_ARCH_64_BIT
    if (c_signature->ArgumentInfo(i).GetType() == CTypeInfo::Type::kInt64 ||
        c_signature->ArgumentInfo(i).GetType() == CTypeInfo::Type::kUint64) {
      return false;
    }
#endif
  }

  return true;
}

#define __ gasm()->

class FastApiCallBuilder {
 public:
  FastApiCallBuilder(Isolate* isolate, Graph* graph,
                     GraphAssembler* graph_assembler,
                     const GetParameter& get_parameter,
                     const ConvertReturnValue& convert_return_value,
                     const InitializeOptions& initialize_options,
                     const GenerateSlowApiCall& generate_slow_api_call)
      : isolate_(isolate),
        graph_(graph),
        graph_assembler_(graph_assembler),
        get_parameter_(get_parameter),
        convert_return_value_(convert_return_value),
        initialize_options_(initialize_options),
        generate_slow_api_call_(generate_slow_api_call) {}

  Node* Build(const FastApiCallFunctionVector& c_functions,
              const CFunctionInfo* c_signature, Node* data_argument);

 private:
  Node* WrapFastCall(const CallDescriptor* call_descriptor, int inputs_size,
                     Node** inputs, Node* target,
                     const CFunctionInfo* c_signature, int c_arg_count,
                     Node* stack_slot);

  Isolate* isolate() const { return isolate_; }
  Graph* graph() const { return graph_; }
  GraphAssembler* gasm() const { return graph_assembler_; }
  Isolate* isolate_;
  Graph* graph_;
  GraphAssembler* graph_assembler_;
  const GetParameter& get_parameter_;
  const ConvertReturnValue& convert_return_value_;
  const InitializeOptions& initialize_options_;
  const GenerateSlowApiCall& generate_slow_api_call_;
};

Node* FastApiCallBuilder::WrapFastCall(const CallDescriptor* call_descriptor,
                                       int inputs_size, Node** inputs,
                                       Node* target,
                                       const CFunctionInfo* c_signature,
                                       int c_arg_count, Node* stack_slot) {
  // CPU profiler support
  Node* target_address = __ ExternalConstant(
      ExternalReference::fast_api_call_target_address(isolate()));
  __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                               kNoWriteBarrier),
           target_address, 0, __ BitcastTaggedToWord(target));

  // Disable JS execution
  Node* javascript_execution_assert = __ ExternalConstant(
      ExternalReference::javascript_execution_assert(isolate()));
  static_assert(sizeof(bool) == 1, "Wrong assumption about boolean size.");

  if (v8_flags.debug_code) {
    auto do_store = __ MakeLabel();
    Node* old_scope_value =
        __ Load(MachineType::Int8(), javascript_execution_assert, 0);
    __ GotoIf(__ Word32Equal(old_scope_value, __ Int32Constant(1)), &do_store);

    // We expect that JS execution is enabled, otherwise assert.
    __ Unreachable();
    __ Bind(&do_store);
  }
  __ Store(StoreRepresentation(MachineRepresentation::kWord8, kNoWriteBarrier),
           javascript_execution_assert, 0, __ Int32Constant(0));

  // Update effect and control
  if (stack_slot != nullptr) {
    inputs[c_arg_count + 1] = stack_slot;
    inputs[c_arg_count + 2] = __ effect();
    inputs[c_arg_count + 3] = __ control();
  } else {
    inputs[c_arg_count + 1] = __ effect();
    inputs[c_arg_count + 2] = __ control();
  }

  // Create the fast call
  Node* call = __ Call(call_descriptor, inputs_size, inputs);

  // Reenable JS execution
  __ Store(StoreRepresentation(MachineRepresentation::kWord8, kNoWriteBarrier),
           javascript_execution_assert, 0, __ Int32Constant(1));

  // Reset the CPU profiler target address.
  __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                               kNoWriteBarrier),
           target_address, 0, __ IntPtrConstant(0));

  return call;
}

Node* FastApiCallBuilder::Build(const FastApiCallFunctionVector& c_functions,
                                const CFunctionInfo* c_signature,
                                Node* data_argument) {
  const int c_arg_count = c_signature->ArgumentCount();

  // Hint to fast path.
  auto if_success = __ MakeLabel();
  auto if_error = __ MakeDeferredLabel();

  // Overload resolution
  bool generate_fast_call = false;
  OverloadsResolutionResult overloads_resolution_result =
      OverloadsResolutionResult::Invalid();

  if (c_functions.size() == 1) {
    generate_fast_call = true;
  } else {
    DCHECK_EQ(c_functions.size(), 2);
    overloads_resolution_result = ResolveOverloads(c_functions, c_arg_count);
    if (overloads_resolution_result.is_valid()) {
      generate_fast_call = true;
    }
  }

  if (!generate_fast_call) {
    // Only generate the slow call.
    return generate_slow_api_call_();
  }

  // Generate fast call.

  const int kFastTargetAddressInputIndex = 0;
  const int kFastTargetAddressInputCount = 1;

  int extra_input_count = FastApiCallNode::kEffectAndControlInputCount +
                          (c_signature->HasOptions() ? 1 : 0);

  Node** const inputs = graph()->zone()->AllocateArray<Node*>(
      kFastTargetAddressInputCount + c_arg_count + extra_input_count);

  ExternalReference::Type ref_type = ExternalReference::FAST_C_CALL;

  // The inputs to {Call} node for the fast call look like:
  // [fast callee, receiver, ... C arguments, [optional Options], effect,
  //  control].
  //
  // The first input node represents the target address for the fast call.
  // If the function is not overloaded (c_functions.size() == 1) this is the
  // address associated to the first and only element in the c_functions vector.
  // If there are multiple overloads the value of this input will be set later
  // with a Phi node created by AdaptOverloadedFastCallArgument.
  inputs[kFastTargetAddressInputIndex] =
      (c_functions.size() == 1) ? __ ExternalConstant(ExternalReference::Create(
                                      c_functions[0].address, ref_type))
                                : nullptr;

  for (int i = 0; i < c_arg_count; ++i) {
    inputs[i + kFastTargetAddressInputCount] =
        get_parameter_(i, overloads_resolution_result, &if_error);
    if (overloads_resolution_result.target_address) {
      inputs[kFastTargetAddressInputIndex] =
          overloads_resolution_result.target_address;
    }
  }
  DCHECK_NOT_NULL(inputs[kFastTargetAddressInputIndex]);

  MachineSignature::Builder builder(
      graph()->zone(), 1, c_arg_count + (c_signature->HasOptions() ? 1 : 0));
  MachineType return_type =
      MachineType::TypeForCType(c_signature->ReturnInfo());
  builder.AddReturn(return_type);
  for (int i = 0; i < c_arg_count; ++i) {
    CTypeInfo type = c_signature->ArgumentInfo(i);
    MachineType machine_type =
        type.GetSequenceType() == CTypeInfo::SequenceType::kScalar
            ? MachineType::TypeForCType(type)
            : MachineType::AnyTagged();
    builder.AddParam(machine_type);
  }

  Node* stack_slot = nullptr;
  if (c_signature->HasOptions()) {
    const int kAlign = alignof(v8::FastApiCallbackOptions);
    const int kSize = sizeof(v8::FastApiCallbackOptions);
    // If this check fails, you've probably added new fields to
    // v8::FastApiCallbackOptions, which means you'll need to write code
    // that initializes and reads from them too.
    static_assert(kSize == sizeof(uintptr_t) * 3);
    stack_slot = __ StackSlot(kSize, kAlign);

    __ Store(
        StoreRepresentation(MachineRepresentation::kWord32, kNoWriteBarrier),
        stack_slot,
        static_cast<int>(offsetof(v8::FastApiCallbackOptions, fallback)),
        __ Int32Constant(0));

    Node* data_argument_to_pass = __ AdaptLocalArgument(data_argument);

    __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                 kNoWriteBarrier),
             stack_slot,
             static_cast<int>(offsetof(v8::FastApiCallbackOptions, data)),
             data_argument_to_pass);

    initialize_options_(stack_slot);

    builder.AddParam(MachineType::Pointer());  // stack_slot
  }

  CallDescriptor* call_descriptor =
      Linkage::GetSimplifiedCDescriptor(graph()->zone(), builder.Build());

  Node* c_call_result =
      WrapFastCall(call_descriptor, c_arg_count + extra_input_count + 1, inputs,
                   inputs[0], c_signature, c_arg_count, stack_slot);

  Node* fast_call_result = convert_return_value_(c_signature, c_call_result);

  auto merge = __ MakeLabel(MachineRepresentation::kTagged);
  if (c_signature->HasOptions()) {
    DCHECK_NOT_NULL(stack_slot);
    Node* load = __ Load(
        MachineType::Int32(), stack_slot,
        static_cast<int>(offsetof(v8::FastApiCallbackOptions, fallback)));

    Node* is_zero = __ Word32Equal(load, __ Int32Constant(0));
    __ Branch(is_zero, &if_success, &if_error);
  } else {
    __ Goto(&if_success);
  }

  // We need to generate a fallback (both fast and slow call) in case:
  // 1) the generated code might fail, in case e.g. a Smi was passed where
  // a JSObject was expected and an error must be thrown or
  // 2) the embedder requested fallback possibility via providing options arg.
  // None of the above usually holds true for Wasm functions with primitive
  // types only, so we avoid generating an extra branch here.
  DCHECK_IMPLIES(c_signature->HasOptions(), if_error.IsUsed());
  if (if_error.IsUsed()) {
    // Generate direct slow call.
    __ Bind(&if_error);
    {
      Node* slow_call_result = generate_slow_api_call_();
      __ Goto(&merge, slow_call_result);
    }
  }

  __ Bind(&if_success);
  __ Goto(&merge, fast_call_result);

  __ Bind(&merge);
  return merge.PhiAt(0);
}

#undef __

Node* BuildFastApiCall(Isolate* isolate, Graph* graph,
                       GraphAssembler* graph_assembler,
                       const FastApiCallFunctionVector& c_functions,
                       const CFunctionInfo* c_signature, Node* data_argument,
                       const GetParameter& get_parameter,
                       const ConvertReturnValue& convert_return_value,
                       const InitializeOptions& initialize_options,
                       const GenerateSlowApiCall& generate_slow_api_call) {
  FastApiCallBuilder builder(isolate, graph, graph_assembler, get_parameter,
                             convert_return_value, initialize_options,
                             generate_slow_api_call);
  return builder.Build(c_functions, c_signature, data_argument);
}

}  // namespace fast_api_call
}  // namespace compiler
}  // namespace internal
}  // namespace v8
