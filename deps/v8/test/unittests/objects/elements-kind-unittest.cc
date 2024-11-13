// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <utility>

#include "src/codegen/compilation-cache.h"
#include "src/execution/execution.h"
#include "src/handles/global-handles.h"
#include "src/heap/factory.h"
#include "src/ic/stub-cache.h"
#include "src/init/v8.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using ElementsKindTest = TestWithContext;

//
// Helper functions.
//

namespace {

template <typename T, typename M>
bool EQUALS(Isolate* isolate, Handle<T> left, Handle<M> right) {
  if (*left == *right) return true;
  return Object::Equals(isolate, Cast<Object>(left), Cast<Object>(right))
      .FromJust();
}

template <typename T, typename M>
bool EQUALS(Isolate* isolate, Handle<T> left, M right) {
  return EQUALS(isolate, left, handle(right, isolate));
}

template <typename T, typename M>
bool EQUALS(Isolate* isolate, T left, Handle<M> right) {
  return EQUALS(isolate, handle(left, isolate), right);
}

bool ElementsKindIsHoleyElementsKindForRead(ElementsKind kind) {
  switch (kind) {
    case ElementsKind::HOLEY_SMI_ELEMENTS:
    case ElementsKind::HOLEY_ELEMENTS:
    case ElementsKind::HOLEY_DOUBLE_ELEMENTS:
    case ElementsKind::HOLEY_NONEXTENSIBLE_ELEMENTS:
    case ElementsKind::HOLEY_SEALED_ELEMENTS:
    case ElementsKind::HOLEY_FROZEN_ELEMENTS:
      return true;
    default:
      return false;
  }
}

bool ElementsKindIsHoleyElementsKind(ElementsKind kind) {
  switch (kind) {
    case ElementsKind::HOLEY_SMI_ELEMENTS:
    case ElementsKind::HOLEY_ELEMENTS:
    case ElementsKind::HOLEY_DOUBLE_ELEMENTS:
      return true;
    default:
      return false;
  }
}

bool ElementsKindIsFastPackedElementsKind(ElementsKind kind) {
  switch (kind) {
    case ElementsKind::PACKED_SMI_ELEMENTS:
    case ElementsKind::PACKED_ELEMENTS:
    case ElementsKind::PACKED_DOUBLE_ELEMENTS:
      return true;
    default:
      return false;
  }
}

}  // namespace

//
// Tests
//

TEST_F(ElementsKindTest, SystemPointerElementsKind) {
  CHECK_EQ(ElementsKindToShiftSize(SYSTEM_POINTER_ELEMENTS),
           kSystemPointerSizeLog2);
  CHECK_EQ(ElementsKindToByteSize(SYSTEM_POINTER_ELEMENTS), kSystemPointerSize);
}

TEST_F(ElementsKindTest, JSObjectAddingProperties) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<FixedArray> empty_fixed_array(factory->empty_fixed_array());
  Handle<PropertyArray> empty_property_array(factory->empty_property_array());
  Handle<JSFunction> function =
      factory->NewFunctionForTesting(factory->empty_string());
  Handle<Object> value(Smi::FromInt(42), i_isolate());

  Handle<JSObject> object = factory->NewJSObject(function);
  DirectHandle<Map> previous_map(object->map(), i_isolate());
  CHECK_EQ(HOLEY_ELEMENTS, previous_map->elements_kind());
  CHECK(EQUALS(i_isolate(), object->property_array(), empty_property_array));
  CHECK(EQUALS(i_isolate(), object->elements(), empty_fixed_array));

  // for the default constructor function no in-object properties are reserved
  // hence adding a single property will initialize the property-array
  Handle<String> name = MakeName("property", 0);
  JSObject::DefinePropertyOrElementIgnoreAttributes(object, name, value, NONE)
      .Check();
  CHECK_NE(object->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, object->map()->elements_kind());
  CHECK_LE(1, object->property_array()->length());
  CHECK(EQUALS(i_isolate(), object->elements(), empty_fixed_array));
}

