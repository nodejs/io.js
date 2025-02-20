// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/translated-state.h"

#include <inttypes.h>

#include <iomanip>
#include <optional>

#include "src/base/memory.h"
#include "src/common/assert-scope.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/deoptimizer/materialized-object-store.h"
#include "src/deoptimizer/translation-opcode.h"
#include "src/diagnostics/disasm.h"
#include "src/execution/frames.h"
#include "src/execution/isolate.h"
#include "src/heap/heap.h"
#include "src/numbers/conversions.h"
#include "src/objects/arguments.h"
#include "src/objects/deoptimization-data.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/oddball.h"

// Has to be the last include (doesn't have include guards)
#include "src/objects/object-macros.h"
#include "src/objects/string.h"

namespace v8 {

using base::Memory;
using base::ReadUnalignedValue;

namespace internal {

void DeoptimizationFrameTranslationPrintSingleOpcode(
    std::ostream& os, TranslationOpcode opcode,
    DeoptimizationFrameTranslation::Iterator& iterator,
    Tagged<ProtectedDeoptimizationLiteralArray> protected_literal_array,
    Tagged<DeoptimizationLiteralArray> literal_array) {
  disasm::NameConverter converter;
  switch (opcode) {
    case TranslationOpcode::BEGIN_WITH_FEEDBACK:
    case TranslationOpcode::BEGIN_WITHOUT_FEEDBACK:
    case TranslationOpcode::MATCH_PREVIOUS_TRANSLATION: {
      iterator.NextOperand();  // Skip the lookback distance.
      int frame_count = iterator.NextOperand();
      int jsframe_count = iterator.NextOperand();
      os << "{frame count=" << frame_count
         << ", js frame count=" << jsframe_count << "}";
      break;
    }

    case TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN:
    case TranslationOpcode::INTERPRETED_FRAME_WITHOUT_RETURN: {
      int bytecode_offset = iterator.NextOperand();
      int shared_info_id = iterator.NextOperand();
      int bytecode_array_id = iterator.NextOperand();
      unsigned height = iterator.NextOperand();
      int return_value_offset = 0;
      int return_value_count = 0;
      if (opcode == TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN) {
        DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 5);
        return_value_offset = iterator.NextOperand();
        return_value_count = iterator.NextOperand();
      } else {
        DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
      }
      Tagged<Object> shared_info = literal_array->get(shared_info_id);
      Tagged<Object> bytecode_array =
          protected_literal_array->get(bytecode_array_id);
      os << "{bytecode_offset=" << bytecode_offset << ", function="
         << Cast<SharedFunctionInfo>(shared_info)->DebugNameCStr().get()
         << ", bytecode=" << Brief(bytecode_array) << ", height=" << height
         << ", retval=@" << return_value_offset << "(#" << return_value_count
         << ")}";
      break;
    }

#if V8_ENABLE_WEBASSEMBLY
    case TranslationOpcode::WASM_INLINED_INTO_JS_FRAME: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
      int bailout_id = iterator.NextOperand();
      int shared_info_id = iterator.NextOperand();
      Tagged<Object> shared_info = literal_array->get(shared_info_id);
      unsigned height = iterator.NextOperand();
      os << "{bailout_id=" << bailout_id << ", function="
         << Cast<SharedFunctionInfo>(shared_info)->DebugNameCStr().get()
         << ", height=" << height << "}";
      break;
    }
#endif
    case TranslationOpcode::CONSTRUCT_CREATE_STUB_FRAME: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 2);
      int shared_info_id = iterator.NextOperand();
      Tagged<Object> shared_info = literal_array->get(shared_info_id);
      unsigned height = iterator.NextOperand();
      os << "{construct create stub, function="
         << Cast<SharedFunctionInfo>(shared_info)->DebugNameCStr().get()
         << ", height=" << height << "}";
      break;
    }

    case TranslationOpcode::CONSTRUCT_INVOKE_STUB_FRAME: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int shared_info_id = iterator.NextOperand();
      Tagged<Object> shared_info = literal_array->get(shared_info_id);
      os << "{construct invoke stub, function="
         << Cast<SharedFunctionInfo>(shared_info)->DebugNameCStr().get() << "}";
      break;
    }

    case TranslationOpcode::BUILTIN_CONTINUATION_FRAME:
    case TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_FRAME:
    case TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
      int bailout_id = iterator.NextOperand();
      int shared_info_id = iterator.NextOperand();
      Tagged<Object> shared_info = literal_array->get(shared_info_id);
      unsigned height = iterator.NextOperand();
      os << "{bailout_id=" << bailout_id << ", function="
         << Cast<SharedFunctionInfo>(shared_info)->DebugNameCStr().get()
         << ", height=" << height << "}";
      break;
    }

#if V8_ENABLE_WEBASSEMBLY
    case TranslationOpcode::JS_TO_WASM_BUILTIN_CONTINUATION_FRAME: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 4);
      int bailout_id = iterator.NextOperand();
      int shared_info_id = iterator.NextOperand();
      Tagged<Object> shared_info = literal_array->get(shared_info_id);
      unsigned height = iterator.NextOperand();
      int wasm_return_type = iterator.NextOperand();
      os << "{bailout_id=" << bailout_id << ", function="
         << Cast<SharedFunctionInfo>(shared_info)->DebugNameCStr().get()
         << ", height=" << height << ", wasm_return_type=" << wasm_return_type
         << "}";
      break;
    }

    case v8::internal::TranslationOpcode::LIFTOFF_FRAME: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
      int bailout_id = iterator.NextOperand();
      unsigned height = iterator.NextOperand();
      unsigned function_id = iterator.NextOperand();
      os << "{bailout_id=" << bailout_id << ", height=" << height
         << ", function_id=" << function_id << "}";
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    case TranslationOpcode::INLINED_EXTRA_ARGUMENTS: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 2);
      int shared_info_id = iterator.NextOperand();
      Tagged<Object> shared_info = literal_array->get(shared_info_id);
      unsigned height = iterator.NextOperand();
      os << "{function="
         << Cast<SharedFunctionInfo>(shared_info)->DebugNameCStr().get()
         << ", height=" << height << "}";
      break;
    }

    case TranslationOpcode::REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << converter.NameOfCPURegister(reg_code) << "}";
      break;
    }

    case TranslationOpcode::INT32_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << converter.NameOfCPURegister(reg_code) << " (int32)}";
      break;
    }

    case TranslationOpcode::INT64_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << converter.NameOfCPURegister(reg_code) << " (int64)}";
      break;
    }

    case TranslationOpcode::SIGNED_BIGINT64_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << converter.NameOfCPURegister(reg_code)
         << " (signed bigint64)}";
      break;
    }

    case TranslationOpcode::UNSIGNED_BIGINT64_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << converter.NameOfCPURegister(reg_code)
         << " (unsigned bigint64)}";
      break;
    }

    case TranslationOpcode::UINT32_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << converter.NameOfCPURegister(reg_code) << " (uint32)}";
      break;
    }

    case TranslationOpcode::BOOL_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << converter.NameOfCPURegister(reg_code) << " (bool)}";
      break;
    }

    case TranslationOpcode::FLOAT_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << FloatRegister::from_code(reg_code) << "}";
      break;
    }

    case TranslationOpcode::DOUBLE_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << DoubleRegister::from_code(reg_code) << "}";
      break;
    }

    case TranslationOpcode::HOLEY_DOUBLE_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << DoubleRegister::from_code(reg_code) << " (holey)}";
      break;
    }

    case TranslationOpcode::SIMD128_REGISTER: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int reg_code = iterator.NextOperandUnsigned();
      os << "{input=" << Simd128Register::from_code(reg_code) << " (Simd128)}";
      break;
    }

    case TranslationOpcode::TAGGED_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << "}";
      break;
    }

    case TranslationOpcode::INT32_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << " (int32)}";
      break;
    }

    case TranslationOpcode::INT64_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << " (int64)}";
      break;
    }

    case TranslationOpcode::SIGNED_BIGINT64_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << " (signed bigint64)}";
      break;
    }

    case TranslationOpcode::UNSIGNED_BIGINT64_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << " (unsigned bigint64)}";
      break;
    }

    case TranslationOpcode::UINT32_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << " (uint32)}";
      break;
    }

    case TranslationOpcode::BOOL_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << " (bool)}";
      break;
    }

    case TranslationOpcode::FLOAT_STACK_SLOT:
    case TranslationOpcode::DOUBLE_STACK_SLOT:
    case TranslationOpcode::SIMD128_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << "}";
      break;
    }

    case TranslationOpcode::HOLEY_DOUBLE_STACK_SLOT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int input_slot_index = iterator.NextOperand();
      os << "{input=" << input_slot_index << " (holey)}";
      break;
    }

    case TranslationOpcode::OPTIMIZED_OUT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 0);
      os << "{optimized_out}}";
      break;
    }

    case TranslationOpcode::LITERAL: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int literal_index = iterator.NextOperand();
      Tagged<Object> literal_value = literal_array->get(literal_index);
      os << "{literal_id=" << literal_index << " (" << Brief(literal_value)
         << ")}";
      break;
    }

    case TranslationOpcode::DUPLICATED_OBJECT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int object_index = iterator.NextOperand();
      os << "{object_index=" << object_index << "}";
      break;
    }

    case TranslationOpcode::ARGUMENTS_ELEMENTS: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      CreateArgumentsType arguments_type =
          static_cast<CreateArgumentsType>(iterator.NextOperand());
      os << "{arguments_type=" << arguments_type << "}";
      break;
    }
    case TranslationOpcode::ARGUMENTS_LENGTH: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 0);
      os << "{arguments_length}";
      break;
    }
    case TranslationOpcode::REST_LENGTH: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 0);
      os << "{rest_length}";
      break;
    }

    case TranslationOpcode::CAPTURED_OBJECT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
      int args_length = iterator.NextOperand();
      os << "{length=" << args_length << "}";
      break;
    }

    case TranslationOpcode::STRING_CONCAT: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 0);
      os << "{string_concat}";
      break;
    }

    case TranslationOpcode::UPDATE_FEEDBACK: {
      DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 2);
      int literal_index = iterator.NextOperand();
      FeedbackSlot slot(iterator.NextOperand());
      os << "{feedback={vector_index=" << literal_index << ", slot=" << slot
         << "}}";
      break;
    }
  }
  os << "\n";
}

// static
TranslatedValue TranslatedValue::NewDeferredObject(TranslatedState* container,
                                                   int length,
                                                   int object_index) {
  TranslatedValue slot(container, kCapturedObject);
  slot.materialization_info_ = {object_index, length};
  return slot;
}

// static
TranslatedValue TranslatedValue::NewDuplicateObject(TranslatedState* container,
                                                    int id) {
  TranslatedValue slot(container, kDuplicatedObject);
  slot.materialization_info_ = {id, -1};
  return slot;
}

// static
TranslatedValue TranslatedValue::NewStringConcat(TranslatedState* container,
                                                 int id) {
  TranslatedValue slot(container, kCapturedStringConcat);
  slot.materialization_info_ = {id, -1};
  return slot;
}

