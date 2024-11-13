// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ITERATOR_HELPERS_INL_H_
#define V8_OBJECTS_JS_ITERATOR_HELPERS_INL_H_

#include "src/objects/js-iterator-helpers.h"
#include "src/objects/oddball-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-iterator-helpers-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSIteratorHelper)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSIteratorMapHelper)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSIteratorFilterHelper)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSIteratorTakeHelper)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSIteratorDropHelper)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSIteratorFlatMapHelper)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ITERATOR_HELPERS_INL_H_
