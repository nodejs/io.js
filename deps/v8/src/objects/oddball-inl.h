// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ODDBALL_INL_H_
#define V8_OBJECTS_ODDBALL_INL_H_

#include "src/handles/handles.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/primitive-heap-object-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

double Oddball::to_number_raw() const { return to_number_raw_.value(); }
void Oddball::set_to_number_raw(double value) {
  to_number_raw_.set_value(value);
}

void Oddball::set_to_number_raw_as_bits(uint64_t bits) {
  // Bug(v8:8875): HeapNumber's double may be unaligned.
  to_number_raw_.set_value_as_bits(bits);
}

Tagged<String> Oddball::to_string() const { return to_string_.load(); }
void Oddball::set_to_string(Tagged<String> value, WriteBarrierMode mode) {
  to_string_.store(this, value);
}

Tagged<Number> Oddball::to_number() const { return to_number_.load(); }
void Oddball::set_to_number(Tagged<Number> value, WriteBarrierMode mode) {
  to_number_.store(this, value);
}

Tagged<String> Oddball::type_of() const { return type_of_.load(); }
void Oddball::set_type_of(Tagged<String> value, WriteBarrierMode mode) {
  type_of_.store(this, value);
}

uint8_t Oddball::kind() const { return kind_.load().value(); }

void Oddball::set_kind(uint8_t value) {
  kind_.store(this, Smi::FromInt(value));
}

// static
Handle<Number> Oddball::ToNumber(Isolate* isolate,
                                 DirectHandle<Oddball> input) {
  return handle(input->to_number(), isolate);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsBoolean) {
  return IsOddball(obj, cage_base) &&
         ((Cast<Oddball>(obj)->kind() & Oddball::kNotBooleanMask) == 0);
}

bool Boolean::ToBool(Isolate* isolate) const {
  DCHECK(IsBoolean(this, isolate));
  return IsTrue(this, isolate);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ODDBALL_INL_H_
