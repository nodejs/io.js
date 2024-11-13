// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_FAST_API_CALL_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_FAST_API_CALL_LOWERING_REDUCER_H_

#include "include/v8-fast-api-calls.h"
#include "src/compiler/fast-api-calls.h"
#include "src/compiler/globals.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename Next>
class FastApiCallLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(FastApiCallLowering)

  OpIndex REDUCE(FastApiCall)(V<FrameState> frame_state,
                              V<Object> data_argument, V<Context> context,
                              base::Vector<const OpIndex> arguments,
                              const FastApiCallParameters* parameters) {
    const auto& c_functions = parameters->c_functions;
    const auto& c_signature = parameters->c_signature();
    const int c_arg_count = c_signature->ArgumentCount();
    DCHECK_EQ(c_arg_count, arguments.size());
    const auto& resolution_result = parameters->resolution_result;

    Label<> handle_error(this);
    Label<Word32, Object> done(this);

    OpIndex callee;
    base::SmallVector<OpIndex, 16> args;
    for (int i = 0; i < c_arg_count; ++i) {
      // Check if this is the argument on which we need to perform overload
      // resolution.
      if (i == resolution_result.distinguishable_arg_index) {
        DCHECK_GT(c_functions.size(), 1);
        // This only happens when the FastApiCall node represents multiple
        // overloaded functions and {i} is the index of the distinguishable
        // argument.
        OpIndex arg_i;
        std::tie(callee, arg_i) = AdaptOverloadedFastCallArgument(
            arguments[i], c_functions, resolution_result, handle_error);
        args.push_back(arg_i);
      } else {
        CTypeInfo type = c_signature->ArgumentInfo(i);
        args.push_back(AdaptFastCallArgument(arguments[i], type, handle_error));
      }
    }

    if (c_functions.size() == 1) {
      DCHECK(!callee.valid());
      callee = __ ExternalConstant(ExternalReference::Create(
          c_functions[0].address, ExternalReference::FAST_C_CALL));
    }

    // While adapting the arguments, we might have noticed an inconsistency that
    // lead to unconditionally jumping to {handle_error}. If this happens, then
    // we don't emit the call.
    if (V8_LIKELY(!__ generating_unreachable_operations())) {
      MachineSignature::Builder builder(
          __ graph_zone(), 1,
          c_arg_count + (c_signature->HasOptions() ? 1 : 0));
      builder.AddReturn(MachineType::TypeForCType(c_signature->ReturnInfo()));
      for (int i = 0; i < c_arg_count; ++i) {
        CTypeInfo type = c_signature->ArgumentInfo(i);
        MachineType machine_type =
            type.GetSequenceType() == CTypeInfo::SequenceType::kScalar
                ? MachineType::TypeForCType(type)
                : MachineType::AnyTagged();
        builder.AddParam(machine_type);
      }

      OpIndex stack_slot;
      if (c_signature->HasOptions()) {
        const int kAlign = alignof(v8::FastApiCallbackOptions);
        const int kSize = sizeof(v8::FastApiCallbackOptions);
        // If this check fails, you've probably added new fields to
        // v8::FastApiCallbackOptions, which means you'll need to write code
        // that initializes and reads from them too.
        static_assert(kSize == sizeof(uintptr_t) * 2);
        stack_slot = __ StackSlot(kSize, kAlign);

        // isolate
        __ StoreOffHeap(
            stack_slot,
            __ ExternalConstant(ExternalReference::isolate_address()),
            MemoryRepresentation::UintPtr(),
            offsetof(v8::FastApiCallbackOptions, isolate));
        // data = data_argument
        OpIndex data_argument_to_pass = __ AdaptLocalArgument(data_argument);
        __ StoreOffHeap(stack_slot, data_argument_to_pass,
                        MemoryRepresentation::UintPtr(),
                        offsetof(v8::FastApiCallbackOptions, data));

        args.push_back(stack_slot);
        builder.AddParam(MachineType::Pointer());
      }

      // Build the actual call.
      const TSCallDescriptor* call_descriptor = TSCallDescriptor::Create(
          Linkage::GetSimplifiedCDescriptor(__ graph_zone(), builder.Build(),
                                            CallDescriptor::kNeedsFrameState),
          CanThrow::kNo, LazyDeoptOnThrow::kNo, __ graph_zone());
      OpIndex c_call_result = WrapFastCall(call_descriptor, callee, frame_state,
                                           context, base::VectorOf(args));

      Label<> trigger_exception(this);

      V<Object> exception =
          __ Load(__ ExternalConstant(ExternalReference::Create(
                      IsolateAddressId::kExceptionAddress, isolate_)),
                  LoadOp::Kind::RawAligned(), MemoryRepresentation::UintPtr());
      GOTO_IF_NOT(LIKELY(__ TaggedEqual(
                      exception,
                      __ HeapConstant(isolate_->factory()->the_hole_value()))),
                  trigger_exception);

      V<Object> fast_call_result =
          ConvertReturnValue(c_signature, c_call_result);

      GOTO(done, FastApiCallOp::kSuccessValue, fast_call_result);
      BIND(trigger_exception);
      __ template CallRuntime<
          typename RuntimeCallDescriptor::PropagateException>(
          isolate_, frame_state, __ NoContextConstant(), LazyDeoptOnThrow::kNo,
          {});

      GOTO(done, FastApiCallOp::kFailureValue, __ TagSmi(0));
    }

    if (BIND(handle_error)) {
      // We pass Tagged<Smi>(0) as the value here, although this should never be
      // visible when calling code reacts to `kFailureValue` properly.
      GOTO(done, FastApiCallOp::kFailureValue, __ TagSmi(0));
    }

    BIND(done, state, value);
    return __ Tuple(state, value);
  }

 private:
  std::pair<OpIndex, OpIndex> AdaptOverloadedFastCallArgument(
      OpIndex argument, const FastApiCallFunctionVector& c_functions,
      const fast_api_call::OverloadsResolutionResult& resolution_result,
      Label<>& handle_error) {
    Label<WordPtr, WordPtr> done(this);

    for (size_t func_index = 0; func_index < c_functions.size(); ++func_index) {
      const CFunctionInfo* c_signature = c_functions[func_index].signature;
      CTypeInfo arg_type = c_signature->ArgumentInfo(
          resolution_result.distinguishable_arg_index);

      Label<> next(this);

      // Check that the value is a HeapObject.
      GOTO_IF(__ ObjectIsSmi(argument), handle_error);

      switch (arg_type.GetSequenceType()) {
        case CTypeInfo::SequenceType::kIsSequence: {
          CHECK_EQ(arg_type.GetType(), CTypeInfo::Type::kVoid);

          // Check that the value is a JSArray.
          V<Map> map = __ LoadMapField(argument);
          V<Word32> instance_type = __ LoadInstanceTypeField(map);
          GOTO_IF_NOT(__ Word32Equal(instance_type, JS_ARRAY_TYPE), next);

          OpIndex argument_to_pass = __ AdaptLocalArgument(argument);
          OpIndex target_address = __ ExternalConstant(
              ExternalReference::Create(c_functions[func_index].address,
                                        ExternalReference::FAST_C_CALL));
          GOTO(done, target_address, argument_to_pass);
          break;
        }
        case CTypeInfo::SequenceType::kIsTypedArray: {
          // Check that the value is a TypedArray with a type that matches the
          // type declared in the c-function.
          OpIndex stack_slot = AdaptFastCallTypedArrayArgument(
              argument,
              fast_api_call::GetTypedArrayElementsKind(
                  resolution_result.element_type),
              next);
          OpIndex target_address = __ ExternalConstant(
              ExternalReference::Create(c_functions[func_index].address,
                                        ExternalReference::FAST_C_CALL));
          GOTO(done, target_address, stack_slot);
          break;
        }

        default: {
          UNREACHABLE();
        }
      }

      BIND(next);
    }
    GOTO(handle_error);

    BIND(done, callee, arg);
    return {callee, arg};
  }

  template <typename T>
  V<T> Checked(V<Tuple<T, Word32>> result, Label<>& otherwise) {
    V<Word32> result_state = __ template Projection<1>(result);
    GOTO_IF_NOT(__ Word32Equal(result_state, TryChangeOp::kSuccessValue),
                otherwise);
    return __ template Projection<0>(result);
  }

  OpIndex AdaptFastCallArgument(OpIndex argument, CTypeInfo arg_type,
                                Label<>& handle_error) {
    switch (arg_type.GetSequenceType()) {
      case CTypeInfo::SequenceType::kScalar: {
        uint8_t flags = static_cast<uint8_t>(arg_type.GetFlags());
        if (flags & static_cast<uint8_t>(CTypeInfo::Flags::kEnforceRangeBit)) {
          switch (arg_type.GetType()) {
            case CTypeInfo::Type::kInt32: {
              auto result = __ TryTruncateFloat64ToInt32(argument);
              return Checked(result, handle_error);
            }
            case CTypeInfo::Type::kUint32: {
              auto result = __ TryTruncateFloat64ToUint32(argument);
              return Checked(result, handle_error);
            }
            case CTypeInfo::Type::kInt64: {
              auto result = __ TryTruncateFloat64ToInt64(argument);
              return Checked(result, handle_error);
            }
            case CTypeInfo::Type::kUint64: {
              auto result = __ TryTruncateFloat64ToUint64(argument);
              return Checked(result, handle_error);
            }
            default: {
              GOTO(handle_error);
              return argument;
            }
          }
        } else if (flags & static_cast<uint8_t>(CTypeInfo::Flags::kClampBit)) {
          return ClampFastCallArgument(argument, arg_type.GetType());
        } else {
          switch (arg_type.GetType()) {
            case CTypeInfo::Type::kV8Value: {
              return __ AdaptLocalArgument(argument);
            }
            case CTypeInfo::Type::kFloat32: {
              return __ TruncateFloat64ToFloat32(argument);
            }
            case CTypeInfo::Type::kPointer: {
              // Check that the value is a HeapObject.
              GOTO_IF(__ ObjectIsSmi(argument), handle_error);
              Label<WordPtr> done(this);

              // Check if the value is null.
              GOTO_IF(UNLIKELY(__ TaggedEqual(
                          argument, __ HeapConstant(factory_->null_value()))),
                      done, 0);

              // Check that the value is a JSExternalObject.
              GOTO_IF_NOT(
                  __ TaggedEqual(__ LoadMapField(argument),
                                 __ HeapConstant(factory_->external_map())),
                  handle_error);

              GOTO(done, __ template LoadField<WordPtr>(
                             V<HeapObject>::Cast(argument),
                             AccessBuilder::ForJSExternalObjectValue()));

              BIND(done, result);
              return result;
            }
            case CTypeInfo::Type::kSeqOneByteString: {
              // Check that the value is a HeapObject.
              GOTO_IF(__ ObjectIsSmi(argument), handle_error);
              V<HeapObject> argument_obj = V<HeapObject>::Cast(argument);

              V<Map> map = __ LoadMapField(argument_obj);
              V<Word32> instance_type = __ LoadInstanceTypeField(map);

              V<Word32> encoding = __ Word32BitwiseAnd(
                  instance_type, kStringRepresentationAndEncodingMask);
              GOTO_IF_NOT(__ Word32Equal(encoding, kSeqOneByteStringTag),
                          handle_error);

              V<WordPtr> length_in_bytes = __ template LoadField<WordPtr>(
                  argument_obj, AccessBuilder::ForStringLength());
              V<WordPtr> data_ptr = __ GetElementStartPointer(
                  argument_obj, AccessBuilder::ForSeqOneByteStringCharacter());

              constexpr int kAlign = alignof(FastOneByteString);
              constexpr int kSize = sizeof(FastOneByteString);
              static_assert(kSize == sizeof(uintptr_t) + sizeof(size_t),
                            "The size of "
                            "FastOneByteString isn't equal to the sum of its "
                            "expected members.");
              OpIndex stack_slot = __ StackSlot(kSize, kAlign);
              __ StoreOffHeap(stack_slot, data_ptr,
                              MemoryRepresentation::UintPtr());
              __ StoreOffHeap(stack_slot, length_in_bytes,
                              MemoryRepresentation::Uint32(), sizeof(size_t));
              static_assert(sizeof(uintptr_t) == sizeof(size_t),
                            "The string length can't "
                            "fit the PointerRepresentation used to store it.");
              return stack_slot;
            }
            default: {
              return argument;
            }
          }
        }
      }
      case CTypeInfo::SequenceType::kIsSequence: {
        CHECK_EQ(arg_type.GetType(), CTypeInfo::Type::kVoid);

        // Check that the value is a HeapObject.
        GOTO_IF(__ ObjectIsSmi(argument), handle_error);

        // Check that the value is a JSArray.
        V<Map> map = __ LoadMapField(argument);
        V<Word32> instance_type = __ LoadInstanceTypeField(map);
        GOTO_IF_NOT(__ Word32Equal(instance_type, JS_ARRAY_TYPE), handle_error);

        return __ AdaptLocalArgument(argument);
      }
      case CTypeInfo::SequenceType::kIsTypedArray: {
        // Check that the value is a HeapObject.
        GOTO_IF(__ ObjectIsSmi(argument), handle_error);

        return AdaptFastCallTypedArrayArgument(
            argument,
            fast_api_call::GetTypedArrayElementsKind(arg_type.GetType()),
            handle_error);
      }
      default: {
        UNREACHABLE();
      }
    }
  }

  OpIndex ClampFastCallArgument(V<Float64> argument,
                                CTypeInfo::Type scalar_type) {
    double min, max;
    switch (scalar_type) {
      case CTypeInfo::Type::kInt32:
        min = std::numeric_limits<int32_t>::min();
        max = std::numeric_limits<int32_t>::max();
        break;
      case CTypeInfo::Type::kUint32:
        min = 0;
        max = std::numeric_limits<uint32_t>::max();
        break;
      case CTypeInfo::Type::kInt64:
        min = kMinSafeInteger;
        max = kMaxSafeInteger;
        break;
      case CTypeInfo::Type::kUint64:
        min = 0;
        max = kMaxSafeInteger;
        break;
      default:
        UNREACHABLE();
    }

    V<Float64> clamped =
        __ Conditional(__ Float64LessThan(min, argument),
                       __ Conditional(__ Float64LessThan(argument, max),
                                      argument, __ Float64Constant(max)),
                       __ Float64Constant(min));

    Label<Float64> done(this);
    V<Float64> rounded = __ Float64RoundTiesEven(clamped);
    GOTO_IF(__ Float64IsNaN(rounded), done, 0.0);
    GOTO(done, rounded);

    BIND(done, rounded_result);
    switch (scalar_type) {
      case CTypeInfo::Type::kInt32:
        return __ ReversibleFloat64ToInt32(rounded_result);
      case CTypeInfo::Type::kUint32:
        return __ ReversibleFloat64ToUint32(rounded_result);
      case CTypeInfo::Type::kInt64:
        return __ ReversibleFloat64ToInt64(rounded_result);
      case CTypeInfo::Type::kUint64:
        return __ ReversibleFloat64ToUint64(rounded_result);
      default:
        UNREACHABLE();
    }
  }

  OpIndex AdaptFastCallTypedArrayArgument(V<HeapObject> argument,
                                          ElementsKind expected_elements_kind,
                                          Label<>& bailout) {
    V<Map> map = __ LoadMapField(argument);
    V<Word32> instance_type = __ LoadInstanceTypeField(map);
    GOTO_IF_NOT(LIKELY(__ Word32Equal(instance_type, JS_TYPED_ARRAY_TYPE)),
                bailout);

    V<Word32> bitfield2 =
        __ template LoadField<Word32>(map, AccessBuilder::ForMapBitField2());
    V<Word32> kind = __ Word32ShiftRightLogical(
        __ Word32BitwiseAnd(bitfield2, Map::Bits2::ElementsKindBits::kMask),
        Map::Bits2::ElementsKindBits::kShift);
    GOTO_IF_NOT(LIKELY(__ Word32Equal(kind, expected_elements_kind)), bailout);

    V<HeapObject> buffer = __ template LoadField<HeapObject>(
        argument, AccessBuilder::ForJSArrayBufferViewBuffer());
    V<Word32> buffer_bitfield = __ template LoadField<Word32>(
        buffer, AccessBuilder::ForJSArrayBufferBitField());

    // Go to the slow path if the {buffer} was detached.
    GOTO_IF(UNLIKELY(__ Word32BitwiseAnd(buffer_bitfield,
                                         JSArrayBuffer::WasDetachedBit::kMask)),
            bailout);

    // Go to the slow path if the {buffer} is shared.
    GOTO_IF(UNLIKELY(__ Word32BitwiseAnd(buffer_bitfield,
                                         JSArrayBuffer::IsSharedBit::kMask)),
            bailout);

    // Unpack the store and length, and store them to a struct
    // FastApiTypedArray.
    OpIndex external_pointer =
        __ LoadField(argument, AccessBuilder::ForJSTypedArrayExternalPointer());

    // Load the base pointer for the buffer. This will always be Smi
    // zero unless we allow on-heap TypedArrays, which is only the case
    // for Chrome. Node and Electron both set this limit to 0. Setting
    // the base to Smi zero here allows the BuildTypedArrayDataPointer
    // to optimize away the tricky part of the access later.
    V<WordPtr> data_ptr;
    if constexpr (JSTypedArray::kMaxSizeInHeap == 0) {
      data_ptr = external_pointer;
    } else {
      V<Object> base_pointer = __ template LoadField<Object>(
          argument, AccessBuilder::ForJSTypedArrayBasePointer());
      V<WordPtr> base = __ BitcastTaggedToWordPtr(base_pointer);
      if (COMPRESS_POINTERS_BOOL) {
        // Zero-extend Tagged_t to UintPtr according to current compression
        // scheme so that the addition with |external_pointer| (which already
        // contains compensated offset value) will decompress the tagged value.
        // See JSTypedArray::ExternalPointerCompensationForOnHeapArray() for
        // details.
        base = __ ChangeUint32ToUintPtr(__ TruncateWordPtrToWord32(base));
      }
      data_ptr = __ WordPtrAdd(base, external_pointer);
    }

    V<WordPtr> length_in_bytes = __ template LoadField<WordPtr>(
        argument, AccessBuilder::ForJSTypedArrayLength());

    // We hard-code int32_t here, because all specializations of
    // FastApiTypedArray have the same size.
    START_ALLOW_USE_DEPRECATED()
    constexpr int kAlign = alignof(FastApiTypedArray<int32_t>);
    constexpr int kSize = sizeof(FastApiTypedArray<int32_t>);
    static_assert(kAlign == alignof(FastApiTypedArray<double>),
                  "Alignment mismatch between different specializations of "
                  "FastApiTypedArray");
    static_assert(kSize == sizeof(FastApiTypedArray<double>),
                  "Size mismatch between different specializations of "
                  "FastApiTypedArray");
    END_ALLOW_USE_DEPRECATED()
    static_assert(
        kSize == sizeof(uintptr_t) + sizeof(size_t),
        "The size of "
        "FastApiTypedArray isn't equal to the sum of its expected members.");
    OpIndex stack_slot = __ StackSlot(kSize, kAlign);
    __ StoreOffHeap(stack_slot, length_in_bytes,
                    MemoryRepresentation::UintPtr());
    __ StoreOffHeap(stack_slot, data_ptr, MemoryRepresentation::UintPtr(),
                    sizeof(size_t));
    static_assert(sizeof(uintptr_t) == sizeof(size_t),
                  "The buffer length can't "
                  "fit the PointerRepresentation used to store it.");
    return stack_slot;
  }

  V<Object> ConvertReturnValue(const CFunctionInfo* c_signature,
                               OpIndex result) {
    switch (c_signature->ReturnInfo().GetType()) {
      case CTypeInfo::Type::kVoid:
        return __ HeapConstant(factory_->undefined_value());
      case CTypeInfo::Type::kBool:
        static_assert(sizeof(bool) == 1, "unsupported bool size");
        return __ ConvertWord32ToBoolean(
            __ Word32BitwiseAnd(result, __ Word32Constant(0xFF)));
      case CTypeInfo::Type::kInt32:
        return __ ConvertInt32ToNumber(result);
      case CTypeInfo::Type::kUint32:
        return __ ConvertUint32ToNumber(result);
      case CTypeInfo::Type::kInt64: {
        CFunctionInfo::Int64Representation repr =
            c_signature->GetInt64Representation();
        if (repr == CFunctionInfo::Int64Representation::kBigInt) {
          return __ ConvertUntaggedToJSPrimitive(
              result, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kBigInt,
              RegisterRepresentation::Word64(),
              ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
              CheckForMinusZeroMode::kDontCheckForMinusZero);
        } else if (repr == CFunctionInfo::Int64Representation::kNumber) {
          return __ ConvertUntaggedToJSPrimitive(
              result, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber,
              RegisterRepresentation::Word64(),
              ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
              CheckForMinusZeroMode::kDontCheckForMinusZero);
        } else {
          UNREACHABLE();
        }
      }
      case CTypeInfo::Type::kUint64: {
        CFunctionInfo::Int64Representation repr =
            c_signature->GetInt64Representation();
        if (repr == CFunctionInfo::Int64Representation::kBigInt) {
          return __ ConvertUntaggedToJSPrimitive(
              result, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kBigInt,
              RegisterRepresentation::Word64(),
              ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kUnsigned,
              CheckForMinusZeroMode::kDontCheckForMinusZero);
        } else if (repr == CFunctionInfo::Int64Representation::kNumber) {
          return __ ConvertUntaggedToJSPrimitive(
              result, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber,
              RegisterRepresentation::Word64(),
              ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kUnsigned,
              CheckForMinusZeroMode::kDontCheckForMinusZero);
        } else {
          UNREACHABLE();
        }
      }
      case CTypeInfo::Type::kFloat32:
        return __ ConvertFloat64ToNumber(
            __ ChangeFloat32ToFloat64(result),
            CheckForMinusZeroMode::kCheckForMinusZero);
      case CTypeInfo::Type::kFloat64:
        return __ ConvertFloat64ToNumber(
            result, CheckForMinusZeroMode::kCheckForMinusZero);
      case CTypeInfo::Type::kPointer:
        return BuildAllocateJSExternalObject(result);
      case CTypeInfo::Type::kSeqOneByteString:
      case CTypeInfo::Type::kV8Value:
      case CTypeInfo::Type::kApiObject:
      case CTypeInfo::Type::kUint8:
        UNREACHABLE();
      case CTypeInfo::Type::kAny:
        return __ ConvertFloat64ToNumber(
            __ ChangeInt64ToFloat64(result),
            CheckForMinusZeroMode::kCheckForMinusZero);
    }
  }

  V<HeapObject> BuildAllocateJSExternalObject(V<WordPtr> pointer) {
    Label<HeapObject> done(this);

    // Check if the pointer is a null pointer.
    GOTO_IF(__ WordPtrEqual(pointer, 0), done,
            __ HeapConstant(factory_->null_value()));

    Uninitialized<HeapObject> external =
        __ Allocate(JSExternalObject::kHeaderSize, AllocationType::kYoung);
    __ InitializeField(external, AccessBuilder::ForMap(),
                       __ HeapConstant(factory_->external_map()));
    V<FixedArray> empty_fixed_array =
        __ HeapConstant(factory_->empty_fixed_array());
    __ InitializeField(external, AccessBuilder::ForJSObjectPropertiesOrHash(),
                       empty_fixed_array);
    __ InitializeField(external, AccessBuilder::ForJSObjectElements(),
                       empty_fixed_array);

#ifdef V8_ENABLE_SANDBOX
    OpIndex isolate_ptr =
        __ ExternalConstant(ExternalReference::isolate_address());
    MachineSignature::Builder builder(__ graph_zone(), 1, 2);
    builder.AddReturn(MachineType::Uint32());
    builder.AddParam(MachineType::Pointer());
    builder.AddParam(MachineType::Pointer());
    OpIndex allocate_and_initialize_young_external_pointer_table_entry =
        __ ExternalConstant(
            ExternalReference::
                allocate_and_initialize_young_external_pointer_table_entry());
    auto call_descriptor =
        Linkage::GetSimplifiedCDescriptor(__ graph_zone(), builder.Build());
    OpIndex handle = __ Call(
        allocate_and_initialize_young_external_pointer_table_entry,
        {isolate_ptr, pointer},
        TSCallDescriptor::Create(call_descriptor, CanThrow::kNo,
                                 LazyDeoptOnThrow::kNo, __ graph_zone()));
    __ InitializeField(
        external, AccessBuilder::ForJSExternalObjectPointerHandle(), handle);
#else
    __ InitializeField(external, AccessBuilder::ForJSExternalObjectValue(),
                       pointer);
#endif  // V8_ENABLE_SANDBOX
    GOTO(done, __ FinishInitialization(std::move(external)));

    BIND(done, result);
    return result;
  }

  OpIndex WrapFastCall(const TSCallDescriptor* descriptor, OpIndex callee,
                       V<FrameState> frame_state, V<Context> context,
                       base::Vector<const OpIndex> arguments) {
    // CPU profiler support.
    OpIndex target_address =
        __ IsolateField(IsolateFieldId::kFastApiCallTarget);
    __ StoreOffHeap(target_address, __ BitcastHeapObjectToWordPtr(callee),
                    MemoryRepresentation::UintPtr());

    OpIndex context_address = __ ExternalConstant(
        ExternalReference::Create(IsolateAddressId::kContextAddress, isolate_));

    __ StoreOffHeap(context_address, __ BitcastHeapObjectToWordPtr(context),
                    MemoryRepresentation::UintPtr());

    // Create the fast call.
    OpIndex result = __ Call(callee, frame_state, arguments, descriptor);

    // Reset the CPU profiler target address.
    __ StoreOffHeap(target_address, __ IntPtrConstant(0),
                    MemoryRepresentation::UintPtr());

#if DEBUG
    // Reset the context again after the call, to make sure nobody is using the
    // leftover context in the isolate.
    __ StoreOffHeap(context_address,
                    __ WordPtrConstant(Context::kInvalidContext),
                    MemoryRepresentation::UintPtr());
#endif

    return result;
  }

  Isolate* isolate_ = __ data() -> isolate();
  Factory* factory_ = isolate_->factory();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_FAST_API_CALL_LOWERING_REDUCER_H_