TEST_F(ElementsKindTest, JSObjectInObjectAddingProperties) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<FixedArray> empty_fixed_array(factory->empty_fixed_array());
  Handle<PropertyArray> empty_property_array(factory->empty_property_array());
  Handle<JSFunction> function =
      factory->NewFunctionForTesting(factory->empty_string());
  int nof_inobject_properties = 10;
  // force in object properties by changing the expected_nof_properties
  // (we always reserve 8 inobject properties slack on top).
  function->shared()->set_expected_nof_properties(nof_inobject_properties - 8);
  Handle<Object> value(Smi::FromInt(42), i_isolate());

  Handle<JSObject> object = factory->NewJSObject(function);
  DirectHandle<Map> previous_map(object->map(), i_isolate());
  CHECK_EQ(HOLEY_ELEMENTS, previous_map->elements_kind());
  CHECK(EQUALS(i_isolate(), object->property_array(), empty_property_array));
  CHECK(EQUALS(i_isolate(), object->elements(), empty_fixed_array));

  // we have reserved space for in-object properties, hence adding up to
  // |nof_inobject_properties| will not create a property store
  for (int i = 0; i < nof_inobject_properties; i++) {
    Handle<String> name = MakeName("property", i);
    JSObject::DefinePropertyOrElementIgnoreAttributes(object, name, value, NONE)
        .Check();
  }
  CHECK_NE(object->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, object->map()->elements_kind());
  CHECK(EQUALS(i_isolate(), object->property_array(), empty_property_array));
  CHECK(EQUALS(i_isolate(), object->elements(), empty_fixed_array));

  // adding one more property will not fit in the in-object properties, thus
  // creating a property store
  int index = nof_inobject_properties + 1;
  Handle<String> name = MakeName("property", index);
  JSObject::DefinePropertyOrElementIgnoreAttributes(object, name, value, NONE)
      .Check();
  CHECK_NE(object->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, object->map()->elements_kind());
  // there must be at least 1 element in the properies store
  CHECK_LE(1, object->property_array()->length());
  CHECK(EQUALS(i_isolate(), object->elements(), empty_fixed_array));
}

TEST_F(ElementsKindTest, JSObjectAddingElements) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<String> name;
  Handle<FixedArray> empty_fixed_array(factory->empty_fixed_array());
  Handle<PropertyArray> empty_property_array(factory->empty_property_array());
  Handle<JSFunction> function =
      factory->NewFunctionForTesting(factory->empty_string());
  Handle<Object> value(Smi::FromInt(42), i_isolate());

  Handle<JSObject> object = factory->NewJSObject(function);
  DirectHandle<Map> previous_map(object->map(), i_isolate());
  CHECK_EQ(HOLEY_ELEMENTS, previous_map->elements_kind());
  CHECK(EQUALS(i_isolate(), object->property_array(), empty_property_array));
  CHECK(EQUALS(i_isolate(), object->elements(), empty_fixed_array));

  // Adding an indexed element initializes the elements array
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(object, name, value, NONE)
      .Check();
  // no change in elements_kind => no map transition
  CHECK_EQ(object->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, object->map()->elements_kind());
  CHECK(EQUALS(i_isolate(), object->property_array(), empty_property_array));
  CHECK_LE(1, object->elements()->length());

  // Adding more consecutive elements without a change in the backing store
  int non_dict_backing_store_limit = 100;
  for (int i = 1; i < non_dict_backing_store_limit; i++) {
    name = MakeName("", i);
    JSObject::DefinePropertyOrElementIgnoreAttributes(object, name, value, NONE)
        .Check();
  }
  // no change in elements_kind => no map transition
  CHECK_EQ(object->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, object->map()->elements_kind());
  CHECK(EQUALS(i_isolate(), object->property_array(), empty_property_array));
  CHECK_LE(non_dict_backing_store_limit, object->elements()->length());

  // Adding an element at an very large index causes a change to
  // DICTIONARY_ELEMENTS
  name = MakeString("100000000");
  JSObject::DefinePropertyOrElementIgnoreAttributes(object, name, value, NONE)
      .Check();
  // change in elements_kind => map transition
  CHECK_NE(object->map(), *previous_map);
  CHECK_EQ(DICTIONARY_ELEMENTS, object->map()->elements_kind());
  CHECK(EQUALS(i_isolate(), object->property_array(), empty_property_array));
  CHECK_LE(non_dict_backing_store_limit, object->elements()->length());
}

TEST_F(ElementsKindTest, JSArrayAddingProperties) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<FixedArray> empty_fixed_array(factory->empty_fixed_array());
  Handle<PropertyArray> empty_property_array(factory->empty_property_array());
  Handle<Object> value(Smi::FromInt(42), i_isolate());

  Handle<JSArray> array =
      factory->NewJSArray(ElementsKind::PACKED_SMI_ELEMENTS, 0, 0);
  DirectHandle<Map> previous_map(array->map(), i_isolate());
  CHECK_EQ(PACKED_SMI_ELEMENTS, previous_map->elements_kind());
  CHECK(EQUALS(i_isolate(), array->property_array(), empty_property_array));
  CHECK(EQUALS(i_isolate(), array->elements(), empty_fixed_array));
  CHECK_EQ(0, Smi::ToInt(array->length()));

  // for the default constructor function no in-object properties are reserved
  // hence adding a single property will initialize the property-array
  Handle<String> name = MakeName("property", 0);
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value, NONE)
      .Check();
  // No change in elements_kind but added property => new map
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(PACKED_SMI_ELEMENTS, array->map()->elements_kind());
  CHECK_LE(1, array->property_array()->length());
  CHECK(EQUALS(i_isolate(), array->elements(), empty_fixed_array));
  CHECK_EQ(0, Smi::ToInt(array->length()));
}

