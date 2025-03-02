// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROPERTY_H_
#define V8_OBJECTS_PROPERTY_H_

#include <iosfwd>

#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/name.h"
#include "src/objects/objects.h"
#include "src/objects/property-details.h"

namespace v8 {
namespace internal {

// Abstraction for elements in instance-descriptor arrays.
//
// Each descriptor has a key, property attributes, property type,
// property index (in the actual instance-descriptor array) and
// optionally a piece of data.
class V8_EXPORT_PRIVATE Descriptor final {
 public:
  Descriptor();

  DirectHandle<Name> GetKey() const { return key_; }
  MaybeObjectDirectHandle GetValue() const { return value_; }
  PropertyDetails GetDetails() const { return details_; }

  void SetSortedKeyIndex(int index) { details_ = details_.set_pointer(index); }

  static Descriptor DataField(Isolate* isolate, DirectHandle<Name> key,
                              int field_index, PropertyAttributes attributes,
                              Representation representation);

  static Descriptor DataField(
      DirectHandle<Name> key, int field_index, PropertyAttributes attributes,
      PropertyConstness constness, Representation representation,
      const MaybeObjectDirectHandle& wrapped_field_type);

  static Descriptor DataConstant(DirectHandle<Name> key,
                                 DirectHandle<Object> value,
                                 PropertyAttributes attributes);

  static Descriptor DataConstant(Isolate* isolate, DirectHandle<Name> key,
                                 int field_index, DirectHandle<Object> value,
                                 PropertyAttributes attributes);

  static Descriptor AccessorConstant(DirectHandle<Name> key,
                                     DirectHandle<Object> foreign,
                                     PropertyAttributes attributes);

 private:
  DirectHandle<Name> key_;
  MaybeObjectDirectHandle value_;
  PropertyDetails details_;

 protected:
  Descriptor(DirectHandle<Name> key, const MaybeObjectDirectHandle& value,
             PropertyDetails details);

  Descriptor(DirectHandle<Name> key, const MaybeObjectDirectHandle& value,
             PropertyKind kind, PropertyAttributes attributes,
             PropertyLocation location, PropertyConstness constness,
             Representation representation, int field_index);

  friend class MapUpdater;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_PROPERTY_H_
