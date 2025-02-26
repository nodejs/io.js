// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TURBOFAN_TYPES_INL_H_
#define V8_OBJECTS_TURBOFAN_TYPES_INL_H_

#include "src/heap/heap-layout-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/objects/objects-inl.h"
#include "src/objects/turbofan-types.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/turbofan-types-tq-inl.inc"

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TURBOFAN_TYPES_INL_H_
