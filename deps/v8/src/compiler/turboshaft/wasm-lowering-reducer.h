// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/builtin-call-descriptors.h"
#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_LOWERING_REDUCER_H_

#include "src/compiler/globals.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/wasm-assembler-helpers.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class WasmLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmLowering)

  V<Any> REDUCE(GlobalGet)(V<WasmTrustedInstanceData> instance,
                           const wasm::WasmGlobal* global) {
    return LowerGlobalSetOrGet(instance, OpIndex::Invalid(), global,
                               GlobalMode::kLoad);
  }

  OpIndex REDUCE(GlobalSet)(V<WasmTrustedInstanceData> instance, V<Any> value,
                            const wasm::WasmGlobal* global) {
    return LowerGlobalSetOrGet(instance, value, global, GlobalMode::kStore);
  }

  OpIndex REDUCE(Null)(wasm::ValueType type) { return Null(type); }

  V<Word32> REDUCE(IsNull)(OpIndex object, wasm::ValueType type) {
#if V8_STATIC_ROOTS_BOOL
    // TODO(14616): Extend this for shared types.
    const bool is_wasm_null =
        !wasm::IsSubtypeOf(type, wasm::kWasmExternRef, module_) &&
        !wasm::IsSubtypeOf(type, wasm::kWasmExnRef, module_);
    V<Object> null_value = V<Object>::Cast(
        __ UintPtrConstant(is_wasm_null ? StaticReadOnlyRoot::kWasmNull
                                        : StaticReadOnlyRoot::kNullValue));
#else
    OpIndex null_value = Null(type);
#endif
    return __ TaggedEqual(object, null_value);
  }

  V<Object> REDUCE(AssertNotNull)(V<Object> object, wasm::ValueType type,
                                  TrapId trap_id) {
    if (trap_id == TrapId::kTrapNullDereference) {
      // Skip the check altogether if null checks are turned off.
      if (!v8_flags.experimental_wasm_skip_null_checks) {
        // Use an explicit null check if
        // (1) we cannot use trap handler or
        // (2) the object might be a Smi or
        // (3) the object might be a JS object.
        if (null_check_strategy_ == NullCheckStrategy::kExplicit ||
            wasm::IsSubtypeOf(wasm::kWasmI31Ref.AsNonNull(), type, module_) ||
            wasm::IsSubtypeOf(type, wasm::kWasmExternRef, module_) ||
            wasm::IsSubtypeOf(type, wasm::kWasmExnRef, module_)) {
          __ TrapIf(__ IsNull(object, type), trap_id);
        } else {
          // Otherwise, load the word after the map word.
          static_assert(WasmStruct::kHeaderSize > kTaggedSize);
          static_assert(WasmArray::kHeaderSize > kTaggedSize);
          static_assert(WasmInternalFunction::kHeaderSize > kTaggedSize);
          __ Load(object, LoadOp::Kind::TrapOnNull().Immutable(),
                  MemoryRepresentation::Int32(), kTaggedSize);
        }
      }
    } else {
      __ TrapIf(__ IsNull(object, type), trap_id);
    }
    return object;
  }

  V<Map> REDUCE(RttCanon)(V<FixedArray> rtts, uint32_t type_index) {
    int map_offset = FixedArray::kHeaderSize + type_index * kTaggedSize;
    return __ Load(rtts, LoadOp::Kind::TaggedBase().Immutable(),
                   MemoryRepresentation::AnyTagged(), map_offset);
  }

  V<Word32> REDUCE(WasmTypeCheck)(V<Object> object, OptionalV<Map> rtt,
                                  WasmTypeCheckConfig config) {
    if (rtt.has_value()) {
      return ReduceWasmTypeCheckRtt(object, rtt, config);
    } else {
      return ReduceWasmTypeCheckAbstract(object, config);
    }
  }

  V<Object> REDUCE(WasmTypeCast)(V<Object> object, OptionalV<Map> rtt,
                                 WasmTypeCheckConfig config) {
    if (rtt.has_value()) {
      return ReduceWasmTypeCastRtt(object, rtt, config);
    } else {
      return ReduceWasmTypeCastAbstract(object, config);
    }
  }

  V<Object> REDUCE(AnyConvertExtern)(V<Object> object) {
    Label<Object> end_label(&Asm());
    Label<> null_label(&Asm());
    Label<> smi_label(&Asm());
    Label<> int_to_smi_label(&Asm());
    Label<> heap_number_label(&Asm());

    constexpr int32_t kInt31MaxValue = 0x3fffffff;
    constexpr int32_t kInt31MinValue = -kInt31MaxValue - 1;

    GOTO_IF(__ IsNull(object, wasm::kWasmExternRef), null_label);
    GOTO_IF(__ IsSmi(object), smi_label);
    GOTO_IF(__ HasInstanceType(object, HEAP_NUMBER_TYPE), heap_number_label);
    // For anything else, just pass through the value.
    GOTO(end_label, object);

    BIND(null_label);
    GOTO(end_label, Null(wasm::kWasmAnyRef));

    // Canonicalize SMI.
    BIND(smi_label);
    if constexpr (SmiValuesAre31Bits()) {
      GOTO(end_label, object);
    } else {
      Label<> convert_to_heap_number_label(&Asm());
      V<Word32> int_value = __ UntagSmi(V<Smi>::Cast(object));

      // Convert to heap number if the int32 does not fit into an i31ref.
      GOTO_IF(__ Int32LessThan(__ Word32Constant(kInt31MaxValue), int_value),
              convert_to_heap_number_label);
      GOTO_IF(__ Int32LessThan(int_value, __ Word32Constant(kInt31MinValue)),
              convert_to_heap_number_label);
      GOTO(end_label, object);

      BIND(convert_to_heap_number_label);
      V<Object> heap_number = __ template WasmCallBuiltinThroughJumptable<
          BuiltinCallDescriptor::WasmInt32ToHeapNumber>({int_value});
      GOTO(end_label, heap_number);
    }

    // Convert HeapNumber to SMI if possible.
    BIND(heap_number_label);
    V<Float64> float_value =
        __ LoadHeapNumberValue(V<HeapNumber>::Cast(object));
    // Check range of float value.
    GOTO_IF(__ Float64LessThan(float_value, __ Float64Constant(kInt31MinValue)),
            end_label, object);
    GOTO_IF(__ Float64LessThan(__ Float64Constant(kInt31MaxValue), float_value),
            end_label, object);
    // Check if value is -0.
    V<Word32> is_minus_zero;
    if constexpr (Is64()) {
      V<Word64> minus_zero = __ Word64Constant(kMinusZeroBits);
      V<Word64> float_bits = __ BitcastFloat64ToWord64(float_value);
      is_minus_zero = __ Word64Equal(float_bits, minus_zero);
    } else {
      Label<Word32> done(&Asm());

      V<Word32> value_lo = __ Float64ExtractLowWord32(float_value);
      GOTO_IF_NOT(__ Word32Equal(value_lo, __ Word32Constant(kMinusZeroLoBits)),
                  done, __ Word32Constant(0));
      V<Word32> value_hi = __ Float64ExtractHighWord32(float_value);
      GOTO(done, __ Word32Equal(value_hi, __ Word32Constant(kMinusZeroHiBits)));
      BIND(done, phi_is_minus_zero);
      is_minus_zero = phi_is_minus_zero;
    }
    GOTO_IF(is_minus_zero, end_label, object);
    // Check if value is integral.
    V<Word32> int_value =
        __ TruncateFloat64ToInt32OverflowUndefined(float_value);
    GOTO_IF(__ Float64Equal(float_value, __ ChangeInt32ToFloat64(int_value)),
            int_to_smi_label);
    GOTO(end_label, object);

    BIND(int_to_smi_label);
    GOTO(end_label, __ TagSmi(int_value));

    BIND(end_label, result);
    return result;
  }

  V<Object> REDUCE(ExternConvertAny)(V<Object> object) {
    Label<Object> end(&Asm());
    GOTO_IF_NOT(__ IsNull(object, wasm::kWasmAnyRef), end, object);
    GOTO(end, Null(wasm::kWasmExternRef));
    BIND(end, result);
    return result;
  }

  V<Object> REDUCE(WasmTypeAnnotation)(V<Object> value, wasm::ValueType type) {
    // Remove type annotation operations as they are not needed any more.
    return value;
  }

  V<Any> REDUCE(StructGet)(V<WasmStructNullable> object,
                           const wasm::StructType* type, uint32_t type_index,
                           int field_index, bool is_signed,
                           CheckForNull null_check) {
    auto [explicit_null_check, implicit_null_check] =
        null_checks_for_struct_op(null_check, field_index);

    if (explicit_null_check) {
      __ TrapIf(__ IsNull(object, wasm::kWasmAnyRef),
                TrapId::kTrapNullDereference);
    }

    LoadOp::Kind load_kind = implicit_null_check ? LoadOp::Kind::TrapOnNull()
                                                 : LoadOp::Kind::TaggedBase();
    if (!type->mutability(field_index)) {
      load_kind = load_kind.Immutable();
    }
    MemoryRepresentation repr =
        RepresentationFor(type->field(field_index), is_signed);

    return __ Load(object, load_kind, repr, field_offset(type, field_index));
  }

  V<None> REDUCE(StructSet)(V<WasmStructNullable> object, V<Any> value,
                            const wasm::StructType* type, uint32_t type_index,
                            int field_index, CheckForNull null_check) {
    auto [explicit_null_check, implicit_null_check] =
        null_checks_for_struct_op(null_check, field_index);

    if (explicit_null_check) {
      __ TrapIf(__ IsNull(object, wasm::kWasmAnyRef),
                TrapId::kTrapNullDereference);
    }

    StoreOp::Kind store_kind = implicit_null_check
                                   ? StoreOp::Kind::TrapOnNull()
                                   : StoreOp::Kind::TaggedBase();
    MemoryRepresentation repr =
        RepresentationFor(type->field(field_index), true);

    __ Store(object, value, store_kind, repr,
             type->field(field_index).is_reference() ? kFullWriteBarrier
                                                     : kNoWriteBarrier,
             field_offset(type, field_index));

    return OpIndex::Invalid();
  }

  V<Any> REDUCE(ArrayGet)(V<WasmArrayNullable> array, V<Word32> index,
                          const wasm::ArrayType* array_type, bool is_signed) {
    bool is_mutable = array_type->mutability();
    LoadOp::Kind load_kind = is_mutable
                                 ? LoadOp::Kind::TaggedBase()
                                 : LoadOp::Kind::TaggedBase().Immutable();
    return __ Load(array, __ ChangeInt32ToIntPtr(index), load_kind,
                   RepresentationFor(array_type->element_type(), is_signed),
                   WasmArray::kHeaderSize,
                   array_type->element_type().value_kind_size_log2());
  }

  V<None> REDUCE(ArraySet)(V<WasmArrayNullable> array, V<Word32> index,
                           V<Any> value, wasm::ValueType element_type) {
    __ Store(array, __ ChangeInt32ToIntPtr(index), value,
             LoadOp::Kind::TaggedBase(), RepresentationFor(element_type, true),
             element_type.is_reference() ? kFullWriteBarrier : kNoWriteBarrier,
             WasmArray::kHeaderSize, element_type.value_kind_size_log2());
    return {};
  }

  V<Word32> REDUCE(ArrayLength)(V<WasmArrayNullable> array,
                                CheckForNull null_check) {
    bool explicit_null_check =
        null_check == kWithNullCheck &&
        null_check_strategy_ == NullCheckStrategy::kExplicit;
    bool implicit_null_check =
        null_check == kWithNullCheck &&
        null_check_strategy_ == NullCheckStrategy::kTrapHandler;

    if (explicit_null_check) {
      __ TrapIf(__ IsNull(array, wasm::kWasmAnyRef),
                TrapId::kTrapNullDereference);
    }

    LoadOp::Kind load_kind = implicit_null_check
                                 ? LoadOp::Kind::TrapOnNull().Immutable()
                                 : LoadOp::Kind::TaggedBase().Immutable();

    return __ Load(array, load_kind, RepresentationFor(wasm::kWasmI32, true),
                   WasmArray::kLengthOffset);
  }

  V<WasmArray> REDUCE(WasmAllocateArray)(V<Map> rtt, V<Word32> length,
                                         const wasm::ArrayType* array_type) {
    __ TrapIfNot(
        __ Uint32LessThanOrEqual(
            length, __ Word32Constant(WasmArray::MaxLength(array_type))),
        TrapId::kTrapArrayTooLarge);
    wasm::ValueType element_type = array_type->element_type();

    // RoundUp(length * value_size, kObjectAlignment) =
    //   RoundDown(length * value_size + kObjectAlignment - 1,
    //             kObjectAlignment);
    V<Word32> padded_length = __ Word32BitwiseAnd(
        __ Word32Add(__ Word32Mul(length, __ Word32Constant(
                                              element_type.value_kind_size())),
                     __ Word32Constant(int32_t{kObjectAlignment - 1})),
        __ Word32Constant(int32_t{-kObjectAlignment}));
    Uninitialized<WasmArray> a = __ template Allocate<WasmArray>(
        __ ChangeUint32ToUintPtr(__ Word32Add(
            padded_length, __ Word32Constant(WasmArray::kHeaderSize))),
        AllocationType::kYoung);

    // TODO(14108): The map and empty fixed array initialization should be an
    // immutable store.
    __ InitializeField(a, AccessBuilder::ForMap(compiler::kNoWriteBarrier),
                       rtt);
    __ InitializeField(a, AccessBuilder::ForJSObjectPropertiesOrHash(),
                       LOAD_ROOT(EmptyFixedArray));
    __ InitializeField(a, AccessBuilder::ForWasmArrayLength(), length);

    // Note: Only the array header initialization is finished here, the elements
    // still need to be initialized by other code.
    V<WasmArray> array = __ FinishInitialization(std::move(a));
    return array;
  }

  V<WasmStruct> REDUCE(WasmAllocateStruct)(
      V<Map> rtt, const wasm::StructType* struct_type) {
    int size = WasmStruct::Size(struct_type);
    Uninitialized<WasmStruct> s =
        __ template Allocate<WasmStruct>(size, AllocationType::kYoung);
    __ InitializeField(s, AccessBuilder::ForMap(compiler::kNoWriteBarrier),
                       rtt);
    __ InitializeField(s, AccessBuilder::ForJSObjectPropertiesOrHash(),
                       LOAD_ROOT(EmptyFixedArray));
    // Note: Struct initialization isn't finished here, the user defined fields
    // still need to be initialized by other operations.
    V<WasmStruct> struct_value = __ FinishInitialization(std::move(s));
    return struct_value;
  }

  V<WasmFuncRef> REDUCE(WasmRefFunc)(V<WasmTrustedInstanceData> wasm_instance,
                                     uint32_t function_index) {
    V<FixedArray> func_refs = LOAD_IMMUTABLE_INSTANCE_FIELD(
        wasm_instance, FuncRefs, MemoryRepresentation::TaggedPointer());
    V<Object> maybe_func_ref =
        __ LoadFixedArrayElement(func_refs, function_index);

    Label<WasmFuncRef> done(&Asm());
    IF (UNLIKELY(__ IsSmi(maybe_func_ref))) {
      bool extract_shared_data =
          !shared_ && module_->function_is_shared(function_index);

      V<WasmFuncRef> from_builtin = __ template WasmCallBuiltinThroughJumptable<
          BuiltinCallDescriptor::WasmRefFunc>(
          {__ Word32Constant(function_index),
           __ Word32Constant(extract_shared_data ? 1 : 0)});

      GOTO(done, from_builtin);
    } ELSE {
      GOTO(done, V<WasmFuncRef>::Cast(maybe_func_ref));
    }

    BIND(done, result_value);
    return result_value;
  }

  V<String> REDUCE(StringAsWtf16)(V<String> string) {
    Label<String> done(&Asm());
    V<Word32> instance_type = __ LoadInstanceTypeField(__ LoadMapField(string));
    V<Word32> string_representation = __ Word32BitwiseAnd(
        instance_type, __ Word32Constant(kStringRepresentationMask));
    GOTO_IF(__ Word32Equal(string_representation, kSeqStringTag), done, string);

    GOTO(done, __ template WasmCallBuiltinThroughJumptable<
                   BuiltinCallDescriptor::WasmStringAsWtf16>({string}));
    BIND(done, result);
    return result;
  }

  OpIndex REDUCE(StringPrepareForGetCodeUnit)(V<Object> original_string) {
    LoopLabel<Object /*string*/, Word32 /*instance type*/, Word32 /*offset*/>
        dispatch(&Asm());
    Label<Object /*string*/, Word32 /*instance type*/, Word32 /*offset*/>
        direct_string(&Asm());

    // These values will be used to replace the original node's projections.
    // The first, "string", is either a SeqString or Tagged<Smi>(0) (in case of
    // external string). Notably this makes it GC-safe: if that string moves,
    // this pointer will be updated accordingly. The second, "offset", has full
    // register width so that it can be used to store external pointers: for
    // external strings, we add up the character backing store's base address
    // and any slice offset. The third, "character width", is a shift width,
    // i.e. it is 0 for one-byte strings, 1 for two-byte strings,
    // kCharWidthBailoutSentinel for uncached external strings (for which
    // "string"/"offset" are invalid and unusable).
    Label<Object /*string*/, WordPtr /*offset*/, Word32 /*character width*/>
        done(&Asm());

    V<Word32> original_type =
        __ LoadInstanceTypeField(__ LoadMapField(original_string));
    GOTO(dispatch, original_string, original_type, __ Word32Constant(0));

    BIND_LOOP(dispatch, string, instance_type, offset) {
      Label<> thin_string(&Asm());
      Label<> cons_string(&Asm());

      static_assert(kIsIndirectStringTag == 1);
      static constexpr int kIsDirectStringTag = 0;
      GOTO_IF(__ Word32Equal(
                  __ Word32BitwiseAnd(instance_type, kIsIndirectStringMask),
                  kIsDirectStringTag),
              direct_string, string, instance_type, offset);

      // Handle indirect strings.
      V<Word32> string_representation =
          __ Word32BitwiseAnd(instance_type, kStringRepresentationMask);
      GOTO_IF(__ Word32Equal(string_representation, kThinStringTag),
              thin_string);
      GOTO_IF(__ Word32Equal(string_representation, kConsStringTag),
              cons_string);

      // Sliced string.
      V<Word32> new_offset = __ Word32Add(
          offset, __ UntagSmi(__ template LoadField<Smi>(
                      string, AccessBuilder::ForSlicedStringOffset())));
      V<Object> parent = __ template LoadField<Object>(
          string, AccessBuilder::ForSlicedStringParent());
      V<Word32> parent_type = __ LoadInstanceTypeField(__ LoadMapField(parent));
      GOTO(dispatch, parent, parent_type, new_offset);

      // Thin string.
      BIND(thin_string);
      V<Object> actual = __ template LoadField<Object>(
          string, AccessBuilder::ForThinStringActual());
      V<Word32> actual_type = __ LoadInstanceTypeField(__ LoadMapField(actual));
      // ThinStrings always reference (internalized) direct strings.
      GOTO(direct_string, actual, actual_type, offset);

      // Flat cons string. (Non-flat cons strings are ruled out by
      // string.as_wtf16.)
      BIND(cons_string);
      V<Object> first = __ template LoadField<Object>(
          string, AccessBuilder::ForConsStringFirst());
      V<Word32> first_type = __ LoadInstanceTypeField(__ LoadMapField(first));
      GOTO(dispatch, first, first_type, offset);
    }
    {
      BIND(direct_string, string, instance_type, offset);

      V<Word32> is_onebyte =
          __ Word32BitwiseAnd(instance_type, kStringEncodingMask);
      // Char width shift is 1 - (is_onebyte).
      static_assert(kStringEncodingMask == 1 << 3);
      V<Word32> charwidth_shift =
          __ Word32Sub(1, __ Word32ShiftRightLogical(is_onebyte, 3));

      Label<> external(&Asm());
      V<Word32> string_representation =
          __ Word32BitwiseAnd(instance_type, kStringRepresentationMask);
      GOTO_IF(__ Word32Equal(string_representation, kExternalStringTag),
              external);

      // Sequential string.
      DCHECK_EQ(AccessBuilder::ForSeqOneByteStringCharacter().header_size,
                AccessBuilder::ForSeqTwoByteStringCharacter().header_size);
      const int chars_start_offset =
          AccessBuilder::ForSeqOneByteStringCharacter().header_size;
      V<Word32> final_offset =
          __ Word32Add(chars_start_offset - kHeapObjectTag,
                       __ Word32ShiftLeft(offset, charwidth_shift));
      GOTO(done, string, __ ChangeInt32ToIntPtr(final_offset), charwidth_shift);

      // External string.
      BIND(external);
      GOTO_IF(__ Word32BitwiseAnd(instance_type, kUncachedExternalStringMask),
              done, string, /*offset*/ 0, kCharWidthBailoutSentinel);
      V<WordPtr> resource = BuildLoadExternalPointerFromObject(
          string, AccessBuilder::ForExternalStringResourceData());
      V<Word32> shifted_offset = __ Word32ShiftLeft(offset, charwidth_shift);
      V<WordPtr> final_offset_external =
          __ WordPtrAdd(resource, __ ChangeInt32ToIntPtr(shifted_offset));
      GOTO(done, __ SmiConstant(Smi::FromInt(0)), final_offset_external,
           charwidth_shift);
    }
    {
      BIND(done, base, final_offset, charwidth_shift);
      return __ Tuple({base, final_offset, charwidth_shift});
    }
  }

 private:
  enum class GlobalMode { kLoad, kStore };

  static constexpr MemoryRepresentation kMaybeSandboxedPointer =
      V8_ENABLE_SANDBOX_BOOL ? MemoryRepresentation::SandboxedPointer()
                             : MemoryRepresentation::UintPtr();

  MemoryRepresentation RepresentationFor(wasm::ValueType type, bool is_signed) {
    switch (type.kind()) {
      case wasm::kI8:
        return is_signed ? MemoryRepresentation::Int8()
                         : MemoryRepresentation::Uint8();
      case wasm::kI16:
        return is_signed ? MemoryRepresentation::Int16()
                         : MemoryRepresentation::Uint16();
      case wasm::kI32:
        return is_signed ? MemoryRepresentation::Int32()
                         : MemoryRepresentation::Uint32();
      case wasm::kI64:
        return is_signed ? MemoryRepresentation::Int64()
                         : MemoryRepresentation::Uint64();
      case wasm::kF16:
        return MemoryRepresentation::Float16();
      case wasm::kF32:
        return MemoryRepresentation::Float32();
      case wasm::kF64:
        return MemoryRepresentation::Float64();
      case wasm::kS128:
        return MemoryRepresentation::Simd128();
      case wasm::kRtt:
      case wasm::kRef:
      case wasm::kRefNull:
        return MemoryRepresentation::AnyTagged();
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }
  }

  V<WordPtr> BuildLoadExternalPointerFromObject(V<Object> object,
                                                FieldAccess access) {
#ifdef V8_ENABLE_SANDBOX
    DCHECK_NE(access.external_pointer_tag, kExternalPointerNullTag);
    V<Word32> handle = __ Load(object, LoadOp::Kind::TaggedBase(),
                               MemoryRepresentation::Uint32(), access.offset);
    return __ DecodeExternalPointer(handle, access.external_pointer_tag);
#else
    return __ Load(object, LoadOp::Kind::TaggedBase(),
                   MemoryRepresentation::UintPtr(), access.offset);
#endif  // V8_ENABLE_SANDBOX
  }

  V<Word32> ReduceWasmTypeCheckAbstract(V<Object> object,
                                        WasmTypeCheckConfig config) {
    const bool object_can_be_null = config.from.is_nullable();
    const bool null_succeeds = config.to.is_nullable();
    const bool object_can_be_i31 =
        wasm::IsSubtypeOf(wasm::kWasmI31Ref.AsNonNull(), config.from,
                          module_) ||
        config.from.heap_representation() == wasm::HeapType::kExtern;

    V<Word32> result;
    Label<Word32> end_label(&Asm());

    wasm::HeapType::Representation to_rep = config.to.heap_representation();
    do {
      // The none-types only perform a null check. They need no control flow.
      if (to_rep == wasm::HeapType::kNone ||
          to_rep == wasm::HeapType::kNoExtern ||
          to_rep == wasm::HeapType::kNoFunc ||
          to_rep == wasm::HeapType::kNoExn) {
        result = __ IsNull(object, config.from);
        break;
      }
      // Null checks performed by any other type check need control flow. We can
      // skip the null check if null fails, because it's covered by the Smi
      // check or instance type check we'll do later.
      if (object_can_be_null && null_succeeds) {
        const int kResult = 1;
        GOTO_IF(UNLIKELY(__ IsNull(object, wasm::kWasmAnyRef)), end_label,
                __ Word32Constant(kResult));
      }
      // i31 is special in that the Smi check is the last thing to do.
      if (to_rep == wasm::HeapType::kI31) {
        // If earlier optimization passes reached the limit of possible graph
        // transformations, we could DCHECK(object_can_be_i31) here.
        result = object_can_be_i31 ? __ IsSmi(object) : __ Word32Constant(0);
        break;
      }
      if (to_rep == wasm::HeapType::kEq) {
        if (object_can_be_i31) {
          GOTO_IF(UNLIKELY(__ IsSmi(object)), end_label, __ Word32Constant(1));
        }
        result = IsDataRefMap(__ LoadMapField(object));
        break;
      }
      // array, struct, string: i31 fails.
      if (object_can_be_i31) {
        GOTO_IF(UNLIKELY(__ IsSmi(object)), end_label, __ Word32Constant(0));
      }
      if (to_rep == wasm::HeapType::kArray) {
        result = __ HasInstanceType(object, WASM_ARRAY_TYPE);
        break;
      }
      if (to_rep == wasm::HeapType::kStruct) {
        result = __ HasInstanceType(object, WASM_STRUCT_TYPE);
        break;
      }
      if (to_rep == wasm::HeapType::kString ||
          to_rep == wasm::HeapType::kExternString) {
        V<Word32> instance_type =
            __ LoadInstanceTypeField(__ LoadMapField(object));
        result = __ Uint32LessThan(instance_type,
                                   __ Word32Constant(FIRST_NONSTRING_TYPE));
        break;
      }
      UNREACHABLE();
    } while (false);

    DCHECK(__ generating_unreachable_operations() || result.valid());
    GOTO(end_label, result);
    BIND(end_label, final_result);
    return final_result;
  }

  V<Object> ReduceWasmTypeCastAbstract(V<Object> object,
                                       WasmTypeCheckConfig config) {
    const bool object_can_be_null = config.from.is_nullable();
    const bool null_succeeds = config.to.is_nullable();
    const bool object_can_be_i31 =
        wasm::IsSubtypeOf(wasm::kWasmI31Ref.AsNonNull(), config.from,
                          module_) ||
        config.from.heap_representation() == wasm::HeapType::kExtern;

    Label<> end_label(&Asm());

    wasm::HeapType::Representation to_rep = config.to.heap_representation();

    do {
      // The none-types only perform a null check.
      if (to_rep == wasm::HeapType::kNone ||
          to_rep == wasm::HeapType::kNoExtern ||
          to_rep == wasm::HeapType::kNoFunc ||
          to_rep == wasm::HeapType::kNoExn) {
        __ TrapIfNot(__ IsNull(object, config.from), TrapId::kTrapIllegalCast);
        break;
      }
      // Null checks performed by any other type cast can be skipped if null
      // fails, because it's covered by the Smi check
      // or instance type check we'll do later.
      if (object_can_be_null && null_succeeds &&
          !v8_flags.experimental_wasm_skip_null_checks) {
        GOTO_IF(UNLIKELY(__ IsNull(object, config.from)), end_label);
      }
      if (to_rep == wasm::HeapType::kI31) {
        // If earlier optimization passes reached the limit of possible graph
        // transformations, we could DCHECK(object_can_be_i31) here.
        V<Word32> success =
            object_can_be_i31 ? __ IsSmi(object) : __ Word32Constant(0);
        __ TrapIfNot(success, TrapId::kTrapIllegalCast);
        break;
      }
      if (to_rep == wasm::HeapType::kEq) {
        if (object_can_be_i31) {
          GOTO_IF(UNLIKELY(__ IsSmi(object)), end_label);
        }
        __ TrapIfNot(IsDataRefMap(__ LoadMapField(object)),
                     TrapId::kTrapIllegalCast);
        break;
      }
      // array, struct, string: i31 fails.
      if (object_can_be_i31) {
        __ TrapIf(__ IsSmi(object), TrapId::kTrapIllegalCast);
      }
      if (to_rep == wasm::HeapType::kArray) {
        __ TrapIfNot(__ HasInstanceType(object, WASM_ARRAY_TYPE),
                     TrapId::kTrapIllegalCast);
        break;
      }
      if (to_rep == wasm::HeapType::kStruct) {
        __ TrapIfNot(__ HasInstanceType(object, WASM_STRUCT_TYPE),
                     TrapId::kTrapIllegalCast);
        break;
      }
      if (to_rep == wasm::HeapType::kString ||
          to_rep == wasm::HeapType::kExternString) {
        V<Word32> instance_type =
            __ LoadInstanceTypeField(__ LoadMapField(object));
        __ TrapIfNot(__ Uint32LessThan(instance_type,
                                       __ Word32Constant(FIRST_NONSTRING_TYPE)),
                     TrapId::kTrapIllegalCast);
        break;
      }
      UNREACHABLE();
    } while (false);

    GOTO(end_label);
    BIND(end_label);
    return object;
  }

  V<Object> ReduceWasmTypeCastRtt(V<Object> object, OptionalV<Map> rtt,
                                  WasmTypeCheckConfig config) {
    DCHECK(rtt.has_value());
    int rtt_depth = wasm::GetSubtypingDepth(module_, config.to.ref_index());
    bool object_can_be_null = config.from.is_nullable();
    bool object_can_be_i31 =
        wasm::IsSubtypeOf(wasm::kWasmI31Ref.AsNonNull(), config.from, module_);

    Label<> end_label(&Asm());
    bool is_cast_from_any = config.from.is_reference_to(wasm::HeapType::kAny);

    // If we are casting from any and null results in check failure, then the
    // {IsDataRefMap} check below subsumes the null check. Otherwise, perform
    // an explicit null check now.
    if (object_can_be_null && (!is_cast_from_any || config.to.is_nullable())) {
      V<Word32> is_null = __ IsNull(object, wasm::kWasmAnyRef);
      if (config.to.is_nullable()) {
        GOTO_IF(UNLIKELY(is_null), end_label);
      } else if (!v8_flags.experimental_wasm_skip_null_checks) {
        __ TrapIf(is_null, TrapId::kTrapIllegalCast);
      }
    }

    if (object_can_be_i31) {
      __ TrapIf(__ IsSmi(object), TrapId::kTrapIllegalCast);
    }

    V<Map> map = __ LoadMapField(object);

    if (module_->types[config.to.ref_index()].is_final) {
      __ TrapIfNot(__ TaggedEqual(map, rtt.value()), TrapId::kTrapIllegalCast);
      GOTO(end_label);
    } else {
      // First, check if types happen to be equal. This has been shown to give
      // large speedups.
      GOTO_IF(LIKELY(__ TaggedEqual(map, rtt.value())), end_label);

      // Check if map instance type identifies a wasm object.
      if (is_cast_from_any) {
        V<Word32> is_wasm_obj = IsDataRefMap(map);
        __ TrapIfNot(is_wasm_obj, TrapId::kTrapIllegalCast);
      }

      V<Object> type_info = LoadWasmTypeInfo(map);
      DCHECK_GE(rtt_depth, 0);
      // If the depth of the rtt is known to be less that the minimum supertype
      // array length, we can access the supertype without bounds-checking the
      // supertype array.
      if (static_cast<uint32_t>(rtt_depth) >=
          wasm::kMinimumSupertypeArraySize) {
        V<Word32> supertypes_length = __ UntagSmi(
            __ Load(type_info, LoadOp::Kind::TaggedBase().Immutable(),
                    MemoryRepresentation::TaggedSigned(),
                    WasmTypeInfo::kSupertypesLengthOffset));
        __ TrapIfNot(__ Uint32LessThan(rtt_depth, supertypes_length),
                     TrapId::kTrapIllegalCast);
      }

      V<Object> maybe_match =
          __ Load(type_info, LoadOp::Kind::TaggedBase().Immutable(),
                  MemoryRepresentation::TaggedPointer(),
                  WasmTypeInfo::kSupertypesOffset + kTaggedSize * rtt_depth);

      __ TrapIfNot(__ TaggedEqual(maybe_match, rtt.value()),
                   TrapId::kTrapIllegalCast);
      GOTO(end_label);
    }

    BIND(end_label);
    return object;
  }

  V<Word32> ReduceWasmTypeCheckRtt(V<Object> object, OptionalV<Map> rtt,
                                   WasmTypeCheckConfig config) {
    DCHECK(rtt.has_value());
    int rtt_depth = wasm::GetSubtypingDepth(module_, config.to.ref_index());
    bool object_can_be_null = config.from.is_nullable();
    bool object_can_be_i31 =
        wasm::IsSubtypeOf(wasm::kWasmI31Ref.AsNonNull(), config.from, module_);
    bool is_cast_from_any = config.from.is_reference_to(wasm::HeapType::kAny);

    Label<Word32> end_label(&Asm());

    // If we are casting from any and null results in check failure, then the
    // {IsDataRefMap} check below subsumes the null check. Otherwise, perform
    // an explicit null check now.
    if (object_can_be_null && (!is_cast_from_any || config.to.is_nullable())) {
      const int kResult = config.to.is_nullable() ? 1 : 0;
      GOTO_IF(UNLIKELY(__ IsNull(object, wasm::kWasmAnyRef)), end_label,
              __ Word32Constant(kResult));
    }

    if (object_can_be_i31) {
      GOTO_IF(__ IsSmi(object), end_label, __ Word32Constant(0));
    }

    V<Map> map = __ LoadMapField(object);

    if (module_->types[config.to.ref_index()].is_final) {
      GOTO(end_label, __ TaggedEqual(map, rtt.value()));
    } else {
      // First, check if types happen to be equal. This has been shown to give
      // large speedups.
      GOTO_IF(LIKELY(__ TaggedEqual(map, rtt.value())), end_label,
              __ Word32Constant(1));

      // Check if map instance type identifies a wasm object.
      if (is_cast_from_any) {
        V<Word32> is_wasm_obj = IsDataRefMap(map);
        GOTO_IF_NOT(LIKELY(is_wasm_obj), end_label, __ Word32Constant(0));
      }

      V<Object> type_info = LoadWasmTypeInfo(map);
      DCHECK_GE(rtt_depth, 0);
      // If the depth of the rtt is known to be less that the minimum supertype
      // array length, we can access the supertype without bounds-checking the
      // supertype array.
      if (static_cast<uint32_t>(rtt_depth) >=
          wasm::kMinimumSupertypeArraySize) {
        V<Word32> supertypes_length = __ UntagSmi(
            __ Load(type_info, LoadOp::Kind::TaggedBase().Immutable(),
                    MemoryRepresentation::TaggedSigned(),
                    WasmTypeInfo::kSupertypesLengthOffset));
        GOTO_IF_NOT(LIKELY(__ Uint32LessThan(rtt_depth, supertypes_length)),
                    end_label, __ Word32Constant(0));
      }

      V<Object> maybe_match =
          __ Load(type_info, LoadOp::Kind::TaggedBase().Immutable(),
                  MemoryRepresentation::TaggedPointer(),
                  WasmTypeInfo::kSupertypesOffset + kTaggedSize * rtt_depth);

      GOTO(end_label, __ TaggedEqual(maybe_match, rtt.value()));
    }

    BIND(end_label, result);
    return result;
  }

  OpIndex LowerGlobalSetOrGet(V<WasmTrustedInstanceData> instance, V<Any> value,
                              const wasm::WasmGlobal* global, GlobalMode mode) {
    bool is_mutable = global->mutability;
    DCHECK_IMPLIES(!is_mutable, mode == GlobalMode::kLoad);
    if (is_mutable && global->imported) {
      V<FixedAddressArray> imported_mutable_globals =
          LOAD_IMMUTABLE_INSTANCE_FIELD(instance, ImportedMutableGlobals,
                                        MemoryRepresentation::TaggedPointer());
      int field_offset =
          FixedAddressArray::kHeaderSize + global->index * kSystemPointerSize;
      if (global->type.is_reference()) {
        V<FixedArray> buffers = LOAD_IMMUTABLE_INSTANCE_FIELD(
            instance, ImportedMutableGlobalsBuffers,
            MemoryRepresentation::TaggedPointer());
        int offset_in_buffers =
            FixedArray::kHeaderSize + global->offset * kTaggedSize;
        V<HeapObject> base =
            __ Load(buffers, LoadOp::Kind::TaggedBase(),
                    MemoryRepresentation::AnyTagged(), offset_in_buffers);
        V<Word32> index = __ Load(imported_mutable_globals, OpIndex::Invalid(),
                                  LoadOp::Kind::TaggedBase(),
                                  MemoryRepresentation::Int32(), field_offset);
        V<WordPtr> index_ptr = __ ChangeInt32ToIntPtr(index);
        if (mode == GlobalMode::kLoad) {
          return __ Load(base, index_ptr, LoadOp::Kind::TaggedBase(),
                         MemoryRepresentation::AnyTagged(),
                         FixedArray::OffsetOfElementAt(0), kTaggedSizeLog2);
        } else {
          __ Store(base, index_ptr, value, StoreOp::Kind::TaggedBase(),
                   MemoryRepresentation::AnyTagged(),
                   WriteBarrierKind::kFullWriteBarrier,
                   FixedArray::OffsetOfElementAt(0), kTaggedSizeLog2);
          return OpIndex::Invalid();
        }
      } else {
        // Global is imported mutable but not a reference.
        OpIndex base = __ Load(imported_mutable_globals, OpIndex::Invalid(),
                               LoadOp::Kind::TaggedBase(),
                               kMaybeSandboxedPointer, field_offset);
        if (mode == GlobalMode::kLoad) {
          return __ Load(base, LoadOp::Kind::RawAligned(),
                         RepresentationFor(global->type, true), 0);
        } else {
          __ Store(base, value, StoreOp::Kind::RawAligned(),
                   RepresentationFor(global->type, true),
                   WriteBarrierKind::kNoWriteBarrier, 0);
          return OpIndex::Invalid();
        }
      }
    } else if (global->type.is_reference()) {
      V<HeapObject> base = LOAD_IMMUTABLE_INSTANCE_FIELD(
          instance, TaggedGlobalsBuffer, MemoryRepresentation::TaggedPointer());
      int offset = FixedArray::kHeaderSize + global->offset * kTaggedSize;
      if (mode == GlobalMode::kLoad) {
        LoadOp::Kind load_kind = is_mutable
                                     ? LoadOp::Kind::TaggedBase()
                                     : LoadOp::Kind::TaggedBase().Immutable();
        return __ Load(base, load_kind, MemoryRepresentation::AnyTagged(),
                       offset);
      } else {
        __ Store(base, value, StoreOp::Kind::TaggedBase(),
                 MemoryRepresentation::AnyTagged(),
                 WriteBarrierKind::kFullWriteBarrier, offset);
        return OpIndex::Invalid();
      }
    } else {
      OpIndex base = LOAD_IMMUTABLE_INSTANCE_FIELD(
          instance, GlobalsStart, MemoryRepresentation::UintPtr());
      if (mode == GlobalMode::kLoad) {
        LoadOp::Kind load_kind = is_mutable
                                     ? LoadOp::Kind::RawAligned()
                                     : LoadOp::Kind::RawAligned().Immutable();
        return __ Load(base, load_kind, RepresentationFor(global->type, true),
                       global->offset);
      } else {
        __ Store(base, value, StoreOp::Kind::RawAligned(),
                 RepresentationFor(global->type, true),
                 WriteBarrierKind::kNoWriteBarrier, global->offset);
        return OpIndex::Invalid();
      }
    }
  }

  OpIndex Null(wasm::ValueType type) {
    OpIndex roots = __ LoadRootRegister();
    RootIndex index =
        wasm::IsSubtypeOf(type, wasm::kWasmExternRef, module_) ||
                wasm::IsSubtypeOf(type, wasm::kWasmExnRef, module_)
            ? RootIndex::kNullValue
            : RootIndex::kWasmNull;
    // We load WasmNull as a pointer here and not as a TaggedPointer because
    // WasmNull is stored uncompressed in the IsolateData, and a load of a
    // TaggedPointer loads compressed pointers.
#if V8_TARGET_BIG_ENDIAN
    // On big endian a full pointer load is needed as otherwise the wrong half
    // of the 64 bit address is loaded.
    return __ BitcastWordPtrToTagged(__ Load(
        roots, LoadOp::Kind::RawAligned().Immutable(),
        MemoryRepresentation::UintPtr(), IsolateData::root_slot_offset(index)));
#else
    // On little endian a tagged load is enough and saves the bitcast.
    return __ Load(roots, LoadOp::Kind::RawAligned().Immutable(),
                   MemoryRepresentation::TaggedPointer(),
                   IsolateData::root_slot_offset(index));
#endif
  }

  V<Word32> IsDataRefMap(V<Map> map) {
    V<Word32> instance_type = __ LoadInstanceTypeField(map);
    // We're going to test a range of WasmObject instance types with a single
    // unsigned comparison.
    V<Word32> comparison_value =
        __ Word32Sub(instance_type, FIRST_WASM_OBJECT_TYPE);
    return __ Uint32LessThanOrEqual(
        comparison_value, LAST_WASM_OBJECT_TYPE - FIRST_WASM_OBJECT_TYPE);
  }

  V<Object> LoadWasmTypeInfo(V<Map> map) {
    int offset = Map::kConstructorOrBackPointerOrNativeContextOffset;
    return __ Load(map, LoadOp::Kind::TaggedBase().Immutable(),
                   MemoryRepresentation::TaggedPointer(), offset);
  }

  std::pair<bool, bool> null_checks_for_struct_op(CheckForNull null_check,
                                                  int field_index) {
    bool explicit_null_check =
        null_check == kWithNullCheck &&
        (null_check_strategy_ == NullCheckStrategy::kExplicit ||
         field_index > wasm::kMaxStructFieldIndexForImplicitNullCheck);
    bool implicit_null_check =
        null_check == kWithNullCheck && !explicit_null_check;
    return {explicit_null_check, implicit_null_check};
  }

  int field_offset(const wasm::StructType* type, int field_index) {
    return WasmStruct::kHeaderSize + type->field_offset(field_index);
  }

  const wasm::WasmModule* module_ = __ data() -> wasm_module();
  const bool shared_ = __ data() -> wasm_shared();
  const NullCheckStrategy null_check_strategy_ =
      trap_handler::IsTrapHandlerEnabled() && V8_STATIC_ROOTS_BOOL
          ? NullCheckStrategy::kTrapHandler
          : NullCheckStrategy::kExplicit;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_LOWERING_REDUCER_H_
