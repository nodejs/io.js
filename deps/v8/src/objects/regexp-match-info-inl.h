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

CAST_ACCESSOR(RegExpMatchInfo)
OBJECT_CONSTRUCTORS_IMPL(RegExpMatchInfo, RegExpMatchInfo::Super)

SMI_ACCESSORS(RegExpMatchInfo, number_of_capture_registers,
              RegExpMatchInfo::kNumberOfCaptureRegistersOffset)
ACCESSORS_NOCAGE(RegExpMatchInfo, last_subject, Tagged<String>,
                 RegExpMatchInfo::kLastSubjectOffset)
ACCESSORS_NOCAGE(RegExpMatchInfo, last_input, Tagged<Object>,
                 RegExpMatchInfo::kLastInputOffset)

int RegExpMatchInfo::capture(int index) const { return get(index).value(); }

void RegExpMatchInfo::set_capture(int index, int value) {
  set(index, Smi::FromInt(value));
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_REGEXP_MATCH_INFO_INL_H_
