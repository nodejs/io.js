// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MEGADOM_HANDLER_H_
#define V8_OBJECTS_MEGADOM_HANDLER_H_

#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/megadom-handler-tq.inc"

class MegaDomHandler
    : public TorqueGeneratedMegaDomHandler<MegaDomHandler, HeapObject> {
 public:
  void BriefPrintDetails(std::ostream& os);
  class BodyDescriptor;

  DECL_RELEASE_ACQUIRE_ACCESSORS(accessor, Tagged<MaybeObject>)

  TQ_OBJECT_CONSTRUCTORS(MegaDomHandler)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_MEGADOM_HANDLER_H_
