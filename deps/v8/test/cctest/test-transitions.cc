// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/test-transitions.h"

#include <stdlib.h>

#include <utility>

#include "src/codegen/compilation-cache.h"
#include "src/execution/execution.h"
#include "src/heap/factory.h"
#include "src/objects/field-type.h"
#include "src/objects/objects-inl.h"
#include "src/objects/transitions-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

TEST(TransitionArray_SimpleFieldTransitions) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  DirectHandle<String> name1 = factory->InternalizeUtf8String("foo");
  DirectHandle<String> name2 = factory->InternalizeUtf8String("bar");
  PropertyAttributes attributes = NONE;

  DirectHandle<Map> map0 = Map::Create(isolate, 0);
  DirectHandle<Map> map1 =
      Map::CopyWithField(isolate, map0, name1, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  DirectHandle<Map> map2 =
      Map::CopyWithField(isolate, map0, name2, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();

  CHECK(IsSmi(map0->raw_transitions()));

  {
    TransitionsAccessor::Insert(isolate, map0, name1, map1,
                                SIMPLE_PROPERTY_TRANSITION);
  }
  {
    {
      TestTransitionsAccessor transitions(isolate, map0);
      CHECK(transitions.IsWeakRefEncoding());
      CHECK_EQ(*map1, transitions.SearchTransition(*name1, PropertyKind::kData,
                                                   attributes));
      CHECK_EQ(1, transitions.NumberOfTransitions());
      CHECK_EQ(*name1, transitions.GetKey(0));
      CHECK_EQ(*map1, transitions.GetTarget(0));
    }

    TransitionsAccessor::Insert(isolate, map0, name2, map2,
                                SIMPLE_PROPERTY_TRANSITION);
  }
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());

    CHECK_EQ(*map1, transitions.SearchTransition(*name1, PropertyKind::kData,
                                                 attributes));
    CHECK_EQ(*map2, transitions.SearchTransition(*name2, PropertyKind::kData,
                                                 attributes));
    CHECK_EQ(2, transitions.NumberOfTransitions());
    for (int i = 0; i < 2; i++) {
      Tagged<Name> key = transitions.GetKey(i);
      Tagged<Map> target = transitions.GetTarget(i);
      CHECK((key == *name1 && target == *map1) ||
            (key == *name2 && target == *map2));
    }

    DCHECK(transitions.IsSortedNoDuplicates());
  }
}


TEST(TransitionArray_FullFieldTransitions) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  DirectHandle<String> name1 = factory->InternalizeUtf8String("foo");
  DirectHandle<String> name2 = factory->InternalizeUtf8String("bar");
  PropertyAttributes attributes = NONE;

  DirectHandle<Map> map0 = Map::Create(isolate, 0);
  DirectHandle<Map> map1 =
      Map::CopyWithField(isolate, map0, name1, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  DirectHandle<Map> map2 =
      Map::CopyWithField(isolate, map0, name2, FieldType::Any(isolate),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();

  CHECK(IsSmi(map0->raw_transitions()));

  {
    TransitionsAccessor::Insert(isolate, map0, name1, map1,
                                PROPERTY_TRANSITION);
  }
  {
    {
      TestTransitionsAccessor transitions(isolate, map0);
      CHECK(transitions.IsFullTransitionArrayEncoding());
      CHECK_EQ(*map1, transitions.SearchTransition(*name1, PropertyKind::kData,
                                                   attributes));
      CHECK_EQ(1, transitions.NumberOfTransitions());
      CHECK_EQ(*name1, transitions.GetKey(0));
      CHECK_EQ(*map1, transitions.GetTarget(0));
    }

    TransitionsAccessor::Insert(isolate, map0, name2, map2,
                                PROPERTY_TRANSITION);
  }
  {
    TestTransitionsAccessor transitions(isolate, map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());

    CHECK_EQ(*map1, transitions.SearchTransition(*name1, PropertyKind::kData,
                                                 attributes));
    CHECK_EQ(*map2, transitions.SearchTransition(*name2, PropertyKind::kData,
                                                 attributes));
    CHECK_EQ(2, transitions.NumberOfTransitions());
    for (int i = 0; i < 2; i++) {
      Tagged<Name> key = transitions.GetKey(i);
      Tagged<Map> target = transitions.GetTarget(i);
      CHECK((key == *name1 && target == *map1) ||
            (key == *name2 && target == *map2));
    }

    DCHECK(transitions.IsSortedNoDuplicates());
  }
}


TEST(TransitionArray_DifferentFieldNames) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  const int PROPS_COUNT = 10;
  Handle<String> names[PROPS_COUNT];
  Handle<Map> maps[PROPS_COUNT];
  PropertyAttributes attributes = NONE;

  DirectHandle<Map> map0 = Map::Create(isolate, 0);
  CHECK(IsSmi(map0->raw_transitions()));

  for (int i = 0; i < PROPS_COUNT; i++) {
    base::EmbeddedVector<char, 64> buffer;
    SNPrintF(buffer, "prop%d", i);
    Handle<String> name = factory->InternalizeUtf8String(buffer.begin());
    Handle<Map> map =
        Map::CopyWithField(isolate, map0, name, FieldType::Any(isolate),
                           attributes, PropertyConstness::kMutable,
                           Representation::Tagged(), OMIT_TRANSITION)
            .ToHandleChecked();
    names[i] = name;
    maps[i] = map;

    TransitionsAccessor::Insert(isolate, map0, name, map, PROPERTY_TRANSITION);
  }

  TransitionsAccessor transitions(isolate, *map0);
  for (int i = 0; i < PROPS_COUNT; i++) {
    CHECK_EQ(*maps[i], transitions.SearchTransition(
                           *names[i], PropertyKind::kData, attributes));
  }
  for (int i = 0; i < PROPS_COUNT; i++) {
    Tagged<Name> key = transitions.GetKey(i);
    Tagged<Map> target = transitions.GetTarget(i);
    for (int j = 0; j < PROPS_COUNT; j++) {
      if (*names[i] == key) {
        CHECK_EQ(*maps[i], target);
        break;
      }
    }
  }

  DCHECK(transitions.IsSortedNoDuplicates());
}


