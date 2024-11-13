// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/base/small-vector.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/compiler/linkage.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/wasm-assembler-helpers.h"
#include "src/objects/object-list-macros.h"
#include "src/wasm/turboshaft-graph-interface.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/zone/zone.h"

namespace v8::internal::wasm {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using compiler::CallDescriptor;
using compiler::Operator;
using compiler::turboshaft::ConditionWithHint;
using compiler::turboshaft::Float32;
using compiler::turboshaft::Float64;
using compiler::turboshaft::Label;
using compiler::turboshaft::LoadOp;
using compiler::turboshaft::MemoryRepresentation;
using TSBlock = compiler::turboshaft::Block;
using compiler::turboshaft::OpEffects;
using compiler::turboshaft::OpIndex;
using compiler::turboshaft::OptionalOpIndex;
using compiler::turboshaft::RegisterRepresentation;
using compiler::turboshaft::StoreOp;
using compiler::turboshaft::TSCallDescriptor;
using compiler::turboshaft::Tuple;
using compiler::turboshaft::V;
using compiler::turboshaft::Variable;
using compiler::turboshaft::Word32;
using compiler::turboshaft::WordPtr;

namespace {
const TSCallDescriptor* GetBuiltinCallDescriptor(Builtin name, Zone* zone) {
  CallInterfaceDescriptor interface_descriptor =
      Builtins::CallInterfaceDescriptorFor(name);
  CallDescriptor* call_desc = compiler::Linkage::GetStubCallDescriptor(
      zone,                                           // zone
      interface_descriptor,                           // descriptor
      interface_descriptor.GetStackParameterCount(),  // stack parameter count
      CallDescriptor::kNoFlags,                       // flags
      compiler::Operator::kNoProperties,              // properties
      StubCallMode::kCallBuiltinPointer);             // stub call mode
  return TSCallDescriptor::Create(call_desc, compiler::CanThrow::kNo,
                                  compiler::LazyDeoptOnThrow::kNo, zone);
}
}  // namespace

class WasmWrapperTSGraphBuilder : public WasmGraphBuilderBase {
 public:
  WasmWrapperTSGraphBuilder(Zone* zone, Assembler& assembler,
                            const WasmModule* module,
                            const wasm::FunctionSig* sig,
                            StubCallMode stub_mode)
      : WasmGraphBuilderBase(zone, assembler), module_(module), sig_(sig) {}

  void AbortIfNot(V<Word32> condition, AbortReason abort_reason) {
    if (!v8_flags.debug_code) return;
    IF_NOT (condition) {
      V<Number> message_id =
          __ NumberConstant(static_cast<int32_t>(abort_reason));
      CallRuntime(__ phase_zone(), Runtime::kAbort, {message_id},
                  __ NoContextConstant());
    }
  }

  class ModifyThreadInWasmFlagScope {
   public:
    ModifyThreadInWasmFlagScope(
        WasmWrapperTSGraphBuilder* wasm_wrapper_graph_builder, Assembler& asm_)
        : wasm_wrapper_graph_builder_(wasm_wrapper_graph_builder) {
      if (!trap_handler::IsTrapHandlerEnabled()) return;

      thread_in_wasm_flag_address_ =
          asm_.Load(asm_.LoadRootRegister(), OptionalOpIndex::Nullopt(),
                    LoadOp::Kind::RawAligned(), MemoryRepresentation::UintPtr(),
                    RegisterRepresentation::WordPtr(),
                    Isolate::thread_in_wasm_flag_address_offset());
      wasm_wrapper_graph_builder_->BuildModifyThreadInWasmFlagHelper(
          wasm_wrapper_graph_builder_->Asm().phase_zone(),
          thread_in_wasm_flag_address_, true);
    }

    ModifyThreadInWasmFlagScope(const ModifyThreadInWasmFlagScope&) = delete;

    ~ModifyThreadInWasmFlagScope() {
      if (!trap_handler::IsTrapHandlerEnabled()) return;
      wasm_wrapper_graph_builder_->BuildModifyThreadInWasmFlagHelper(
          wasm_wrapper_graph_builder_->Asm().phase_zone(),
          thread_in_wasm_flag_address_, false);
    }

   private:
    WasmWrapperTSGraphBuilder* wasm_wrapper_graph_builder_;
    V<WordPtr> thread_in_wasm_flag_address_;
  };

  V<Smi> LoadExportedFunctionIndexAsSmi(V<Object> exported_function_data) {
    return __ Load(exported_function_data,
                   LoadOp::Kind::TaggedBase().Immutable(),
                   MemoryRepresentation::TaggedSigned(),
                   WasmExportedFunctionData::kFunctionIndexOffset);
  }

  V<Smi> BuildChangeInt32ToSmi(V<Word32> value) {
    // With pointer compression, only the lower 32 bits are used.
    return V<Smi>::Cast(COMPRESS_POINTERS_BOOL
                            ? __ BitcastWord32ToWord64(__ Word32ShiftLeft(
                                  value, BuildSmiShiftBitsConstant32()))
                            : __ Word64ShiftLeft(__ ChangeInt32ToInt64(value),
                                                 BuildSmiShiftBitsConstant()));
  }

  V<WordPtr> GetTargetForBuiltinCall(Builtin builtin) {
    return WasmGraphBuilderBase::GetTargetForBuiltinCall(
        builtin, StubCallMode::kCallBuiltinPointer);
  }

  template <typename Descriptor, typename... Args>
  OpIndex CallBuiltin(Builtin name, OpIndex frame_state,
                      Operator::Properties properties, Args... args) {
    auto call_descriptor = compiler::Linkage::GetStubCallDescriptor(
        __ graph_zone(), Descriptor(), 0,
        frame_state.valid() ? CallDescriptor::kNeedsFrameState
                            : CallDescriptor::kNoFlags,
        Operator::kNoProperties, StubCallMode::kCallBuiltinPointer);
    const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
        call_descriptor, compiler::CanThrow::kNo,
        compiler::LazyDeoptOnThrow::kNo, __ graph_zone());
    V<WordPtr> call_target = GetTargetForBuiltinCall(name);
    return __ Call(call_target, frame_state, base::VectorOf({args...}),
                   ts_call_descriptor);
  }

