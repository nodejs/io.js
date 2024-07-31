// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ABSTRACT_CODE_INL_H_
#define V8_OBJECTS_ABSTRACT_CODE_INL_H_

#include "src/objects/abstract-code.h"
#include "src/objects/bytecode-array-inl.h"
#include "src/objects/code-inl.h"
#include "src/objects/instance-type-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(AbstractCode, HeapObject)
CAST_ACCESSOR(AbstractCode)

int AbstractCode::InstructionSize(PtrComprCageBase cage_base) {
  Tagged<Map> map_object = map(cage_base);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->instruction_size();
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return GetBytecodeArray()->length();
  }
}

Tagged<TrustedByteArray> AbstractCode::SourcePositionTableInternal(
    Isolate* isolate) {
  Tagged<Map> map_object = map();
  if (InstanceTypeChecker::IsCode(map_object)) {
    Tagged<Code> code = GetCode();
    if (!code->has_instruction_stream()) {
      return *isolate->factory()->empty_trusted_byte_array();
    }
    return code->source_position_table();
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return GetBytecodeArray()->SourcePositionTable();
  }
}

Tagged<TrustedByteArray> AbstractCode::SourcePositionTable(
    Isolate* isolate, Tagged<SharedFunctionInfo> sfi) {
  Tagged<Map> map_object = map(isolate);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->SourcePositionTable(isolate, sfi);
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return GetBytecodeArray()->SourcePositionTable(isolate);
  }
}

int AbstractCode::SizeIncludingMetadata(PtrComprCageBase cage_base) {
  Tagged<Map> map_object = map(cage_base);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->SizeIncludingMetadata();
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return GetBytecodeArray()->SizeIncludingMetadata();
  }
}

Address AbstractCode::InstructionStart(PtrComprCageBase cage_base) {
  Tagged<Map> map_object = map(cage_base);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->instruction_start();
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return GetBytecodeArray()->GetFirstBytecodeAddress();
  }
}

Address AbstractCode::InstructionEnd(PtrComprCageBase cage_base) {
  Tagged<Map> map_object = map(cage_base);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->instruction_end();
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    Tagged<BytecodeArray> bytecode_array = GetBytecodeArray();
    return bytecode_array->GetFirstBytecodeAddress() + bytecode_array->length();
  }
}

bool AbstractCode::contains(Isolate* isolate, Address inner_pointer) {
  PtrComprCageBase cage_base(isolate);
  Tagged<Map> map_object = map(cage_base);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->contains(isolate, inner_pointer);
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return (address() <= inner_pointer) &&
           (inner_pointer <= address() + Size(cage_base));
  }
}

CodeKind AbstractCode::kind(PtrComprCageBase cage_base) {
  Tagged<Map> map_object = map(cage_base);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->kind();
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return CodeKind::INTERPRETED_FUNCTION;
  }
}

Builtin AbstractCode::builtin_id(PtrComprCageBase cage_base) {
  Tagged<Map> map_object = map(cage_base);
  if (InstanceTypeChecker::IsCode(map_object)) {
    return GetCode()->builtin_id();
  } else {
    DCHECK(InstanceTypeChecker::IsBytecodeArray(map_object));
    return Builtin::kNoBuiltinId;
  }
}

bool AbstractCode::has_instruction_stream(PtrComprCageBase cage_base) {
  DCHECK(InstanceTypeChecker::IsCode(map(cage_base)));
  return GetCode()->has_instruction_stream();
}

Tagged<Code> AbstractCode::GetCode() { return Code::cast(*this); }

Tagged<BytecodeArray> AbstractCode::GetBytecodeArray() {
  return BytecodeArray::cast(*this);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ABSTRACT_CODE_INL_H_
