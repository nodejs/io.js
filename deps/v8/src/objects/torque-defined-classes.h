// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_OBJECTS_TORQUE_DEFINED_CLASSES_H_
#define V8_OBJECTS_TORQUE_DEFINED_CLASSES_H_

#include "src/objects/arguments.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-object.h"
#include "src/objects/megadom-handler.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Oddball;

#include "torque-generated/src/objects/torque-defined-classes-tq.inc"

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TORQUE_DEFINED_CLASSES_H_