  template <typename Descriptor, typename... Args>
  OpIndex CallBuiltin(Builtin name, Operator::Properties properties,
                      Args... args) {
    auto call_descriptor = compiler::Linkage::GetStubCallDescriptor(
        __ graph_zone(), Descriptor(), 0, CallDescriptor::kNoFlags,
        Operator::kNoProperties, StubCallMode::kCallBuiltinPointer);
    const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
        call_descriptor, compiler::CanThrow::kNo,
        compiler::LazyDeoptOnThrow::kNo, __ graph_zone());
    V<WordPtr> call_target = GetTargetForBuiltinCall(name);
    return __ Call(call_target, {args...}, ts_call_descriptor);
  }

  V<Number> BuildChangeInt32ToNumber(V<Word32> value) {
    // We expect most integers at runtime to be Smis, so it is important for
    // wrapper performance that Smi conversion be inlined.
    if (SmiValuesAre32Bits()) {
      return BuildChangeInt32ToSmi(value);
    }
    DCHECK(SmiValuesAre31Bits());

    // Double value to test if value can be a Smi, and if so, to convert it.
    V<Tuple<Word32, Word32>> add = __ Int32AddCheckOverflow(value, value);
    V<Word32> ovf = __ template Projection<1>(add);
    ScopedVar<Number> result(this, OpIndex::Invalid());
    IF_NOT (UNLIKELY(ovf)) {
      // If it didn't overflow, the result is {2 * value} as pointer-sized
      // value.
      result = __ BitcastWordPtrToSmi(
          __ ChangeInt32ToIntPtr(__ template Projection<0>(add)));
    } ELSE{
      // Otherwise, call builtin, to convert to a HeapNumber.
      result = CallBuiltin<WasmInt32ToHeapNumberDescriptor>(
          Builtin::kWasmInt32ToHeapNumber, Operator::kNoProperties, value);
    }
    return result;
  }

  V<Number> BuildChangeFloat32ToNumber(V<Float32> value) {
    return CallBuiltin<WasmFloat32ToNumberDescriptor>(
        Builtin::kWasmFloat32ToNumber, Operator::kNoProperties, value);
  }

  V<Number> BuildChangeFloat64ToNumber(V<Float64> value) {
    return CallBuiltin<WasmFloat64ToTaggedDescriptor>(
        Builtin::kWasmFloat64ToNumber, Operator::kNoProperties, value);
  }

  V<Object> ToJS(OpIndex ret, ValueType type, V<Context> context) {
    switch (type.kind()) {
      case wasm::kI32:
        return BuildChangeInt32ToNumber(ret);
      case wasm::kI64:
        return BuildChangeInt64ToBigInt(ret, StubCallMode::kCallBuiltinPointer);
      case wasm::kF32:
        return BuildChangeFloat32ToNumber(ret);
      case wasm::kF64:
        return BuildChangeFloat64ToNumber(ret);
      case wasm::kRef:
        switch (type.heap_representation_non_shared()) {
          case wasm::HeapType::kEq:
          case wasm::HeapType::kI31:
          case wasm::HeapType::kStruct:
          case wasm::HeapType::kArray:
          case wasm::HeapType::kAny:
          case wasm::HeapType::kExtern:
          case wasm::HeapType::kString:
          case wasm::HeapType::kNone:
          case wasm::HeapType::kNoFunc:
          case wasm::HeapType::kNoExtern:
          case wasm::HeapType::kExn:
          case wasm::HeapType::kNoExn:
            return ret;
          case wasm::HeapType::kBottom:
          case wasm::HeapType::kStringViewWtf8:
          case wasm::HeapType::kStringViewWtf16:
          case wasm::HeapType::kStringViewIter:
            UNREACHABLE();
          case wasm::HeapType::kFunc:
          default:
            if (type.heap_representation_non_shared() ==
                    wasm::HeapType::kFunc ||
                module_->has_signature(type.ref_index())) {
              // Function reference. Extract the external function.
              V<WasmInternalFunction> internal =
                  V<WasmInternalFunction>::Cast(__ LoadTrustedPointerField(
                      ret, LoadOp::Kind::TaggedBase(),
                      kWasmInternalFunctionIndirectPointerTag,
                      WasmFuncRef::kTrustedInternalOffset));
              ScopedVar<Object> maybe_external(
                  this, __ Load(internal, LoadOp::Kind::TaggedBase(),
                                MemoryRepresentation::TaggedPointer(),
                                WasmInternalFunction::kExternalOffset));
              IF (__ TaggedEqual(maybe_external, LOAD_ROOT(UndefinedValue))) {
                maybe_external =
                    CallBuiltin<WasmInternalFunctionCreateExternalDescriptor>(
                        Builtin::kWasmInternalFunctionCreateExternal,
                        Operator::kNoProperties, internal, context);
              }
              return maybe_external;
            } else {
              return ret;
            }
        }
      case wasm::kRefNull:
        switch (type.heap_representation_non_shared()) {
          case wasm::HeapType::kExtern:
          case wasm::HeapType::kNoExtern:
          case wasm::HeapType::kExn:
          case wasm::HeapType::kNoExn:
            return ret;
          case wasm::HeapType::kNone:
          case wasm::HeapType::kNoFunc:
            return LOAD_ROOT(NullValue);
          case wasm::HeapType::kEq:
          case wasm::HeapType::kStruct:
          case wasm::HeapType::kArray:
          case wasm::HeapType::kString:
          case wasm::HeapType::kI31:
          case wasm::HeapType::kAny: {
            ScopedVar<Object> result(this, OpIndex::Invalid());
            IF_NOT (__ TaggedEqual(ret, LOAD_ROOT(WasmNull))) {
              result = ret;
            } ELSE{
              result = LOAD_ROOT(NullValue);
            }
            return result;
          }
          case wasm::HeapType::kFunc:
          default: {
            if (type.heap_representation_non_shared() ==
                    wasm::HeapType::kFunc ||
                module_->has_signature(type.ref_index())) {
              ScopedVar<Object> result(this, OpIndex::Invalid());
              IF (__ TaggedEqual(ret, LOAD_ROOT(WasmNull))) {
                result = LOAD_ROOT(NullValue);
              } ELSE{
                V<WasmInternalFunction> internal =
                    V<WasmInternalFunction>::Cast(__ LoadTrustedPointerField(
                        ret, LoadOp::Kind::TaggedBase(),
                        kWasmInternalFunctionIndirectPointerTag,
                        WasmFuncRef::kTrustedInternalOffset));
                V<Object> maybe_external =
                    __ Load(internal, LoadOp::Kind::TaggedBase(),
                            MemoryRepresentation::AnyTagged(),
                            WasmInternalFunction::kExternalOffset);
                IF (__ TaggedEqual(maybe_external, LOAD_ROOT(UndefinedValue))) {
                  V<Object> from_builtin =
                      CallBuiltin<WasmInternalFunctionCreateExternalDescriptor>(
                          Builtin::kWasmInternalFunctionCreateExternal,
                          Operator::kNoProperties, internal, context);
                  result = from_builtin;
                } ELSE{
                  result = maybe_external;
                }
              }
              return result;
            } else {
              ScopedVar<Object> result(this, OpIndex::Invalid());
              IF (__ TaggedEqual(ret, LOAD_ROOT(WasmNull))) {
                result = LOAD_ROOT(NullValue);
              } ELSE{
                result = ret;
              }
              return result;
            }
          }
        }
      case wasm::kRtt:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kS128:
      case wasm::kVoid:
      case wasm::kBottom:
        // If this is reached, then IsJSCompatibleSignature() is too permissive.
        UNREACHABLE();
    }
  }

  // Generate a call to the AllocateJSArray builtin.
  V<JSArray> BuildCallAllocateJSArray(V<Number> array_length,
                                      V<Object> context) {
    // Since we don't check that args will fit in an array,
    // we make sure this is true based on statically known limits.
    static_assert(wasm::kV8MaxWasmFunctionReturns <=
                  JSArray::kInitialMaxFastElementArray);
    return CallBuiltin<WasmAllocateJSArrayDescriptor>(
        Builtin::kWasmAllocateJSArray, Operator::kEliminatable, array_length,
        context);
  }

  void BuildCallWasmFromWrapper(Zone* zone, const FunctionSig* sig,
                                V<WordPtr> callee,
                                V<HeapObject> implicit_first_arg,
                                base::SmallVector<OpIndex, 16> args,
                                base::SmallVector<OpIndex, 1>& returns) {
    const TSCallDescriptor* descriptor = TSCallDescriptor::Create(
        compiler::GetWasmCallDescriptor(__ graph_zone(), sig),
        compiler::CanThrow::kYes, compiler::LazyDeoptOnThrow::kNo,
        __ graph_zone());

    args[0] = implicit_first_arg;
    OpIndex call = __ Call(callee, OpIndex::Invalid(), base::VectorOf(args),
                           descriptor, OpEffects().CanCallAnything());

    if (sig->return_count() == 1) {
      returns[0] = call;
    } else if (sig->return_count() > 1) {
      for (uint32_t i = 0; i < sig->return_count(); i++) {
        wasm::ValueType type = sig->GetReturn(i);
        returns[i] = __ Projection(call, i, RepresentationFor(type));
      }
    }
  }

  OpIndex BuildCallAndReturn(V<Context> js_context, V<HeapObject> function_data,
                             base::SmallVector<OpIndex, 16> args,
                             bool do_conversion, bool set_in_wasm_flag,
                             uint64_t expected_sig_hash) {
    const int rets_count = static_cast<int>(sig_->return_count());
    base::SmallVector<OpIndex, 1> rets(rets_count);

    // Set the ThreadInWasm flag before we do the actual call.
    {
      std::optional<ModifyThreadInWasmFlagScope>
          modify_thread_in_wasm_flag_builder;
      if (set_in_wasm_flag) {
        modify_thread_in_wasm_flag_builder.emplace(this, Asm());
      }

      V<WasmInternalFunction> internal =
          V<WasmInternalFunction>::Cast(__ LoadProtectedPointerField(
              function_data, LoadOp::Kind::TaggedBase().Immutable(),
              WasmExportedFunctionData::kProtectedInternalOffset));
      auto [target, implicit_arg] =
          BuildFunctionTargetAndImplicitArg(internal, expected_sig_hash);
      BuildCallWasmFromWrapper(__ phase_zone(), sig_, target, implicit_arg,
                               args, rets);
    }

    V<Object> jsval;
    if (sig_->return_count() == 0) {
      jsval = LOAD_ROOT(UndefinedValue);
    } else if (sig_->return_count() == 1) {
      jsval = do_conversion ? ToJS(rets[0], sig_->GetReturn(), js_context)
                            : rets[0];
    } else {
      int32_t return_count = static_cast<int32_t>(sig_->return_count());
      V<Smi> size = __ SmiConstant(Smi::FromInt(return_count));

      jsval = BuildCallAllocateJSArray(size, js_context);

      V<FixedArray> fixed_array = __ Load(jsval, LoadOp::Kind::TaggedBase(),
                                          MemoryRepresentation::TaggedPointer(),
                                          JSObject::kElementsOffset);

      for (int i = 0; i < return_count; ++i) {
        V<Object> value = ToJS(rets[i], sig_->GetReturn(i), js_context);
        __ StoreFixedArrayElement(fixed_array, i, value,
                                  compiler::kFullWriteBarrier);
      }
    }
    return jsval;
  }

  void BuildJSToWasmWrapper(
      bool do_conversion = true,
      compiler::turboshaft::OptionalOpIndex frame_state =
          compiler::turboshaft::OptionalOpIndex::Nullopt(),
      bool set_in_wasm_flag = true) {
    const int wasm_param_count = static_cast<int>(sig_->parameter_count());

    __ Bind(__ NewBlock());

    // Create the js_closure and js_context parameters.
    V<JSFunction> js_closure =
        __ Parameter(compiler::Linkage::kJSCallClosureParamIndex,
                     RegisterRepresentation::Tagged());
    V<Context> js_context = __ Parameter(
        compiler::Linkage::GetJSCallContextParamIndex(wasm_param_count + 1),
        RegisterRepresentation::Tagged());
    V<SharedFunctionInfo> shared =
        __ Load(js_closure, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::TaggedPointer(),
                JSFunction::kSharedFunctionInfoOffset);
#ifdef V8_ENABLE_SANDBOX
    uint64_t signature_hash = SignatureHasher::Hash(sig_);
#else
    uint64_t signature_hash = 0;
#endif
    V<WasmFunctionData> function_data =
        V<WasmFunctionData>::Cast(__ LoadTrustedPointerField(
            shared, LoadOp::Kind::TaggedBase(),
            kWasmFunctionDataIndirectPointerTag,
            SharedFunctionInfo::kTrustedFunctionDataOffset));

    if (!wasm::IsJSCompatibleSignature(sig_)) {
      // Throw a TypeError. Use the js_context of the calling javascript
      // function (passed as a parameter), such that the generated code is
      // js_context independent.
      CallRuntime(__ phase_zone(), Runtime::kWasmThrowJSTypeError, {},
                  js_context);
      __ Unreachable();
      return;
    }

    const int args_count = wasm_param_count + 1;  // +1 for wasm_code.

    // Check whether the signature of the function allows for a fast
    // transformation (if any params exist that need transformation).
    // Create a fast transformation path, only if it does.
    bool include_fast_path =
        do_conversion && wasm_param_count > 0 && QualifiesForFastTransform();

    // Prepare Param() nodes. Param() nodes can only be created once,
    // so we need to use the same nodes along all possible transformation paths.
    base::SmallVector<OpIndex, 16> params(args_count);
    for (int i = 0; i < wasm_param_count; ++i) {
      params[i + 1] = __ Parameter(i + 1, RegisterRepresentation::Tagged());
    }

    Label<Object> done(&Asm());
    V<Object> jsval;
    if (include_fast_path) {
      TSBlock* slow_path = __ NewBlock();
      // Check if the params received on runtime can be actually transformed
      // using the fast transformation. When a param that cannot be transformed
      // fast is encountered, skip checking the rest and fall back to the slow
      // path.
      for (int i = 0; i < wasm_param_count; ++i) {
        CanTransformFast(params[i + 1], sig_->GetParam(i), slow_path);
      }
      // Convert JS parameters to wasm numbers using the fast transformation
      // and build the call.
      base::SmallVector<OpIndex, 16> args(args_count);
      for (int i = 0; i < wasm_param_count; ++i) {
        OpIndex wasm_param = FromJSFast(params[i + 1], sig_->GetParam(i));
        args[i + 1] = wasm_param;
      }
      jsval = BuildCallAndReturn(js_context, function_data, args, do_conversion,
                                 set_in_wasm_flag, signature_hash);
      GOTO(done, jsval);
      __ Bind(slow_path);
    }
    // Convert JS parameters to wasm numbers using the default transformation
    // and build the call.
    base::SmallVector<OpIndex, 16> args(args_count);
    for (int i = 0; i < wasm_param_count; ++i) {
      if (do_conversion) {
        args[i + 1] = FromJS(params[i + 1], js_context, sig_->GetParam(i),
                             module_, frame_state);
      } else {
        OpIndex wasm_param = params[i + 1];

        // For Float32 parameters
        // we set UseInfo::CheckedNumberOrOddballAsFloat64 in
        // simplified-lowering and we need to add here a conversion from Float64
        // to Float32.
        if (sig_->GetParam(i).kind() == wasm::kF32) {
          wasm_param = __ TruncateFloat64ToFloat32(wasm_param);
        }
        args[i + 1] = wasm_param;
      }
    }

    jsval = BuildCallAndReturn(js_context, function_data, args, do_conversion,
                               set_in_wasm_flag, signature_hash);
    // If both the default and a fast transformation paths are present,
    // get the return value based on the path used.
    if (include_fast_path) {
      GOTO(done, jsval);
      BIND(done, result);
      __ Return(result);
    } else {
      __ Return(jsval);
    }
  }

  void BuildWasmToJSWrapper(wasm::ImportCallKind kind, int expected_arity,
                            wasm::Suspend suspend, const WasmModule* module) {
    int wasm_count = static_cast<int>(sig_->parameter_count());

    __ Bind(__ NewBlock());
    base::SmallVector<OpIndex, 16> wasm_params(wasm_count);
    OpIndex ref = __ Parameter(0, RegisterRepresentation::Tagged());
    for (int i = 0; i < wasm_count; ++i) {
      RegisterRepresentation rep = RepresentationFor(sig_->GetParam(i));
      wasm_params[i] = (__ Parameter(1 + i, rep));
    }

    V<Context> native_context = __ Load(ref, LoadOp::Kind::TaggedBase(),
                                        MemoryRepresentation::TaggedPointer(),
                                        WasmImportData::kNativeContextOffset);

    if (kind == wasm::ImportCallKind::kRuntimeTypeError) {
      // =======================================================================
      // === Runtime TypeError =================================================
      // =======================================================================
      CallRuntime(zone_, Runtime::kWasmThrowJSTypeError, {}, native_context);
      __ Unreachable();
      return;
    }

    V<Undefined> undefined_node = LOAD_ROOT(UndefinedValue);
    int pushed_count = std::max(expected_arity, wasm_count);
    // 4 extra arguments: receiver, new target, arg count and context.
    base::SmallVector<OpIndex, 16> args(pushed_count + 4);
    // Position of the first wasm argument in the JS arguments.
    int pos = kind == ImportCallKind::kUseCallBuiltin ? 3 : 1;
    pos = AddArgumentNodes(base::VectorOf(args), pos, wasm_params, sig_,
                           native_context);
    for (int i = wasm_count; i < expected_arity; ++i) {
      args[pos++] = undefined_node;
    }

    V<JSFunction> callable_node = __ Load(ref, LoadOp::Kind::TaggedBase(),
                                          MemoryRepresentation::TaggedPointer(),
                                          WasmImportData::kCallableOffset);
    OpIndex old_sp = BuildSwitchToTheCentralStackIfNeeded();
    BuildModifyThreadInWasmFlag(__ phase_zone(), false);
    OpIndex call = OpIndex::Invalid();
    switch (kind) {
      // =======================================================================
      // === JS Functions ======================================================
      // =======================================================================
      case wasm::ImportCallKind::kJSFunctionArityMatch:
        DCHECK_EQ(expected_arity, wasm_count);
        [[fallthrough]];
      case wasm::ImportCallKind::kJSFunctionArityMismatch: {
        auto call_descriptor = compiler::Linkage::GetJSCallDescriptor(
            __ graph_zone(), false, pushed_count + 1, CallDescriptor::kNoFlags);
        const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
            call_descriptor, compiler::CanThrow::kYes,
            compiler::LazyDeoptOnThrow::kNo, __ graph_zone());

        // Determine receiver at runtime.
        args[0] =
            BuildReceiverNode(callable_node, native_context, undefined_node);
        DCHECK_EQ(pos, pushed_count + 1);
        args[pos++] = undefined_node;  // new target
        args[pos++] =
            __ Word32Constant(JSParameterCount(wasm_count));  // argument count
        args[pos++] = LoadContextFromJSFunction(callable_node);
        call = __ Call(callable_node, OpIndex::Invalid(), base::VectorOf(args),
                       ts_call_descriptor);
        break;
      }
      // =======================================================================
      // === General case of unknown callable ==================================
      // =======================================================================
      case wasm::ImportCallKind::kUseCallBuiltin: {
        DCHECK_EQ(expected_arity, wasm_count);
        OpIndex target = GetBuiltinPointerTarget(Builtin::kCall_ReceiverIsAny);
        args[0] = callable_node;
        args[1] =
            __ Word32Constant(JSParameterCount(wasm_count));  // argument count
        args[2] = undefined_node;                             // receiver

        auto call_descriptor = compiler::Linkage::GetStubCallDescriptor(
            __ graph_zone(), CallTrampolineDescriptor{}, wasm_count + 1,
            CallDescriptor::kNoFlags, Operator::kNoProperties,
            StubCallMode::kCallBuiltinPointer);
        const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
            call_descriptor, compiler::CanThrow::kYes,
            compiler::LazyDeoptOnThrow::kNo, __ graph_zone());

        // The native_context is sufficient here, because all kind of callables
        // which depend on the context provide their own context. The context
        // here is only needed if the target is a constructor to throw a
        // TypeError, if the target is a native function, or if the target is a
        // callable JSObject, which can only be constructed by the runtime.
        args[pos++] = native_context;
        call = __ Call(target, OpIndex::Invalid(), base::VectorOf(args),
                       ts_call_descriptor);
        break;
      }
      default:
        UNIMPLEMENTED();
    }
    // For asm.js the error location can differ depending on whether an
    // exception was thrown in imported JS code or an exception was thrown in
    // the ToNumber builtin that converts the result of the JS code a
    // WebAssembly value. The source position allows asm.js to determine the
    // correct error location. Source position 1 encodes the call to ToNumber,
    // source position 0 encodes the call to the imported JS code.
    __ output_graph().source_positions()[call] = SourcePosition(0);
    DCHECK(call.valid());

    if (suspend == kSuspend) {
      call = BuildSuspend(call, LOAD_ROOT(ActiveSuspender), ref, &old_sp);
    }

    // Convert the return value(s) back.
    OpIndex val;
    base::SmallVector<OpIndex, 8> wasm_values;
    if (sig_->return_count() <= 1) {
      val = sig_->return_count() == 0
                ? __ Word32Constant(0)
                : FromJS(call, native_context, sig_->GetReturn(), module);
    } else {
      V<FixedArray> fixed_array =
          BuildMultiReturnFixedArrayFromIterable(call, native_context);
      wasm_values.resize_no_init(sig_->return_count());
      for (unsigned i = 0; i < sig_->return_count(); ++i) {
        wasm_values[i] = FromJS(__ LoadFixedArrayElement(fixed_array, i),
                                native_context, sig_->GetReturn(i), module);
      }
    }
    BuildModifyThreadInWasmFlag(__ phase_zone(), true);
    IF_NOT (LIKELY(__ WordPtrEqual(old_sp, __ IntPtrConstant(0)))) {
      BuildSwitchBackFromCentralStack(old_sp);
    }
    if (sig_->return_count() <= 1) {
      __ Return(val);
    } else {
      __ Return(__ Word32Constant(0), base::VectorOf(wasm_values));
    }
  }

  void BuildCapiCallWrapper(const WasmModule* module) {
    __ Bind(__ NewBlock());
    base::SmallVector<OpIndex, 8> incoming_params;
    // Instance.
    incoming_params.push_back(
        __ Parameter(0, RegisterRepresentation::Tagged()));
    // Wasm parameters.
    for (int i = 0; i < static_cast<int>(sig_->parameter_count()); ++i) {
      incoming_params.push_back(
          __ Parameter(i + 1, RepresentationFor(sig_->GetParam(i))));
    }
    // Store arguments on our stack, then align the stack for calling to C.
    int param_bytes = 0;
    for (wasm::ValueType type : sig_->parameters()) {
      param_bytes += type.value_kind_size();
    }
    int return_bytes = 0;
    for (wasm::ValueType type : sig_->returns()) {
      return_bytes += type.value_kind_size();
    }

    int stack_slot_bytes = std::max(param_bytes, return_bytes);
    OpIndex values = stack_slot_bytes == 0
                         ? __ IntPtrConstant(0)
                         : __ StackSlot(stack_slot_bytes, kDoubleAlignment);

    int offset = 0;
    for (size_t i = 0; i < sig_->parameter_count(); ++i) {
      wasm::ValueType type = sig_->GetParam(i);
      // Start from the parameter with index 1 to drop the instance_node.
      // TODO(jkummerow): When a values is a reference type, we should pass it
      // in a GC-safe way, not just as a raw pointer.
      SafeStore(offset, type, values, incoming_params[i + 1]);
      offset += type.value_kind_size();
    }

    V<Object> function_node =
        __ LoadTaggedField(incoming_params[0], WasmImportData::kCallableOffset);
    V<HeapObject> shared = LoadSharedFunctionInfo(function_node);
    V<WasmFunctionData> function_data =
        V<WasmFunctionData>::Cast(__ LoadTrustedPointerField(
            shared, LoadOp::Kind::TaggedBase(),
            kWasmFunctionDataIndirectPointerTag,
            SharedFunctionInfo::kTrustedFunctionDataOffset));
    V<Object> host_data_foreign = __ LoadTaggedField(
        function_data, WasmCapiFunctionData::kEmbedderDataOffset);

    BuildModifyThreadInWasmFlag(__ phase_zone(), false);
    OpIndex isolate_root = __ LoadRootRegister();
    OpIndex fp_value = __ FramePointer();
    __ Store(isolate_root, fp_value, StoreOp::Kind::RawAligned(),
             MemoryRepresentation::UintPtr(), compiler::kNoWriteBarrier,
             Isolate::c_entry_fp_offset());

    V<WordPtr> call_target =
        BuildLoadCallTargetFromExportedFunctionData(function_data);

    // Parameters: Address host_data_foreign, Address arguments.
    auto host_sig =
        FixedSizeSignature<MachineType>::Returns(MachineType::Pointer())
            .Params(MachineType::AnyTagged(), MachineType::Pointer());
    OpIndex return_value =
        CallC(&host_sig, call_target, {host_data_foreign, values});

    BuildModifyThreadInWasmFlag(__ phase_zone(), true);

    IF_NOT (__ WordPtrEqual(return_value, __ IntPtrConstant(0))) {
      WasmRethrowExplicitContextDescriptor interface_descriptor;
      auto call_descriptor = compiler::Linkage::GetStubCallDescriptor(
          __ graph_zone(), interface_descriptor,
          interface_descriptor.GetStackParameterCount(),
          CallDescriptor::kNoFlags, Operator::kNoProperties,
          StubCallMode::kCallWasmRuntimeStub);
      const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
          call_descriptor, compiler::CanThrow::kYes,
          compiler::LazyDeoptOnThrow::kNo, __ graph_zone());
      OpIndex call_target = __ RelocatableWasmBuiltinCallTarget(
          Builtin::kWasmRethrowExplicitContext);
      V<Context> context =
          __ Load(incoming_params[0], LoadOp::Kind::TaggedBase(),
                  MemoryRepresentation::TaggedPointer(),
                  WasmImportData::kNativeContextOffset);
      __ Call(call_target, {return_value, context}, ts_call_descriptor);
      __ Unreachable();
    }

    DCHECK_LT(sig_->return_count(), wasm::kV8MaxWasmFunctionReturns);
    size_t return_count = sig_->return_count();
    if (return_count == 0) {
      __ Return(__ Word32Constant(0));
    } else {
      base::SmallVector<OpIndex, 8> returns(return_count);
      offset = 0;
      for (size_t i = 0; i < return_count; ++i) {
        wasm::ValueType type = sig_->GetReturn(i);
        OpIndex val = SafeLoad(values, offset, type);
        returns[i] = val;
        offset += type.value_kind_size();
      }
      __ Return(__ Word32Constant(0), base::VectorOf(returns));
    }
  }

  V<Word32> BuildSmiShiftBitsConstant() {
    return __ Word32Constant(kSmiShiftSize + kSmiTagSize);
  }

  V<Word32> BuildSmiShiftBitsConstant32() {
    return __ Word32Constant(kSmiShiftSize + kSmiTagSize);
  }

  V<Word32> BuildChangeSmiToInt32(OpIndex value) {
    return COMPRESS_POINTERS_BOOL
               ? __ Word32ShiftRightArithmetic(value,
                                               BuildSmiShiftBitsConstant32())
               : __
                 TruncateWordPtrToWord32(__ WordPtrShiftRightArithmetic(
                     value, BuildSmiShiftBitsConstant()));
  }

  V<Float64> HeapNumberToFloat64(V<HeapNumber> input) {
    return __ template LoadField<Float64>(
        input, compiler::AccessBuilder::ForHeapNumberValue());
  }

  OpIndex FromJSFast(OpIndex input, wasm::ValueType type) {
    switch (type.kind()) {
      case wasm::kI32:
        return BuildChangeSmiToInt32(input);
      case wasm::kF32: {
        ScopedVar<Float32> result(this, OpIndex::Invalid());
        IF (__ IsSmi(input)) {
          result = __ ChangeInt32ToFloat32(__ UntagSmi(input));
        } ELSE {
          result = __ TruncateFloat64ToFloat32(HeapNumberToFloat64(input));
        }
        return result;
      }
      case wasm::kF64: {
        ScopedVar<Float64> result(this, OpIndex::Invalid());
        IF (__ IsSmi(input)) {
          result = __ ChangeInt32ToFloat64(__ UntagSmi(input));
        } ELSE{
          result = HeapNumberToFloat64(input);
        }
        return result;
      }
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kI64:
      case wasm::kRtt:
      case wasm::kS128:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kBottom:
      case wasm::kVoid:
        UNREACHABLE();
    }
  }

  OpIndex LoadInstanceType(V<Map> map) {
    return __ Load(map, LoadOp::Kind::TaggedBase().Immutable(),
                   MemoryRepresentation::Uint16(), Map::kInstanceTypeOffset);
  }

  OpIndex BuildCheckString(OpIndex input, OpIndex js_context, ValueType type) {
    auto done = __ NewBlock();
    auto type_error = __ NewBlock();
    ScopedVar<Object> result(this, LOAD_ROOT(WasmNull));
    __ GotoIf(__ IsSmi(input), type_error, BranchHint::kFalse);
    if (type.is_nullable()) {
      auto not_null = __ NewBlock();
      __ GotoIfNot(__ TaggedEqual(input, LOAD_ROOT(NullValue)), not_null);
      __ Goto(done);
      __ Bind(not_null);
    }
    V<Map> map = LoadMap(input);
    OpIndex instance_type = LoadInstanceType(map);
    OpIndex check = __ Uint32LessThan(instance_type,
                                      __ Word32Constant(FIRST_NONSTRING_TYPE));
    result = input;
    __ GotoIf(check, done, BranchHint::kTrue);
    __ Goto(type_error);
    __ Bind(type_error);
    CallRuntime(__ phase_zone(), Runtime::kWasmThrowJSTypeError, {},
                js_context);
    __ Unreachable();
    __ Bind(done);
    return result;
  }

  V<Float64> BuildChangeTaggedToFloat64(
      OpIndex value, OpIndex context,
      compiler::turboshaft::OptionalOpIndex frame_state) {
    OpIndex call = frame_state.valid()
                       ? CallBuiltin<WasmTaggedToFloat64Descriptor>(
                             Builtin::kWasmTaggedToFloat64, frame_state.value(),
                             Operator::kNoProperties, value, context)
                       : CallBuiltin<WasmTaggedToFloat64Descriptor>(
                             Builtin::kWasmTaggedToFloat64,
                             Operator::kNoProperties, value, context);
    // The source position here is needed for asm.js, see the comment on the
    // source position of the call to JavaScript in the wasm-to-js wrapper.
    __ output_graph().source_positions()[call] = SourcePosition(1);
    return call;
  }

  OpIndex BuildChangeTaggedToInt32(
      OpIndex value, OpIndex context,
      compiler::turboshaft::OptionalOpIndex frame_state) {
    // We expect most integers at runtime to be Smis, so it is important for
    // wrapper performance that Smi conversion be inlined.
    ScopedVar<Word32> result(this, OpIndex::Invalid());
    IF (__ IsSmi(value)) {
      result = BuildChangeSmiToInt32(value);
    } ELSE{
      OpIndex call =
          frame_state.valid()
              ? CallBuiltin<WasmTaggedNonSmiToInt32Descriptor>(
                    Builtin::kWasmTaggedNonSmiToInt32, frame_state.value(),
                    Operator::kNoProperties, value, context)
              : CallBuiltin<WasmTaggedNonSmiToInt32Descriptor>(
                    Builtin::kWasmTaggedNonSmiToInt32, Operator::kNoProperties,
                    value, context);
      result = call;
      // The source position here is needed for asm.js, see the comment on the
      // source position of the call to JavaScript in the wasm-to-js wrapper.
      __ output_graph().source_positions()[call] = SourcePosition(1);
    }
    return result;
  }

  CallDescriptor* GetBigIntToI64CallDescriptor(bool needs_frame_state) {
    return wasm::GetWasmEngine()->call_descriptors()->GetBigIntToI64Descriptor(
        needs_frame_state);
  }

  OpIndex BuildChangeBigIntToInt64(
      OpIndex input, OpIndex context,
      compiler::turboshaft::OptionalOpIndex frame_state) {
    OpIndex target;
    if (Is64()) {
      target = GetTargetForBuiltinCall(Builtin::kBigIntToI64);
    } else {
      // On 32-bit platforms we already set the target to the
      // BigIntToI32Pair builtin here, so that we don't have to replace the
      // target in the int64-lowering.
      target = GetTargetForBuiltinCall(Builtin::kBigIntToI32Pair);
    }

    CallDescriptor* call_descriptor =
        GetBigIntToI64CallDescriptor(frame_state.valid());
    const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
        call_descriptor, compiler::CanThrow::kNo,
        compiler::LazyDeoptOnThrow::kNo, __ graph_zone());
    return frame_state.valid()
               ? __ Call(target, frame_state.value(),
                         base::VectorOf({input, context}), ts_call_descriptor)
               : __ Call(target, {input, context}, ts_call_descriptor);
  }

  OpIndex FromJS(OpIndex input, OpIndex context, ValueType type,
                 const WasmModule* module, OptionalOpIndex frame_state = {}) {
    switch (type.kind()) {
      case wasm::kRef:
      case wasm::kRefNull: {
        switch (type.heap_representation_non_shared()) {
          // TODO(14034): Add more fast paths?
          case wasm::HeapType::kExtern:
          case wasm::HeapType::kNoExtern:
          case wasm::HeapType::kExn:
          case wasm::HeapType::kNoExn:
            if (type.kind() == wasm::kRef) {
              IF (UNLIKELY(__ TaggedEqual(input, LOAD_ROOT(NullValue)))) {
                CallRuntime(__ phase_zone(), Runtime::kWasmThrowJSTypeError, {},
                            context);
                __ Unreachable();
              }
            }
            return input;
          case wasm::HeapType::kString:
            return BuildCheckString(input, context, type);
          case wasm::HeapType::kNone:
          case wasm::HeapType::kNoFunc:
          case wasm::HeapType::kI31:
          case wasm::HeapType::kAny:
          case wasm::HeapType::kFunc:
          case wasm::HeapType::kStruct:
          case wasm::HeapType::kArray:
          case wasm::HeapType::kEq:
          default: {
            // Make sure ValueType fits in a Smi.
            static_assert(wasm::ValueType::kLastUsedBit + 1 <= kSmiValueSize);

            uint32_t canonical_index = kInvalidCanonicalIndex;
            if (type.has_index()) {
              DCHECK_NOT_NULL(module);
              canonical_index =
                  module->isorecursive_canonical_type_ids[type.ref_index()];
              DCHECK_LE(canonical_index, kSmiMaxValue);
            }

            std::initializer_list<const OpIndex> inputs = {
                input,
                __ IntPtrConstant(
                    IntToSmi(static_cast<int>(type.raw_bit_field()))),
                __ IntPtrConstant(IntToSmi(static_cast<int>(canonical_index)))};
            return CallRuntime(__ phase_zone(), Runtime::kWasmJSToWasmObject,
                               inputs, context);
          }
        }
      }
      case wasm::kF32:
        return __ TruncateFloat64ToFloat32(
            BuildChangeTaggedToFloat64(input, context, frame_state));

      case wasm::kF64:
        return BuildChangeTaggedToFloat64(input, context, frame_state);

      case wasm::kI32:
        return BuildChangeTaggedToInt32(input, context, frame_state);

      case wasm::kI64:
        // i64 values can only come from BigInt.
        return BuildChangeBigIntToInt64(input, context, frame_state);

      case wasm::kRtt:
      case wasm::kS128:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kBottom:
      case wasm::kVoid:
        // If this is reached, then IsJSCompatibleSignature() is too permissive.
        UNREACHABLE();
    }
  }

  bool QualifiesForFastTransform() {
    const int wasm_count = static_cast<int>(sig_->parameter_count());
    for (int i = 0; i < wasm_count; ++i) {
      wasm::ValueType type = sig_->GetParam(i);
      switch (type.kind()) {
        case wasm::kRef:
        case wasm::kRefNull:
        case wasm::kI64:
        case wasm::kRtt:
        case wasm::kS128:
        case wasm::kI8:
        case wasm::kI16:
        case wasm::kF16:
        case wasm::kBottom:
        case wasm::kVoid:
          return false;
        case wasm::kI32:
        case wasm::kF32:
        case wasm::kF64:
          break;
      }
    }
    return true;
  }

