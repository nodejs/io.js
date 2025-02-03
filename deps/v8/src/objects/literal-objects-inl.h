// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_LITERAL_OBJECTS_INL_H_
#define V8_OBJECTS_LITERAL_OBJECTS_INL_H_

#include <optional>

#include "src/objects/literal-objects.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/objects/literal-objects-tq-inl.inc"

//
// ObjectBoilerplateDescription
//

OBJECT_CONSTRUCTORS_IMPL(ObjectBoilerplateDescription,
                         ObjectBoilerplateDescription::Super)

// static
template <class IsolateT>
Handle<ObjectBoilerplateDescription> ObjectBoilerplateDescription::New(
    IsolateT* isolate, int boilerplate, int all_properties, int index_keys,
    bool has_seen_proto, AllocationType allocation) {
  DCHECK_GE(boilerplate, 0);
  DCHECK_GE(all_properties, index_keys);
  DCHECK_GE(index_keys, 0);

  int capacity = boilerplate * kElementsPerEntry;
  CHECK_LE(static_cast<unsigned>(capacity), kMaxCapacity);

  int backing_store_size =
      all_properties - index_keys - (has_seen_proto ? 1 : 0);
  DCHECK_GE(backing_store_size, 0);

  // Note we explicitly do NOT canonicalize to the
  // empty_object_boilerplate_description here since `flags` may be modified
  // even on empty descriptions.

  std::optional<DisallowGarbageCollection> no_gc;
  auto result = Cast<ObjectBoilerplateDescription>(
      Allocate(isolate, capacity, &no_gc, allocation));
  result->set_flags(0);
  result->set_backing_store_size(backing_store_size);
  MemsetTagged((*result)->RawFieldOfFirstElement(),
               ReadOnlyRoots{isolate}.undefined_value(), capacity);
  return result;
}

SMI_ACCESSORS(ObjectBoilerplateDescription, backing_store_size,
              Shape::kBackingStoreSizeOffset)
SMI_ACCESSORS(ObjectBoilerplateDescription, flags, Shape::kFlagsOffset)

Tagged<Object> ObjectBoilerplateDescription::name(int index) const {
  return get(NameIndex(index));
}

Tagged<Object> ObjectBoilerplateDescription::value(int index) const {
  return get(ValueIndex(index));
}

void ObjectBoilerplateDescription::set_key_value(int index, Tagged<Object> key,
                                                 Tagged<Object> value) {
  DCHECK_LT(static_cast<unsigned>(index), boilerplate_properties_count());
  set(NameIndex(index), key);
  set(ValueIndex(index), value);
}

int ObjectBoilerplateDescription::boilerplate_properties_count() const {
  DCHECK_EQ(0, capacity() % kElementsPerEntry);
  return capacity() / kElementsPerEntry;
}

//
// ClassBoilerplate
//

OBJECT_CONSTRUCTORS_IMPL(ClassBoilerplate, Struct)

SMI_ACCESSORS(ClassBoilerplate, arguments_count, kArgumentsCountOffset)
ACCESSORS(ClassBoilerplate, static_properties_template, Tagged<Object>,
          kStaticPropertiesTemplateOffset)
ACCESSORS(ClassBoilerplate, static_elements_template, Tagged<Object>,
          kStaticElementsTemplateOffset)
ACCESSORS(ClassBoilerplate, static_computed_properties, Tagged<FixedArray>,
          kStaticComputedPropertiesOffset)
ACCESSORS(ClassBoilerplate, instance_properties_template, Tagged<Object>,
          kInstancePropertiesTemplateOffset)
ACCESSORS(ClassBoilerplate, instance_elements_template, Tagged<Object>,
          kInstanceElementsTemplateOffset)
ACCESSORS(ClassBoilerplate, instance_computed_properties, Tagged<FixedArray>,
          kInstanceComputedPropertiesOffset)

//
// ArrayBoilerplateDescription
//

TQ_OBJECT_CONSTRUCTORS_IMPL(ArrayBoilerplateDescription)

ElementsKind ArrayBoilerplateDescription::elements_kind() const {
  return static_cast<ElementsKind>(flags());
}

void ArrayBoilerplateDescription::set_elements_kind(ElementsKind kind) {
  set_flags(kind);
}

bool ArrayBoilerplateDescription::is_empty() const {
  return constant_elements()->length() == 0;
}

//
// RegExpBoilerplateDescription
//

OBJECT_CONSTRUCTORS_IMPL(RegExpBoilerplateDescription, Struct)
TRUSTED_POINTER_ACCESSORS(RegExpBoilerplateDescription, data, RegExpData,
                          kDataOffset, kRegExpDataIndirectPointerTag)
ACCESSORS(RegExpBoilerplateDescription, source, Tagged<String>, kSourceOffset)
SMI_ACCESSORS(RegExpBoilerplateDescription, flags, kFlagsOffset)

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_LITERAL_OBJECTS_INL_H_
