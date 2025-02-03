// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_BYTECODE_ARRAY_INL_H_
#define V8_OBJECTS_BYTECODE_ARRAY_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/interpreter/bytecode-register.h"
#include "src/objects/bytecode-array.h"
#include "src/objects/fixed-array-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(BytecodeArray, ExposedTrustedObject)

SMI_ACCESSORS(BytecodeArray, length, kLengthOffset)
RELEASE_ACQUIRE_SMI_ACCESSORS(BytecodeArray, length, kLengthOffset)
PROTECTED_POINTER_ACCESSORS(BytecodeArray, handler_table, TrustedByteArray,
                            kHandlerTableOffset)
PROTECTED_POINTER_ACCESSORS(BytecodeArray, constant_pool, TrustedFixedArray,
                            kConstantPoolOffset)
ACCESSORS(BytecodeArray, wrapper, Tagged<BytecodeWrapper>, kWrapperOffset)
RELEASE_ACQUIRE_PROTECTED_POINTER_ACCESSORS(BytecodeArray,
                                            source_position_table,
                                            TrustedByteArray,
                                            kSourcePositionTableOffset)

uint8_t BytecodeArray::get(int index) const {
  DCHECK(index >= 0 && index < length());
  return ReadField<uint8_t>(kHeaderSize + index * kCharSize);
}

void BytecodeArray::set(int index, uint8_t value) {
  DCHECK(index >= 0 && index < length());
  WriteField<uint8_t>(kHeaderSize + index * kCharSize, value);
}

void BytecodeArray::set_frame_size(int32_t frame_size) {
  DCHECK_GE(frame_size, 0);
  DCHECK(IsAligned(frame_size, kSystemPointerSize));
  WriteField<int32_t>(kFrameSizeOffset, frame_size);
}

int32_t BytecodeArray::frame_size() const {
  return ReadField<int32_t>(kFrameSizeOffset);
}

int BytecodeArray::register_count() const {
  return static_cast<int>(frame_size()) / kSystemPointerSize;
}

uint16_t BytecodeArray::parameter_count() const {
  return ReadField<uint16_t>(kParameterSizeOffset);
}

void BytecodeArray::set_parameter_count(uint16_t number_of_parameters) {
  WriteField<uint16_t>(kParameterSizeOffset, number_of_parameters);
}

uint16_t BytecodeArray::max_arguments() const {
  return ReadField<uint16_t>(kMaxArgumentsOffset);
}

void BytecodeArray::set_max_arguments(uint16_t max_arguments) {
  WriteField<uint16_t>(kMaxArgumentsOffset, max_arguments);
}

int32_t BytecodeArray::max_frame_size() const {
  return frame_size() + (max_arguments() << kSystemPointerSizeLog2);
}

interpreter::Register BytecodeArray::incoming_new_target_or_generator_register()
    const {
  int32_t register_operand =
      ReadField<int32_t>(kIncomingNewTargetOrGeneratorRegisterOffset);
  if (register_operand == 0) {
    return interpreter::Register::invalid_value();
  } else {
    return interpreter::Register::FromOperand(register_operand);
  }
}

void BytecodeArray::set_incoming_new_target_or_generator_register(
    interpreter::Register incoming_new_target_or_generator_register) {
  if (!incoming_new_target_or_generator_register.is_valid()) {
    WriteField<int32_t>(kIncomingNewTargetOrGeneratorRegisterOffset, 0);
  } else {
    DCHECK(incoming_new_target_or_generator_register.index() <
           register_count());
    DCHECK_NE(0, incoming_new_target_or_generator_register.ToOperand());
    WriteField<int32_t>(kIncomingNewTargetOrGeneratorRegisterOffset,
                        incoming_new_target_or_generator_register.ToOperand());
  }
}

void BytecodeArray::clear_padding() {
  int data_size = kHeaderSize + length();
  memset(reinterpret_cast<void*>(address() + data_size), 0,
         SizeFor(length()) - data_size);
}

Address BytecodeArray::GetFirstBytecodeAddress() {
  return ptr() - kHeapObjectTag + kHeaderSize;
}

bool BytecodeArray::HasSourcePositionTable() const {
  return has_source_position_table(kAcquireLoad);
}

DEF_GETTER(BytecodeArray, SourcePositionTable, Tagged<TrustedByteArray>) {
  // WARNING: This function may be called from a background thread, hence
  // changes to how it accesses the heap can easily lead to bugs.
  Tagged<Object> maybe_table = raw_source_position_table(kAcquireLoad);
  if (IsTrustedByteArray(maybe_table))
    return Cast<TrustedByteArray>(maybe_table);
  DCHECK_EQ(maybe_table, Smi::zero());
  return GetIsolateFromWritableObject(*this)
      ->heap()
      ->empty_trusted_byte_array();
}

void BytecodeArray::SetSourcePositionsFailedToCollect() {
  TaggedField<Object>::Release_Store(*this, kSourcePositionTableOffset,
                                     Smi::zero());
}

DEF_GETTER(BytecodeArray, raw_constant_pool, Tagged<Object>) {
  Tagged<Object> value = RawProtectedPointerField(kConstantPoolOffset).load();
  // This field might be 0 during deserialization.
  DCHECK(value == Smi::zero() || IsTrustedFixedArray(value));
  return value;
}

DEF_GETTER(BytecodeArray, raw_handler_table, Tagged<Object>) {
  Tagged<Object> value = RawProtectedPointerField(kHandlerTableOffset).load();
  // This field might be 0 during deserialization.
  DCHECK(value == Smi::zero() || IsTrustedByteArray(value));
  return value;
}

DEF_ACQUIRE_GETTER(BytecodeArray, raw_source_position_table, Tagged<Object>) {
  Tagged<Object> value =
      RawProtectedPointerField(kSourcePositionTableOffset).Acquire_Load();
  // This field might be 0 during deserialization or if source positions have
  // not been (successfully) collected.
  DCHECK(value == Smi::zero() || IsTrustedByteArray(value));
  return value;
}

int BytecodeArray::BytecodeArraySize() const { return SizeFor(this->length()); }

DEF_GETTER(BytecodeArray, SizeIncludingMetadata, int) {
  int size = BytecodeArraySize();
  Tagged<Object> maybe_constant_pool = raw_constant_pool(cage_base);
  if (IsTrustedFixedArray(maybe_constant_pool)) {
    size += Cast<TrustedFixedArray>(maybe_constant_pool)->Size(cage_base);
  } else {
    DCHECK_EQ(maybe_constant_pool, Smi::zero());
  }
  Tagged<Object> maybe_handler_table = raw_handler_table(cage_base);
  if (IsTrustedByteArray(maybe_handler_table)) {
    size += Cast<TrustedByteArray>(maybe_handler_table)->AllocatedSize();
  } else {
    DCHECK_EQ(maybe_handler_table, Smi::zero());
  }
  Tagged<Object> maybe_table = raw_source_position_table(kAcquireLoad);
  if (IsByteArray(maybe_table)) {
    size += Cast<ByteArray>(maybe_table)->AllocatedSize();
  }
  return size;
}

OBJECT_CONSTRUCTORS_IMPL(BytecodeWrapper, Struct)

TRUSTED_POINTER_ACCESSORS(BytecodeWrapper, bytecode, BytecodeArray,
                          kBytecodeOffset, kBytecodeArrayIndirectPointerTag)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_BYTECODE_ARRAY_INL_H_