// static
TranslatedValue TranslatedValue::NewFloat(TranslatedState* container,
                                          Float32 value) {
  TranslatedValue slot(container, kFloat);
  slot.float_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewDouble(TranslatedState* container,
                                           Float64 value) {
  TranslatedValue slot(container, kDouble);
  slot.double_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewHoleyDouble(TranslatedState* container,
                                                Float64 value) {
  TranslatedValue slot(container, kHoleyDouble);
  slot.double_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewSimd128(TranslatedState* container,
                                            Simd128 value) {
  TranslatedValue slot(container, kSimd128);
  slot.simd128_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewInt32(TranslatedState* container,
                                          int32_t value) {
  TranslatedValue slot(container, kInt32);
  slot.int32_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewInt64(TranslatedState* container,
                                          int64_t value) {
  TranslatedValue slot(container, kInt64);
  slot.int64_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewInt64ToBigInt(TranslatedState* container,
                                                  int64_t value) {
  TranslatedValue slot(container, kInt64ToBigInt);
  slot.int64_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewUint64ToBigInt(TranslatedState* container,
                                                   uint64_t value) {
  TranslatedValue slot(container, kUint64ToBigInt);
  slot.uint64_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewUint32(TranslatedState* container,
                                           uint32_t value) {
  TranslatedValue slot(container, kUint32);
  slot.uint32_value_ = value;
  return slot;
}

TranslatedValue TranslatedValue::NewUint64(TranslatedState* container,
                                           uint64_t value) {
  TranslatedValue slot(container, kUint64);
  slot.uint64_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewBool(TranslatedState* container,
                                         uint32_t value) {
  TranslatedValue slot(container, kBoolBit);
  slot.uint32_value_ = value;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewTagged(TranslatedState* container,
                                           Tagged<Object> literal) {
  TranslatedValue slot(container, kTagged);
  slot.raw_literal_ = literal;
  return slot;
}

// static
TranslatedValue TranslatedValue::NewInvalid(TranslatedState* container) {
  return TranslatedValue(container, kInvalid);
}

Isolate* TranslatedValue::isolate() const { return container_->isolate(); }

Tagged<Object> TranslatedValue::raw_literal() const {
  DCHECK_EQ(kTagged, kind());
  return raw_literal_;
}

int32_t TranslatedValue::int32_value() const {
  DCHECK_EQ(kInt32, kind());
  return int32_value_;
}

int64_t TranslatedValue::int64_value() const {
  DCHECK(kInt64 == kind() || kInt64ToBigInt == kind());
  return int64_value_;
}

uint64_t TranslatedValue::uint64_value() const {
  DCHECK(kUint64ToBigInt == kind());
  return uint64_value_;
}

uint32_t TranslatedValue::uint32_value() const {
  DCHECK(kind() == kUint32 || kind() == kBoolBit);
  return uint32_value_;
}

Float32 TranslatedValue::float_value() const {
  DCHECK_EQ(kFloat, kind());
  return float_value_;
}

Float64 TranslatedValue::double_value() const {
  DCHECK(kDouble == kind() || kHoleyDouble == kind());
  return double_value_;
}

Simd128 TranslatedValue::simd_value() const {
  CHECK_EQ(kind(), kSimd128);
  return simd128_value_;
}

int TranslatedValue::object_length() const {
  DCHECK_EQ(kind(), kCapturedObject);
  return materialization_info_.length_;
}

int TranslatedValue::object_index() const {
  DCHECK(kind() == kCapturedObject || kind() == kDuplicatedObject ||
         kind() == kCapturedStringConcat);
  return materialization_info_.id_;
}

Tagged<Object> TranslatedValue::GetRawValue() const {
  // If we have a value, return it.
  if (materialization_state() == kFinished) {
    int smi;
    if (IsHeapNumber(*storage_) &&
        DoubleToSmiInteger(Object::NumberValue(*storage_), &smi)) {
      return Smi::FromInt(smi);
    }
    return *storage_;
  }

  // Otherwise, do a best effort to get the value without allocation.
  switch (kind()) {
    case kTagged: {
      Tagged<Object> object = raw_literal();
      if (IsSlicedString(object)) {
        // If {object} is a sliced string of length smaller than
        // SlicedString::kMinLength, then trim the underlying SeqString and
        // return it. This assumes that such sliced strings are only built by
        // the fast string builder optimization of Turbofan's
        // StringBuilderOptimizer/EffectControlLinearizer.
        Tagged<SlicedString> string = Cast<SlicedString>(object);
        if (string->length() < SlicedString::kMinLength) {
          Tagged<String> backing_store = string->parent();
          CHECK(IsSeqString(backing_store));

          // Creating filler at the end of the backing store if needed.
          int string_size =
              IsSeqOneByteString(backing_store)
                  ? SeqOneByteString::SizeFor(backing_store->length())
                  : SeqTwoByteString::SizeFor(backing_store->length());
          int needed_size = IsSeqOneByteString(backing_store)
                                ? SeqOneByteString::SizeFor(string->length())
                                : SeqTwoByteString::SizeFor(string->length());
          if (needed_size < string_size) {
            Address new_end = backing_store.address() + needed_size;
            isolate()->heap()->CreateFillerObjectAt(
                new_end, (string_size - needed_size));
          }

          // Updating backing store's length, effectively trimming it.
          backing_store->set_length(string->length());

          // Zeroing the padding bytes of {backing_store}.
          SeqString::DataAndPaddingSizes sz =
              Cast<SeqString>(backing_store)->GetDataAndPaddingSizes();
          auto padding =
              reinterpret_cast<char*>(backing_store.address() + sz.data_size);
          for (int i = 0; i < sz.padding_size; ++i) {
            padding[i] = 0;
          }

          // Overwriting {string} with a filler, so that we don't leave around a
          // potentially-too-small SlicedString.
          isolate()->heap()->CreateFillerObjectAt(string.address(),
                                                  sizeof(SlicedString));

          return backing_store;
        }
      }
      return object;
    }

    case kInt32: {
      bool is_smi = Smi::IsValid(int32_value());
      if (is_smi) {
        return Smi::FromInt(int32_value());
      }
      break;
    }

    case kInt64: {
      bool is_smi = (int64_value() >= static_cast<int64_t>(Smi::kMinValue) &&
                     int64_value() <= static_cast<int64_t>(Smi::kMaxValue));
      if (is_smi) {
        return Smi::FromIntptr(static_cast<intptr_t>(int64_value()));
      }
      break;
    }

    case kInt64ToBigInt:
      // Return the arguments marker.
      break;

    case kUint32: {
      bool is_smi = (uint32_value() <= static_cast<uintptr_t>(Smi::kMaxValue));
      if (is_smi) {
        return Smi::FromInt(static_cast<int32_t>(uint32_value()));
      }
      break;
    }

    case kBoolBit: {
      if (uint32_value() == 0) {
        return ReadOnlyRoots(isolate()).false_value();
      } else {
        CHECK_EQ(1U, uint32_value());
        return ReadOnlyRoots(isolate()).true_value();
      }
    }

    case kFloat: {
      int smi;
      if (DoubleToSmiInteger(float_value().get_scalar(), &smi)) {
        return Smi::FromInt(smi);
      }
      break;
    }

    case kHoleyDouble:
      if (double_value().is_hole_nan()) {
        // Hole NaNs that made it to here represent the undefined value.
        return ReadOnlyRoots(isolate()).undefined_value();
      }
      // If this is not the hole nan, then this is a normal double value, so
      // fall through to that.
      [[fallthrough]];

    case kDouble: {
      int smi;
      if (DoubleToSmiInteger(double_value().get_scalar(), &smi)) {
        return Smi::FromInt(smi);
      }
      break;
    }

    default:
      break;
  }

  // If we could not get the value without allocation, return the arguments
  // marker.
  return ReadOnlyRoots(isolate()).arguments_marker();
}

void TranslatedValue::set_initialized_storage(Handle<HeapObject> storage) {
  DCHECK_EQ(kUninitialized, materialization_state());
  storage_ = storage;
  materialization_state_ = kFinished;
}

Handle<Object> TranslatedValue::GetValue() {
  Handle<Object> value(GetRawValue(), isolate());
  if (materialization_state() == kFinished) return value;

  if (IsSmi(*value)) {
    // Even though stored as a Smi, this number might instead be needed as a
    // HeapNumber when materializing a JSObject with a field of HeapObject
    // representation. Since we don't have this information available here, we
    // just always allocate a HeapNumber and later extract the Smi again if we
    // don't need a HeapObject.
    set_initialized_storage(
        isolate()->factory()->NewHeapNumber(Object::NumberValue(*value)));
    return value;
  }

  if (*value != ReadOnlyRoots(isolate()).arguments_marker()) {
    set_initialized_storage(Cast<HeapObject>(value));
    return storage_;
  }

  // Otherwise we have to materialize.

  if (kind() == TranslatedValue::kCapturedObject ||
      kind() == TranslatedValue::kDuplicatedObject) {
    // We need to materialize the object (or possibly even object graphs).
    // To make the object verifier happy, we materialize in two steps.

    // 1. Allocate storage for reachable objects. This makes sure that for
    //    each object we have allocated space on heap. The space will be
    //    a byte array that will be later initialized, or a fully
    //    initialized object if it is safe to allocate one that will
    //    pass the verifier.
    container_->EnsureObjectAllocatedAt(this);

    // 2. Initialize the objects. If we have allocated only byte arrays
    //    for some objects, we now overwrite the byte arrays with the
    //    correct object fields. Note that this phase does not allocate
    //    any new objects, so it does not trigger the object verifier.
    return container_->InitializeObjectAt(this);
  }

  if (kind() == TranslatedValue::kCapturedStringConcat) {
    // We need to materialize the string concatenation.
    return container_->ResolveStringConcat(this);
  }

  double number = 0;
  Handle<HeapObject> heap_object;
  switch (kind()) {
    case TranslatedValue::kInt32:
      number = int32_value();
      heap_object = isolate()->factory()->NewHeapNumber(number);
      break;
    case TranslatedValue::kInt64:
      number = int64_value();
      heap_object = isolate()->factory()->NewHeapNumber(number);
      break;
    case TranslatedValue::kInt64ToBigInt:
      heap_object = BigInt::FromInt64(isolate(), int64_value());
      break;
    case TranslatedValue::kUint64ToBigInt:
      heap_object = BigInt::FromUint64(isolate(), uint64_value());
      break;
    case TranslatedValue::kUint32:
      number = uint32_value();
      heap_object = isolate()->factory()->NewHeapNumber(number);
      break;
    case TranslatedValue::kFloat:
      number = float_value().get_scalar();
      heap_object = isolate()->factory()->NewHeapNumber(number);
      break;
    case TranslatedValue::kDouble:
    // We shouldn't have hole values by now, so treat holey double as normal
    // double.s
    case TranslatedValue::kHoleyDouble:
      number = double_value().get_scalar();
      heap_object = isolate()->factory()->NewHeapNumber(number);
      break;
    default:
      UNREACHABLE();
  }
  DCHECK(!IsSmiDouble(number) || kind() == TranslatedValue::kInt64ToBigInt ||
         kind() == TranslatedValue::kUint64ToBigInt);
  set_initialized_storage(heap_object);
  return storage_;
}

bool TranslatedValue::IsMaterializedObject() const {
  switch (kind()) {
    case kCapturedObject:
    case kDuplicatedObject:
    case kCapturedStringConcat:
      return true;
    default:
      return false;
  }
}

bool TranslatedValue::IsMaterializableByDebugger() const {
  // At the moment, we only allow materialization of doubles.
  return (kind() == kDouble || kind() == kHoleyDouble);
}

int TranslatedValue::GetChildrenCount() const {
  if (kind() == kCapturedObject) {
    return object_length();
  } else if (kind() == kCapturedStringConcat) {
    static constexpr int kLeft = 1, kRight = 1;
    return kLeft + kRight;
  } else {
    return 0;
  }
}

uint64_t TranslatedState::GetUInt64Slot(Address fp, int slot_offset) {
#if V8_TARGET_ARCH_32_BIT
  return ReadUnalignedValue<uint64_t>(fp + slot_offset);
#else
  return Memory<uint64_t>(fp + slot_offset);
#endif
}

uint32_t TranslatedState::GetUInt32Slot(Address fp, int slot_offset) {
  Address address = fp + slot_offset;
#if V8_TARGET_BIG_ENDIAN && V8_HOST_ARCH_64_BIT
  return Memory<uint32_t>(address + kIntSize);
#else
  return Memory<uint32_t>(address);
#endif
}

Float32 TranslatedState::GetFloatSlot(Address fp, int slot_offset) {
#if !V8_TARGET_ARCH_S390X && !V8_TARGET_ARCH_PPC64
  return Float32::FromBits(GetUInt32Slot(fp, slot_offset));
#else
  return Float32::FromBits(Memory<uint32_t>(fp + slot_offset));
#endif
}

Float64 TranslatedState::GetDoubleSlot(Address fp, int slot_offset) {
  return Float64::FromBits(GetUInt64Slot(fp, slot_offset));
}

Simd128 TranslatedState::getSimd128Slot(Address fp, int slot_offset) {
  return base::ReadUnalignedValue<Simd128>(fp + slot_offset);
}

void TranslatedValue::Handlify() {
  if (kind() == kTagged && IsHeapObject(raw_literal())) {
    set_initialized_storage(
        Handle<HeapObject>(Cast<HeapObject>(raw_literal()), isolate()));
    raw_literal_ = Tagged<Object>();
  }
}

TranslatedFrame TranslatedFrame::UnoptimizedJSFrame(
    BytecodeOffset bytecode_offset, Tagged<SharedFunctionInfo> shared_info,
    Tagged<BytecodeArray> bytecode_array, uint32_t height,
    int return_value_offset, int return_value_count) {
  TranslatedFrame frame(kUnoptimizedFunction, shared_info, bytecode_array,
                        height, return_value_offset, return_value_count);
  frame.bytecode_offset_ = bytecode_offset;
  return frame;
}

TranslatedFrame TranslatedFrame::InlinedExtraArguments(
    Tagged<SharedFunctionInfo> shared_info, uint32_t height,
    uint32_t formal_parameter_count) {
  TranslatedFrame frame(kInlinedExtraArguments, shared_info, {}, height);
  frame.formal_parameter_count_ = formal_parameter_count;
  return frame;
}

TranslatedFrame TranslatedFrame::ConstructCreateStubFrame(
    Tagged<SharedFunctionInfo> shared_info, uint32_t height) {
  return TranslatedFrame(kConstructCreateStub, shared_info, {}, height);
}

TranslatedFrame TranslatedFrame::ConstructInvokeStubFrame(
    Tagged<SharedFunctionInfo> shared_info) {
  return TranslatedFrame(kConstructInvokeStub, shared_info, {}, 0);
}

TranslatedFrame TranslatedFrame::BuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, Tagged<SharedFunctionInfo> shared_info,
    uint32_t height) {
  TranslatedFrame frame(kBuiltinContinuation, shared_info, {}, height);
  frame.bytecode_offset_ = bytecode_offset;
  return frame;
}

#if V8_ENABLE_WEBASSEMBLY
TranslatedFrame TranslatedFrame::WasmInlinedIntoJSFrame(
    BytecodeOffset bytecode_offset, Tagged<SharedFunctionInfo> shared_info,
    uint32_t height) {
  TranslatedFrame frame(kWasmInlinedIntoJS, shared_info, {}, height);
  frame.bytecode_offset_ = bytecode_offset;
  return frame;
}

TranslatedFrame TranslatedFrame::JSToWasmBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, Tagged<SharedFunctionInfo> shared_info,
    uint32_t height, std::optional<wasm::ValueKind> return_kind) {
  TranslatedFrame frame(kJSToWasmBuiltinContinuation, shared_info, {}, height);
  frame.bytecode_offset_ = bytecode_offset;
  frame.return_kind_ = return_kind;
  return frame;
}

TranslatedFrame TranslatedFrame::LiftoffFrame(BytecodeOffset bytecode_offset,
                                              uint32_t height,
                                              uint32_t function_index) {
  // WebAssembly functions do not have a SharedFunctionInfo on the stack.
  // The deoptimizer has to recover the function-specific data based on the PC.
  Tagged<SharedFunctionInfo> shared_info;
  TranslatedFrame frame(kLiftoffFunction, shared_info, {}, height);
  frame.bytecode_offset_ = bytecode_offset;
  frame.wasm_function_index_ = function_index;
  return frame;
}
#endif  // V8_ENABLE_WEBASSEMBLY

TranslatedFrame TranslatedFrame::JavaScriptBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, Tagged<SharedFunctionInfo> shared_info,
    uint32_t height) {
  TranslatedFrame frame(kJavaScriptBuiltinContinuation, shared_info, {},
                        height);
  frame.bytecode_offset_ = bytecode_offset;
  return frame;
}

TranslatedFrame TranslatedFrame::JavaScriptBuiltinContinuationWithCatchFrame(
    BytecodeOffset bytecode_offset, Tagged<SharedFunctionInfo> shared_info,
    uint32_t height) {
  TranslatedFrame frame(kJavaScriptBuiltinContinuationWithCatch, shared_info,
                        {}, height);
  frame.bytecode_offset_ = bytecode_offset;
  return frame;
}

int TranslatedFrame::GetValueCount() const {
  // The function is added to all frame state descriptors in
  // InstructionSelector::AddInputsToFrameStateDescriptor.
  static constexpr int kTheFunction = 1;

  switch (kind()) {
    case kUnoptimizedFunction: {
      int parameter_count = raw_bytecode_array_->parameter_count();
      static constexpr int kTheContext = 1;
      static constexpr int kTheAccumulator = 1;
      return height() + parameter_count + kTheContext + kTheFunction +
             kTheAccumulator;
    }

    case kInlinedExtraArguments:
      return height() + kTheFunction;

    case kConstructCreateStub:
    case kConstructInvokeStub:
    case kBuiltinContinuation:
#if V8_ENABLE_WEBASSEMBLY
    case kJSToWasmBuiltinContinuation:
#endif  // V8_ENABLE_WEBASSEMBLY
    case kJavaScriptBuiltinContinuation:
    case kJavaScriptBuiltinContinuationWithCatch: {
      static constexpr int kTheContext = 1;
      return height() + kTheContext + kTheFunction;
    }
#if V8_ENABLE_WEBASSEMBLY
    case kWasmInlinedIntoJS: {
      static constexpr int kTheContext = 1;
      return height() + kTheContext + kTheFunction;
    }
    case kLiftoffFunction: {
      return height();
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    case kInvalid:
      UNREACHABLE();
  }
  UNREACHABLE();
}

void TranslatedFrame::Handlify(Isolate* isolate) {
  CHECK_EQ(handle_state_, kRawPointers);
  if (!raw_shared_info_.is_null()) {
    shared_info_ = handle(raw_shared_info_, isolate);
  }
  if (!raw_bytecode_array_.is_null()) {
    bytecode_array_ = handle(raw_bytecode_array_, isolate);
  }
  for (auto& value : values_) {
    value.Handlify();
  }
  handle_state_ = kHandles;
}

DeoptimizationLiteralProvider::DeoptimizationLiteralProvider(
    Tagged<DeoptimizationLiteralArray> literal_array)
    : literals_on_heap_(literal_array) {}

DeoptimizationLiteralProvider::DeoptimizationLiteralProvider(
    std::vector<DeoptimizationLiteral> literals)
    : literals_off_heap_(std::move(literals)) {}

DeoptimizationLiteralProvider::~DeoptimizationLiteralProvider() = default;

TranslatedValue DeoptimizationLiteralProvider::Get(TranslatedState* container,
                                                   int literal_index) const {
  if (V8_LIKELY(!literals_on_heap_.is_null())) {
    return TranslatedValue::NewTagged(container,
                                      literals_on_heap_->get(literal_index));
  }
#if !V8_ENABLE_WEBASSEMBLY
  UNREACHABLE();
#else
  CHECK(v8_flags.wasm_deopt);
  CHECK_LT(literal_index, literals_off_heap_.size());
  const DeoptimizationLiteral& literal = literals_off_heap_[literal_index];
  switch (literal.kind()) {
    case DeoptimizationLiteralKind::kWasmInt32:
      return TranslatedValue::NewInt32(container, literal.GetInt32());
    case DeoptimizationLiteralKind::kWasmInt64:
      return TranslatedValue::NewInt64(container, literal.GetInt64());
    case DeoptimizationLiteralKind::kWasmFloat32:
      return TranslatedValue::NewFloat(container, literal.GetFloat32());
    case DeoptimizationLiteralKind::kWasmFloat64:
      return TranslatedValue::NewDouble(container, literal.GetFloat64());
    case DeoptimizationLiteralKind::kWasmI31Ref:
      return TranslatedValue::NewTagged(container, literal.GetSmi());
    default:
      UNIMPLEMENTED();
  }
#endif
}

TranslatedFrame TranslatedState::CreateNextTranslatedFrame(
    DeoptTranslationIterator* iterator,
    Tagged<ProtectedDeoptimizationLiteralArray> protected_literal_array,
    const DeoptimizationLiteralProvider& literal_array, Address fp,
    FILE* trace_file) {
  TranslationOpcode opcode = iterator->NextOpcode();
  switch (opcode) {
    case TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN:
    case TranslationOpcode::INTERPRETED_FRAME_WITHOUT_RETURN: {
      BytecodeOffset bytecode_offset = BytecodeOffset(iterator->NextOperand());
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      Tagged<BytecodeArray> bytecode_array = Cast<BytecodeArray>(
          protected_literal_array->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      int return_value_offset = 0;
      int return_value_count = 0;
      if (opcode == TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN) {
        return_value_offset = iterator->NextOperand();
        return_value_count = iterator->NextOperand();
      }
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file, "  reading input frame %s", name.get());
        int arg_count = bytecode_array->parameter_count();
        PrintF(trace_file,
               " => bytecode_offset=%d, args=%d, height=%u, retval=%i(#%i); "
               "inputs:\n",
               bytecode_offset.ToInt(), arg_count, height, return_value_offset,
               return_value_count);
      }
      return TranslatedFrame::UnoptimizedJSFrame(
          bytecode_offset, shared_info, bytecode_array, height,
          return_value_offset, return_value_count);
    }

    case TranslationOpcode::INLINED_EXTRA_ARGUMENTS: {
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      uint32_t parameter_count = iterator->NextOperandUnsigned();
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file, "  reading inlined arguments frame %s", name.get());
        PrintF(trace_file, " => height=%u, parameter_count=%u; inputs:\n",
               height, parameter_count);
      }
      return TranslatedFrame::InlinedExtraArguments(shared_info, height,
                                                    parameter_count);
    }

    case TranslationOpcode::CONSTRUCT_CREATE_STUB_FRAME: {
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file,
               "  reading construct create stub frame %s => height = %d; "
               "inputs:\n",
               name.get(), height);
      }
      return TranslatedFrame::ConstructCreateStubFrame(shared_info, height);
    }

    case TranslationOpcode::CONSTRUCT_INVOKE_STUB_FRAME: {
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file,
               "  reading construct invoke stub frame %s, inputs:\n",
               name.get());
      }
      return TranslatedFrame::ConstructInvokeStubFrame(shared_info);
    }

    case TranslationOpcode::BUILTIN_CONTINUATION_FRAME: {
      BytecodeOffset bytecode_offset = BytecodeOffset(iterator->NextOperand());
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file, "  reading builtin continuation frame %s",
               name.get());
        PrintF(trace_file, " => bytecode_offset=%d, height=%u; inputs:\n",
               bytecode_offset.ToInt(), height);
      }
      return TranslatedFrame::BuiltinContinuationFrame(bytecode_offset,
                                                       shared_info, height);
    }

#if V8_ENABLE_WEBASSEMBLY
    case TranslationOpcode::WASM_INLINED_INTO_JS_FRAME: {
      BytecodeOffset bailout_id = BytecodeOffset(iterator->NextOperand());
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file, "  reading Wasm inlined into JS frame %s",
               name.get());
        PrintF(trace_file, " => bailout_id=%d, height=%u ; inputs:\n",
               bailout_id.ToInt(), height);
      }
      return TranslatedFrame::WasmInlinedIntoJSFrame(bailout_id, shared_info,
                                                     height);
    }

    case TranslationOpcode::JS_TO_WASM_BUILTIN_CONTINUATION_FRAME: {
      BytecodeOffset bailout_id = BytecodeOffset(iterator->NextOperand());
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      int return_kind_code = iterator->NextOperand();
      std::optional<wasm::ValueKind> return_kind;
      if (return_kind_code != kNoWasmReturnKind) {
        return_kind = static_cast<wasm::ValueKind>(return_kind_code);
      }
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file, "  reading JS to Wasm builtin continuation frame %s",
               name.get());
        PrintF(trace_file,
               " => bailout_id=%d, height=%u return_type=%d; inputs:\n",
               bailout_id.ToInt(), height,
               return_kind.has_value() ? return_kind.value() : -1);
      }
      return TranslatedFrame::JSToWasmBuiltinContinuationFrame(
          bailout_id, shared_info, height, return_kind);
    }

    case TranslationOpcode::LIFTOFF_FRAME: {
      BytecodeOffset bailout_id = BytecodeOffset(iterator->NextOperand());
      uint32_t height = iterator->NextOperandUnsigned();
      uint32_t function_id = iterator->NextOperandUnsigned();
      if (trace_file != nullptr) {
        PrintF(trace_file, "  reading input for liftoff frame");
        PrintF(trace_file,
               " => bailout_id=%d, height=%u, function_id=%u ; inputs:\n",
               bailout_id.ToInt(), height, function_id);
      }
      return TranslatedFrame::LiftoffFrame(bailout_id, height, function_id);
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    case TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_FRAME: {
      BytecodeOffset bytecode_offset = BytecodeOffset(iterator->NextOperand());
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file, "  reading JavaScript builtin continuation frame %s",
               name.get());
        PrintF(trace_file, " => bytecode_offset=%d, height=%u; inputs:\n",
               bytecode_offset.ToInt(), height);
      }
      return TranslatedFrame::JavaScriptBuiltinContinuationFrame(
          bytecode_offset, shared_info, height);
    }

    case TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME: {
      BytecodeOffset bytecode_offset = BytecodeOffset(iterator->NextOperand());
      Tagged<SharedFunctionInfo> shared_info = Cast<SharedFunctionInfo>(
          literal_array.get_on_heap_literals()->get(iterator->NextOperand()));
      uint32_t height = iterator->NextOperandUnsigned();
      if (trace_file != nullptr) {
        std::unique_ptr<char[]> name = shared_info->DebugNameCStr();
        PrintF(trace_file,
               "  reading JavaScript builtin continuation frame with catch %s",
               name.get());
        PrintF(trace_file, " => bytecode_offset=%d, height=%u; inputs:\n",
               bytecode_offset.ToInt(), height);
      }
      return TranslatedFrame::JavaScriptBuiltinContinuationWithCatchFrame(
          bytecode_offset, shared_info, height);
    }
    case TranslationOpcode::UPDATE_FEEDBACK:
    case TranslationOpcode::BEGIN_WITH_FEEDBACK:
    case TranslationOpcode::BEGIN_WITHOUT_FEEDBACK:
    case TranslationOpcode::DUPLICATED_OBJECT:
    case TranslationOpcode::ARGUMENTS_ELEMENTS:
    case TranslationOpcode::ARGUMENTS_LENGTH:
    case TranslationOpcode::REST_LENGTH:
    case TranslationOpcode::CAPTURED_OBJECT:
    case TranslationOpcode::STRING_CONCAT:
    case TranslationOpcode::REGISTER:
    case TranslationOpcode::INT32_REGISTER:
    case TranslationOpcode::INT64_REGISTER:
    case TranslationOpcode::SIGNED_BIGINT64_REGISTER:
    case TranslationOpcode::UNSIGNED_BIGINT64_REGISTER:
    case TranslationOpcode::UINT32_REGISTER:
    case TranslationOpcode::BOOL_REGISTER:
    case TranslationOpcode::FLOAT_REGISTER:
    case TranslationOpcode::DOUBLE_REGISTER:
    case TranslationOpcode::HOLEY_DOUBLE_REGISTER:
    case TranslationOpcode::SIMD128_REGISTER:
    case TranslationOpcode::TAGGED_STACK_SLOT:
    case TranslationOpcode::INT32_STACK_SLOT:
    case TranslationOpcode::INT64_STACK_SLOT:
    case TranslationOpcode::SIGNED_BIGINT64_STACK_SLOT:
    case TranslationOpcode::UNSIGNED_BIGINT64_STACK_SLOT:
    case TranslationOpcode::UINT32_STACK_SLOT:
    case TranslationOpcode::BOOL_STACK_SLOT:
    case TranslationOpcode::FLOAT_STACK_SLOT:
    case TranslationOpcode::DOUBLE_STACK_SLOT:
    case TranslationOpcode::SIMD128_STACK_SLOT:
    case TranslationOpcode::HOLEY_DOUBLE_STACK_SLOT:
    case TranslationOpcode::LITERAL:
    case TranslationOpcode::OPTIMIZED_OUT:
    case TranslationOpcode::MATCH_PREVIOUS_TRANSLATION:
      break;
  }
  UNREACHABLE();
}

