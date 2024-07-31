// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/bailout-reason.h"
#include "src/compiler/linkage.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/wasm-assembler-helpers.h"
#include "src/objects/object-list-macros.h"
#include "src/wasm/turboshaft-graph-interface.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module.h"
#include "src/zone/zone.h"

namespace v8::internal::wasm {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using compiler::CallDescriptor;
using compiler::Operator;
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
using compiler::turboshaft::V;
using compiler::turboshaft::Variable;
using compiler::turboshaft::Word32;
using compiler::turboshaft::WordPtr;

namespace {
const TSCallDescriptor* GetBuiltinCallDescriptor(Builtin name, Zone* zone,
                                                 StubCallMode stub_mode) {
  CallInterfaceDescriptor interface_descriptor =
      Builtins::CallInterfaceDescriptorFor(name);
  CallDescriptor* call_desc = compiler::Linkage::GetStubCallDescriptor(
      zone,                                           // zone
      interface_descriptor,                           // descriptor
      interface_descriptor.GetStackParameterCount(),  // stack parameter count
      CallDescriptor::kNoFlags,                       // flags
      compiler::Operator::kNoProperties,              // properties
      stub_mode);                                     // stub call mode
  return TSCallDescriptor::Create(call_desc, compiler::CanThrow::kNo, zone);
}
}  // namespace

class WasmWrapperTSGraphBuilder : public WasmGraphBuilderBase {
 public:
  WasmWrapperTSGraphBuilder(Zone* zone, Assembler& assembler,
                            const WasmModule* module,
                            const wasm::FunctionSig* sig,
                            StubCallMode stub_mode)
      : WasmGraphBuilderBase(zone, assembler),
        module_(module),
        sig_(sig),
        stub_mode_(stub_mode) {}

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
          asm_.Load(asm_.LoadRootRegister(), OptionalOpIndex::Invalid(),
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
    return WasmGraphBuilderBase::GetTargetForBuiltinCall(builtin, stub_mode_);
  }

  template <typename Descriptor, typename... Args>
  OpIndex CallBuiltin(Builtin name, OpIndex frame_state,
                      Operator::Properties properties, Args... args) {
    auto call_descriptor = compiler::Linkage::GetStubCallDescriptor(
        __ graph_zone(), Descriptor(), 0,
        frame_state.valid() ? CallDescriptor::kNeedsFrameState
                            : CallDescriptor::kNoFlags,
        Operator::kNoProperties, stub_mode_);
    const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
        call_descriptor, compiler::CanThrow::kNo, __ graph_zone());
    V<WordPtr> call_target = GetTargetForBuiltinCall(name);
    return __ Call(call_target, frame_state, base::VectorOf({args...}),
                   ts_call_descriptor);
  }

