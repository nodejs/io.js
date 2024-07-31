// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_REGEXP_INL_H_
#define V8_OBJECTS_JS_REGEXP_INL_H_

#include "src/objects/js-regexp.h"

#include "src/objects/js-array-inl.h"
#include "src/objects/objects-inl.h"  // Needed for write barriers
#include "src/objects/smi.h"
#include "src/objects/string.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-regexp-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSRegExp)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSRegExpResult)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSRegExpResultIndices)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSRegExpResultWithIndices)

ACCESSORS(JSRegExp, last_index, Tagged<Object>, kLastIndexOffset)

JSRegExp::Type JSRegExp::type_tag() const {
  Tagged<Object> data = this->data();
  if (IsUndefined(data)) return JSRegExp::NOT_COMPILED;
  Tagged<Smi> smi = Smi::cast(FixedArray::cast(data)->get(kTagIndex));
  return static_cast<JSRegExp::Type>(smi.value());
}

int JSRegExp::capture_count() const {
  switch (type_tag()) {
    case ATOM:
      return 0;
    case EXPERIMENTAL:
    case IRREGEXP:
      return Smi::ToInt(DataAt(kIrregexpCaptureCountIndex));
    default:
      UNREACHABLE();
  }
}

int JSRegExp::max_register_count() const {
  CHECK_EQ(type_tag(), IRREGEXP);
  return Smi::ToInt(DataAt(kIrregexpMaxRegisterCountIndex));
}

Tagged<String> JSRegExp::atom_pattern() const {
  DCHECK_EQ(type_tag(), ATOM);
  return String::cast(DataAt(JSRegExp::kAtomPatternIndex));
}

Tagged<String> JSRegExp::source() const {
  return String::cast(TorqueGeneratedClass::source());
}

JSRegExp::Flags JSRegExp::flags() const {
  Tagged<Smi> smi = Smi::cast(TorqueGeneratedClass::flags());
  return Flags(smi.value());
}

// static
const char* JSRegExp::FlagsToString(Flags flags, FlagsBuffer* out_buffer) {
  int cursor = 0;
  FlagsBuffer& buffer = *out_buffer;
#define V(Lower, Camel, LowerCamel, Char, Bit) \
  if (flags & JSRegExp::k##Camel) buffer[cursor++] = Char;
  REGEXP_FLAG_LIST(V)
#undef V
  buffer[cursor++] = '\0';
  return buffer.begin();
}

Tagged<String> JSRegExp::EscapedPattern() {
  DCHECK(IsString(source()));
  return String::cast(source());
}

Tagged<Object> JSRegExp::capture_name_map() {
  DCHECK(TypeSupportsCaptures(type_tag()));
  Tagged<Object> value = DataAt(kIrregexpCaptureNameMapIndex);
  DCHECK_NE(value, Smi::FromInt(JSRegExp::kUninitializedValue));
  return value;
}

void JSRegExp::set_capture_name_map(Handle<FixedArray> capture_name_map) {
  if (capture_name_map.is_null()) {
    SetDataAt(JSRegExp::kIrregexpCaptureNameMapIndex, Smi::zero());
  } else {
    SetDataAt(JSRegExp::kIrregexpCaptureNameMapIndex, *capture_name_map);
  }
}

Tagged<Object> JSRegExp::DataAt(int index) const {
  DCHECK(type_tag() != NOT_COMPILED);
  return FixedArray::cast(data())->get(index);
}

void JSRegExp::SetDataAt(int index, Tagged<Object> value) {
  DCHECK(type_tag() != NOT_COMPILED);
  // Only implementation data can be set this way.
  DCHECK_GE(index, kFirstTypeSpecificIndex);
  FixedArray::cast(data())->set(index, value);
}

bool JSRegExp::HasCompiledCode() const {
  if (type_tag() != IRREGEXP) return false;
  Tagged<Smi> uninitialized = Smi::FromInt(kUninitializedValue);
#ifdef DEBUG
  DCHECK(IsCodeWrapper(DataAt(kIrregexpLatin1CodeIndex)) ||
         DataAt(kIrregexpLatin1CodeIndex) == uninitialized);
  DCHECK(IsCodeWrapper(DataAt(kIrregexpUC16CodeIndex)) ||
         DataAt(kIrregexpUC16CodeIndex) == uninitialized);
  DCHECK(IsByteArray(DataAt(kIrregexpLatin1BytecodeIndex)) ||
         DataAt(kIrregexpLatin1BytecodeIndex) == uninitialized);
  DCHECK(IsByteArray(DataAt(kIrregexpUC16BytecodeIndex)) ||
         DataAt(kIrregexpUC16BytecodeIndex) == uninitialized);
#endif  // DEBUG
  return (DataAt(kIrregexpLatin1CodeIndex) != uninitialized ||
          DataAt(kIrregexpUC16CodeIndex) != uninitialized);
}

void JSRegExp::DiscardCompiledCodeForSerialization() {
  DCHECK(HasCompiledCode());
  Tagged<Smi> uninitialized = Smi::FromInt(kUninitializedValue);
  SetDataAt(kIrregexpLatin1CodeIndex, uninitialized);
  SetDataAt(kIrregexpUC16CodeIndex, uninitialized);
  SetDataAt(kIrregexpLatin1BytecodeIndex, uninitialized);
  SetDataAt(kIrregexpUC16BytecodeIndex, uninitialized);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_REGEXP_INL_H_