#ifdef V8_MAP_PACKING
  V<Map> UnpackMapWord(OpIndex map_word) {
    map_word = __ BitcastTaggedToWordPtrForTagAndSmiBits(map_word);
    // TODO(wenyuzhao): Clear header metadata.
    OpIndex map = __ WordBitwiseXor(
        map_word, __ IntPtrConstant(Internals::kMapWordXorMask),
        WordRepresentation::UintPtr());
    return V<Map>::Cast(__ BitcastWordPtrToTagged(map));
  }
#endif

  V<Map> LoadMap(V<Object> object) {
    // TODO(thibaudm): Handle map packing.
    OpIndex map_word = __ Load(object, LoadOp::Kind::TaggedBase(),
                               MemoryRepresentation::TaggedPointer(), 0);
#ifdef V8_MAP_PACKING
    return UnpackMapWord(map_word);
#else
    return map_word;
#endif
  }

  void CanTransformFast(OpIndex input, wasm::ValueType type,
                        TSBlock* slow_path) {
    switch (type.kind()) {
      case wasm::kI32: {
        __ GotoIfNot(LIKELY(__ IsSmi(input)), slow_path);
        return;
      }
      case wasm::kF32:
      case wasm::kF64: {
        TSBlock* done = __ NewBlock();
        __ GotoIf(__ IsSmi(input), done);
        V<Map> map = LoadMap(input);
        V<Map> heap_number_map = LOAD_ROOT(HeapNumberMap);
        // TODO(thibaudm): Handle map packing.
        V<Word32> is_heap_number = __ TaggedEqual(heap_number_map, map);
        __ GotoIf(LIKELY(is_heap_number), done);
        __ Goto(slow_path);
        __ Bind(done);
        return;
      }
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kI64:
      case wasm::kRtt:
      case wasm::kS128:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kBottom:
      case wasm::kVoid:
        UNREACHABLE();
    }
  }

  // Must be called in the first block to emit the Parameter ops.
  int AddArgumentNodes(base::Vector<OpIndex> args, int pos,
                       base::SmallVector<OpIndex, 16> wasm_params,
                       const wasm::FunctionSig* sig, V<Context> context) {
    // Convert wasm numbers to JS values.
    for (size_t i = 0; i < wasm_params.size(); ++i) {
      args[pos++] = ToJS(wasm_params[i], sig->GetParam(i), context);
    }
    return pos;
  }

  OpIndex LoadSharedFunctionInfo(V<Object> js_function) {
    return __ Load(js_function, LoadOp::Kind::TaggedBase(),
                   MemoryRepresentation::TaggedPointer(),
                   JSFunction::kSharedFunctionInfoOffset);
  }

  OpIndex BuildReceiverNode(OpIndex callable_node, OpIndex native_context,
                            V<Undefined> undefined_node) {
    // Check function strict bit.
    V<SharedFunctionInfo> shared_function_info =
        LoadSharedFunctionInfo(callable_node);
    OpIndex flags = __ Load(shared_function_info, LoadOp::Kind::TaggedBase(),
                            MemoryRepresentation::Int32(),
                            SharedFunctionInfo::kFlagsOffset);
    OpIndex strict_check = __ Word32BitwiseAnd(
        flags, __ Word32Constant(SharedFunctionInfo::IsNativeBit::kMask |
                                 SharedFunctionInfo::IsStrictBit::kMask));

    // Load global receiver if sloppy else use undefined.
    ScopedVar<Object> strict_d(this, OpIndex::Invalid());
    IF (strict_check) {
      strict_d = undefined_node;
    } ELSE {
      strict_d =
          __ LoadFixedArrayElement(native_context, Context::GLOBAL_PROXY_INDEX);
    }
    return strict_d;
  }

  V<Context> LoadContextFromJSFunction(V<JSFunction> js_function) {
    return __ Load(js_function, LoadOp::Kind::TaggedBase(),
                   MemoryRepresentation::TaggedPointer(),
                   JSFunction::kContextOffset);
  }

  OpIndex BuildSwitchToTheCentralStack() {
    V<WordPtr> stack_limit_slot = __ WordPtrAdd(
        __ FramePointer(),
        __ UintPtrConstant(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset));

    MachineType reps[] = {MachineType::Pointer(), MachineType::Pointer(),
                          MachineType::Pointer()};
    MachineSignature sig(1, 2, reps);

    OpIndex central_stack_sp = CallC(
        &sig, ExternalReference::wasm_switch_to_the_central_stack_for_js(),
        {__ ExternalConstant(ExternalReference::isolate_address()),
         stack_limit_slot});
    OpIndex old_sp = __ LoadStackPointer();
    // Temporarily disallow sp-relative offsets.
    __ SetStackPointer(central_stack_sp);
    __ Store(__ FramePointer(), central_stack_sp, StoreOp::Kind::RawAligned(),
             MemoryRepresentation::UintPtr(), compiler::kNoWriteBarrier,
             WasmImportWrapperFrameConstants::kCentralStackSPOffset);
    return old_sp;
  }

  OpIndex BuildSwitchToTheCentralStackIfNeeded() {
    OpIndex isolate_root = __ LoadRootRegister();
    OpIndex is_on_central_stack_flag = __ Load(
        isolate_root, LoadOp::Kind::RawAligned(), MemoryRepresentation::Uint8(),
        IsolateData::is_on_central_stack_flag_offset());
    ScopedVar<WordPtr> old_sp_var(this, __ IntPtrConstant(0));
    // The stack switch performs a C call which causes some spills that would
    // not be needed otherwise. Add a branch hint such that we don't spill if we
    // are already on the central stack.
    // TODO(thibaudm): Look into ways to optimize the switching case as well.
    // Can we avoid the C call? Can we avoid spilling callee-saved registers?
    IF_NOT (LIKELY(is_on_central_stack_flag)) {
      OpIndex old_sp = BuildSwitchToTheCentralStack();
      old_sp_var = old_sp;
    }
    return old_sp_var;
  }

  void BuildSwitchBackFromCentralStack(OpIndex old_sp) {
    OpIndex stack_limit =
        __ Load(__ FramePointer(), LoadOp::Kind::RawAligned(),
                MemoryRepresentation::UintPtr(),
                WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset);

    MachineType reps[] = {MachineType::Pointer(), MachineType::Pointer()};
    MachineSignature sig(0, 2, reps);
    CallC(&sig, ExternalReference::wasm_switch_from_the_central_stack_for_js(),
          {__ ExternalConstant(ExternalReference::isolate_address()),
           stack_limit});
    __ Store(__ FramePointer(), __ IntPtrConstant(0),
             StoreOp::Kind::RawAligned(), MemoryRepresentation::UintPtr(),
             compiler::kNoWriteBarrier,
             WasmImportWrapperFrameConstants::kCentralStackSPOffset);
    __ SetStackPointer(old_sp);
  }

  OpIndex BuildSuspend(OpIndex value, V<Object> suspender,
                       V<Object> import_data, OpIndex* old_sp) {
    V<Context> native_context = __ Load(import_data, LoadOp::Kind::TaggedBase(),
                                        MemoryRepresentation::TaggedPointer(),
                                        WasmImportData::kNativeContextOffset);
    OpIndex active_suspender = LOAD_ROOT(ActiveSuspender);

    // If value is a promise, suspend to the js-to-wasm prompt, and resume later
    // with the promise's resolved value.
    ScopedVar<Object> result(this, value);
    ScopedVar<WordPtr> old_sp_var(this, *old_sp);
    IF_NOT (__ IsSmi(value)) {
      IF (__ HasInstanceType(value, JS_PROMISE_TYPE)) {
        IF (__ TaggedEqual(active_suspender, LOAD_ROOT(UndefinedValue))) {
          CallRuntime(__ phase_zone(), Runtime::kThrowBadSuspenderError, {},
                      native_context);
          __ Unreachable();
        }
        IF_NOT (__ TaggedEqual(suspender, active_suspender)) {
          CallRuntime(__ phase_zone(), Runtime::kThrowBadSuspenderError, {},
                      native_context);
          __ Unreachable();
        }
        // If {old_sp} is null, it must be that we were on the central stack
        // before entering the wasm-to-js wrapper, which means that there are JS
        // frames in the current suspender. JS frames cannot be suspended, so
        // trap.
        OpIndex has_js_frames = __ WordPtrEqual(__ IntPtrConstant(0), *old_sp);
        IF (has_js_frames) {
          // {ThrowWasmError} expects to be called from wasm code, so set the
          // thread-in-wasm flag now.
          // Usually we set this flag later so that it stays off while we
          // convert the return values. This is a special case, it is safe to
          // set it now because the error will unwind this frame.
          BuildModifyThreadInWasmFlag(__ phase_zone(), true);
          V<Smi> error = __ SmiConstant(Smi::FromInt(
              static_cast<int32_t>(MessageTemplate::kWasmTrapSuspendJSFrames)));
          CallRuntime(__ phase_zone(), Runtime::kThrowWasmError, {error},
                      native_context);
          __ Unreachable();
        }
        V<Object> on_fulfilled = __ Load(suspender, LoadOp::Kind::TaggedBase(),
                                         MemoryRepresentation::TaggedPointer(),
                                         WasmSuspenderObject::kResumeOffset);
        V<Object> on_rejected = __ Load(suspender, LoadOp::Kind::TaggedBase(),
                                        MemoryRepresentation::TaggedPointer(),
                                        WasmSuspenderObject::kRejectOffset);

        OpIndex promise_then =
            GetBuiltinPointerTarget(Builtin::kPerformPromiseThen);
        auto* then_call_desc = GetBuiltinCallDescriptor(
            Builtin::kPerformPromiseThen, __ graph_zone());
        base::SmallVector<OpIndex, 16> args{value, on_fulfilled, on_rejected,
                                            LOAD_ROOT(UndefinedValue),
                                            native_context};
        __ Call(promise_then, OpIndex::Invalid(), base::VectorOf(args),
                then_call_desc);

        OpIndex suspend = GetTargetForBuiltinCall(Builtin::kWasmSuspend);
        auto* suspend_call_descriptor =
            GetBuiltinCallDescriptor(Builtin::kWasmSuspend, __ graph_zone());
        BuildSwitchBackFromCentralStack(*old_sp);
        OpIndex resolved =
            __ Call(suspend, {suspender}, suspend_call_descriptor);
        old_sp_var = BuildSwitchToTheCentralStack();
        result = resolved;
      }
    }
    *old_sp = old_sp_var;
    return result;
  }

  V<FixedArray> BuildMultiReturnFixedArrayFromIterable(OpIndex iterable,
                                                       V<Context> context) {
    V<Smi> length = __ SmiConstant(Smi::FromIntptr(sig_->return_count()));
    return CallBuiltin<IterableToFixedArrayForWasmDescriptor>(
        Builtin::kIterableToFixedArrayForWasm, Operator::kEliminatable,
        iterable, length, context);
  }

  void SafeStore(int offset, wasm::ValueType type, OpIndex base,
                 OpIndex value) {
    int alignment = offset % type.value_kind_size();
    auto rep = MemoryRepresentation::FromMachineRepresentation(
        type.machine_representation());
    if (COMPRESS_POINTERS_BOOL && rep.IsCompressibleTagged()) {
      // We are storing tagged value to off-heap location, so we need to store
      // it as a full word otherwise we will not be able to decompress it.
      rep = MemoryRepresentation::UintPtr();
      value = __ BitcastTaggedToWordPtr(value);
    }
    StoreOp::Kind store_kind =
        alignment == 0 || compiler::turboshaft::SupportedOperations::
                              IsUnalignedStoreSupported(rep)
            ? StoreOp::Kind::RawAligned()
            : StoreOp::Kind::RawUnaligned();
    __ Store(base, value, store_kind, rep, compiler::kNoWriteBarrier, offset);
  }

  V<WordPtr> BuildLoadCallTargetFromExportedFunctionData(
      V<WasmFunctionData> function_data) {
    V<WasmInternalFunction> internal =
        V<WasmInternalFunction>::Cast(__ LoadProtectedPointerField(
            function_data, LoadOp::Kind::TaggedBase().Immutable(),
            WasmFunctionData::kProtectedInternalOffset));
    return __ Load(internal, LoadOp::Kind::TaggedBase(),
                   MemoryRepresentation::UintPtr(),
                   WasmInternalFunction::kCallTargetOffset);
  }

  const OpIndex SafeLoad(OpIndex base, int offset, wasm::ValueType type) {
    int alignment = offset % type.value_kind_size();
    auto rep = MemoryRepresentation::FromMachineRepresentation(
        type.machine_representation());
    if (COMPRESS_POINTERS_BOOL && rep.IsCompressibleTagged()) {
      // We are loading tagged value from off-heap location, so we need to load
      // it as a full word otherwise we will not be able to decompress it.
      rep = MemoryRepresentation::UintPtr();
    }
    LoadOp::Kind load_kind = alignment == 0 ||
                                     compiler::turboshaft::SupportedOperations::
                                         IsUnalignedLoadSupported(rep)
                                 ? LoadOp::Kind::RawAligned()
                                 : LoadOp::Kind::RawUnaligned();
    return __ Load(base, load_kind, rep, offset);
  }

 private:
  const WasmModule* module_;
  const wasm::FunctionSig* const sig_;
};

