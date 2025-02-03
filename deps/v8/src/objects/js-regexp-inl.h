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

OBJECT_CONSTRUCTORS_IMPL(RegExpData, ExposedTrustedObject)
OBJECT_CONSTRUCTORS_IMPL(AtomRegExpData, RegExpData)
OBJECT_CONSTRUCTORS_IMPL(IrRegExpData, RegExpData)
OBJECT_CONSTRUCTORS_IMPL(RegExpDataWrapper, Struct)

ACCESSORS(JSRegExp, last_index, Tagged<Object>, kLastIndexOffset)

Tagged<String> JSRegExp::source() const {
  return Cast<String>(TorqueGeneratedClass::source());
}

JSRegExp::Flags JSRegExp::flags() const {
  Tagged<Smi> smi = Cast<Smi>(TorqueGeneratedClass::flags());
  return Flags(smi.value());
}

TRUSTED_POINTER_ACCESSORS(JSRegExp, data, RegExpData, kDataOffset,
                          kRegExpDataIndirectPointerTag)

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
  return Cast<String>(source());
}

RegExpData::Type RegExpData::type_tag() const {
  Tagged<Smi> value = TaggedField<Smi, kTypeTagOffset>::load(*this);
  return Type(value.value());
}

void RegExpData::set_type_tag(Type type) {
  TaggedField<Smi, kTypeTagOffset>::store(
      *this, Smi::FromInt(static_cast<uint8_t>(type)));
}

ACCESSORS(RegExpData, source, Tagged<String>, kSourceOffset)

JSRegExp::Flags RegExpData::flags() const {
  Tagged<Smi> value = TaggedField<Smi, kFlagsOffset>::load(*this);
  return JSRegExp::Flags(value.value());
}

void RegExpData::set_flags(JSRegExp::Flags flags) {
  TaggedField<Smi, kFlagsOffset>::store(*this, Smi::FromInt(flags));
}

ACCESSORS(RegExpData, wrapper, Tagged<RegExpDataWrapper>, kWrapperOffset)

int RegExpData::capture_count() const {
  switch (type_tag()) {
    case Type::ATOM:
      return 0;
    case Type::EXPERIMENTAL:
    case Type::IRREGEXP:
      return Cast<IrRegExpData>(*this)->capture_count();
  }
}

TRUSTED_POINTER_ACCESSORS(RegExpDataWrapper, data, RegExpData, kDataOffset,
                          kRegExpDataIndirectPointerTag)

ACCESSORS(AtomRegExpData, pattern, Tagged<String>, kPatternOffset)

CODE_POINTER_ACCESSORS(IrRegExpData, latin1_code, kLatin1CodeOffset)
CODE_POINTER_ACCESSORS(IrRegExpData, uc16_code, kUc16CodeOffset)
bool IrRegExpData::has_code(bool is_one_byte) const {
  return is_one_byte ? has_latin1_code() : has_uc16_code();
}
void IrRegExpData::set_code(bool is_one_byte, Tagged<Code> code) {
  if (is_one_byte) {
    set_latin1_code(code);
  } else {
    set_uc16_code(code);
  }
}
Tagged<Code> IrRegExpData::code(IsolateForSandbox isolate,
                                bool is_one_byte) const {
  return is_one_byte ? latin1_code(isolate) : uc16_code(isolate);
}
PROTECTED_POINTER_ACCESSORS(IrRegExpData, latin1_bytecode, TrustedByteArray,
                            kLatin1BytecodeOffset)
PROTECTED_POINTER_ACCESSORS(IrRegExpData, uc16_bytecode, TrustedByteArray,
                            kUc16BytecodeOffset)
bool IrRegExpData::has_bytecode(bool is_one_byte) const {
  return is_one_byte ? has_latin1_bytecode() : has_uc16_bytecode();
}
void IrRegExpData::clear_bytecode(bool is_one_byte) {
  if (is_one_byte) {
    clear_latin1_bytecode();
  } else {
    clear_uc16_bytecode();
  }
}
void IrRegExpData::set_bytecode(bool is_one_byte,
                                Tagged<TrustedByteArray> bytecode) {
  if (is_one_byte) {
    set_latin1_bytecode(bytecode);
  } else {
    set_uc16_bytecode(bytecode);
  }
}
Tagged<TrustedByteArray> IrRegExpData::bytecode(bool is_one_byte) const {
  return is_one_byte ? latin1_bytecode() : uc16_bytecode();
}
ACCESSORS(IrRegExpData, capture_name_map, Tagged<Object>, kCaptureNameMapOffset)
void IrRegExpData::set_capture_name_map(Handle<FixedArray> capture_name_map) {
  if (capture_name_map.is_null()) {
    set_capture_name_map(Smi::zero());
  } else {
    set_capture_name_map(*capture_name_map);
  }
}

SMI_ACCESSORS(IrRegExpData, max_register_count, kMaxRegisterCountOffset)
SMI_ACCESSORS(IrRegExpData, capture_count, kCaptureCountOffset)
SMI_ACCESSORS(IrRegExpData, ticks_until_tier_up, kTicksUntilTierUpOffset)
SMI_ACCESSORS(IrRegExpData, backtrack_limit, kBacktrackLimitOffset)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_REGEXP_INL_H_
