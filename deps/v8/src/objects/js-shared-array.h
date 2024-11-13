// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_SHARED_ARRAY_H_
#define V8_OBJECTS_JS_SHARED_ARRAY_H_

#include "src/objects/js-objects.h"
#include "src/objects/js-struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-shared-array-tq.inc"

class JSSharedArray
    : public TorqueGeneratedJSSharedArray<JSSharedArray,
                                          AlwaysSharedSpaceJSObject> {
 public:
  DECL_PRINTER(JSSharedArray)
  EXPORT_DECL_VERIFIER(JSSharedArray)

  // In-object fields.
  enum {
    // The length field is constant and is equal to elements().length().
    //
    // TODO(v8:12547): We can save the space for this field by making it
    // possible to put AccessorInfo in shared or RO space.
    kLengthFieldIndex = 0,
    kInObjectFieldCount,
  };
  static constexpr int kSize =
      kHeaderSize + (kTaggedSize * kInObjectFieldCount);

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSSharedArray)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_SHARED_ARRAY_H_
