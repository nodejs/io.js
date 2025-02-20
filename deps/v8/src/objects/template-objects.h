// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TEMPLATE_OBJECTS_H_
#define V8_OBJECTS_TEMPLATE_OBJECTS_H_

#include "src/objects/fixed-array.h"
#include "src/objects/struct.h"
#include "src/objects/torque-defined-classes.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Oddball;
class StructBodyDescriptor;

#include "torque-generated/src/objects/template-objects-tq.inc"

// TemplateObjectDescription is a tuple of raw strings and cooked strings for
// tagged template literals. Used to communicate with the runtime for template
// object creation within the {Runtime_GetTemplateObject} method.
class TemplateObjectDescription final
    : public TorqueGeneratedTemplateObjectDescription<TemplateObjectDescription,
                                                      Struct> {
 public:
  static DirectHandle<JSArray> GetTemplateObject(
      Isolate* isolate, DirectHandle<NativeContext> native_context,
      DirectHandle<TemplateObjectDescription> description,
      DirectHandle<SharedFunctionInfo> shared_info, int slot_id);

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(TemplateObjectDescription)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TEMPLATE_OBJECTS_H_