TEST(TransitionArray_SameFieldNamesDifferentAttributesSimple) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  DirectHandle<Map> map0 = Map::Create(isolate, 0);
  CHECK(IsSmi(map0->raw_transitions()));

  const int ATTRS_COUNT = (READ_ONLY | DONT_ENUM | DONT_DELETE) + 1;
  static_assert(ATTRS_COUNT == 8);
  Handle<Map> attr_maps[ATTRS_COUNT];
  DirectHandle<String> name = factory->InternalizeUtf8String("foo");

  // Add transitions for same field name but different attributes.
  for (int i = 0; i < ATTRS_COUNT; i++) {
    auto attributes = PropertyAttributesFromInt(i);

    Handle<Map> map =
        Map::CopyWithField(isolate, map0, name, FieldType::Any(isolate),
                           attributes, PropertyConstness::kMutable,
                           Representation::Tagged(), OMIT_TRANSITION)
            .ToHandleChecked();
    attr_maps[i] = map;

    TransitionsAccessor::Insert(isolate, map0, name, map, PROPERTY_TRANSITION);
  }

  // Ensure that transitions for |name| field are valid.
  TransitionsAccessor transitions(isolate, *map0);
  for (int i = 0; i < ATTRS_COUNT; i++) {
    auto attributes = PropertyAttributesFromInt(i);
    CHECK_EQ(*attr_maps[i], transitions.SearchTransition(
                                *name, PropertyKind::kData, attributes));
    // All transitions use the same key, so this check doesn't need to
    // care about ordering.
    CHECK_EQ(*name, transitions.GetKey(i));
  }

  DCHECK(transitions.IsSortedNoDuplicates());
}


TEST(TransitionArray_SameFieldNamesDifferentAttributes) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  const int PROPS_COUNT = 10;
  Handle<String> names[PROPS_COUNT];
  Handle<Map> maps[PROPS_COUNT];

  DirectHandle<Map> map0 = Map::Create(isolate, 0);
  CHECK(IsSmi(map0->raw_transitions()));

  // Some number of fields.
  for (int i = 0; i < PROPS_COUNT; i++) {
    base::EmbeddedVector<char, 64> buffer;
    SNPrintF(buffer, "prop%d", i);
    Handle<String> name = factory->InternalizeUtf8String(buffer.begin());
    Handle<Map> map =
        Map::CopyWithField(isolate, map0, name, FieldType::Any(isolate), NONE,
                           PropertyConstness::kMutable,
                           Representation::Tagged(), OMIT_TRANSITION)
            .ToHandleChecked();
    names[i] = name;
    maps[i] = map;

    TransitionsAccessor::Insert(isolate, map0, name, map, PROPERTY_TRANSITION);
  }

  const int ATTRS_COUNT = (READ_ONLY | DONT_ENUM | DONT_DELETE) + 1;
  static_assert(ATTRS_COUNT == 8);
  Handle<Map> attr_maps[ATTRS_COUNT];
  DirectHandle<String> name = factory->InternalizeUtf8String("foo");

  // Add transitions for same field name but different attributes.
  for (int i = 0; i < ATTRS_COUNT; i++) {
    auto attributes = PropertyAttributesFromInt(i);

    Handle<Map> map =
        Map::CopyWithField(isolate, map0, name, FieldType::Any(isolate),
                           attributes, PropertyConstness::kMutable,
                           Representation::Tagged(), OMIT_TRANSITION)
            .ToHandleChecked();
    attr_maps[i] = map;

    TransitionsAccessor::Insert(isolate, map0, name, map, PROPERTY_TRANSITION);
  }

  // Ensure that transitions for |name| field are valid.
  TransitionsAccessor transitions(isolate, *map0);
  for (int i = 0; i < ATTRS_COUNT; i++) {
    auto attr = PropertyAttributesFromInt(i);
    CHECK_EQ(*attr_maps[i],
             transitions.SearchTransition(*name, PropertyKind::kData, attr));
  }

  // Ensure that info about the other fields still valid.
  CHECK_EQ(PROPS_COUNT + ATTRS_COUNT, transitions.NumberOfTransitions());
  for (int i = 0; i < PROPS_COUNT + ATTRS_COUNT; i++) {
    Tagged<Name> key = transitions.GetKey(i);
    Tagged<Map> target = transitions.GetTarget(i);
    if (key == *name) {
      // Attributes transition.
      PropertyAttributes attributes =
          target->GetLastDescriptorDetails(isolate).attributes();
      CHECK_EQ(*attr_maps[static_cast<int>(attributes)], target);
    } else {
      for (int j = 0; j < PROPS_COUNT; j++) {
        if (*names[j] == key) {
          CHECK_EQ(*maps[j], target);
          break;
        }
      }
    }
  }

  DCHECK(transitions.IsSortedNoDuplicates());
}

}  // namespace internal
}  // namespace v8