TEST_F(ElementsKindTest, JSArrayAddingElements) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<String> name;
  Handle<FixedArray> empty_fixed_array(factory->empty_fixed_array());
  Handle<PropertyArray> empty_property_array(factory->empty_property_array());
  Handle<Object> value(Smi::FromInt(42), i_isolate());

  Handle<JSArray> array =
      factory->NewJSArray(ElementsKind::PACKED_SMI_ELEMENTS, 0, 0);
  DirectHandle<Map> previous_map(array->map(), i_isolate());
  CHECK_EQ(PACKED_SMI_ELEMENTS, previous_map->elements_kind());
  CHECK(EQUALS(i_isolate(), array->property_array(), empty_property_array));
  CHECK(EQUALS(i_isolate(), array->elements(), empty_fixed_array));
  CHECK_EQ(0, Smi::ToInt(array->length()));

  // Adding an indexed element initializes the elements array
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value, NONE)
      .Check();
  // no change in elements_kind => no map transition
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(PACKED_SMI_ELEMENTS, array->map()->elements_kind());
  CHECK(EQUALS(i_isolate(), array->property_array(), empty_property_array));
  CHECK_LE(1, array->elements()->length());
  CHECK_EQ(1, Smi::ToInt(array->length()));

  // Adding more consecutive elements without a change in the backing store
  int non_dict_backing_store_limit = 100;
  for (int i = 1; i < non_dict_backing_store_limit; i++) {
    name = MakeName("", i);
    JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value, NONE)
        .Check();
  }
  // no change in elements_kind => no map transition
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(PACKED_SMI_ELEMENTS, array->map()->elements_kind());
  CHECK(EQUALS(i_isolate(), array->property_array(), empty_property_array));
  CHECK_LE(non_dict_backing_store_limit, array->elements()->length());
  CHECK_EQ(non_dict_backing_store_limit, Smi::ToInt(array->length()));

  // Adding an element at an very large index causes a change to
  // DICTIONARY_ELEMENTS
  int index = 100000000;
  name = MakeName("", index);
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value, NONE)
      .Check();
  // change in elements_kind => map transition
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(DICTIONARY_ELEMENTS, array->map()->elements_kind());
  CHECK(EQUALS(i_isolate(), array->property_array(), empty_property_array));
  CHECK_LE(non_dict_backing_store_limit, array->elements()->length());
  CHECK_LE(array->elements()->length(), index);
  CHECK_EQ(index + 1, Smi::ToInt(array->length()));
}

TEST_F(ElementsKindTest, JSArrayAddingElementsGeneralizingiFastSmiElements) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<String> name;
  Handle<Object> value_smi(Smi::FromInt(42), i_isolate());
  Handle<Object> value_string(MakeString("value"));
  Handle<Object> value_double = factory->NewNumber(3.1415);

  Handle<JSArray> array =
      factory->NewJSArray(ElementsKind::PACKED_SMI_ELEMENTS, 0, 0);
  DirectHandle<Map> previous_map(array->map(), i_isolate());
  CHECK_EQ(PACKED_SMI_ELEMENTS, previous_map->elements_kind());
  CHECK_EQ(0, Smi::ToInt(array->length()));

  // `array[0] = smi_value` doesn't change the elements_kind
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_smi,
                                                    NONE)
      .Check();
  // no change in elements_kind => no map transition
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(PACKED_SMI_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(1, Smi::ToInt(array->length()));

  // `delete array[0]` does not alter length, but changes the elments_kind
  name = MakeString("0");
  CHECK(JSReceiver::DeletePropertyOrElement(array, name).FromMaybe(false));
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(HOLEY_SMI_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(1, Smi::ToInt(array->length()));
  previous_map = handle(array->map(), i_isolate());

  // add a couple of elements again
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_smi,
                                                    NONE)
      .Check();
  name = MakeString("1");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_smi,
                                                    NONE)
      .Check();
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(HOLEY_SMI_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(2, Smi::ToInt(array->length()));

  // Adding a string to the array changes from FAST_HOLEY_SMI to FAST_HOLEY
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_string,
                                                    NONE)
      .Check();
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(2, Smi::ToInt(array->length()));
  previous_map = handle(array->map(), i_isolate());

  // We don't transition back to FAST_SMI even if we remove the string
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_smi,
                                                    NONE)
      .Check();
  CHECK_EQ(array->map(), *previous_map);

  // Adding a double doesn't change the map either
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_double,
                                                    NONE)
      .Check();
  CHECK_EQ(array->map(), *previous_map);
}

