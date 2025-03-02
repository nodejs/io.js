// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_REGEXP_MATCH_INFO_INL_H_
#define V8_OBJECTS_REGEXP_MATCH_INFO_INL_H_

#include "src/objects/fixed-array-inl.h"
#include "src/objects/regexp-match-info.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

int RegExpMatchInfo::number_of_capture_registers() const {
  return number_of_capture_registers_.load().value();
}
void RegExpMatchInfo::set_number_of_capture_registers(int value) {
  number_of_capture_registers_.store(this, Smi::FromInt(value));
}

Tagged<String> RegExpMatchInfo::last_subject() const {
  return last_subject_.load();
}
void RegExpMatchInfo::set_last_subject(Tagged<String> value,
                                       WriteBarrierMode mode) {
  last_subject_.store(this, value, mode);
}

Tagged<Object> RegExpMatchInfo::last_input() const {
  return last_input_.load();
}
void RegExpMatchInfo::set_last_input(Tagged<Object> value,
                                     WriteBarrierMode mode) {
  last_input_.store(this, value, mode);
}

int RegExpMatchInfo::capture(int index) const { return get(index).value(); }

void RegExpMatchInfo::set_capture(int index, int value) {
  set(index, Smi::FromInt(value));
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_REGEXP_MATCH_INFO_INL_H_