void BuildWasmWrapper(compiler::turboshaft::PipelineData* data,
                      AccountingAllocator* allocator,
                      compiler::turboshaft::Graph& graph,
                      const wasm::FunctionSig* sig,
                      WrapperCompilationInfo wrapper_info,
                      const WasmModule* module) {
  Zone zone(allocator, ZONE_NAME);
  WasmGraphBuilderBase::Assembler assembler(data, graph, graph, &zone);
  WasmWrapperTSGraphBuilder builder(&zone, assembler, module, sig,
                                    wrapper_info.stub_mode);
  if (wrapper_info.code_kind == CodeKind::JS_TO_WASM_FUNCTION) {
    builder.BuildJSToWasmWrapper();
  } else if (wrapper_info.code_kind == CodeKind::WASM_TO_JS_FUNCTION) {
    builder.BuildWasmToJSWrapper(wrapper_info.import_kind,
                                 wrapper_info.expected_arity,
                                 wrapper_info.suspend, module);
  } else if (wrapper_info.code_kind == CodeKind::WASM_TO_CAPI_FUNCTION) {
    builder.BuildCapiCallWrapper(module);
  } else {
    // TODO(thibaudm): Port remaining wrappers.
    UNREACHABLE();
  }
}

}  // namespace v8::internal::wasm