TEST_F(ElementsKindTest, JSArrayAddingElementsGeneralizingFastElements) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<String> name;
  Handle<Object> value_smi(Smi::FromInt(42), i_isolate());
  Handle<Object> value_string(MakeString("value"));

  Handle<JSArray> array =
      factory->NewJSArray(ElementsKind::PACKED_ELEMENTS, 0, 0);
  DirectHandle<Map> previous_map(array->map(), i_isolate());
  CHECK_EQ(PACKED_ELEMENTS, previous_map->elements_kind());
  CHECK_EQ(0, Smi::ToInt(array->length()));

  // `array[0] = smi_value` doesn't change the elements_kind
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_smi,
                                                    NONE)
      .Check();
  // no change in elements_kind => no map transition
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(PACKED_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(1, Smi::ToInt(array->length()));

  // `delete array[0]` does not alter length, but changes the elments_kind
  name = MakeString("0");
  CHECK(JSReceiver::DeletePropertyOrElement(array, name).FromMaybe(false));
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(1, Smi::ToInt(array->length()));
  previous_map = handle(array->map(), i_isolate());

  // add a couple of elements, elements_kind stays HOLEY
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_string,
                                                    NONE)
      .Check();
  name = MakeString("1");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_smi,
                                                    NONE)
      .Check();
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(2, Smi::ToInt(array->length()));
}

TEST_F(ElementsKindTest, JSArrayAddingElementsGeneralizingiFastDoubleElements) {
  Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  Handle<String> name;
  Handle<Object> value_smi(Smi::FromInt(42), i_isolate());
  Handle<Object> value_string(MakeString("value"));
  Handle<Object> value_double = factory->NewNumber(3.1415);

  Handle<JSArray> array =
      factory->NewJSArray(ElementsKind::PACKED_SMI_ELEMENTS, 0, 0);
  DirectHandle<Map> previous_map(array->map(), i_isolate());

  // `array[0] = value_double` changes |elements_kind| to PACKED_DOUBLE_ELEMENTS
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_double,
                                                    NONE)
      .Check();
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(PACKED_DOUBLE_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(1, Smi::ToInt(array->length()));
  previous_map = handle(array->map(), i_isolate());

  // `array[1] = value_smi` doesn't alter the |elements_kind|
  name = MakeString("1");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_smi,
                                                    NONE)
      .Check();
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(PACKED_DOUBLE_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(2, Smi::ToInt(array->length()));

  // `delete array[0]` does not alter length, but changes the elments_kind
  name = MakeString("0");
  CHECK(JSReceiver::DeletePropertyOrElement(array, name).FromMaybe(false));
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(HOLEY_DOUBLE_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(2, Smi::ToInt(array->length()));
  previous_map = handle(array->map(), i_isolate());

  // filling the hole `array[0] = value_smi` again doesn't transition back
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_double,
                                                    NONE)
      .Check();
  CHECK_EQ(array->map(), *previous_map);
  CHECK_EQ(HOLEY_DOUBLE_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(2, Smi::ToInt(array->length()));

  // Adding a string to the array changes to elements_kind PACKED_ELEMENTS
  name = MakeString("1");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_string,
                                                    NONE)
      .Check();
  CHECK_NE(array->map(), *previous_map);
  CHECK_EQ(HOLEY_ELEMENTS, array->map()->elements_kind());
  CHECK_EQ(2, Smi::ToInt(array->length()));
  previous_map = handle(array->map(), i_isolate());

  // Adding a double doesn't change the map
  name = MakeString("0");
  JSObject::DefinePropertyOrElementIgnoreAttributes(array, name, value_double,
                                                    NONE)
      .Check();
  CHECK_EQ(array->map(), *previous_map);
}

TEST_F(ElementsKindTest, IsHoleyElementsKindForRead) {
  for (int i = 0; i <= ElementsKind::LAST_ELEMENTS_KIND; i++) {
    ElementsKind kind = static_cast<ElementsKind>(i);
    CHECK_EQ(ElementsKindIsHoleyElementsKindForRead(kind),
             IsHoleyElementsKindForRead(kind));
  }
}

TEST_F(ElementsKindTest, IsHoleyElementsKind) {
  for (int i = 0; i <= ElementsKind::LAST_ELEMENTS_KIND; i++) {
    ElementsKind kind = static_cast<ElementsKind>(i);
    CHECK_EQ(ElementsKindIsHoleyElementsKind(kind), IsHoleyElementsKind(kind));
  }
}

TEST_F(ElementsKindTest, IsFastPackedElementsKind) {
  for (int i = 0; i <= ElementsKind::LAST_ELEMENTS_KIND; i++) {
    ElementsKind kind = static_cast<ElementsKind>(i);
    CHECK_EQ(ElementsKindIsFastPackedElementsKind(kind),
             IsFastPackedElementsKind(kind));
  }
}

}  // namespace internal
}  // namespace v8