// static
void TranslatedFrame::AdvanceIterator(
    std::deque<TranslatedValue>::iterator* iter) {
  int values_to_skip = 1;
  while (values_to_skip > 0) {
    // Consume the current element.
    values_to_skip--;
    // Add all the children.
    values_to_skip += (*iter)->GetChildrenCount();

    (*iter)++;
  }
}

// Creates translated values for an arguments backing store, or the backing
// store for rest parameters depending on the given {type}. The TranslatedValue
// objects for the fields are not read from the
// DeoptimizationFrameTranslation::Iterator, but instead created on-the-fly
// based on dynamic information in the optimized frame.
void TranslatedState::CreateArgumentsElementsTranslatedValues(
    int frame_index, Address input_frame_pointer, CreateArgumentsType type,
    FILE* trace_file) {
  TranslatedFrame& frame = frames_[frame_index];
  int length =
      type == CreateArgumentsType::kRestParameter
          ? std::max(0, actual_argument_count_ - formal_parameter_count_)
          : actual_argument_count_;
  int object_index = static_cast<int>(object_positions_.size());
  int value_index = static_cast<int>(frame.values_.size());
  if (trace_file != nullptr) {
    PrintF(trace_file, "arguments elements object #%d (type = %d, length = %d)",
           object_index, static_cast<uint8_t>(type), length);
  }

  object_positions_.push_back({frame_index, value_index});
  frame.Add(TranslatedValue::NewDeferredObject(
      this, length + OFFSET_OF_DATA_START(FixedArray) / kTaggedSize,
      object_index));

  ReadOnlyRoots roots(isolate_);
  frame.Add(TranslatedValue::NewTagged(this, roots.fixed_array_map()));
  frame.Add(TranslatedValue::NewInt32(this, length));

  int number_of_holes = 0;
  if (type == CreateArgumentsType::kMappedArguments) {
    // If the actual number of arguments is less than the number of formal
    // parameters, we have fewer holes to fill to not overshoot the length.
    number_of_holes = std::min(formal_parameter_count_, length);
  }
  for (int i = 0; i < number_of_holes; ++i) {
    frame.Add(TranslatedValue::NewTagged(this, roots.the_hole_value()));
  }
  int argc = length - number_of_holes;
  int start_index = number_of_holes;
  if (type == CreateArgumentsType::kRestParameter) {
    start_index = std::max(0, formal_parameter_count_);
  }
  for (int i = 0; i < argc; i++) {
    // Skip the receiver.
    int offset = i + start_index + 1;
    Address arguments_frame = offset > formal_parameter_count_
                                  ? stack_frame_pointer_
                                  : input_frame_pointer;
    Address argument_slot = arguments_frame +
                            CommonFrameConstants::kFixedFrameSizeAboveFp +
                            offset * kSystemPointerSize;

    frame.Add(TranslatedValue::NewTagged(this, *FullObjectSlot(argument_slot)));
  }
}

