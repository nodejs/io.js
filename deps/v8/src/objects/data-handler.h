// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DATA_HANDLER_H_
#define V8_OBJECTS_DATA_HANDLER_H_

#include "src/objects/struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/data-handler-tq.inc"

// DataHandler is a base class for load and store handlers that can't be
// encoded in one Smi. Kind of a handler can be deduced from instance type.
class DataHandler : public TorqueGeneratedDataHandler<DataHandler, Struct> {
 public:
  // Returns number of optional data fields available in the object.
  inline int data_field_count() const;

  // [data1-3]: These are optional general-purpose fields whose content and
  // presence depends on the handler kind.
  DECL_ACCESSORS(data1, Tagged<MaybeObject>)
  DECL_ACCESSORS(data2, Tagged<MaybeObject>)
  DECL_ACCESSORS(data3, Tagged<MaybeObject>)

  static const int kSizeWithData0 = kData1Offset;
  static const int kSizeWithData1 = kData2Offset;
  static const int kSizeWithData2 = kData3Offset;
  static const int kSizeWithData3 = kHeaderSize;

  DECL_VERIFIER(DataHandler)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(DataHandler)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DATA_HANDLER_H_
