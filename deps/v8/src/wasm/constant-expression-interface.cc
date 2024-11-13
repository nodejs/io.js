// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/constant-expression-interface.h"

#include "src/base/overflowing-math.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/oddball.h"
#include "src/wasm/decoder.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {
namespace wasm {

void ConstantExpressionInterface::I32Const(FullDecoder* decoder, Value* result,
                                           int32_t value) {
  if (generate_value()) result->runtime_value = WasmValue(value);
}

void ConstantExpressionInterface::I64Const(FullDecoder* decoder, Value* result,
                                           int64_t value) {
  if (generate_value()) result->runtime_value = WasmValue(value);
}

void ConstantExpressionInterface::F32Const(FullDecoder* decoder, Value* result,
                                           float value) {
  if (generate_value()) result->runtime_value = WasmValue(value);
}

void ConstantExpressionInterface::F64Const(FullDecoder* decoder, Value* result,
                                           double value) {
  if (generate_value()) result->runtime_value = WasmValue(value);
}

void ConstantExpressionInterface::S128Const(FullDecoder* decoder,
                                            const Simd128Immediate& imm,
                                            Value* result) {
  if (!generate_value()) return;
  result->runtime_value = WasmValue(imm.value, kWasmS128);
}

void ConstantExpressionInterface::UnOp(FullDecoder* decoder, WasmOpcode opcode,
                                       const Value& input, Value* result) {
  if (!generate_value()) return;
  switch (opcode) {
    case kExprExternConvertAny: {
      result->runtime_value = WasmValue(
          WasmToJSObject(isolate_, input.runtime_value.to_ref()),
          ValueType::RefMaybeNull(HeapType::kExtern, input.type.nullability()));
      break;
    }
    case kExprAnyConvertExtern: {
      const char* error_message = nullptr;
      result->runtime_value = WasmValue(
          JSToWasmObject(isolate_, input.runtime_value.to_ref(), kWasmAnyRef, 0,
                         &error_message)
              .ToHandleChecked(),
          ValueType::RefMaybeNull(HeapType::kAny, input.type.nullability()));
      break;
    }
    default:
      UNREACHABLE();
  }
}

void ConstantExpressionInterface::BinOp(FullDecoder* decoder, WasmOpcode opcode,
                                        const Value& lhs, const Value& rhs,
                                        Value* result) {
  if (!generate_value()) return;
  switch (opcode) {
    case kExprI32Add:
      result->runtime_value = WasmValue(base::AddWithWraparound(
          lhs.runtime_value.to_i32(), rhs.runtime_value.to_i32()));
      break;
    case kExprI32Sub:
      result->runtime_value = WasmValue(base::SubWithWraparound(
          lhs.runtime_value.to_i32(), rhs.runtime_value.to_i32()));
      break;
    case kExprI32Mul:
      result->runtime_value = WasmValue(base::MulWithWraparound(
          lhs.runtime_value.to_i32(), rhs.runtime_value.to_i32()));
      break;
    case kExprI64Add:
      result->runtime_value = WasmValue(base::AddWithWraparound(
          lhs.runtime_value.to_i64(), rhs.runtime_value.to_i64()));
      break;
    case kExprI64Sub:
      result->runtime_value = WasmValue(base::SubWithWraparound(
          lhs.runtime_value.to_i64(), rhs.runtime_value.to_i64()));
      break;
    case kExprI64Mul:
      result->runtime_value = WasmValue(base::MulWithWraparound(
          lhs.runtime_value.to_i64(), rhs.runtime_value.to_i64()));
      break;
    default:
      UNREACHABLE();
  }
}

void ConstantExpressionInterface::RefNull(FullDecoder* decoder, ValueType type,
                                          Value* result) {
  if (!generate_value()) return;
  result->runtime_value =
      WasmValue((IsSubtypeOf(type, kWasmExternRef, decoder->module_) ||
                 IsSubtypeOf(type, kWasmExnRef, decoder->module_))
                    ? Cast<Object>(isolate_->factory()->null_value())
                    : Cast<Object>(isolate_->factory()->wasm_null()),
                type);
}

void ConstantExpressionInterface::RefFunc(FullDecoder* decoder,
                                          uint32_t function_index,
                                          Value* result) {
  if (isolate_ == nullptr) {
    outer_module_->functions[function_index].declared = true;
    return;
  }
  if (!generate_value()) return;
  uint32_t sig_index = module_->functions[function_index].sig_index;
  bool function_is_shared = module_->types[sig_index].is_shared;
  ValueType type = ValueType::Ref(module_->functions[function_index].sig_index);
  Handle<WasmFuncRef> func_ref = WasmTrustedInstanceData::GetOrCreateFuncRef(
      isolate_,
      function_is_shared ? shared_trusted_instance_data_
                         : trusted_instance_data_,
      function_index);
  result->runtime_value = WasmValue(func_ref, type);
}

void ConstantExpressionInterface::GlobalGet(FullDecoder* decoder, Value* result,
                                            const GlobalIndexImmediate& imm) {
  if (!generate_value()) return;
  const WasmGlobal& global = module_->globals[imm.index];
  DCHECK(!global.mutability);
  DirectHandle<WasmTrustedInstanceData> data =
      global.shared ? shared_trusted_instance_data_ : trusted_instance_data_;
  result->runtime_value =
      global.type.is_numeric()
          ? WasmValue(reinterpret_cast<uint8_t*>(
                          data->untagged_globals_buffer()->backing_store()) +
                          global.offset,
                      global.type)
          : WasmValue(handle(data->tagged_globals_buffer()->get(global.offset),
                             isolate_),
                      global.type);
}

void ConstantExpressionInterface::StructNew(FullDecoder* decoder,
                                            const StructIndexImmediate& imm,
                                            const Value args[], Value* result) {
  if (!generate_value()) return;
  DirectHandle<WasmTrustedInstanceData> data =
      GetTrustedInstanceDataForTypeIndex(imm.index);
  DirectHandle<Map> rtt{Cast<Map>(data->managed_object_maps()->get(imm.index)),
                        isolate_};
  WasmValue* field_values =
      decoder->zone_->AllocateArray<WasmValue>(imm.struct_type->field_count());
  for (size_t i = 0; i < imm.struct_type->field_count(); i++) {
    field_values[i] = args[i].runtime_value;
  }
  result->runtime_value = WasmValue(
      isolate_->factory()->NewWasmStruct(imm.struct_type, field_values, rtt),
      ValueType::Ref(HeapType(imm.index)));
}

void ConstantExpressionInterface::StringConst(FullDecoder* decoder,
                                              const StringConstImmediate& imm,
                                              Value* result) {
  if (!generate_value()) return;
  static_assert(base::IsInRange(kV8MaxWasmStringLiterals, 0, Smi::kMaxValue));

  DCHECK_LT(imm.index, module_->stringref_literals.size());

  const wasm::WasmStringRefLiteral& literal =
      module_->stringref_literals[imm.index];
  const base::Vector<const uint8_t> module_bytes =
      trusted_instance_data_->native_module()->wire_bytes();
  const base::Vector<const uint8_t> string_bytes = module_bytes.SubVector(
      literal.source.offset(), literal.source.end_offset());
  Handle<String> string =
      isolate_->factory()
          ->NewStringFromUtf8(string_bytes, unibrow::Utf8Variant::kWtf8)
          .ToHandleChecked();
  result->runtime_value = WasmValue(string, kWasmStringRef.AsNonNull());
}

namespace {
WasmValue DefaultValueForType(ValueType type, Isolate* isolate) {
  switch (type.kind()) {
    case kI32:
    case kI8:
    case kI16:
      return WasmValue(0);
    case kI64:
      return WasmValue(int64_t{0});
    case kF16:
    case kF32:
      return WasmValue(0.0f);
    case kF64:
      return WasmValue(0.0);
    case kS128:
      return WasmValue(Simd128());
    case kRefNull:
      return WasmValue(type == kWasmExternRef || type == kWasmNullExternRef ||
                               type == kWasmExnRef || type == kWasmNullExnRef
                           ? Cast<Object>(isolate->factory()->null_value())
                           : Cast<Object>(isolate->factory()->wasm_null()),
                       type);
    case kVoid:
    case kRtt:
    case kRef:
    case kBottom:
      UNREACHABLE();
  }
}
}  // namespace

void ConstantExpressionInterface::StructNewDefault(
    FullDecoder* decoder, const StructIndexImmediate& imm, Value* result) {
  if (!generate_value()) return;
  DirectHandle<WasmTrustedInstanceData> data =
      GetTrustedInstanceDataForTypeIndex(imm.index);
  DirectHandle<Map> rtt{Cast<Map>(data->managed_object_maps()->get(imm.index)),
                        isolate_};
  WasmValue* field_values =
      decoder->zone_->AllocateArray<WasmValue>(imm.struct_type->field_count());
  for (uint32_t i = 0; i < imm.struct_type->field_count(); i++) {
    field_values[i] = DefaultValueForType(imm.struct_type->field(i), isolate_);
  }
  result->runtime_value = WasmValue(
      isolate_->factory()->NewWasmStruct(imm.struct_type, field_values, rtt),
      ValueType::Ref(imm.index));
}

void ConstantExpressionInterface::ArrayNew(FullDecoder* decoder,
                                           const ArrayIndexImmediate& imm,
                                           const Value& length,
                                           const Value& initial_value,
                                           Value* result) {
  if (!generate_value()) return;
  DirectHandle<WasmTrustedInstanceData> data =
      GetTrustedInstanceDataForTypeIndex(imm.index);
  DirectHandle<Map> rtt{Cast<Map>(data->managed_object_maps()->get(imm.index)),
                        isolate_};
  if (length.runtime_value.to_u32() >
      static_cast<uint32_t>(WasmArray::MaxLength(imm.array_type))) {
    error_ = MessageTemplate::kWasmTrapArrayTooLarge;
    return;
  }
  result->runtime_value = WasmValue(
      isolate_->factory()->NewWasmArray(imm.array_type->element_type(),
                                        length.runtime_value.to_u32(),
                                        initial_value.runtime_value, rtt),
      ValueType::Ref(imm.index));
}

void ConstantExpressionInterface::ArrayNewDefault(
    FullDecoder* decoder, const ArrayIndexImmediate& imm, const Value& length,
    Value* result) {
  if (!generate_value()) return;
  Value initial_value(decoder->pc(), imm.array_type->element_type());
  initial_value.runtime_value =
      DefaultValueForType(imm.array_type->element_type(), isolate_);
  return ArrayNew(decoder, imm, length, initial_value, result);
}

void ConstantExpressionInterface::ArrayNewFixed(
    FullDecoder* decoder, const ArrayIndexImmediate& array_imm,
    const IndexImmediate& length_imm, const Value elements[], Value* result) {
  if (!generate_value()) return;
  DirectHandle<WasmTrustedInstanceData> data =
      GetTrustedInstanceDataForTypeIndex(array_imm.index);
  DirectHandle<Map> rtt{
      Cast<Map>(data->managed_object_maps()->get(array_imm.index)), isolate_};
  base::Vector<WasmValue> element_values =
      decoder->zone_->AllocateVector<WasmValue>(length_imm.index);
  for (size_t i = 0; i < length_imm.index; i++) {
    element_values[i] = elements[i].runtime_value;
  }
  result->runtime_value =
      WasmValue(isolate_->factory()->NewWasmArrayFromElements(
                    array_imm.array_type, element_values, rtt),
                ValueType::Ref(HeapType(array_imm.index)));
}

// TODO(14034): These expressions are non-constant for now. There are plans to
// make them constant in the future, so we retain the required infrastructure
// here.
void ConstantExpressionInterface::ArrayNewSegment(
    FullDecoder* decoder, const ArrayIndexImmediate& array_imm,
    const IndexImmediate& segment_imm, const Value& offset_value,
    const Value& length_value, Value* result) {
  if (!generate_value()) return;

  DirectHandle<WasmTrustedInstanceData> data =
      GetTrustedInstanceDataForTypeIndex(array_imm.index);

  DirectHandle<Map> rtt{
      Cast<Map>(data->managed_object_maps()->get(array_imm.index)), isolate_};

  uint32_t length = length_value.runtime_value.to_u32();
  uint32_t offset = offset_value.runtime_value.to_u32();
  if (length >
      static_cast<uint32_t>(WasmArray::MaxLength(array_imm.array_type))) {
    error_ = MessageTemplate::kWasmTrapArrayTooLarge;
    return;
  }
  ValueType element_type = array_imm.array_type->element_type();
  ValueType result_type = ValueType::Ref(HeapType(array_imm.index));
  if (element_type.is_numeric()) {
    const WasmDataSegment& data_segment =
        module_->data_segments[segment_imm.index];
    uint32_t length_in_bytes =
        length * array_imm.array_type->element_type().value_kind_size();

    if (!base::IsInBounds<uint32_t>(offset, length_in_bytes,
                                    data_segment.source.length())) {
      error_ = MessageTemplate::kWasmTrapDataSegmentOutOfBounds;
      return;
    }

    Address source =
        data->data_segment_starts()->get(segment_imm.index) + offset;
    Handle<WasmArray> array_value =
        isolate_->factory()->NewWasmArrayFromMemory(length, rtt, source);
    result->runtime_value = WasmValue(array_value, result_type);
  } else {
    const wasm::WasmElemSegment* elem_segment =
        &decoder->module_->elem_segments[segment_imm.index];
    // A constant expression should not observe if a passive segment is dropped.
    // However, it should consider active and declarative segments as empty.
    if (!base::IsInBounds<size_t>(
            offset, length,
            elem_segment->status == WasmElemSegment::kStatusPassive
                ? elem_segment->element_count
                : 0)) {
      error_ = MessageTemplate::kWasmTrapElementSegmentOutOfBounds;
      return;
    }

    Handle<Object> array_object =
        isolate_->factory()->NewWasmArrayFromElementSegment(
            trusted_instance_data_, shared_trusted_instance_data_,
            segment_imm.index, offset, length, rtt);
    if (IsSmi(*array_object)) {
      // A smi result stands for an error code.
      error_ = static_cast<MessageTemplate>(Cast<Smi>(*array_object).value());
    } else {
      result->runtime_value = WasmValue(array_object, result_type);
    }
  }
}

void ConstantExpressionInterface::RefI31(FullDecoder* decoder,
                                         const Value& input, Value* result) {
  if (!generate_value()) return;
  Address raw = input.runtime_value.to_i32();
  // We have to craft the Smi manually because we accept out-of-bounds inputs.
  // For 32-bit Smi builds, set the topmost bit to sign-extend the second bit.
  // This way, interpretation in JS (if this value escapes there) will be the
  // same as i31.get_s.
  static_assert((SmiValuesAre31Bits() ^ SmiValuesAre32Bits()) == 1);
  intptr_t shifted;
  if constexpr (SmiValuesAre31Bits()) {
    shifted = raw << (kSmiTagSize + kSmiShiftSize);
  } else {
    shifted =
        static_cast<intptr_t>(raw << (kSmiTagSize + kSmiShiftSize + 1)) >> 1;
  }
  result->runtime_value = WasmValue(handle(Tagged<Smi>(shifted), isolate_),
                                    wasm::kWasmI31Ref.AsNonNull());
}

void ConstantExpressionInterface::DoReturn(FullDecoder* decoder,
                                           uint32_t /*drop_values*/) {
  end_found_ = true;
  // End decoding on "end". Note: We need this because we do not know the length
  // of a constant expression while decoding it.
  decoder->set_end(decoder->pc() + 1);
  if (generate_value()) {
    computed_value_ = decoder->stack_value(1)->runtime_value;
  }
}

Handle<WasmTrustedInstanceData>
ConstantExpressionInterface::GetTrustedInstanceDataForTypeIndex(
    uint32_t index) {
  bool type_is_shared = module_->types[index].is_shared;
  return type_is_shared ? shared_trusted_instance_data_
                        : trusted_instance_data_;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