// We can't intermix stack decoding and allocations because the deoptimization
// infrastructure is not GC safe.
// Thus we build a temporary structure in malloced space.
// The TranslatedValue objects created correspond to the static translation
// instructions from the DeoptTranslationIterator, except for
// TranslationOpcode::ARGUMENTS_ELEMENTS, where the number and values of the
// FixedArray elements depend on dynamic information from the optimized frame.
// Returns the number of expected nested translations from the
// DeoptTranslationIterator.
int TranslatedState::CreateNextTranslatedValue(
    int frame_index, DeoptTranslationIterator* iterator,
    const DeoptimizationLiteralProvider& literal_array, Address fp,
    RegisterValues* registers, FILE* trace_file) {
  disasm::NameConverter converter;

  TranslatedFrame& frame = frames_[frame_index];
  int value_index = static_cast<int>(frame.values_.size());

  TranslationOpcode opcode = iterator->NextOpcode();
  switch (opcode) {
    case TranslationOpcode::BEGIN_WITH_FEEDBACK:
    case TranslationOpcode::BEGIN_WITHOUT_FEEDBACK:
    case TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN:
    case TranslationOpcode::INTERPRETED_FRAME_WITHOUT_RETURN:
    case TranslationOpcode::INLINED_EXTRA_ARGUMENTS:
    case TranslationOpcode::CONSTRUCT_CREATE_STUB_FRAME:
    case TranslationOpcode::CONSTRUCT_INVOKE_STUB_FRAME:
    case TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_FRAME:
    case TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME:
    case TranslationOpcode::BUILTIN_CONTINUATION_FRAME:
#if V8_ENABLE_WEBASSEMBLY
    case TranslationOpcode::WASM_INLINED_INTO_JS_FRAME:
    case TranslationOpcode::JS_TO_WASM_BUILTIN_CONTINUATION_FRAME:
    case TranslationOpcode::LIFTOFF_FRAME:
#endif  // V8_ENABLE_WEBASSEMBLY
    case TranslationOpcode::UPDATE_FEEDBACK:
    case TranslationOpcode::MATCH_PREVIOUS_TRANSLATION:
      // Peeled off before getting here.
      break;

    case TranslationOpcode::DUPLICATED_OBJECT: {
      int object_id = iterator->NextOperand();
      if (trace_file != nullptr) {
        PrintF(trace_file, "duplicated object #%d", object_id);
      }
      object_positions_.push_back(object_positions_[object_id]);
      TranslatedValue translated_value =
          TranslatedValue::NewDuplicateObject(this, object_id);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::ARGUMENTS_ELEMENTS: {
      CreateArgumentsType arguments_type =
          static_cast<CreateArgumentsType>(iterator->NextOperand());
      CreateArgumentsElementsTranslatedValues(frame_index, fp, arguments_type,
                                              trace_file);
      return 0;
    }

    case TranslationOpcode::ARGUMENTS_LENGTH: {
      if (trace_file != nullptr) {
        PrintF(trace_file, "arguments length field (length = %d)",
               actual_argument_count_);
      }
      frame.Add(TranslatedValue::NewInt32(this, actual_argument_count_));
      return 0;
    }

    case TranslationOpcode::REST_LENGTH: {
      int rest_length =
          std::max(0, actual_argument_count_ - formal_parameter_count_);
      if (trace_file != nullptr) {
        PrintF(trace_file, "rest length field (length = %d)", rest_length);
      }
      frame.Add(TranslatedValue::NewInt32(this, rest_length));
      return 0;
    }

    case TranslationOpcode::CAPTURED_OBJECT: {
      int field_count = iterator->NextOperand();
      int object_index = static_cast<int>(object_positions_.size());
      if (trace_file != nullptr) {
        PrintF(trace_file, "captured object #%d (length = %d)", object_index,
               field_count);
      }
      object_positions_.push_back({frame_index, value_index});
      TranslatedValue translated_value =
          TranslatedValue::NewDeferredObject(this, field_count, object_index);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::STRING_CONCAT: {
      int object_index = static_cast<int>(object_positions_.size());
      if (trace_file != nullptr) {
        PrintF(trace_file, "string concatenation #%d", object_index);
      }

      object_positions_.push_back({frame_index, value_index});
      TranslatedValue translated_value =
          TranslatedValue::NewStringConcat(this, object_index);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      intptr_t value = registers->GetRegister(input_reg);
      Address uncompressed_value = DecompressIfNeeded(value);
      if (trace_file != nullptr) {
        PrintF(trace_file, V8PRIxPTR_FMT " ; %s ", uncompressed_value,
               converter.NameOfCPURegister(input_reg));
        ShortPrint(Tagged<Object>(uncompressed_value), trace_file);
      }
      TranslatedValue translated_value =
          TranslatedValue::NewTagged(this, Tagged<Object>(uncompressed_value));
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::INT32_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      intptr_t value = registers->GetRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; %s (int32)", value,
               converter.NameOfCPURegister(input_reg));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewInt32(this, static_cast<int32_t>(value));
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::INT64_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      intptr_t value = registers->GetRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; %s (int64)", value,
               converter.NameOfCPURegister(input_reg));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewInt64(this, static_cast<int64_t>(value));
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::SIGNED_BIGINT64_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      intptr_t value = registers->GetRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; %s (signed bigint64)", value,
               converter.NameOfCPURegister(input_reg));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewInt64ToBigInt(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::UNSIGNED_BIGINT64_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      intptr_t value = registers->GetRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; %s (unsigned bigint64)", value,
               converter.NameOfCPURegister(input_reg));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewUint64ToBigInt(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::UINT32_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      intptr_t value = registers->GetRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIuPTR " ; %s (uint32)", value,
               converter.NameOfCPURegister(input_reg));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewUint32(this, static_cast<uint32_t>(value));
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::BOOL_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      intptr_t value = registers->GetRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; %s (bool)", value,
               converter.NameOfCPURegister(input_reg));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewBool(this, static_cast<uint32_t>(value));
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::FLOAT_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      Float32 value = registers->GetFloatRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%e ; %s (float)", value.get_scalar(),
               RegisterName(FloatRegister::from_code(input_reg)));
      }
      TranslatedValue translated_value = TranslatedValue::NewFloat(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::DOUBLE_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      Float64 value = registers->GetDoubleRegister(input_reg);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%e ; %s (double)", value.get_scalar(),
               RegisterName(DoubleRegister::from_code(input_reg)));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewDouble(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::HOLEY_DOUBLE_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      Float64 value = registers->GetDoubleRegister(input_reg);
      if (trace_file != nullptr) {
        if (value.is_hole_nan()) {
          PrintF(trace_file, "the hole");
        } else {
          PrintF(trace_file, "%e", value.get_scalar());
        }
        PrintF(trace_file, " ; %s (holey double)",
               RegisterName(DoubleRegister::from_code(input_reg)));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewHoleyDouble(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::SIMD128_REGISTER: {
      int input_reg = iterator->NextOperandUnsigned();
      if (registers == nullptr) {
        TranslatedValue translated_value = TranslatedValue::NewInvalid(this);
        frame.Add(translated_value);
        return translated_value.GetChildrenCount();
      }
      Simd128 value = registers->GetSimd128Register(input_reg);
      if (trace_file != nullptr) {
        int8x16 val = value.to_i8x16();
        PrintF(trace_file,
               "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x ; %s (Simd128)",
               val.val[0], val.val[1], val.val[2], val.val[3], val.val[4],
               val.val[5], val.val[6], val.val[7], val.val[8], val.val[9],
               val.val[10], val.val[11], val.val[12], val.val[13], val.val[14],
               val.val[15], RegisterName(DoubleRegister::from_code(input_reg)));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewSimd128(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::TAGGED_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      intptr_t value = *(reinterpret_cast<intptr_t*>(fp + slot_offset));
      Address uncompressed_value = DecompressIfNeeded(value);
      if (trace_file != nullptr) {
        PrintF(trace_file, V8PRIxPTR_FMT " ;  [fp %c %3d]  ",
               uncompressed_value, slot_offset < 0 ? '-' : '+',
               std::abs(slot_offset));
        ShortPrint(Tagged<Object>(uncompressed_value), trace_file);
      }
      TranslatedValue translated_value =
          TranslatedValue::NewTagged(this, Tagged<Object>(uncompressed_value));
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::INT32_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      uint32_t value = GetUInt32Slot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%d ; (int32) [fp %c %3d] ",
               static_cast<int32_t>(value), slot_offset < 0 ? '-' : '+',
               std::abs(slot_offset));
      }
      TranslatedValue translated_value = TranslatedValue::NewInt32(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::INT64_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      uint64_t value = GetUInt64Slot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; (int64) [fp %c %3d] ",
               static_cast<intptr_t>(value), slot_offset < 0 ? '-' : '+',
               std::abs(slot_offset));
      }
      TranslatedValue translated_value = TranslatedValue::NewInt64(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::SIGNED_BIGINT64_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      uint64_t value = GetUInt64Slot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; (signed bigint64) [fp %c %3d] ",
               static_cast<intptr_t>(value), slot_offset < 0 ? '-' : '+',
               std::abs(slot_offset));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewInt64ToBigInt(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::UNSIGNED_BIGINT64_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      uint64_t value = GetUInt64Slot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%" V8PRIdPTR " ; (unsigned bigint64) [fp %c %3d] ",
               static_cast<intptr_t>(value), slot_offset < 0 ? '-' : '+',
               std::abs(slot_offset));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewUint64ToBigInt(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::UINT32_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      uint32_t value = GetUInt32Slot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%u ; (uint32) [fp %c %3d] ", value,
               slot_offset < 0 ? '-' : '+', std::abs(slot_offset));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewUint32(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::BOOL_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      uint32_t value = GetUInt32Slot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%u ; (bool) [fp %c %3d] ", value,
               slot_offset < 0 ? '-' : '+', std::abs(slot_offset));
      }
      TranslatedValue translated_value = TranslatedValue::NewBool(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::FLOAT_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      Float32 value = GetFloatSlot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%e ; (float) [fp %c %3d] ", value.get_scalar(),
               slot_offset < 0 ? '-' : '+', std::abs(slot_offset));
      }
      TranslatedValue translated_value = TranslatedValue::NewFloat(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::DOUBLE_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      Float64 value = GetDoubleSlot(fp, slot_offset);
      if (trace_file != nullptr) {
        PrintF(trace_file, "%e ; (double) [fp %c %d] ", value.get_scalar(),
               slot_offset < 0 ? '-' : '+', std::abs(slot_offset));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewDouble(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::SIMD128_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      Simd128 value = getSimd128Slot(fp, slot_offset);
      if (trace_file != nullptr) {
        int8x16 val = value.to_i8x16();
        PrintF(trace_file,
               "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x ; (Simd128) [fp %c %d]",
               val.val[0], val.val[1], val.val[2], val.val[3], val.val[4],
               val.val[5], val.val[6], val.val[7], val.val[8], val.val[9],
               val.val[10], val.val[11], val.val[12], val.val[13], val.val[14],
               val.val[15], slot_offset < 0 ? '-' : '+', std::abs(slot_offset));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewSimd128(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::HOLEY_DOUBLE_STACK_SLOT: {
      int slot_offset = OptimizedJSFrame::StackSlotOffsetRelativeToFp(
          iterator->NextOperand());
      Float64 value = GetDoubleSlot(fp, slot_offset);
      if (trace_file != nullptr) {
        if (value.is_hole_nan()) {
          PrintF(trace_file, "the hole");
        } else {
          PrintF(trace_file, "%e", value.get_scalar());
        }
        PrintF(trace_file, " ; (holey double) [fp %c %d] ",
               slot_offset < 0 ? '-' : '+', std::abs(slot_offset));
      }
      TranslatedValue translated_value =
          TranslatedValue::NewHoleyDouble(this, value);
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::LITERAL: {
      int literal_index = iterator->NextOperand();
      TranslatedValue translated_value = literal_array.Get(this, literal_index);
      if (trace_file != nullptr) {
        if (translated_value.kind() == TranslatedValue::Kind::kTagged) {
          PrintF(trace_file, V8PRIxPTR_FMT " ; (literal %2d) ",
                 translated_value.raw_literal().ptr(), literal_index);
          ShortPrint(translated_value.raw_literal(), trace_file);
        } else {
          switch (translated_value.kind()) {
            case TranslatedValue::Kind::kDouble:
              if (translated_value.double_value().is_nan()) {
                PrintF(trace_file, "(wasm double literal %f 0x%" PRIx64 ")",
                       translated_value.double_value().get_scalar(),
                       translated_value.double_value().get_bits());
              } else {
                PrintF(trace_file, "(wasm double literal %f)",
                       translated_value.double_value().get_scalar());
              }
              break;
            case TranslatedValue::Kind::kFloat:
              if (translated_value.float_value().is_nan()) {
                PrintF(trace_file, "(wasm float literal %f 0x%x)",
                       translated_value.float_value().get_scalar(),
                       translated_value.float_value().get_bits());
              } else {
                PrintF(trace_file, "(wasm float literal %f)",
                       translated_value.float_value().get_scalar());
              }
              break;
            case TranslatedValue::Kind::kInt64:
              PrintF(trace_file, "(wasm int64 literal %" PRId64 ")",
                     translated_value.int64_value());
              break;
            case TranslatedValue::Kind::kInt32:
              PrintF(trace_file, "(wasm int32 literal %d)",
                     translated_value.int32_value());
              break;
            default:
              PrintF(trace_file, " (wasm literal) ");
              break;
          }
        }
      }
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }

    case TranslationOpcode::OPTIMIZED_OUT: {
      if (trace_file != nullptr) {
        PrintF(trace_file, "(optimized out)");
      }

      TranslatedValue translated_value = TranslatedValue::NewTagged(
          this, ReadOnlyRoots(isolate_).optimized_out());
      frame.Add(translated_value);
      return translated_value.GetChildrenCount();
    }
  }

  FATAL("We should never get here - unexpected deopt info.");
}

Address TranslatedState::DecompressIfNeeded(intptr_t value) {
  if (COMPRESS_POINTERS_BOOL &&
#ifdef V8_TARGET_ARCH_LOONG64
      // The 32-bit compressed values are supposed to be sign-extended on
      // loongarch64.
      is_int32(value)) {
#else
      static_cast<uintptr_t>(value) <= std::numeric_limits<uint32_t>::max()) {
#endif
    return V8HeapCompressionScheme::DecompressTagged(
        isolate(), static_cast<uint32_t>(value));
  } else {
    return value;
  }
}

TranslatedState::TranslatedState(const JavaScriptFrame* frame)
    : purpose_(kFrameInspection) {
  int deopt_index = SafepointEntry::kNoDeoptIndex;
  Tagged<Code> code = frame->LookupCode();
  Tagged<DeoptimizationData> data =
      static_cast<const OptimizedJSFrame*>(frame)->GetDeoptimizationData(
          code, &deopt_index);
  DCHECK(!data.is_null() && deopt_index != SafepointEntry::kNoDeoptIndex);
  DeoptimizationFrameTranslation::Iterator it(
      data->FrameTranslation(), data->TranslationIndex(deopt_index).value());
  int actual_argc = frame->GetActualArgumentCount();
  DeoptimizationLiteralProvider literals(data->LiteralArray());
  Init(frame->isolate(), frame->fp(), frame->fp(), &it,
       data->ProtectedLiteralArray(), literals, nullptr /* registers */,
       nullptr /* trace file */, code->parameter_count_without_receiver(),
       actual_argc);
}

void TranslatedState::Init(
    Isolate* isolate, Address input_frame_pointer, Address stack_frame_pointer,
    DeoptTranslationIterator* iterator,
    Tagged<ProtectedDeoptimizationLiteralArray> protected_literal_array,
    const DeoptimizationLiteralProvider& literal_array,
    RegisterValues* registers, FILE* trace_file, int formal_parameter_count,
    int actual_argument_count) {
  DCHECK(frames_.empty());

  stack_frame_pointer_ = stack_frame_pointer;
  formal_parameter_count_ = formal_parameter_count;
  actual_argument_count_ = actual_argument_count;
  isolate_ = isolate;

  // Read out the 'header' translation.
  TranslationOpcode opcode = iterator->NextOpcode();
  CHECK(TranslationOpcodeIsBegin(opcode));
  iterator->NextOperand();  // Skip the lookback distance.
  int count = iterator->NextOperand();
  frames_.reserve(count);
  iterator->NextOperand();  // Drop JS frames count.

  if (opcode == TranslationOpcode::BEGIN_WITH_FEEDBACK) {
    ReadUpdateFeedback(iterator, literal_array.get_on_heap_literals(),
                       trace_file);
  }

  std::stack<int> nested_counts;

  // Read the frames
  for (int frame_index = 0; frame_index < count; frame_index++) {
    // Read the frame descriptor.
    frames_.push_back(CreateNextTranslatedFrame(
        iterator, protected_literal_array, literal_array, input_frame_pointer,
        trace_file));
    TranslatedFrame& frame = frames_.back();

    // Read the values.
    int values_to_process = frame.GetValueCount();
    while (values_to_process > 0 || !nested_counts.empty()) {
      if (trace_file != nullptr) {
        if (nested_counts.empty()) {
          // For top level values, print the value number.
          PrintF(trace_file,
                 "    %3i: ", frame.GetValueCount() - values_to_process);
        } else {
          // Take care of indenting for nested values.
          PrintF(trace_file, "         ");
          for (size_t j = 0; j < nested_counts.size(); j++) {
            PrintF(trace_file, "  ");
          }
        }
      }

      int nested_count =
          CreateNextTranslatedValue(frame_index, iterator, literal_array,
                                    input_frame_pointer, registers, trace_file);

      if (trace_file != nullptr) {
        PrintF(trace_file, "\n");
      }

      // Update the value count and resolve the nesting.
      values_to_process--;
      if (nested_count > 0) {
        nested_counts.push(values_to_process);
        values_to_process = nested_count;
      } else {
        while (values_to_process == 0 && !nested_counts.empty()) {
          values_to_process = nested_counts.top();
          nested_counts.pop();
        }
      }
    }
  }

  CHECK(!iterator->HasNextOpcode() ||
        TranslationOpcodeIsBegin(iterator->NextOpcode()));
}

void TranslatedState::Prepare(Address stack_frame_pointer) {
  for (auto& frame : frames_) {
    frame.Handlify(isolate());
  }

  if (!feedback_vector_.is_null()) {
    feedback_vector_handle_ = handle(feedback_vector_, isolate());
    feedback_vector_ = FeedbackVector();
  }
  stack_frame_pointer_ = stack_frame_pointer;

  UpdateFromPreviouslyMaterializedObjects();
}

TranslatedValue* TranslatedState::GetValueByObjectIndex(int object_index) {
  CHECK_LT(static_cast<size_t>(object_index), object_positions_.size());
  TranslatedState::ObjectPosition pos = object_positions_[object_index];
  return &(frames_[pos.frame_index_].values_[pos.value_index_]);
}

Handle<HeapObject> TranslatedState::ResolveStringConcat(TranslatedValue* slot) {
  if (slot->materialization_state() == TranslatedValue::kFinished) {
    return slot->storage();
  }

  CHECK_EQ(TranslatedValue::kUninitialized, slot->materialization_state());

  int index = slot->object_index();
  TranslatedState::ObjectPosition pos = object_positions_[index];
  int value_index = pos.value_index_;

  TranslatedFrame* frame = &(frames_[pos.frame_index_]);
  CHECK_EQ(slot, &(frame->values_[value_index]));

  // TODO(dmercadier): try to avoid the recursive GetValue call.
  value_index++;
  TranslatedValue* left_slot = &(frame->values_[value_index]);
  Handle<Object> left = left_slot->GetValue();

  // Skipping the left input that we've just processed. Note that we can't just
  // do `value_index++`, because the left input could itself be a dematerialized
  // string concatenation, in which case it will occupy multiple slots.
  SkipSlots(1, frame, &value_index);

  TranslatedValue* right_slot = &(frame->values_[value_index]);
  Handle<Object> right = right_slot->GetValue();

  Handle<String> result =
      isolate()
          ->factory()
          ->NewConsString(Cast<String>(left), Cast<String>(right))
          .ToHandleChecked();

  slot->set_initialized_storage(result);
  return result;
}

Handle<HeapObject> TranslatedState::InitializeObjectAt(TranslatedValue* slot) {
  DisallowGarbageCollection no_gc;

  slot = ResolveCapturedObject(slot);
  if (slot->materialization_state() != TranslatedValue::kFinished) {
    std::stack<int> worklist;
    worklist.push(slot->object_index());
    slot->mark_finished();

    while (!worklist.empty()) {
      int index = worklist.top();
      worklist.pop();
      InitializeCapturedObjectAt(index, &worklist, no_gc);
    }
  }
  return slot->storage();
}

void TranslatedState::InitializeCapturedObjectAt(
    int object_index, std::stack<int>* worklist,
    const DisallowGarbageCollection& no_gc) {
  CHECK_LT(static_cast<size_t>(object_index), object_positions_.size());
  TranslatedState::ObjectPosition pos = object_positions_[object_index];
  int value_index = pos.value_index_;

  TranslatedFrame* frame = &(frames_[pos.frame_index_]);
  TranslatedValue* slot = &(frame->values_[value_index]);
  value_index++;

  // Note that we cannot reach this point with kCapturedStringConcats slots,
  // since they have been already finalized in EnsureObjectAllocatedAt and
  // EnsureCapturedObjectAllocatedAt.
  CHECK_EQ(TranslatedValue::kFinished, slot->materialization_state());
  CHECK_EQ(TranslatedValue::kCapturedObject, slot->kind());

  // Ensure all fields are initialized.
  int children_init_index = value_index;
  for (int i = 0; i < slot->GetChildrenCount(); i++) {
    // If the field is an object that has not been initialized yet, queue it
    // for initialization (and mark it as such).
    TranslatedValue* child_slot = frame->ValueAt(children_init_index);
    if (child_slot->kind() == TranslatedValue::kCapturedObject ||
        child_slot->kind() == TranslatedValue::kDuplicatedObject) {
      child_slot = ResolveCapturedObject(child_slot);
      if (child_slot->materialization_state() != TranslatedValue::kFinished) {
        CHECK_EQ(TranslatedValue::kAllocated,
                 child_slot->materialization_state());
        worklist->push(child_slot->object_index());
        child_slot->mark_finished();
      }
    }
    SkipSlots(1, frame, &children_init_index);
  }

  // Read the map.
  // The map should never be materialized, so let us check we already have
  // an existing object here.
  CHECK_EQ(frame->values_[value_index].kind(), TranslatedValue::kTagged);
  auto map = Cast<Map>(frame->values_[value_index].GetValue());
  CHECK(IsMap(*map));
  value_index++;

  // Handle the special cases.
  switch (map->instance_type()) {
    case HEAP_NUMBER_TYPE:
    case FIXED_DOUBLE_ARRAY_TYPE:
      return;

    case FIXED_ARRAY_TYPE:
    case AWAIT_CONTEXT_TYPE:
    case BLOCK_CONTEXT_TYPE:
    case CATCH_CONTEXT_TYPE:
    case DEBUG_EVALUATE_CONTEXT_TYPE:
    case EVAL_CONTEXT_TYPE:
    case FUNCTION_CONTEXT_TYPE:
    case MODULE_CONTEXT_TYPE:
    case NATIVE_CONTEXT_TYPE:
    case SCRIPT_CONTEXT_TYPE:
    case WITH_CONTEXT_TYPE:
    case OBJECT_BOILERPLATE_DESCRIPTION_TYPE:
    case HASH_TABLE_TYPE:
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
    case NAME_DICTIONARY_TYPE:
    case GLOBAL_DICTIONARY_TYPE:
    case NUMBER_DICTIONARY_TYPE:
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
    case PROPERTY_ARRAY_TYPE:
    case SCRIPT_CONTEXT_TABLE_TYPE:
    case SLOPPY_ARGUMENTS_ELEMENTS_TYPE:
      InitializeObjectWithTaggedFieldsAt(frame, &value_index, slot, map, no_gc);
      break;

    default:
      CHECK(IsJSObjectMap(*map));
      InitializeJSObjectAt(frame, &value_index, slot, map, no_gc);
      break;
  }
  CHECK_EQ(value_index, children_init_index);
}

void TranslatedState::EnsureObjectAllocatedAt(TranslatedValue* slot) {
  slot = ResolveCapturedObject(slot);

  if (slot->kind() == TranslatedValue::kCapturedStringConcat) {
    ResolveStringConcat(slot);
    return;
  }

  if (slot->materialization_state() == TranslatedValue::kUninitialized) {
    std::stack<int> worklist;
    worklist.push(slot->object_index());
    slot->mark_allocated();

    while (!worklist.empty()) {
      int index = worklist.top();
      worklist.pop();
      EnsureCapturedObjectAllocatedAt(index, &worklist);
    }
  }
}

int TranslatedValue::GetSmiValue() const {
  Tagged<Object> value = GetRawValue();
  CHECK(IsSmi(value));
  return Cast<Smi>(value).value();
}

void TranslatedState::MaterializeFixedDoubleArray(TranslatedFrame* frame,
                                                  int* value_index,
                                                  TranslatedValue* slot,
                                                  DirectHandle<Map> map) {
  int length = frame->values_[*value_index].GetSmiValue();
  (*value_index)++;
  Handle<FixedDoubleArray> array =
      Cast<FixedDoubleArray>(isolate()->factory()->NewFixedDoubleArray(length));
  CHECK_GT(length, 0);
  for (int i = 0; i < length; i++) {
    CHECK_NE(TranslatedValue::kCapturedObject,
             frame->values_[*value_index].kind());
    DirectHandle<Object> value = frame->values_[*value_index].GetValue();
    if (IsNumber(*value)) {
      array->set(i, Object::NumberValue(*value));
    } else {
      CHECK(value.is_identical_to(isolate()->factory()->the_hole_value()));
      array->set_the_hole(isolate(), i);
    }
    (*value_index)++;
  }
  slot->set_storage(array);
}

void TranslatedState::MaterializeHeapNumber(TranslatedFrame* frame,
                                            int* value_index,
                                            TranslatedValue* slot) {
  CHECK_NE(TranslatedValue::kCapturedObject,
           frame->values_[*value_index].kind());
  DirectHandle<Object> value = frame->values_[*value_index].GetValue();
  CHECK(IsNumber(*value));
  Handle<HeapNumber> box =
      isolate()->factory()->NewHeapNumber(Object::NumberValue(*value));
  (*value_index)++;
  slot->set_storage(box);
}

namespace {

enum StorageKind : uint8_t { kStoreTagged, kStoreHeapObject };

}  // namespace

void TranslatedState::SkipSlots(int slots_to_skip, TranslatedFrame* frame,
                                int* value_index) {
  while (slots_to_skip > 0) {
    TranslatedValue* slot = &(frame->values_[*value_index]);
    (*value_index)++;
    slots_to_skip--;

    if (slot->kind() == TranslatedValue::kCapturedObject ||
        slot->kind() == TranslatedValue::kCapturedStringConcat) {
      slots_to_skip += slot->GetChildrenCount();
    }
  }
}

void TranslatedState::EnsureCapturedObjectAllocatedAt(
    int object_index, std::stack<int>* worklist) {
  CHECK_LT(static_cast<size_t>(object_index), object_positions_.size());
  TranslatedState::ObjectPosition pos = object_positions_[object_index];
  int value_index = pos.value_index_;

  TranslatedFrame* frame = &(frames_[pos.frame_index_]);
  TranslatedValue* slot = &(frame->values_[value_index]);
  value_index++;

  CHECK_EQ(TranslatedValue::kAllocated, slot->materialization_state());
  CHECK_EQ(TranslatedValue::kCapturedObject, slot->kind());

  // Read the map.
  // The map should never be materialized, so let us check we already have
  // an existing object here.
  CHECK_EQ(frame->values_[value_index].kind(), TranslatedValue::kTagged);
  auto map = Cast<Map>(frame->values_[value_index].GetValue());
  CHECK(IsMap(*map));
  value_index++;

  // Handle the special cases.
  switch (map->instance_type()) {
    case FIXED_DOUBLE_ARRAY_TYPE:
      // Materialize (i.e. allocate&initialize) the array and return since
      // there is no need to process the children.
      return MaterializeFixedDoubleArray(frame, &value_index, slot, map);

    case HEAP_NUMBER_TYPE:
      // Materialize (i.e. allocate&initialize) the heap number and return.
      // There is no need to process the children.
      return MaterializeHeapNumber(frame, &value_index, slot);

    case FIXED_ARRAY_TYPE:
    case SCRIPT_CONTEXT_TABLE_TYPE:
    case AWAIT_CONTEXT_TYPE:
    case BLOCK_CONTEXT_TYPE:
    case CATCH_CONTEXT_TYPE:
    case DEBUG_EVALUATE_CONTEXT_TYPE:
    case EVAL_CONTEXT_TYPE:
    case FUNCTION_CONTEXT_TYPE:
    case MODULE_CONTEXT_TYPE:
    case NATIVE_CONTEXT_TYPE:
    case SCRIPT_CONTEXT_TYPE:
    case WITH_CONTEXT_TYPE:
    case HASH_TABLE_TYPE:
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
    case NAME_DICTIONARY_TYPE:
    case GLOBAL_DICTIONARY_TYPE:
    case NUMBER_DICTIONARY_TYPE:
    case SIMPLE_NUMBER_DICTIONARY_TYPE: {
      // Check we have the right size.
      int array_length = frame->values_[value_index].GetSmiValue();
      int instance_size = FixedArray::SizeFor(array_length);
      CHECK_EQ(instance_size, slot->GetChildrenCount() * kTaggedSize);

      // Canonicalize empty fixed array.
      if (*map == ReadOnlyRoots(isolate()).empty_fixed_array()->map() &&
          array_length == 0) {
        slot->set_storage(isolate()->factory()->empty_fixed_array());
      } else {
        slot->set_storage(AllocateStorageFor(slot));
      }

      // Make sure all the remaining children (after the map) are allocated.
      return EnsureChildrenAllocated(slot->GetChildrenCount() - 1, frame,
                                     &value_index, worklist);
    }

    case SLOPPY_ARGUMENTS_ELEMENTS_TYPE: {
      // Verify that the arguments size is correct.
      int args_length = frame->values_[value_index].GetSmiValue();
      int args_size = SloppyArgumentsElements::SizeFor(args_length);
      CHECK_EQ(args_size, slot->GetChildrenCount() * kTaggedSize);

      slot->set_storage(AllocateStorageFor(slot));

      // Make sure all the remaining children (after the map) are allocated.
      return EnsureChildrenAllocated(slot->GetChildrenCount() - 1, frame,
                                     &value_index, worklist);
    }

    case PROPERTY_ARRAY_TYPE: {
      // Check we have the right size.
      int length_or_hash = frame->values_[value_index].GetSmiValue();
      int array_length = PropertyArray::LengthField::decode(length_or_hash);
      int instance_size = PropertyArray::SizeFor(array_length);
      CHECK_EQ(instance_size, slot->GetChildrenCount() * kTaggedSize);

      slot->set_storage(AllocateStorageFor(slot));

      // Make sure all the remaining children (after the map) are allocated.
      return EnsureChildrenAllocated(slot->GetChildrenCount() - 1, frame,
                                     &value_index, worklist);
    }

    default:
      EnsureJSObjectAllocated(slot, map);
      int remaining_children_count = slot->GetChildrenCount() - 1;

      TranslatedValue* properties_slot = frame->ValueAt(value_index);
      value_index++, remaining_children_count--;
      if (properties_slot->kind() == TranslatedValue::kCapturedObject) {
        // We are materializing the property array, so make sure we put the
        // mutable heap numbers at the right places.
        EnsurePropertiesAllocatedAndMarked(properties_slot, map);
        EnsureChildrenAllocated(properties_slot->GetChildrenCount(), frame,
                                &value_index, worklist);
      } else {
        CHECK_EQ(properties_slot->kind(), TranslatedValue::kTagged);
      }

      TranslatedValue* elements_slot = frame->ValueAt(value_index);
      value_index++, remaining_children_count--;
      if (elements_slot->kind() == TranslatedValue::kCapturedObject ||
          !IsJSArrayMap(*map)) {
        // Handle this case with the other remaining children below.
        value_index--, remaining_children_count++;
      } else {
        CHECK_EQ(elements_slot->kind(), TranslatedValue::kTagged);
        elements_slot->GetValue();
        if (purpose_ == kFrameInspection) {
          // We are materializing a JSArray for the purpose of frame inspection.
          // If we were to construct it with the above elements value then an
          // actual deopt later on might create another JSArray instance with
          // the same elements store. That would violate the key assumption
          // behind left-trimming.
          elements_slot->ReplaceElementsArrayWithCopy();
        }
      }

      // Make sure all the remaining children (after the map, properties store,
      // and possibly elements store) are allocated.
      return EnsureChildrenAllocated(remaining_children_count, frame,
                                     &value_index, worklist);
  }
  UNREACHABLE();
}

void TranslatedValue::ReplaceElementsArrayWithCopy() {
  DCHECK_EQ(kind(), TranslatedValue::kTagged);
  DCHECK_EQ(materialization_state(), TranslatedValue::kFinished);
  auto elements = Cast<FixedArrayBase>(GetValue());
  DCHECK(IsFixedArray(*elements) || IsFixedDoubleArray(*elements));
  if (IsFixedDoubleArray(*elements)) {
    DCHECK(!elements->IsCowArray());
    set_storage(isolate()->factory()->CopyFixedDoubleArray(
        Cast<FixedDoubleArray>(elements)));
  } else if (!elements->IsCowArray()) {
    set_storage(
        isolate()->factory()->CopyFixedArray(Cast<FixedArray>(elements)));
  }
}

void TranslatedState::EnsureChildrenAllocated(int count, TranslatedFrame* frame,
                                              int* value_index,
                                              std::stack<int>* worklist) {
  // Ensure all children are allocated.
  for (int i = 0; i < count; i++) {
    // If the field is an object that has not been allocated yet, queue it
    // for initialization (and mark it as such).
    TranslatedValue* child_slot = frame->ValueAt(*value_index);
    if (child_slot->kind() == TranslatedValue::kCapturedObject ||
        child_slot->kind() == TranslatedValue::kDuplicatedObject) {
      child_slot = ResolveCapturedObject(child_slot);

      if (child_slot->kind() == TranslatedValue::kCapturedStringConcat) {
        ResolveStringConcat(child_slot);
      } else if (child_slot->materialization_state() ==
                 TranslatedValue::kUninitialized) {
        worklist->push(child_slot->object_index());
        child_slot->mark_allocated();
      }
    } else {
      // Make sure the simple values (heap numbers, etc.) are properly
      // initialized.
      child_slot->GetValue();
    }
    SkipSlots(1, frame, value_index);
  }
}

void TranslatedState::EnsurePropertiesAllocatedAndMarked(
    TranslatedValue* properties_slot, DirectHandle<Map> map) {
  CHECK_EQ(TranslatedValue::kUninitialized,
           properties_slot->materialization_state());

  Handle<ByteArray> object_storage = AllocateStorageFor(properties_slot);
  properties_slot->mark_allocated();
  properties_slot->set_storage(object_storage);

  DisallowGarbageCollection no_gc;
  Tagged<Map> raw_map = *map;
  Tagged<ByteArray> raw_object_storage = *object_storage;

  // Set markers for out-of-object properties.
  Tagged<DescriptorArray> descriptors = map->instance_descriptors(isolate());
  for (InternalIndex i : map->IterateOwnDescriptors()) {
    FieldIndex index = FieldIndex::ForDescriptor(raw_map, i);
    Representation representation = descriptors->GetDetails(i).representation();
    if (!index.is_inobject() &&
        (representation.IsDouble() || representation.IsHeapObject())) {
      int outobject_index = index.outobject_array_index();
      int array_index = outobject_index * kTaggedSize;
      raw_object_storage->set(array_index, kStoreHeapObject);
    }
  }
}

Handle<ByteArray> TranslatedState::AllocateStorageFor(TranslatedValue* slot) {
  int allocate_size =
      ByteArray::LengthFor(slot->GetChildrenCount() * kTaggedSize);
  // It is important to allocate all the objects tenured so that the marker
  // does not visit them.
  Handle<ByteArray> object_storage =
      isolate()->factory()->NewByteArray(allocate_size, AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  Tagged<ByteArray> raw_object_storage = *object_storage;
  for (int i = 0; i < object_storage->length(); i++) {
    raw_object_storage->set(i, kStoreTagged);
  }
  return object_storage;
}

void TranslatedState::EnsureJSObjectAllocated(TranslatedValue* slot,
                                              DirectHandle<Map> map) {
  CHECK(IsJSObjectMap(*map));
  CHECK_EQ(map->instance_size(), slot->GetChildrenCount() * kTaggedSize);

  Handle<ByteArray> object_storage = AllocateStorageFor(slot);

  // Now we handle the interesting (JSObject) case.
  DisallowGarbageCollection no_gc;
  Tagged<Map> raw_map = *map;
  Tagged<ByteArray> raw_object_storage = *object_storage;
  Tagged<DescriptorArray> descriptors = map->instance_descriptors(isolate());

  // Set markers for in-object properties.
  for (InternalIndex i : raw_map->IterateOwnDescriptors()) {
    FieldIndex index = FieldIndex::ForDescriptor(raw_map, i);
    Representation representation = descriptors->GetDetails(i).representation();
    if (index.is_inobject() &&
        (representation.IsDouble() || representation.IsHeapObject())) {
      CHECK_GE(index.index(), OFFSET_OF_DATA_START(FixedArray) / kTaggedSize);
      int array_index =
          index.index() * kTaggedSize - OFFSET_OF_DATA_START(FixedArray);
      raw_object_storage->set(array_index, kStoreHeapObject);
    }
  }
  slot->set_storage(object_storage);
}

TranslatedValue* TranslatedState::GetResolvedSlot(TranslatedFrame* frame,
                                                  int value_index) {
  TranslatedValue* slot = frame->ValueAt(value_index);
  if (slot->kind() == TranslatedValue::kDuplicatedObject) {
    slot = ResolveCapturedObject(slot);
  }
  CHECK_NE(slot->materialization_state(), TranslatedValue::kUninitialized);
  return slot;
}

TranslatedValue* TranslatedState::GetResolvedSlotAndAdvance(
    TranslatedFrame* frame, int* value_index) {
  TranslatedValue* slot = GetResolvedSlot(frame, *value_index);
  SkipSlots(1, frame, value_index);
  return slot;
}

DirectHandle<Object> TranslatedState::GetValueAndAdvance(TranslatedFrame* frame,
                                                         int* value_index) {
  TranslatedValue* slot = GetResolvedSlot(frame, *value_index);
  SkipSlots(1, frame, value_index);
  return slot->GetValue();
}

void TranslatedState::InitializeJSObjectAt(
    TranslatedFrame* frame, int* value_index, TranslatedValue* slot,
    DirectHandle<Map> map, const DisallowGarbageCollection& no_gc) {
  auto object_storage = Cast<HeapObject>(slot->storage_);
  DCHECK_EQ(TranslatedValue::kCapturedObject, slot->kind());
  int children_count = slot->GetChildrenCount();

  // The object should have at least a map and some payload.
  CHECK_GE(children_count, 2);

#if DEBUG
  // No need to invalidate slots in object because no slot was recorded yet.
  // Verify this here.
  Address object_start = object_storage->address();
  Address object_end = object_start + children_count * kTaggedSize;
  isolate()->heap()->VerifySlotRangeHasNoRecordedSlots(object_start,
                                                       object_end);
#endif  // DEBUG

  // Notify the concurrent marker about the layout change.
  isolate()->heap()->NotifyObjectLayoutChange(
      *object_storage, no_gc, InvalidateRecordedSlots::kNo,
      InvalidateExternalPointerSlots::kNo);

  // Finish any sweeping so that it becomes safe to overwrite the ByteArray
  // headers. See chromium:1228036.
  isolate()->heap()->EnsureSweepingCompletedForObject(*object_storage);

  // Fill the property array field.
  {
    DirectHandle<Object> properties = GetValueAndAdvance(frame, value_index);
    WRITE_FIELD(*object_storage, JSObject::kPropertiesOrHashOffset,
                *properties);
    WRITE_BARRIER(*object_storage, JSObject::kPropertiesOrHashOffset,
                  *properties);
  }

  // For all the other fields we first look at the fixed array and check the
  // marker to see if we store an unboxed double.
  DCHECK_EQ(kTaggedSize, JSObject::kPropertiesOrHashOffset);
  for (int i = 2; i < children_count; i++) {
    slot = GetResolvedSlotAndAdvance(frame, value_index);
    // Read out the marker and ensure the field is consistent with
    // what the markers in the storage say (note that all heap numbers
    // should be fully initialized by now).
    int offset = i * kTaggedSize;
    uint8_t marker = object_storage->ReadField<uint8_t>(offset);
    InstanceType instance_type = map->instance_type();
    USE(instance_type);
#ifdef V8_ENABLE_LEAPTIERING
    if (InstanceTypeChecker::IsJSFunction(instance_type) &&
        offset == JSFunction::kDispatchHandleOffset) {
      // The JSDispatchHandle will be materialized as a number, but we need
      // the raw value here. TODO(saelo): can we implement "proper" support
      // for JSDispatchHandles in the deoptimizer?
      DirectHandle<Object> field_value = slot->GetValue();
      CHECK(IsNumber(*field_value));
      JSDispatchHandle handle(Object::NumberValue(Cast<Number>(*field_value)));
      object_storage->WriteField<JSDispatchHandle::underlying_type>(
          JSFunction::kDispatchHandleOffset, handle.value());
      continue;
    }
#endif  // V8_ENABLE_LEAPTIERING
#ifdef V8_ENABLE_SANDBOX
    if (InstanceTypeChecker::IsJSRegExp(instance_type) &&
        offset == JSRegExp::kDataOffset) {
      DirectHandle<HeapObject> field_value = slot->storage();
      // If the value comes from the DeoptimizationLiteralArray, it is a
      // RegExpDataWrapper as we can't store TrustedSpace values in a FixedArray
      // directly.
      Tagged<RegExpData> value;
      if (Is<RegExpDataWrapper>(*field_value)) {
        value = Cast<RegExpDataWrapper>(*field_value)->data(isolate());
      } else {
        CHECK(IsRegExpData(*field_value));
        value = Cast<RegExpData>(*field_value);
      }
      object_storage
          ->RawIndirectPointerField(offset, kRegExpDataIndirectPointerTag)
          .Relaxed_Store(value);
      INDIRECT_POINTER_WRITE_BARRIER(*object_storage, offset,
                                     kRegExpDataIndirectPointerTag, value);
      continue;
    }
#endif  // V8_ENABLE_SANDBOX
    if (marker == kStoreHeapObject) {
      DirectHandle<HeapObject> field_value = slot->storage();
      WRITE_FIELD(*object_storage, offset, *field_value);
      WRITE_BARRIER(*object_storage, offset, *field_value);
      continue;
    }

    CHECK_EQ(kStoreTagged, marker);
    DirectHandle<Object> field_value = slot->GetValue();
    DCHECK_IMPLIES(IsHeapNumber(*field_value),
                   !IsSmiDouble(Object::NumberValue(*field_value)));
    WRITE_FIELD(*object_storage, offset, *field_value);
    WRITE_BARRIER(*object_storage, offset, *field_value);
  }
  object_storage->set_map(isolate(), *map, kReleaseStore);
}

void TranslatedState::InitializeObjectWithTaggedFieldsAt(
    TranslatedFrame* frame, int* value_index, TranslatedValue* slot,
    DirectHandle<Map> map, const DisallowGarbageCollection& no_gc) {
  auto object_storage = Cast<HeapObject>(slot->storage_);
  int children_count = slot->GetChildrenCount();

  // Skip the writes if we already have the canonical empty fixed array.
  if (*object_storage == ReadOnlyRoots(isolate()).empty_fixed_array()) {
    CHECK_EQ(2, children_count);
    DirectHandle<Object> length_value = GetValueAndAdvance(frame, value_index);
    CHECK_EQ(*length_value, Smi::FromInt(0));
    return;
  }

#if DEBUG
  // No need to invalidate slots in object because no slot was recorded yet.
  // Verify this here.
  Address object_start = object_storage->address();
  Address object_end = object_start + children_count * kTaggedSize;
  isolate()->heap()->VerifySlotRangeHasNoRecordedSlots(object_start,
                                                       object_end);
#endif  // DEBUG

  // Notify the concurrent marker about the layout change.
  isolate()->heap()->NotifyObjectLayoutChange(
      *object_storage, no_gc, InvalidateRecordedSlots::kNo,
      InvalidateExternalPointerSlots::kNo);

  // Finish any sweeping so that it becomes safe to overwrite the ByteArray
  // headers. See chromium:1228036.
  isolate()->heap()->EnsureSweepingCompletedForObject(*object_storage);

  // Write the fields to the object.
  for (int i = 1; i < children_count; i++) {
    slot = GetResolvedSlotAndAdvance(frame, value_index);
    int offset = i * kTaggedSize;
    uint8_t marker = object_storage->ReadField<uint8_t>(offset);
    DirectHandle<Object> field_value;
    if (i > 1 && marker == kStoreHeapObject) {
      field_value = slot->storage();
    } else {
      CHECK(marker == kStoreTagged || i == 1);
      field_value = slot->GetValue();
      DCHECK_IMPLIES(IsHeapNumber(*field_value),
                     !IsSmiDouble(Object::NumberValue(*field_value)));
    }
    WRITE_FIELD(*object_storage, offset, *field_value);
    WRITE_BARRIER(*object_storage, offset, *field_value);
  }

  object_storage->set_map(isolate(), *map, kReleaseStore);
}

TranslatedValue* TranslatedState::ResolveCapturedObject(TranslatedValue* slot) {
  while (slot->kind() == TranslatedValue::kDuplicatedObject) {
    slot = GetValueByObjectIndex(slot->object_index());
  }
  CHECK(slot->kind() == TranslatedValue::kCapturedObject ||
        slot->kind() == TranslatedValue::kCapturedStringConcat);
  return slot;
}

TranslatedFrame* TranslatedState::GetFrameFromJSFrameIndex(int jsframe_index) {
  for (size_t i = 0; i < frames_.size(); i++) {
    if (frames_[i].kind() == TranslatedFrame::kUnoptimizedFunction ||
        frames_[i].kind() == TranslatedFrame::kJavaScriptBuiltinContinuation ||
        frames_[i].kind() ==
            TranslatedFrame::kJavaScriptBuiltinContinuationWithCatch) {
      if (jsframe_index > 0) {
        jsframe_index--;
      } else {
        return &(frames_[i]);
      }
    }
  }
  return nullptr;
}

TranslatedFrame* TranslatedState::GetArgumentsInfoFromJSFrameIndex(
    int jsframe_index, int* args_count) {
  for (size_t i = 0; i < frames_.size(); i++) {
    if (frames_[i].kind() == TranslatedFrame::kUnoptimizedFunction ||
        frames_[i].kind() == TranslatedFrame::kJavaScriptBuiltinContinuation ||
        frames_[i].kind() ==
            TranslatedFrame::kJavaScriptBuiltinContinuationWithCatch) {
      if (jsframe_index > 0) {
        jsframe_index--;
      } else {
        // We have the JS function frame, now check if it has arguments
        // adaptor.
        if (i > 0 &&
            frames_[i - 1].kind() == TranslatedFrame::kInlinedExtraArguments) {
          *args_count = frames_[i - 1].height();
          return &(frames_[i - 1]);
        }

        // JavaScriptBuiltinContinuation frames that are not preceded by
        // a arguments adapter frame are currently only used by C++ API calls
        // from TurboFan. Calls to C++ API functions from TurboFan need
        // a special marker frame state, otherwise the API call wouldn't
        // be shown in a stack trace.
        if (frames_[i].kind() ==
            TranslatedFrame::kJavaScriptBuiltinContinuation) {
          DCHECK(frames_[i].shared_info()->IsDontAdaptArguments());
          DCHECK(frames_[i].shared_info()->IsApiFunction());

          // The argument count for this special case is always the second
          // to last value in the TranslatedFrame. It should also always be
          // {1}, as the GenericLazyDeoptContinuation builtin has one explicit
          // argument (the result).
          static constexpr int kTheContext = 1;
          const uint32_t height = frames_[i].height() + kTheContext;
          *args_count = frames_[i].ValueAt(height - 1)->GetSmiValue();
          DCHECK_EQ(*args_count, JSParameterCount(1));
          return &(frames_[i]);
        }

        DCHECK_EQ(frames_[i].kind(), TranslatedFrame::kUnoptimizedFunction);
        *args_count = frames_[i].bytecode_array()->parameter_count();
        return &(frames_[i]);
      }
    }
  }
  return nullptr;
}

void TranslatedState::StoreMaterializedValuesAndDeopt(JavaScriptFrame* frame) {
  MaterializedObjectStore* materialized_store =
      isolate_->materialized_object_store();
  DirectHandle<FixedArray> previously_materialized_objects =
      materialized_store->Get(stack_frame_pointer_);

  Handle<Object> marker = isolate_->factory()->arguments_marker();

  int length = static_cast<int>(object_positions_.size());
  bool new_store = false;
  if (previously_materialized_objects.is_null()) {
    previously_materialized_objects =
        isolate_->factory()->NewFixedArray(length, AllocationType::kOld);
    for (int i = 0; i < length; i++) {
      previously_materialized_objects->set(i, *marker);
    }
    new_store = true;
  }

  CHECK_EQ(length, previously_materialized_objects->length());

  bool value_changed = false;
  for (int i = 0; i < length; i++) {
    TranslatedState::ObjectPosition pos = object_positions_[i];
    TranslatedValue* value_info =
        &(frames_[pos.frame_index_].values_[pos.value_index_]);

    CHECK(value_info->IsMaterializedObject());

    // Skip duplicate objects (i.e., those that point to some other object id).
    if (value_info->object_index() != i) continue;

    DirectHandle<Object> previous_value(previously_materialized_objects->get(i),
                                        isolate_);
    DirectHandle<Object> value(value_info->GetRawValue(), isolate_);

    if (value.is_identical_to(marker)) {
      DCHECK_EQ(*previous_value, *marker);
    } else {
      if (*previous_value == *marker) {
        if (IsSmi(*value)) {
          value =
              isolate()->factory()->NewHeapNumber(Object::NumberValue(*value));
        }
        previously_materialized_objects->set(i, *value);
        value_changed = true;
      } else {
        CHECK(*previous_value == *value ||
              (IsHeapNumber(*previous_value) && IsSmi(*value) &&
               Object::NumberValue(*previous_value) ==
                   Object::NumberValue(*value)));
      }
    }
  }

  if (new_store && value_changed) {
    materialized_store->Set(stack_frame_pointer_,
                            previously_materialized_objects);
    CHECK_EQ(frames_[0].kind(), TranslatedFrame::kUnoptimizedFunction);
    CHECK_EQ(frame->function(), frames_[0].front().GetRawValue());
    Deoptimizer::DeoptimizeFunction(
        frame->function(), LazyDeoptimizeReason::kFrameValueMaterialized,
        frame->LookupCode());
  }
}

void TranslatedState::UpdateFromPreviouslyMaterializedObjects() {
  MaterializedObjectStore* materialized_store =
      isolate_->materialized_object_store();
  DirectHandle<FixedArray> previously_materialized_objects =
      materialized_store->Get(stack_frame_pointer_);

  // If we have no previously materialized objects, there is nothing to do.
  if (previously_materialized_objects.is_null()) return;

  DirectHandle<Object> marker = isolate_->factory()->arguments_marker();

  int length = static_cast<int>(object_positions_.size());
  CHECK_EQ(length, previously_materialized_objects->length());

  for (int i = 0; i < length; i++) {
    // For a previously materialized objects, inject their value into the
    // translated values.
    if (previously_materialized_objects->get(i) != *marker) {
      TranslatedState::ObjectPosition pos = object_positions_[i];
      TranslatedValue* value_info =
          &(frames_[pos.frame_index_].values_[pos.value_index_]);
      CHECK(value_info->IsMaterializedObject());

      if (value_info->kind() == TranslatedValue::kCapturedObject ||
          value_info->kind() == TranslatedValue::kCapturedStringConcat) {
        Handle<Object> object(previously_materialized_objects->get(i),
                              isolate_);
        CHECK(IsHeapObject(*object));
        value_info->set_initialized_storage(Cast<HeapObject>(object));
      }
    }
  }
}

void TranslatedState::VerifyMaterializedObjects() {
#if VERIFY_HEAP
  if (!v8_flags.verify_heap) return;
  int length = static_cast<int>(object_positions_.size());
  for (int i = 0; i < length; i++) {
    TranslatedValue* slot = GetValueByObjectIndex(i);
    if (slot->kind() == TranslatedValue::kCapturedObject ||
        slot->kind() == TranslatedValue::kCapturedStringConcat) {
      CHECK_EQ(slot, GetValueByObjectIndex(slot->object_index()));
      if (slot->materialization_state() == TranslatedValue::kFinished) {
        Object::ObjectVerify(*slot->storage(), isolate());
      } else {
        CHECK_EQ(slot->materialization_state(),
                 TranslatedValue::kUninitialized);
      }
    }
  }
#endif
}

bool TranslatedState::DoUpdateFeedback() {
  if (!feedback_vector_handle_.is_null()) {
    CHECK(!feedback_slot_.IsInvalid());
    isolate()->CountUsage(v8::Isolate::kDeoptimizerDisableSpeculation);
    FeedbackNexus nexus(isolate(), feedback_vector_handle_, feedback_slot_);
    nexus.SetSpeculationMode(SpeculationMode::kDisallowSpeculation);
    return true;
  }
  return false;
}

void TranslatedState::ReadUpdateFeedback(
    DeoptTranslationIterator* iterator,
    Tagged<DeoptimizationLiteralArray> literal_array, FILE* trace_file) {
  CHECK_EQ(TranslationOpcode::UPDATE_FEEDBACK, iterator->NextOpcode());
  feedback_vector_ =
      Cast<FeedbackVector>(literal_array->get(iterator->NextOperand()));
  feedback_slot_ = FeedbackSlot(iterator->NextOperand());
  if (trace_file != nullptr) {
    PrintF(trace_file, "  reading FeedbackVector (slot %d)\n",
           feedback_slot_.ToInt());
  }
}

}  // namespace internal
}  // namespace v8

// Undefine the heap manipulation macros.
#include "src/objects/object-macros-undef.h"