  template <typename Descriptor, typename... Args>
  OpIndex CallBuiltin(Builtin name, Operator::Properties properties,
                      Args... args) {
    auto call_descriptor = compiler::Linkage::GetStubCallDescriptor(
        __ graph_zone(), Descriptor(), 0, CallDescriptor::kNoFlags,
        Operator::kNoProperties, stub_mode_);
    const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
        call_descriptor, compiler::CanThrow::kNo, __ graph_zone());
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
    V<Word32> add = __ Int32AddCheckOverflow(value, value);
    V<Word32> ovf = __ Projection<Word32>(add, 1);
    Variable result = __ NewVariable(RegisterRepresentation::Tagged());
    IF_NOT (ovf) {
      // If it didn't overflow, the result is {2 * value} as pointer-sized
      // value.
      __ SetVariable(result,
                     __ ChangeInt32ToIntPtr(__ Projection<Word32>(add, 0)));
    } ELSE{
      // Otherwise, call builtin, to convert to a HeapNumber.
      V<HeapNumber> call = CallBuiltin<WasmInt32ToHeapNumberDescriptor>(
          Builtin::kWasmInt32ToHeapNumber, Operator::kNoProperties, value);
      __ SetVariable(result, call);
    }
    OpIndex merge = __ GetVariable(result);
    __ SetVariable(result, OpIndex::Invalid());
    return merge;
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
        return BuildChangeInt64ToBigInt(ret, stub_mode_);
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
              Variable maybe_external =
                  __ NewVariable(RegisterRepresentation::Tagged());
              V<WasmInternalFunction> internal =
                  __ Load(ret, LoadOp::Kind::TaggedBase(),
                          MemoryRepresentation::TaggedPointer(),
                          WasmFuncRef::kInternalOffset);
              __ SetVariable(maybe_external,
                             __ Load(internal, LoadOp::Kind::TaggedBase(),
                                     MemoryRepresentation::TaggedPointer(),
                                     WasmInternalFunction::kExternalOffset));
              IF (__ TaggedEqual(__ GetVariable(maybe_external),
                                 LOAD_ROOT(UndefinedValue))) {
                __ SetVariable(
                    maybe_external,
                    CallBuiltin<WasmInternalFunctionCreateExternalDescriptor>(
                        Builtin::kWasmInternalFunctionCreateExternal,
                        Operator::kNoProperties, internal, context));
              }
              OpIndex merge = __ GetVariable(maybe_external);
              __ SetVariable(maybe_external, OpIndex::Invalid());
              return merge;
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
            Variable result = __ NewVariable(RegisterRepresentation::Tagged());
            IF_NOT (__ TaggedEqual(ret, LOAD_ROOT(WasmNull))) {
              __ SetVariable(result, ret);
            } ELSE{
              __ SetVariable(result, LOAD_ROOT(NullValue));
            }
            OpIndex merge = __ GetVariable(result);
            __ SetVariable(result, OpIndex::Invalid());
            return merge;
          }
          case wasm::HeapType::kFunc:
          default: {
            if (type.heap_representation_non_shared() ==
                    wasm::HeapType::kFunc ||
                module_->has_signature(type.ref_index())) {
              Variable result =
                  __ NewVariable(RegisterRepresentation::Tagged());
              IF (__ TaggedEqual(ret, LOAD_ROOT(WasmNull))) {
                __ SetVariable(result, LOAD_ROOT(NullValue));
              } ELSE{
                V<WasmInternalFunction> internal =
                    __ Load(ret, LoadOp::Kind::TaggedBase(),
                            MemoryRepresentation::TaggedPointer(),
                            WasmFuncRef::kInternalOffset);
                V<Object> maybe_external =
                    __ Load(internal, LoadOp::Kind::TaggedBase(),
                            MemoryRepresentation::AnyTagged(),
                            WasmInternalFunction::kExternalOffset);
                IF (__ TaggedEqual(maybe_external, LOAD_ROOT(UndefinedValue))) {
                  V<Object> from_builtin =
                      CallBuiltin<WasmInternalFunctionCreateExternalDescriptor>(
                          Builtin::kWasmInternalFunctionCreateExternal,
                          Operator::kNoProperties, internal, context);
                  __ SetVariable(result, from_builtin);
                } ELSE{
                  __ SetVariable(result, maybe_external);
                }
              }
              OpIndex merge = __ GetVariable(result);
              __ SetVariable(result, OpIndex::Invalid());
              return merge;
            } else {
              Variable result =
                  __ NewVariable(RegisterRepresentation::Tagged());
              IF (__ TaggedEqual(ret, LOAD_ROOT(WasmNull))) {
                __ SetVariable(result, LOAD_ROOT(NullValue));
              } ELSE{
                __ SetVariable(result, ret);
              }
              OpIndex merge = __ GetVariable(result);
              __ SetVariable(result, OpIndex::Invalid());
              return merge;
            }
          }
        }
      case wasm::kRtt:
      case wasm::kI8:
      case wasm::kI16:
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
        compiler::CanThrow::kYes, __ graph_zone());

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

  OpIndex BuildCallAndReturn(bool is_import, V<Context> js_context,
                             V<Object> function_data,
                             base::SmallVector<OpIndex, 16> args,
                             bool do_conversion, bool set_in_wasm_flag) {
    const int rets_count = static_cast<int>(sig_->return_count());
    base::SmallVector<OpIndex, 1> rets(rets_count);

    // Set the ThreadInWasm flag before we do the actual call.
    {
      base::Optional<ModifyThreadInWasmFlagScope>
          modify_thread_in_wasm_flag_builder;
      if (set_in_wasm_flag) {
        modify_thread_in_wasm_flag_builder.emplace(this, Asm());
      }

      V<HeapObject> instance_object =
          __ Load(function_data, LoadOp::Kind::TaggedBase(),
                  MemoryRepresentation::TaggedPointer(),
                  WasmExportedFunctionData::kInstanceOffset);
      V<WasmTrustedInstanceData> instance_data =
          LoadTrustedDataFromInstanceObject(instance_object);

      if (is_import) {
        // Call to an imported function.
        // Load function index from {WasmExportedFunctionData}.
        V<Word32> function_index = BuildChangeSmiToInt32(
            LoadExportedFunctionIndexAsSmi(function_data));
        auto [target, ref] =
            BuildImportedFunctionTargetAndRef(function_index, instance_data);
        BuildCallWasmFromWrapper(__ phase_zone(), sig_, target, ref, args,
                                 rets);
      } else {
        // Call to a wasm function defined in this module.
        // The (cached) call target is the jump table slot for that function.
        V<Object> internal = __ Load(function_data, LoadOp::Kind::TaggedBase(),
                                     MemoryRepresentation::TaggedPointer(),
                                     WasmFunctionData::kInternalOffset);
#ifdef V8_ENABLE_SANDBOX
        V<Word32> target_handle =
            __ Load(internal, LoadOp::Kind::TaggedBase(),
                    MemoryRepresentation::Uint32(),
                    WasmInternalFunction::kCallTargetOffset);
        V<WordPtr> callee = __ DecodeExternalPointer(
            target_handle, kWasmInternalFunctionCallTargetTag);
#else
        V<WordPtr> callee = __ Load(internal, LoadOp::Kind::TaggedBase(),
                                    MemoryRepresentation::UintPtr(),
                                    WasmInternalFunction::kCallTargetOffset);
#endif
        BuildCallWasmFromWrapper(__ phase_zone(), sig_, callee, instance_data,
                                 args, rets);
      }
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
      bool is_import, bool do_conversion = true,
      compiler::turboshaft::OptionalOpIndex frame_state =
          compiler::turboshaft::OptionalOpIndex::Invalid(),
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
    V<Object> function_data = __ Load(shared, LoadOp::Kind::TaggedBase(),
                                      MemoryRepresentation::TaggedPointer(),
                                      SharedFunctionInfo::kFunctionDataOffset);

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
      jsval = BuildCallAndReturn(is_import, js_context, function_data, args,
                                 do_conversion, set_in_wasm_flag);
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

    jsval = BuildCallAndReturn(is_import, js_context, function_data, args,
                               do_conversion, set_in_wasm_flag);
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
    int wasm_count = static_cast<int>(sig_->parameter_count()) - suspend;

    __ Bind(__ NewBlock());
    base::SmallVector<OpIndex, 16> wasm_params(wasm_count);
    OpIndex ref = __ Parameter(0, RegisterRepresentation::Tagged());
    V<HeapObject> suspender =
        suspend ? __ Parameter(1, RegisterRepresentation::Tagged())
                : OpIndex::Invalid();
    for (int i = 0; i < wasm_count; ++i) {
      RegisterRepresentation rep =
          RepresentationFor(sig_->GetParam(i + suspend));
      wasm_params[i] = (__ Parameter(1 + suspend + i, rep));
    }

    V<Context> native_context = __ Load(
        ref, LoadOp::Kind::TaggedBase(), MemoryRepresentation::TaggedPointer(),
        WasmApiFunctionRef::kNativeContextOffset);

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
    // If {suspend} is true, {wasm_count} includes the suspender argument, which
    // is dropped in {AddArgumentNodes}.
    pos = AddArgumentNodes(base::VectorOf(args), pos, wasm_params, sig_,
                           native_context, suspend);
    for (int i = wasm_count; i < expected_arity; ++i) {
      args[pos++] = undefined_node;
    }

    V<JSFunction> callable_node = __ Load(ref, LoadOp::Kind::TaggedBase(),
                                          MemoryRepresentation::TaggedPointer(),
                                          WasmApiFunctionRef::kCallableOffset);
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
            call_descriptor, compiler::CanThrow::kYes, __ graph_zone());

        // Determine receiver at runtime.
        args[0] =
            BuildReceiverNode(callable_node, native_context, undefined_node);
        DCHECK_EQ(pos, pushed_count + 1);
        args[pos++] = undefined_node;  // new target
        args[pos++] =
            __ Word32Constant(JSParameterCount(wasm_count));  // argument count
        args[pos++] = LoadContextFromJSFunction(callable_node);
        call = BuildCallOnCentralStack(callable_node, args, ts_call_descriptor,
                                       callable_node);
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
            call_descriptor, compiler::CanThrow::kYes, __ graph_zone());

        // The native_context is sufficient here, because all kind of callables
        // which depend on the context provide their own context. The context
        // here is only needed if the target is a constructor to throw a
        // TypeError, if the target is a native function, or if the target is a
        // callable JSObject, which can only be constructed by the runtime.
        args[pos++] = native_context;
        call = BuildCallOnCentralStack(target, args, ts_call_descriptor,
                                       callable_node);
        break;
      }
      default:
        UNIMPLEMENTED();
    }
    DCHECK(call.valid());
    if (suspend) {
      call = BuildSuspend(call, suspender, ref);
    }

    // Convert the return value(s) back.
    if (sig_->return_count() <= 1) {
      OpIndex val =
          sig_->return_count() == 0
              ? __ Word32Constant(0)
              : FromJS(call, native_context, sig_->GetReturn(), module);
      BuildModifyThreadInWasmFlag(__ phase_zone(), true);
      __ Return(val);
    } else {
      V<FixedArray> fixed_array =
          BuildMultiReturnFixedArrayFromIterable(call, native_context);
      base::SmallVector<OpIndex, 8> wasm_values(sig_->return_count());
      for (unsigned i = 0; i < sig_->return_count(); ++i) {
        wasm_values[i] = FromJS(__ LoadFixedArrayElement(fixed_array, i),
                                native_context, sig_->GetReturn(i), module);
      }
      BuildModifyThreadInWasmFlag(__ phase_zone(), true);
      __ Return(__ Word32Constant(0), base::VectorOf(wasm_values));
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
        Variable result = __ NewVariable(RegisterRepresentation::Float32());
        IF (__ IsSmi(input)) {
          __ SetVariable(result, __ ChangeInt32ToFloat32(__ UntagSmi(input)));
        } ELSE {
          __ SetVariable(
              result, __ TruncateFloat64ToFloat32(HeapNumberToFloat64(input)));
        }
        OpIndex merge = __ GetVariable(result);
        __ SetVariable(result, OpIndex::Invalid());
        return merge;
      }
      case wasm::kF64: {
        Variable result = __ NewVariable(RegisterRepresentation::Float64());
        IF (__ IsSmi(input)) {
          __ SetVariable(result, __ ChangeInt32ToFloat64(__ UntagSmi(input)));
        } ELSE{
          __ SetVariable(result, HeapNumberToFloat64(input));
        }
        OpIndex merge = __ GetVariable(result);
        __ SetVariable(result, OpIndex::Invalid());
        return merge;
      }
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kI64:
      case wasm::kRtt:
      case wasm::kS128:
      case wasm::kI8:
      case wasm::kI16:
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
    Variable result = __ NewVariable(RegisterRepresentation::Tagged());
    __ SetVariable(result, LOAD_ROOT(WasmNull));
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
    __ SetVariable(result, input);
    __ GotoIf(check, done, BranchHint::kTrue);
    __ Goto(type_error);
    __ Bind(type_error);
    CallRuntime(__ phase_zone(), Runtime::kWasmThrowJSTypeError, {},
                js_context);
    __ Unreachable();
    __ Bind(done);
    OpIndex merge = __ GetVariable(result);
    __ SetVariable(result, OpIndex::Invalid());
    return merge;
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
    Variable result = __ NewVariable(RegisterRepresentation::Word32());
    IF (__ IsSmi(value)) {
      __ SetVariable(result, BuildChangeSmiToInt32(value));
    } ELSE{
      OpIndex call =
          frame_state.valid()
              ? CallBuiltin<WasmTaggedNonSmiToInt32Descriptor>(
                    Builtin::kWasmTaggedNonSmiToInt32, frame_state.value(),
                    Operator::kNoProperties, value, context)
              : CallBuiltin<WasmTaggedNonSmiToInt32Descriptor>(
                    Builtin::kWasmTaggedNonSmiToInt32, Operator::kNoProperties,
                    value, context);
      __ SetVariable(result, call);
      // The source position here is needed for asm.js, see the comment on the
      // source position of the call to JavaScript in the wasm-to-js wrapper.
      __ output_graph().source_positions()[call] = SourcePosition(1);
    }
    OpIndex merge = __ GetVariable(result);
    __ SetVariable(result, OpIndex::Invalid());
    return merge;
  }

  CallDescriptor* GetBigIntToI64CallDescriptor(bool needs_frame_state) {
    return wasm::GetWasmEngine()->call_descriptors()->GetBigIntToI64Descriptor(
        stub_mode_, needs_frame_state);
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
        call_descriptor, compiler::CanThrow::kNo, __ graph_zone());
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
            if (type.kind() == wasm::kRef) {
              IF (__ TaggedEqual(input, LOAD_ROOT(NullValue))) {
                CallRuntime(__ phase_zone(), Runtime::kWasmThrowJSTypeError, {},
                            context);
                __ Unreachable();
              }
            }
            return input;
          case wasm::HeapType::kString:
            return BuildCheckString(input, context, type);
          case wasm::HeapType::kExn:
          case wasm::HeapType::kNoExn:
            return input;
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

            if (type.has_index()) {
              DCHECK_NOT_NULL(module);
              uint32_t canonical_index =
                  module->isorecursive_canonical_type_ids[type.ref_index()];
              type = wasm::ValueType::RefMaybeNull(canonical_index,
                                                   type.nullability());
            }

            std::initializer_list<const OpIndex> inputs = {
                input, __ IntPtrConstant(
                           IntToSmi(static_cast<int>(type.raw_bit_field())))};
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
        __ GotoIfNot(__ IsSmi(input), slow_path);
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
        __ GotoIf(is_heap_number, done);
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
      case wasm::kBottom:
      case wasm::kVoid:
        UNREACHABLE();
    }
  }

  // Must be called in the first block to emit the Parameter ops.
  int AddArgumentNodes(base::Vector<OpIndex> args, int pos,
                       base::SmallVector<OpIndex, 16> wasm_params,
                       const wasm::FunctionSig* sig, V<Context> context,
                       wasm::Suspend suspend) {
    // Convert wasm numbers to JS values.
    for (size_t i = 0; i < wasm_params.size(); ++i) {
      args[pos++] = ToJS(wasm_params[i], sig->GetParam(i + suspend), context);
    }
    return pos;
  }

  OpIndex LoadSharedFunctionInfo(V<JSFunction> js_function) {
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
    Variable strict_d = __ NewVariable(RegisterRepresentation::Tagged());
    IF (strict_check) {
      __ SetVariable(strict_d, undefined_node);
    } ELSE {
      __ SetVariable(strict_d,
                     __ LoadFixedArrayElement(native_context,
                                              Context::GLOBAL_PROXY_INDEX));
    }
    OpIndex result = __ GetVariable(strict_d);
    __ SetVariable(strict_d, OpIndex::Invalid());
    return result;
  }

  V<Context> LoadContextFromJSFunction(V<JSFunction> js_function) {
    return __ Load(js_function, LoadOp::Kind::TaggedBase(),
                   MemoryRepresentation::TaggedPointer(),
                   JSFunction::kContextOffset);
  }

  OpIndex BuildSwitchToTheCentralStack(OpIndex callable_node) {
    V<WordPtr> stack_limit_slot = __ WordPtrAdd(
        __ FramePointer(),
        __ UintPtrConstant(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset));

    MachineType reps[] = {MachineType::Pointer(), MachineType::Pointer(),
                          MachineType::Pointer()};
    MachineSignature sig(1, 2, reps);

    OpIndex central_stack_sp = CallC(
        &sig, ExternalReference::wasm_switch_to_the_central_stack_for_js(),
        {__ BitcastHeapObjectToWordPtr(callable_node), stack_limit_slot});
    OpIndex old_sp = __ LoadStackPointer();
    // Temporarily disallow sp-relative offsets.
    __ SetStackPointer(central_stack_sp, wasm::kEnterFPRelativeOnlyScope);
    __ Store(__ FramePointer(), central_stack_sp, StoreOp::Kind::RawAligned(),
             MemoryRepresentation::UintPtr(), compiler::kNoWriteBarrier,
             WasmImportWrapperFrameConstants::kCentralStackSPOffset);
    return old_sp;
  }

  void BuildSwitchBackFromCentralStack(OpIndex old_sp, OpIndex receiver) {
    OpIndex stack_limit = __ WordPtrAdd(
        __ FramePointer(),
        __ UintPtrConstant(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset));

    MachineType reps[] = {MachineType::Pointer(), MachineType::Pointer()};
    MachineSignature sig(0, 2, reps);
    CallC(&sig, ExternalReference::wasm_switch_from_the_central_stack_for_js(),
          {__ BitcastHeapObjectToWordPtr(receiver), stack_limit});
    __ Store(__ FramePointer(), __ IntPtrConstant(0),
             StoreOp::Kind::RawAligned(), MemoryRepresentation::UintPtr(),
             compiler::kNoWriteBarrier,
             WasmImportWrapperFrameConstants::kCentralStackSPOffset);
    __ SetStackPointer(old_sp, wasm::kLeaveFPRelativeOnlyScope);
  }

  OpIndex BuildCallOnCentralStack(OpIndex target,
                                  base::SmallVector<OpIndex, 16> args,
                                  const TSCallDescriptor* call_descriptor,
                                  OpIndex receiver) {
    // If the current stack is a secondary stack, switch, perform the call and
    // switch back. Otherwise, just do the call.
    // Return the Phi of the calls in the two branches.
    OpIndex isolate_root = __ LoadRootRegister();
    OpIndex is_on_central_stack_flag = __ Load(
        isolate_root, LoadOp::Kind::RawAligned(), MemoryRepresentation::Uint8(),
        IsolateData::is_on_central_stack_flag_offset());
    Variable result_var = __ NewVariable(RegisterRepresentation::Tagged());
    IF (is_on_central_stack_flag) {
      OpIndex call = __ Call(target, OpIndex::Invalid(), base::VectorOf(args),
                             call_descriptor);
      // For asm.js the error location can differ depending on whether an
      // exception was thrown in imported JS code or an exception was thrown in
      // the ToNumber builtin that converts the result of the JS code a
      // WebAssembly value. The source position allows asm.js to determine the
      // correct error location. Source position 1 encodes the call to ToNumber,
      // source position 0 encodes the call to the imported JS code.
      __ output_graph().source_positions()[call] = SourcePosition(0);
      __ SetVariable(result_var, call);
    } ELSE {
      OpIndex old_sp = BuildSwitchToTheCentralStack(receiver);
      // See comment above.
      OpIndex call = __ Call(target, OpIndex::Invalid(), base::VectorOf(args),
                             call_descriptor);
      __ output_graph().source_positions()[call] = SourcePosition(0);
      __ SetVariable(result_var, call);
      BuildSwitchBackFromCentralStack(old_sp, receiver);
    }
    OpIndex result = __ GetVariable(result_var);
    __ SetVariable(result_var, OpIndex::Invalid());
    return result;
  }

  OpIndex BuildSuspend(OpIndex value, V<Object> suspender,
                       V<Object> api_function_ref) {
    V<Context> native_context =
        __ Load(api_function_ref, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::TaggedPointer(),
                WasmApiFunctionRef::kNativeContextOffset);
    OpIndex active_suspender = LOAD_ROOT(ActiveSuspender);

    // If value is a promise, suspend to the js-to-wasm prompt, and resume later
    // with the promise's resolved value.
    Variable result = __ NewVariable(RegisterRepresentation::Tagged());
    __ SetVariable(result, value);
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
        // Trap if there is any JS frame on the stack.
        OpIndex has_js_frames =
            __ UntagSmi(__ Load(suspender, LoadOp::Kind::TaggedBase(),
                                MemoryRepresentation::TaggedSigned(),
                                WasmSuspenderObject::kHasJsFramesOffset));
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
            Builtin::kPerformPromiseThen, __ graph_zone(),
            StubCallMode::kCallBuiltinPointer);
        base::SmallVector<OpIndex, 16> args{value, on_fulfilled, on_rejected,
                                            LOAD_ROOT(UndefinedValue),
                                            native_context};
        BuildCallOnCentralStack(promise_then, args, then_call_desc, suspender);

        OpIndex suspend = GetTargetForBuiltinCall(Builtin::kWasmSuspend);
        auto* suspend_call_descriptor = GetBuiltinCallDescriptor(
            Builtin::kWasmSuspend, __ graph_zone(), stub_mode_);
        OpIndex resolved =
            __ Call(suspend, {suspender}, suspend_call_descriptor);
        __ SetVariable(result, resolved);
      }
    }
    OpIndex merge = __ GetVariable(result);
    __ SetVariable(result, OpIndex::Invalid());
    return merge;
  }

  V<FixedArray> BuildMultiReturnFixedArrayFromIterable(OpIndex iterable,
                                                       V<Context> context) {
    V<Smi> length = __ SmiConstant(Smi::FromIntptr(sig_->return_count()));
    return CallBuiltin<IterableToFixedArrayForWasmDescriptor>(
        Builtin::kIterableToFixedArrayForWasm, Operator::kEliminatable,
        iterable, length, context);
  }

 private:
  const WasmModule* module_;
  const wasm::FunctionSig* const sig_;
  StubCallMode stub_mode_;
};

void BuildWasmWrapper(AccountingAllocator* allocator,
                      compiler::turboshaft::Graph& graph,
                      const wasm::FunctionSig* sig,
                      WrapperCompilationInfo wrapper_info,
                      const WasmModule* module) {
  Zone zone(allocator, ZONE_NAME);
  WasmGraphBuilderBase::Assembler assembler(graph, graph, &zone);
  WasmWrapperTSGraphBuilder builder(&zone, assembler, module, sig,
                                    StubCallMode::kCallBuiltinPointer);
  if (wrapper_info.code_kind == CodeKind::JS_TO_WASM_FUNCTION) {
    builder.BuildJSToWasmWrapper(wrapper_info.is_import);
  } else if (wrapper_info.code_kind == CodeKind::WASM_TO_JS_FUNCTION) {
    builder.BuildWasmToJSWrapper(wrapper_info.import_info.import_kind,
                                 wrapper_info.import_info.expected_arity,
                                 wrapper_info.import_info.suspend, module);
  } else {
    // TODO(thibaudm): Port remaining wrappers.
    UNREACHABLE();
  }
}

}  // namespace v8::internal::wasm
