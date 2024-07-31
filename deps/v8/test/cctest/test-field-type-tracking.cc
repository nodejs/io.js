// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <utility>

#include "src/base/logging.h"
#include "src/execution/execution.h"
#include "src/heap/factory-inl.h"
#include "src/objects/field-type.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/internal-index.h"
#include "src/objects/map-updater.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-details.h"
#include "src/objects/property.h"
#include "src/objects/struct-inl.h"
#include "src/objects/transitions.h"
#include "src/utils/ostreams.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace test_field_type_tracking {

// TODO(ishell): fix this once TransitionToPrototype stops generalizing
// all field representations (similar to crbug/448711 where elements kind
// and observed transitions caused generalization of all fields).
const bool IS_PROTO_TRANS_ISSUE_FIXED = false;

// TODO(ishell): fix this once TransitionToAccessorProperty is able to always
// keep map in fast mode.
const bool IS_ACCESSOR_FIELD_SUPPORTED = false;

// Number of properties used in the tests.
const int kPropCount = 7;

enum ChangeAlertMechanism { kDeprecation, kFieldOwnerDependency, kNoAlert };

//
// Helper functions.
//

static Handle<AccessorPair> CreateAccessorPair(bool with_getter,
                                               bool with_setter) {
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Handle<AccessorPair> pair = factory->NewAccessorPair();
  Handle<String> empty_string = factory->empty_string();
  if (with_getter) {
    Handle<JSFunction> func = factory->NewFunctionForTesting(empty_string);
    pair->set_getter(*func);
  }
  if (with_setter) {
    Handle<JSFunction> func = factory->NewFunctionForTesting(empty_string);
    pair->set_setter(*func);
  }
  return pair;
}

// Check cached migration target map after Map::Update() and Map::TryUpdate()
static void CheckMigrationTarget(Isolate* isolate, Tagged<Map> old_map,
                                 Tagged<Map> new_map) {
  Tagged<Map> target =
      TransitionsAccessor(isolate, old_map).GetMigrationTarget();
  if (target.is_null()) return;
  CHECK_EQ(new_map, target);
  CHECK_EQ(MapUpdater::TryUpdateNoLock(isolate, old_map,
                                       ConcurrencyMode::kSynchronous),
           target);
}

class Expectations {
  static const int MAX_PROPERTIES = 10;
  Isolate* isolate_;
  ElementsKind elements_kind_;
  PropertyKind kinds_[MAX_PROPERTIES];
  PropertyLocation locations_[MAX_PROPERTIES];
  PropertyConstness constnesses_[MAX_PROPERTIES];
  PropertyAttributes attributes_[MAX_PROPERTIES];
  Representation representations_[MAX_PROPERTIES];
  // FieldType for kField, value for DATA_CONSTANT and getter for
  // ACCESSOR_CONSTANT.
  Handle<Object> values_[MAX_PROPERTIES];
  // Setter for ACCESSOR_CONSTANT.
  Handle<Object> setter_values_[MAX_PROPERTIES];
  int number_of_properties_;

 public:
  explicit Expectations(Isolate* isolate, ElementsKind elements_kind)
      : isolate_(isolate),
        elements_kind_(elements_kind),
        number_of_properties_(0) {}

  explicit Expectations(Isolate* isolate)
      : Expectations(
            isolate,
            isolate->object_function()->initial_map()->elements_kind()) {}

  void Init(int index, PropertyKind kind, PropertyAttributes attributes,
            PropertyConstness constness, PropertyLocation location,
            Representation representation, Handle<Object> value) {
    CHECK(index < MAX_PROPERTIES);
    kinds_[index] = kind;
    locations_[index] = location;
    if (kind == PropertyKind::kData && location == PropertyLocation::kField &&
        IsTransitionableFastElementsKind(elements_kind_)) {
      // Maps with transitionable elements kinds must have the most general
      // field type.
      value = FieldType::Any(isolate_);
      representation = Representation::Tagged();
    }
    constnesses_[index] = constness;
    attributes_[index] = attributes;
    representations_[index] = representation;
    values_[index] = value;
  }

  void Print() const {
    StdoutStream os;
    os << "Expectations: #" << number_of_properties_ << "\n";
    for (int i = 0; i < number_of_properties_; i++) {
      os << " " << i << ": ";
      os << "Descriptor @ ";

      if (kinds_[i] == PropertyKind::kData) {
        FieldType::PrintTo(FieldType::cast(*values_[i]), os);
      } else {
        // kAccessor
        os << "(get: " << Brief(*values_[i])
           << ", set: " << Brief(*setter_values_[i]) << ") ";
      }

      os << " (";
      if (constnesses_[i] == PropertyConstness::kConst) os << "const ";
      os << (kinds_[i] == PropertyKind::kData ? "data " : "accessor ");
      if (locations_[i] == PropertyLocation::kField) {
        os << "field"
           << ": " << representations_[i].Mnemonic();
      } else {
        os << "descriptor";
      }
      os << ", attrs: " << attributes_[i] << ")\n";
    }
    os << "\n";
  }

  void SetElementsKind(ElementsKind elements_kind) {
    elements_kind_ = elements_kind;
  }

  Handle<FieldType> GetFieldType(int index) {
    CHECK(index < MAX_PROPERTIES);
    CHECK_EQ(PropertyLocation::kField, locations_[index]);
    return Handle<FieldType>::cast(values_[index]);
  }

  void SetDataField(int index, PropertyAttributes attrs,
                    PropertyConstness constness, Representation representation,
                    Handle<FieldType> field_type) {
    Init(index, PropertyKind::kData, attrs, constness, PropertyLocation::kField,
         representation, field_type);
  }

  void SetDataField(int index, PropertyConstness constness,
                    Representation representation,
                    Handle<FieldType> field_type) {
    SetDataField(index, attributes_[index], constness, representation,
                 field_type);
  }

  void SetAccessorField(int index, PropertyAttributes attrs) {
    Init(index, PropertyKind::kAccessor, attrs, PropertyConstness::kConst,
         PropertyLocation::kDescriptor, Representation::Tagged(),
         FieldType::Any(isolate_));
  }

  void SetAccessorField(int index) {
    SetAccessorField(index, attributes_[index]);
  }

  void SetDataConstant(int index, PropertyAttributes attrs,
                       Handle<JSFunction> value) {
    Handle<FieldType> field_type(FieldType::Class(value->map()), isolate_);
    Init(index, PropertyKind::kData, attrs, PropertyConstness::kConst,
         PropertyLocation::kField, Representation::HeapObject(), field_type);
  }

  void SetDataConstant(int index, Handle<JSFunction> value) {
    SetDataConstant(index, attributes_[index], value);
  }

  void SetAccessorConstant(int index, PropertyAttributes attrs,
                           Handle<Object> getter, Handle<Object> setter) {
    Init(index, PropertyKind::kAccessor, attrs, PropertyConstness::kConst,
         PropertyLocation::kDescriptor, Representation::Tagged(), getter);
    setter_values_[index] = setter;
  }

  void SetAccessorConstantComponent(int index, PropertyAttributes attrs,
                                    AccessorComponent component,
                                    Handle<Object> accessor) {
    CHECK_EQ(PropertyKind::kAccessor, kinds_[index]);
    CHECK_EQ(PropertyLocation::kDescriptor, locations_[index]);
    CHECK(index < number_of_properties_);
    if (component == ACCESSOR_GETTER) {
      values_[index] = accessor;
    } else {
      setter_values_[index] = accessor;
    }
  }

  void SetAccessorConstant(int index, PropertyAttributes attrs,
                           Handle<AccessorPair> pair) {
    Handle<Object> getter = handle(pair->getter(), isolate_);
    Handle<Object> setter = handle(pair->setter(), isolate_);
    SetAccessorConstant(index, attrs, getter, setter);
  }

  void SetAccessorConstant(int index, Handle<Object> getter,
                           Handle<Object> setter) {
    SetAccessorConstant(index, attributes_[index], getter, setter);
  }

  void SetAccessorConstant(int index, Handle<AccessorPair> pair) {
    Handle<Object> getter = handle(pair->getter(), isolate_);
    Handle<Object> setter = handle(pair->setter(), isolate_);
    SetAccessorConstant(index, getter, setter);
  }

  void GeneralizeField(int index) {
    CHECK(index < number_of_properties_);
    representations_[index] = Representation::Tagged();
    if (locations_[index] == PropertyLocation::kField) {
      values_[index] = FieldType::Any(isolate_);
    }
  }

  bool Check(Tagged<DescriptorArray> descriptors,
             InternalIndex descriptor) const {
    PropertyDetails details = descriptors->GetDetails(descriptor);

    if (details.kind() != kinds_[descriptor.as_int()]) return false;
    if (details.location() != locations_[descriptor.as_int()]) return false;
    if (details.constness() != constnesses_[descriptor.as_int()]) return false;

    PropertyAttributes expected_attributes = attributes_[descriptor.as_int()];
    if (details.attributes() != expected_attributes) return false;

    Representation expected_representation =
        representations_[descriptor.as_int()];

    if (!details.representation().Equals(expected_representation)) return false;

    Tagged<Object> expected_value = *values_[descriptor.as_int()];
    if (details.location() == PropertyLocation::kField) {
      if (details.kind() == PropertyKind::kData) {
        Tagged<FieldType> type = descriptors->GetFieldType(descriptor);
        return FieldType::cast(expected_value) == type;
      } else {
        // kAccessor
        UNREACHABLE();
      }
    } else {
      CHECK_EQ(PropertyKind::kAccessor, details.kind());
      Tagged<Object> value = descriptors->GetStrongValue(descriptor);
      if (value == expected_value) return true;
      if (!IsAccessorPair(value)) return false;
      Tagged<AccessorPair> pair = AccessorPair::cast(value);
      return pair->Equals(expected_value, *setter_values_[descriptor.as_int()]);
    }
    UNREACHABLE();
  }

  bool Check(Tagged<Map> map, int expected_nof) const {
    CHECK_EQ(elements_kind_, map->elements_kind());
    CHECK(number_of_properties_ <= MAX_PROPERTIES);
    CHECK_EQ(expected_nof, map->NumberOfOwnDescriptors());
    CHECK(!map->is_dictionary_map());

    Tagged<DescriptorArray> descriptors = map->instance_descriptors();
    CHECK(expected_nof <= number_of_properties_);
    for (InternalIndex i : InternalIndex::Range(expected_nof)) {
      if (!Check(descriptors, i)) {
        Print();
#ifdef OBJECT_PRINT
        i::Print(descriptors);
#endif
        return false;
      }
    }
    return true;
  }

  bool Check(Tagged<Map> map) const {
    return Check(map, number_of_properties_);
  }

  bool CheckNormalized(Tagged<Map> map) const {
    CHECK(map->is_dictionary_map());
    CHECK_EQ(elements_kind_, map->elements_kind());
    // TODO(leszeks): Iterate over the key/value pairs of the map and compare
    // them against the expected fields.
    return true;
  }

  //
  // Helper methods for initializing expectations and adding properties to
  // given |map|.
  //

  Handle<Map> AsElementsKind(Handle<Map> map, ElementsKind elements_kind) {
    elements_kind_ = elements_kind;
    map = Map::AsElementsKind(isolate_, map, elements_kind);
    CHECK_EQ(elements_kind_, map->elements_kind());
    return map;
  }

  void ChangeAttributesForAllProperties(PropertyAttributes attributes) {
    for (int i = 0; i < number_of_properties_; i++) {
      attributes_[i] = attributes;
    }
  }

  Handle<Map> AddDataField(Handle<Map> map, PropertyAttributes attributes,
                           PropertyConstness constness,
                           Representation representation,
                           Handle<FieldType> field_type) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetDataField(property_index, attributes, constness, representation,
                 field_type);

    Handle<String> name = CcTest::MakeName("prop", property_index);
    return Map::CopyWithField(isolate_, map, name, field_type, attributes,
                              constness, representation, INSERT_TRANSITION)
        .ToHandleChecked();
  }

  Handle<Map> AddDataConstant(Handle<Map> map, PropertyAttributes attributes,
                              Handle<JSFunction> value) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetDataConstant(property_index, attributes, value);

    Handle<String> name = CcTest::MakeName("prop", property_index);
    return Map::CopyWithConstant(isolate_, map, name, value, attributes,
                                 INSERT_TRANSITION)
        .ToHandleChecked();
  }

  Handle<Map> TransitionToDataField(Handle<Map> map,
                                    PropertyAttributes attributes,
                                    PropertyConstness constness,
                                    Representation representation,
                                    Handle<FieldType> heap_type,
                                    Handle<Object> value) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetDataField(property_index, attributes, constness, representation,
                 heap_type);

    Handle<String> name = CcTest::MakeName("prop", property_index);
    return Map::TransitionToDataProperty(isolate_, map, name, value, attributes,
                                         constness, StoreOrigin::kNamed);
  }

  Handle<Map> TransitionToDataConstant(Handle<Map> map,
                                       PropertyAttributes attributes,
                                       Handle<JSFunction> value) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetDataConstant(property_index, attributes, value);

    Handle<String> name = CcTest::MakeName("prop", property_index);
    return Map::TransitionToDataProperty(isolate_, map, name, value, attributes,
                                         PropertyConstness::kConst,
                                         StoreOrigin::kNamed);
  }

  Handle<Map> FollowDataTransition(Handle<Map> map,
                                   PropertyAttributes attributes,
                                   PropertyConstness constness,
                                   Representation representation,
                                   Handle<FieldType> heap_type) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetDataField(property_index, attributes, constness, representation,
                 heap_type);

    Handle<String> name = CcTest::MakeName("prop", property_index);
    MaybeHandle<Map> target = TransitionsAccessor::SearchTransition(
        isolate_, map, *name, PropertyKind::kData, attributes);
    CHECK(!target.is_null());
    return target.ToHandleChecked();
  }

  Handle<Map> AddAccessorConstant(Handle<Map> map,
                                  PropertyAttributes attributes,
                                  Handle<AccessorPair> pair) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetAccessorConstant(property_index, attributes, pair);

    Handle<String> name = CcTest::MakeName("prop", property_index);

    Descriptor d = Descriptor::AccessorConstant(name, pair, attributes);
    return Map::CopyInsertDescriptor(isolate_, map, &d, INSERT_TRANSITION);
  }

  Handle<Map> AddAccessorConstant(Handle<Map> map,
                                  PropertyAttributes attributes,
                                  Handle<Object> getter,
                                  Handle<Object> setter) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetAccessorConstant(property_index, attributes, getter, setter);

    Handle<String> name = CcTest::MakeName("prop", property_index);

    CHECK(!IsNull(*getter, isolate_) || !IsNull(*setter, isolate_));
    Factory* factory = isolate_->factory();

    if (!IsNull(*getter, isolate_)) {
      Handle<AccessorPair> pair = factory->NewAccessorPair();
      pair->SetComponents(*getter, *factory->null_value());
      Descriptor d = Descriptor::AccessorConstant(name, pair, attributes);
      map = Map::CopyInsertDescriptor(isolate_, map, &d, INSERT_TRANSITION);
    }
    if (!IsNull(*setter, isolate_)) {
      Handle<AccessorPair> pair = factory->NewAccessorPair();
      pair->SetComponents(*getter, *setter);
      Descriptor d = Descriptor::AccessorConstant(name, pair, attributes);
      map = Map::CopyInsertDescriptor(isolate_, map, &d, INSERT_TRANSITION);
    }
    return map;
  }

  Handle<Map> TransitionToAccessorConstant(Handle<Map> map,
                                           PropertyAttributes attributes,
                                           Handle<AccessorPair> pair) {
    CHECK_EQ(number_of_properties_, map->NumberOfOwnDescriptors());
    int property_index = number_of_properties_++;
    SetAccessorConstant(property_index, attributes, pair);

    Handle<String> name = CcTest::MakeName("prop", property_index);

    Isolate* isolate = CcTest::i_isolate();
    Handle<Object> getter(pair->getter(), isolate);
    Handle<Object> setter(pair->setter(), isolate);

    InternalIndex descriptor =
        map->instance_descriptors(isolate)->SearchWithCache(isolate, *name,
                                                            *map);
    map = Map::TransitionToAccessorProperty(isolate, map, name, descriptor,
                                            getter, setter, attributes);
    CHECK(!map->is_deprecated());
    CHECK(!map->is_dictionary_map());
    return map;
  }
};


////////////////////////////////////////////////////////////////////////////////
// A set of tests for property reconfiguration that makes new transition tree
// branch.
//

namespace {

Handle<Map> ReconfigureProperty(Isolate* isolate, Handle<Map> map,
                                InternalIndex modify_index,
                                PropertyKind new_kind,
                                PropertyAttributes new_attributes,
                                Representation new_representation,
                                Handle<FieldType> new_field_type) {
  DCHECK_EQ(PropertyKind::kData, new_kind);  // Only kData case is supported.
  MapUpdater mu(isolate, map);
  return mu.ReconfigureToDataField(modify_index, new_attributes,
                                   PropertyConstness::kConst,
                                   new_representation, new_field_type);
}

}  // namespace

TEST(ReconfigureAccessorToNonExistingDataField) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> none_type = FieldType::None(isolate);
  Handle<AccessorPair> pair = CreateAccessorPair(true, true);

  Expectations expectations(isolate);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  map = expectations.AddAccessorConstant(map, NONE, pair);

  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());
  CHECK(expectations.Check(*map));

  InternalIndex first(0);
  Handle<Map> new_map =
      ReconfigureProperty(isolate, map, first, PropertyKind::kData, NONE,
                          Representation::None(), none_type);
  // |map| did not change except marked unstable.
  CHECK(!map->is_deprecated());
  CHECK(!map->is_stable());
  CHECK(expectations.Check(*map));

  // Property kind reconfiguration always makes the field mutable.
  expectations.SetDataField(0, NONE, PropertyConstness::kMutable,
                            Representation::None(), none_type);

  CHECK(!new_map->is_deprecated());
  CHECK(new_map->is_stable());
  CHECK(expectations.Check(*new_map));

  Handle<Map> new_map2 =
      ReconfigureProperty(isolate, map, first, PropertyKind::kData, NONE,
                          Representation::None(), none_type);
  CHECK_EQ(*new_map, *new_map2);

  Handle<Object> value(Smi::zero(), isolate);
  Handle<Map> prepared_map = Map::PrepareForDataProperty(
      isolate, new_map, first, PropertyConstness::kConst, value);
  // None to Smi generalization is trivial, map does not change.
  CHECK_EQ(*new_map, *prepared_map);

  expectations.SetDataField(0, NONE, PropertyConstness::kMutable,
                            Representation::Smi(), any_type);
  CHECK(prepared_map->is_stable());
  CHECK(expectations.Check(*prepared_map));

  // Now create an object with |map|, migrate it to |prepared_map| and ensure
  // that the data property is uninitialized.
  Factory* factory = isolate->factory();
  Handle<JSObject> obj = factory->NewJSObjectFromMap(map);
  JSObject::MigrateToMap(isolate, obj, prepared_map);
  FieldIndex index = FieldIndex::ForDescriptor(*prepared_map, first);
  CHECK(IsUninitialized(obj->RawFastPropertyAt(index), isolate));
#ifdef VERIFY_HEAP
  Object::ObjectVerify(*obj, isolate);
#endif
}


// This test checks that the LookupIterator machinery involved in
// JSObject::SetOwnPropertyIgnoreAttributes() does not try to migrate object
// to a map with a property with None representation.
TEST(ReconfigureAccessorToNonExistingDataFieldHeavy) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  CompileRun(
      "function getter() { return 1; };"
      "function setter() {};"
      "var o = {};"
      "Object.defineProperty(o, 'foo', "
      "                      { get: getter, set: setter, "
      "                        configurable: true, enumerable: true});");

  Handle<String> foo_str = factory->InternalizeUtf8String("foo");
  Handle<String> obj_name = factory->InternalizeUtf8String("o");

  Handle<Object> obj_value =
      Object::GetProperty(isolate, isolate->global_object(), obj_name)
          .ToHandleChecked();
  CHECK(IsJSObject(*obj_value));
  Handle<JSObject> obj = Handle<JSObject>::cast(obj_value);

  CHECK_EQ(1, obj->map()->NumberOfOwnDescriptors());
  InternalIndex first(0);
  CHECK(IsAccessorPair(
      obj->map()->instance_descriptors(isolate)->GetStrongValue(first)));

  Handle<Object> value(Smi::FromInt(42), isolate);
  JSObject::SetOwnPropertyIgnoreAttributes(obj, foo_str, value, NONE).Check();

  // Check that the property contains |value|.
  CHECK_EQ(1, obj->map()->NumberOfOwnDescriptors());
  FieldIndex index = FieldIndex::ForDescriptor(obj->map(), first);
  Tagged<Object> the_value = obj->RawFastPropertyAt(index);
  CHECK(IsSmi(the_value));
  CHECK_EQ(42, Smi::ToInt(the_value));
}


////////////////////////////////////////////////////////////////////////////////
// A set of tests for field generalization case.
//

namespace {

// <Constness, Representation, FieldType> data.
struct CRFTData {
  PropertyConstness constness;
  Representation representation;
  Handle<FieldType> type;
};

Handle<Code> CreateDummyOptimizedCode(Isolate* isolate) {
  uint8_t buffer[1];
  CodeDesc desc;
  desc.buffer = buffer;
  desc.buffer_size = arraysize(buffer);
  desc.instr_size = arraysize(buffer);
  return Factory::CodeBuilder(isolate, desc, CodeKind::TURBOFAN)
      .set_is_turbofanned()
      .set_empty_source_position_table()
      .set_deoptimization_data(DeoptimizationData::Empty(isolate))
      .Build();
}

static void CheckCodeObjectForDeopt(const CRFTData& from,
                                    const CRFTData& expected,
                                    Handle<Code> code_field_type,
                                    Handle<Code> code_field_repr,
                                    Handle<Code> code_field_const,
                                    bool expected_deopt) {
  if (!FieldType::Equals(*from.type, *expected.type)) {
    CHECK_EQ(expected_deopt, code_field_type->marked_for_deoptimization());
  } else {
    CHECK(!code_field_type->marked_for_deoptimization());
  }

  if (!from.representation.Equals(expected.representation)) {
    CHECK_EQ(expected_deopt, code_field_repr->marked_for_deoptimization());
  } else {
    CHECK(!code_field_repr->marked_for_deoptimization());
  }

  if (!code_field_const.is_null()) {
    if (from.constness != expected.constness) {
      CHECK_EQ(expected_deopt, code_field_const->marked_for_deoptimization());
    } else {
      CHECK(!code_field_const->marked_for_deoptimization());
    }
  }
}

// This test ensures that field generalization at |property_index| is done
// correctly independently of the fact that the |map| is detached from
// transition tree or not.
//
//  {} - p0 - p1 - p2: |detach_point_map|
//                  |
//                  X - detached at |detach_property_at_index|
//                  |
//                  + - p3 - p4: |map|
//
// Detaching does not happen if |detach_property_at_index| is -1.
//
void TestGeneralizeField(int detach_property_at_index, int property_index,
                         const CRFTData& from, const CRFTData& to,
                         const CRFTData& expected,
                         ChangeAlertMechanism expected_alert) {
  Isolate* isolate = CcTest::i_isolate();
  Handle<FieldType> any_type = FieldType::Any(isolate);

  CHECK(detach_property_at_index >= -1 &&
        detach_property_at_index < kPropCount);
  CHECK_LT(property_index, kPropCount);
  CHECK_NE(detach_property_at_index, property_index);

  const bool is_detached_map = detach_property_at_index >= 0;

  Expectations expectations(isolate);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  Handle<Map> detach_point_map;
  for (int i = 0; i < kPropCount; i++) {
    if (i == property_index) {
      map = expectations.AddDataField(map, NONE, from.constness,
                                      from.representation, from.type);
    } else {
      map = expectations.AddDataField(map, NONE, PropertyConstness::kConst,
                                      Representation::Smi(), any_type);
      if (i == detach_property_at_index) {
        detach_point_map = map;
      }
    }
  }
  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());
  CHECK(expectations.Check(*map));

  if (is_detached_map) {
    detach_point_map = ReconfigureProperty(
        isolate, detach_point_map, InternalIndex(detach_property_at_index),
        PropertyKind::kData, NONE, Representation::Double(), any_type);
    expectations.SetDataField(detach_property_at_index,
                              PropertyConstness::kConst,
                              Representation::Double(), any_type);
    CHECK(map->is_deprecated());
    CHECK(expectations.Check(*detach_point_map,
                             detach_point_map->NumberOfOwnDescriptors()));
  }

  // Create dummy optimized code object to test correct dependencies
  // on the field owner.
  Handle<Code> code_field_type = CreateDummyOptimizedCode(isolate);
  Handle<Code> code_field_repr = CreateDummyOptimizedCode(isolate);
  Handle<Code> code_field_const = CreateDummyOptimizedCode(isolate);
  Handle<Map> field_owner(
      map->FindFieldOwner(isolate, InternalIndex(property_index)), isolate);
  DependentCode::InstallDependency(isolate, code_field_type, field_owner,
                                   DependentCode::kFieldTypeGroup);
  DependentCode::InstallDependency(isolate, code_field_repr, field_owner,
                                   DependentCode::kFieldRepresentationGroup);
  DependentCode::InstallDependency(isolate, code_field_const, field_owner,
                                   DependentCode::kFieldConstGroup);
  CHECK(!code_field_type->marked_for_deoptimization());
  CHECK(!code_field_repr->marked_for_deoptimization());
  CHECK(!code_field_const->marked_for_deoptimization());

  // Create new maps by generalizing representation of propX field.
  Handle<Map> new_map = ReconfigureProperty(
      isolate, map, InternalIndex(property_index), PropertyKind::kData, NONE,
      to.representation, to.type);

  expectations.SetDataField(property_index, expected.constness,
                            expected.representation, expected.type);

  CHECK(!new_map->is_deprecated());
  CHECK(expectations.Check(*new_map));

  bool should_deopt = false;
  if (is_detached_map) {
    CHECK(!map->is_stable());
    CHECK(map->is_deprecated());
    CHECK_NE(*map, *new_map);
    should_deopt = (expected_alert == kFieldOwnerDependency) &&
                   !field_owner->is_deprecated();
  } else if (expected_alert == kDeprecation) {
    CHECK(!map->is_stable());
    CHECK(map->is_deprecated());
    CHECK(field_owner->is_deprecated());
    should_deopt = false;
  } else {
    CHECK(!field_owner->is_deprecated());
    CHECK(map->is_stable());  // Map did not change, must be left stable.
    CHECK_EQ(*map, *new_map);
    should_deopt = (expected_alert == kFieldOwnerDependency);
  }

  CheckCodeObjectForDeopt(from, expected, code_field_type, code_field_repr,
                          code_field_const, should_deopt);

  {
    // Check that all previous maps are not stable.
    Tagged<Map> tmp = *new_map;
    while (true) {
      Tagged<Object> back = tmp->GetBackPointer();
      if (IsUndefined(back, isolate)) break;
      tmp = Map::cast(back);
      CHECK(!tmp->is_stable());
    }
  }

  // Update all deprecated maps and check that they are now the same.
  Handle<Map> updated_map = Map::Update(isolate, map);
  CHECK_EQ(*new_map, *updated_map);
  CheckMigrationTarget(isolate, *map, *updated_map);
}

void TestGeneralizeField(const CRFTData& from, const CRFTData& to,
                         const CRFTData& expected,
                         ChangeAlertMechanism expected_alert) {
  // Check the cases when the map being reconfigured is a part of the
  // transition tree.
  static_assert(kPropCount > 4);
  int indices[] = {0, 2, kPropCount - 1};
  for (int i = 0; i < static_cast<int>(arraysize(indices)); i++) {
    TestGeneralizeField(-1, indices[i], from, to, expected, expected_alert);
  }

  if (!from.representation.IsNone()) {
    // Check the cases when the map being reconfigured is NOT a part of the
    // transition tree. "None -> anything" representation changes make sense
    // only for "attached" maps.
    int indices2[] = {0, kPropCount - 1};
    for (int i = 0; i < static_cast<int>(arraysize(indices2)); i++) {
      TestGeneralizeField(indices2[i], 2, from, to, expected, expected_alert);
    }

    // Check that reconfiguration to the very same field works correctly.
    CRFTData data = from;
    TestGeneralizeField(-1, 2, data, data, data, kNoAlert);
  }
}

}  // namespace

TEST(GeneralizeSmiFieldToDouble) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      kDeprecation);
}

TEST(GeneralizeSmiFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

TEST(GeneralizeDoubleFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

TEST(GeneralizeHeapObjectFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

TEST(GeneralizeHeapObjectFieldToHeapObject) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  Handle<FieldType> current_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  Handle<FieldType> new_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  Handle<FieldType> expected_type = any_type;

  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), current_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(),
       expected_type},
      kFieldOwnerDependency);
  current_type = expected_type;

  new_type = FieldType::Class(Map::Create(isolate, 0), isolate);

  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      kNoAlert);
}

TEST(GeneralizeNoneFieldToSmi) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> none_type = FieldType::None(isolate);
  Handle<FieldType> any_type = FieldType::Any(isolate);

  // None -> Smi representation change is trivial.
  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::None(), none_type},
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      kFieldOwnerDependency);
}

TEST(GeneralizeNoneFieldToDouble) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> none_type = FieldType::None(isolate);
  Handle<FieldType> any_type = FieldType::Any(isolate);

  // None -> Double representation change is NOT trivial.
  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::None(), none_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      kDeprecation);
}

TEST(GeneralizeNoneFieldToHeapObject) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> none_type = FieldType::None(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  // None -> HeapObject representation change is trivial.
  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::None(), none_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      kFieldOwnerDependency);
}

TEST(GeneralizeNoneFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> none_type = FieldType::None(isolate);
  Handle<FieldType> any_type = FieldType::Any(isolate);

  // None -> HeapObject representation change is trivial.
  TestGeneralizeField(
      {PropertyConstness::kMutable, Representation::None(), none_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}


////////////////////////////////////////////////////////////////////////////////
// A set of tests for field generalization case with kAccessor properties.
//

TEST(GeneralizeFieldWithAccessorProperties) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<AccessorPair> pair = CreateAccessorPair(true, true);

  const int kAccessorProp = kPropCount / 2;
  Expectations expectations(isolate);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  for (int i = 0; i < kPropCount; i++) {
    if (i == kAccessorProp) {
      map = expectations.AddAccessorConstant(map, NONE, pair);
    } else {
      map = expectations.AddDataField(map, NONE, PropertyConstness::kMutable,
                                      Representation::Smi(), any_type);
    }
  }
  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());
  CHECK(expectations.Check(*map));

  // Create new maps by generalizing representation of propX field.
  Handle<Map> maps[kPropCount];
  for (int i = 0; i < kPropCount; i++) {
    if (i == kAccessorProp) {
      // Skip accessor property reconfiguration.
      maps[i] = maps[i - 1];
      continue;
    }
    Handle<Map> new_map =
        ReconfigureProperty(isolate, map, InternalIndex(i), PropertyKind::kData,
                            NONE, Representation::Double(), any_type);
    maps[i] = new_map;

    expectations.SetDataField(i, PropertyConstness::kMutable,
                              Representation::Double(), any_type);

    CHECK(!map->is_stable());
    CHECK(map->is_deprecated());
    CHECK_NE(*map, *new_map);
    CHECK(i == 0 || maps[i - 1]->is_deprecated());

    CHECK(!new_map->is_deprecated());
    CHECK(expectations.Check(*new_map));
  }

  Handle<Map> active_map = maps[kPropCount - 1];
  CHECK(!active_map->is_deprecated());

  // Update all deprecated maps and check that they are now the same.
  Handle<Map> updated_map = Map::Update(isolate, map);
  CHECK_EQ(*active_map, *updated_map);
  CheckMigrationTarget(isolate, *map, *updated_map);
  for (int i = 0; i < kPropCount; i++) {
    updated_map = Map::Update(isolate, maps[i]);
    CHECK_EQ(*active_map, *updated_map);
    CheckMigrationTarget(isolate, *maps[i], *updated_map);
  }
}

////////////////////////////////////////////////////////////////////////////////
// A set of tests for attribute reconfiguration case.
//

namespace {

// This test ensures that field generalization is correctly propagated from one
// branch of transition tree (|map2|) to another (|map|).
//
//             + - p2B - p3 - p4: |map2|
//             |
//  {} - p0 - p1 - p2A - p3 - p4: |map|
//
// where "p2A" and "p2B" differ only in the attributes.
//
void TestReconfigureDataFieldAttribute_GeneralizeField(
    const CRFTData& from, const CRFTData& to, const CRFTData& expected,
    ChangeAlertMechanism expected_alert) {
  Isolate* isolate = CcTest::i_isolate();

  Expectations expectations(isolate);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  for (int i = 0; i < kPropCount; i++) {
    map = expectations.AddDataField(map, NONE, from.constness,
                                    from.representation, from.type);
  }
  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());
  CHECK(expectations.Check(*map));

  // Create another branch in transition tree (property at index |kSplitProp|
  // has different attributes), initialize expectations.
  const int kSplitProp = kPropCount / 2;
  Expectations expectations2(isolate);

  Handle<Map> map2 = initial_map;
  for (int i = 0; i < kSplitProp; i++) {
    map2 = expectations2.FollowDataTransition(map2, NONE, from.constness,
                                              from.representation, from.type);
  }
  map2 = expectations2.AddDataField(map2, READ_ONLY, to.constness,
                                    to.representation, to.type);

  for (int i = kSplitProp + 1; i < kPropCount; i++) {
    map2 = expectations2.AddDataField(map2, NONE, to.constness,
                                      to.representation, to.type);
  }
  CHECK(!map2->is_deprecated());
  CHECK(map2->is_stable());
  CHECK(expectations2.Check(*map2));

  // Create dummy optimized code object to test correct dependencies
  // on the field owner.
  Handle<Code> code_field_type = CreateDummyOptimizedCode(isolate);
  Handle<Code> code_field_repr = CreateDummyOptimizedCode(isolate);
  Handle<Code> code_field_const = CreateDummyOptimizedCode(isolate);
  Handle<Code> code_src_field_const = CreateDummyOptimizedCode(isolate);
  {
    Handle<Map> field_owner(
        map->FindFieldOwner(isolate, InternalIndex(kSplitProp)), isolate);
    DependentCode::InstallDependency(isolate, code_field_type, field_owner,
                                     DependentCode::kFieldTypeGroup);
    DependentCode::InstallDependency(isolate, code_field_repr, field_owner,
                                     DependentCode::kFieldRepresentationGroup);
    DependentCode::InstallDependency(isolate, code_field_const, field_owner,
                                     DependentCode::kFieldConstGroup);
  }
  {
    Handle<Map> field_owner(
        map2->FindFieldOwner(isolate, InternalIndex(kSplitProp)), isolate);
    DependentCode::InstallDependency(isolate, code_src_field_const, field_owner,
                                     DependentCode::kFieldConstGroup);
  }
  CHECK(!code_field_type->marked_for_deoptimization());
  CHECK(!code_field_repr->marked_for_deoptimization());
  CHECK(!code_field_const->marked_for_deoptimization());
  CHECK(!code_src_field_const->marked_for_deoptimization());

  // Reconfigure attributes of property |kSplitProp| of |map2| to NONE, which
  // should generalize representations in |map1|.
  Handle<Map> new_map = MapUpdater::ReconfigureExistingProperty(
      isolate, map2, InternalIndex(kSplitProp), PropertyKind::kData, NONE,
      PropertyConstness::kConst);

  // |map2| should be mosly left unchanged but marked unstable and if the
  // source property was constant it should also be transitioned to kMutable.
  CHECK(!map2->is_stable());
  CHECK(!map2->is_deprecated());
  CHECK_NE(*map2, *new_map);
  CHECK(!code_src_field_const->marked_for_deoptimization());
  CHECK(expectations2.Check(*map2));

  for (int i = kSplitProp; i < kPropCount; i++) {
    expectations.SetDataField(i, expected.constness, expected.representation,
                              expected.type);
  }

  if (expected_alert == kDeprecation) {
    // |map| should be deprecated and |new_map| should match new expectations.
    CHECK(map->is_deprecated());
    CHECK(!code_field_type->marked_for_deoptimization());
    CHECK(!code_field_repr->marked_for_deoptimization());
    CHECK(!code_field_const->marked_for_deoptimization());
    CHECK_NE(*map, *new_map);

    CHECK(!new_map->is_deprecated());
    CHECK(expectations.Check(*new_map));

    // Update deprecated |map|, it should become |new_map|.
    Handle<Map> updated_map = Map::Update(isolate, map);
    CHECK_EQ(*new_map, *updated_map);
    CheckMigrationTarget(isolate, *map, *updated_map);
  } else {
    CHECK(expected_alert == kFieldOwnerDependency ||
          expected_alert == kNoAlert);
    // In case of in-place generalization |map| should be returned as a result
    // of the property reconfiguration, respective field types should be
    // generalized and respective code dependencies should be invalidated.
    // |map| should be NOT deprecated and it should match new expectations.
    CHECK(!map->is_deprecated());
    CHECK_EQ(*map, *new_map);
    bool expect_deopt = expected_alert == kFieldOwnerDependency;
    CheckCodeObjectForDeopt(from, expected, code_field_type, code_field_repr,
                            code_field_const, expect_deopt);

    CHECK(!new_map->is_deprecated());
    CHECK(expectations.Check(*new_map));

    Handle<Map> updated_map = Map::Update(isolate, map);
    CHECK_EQ(*new_map, *updated_map);
  }
}

}  // namespace

TEST(ReconfigureDataFieldAttribute_GeneralizeSmiFieldToDouble) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kConst, Representation::Double(), any_type},
      kDeprecation);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      kDeprecation);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      kDeprecation);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      kDeprecation);
}

TEST(ReconfigureDataFieldAttribute_GeneralizeSmiFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kConst, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

TEST(ReconfigureDataFieldAttribute_GeneralizeDoubleFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kConst, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

TEST(ReconfigureDataFieldAttribute_GeneralizeHeapObjFieldToHeapObj) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  Handle<FieldType> current_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  Handle<FieldType> new_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  Handle<FieldType> expected_type = any_type;

  // Check generalizations that trigger deopts.
  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::HeapObject(), current_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kConst, Representation::HeapObject(), expected_type},
      kFieldOwnerDependency);

  // PropertyConstness::kConst to PropertyConstness::kMutable migration does
  // not create a new map, therefore trivial generalization.
  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::HeapObject(), current_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(),
       expected_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), current_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(),
       expected_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), current_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(),
       expected_type},
      kFieldOwnerDependency);
  current_type = expected_type;

  // Check generalizations that do not trigger deopts.
  new_type = FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::HeapObject(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kConst, Representation::HeapObject(), any_type},
      kNoAlert);

  // PropertyConstness::kConst to PropertyConstness::kMutable migration does
  // not create a new map, therefore trivial generalization.
  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kConst, Representation::HeapObject(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      kFieldOwnerDependency);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      kNoAlert);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      kNoAlert);
}

TEST(ReconfigureDataFieldAttribute_GeneralizeHeapObjectFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureDataFieldAttribute_GeneralizeField(
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

// Checks that given |map| is deprecated and that it updates to given |new_map|
// which in turn should match expectations.
struct CheckDeprecated {
  void Check(Isolate* isolate, Handle<Map> map, Handle<Map> new_map,
             const Expectations& expectations) {
    CHECK(map->is_deprecated());
    CHECK_NE(*map, *new_map);

    CHECK(!new_map->is_deprecated());
    CHECK(expectations.Check(*new_map));

    // Update deprecated |map|, it should become |new_map|.
    Handle<Map> updated_map = Map::Update(isolate, map);
    CHECK_EQ(*new_map, *updated_map);
    CheckMigrationTarget(isolate, *map, *updated_map);
  }
};

// Checks that given |map| is NOT deprecated, equals to given |new_map| and
// matches expectations.
struct CheckSameMap {
  void Check(Isolate* isolate, Handle<Map> map, Handle<Map> new_map,
             const Expectations& expectations) {
    // |map| was not reconfigured, therefore it should stay stable.
    CHECK(map->is_stable());
    CHECK(!map->is_deprecated());
    CHECK_EQ(*map, *new_map);

    CHECK(!new_map->is_deprecated());
    CHECK(expectations.Check(*new_map));

    // Update deprecated |map|, it should become |new_map|.
    Handle<Map> updated_map = Map::Update(isolate, map);
    CHECK_EQ(*new_map, *updated_map);
  }
};

// Checks that given |map| is NOT deprecated and matches expectations.
// |new_map| is unrelated to |map|.
struct CheckUnrelated {
  void Check(Isolate* isolate, Handle<Map> map, Handle<Map> new_map,
             const Expectations& expectations) {
    CHECK(!map->is_deprecated());
    CHECK_NE(*map, *new_map);
    CHECK(expectations.Check(*map));

    CHECK(new_map->is_stable());
    CHECK(!new_map->is_deprecated());
  }
};

// Checks that given |map| is NOT deprecated, and |new_map| is a result of going
// dictionary mode.
struct CheckNormalize {
  void Check(Isolate* isolate, Handle<Map> map, Handle<Map> new_map,
             const Expectations& expectations) {
    CHECK(!map->is_deprecated());
    CHECK_NE(*map, *new_map);

    CHECK(IsUndefined(new_map->GetBackPointer(), isolate));
    CHECK(!new_map->is_deprecated());
    CHECK(expectations.CheckNormalized(*new_map));
  }
};

// This test ensures that field generalization is correctly propagated from one
// branch of transition tree (|map2|) to another (|map1|).
//
//             + - p2B - p3 - p4: |map2|
//             |
//  {} - p0 - p1: |map|
//             |
//             + - p2A - p3 - p4: |map1|
//                        |
//                        + - the property customized by the TestConfig provided
//
// where "p2A" and "p2B" differ only in the attributes.
//
template <typename TestConfig, typename Checker>
static void TestReconfigureProperty_CustomPropertyAfterTargetMap(
    TestConfig* config, Checker* checker) {
  Isolate* isolate = CcTest::i_isolate();
  Handle<FieldType> any_type = FieldType::Any(isolate);

  const int kCustomPropIndex = kPropCount - 2;
  Expectations expectations(isolate);

  const int kSplitProp = 2;
  CHECK_LT(kSplitProp, kCustomPropIndex);

  const PropertyConstness constness = PropertyConstness::kMutable;
  const Representation representation = Representation::Smi();

  // Create common part of transition tree.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  for (int i = 0; i < kSplitProp; i++) {
    map = expectations.AddDataField(map, NONE, constness, representation,
                                    any_type);
  }
  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());
  CHECK(expectations.Check(*map));

  // Create branch to |map1|.
  Handle<Map> map1 = map;
  Expectations expectations1 = expectations;
  for (int i = kSplitProp; i < kCustomPropIndex; i++) {
    map1 = expectations1.AddDataField(map1, NONE, constness, representation,
                                      any_type);
  }
  map1 = config->AddPropertyAtBranch(1, &expectations1, map1);
  for (int i = kCustomPropIndex + 1; i < kPropCount; i++) {
    map1 = expectations1.AddDataField(map1, NONE, constness, representation,
                                      any_type);
  }
  CHECK(!map1->is_deprecated());
  CHECK(map1->is_stable());
  CHECK(expectations1.Check(*map1));

  // Create another branch in transition tree (property at index |kSplitProp|
  // has different attributes), initialize expectations.
  Handle<Map> map2 = map;
  Expectations expectations2 = expectations;
  map2 = expectations2.AddDataField(map2, READ_ONLY, constness, representation,
                                    any_type);
  for (int i = kSplitProp + 1; i < kCustomPropIndex; i++) {
    map2 = expectations2.AddDataField(map2, NONE, constness, representation,
                                      any_type);
  }
  map2 = config->AddPropertyAtBranch(2, &expectations2, map2);
  for (int i = kCustomPropIndex + 1; i < kPropCount; i++) {
    map2 = expectations2.AddDataField(map2, NONE, constness, representation,
                                      any_type);
  }
  CHECK(!map2->is_deprecated());
  CHECK(map2->is_stable());
  CHECK(expectations2.Check(*map2));

  // Reconfigure attributes of property |kSplitProp| of |map2| to NONE, which
  // should generalize representations in |map1|.
  Handle<Map> new_map = MapUpdater::ReconfigureExistingProperty(
      isolate, map2, InternalIndex(kSplitProp), PropertyKind::kData, NONE,
      PropertyConstness::kConst);

  // |map2| should be left unchanged but marked unstable.
  CHECK(!map2->is_stable());
  CHECK(!map2->is_deprecated());
  CHECK_NE(*map2, *new_map);
  CHECK(expectations2.Check(*map2));

  config->UpdateExpectations(kCustomPropIndex, &expectations1);
  checker->Check(isolate, map1, new_map, expectations1);
}

TEST(ReconfigureDataFieldAttribute_SameDataConstantAfterTargetMap) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  struct TestConfig {
    Handle<JSFunction> js_func_;
    TestConfig() {
      Isolate* isolate = CcTest::i_isolate();
      Factory* factory = isolate->factory();
      js_func_ = factory->NewFunctionForTesting(factory->empty_string());
    }

    Handle<Map> AddPropertyAtBranch(int branch_id, Expectations* expectations,
                                    Handle<Map> map) {
      CHECK(branch_id == 1 || branch_id == 2);
      // Add the same data constant property at both transition tree branches.
      return expectations->AddDataConstant(map, NONE, js_func_);
    }

    void UpdateExpectations(int property_index, Expectations* expectations) {
      // Expectations stay the same.
    }
  };

  TestConfig config;
  // Two branches are "compatible" so the |map1| should NOT be deprecated.
  CheckSameMap checker;
  TestReconfigureProperty_CustomPropertyAfterTargetMap(&config, &checker);
}

TEST(ReconfigureDataFieldAttribute_DataConstantToDataFieldAfterTargetMap) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  struct TestConfig {
    Handle<JSFunction> js_func1_;
    Handle<JSFunction> js_func2_;
    Handle<FieldType> function_type_;
    TestConfig() {
      Isolate* isolate = CcTest::i_isolate();
      Factory* factory = isolate->factory();
      Handle<String> name = factory->empty_string();
      Handle<Map> sloppy_map =
          Map::CopyInitialMap(isolate, isolate->sloppy_function_map());
      Handle<SharedFunctionInfo> info =
          factory->NewSharedFunctionInfoForBuiltin(name, Builtin::kIllegal);
      function_type_ = FieldType::Class(sloppy_map, isolate);
      CHECK(sloppy_map->is_stable());

      js_func1_ =
          Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
              .set_map(sloppy_map)
              .Build();

      js_func2_ =
          Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
              .set_map(sloppy_map)
              .Build();
    }

    Handle<Map> AddPropertyAtBranch(int branch_id, Expectations* expectations,
                                    Handle<Map> map) {
      CHECK(branch_id == 1 || branch_id == 2);
      Handle<JSFunction> js_func = branch_id == 1 ? js_func1_ : js_func2_;
      return expectations->AddDataConstant(map, NONE, js_func);
    }

    void UpdateExpectations(int property_index, Expectations* expectations) {
      expectations->SetDataField(property_index, PropertyConstness::kConst,
                                 Representation::HeapObject(), function_type_);
    }
  };

  TestConfig config;
  CheckSameMap checker;
  TestReconfigureProperty_CustomPropertyAfterTargetMap(&config, &checker);
}

TEST(ReconfigureDataFieldAttribute_DataConstantToAccConstantAfterTargetMap) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  struct TestConfig {
    Handle<JSFunction> js_func_;
    Handle<AccessorPair> pair_;
    TestConfig() {
      Isolate* isolate = CcTest::i_isolate();
      Factory* factory = isolate->factory();
      js_func_ = factory->NewFunctionForTesting(factory->empty_string());
      pair_ = CreateAccessorPair(true, true);
    }

    Handle<Map> AddPropertyAtBranch(int branch_id, Expectations* expectations,
                                    Handle<Map> map) {
      CHECK(branch_id == 1 || branch_id == 2);
      if (branch_id == 1) {
        return expectations->AddDataConstant(map, NONE, js_func_);
      } else {
        return expectations->AddAccessorConstant(map, NONE, pair_);
      }
    }

    void UpdateExpectations(int property_index, Expectations* expectations) {}
  };

  TestConfig config;
  // These are completely separate branches in transition tree.
  CheckUnrelated checker;
  TestReconfigureProperty_CustomPropertyAfterTargetMap(&config, &checker);
}


TEST(ReconfigureDataFieldAttribute_SameAccessorConstantAfterTargetMap) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  struct TestConfig {
    Handle<AccessorPair> pair_;
    TestConfig() { pair_ = CreateAccessorPair(true, true); }

    Handle<Map> AddPropertyAtBranch(int branch_id, Expectations* expectations,
                                    Handle<Map> map) {
      CHECK(branch_id == 1 || branch_id == 2);
      // Add the same accessor constant property at both transition tree
      // branches.
      return expectations->AddAccessorConstant(map, NONE, pair_);
    }

    void UpdateExpectations(int property_index, Expectations* expectations) {
      // Two branches are "compatible" so the |map1| should NOT be deprecated.
    }
  };

  TestConfig config;
  CheckSameMap checker;
  TestReconfigureProperty_CustomPropertyAfterTargetMap(&config, &checker);
}


TEST(ReconfigureDataFieldAttribute_AccConstantToAccFieldAfterTargetMap) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  struct TestConfig {
    Handle<AccessorPair> pair1_;
    Handle<AccessorPair> pair2_;
    TestConfig() {
      pair1_ = CreateAccessorPair(true, true);
      pair2_ = CreateAccessorPair(true, true);
    }

    Handle<Map> AddPropertyAtBranch(int branch_id, Expectations* expectations,
                                    Handle<Map> map) {
      CHECK(branch_id == 1 || branch_id == 2);
      Handle<AccessorPair> pair = branch_id == 1 ? pair1_ : pair2_;
      return expectations->AddAccessorConstant(map, NONE, pair);
    }

    void UpdateExpectations(int property_index, Expectations* expectations) {
      if (IS_ACCESSOR_FIELD_SUPPORTED) {
        expectations->SetAccessorField(property_index);
      } else {
        // Currently we have a normalize case and ACCESSOR property becomes
        // ACCESSOR_CONSTANT.
        expectations->SetAccessorConstant(property_index, pair2_);
      }
    }
  };

  TestConfig config;
  if (IS_ACCESSOR_FIELD_SUPPORTED) {
    CheckSameMap checker;
    TestReconfigureProperty_CustomPropertyAfterTargetMap(&config, &checker);
  } else {
    // Currently we have a normalize case.
    CheckNormalize checker;
    TestReconfigureProperty_CustomPropertyAfterTargetMap(&config, &checker);
  }
}


TEST(ReconfigureDataFieldAttribute_AccConstantToDataFieldAfterTargetMap) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  struct TestConfig {
    Handle<AccessorPair> pair_;
    TestConfig() { pair_ = CreateAccessorPair(true, true); }

    Handle<Map> AddPropertyAtBranch(int branch_id, Expectations* expectations,
                                    Handle<Map> map) {
      CHECK(branch_id == 1 || branch_id == 2);
      if (branch_id == 1) {
        return expectations->AddAccessorConstant(map, NONE, pair_);
      } else {
        Isolate* isolate = CcTest::i_isolate();
        Handle<FieldType> any_type = FieldType::Any(isolate);
        return expectations->AddDataField(map, NONE, PropertyConstness::kConst,
                                          Representation::Smi(), any_type);
      }
    }

    void UpdateExpectations(int property_index, Expectations* expectations) {}
  };

  TestConfig config;
  // These are completely separate branches in transition tree.
  CheckUnrelated checker;
  TestReconfigureProperty_CustomPropertyAfterTargetMap(&config, &checker);
}


////////////////////////////////////////////////////////////////////////////////
// A set of tests for elements kind reconfiguration case.
//

namespace {

// This test ensures that in-place field generalization is correctly propagated
// from one branch of transition tree (|map2|) to another (|map|).
//
//   + - p0 - p1 - p2A - p3 - p4: |map|
//   |
//  ek
//   |
//  {} - p0 - p1 - p2B - p3 - p4: |map2|
//
// where "p2A" and "p2B" differ only in the representation/field type.
//
static void TestReconfigureElementsKind_GeneralizeFieldInPlace(
    const CRFTData& from, const CRFTData& to, const CRFTData& expected) {
  Isolate* isolate = CcTest::i_isolate();

  Expectations expectations(isolate, PACKED_SMI_ELEMENTS);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map =
      isolate->factory()->NewContextfulMapForCurrentContext(
          JS_ARRAY_TYPE, JSArray::kHeaderSize, PACKED_SMI_ELEMENTS);
  initial_map->SetConstructor(*isolate->object_function());

  Handle<Map> map = initial_map;
  map = expectations.AsElementsKind(map, PACKED_ELEMENTS);
  for (int i = 0; i < kPropCount; i++) {
    map = expectations.AddDataField(map, NONE, from.constness,
                                    from.representation, from.type);
  }
  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());
  CHECK(expectations.Check(*map));

  // Create another branch in transition tree (property at index |kDiffProp|
  // has different attributes), initialize expectations.
  const int kDiffProp = kPropCount / 2;
  Expectations expectations2(isolate, PACKED_SMI_ELEMENTS);

  Handle<Map> map2 = initial_map;
  for (int i = 0; i < kPropCount; i++) {
    if (i == kDiffProp) {
      map2 = expectations2.AddDataField(map2, NONE, to.constness,
                                        to.representation, to.type);
    } else {
      map2 = expectations2.AddDataField(map2, NONE, from.constness,
                                        from.representation, from.type);
    }
  }
  CHECK(!map2->is_deprecated());
  CHECK(map2->is_stable());
  CHECK(expectations2.Check(*map2));

  // Create dummy optimized code object to test correct dependencies
  // on the field owner.
  Handle<Code> code_field_type = CreateDummyOptimizedCode(isolate);
  Handle<Code> code_field_repr = CreateDummyOptimizedCode(isolate);
  Handle<Code> code_field_const = CreateDummyOptimizedCode(isolate);
  Handle<Map> field_owner(
      map->FindFieldOwner(isolate, InternalIndex(kDiffProp)), isolate);
  DependentCode::InstallDependency(isolate, code_field_type, field_owner,
                                   DependentCode::kFieldTypeGroup);
  DependentCode::InstallDependency(isolate, code_field_repr, field_owner,
                                   DependentCode::kFieldRepresentationGroup);
  DependentCode::InstallDependency(isolate, code_field_const, field_owner,
                                   DependentCode::kFieldConstGroup);
  CHECK(!code_field_type->marked_for_deoptimization());
  CHECK(!code_field_repr->marked_for_deoptimization());
  CHECK(!code_field_const->marked_for_deoptimization());

  // Reconfigure elements kinds of |map2|, which should generalize
  // representations in |map|.
  Handle<Map> new_map =
      MapUpdater{isolate, map2}.ReconfigureElementsKind(PACKED_ELEMENTS);

  // |map2| should be left unchanged but marked unstable.
  CHECK(!map2->is_stable());
  CHECK(!map2->is_deprecated());
  CHECK_NE(*map2, *new_map);
  CHECK(expectations2.Check(*map2));

  // In case of in-place generalization |map| should be returned as a result of
  // the elements kind reconfiguration, respective field types should be
  // generalized and respective code dependencies should be invalidated.
  // |map| should be NOT deprecated and it should match new expectations.
  expectations.SetDataField(kDiffProp, expected.constness,
                            expected.representation, expected.type);
  CHECK(!map->is_deprecated());
  CHECK_EQ(*map, *new_map);
  CHECK_EQ(IsGeneralizableTo(to.constness, from.constness),
           !code_field_const->marked_for_deoptimization());
  CheckCodeObjectForDeopt(from, expected, code_field_type, code_field_repr,
                          Handle<Code>(), false);

  CHECK(!new_map->is_deprecated());
  CHECK(expectations.Check(*new_map));

  Handle<Map> updated_map = Map::Update(isolate, map);
  CHECK_EQ(*new_map, *updated_map);

  // Ensure Map::FindElementsKindTransitionedMap() is able to find the
  // transitioned map.
  {
    MapHandles map_list;
    map_list.push_back(updated_map);
    Tagged<Map> transitioned_map = map2->FindElementsKindTransitionedMap(
        isolate, map_list, ConcurrencyMode::kSynchronous);
    CHECK_EQ(*updated_map, transitioned_map);
  }
}

}  // namespace

TEST(ReconfigureElementsKind_GeneralizeSmiFieldToDouble) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kConst, Representation::Double(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::Double(), any_type});
}

TEST(ReconfigureElementsKind_GeneralizeSmiFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kConst, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});
}

TEST(ReconfigureElementsKind_GeneralizeDoubleFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kConst, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});
}

TEST(ReconfigureElementsKind_GeneralizeHeapObjFieldToHeapObj) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  Handle<FieldType> current_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  Handle<FieldType> new_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  Handle<FieldType> expected_type = any_type;

  // Check generalizations that trigger deopts.
  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::HeapObject(), current_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kConst, Representation::HeapObject(), expected_type});

  // PropertyConstness::kConst to PropertyConstness::kMutable migration does
  // not create a new map, therefore trivial generalization.
  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::HeapObject(), current_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(),
       expected_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::HeapObject(), current_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(),
       expected_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::HeapObject(), current_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(),
       expected_type});
  current_type = expected_type;

  // Check generalizations that do not trigger deopts.
  new_type = FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::HeapObject(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kConst, Representation::HeapObject(), any_type});

  // PropertyConstness::kConst to PropertyConstness::kMutable migration does
  // not create a new map, therefore trivial generalization.
  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::HeapObject(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      {PropertyConstness::kConst, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), new_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), any_type});
}

TEST(ReconfigureElementsKind_GeneralizeHeapObjectFieldToTagged) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kConst, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kConst, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kConst, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});

  TestReconfigureElementsKind_GeneralizeFieldInPlace(
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type});
}

////////////////////////////////////////////////////////////////////////////////
// A set of tests checking split map deprecation.
//

TEST(ReconfigurePropertySplitMapTransitionsOverflow) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  Expectations expectations(isolate);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  for (int i = 0; i < kPropCount; i++) {
    map = expectations.AddDataField(map, NONE, PropertyConstness::kMutable,
                                    Representation::Smi(), any_type);
  }
  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());

  // Generalize representation of property at index |kSplitProp|.
  const int kSplitProp = kPropCount / 2;
  Handle<Map> split_map;
  Handle<Map> map2 = initial_map;
  {
    for (int i = 0; i < kSplitProp + 1; i++) {
      if (i == kSplitProp) {
        split_map = map2;
      }

      Handle<String> name = CcTest::MakeName("prop", i);
      MaybeHandle<Map> target = TransitionsAccessor::SearchTransition(
          isolate, map2, *name, PropertyKind::kData, NONE);
      CHECK(!target.is_null());
      map2 = target.ToHandleChecked();
    }

    map2 = ReconfigureProperty(isolate, map2, InternalIndex(kSplitProp),
                               PropertyKind::kData, NONE,
                               Representation::Double(), any_type);
    expectations.SetDataField(kSplitProp, PropertyConstness::kMutable,
                              Representation::Double(), any_type);

    CHECK(expectations.Check(*split_map, kSplitProp));
    CHECK(expectations.Check(*map2, kSplitProp + 1));
  }

  // At this point |map| should be deprecated and disconnected from the
  // transition tree.
  CHECK(map->is_deprecated());
  CHECK(!split_map->is_deprecated());
  CHECK(map2->is_stable());
  CHECK(!map2->is_deprecated());

  // Fill in transition tree of |map2| so that it can't have more transitions.
  for (int i = 0; i < TransitionsAccessor::kMaxNumberOfTransitions; i++) {
    CHECK(TransitionsAccessor::CanHaveMoreTransitions(isolate, map2));
    Handle<String> name = CcTest::MakeName("foo", i);
    Map::CopyWithField(isolate, map2, name, any_type, NONE,
                       PropertyConstness::kMutable, Representation::Smi(),
                       INSERT_TRANSITION)
        .ToHandleChecked();
  }
  CHECK(!TransitionsAccessor::CanHaveMoreTransitions(isolate, map2));

  // Try to update |map|, since there is no place for propX transition at |map2|
  // |map| should become normalized.
  Handle<Map> updated_map = Map::Update(isolate, map);

  CheckNormalize checker;
  checker.Check(isolate, map2, updated_map, expectations);
}

////////////////////////////////////////////////////////////////////////////////
// A set of tests involving special transitions (such as elements kind
// transition, observed transition or prototype transition).
//

// This test ensures that field generalization is correctly propagated from one
// branch of transition tree (|map2|) to another (|map|).
//
//                            p4B: |map2|
//                             |
//                             * - special transition
//                             |
//  {} - p0 - p1 - p2A - p3 - p4A: |map|
//
// where "p4A" and "p4B" are exactly the same properties.
//
// TODO(ishell): unify this test template with
// TestReconfigureDataFieldAttribute_GeneralizeField once
// IS_PROTO_TRANS_ISSUE_FIXED and IS_NON_EQUIVALENT_TRANSITION_SUPPORTED are
// fixed.
template <typename TestConfig>
static void TestGeneralizeFieldWithSpecialTransition(
    TestConfig* config, const CRFTData& from, const CRFTData& to,
    const CRFTData& expected, ChangeAlertMechanism expected_alert) {
  Isolate* isolate = CcTest::i_isolate();

  Expectations expectations(isolate);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  for (int i = 0; i < kPropCount; i++) {
    map = expectations.AddDataField(map, NONE, from.constness,
                                    from.representation, from.type);
  }
  CHECK(!map->is_deprecated());
  CHECK(map->is_stable());
  CHECK(expectations.Check(*map));

  Expectations expectations2 = expectations;

  // Apply some special transition to |map|.
  CHECK(map->owns_descriptors());
  Handle<Map> map2 = config->Transition(map, &expectations2);

  // |map| should still match expectations.
  CHECK(!map->is_deprecated());
  CHECK(expectations.Check(*map));

  if (config->generalizes_representations()) {
    for (int i = 0; i < kPropCount; i++) {
      expectations2.GeneralizeField(i);
    }
  }

  CHECK(!map2->is_deprecated());
  CHECK(map2->is_stable());
  CHECK(expectations2.Check(*map2));

  // Create new maps by generalizing representation of propX field.
  Handle<Map> maps[kPropCount];
  for (int i = 0; i < kPropCount; i++) {
    Handle<Map> new_map =
        ReconfigureProperty(isolate, map, InternalIndex(i), PropertyKind::kData,
                            NONE, to.representation, to.type);
    maps[i] = new_map;

    expectations.SetDataField(i, expected.constness, expected.representation,
                              expected.type);

    switch (expected_alert) {
      case kDeprecation: {
        CHECK(map->is_deprecated());
        CHECK_NE(*map, *new_map);
        CHECK(i == 0 || maps[i - 1]->is_deprecated());
        CHECK(expectations.Check(*new_map));

        Handle<Map> new_map2 = Map::Update(isolate, map2);
        CHECK(!new_map2->is_deprecated());
        CHECK(!new_map2->is_dictionary_map());

        Handle<Map> tmp_map;
        if (Map::TryUpdate(isolate, map2).ToHandle(&tmp_map)) {
          // If Map::TryUpdate() manages to succeed the result must match the
          // result of Map::Update().
          CHECK_EQ(*new_map2, *tmp_map);
        } else {
          // Equivalent transitions should always find the updated map.
          CHECK(config->is_non_equivalent_transition());
        }

        if (config->is_non_equivalent_transition()) {
          // In case of non-equivalent transition currently we generalize all
          // representations.
          for (int j = 0; j < kPropCount; j++) {
            expectations2.GeneralizeField(j);
          }
          CHECK(IsUndefined(new_map2->GetBackPointer(), isolate));
          CHECK(expectations2.Check(*new_map2));
        } else {
          expectations2.SetDataField(i, expected.constness,
                                     expected.representation, expected.type);

          CHECK(!IsUndefined(new_map2->GetBackPointer(), isolate));
          CHECK(expectations2.Check(*new_map2));
        }
        break;
      }
      case kFieldOwnerDependency: {
        CHECK(!map->is_deprecated());
        // TODO(ishell): Review expectations once IS_PROTO_TRANS_ISSUE_FIXED is
        // removed.
        CHECK(!IS_PROTO_TRANS_ISSUE_FIXED);
        CHECK_EQ(*map, *new_map);
        CHECK(expectations.Check(*new_map));

        CHECK(!map2->is_deprecated());
        CHECK_NE(*map2, *new_map);
        expectations2.SetDataField(i, expected.constness,
                                   expected.representation, expected.type);
        CHECK(expectations2.Check(*map2));
        break;
      }
      case kNoAlert:
        UNREACHABLE();
        break;
    }
  }

  Handle<Map> active_map = maps[kPropCount - 1];
  CHECK(!active_map->is_deprecated());

  // Update all deprecated maps and check that they are now the same.
  Handle<Map> updated_map = Map::Update(isolate, map);
  CHECK_EQ(*active_map, *updated_map);
  CheckMigrationTarget(isolate, *map, *updated_map);
  for (int i = 0; i < kPropCount; i++) {
    updated_map = Map::Update(isolate, maps[i]);
    CHECK_EQ(*active_map, *updated_map);
    CheckMigrationTarget(isolate, *maps[i], *updated_map);
  }
}

TEST(ElementsKindTransitionFromMapOwningDescriptor) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  struct TestConfig {
    TestConfig(PropertyAttributes attributes, Handle<Symbol> symbol,
               ElementsKind kind)
        : attributes(attributes), symbol(symbol), elements_kind(kind) {}

    Handle<Map> Transition(Handle<Map> map, Expectations* expectations) {
      expectations->SetElementsKind(elements_kind);
      expectations->ChangeAttributesForAllProperties(attributes);
      return Map::CopyForPreventExtensions(CcTest::i_isolate(), map, attributes,
                                           symbol, "CopyForPreventExtensions");
    }
    // TODO(ishell): remove once IS_PROTO_TRANS_ISSUE_FIXED is removed.
    bool generalizes_representations() const { return false; }
    bool is_non_equivalent_transition() const { return false; }

    PropertyAttributes attributes;
    Handle<Symbol> symbol;
    ElementsKind elements_kind;
  };
  Factory* factory = isolate->factory();
  TestConfig configs[] = {
      {FROZEN, factory->frozen_symbol(),
       v8_flags.enable_sealed_frozen_elements_kind ? HOLEY_FROZEN_ELEMENTS
                                                   : DICTIONARY_ELEMENTS},
      {SEALED, factory->sealed_symbol(),
       v8_flags.enable_sealed_frozen_elements_kind ? HOLEY_SEALED_ELEMENTS
                                                   : DICTIONARY_ELEMENTS},
      {NONE, factory->nonextensible_symbol(),
       v8_flags.enable_sealed_frozen_elements_kind
           ? HOLEY_NONEXTENSIBLE_ELEMENTS
           : DICTIONARY_ELEMENTS}};
  for (size_t i = 0; i < arraysize(configs); i++) {
    TestGeneralizeFieldWithSpecialTransition(
        &configs[i],
        {PropertyConstness::kMutable, Representation::Smi(), any_type},
        {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
        {PropertyConstness::kMutable, Representation::Tagged(), any_type},
        kFieldOwnerDependency);

    TestGeneralizeFieldWithSpecialTransition(
        &configs[i],
        {PropertyConstness::kMutable, Representation::Double(), any_type},
        {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
        {PropertyConstness::kMutable, Representation::Tagged(), any_type},
        kFieldOwnerDependency);
  }
}

TEST(ElementsKindTransitionFromMapNotOwningDescriptor) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  struct TestConfig {
    TestConfig(PropertyAttributes attributes, Handle<Symbol> symbol,
               ElementsKind kind)
        : attributes(attributes), symbol(symbol), elements_kind(kind) {}

    Handle<Map> Transition(Handle<Map> map, Expectations* expectations) {
      Isolate* isolate = CcTest::i_isolate();
      Handle<FieldType> any_type = FieldType::Any(isolate);

      // Add one more transition to |map| in order to prevent descriptors
      // ownership.
      CHECK(map->owns_descriptors());
      Map::CopyWithField(isolate, map, CcTest::MakeString("foo"), any_type,
                         NONE, PropertyConstness::kMutable,
                         Representation::Smi(), INSERT_TRANSITION)
          .ToHandleChecked();
      CHECK(!map->owns_descriptors());

      expectations->SetElementsKind(elements_kind);
      expectations->ChangeAttributesForAllProperties(attributes);
      return Map::CopyForPreventExtensions(isolate, map, attributes, symbol,
                                           "CopyForPreventExtensions");
    }
    // TODO(ishell): remove once IS_PROTO_TRANS_ISSUE_FIXED is removed.
    bool generalizes_representations() const { return false; }
    bool is_non_equivalent_transition() const { return false; }

    PropertyAttributes attributes;
    Handle<Symbol> symbol;
    ElementsKind elements_kind;
  };
  Factory* factory = isolate->factory();
  TestConfig configs[] = {
      {FROZEN, factory->frozen_symbol(),
       v8_flags.enable_sealed_frozen_elements_kind ? HOLEY_FROZEN_ELEMENTS
                                                   : DICTIONARY_ELEMENTS},
      {SEALED, factory->sealed_symbol(),
       v8_flags.enable_sealed_frozen_elements_kind ? HOLEY_SEALED_ELEMENTS
                                                   : DICTIONARY_ELEMENTS},
      {NONE, factory->nonextensible_symbol(),
       v8_flags.enable_sealed_frozen_elements_kind
           ? HOLEY_NONEXTENSIBLE_ELEMENTS
           : DICTIONARY_ELEMENTS}};
  for (size_t i = 0; i < arraysize(configs); i++) {
    TestGeneralizeFieldWithSpecialTransition(
        &configs[i],
        {PropertyConstness::kMutable, Representation::Smi(), any_type},
        {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
        {PropertyConstness::kMutable, Representation::Tagged(), any_type},
        kFieldOwnerDependency);

    TestGeneralizeFieldWithSpecialTransition(
        &configs[i],
        {PropertyConstness::kMutable, Representation::Double(), any_type},
        {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
        {PropertyConstness::kMutable, Representation::Tagged(), any_type},
        kFieldOwnerDependency);
  }
}


TEST(PrototypeTransitionFromMapOwningDescriptor) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  struct TestConfig {
    Handle<JSObject> prototype_;

    TestConfig() {
      Isolate* isolate = CcTest::i_isolate();
      Factory* factory = isolate->factory();
      prototype_ = factory->NewJSObjectFromMap(Map::Create(isolate, 0));
    }

    Handle<Map> Transition(Handle<Map> map, Expectations* expectations) {
      return Map::TransitionToUpdatePrototype(CcTest::i_isolate(), map,
                                              prototype_);
    }
    // TODO(ishell): remove once IS_PROTO_TRANS_ISSUE_FIXED is removed.
    bool generalizes_representations() const {
      return !IS_PROTO_TRANS_ISSUE_FIXED;
    }
    bool is_non_equivalent_transition() const { return true; }
  };
  TestConfig config;
  TestGeneralizeFieldWithSpecialTransition(
      &config, {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestGeneralizeFieldWithSpecialTransition(
      &config,
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

TEST(PrototypeTransitionFromMapNotOwningDescriptor) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);
  Handle<FieldType> value_type =
      FieldType::Class(Map::Create(isolate, 0), isolate);

  struct TestConfig {
    Handle<JSObject> prototype_;

    TestConfig() {
      Isolate* isolate = CcTest::i_isolate();
      Factory* factory = isolate->factory();
      prototype_ = factory->NewJSObjectFromMap(Map::Create(isolate, 0));
    }

    Handle<Map> Transition(Handle<Map> map, Expectations* expectations) {
      Isolate* isolate = CcTest::i_isolate();
      Handle<FieldType> any_type = FieldType::Any(isolate);

      // Add one more transition to |map| in order to prevent descriptors
      // ownership.
      CHECK(map->owns_descriptors());
      Map::CopyWithField(isolate, map, CcTest::MakeString("foo"), any_type,
                         NONE, PropertyConstness::kMutable,
                         Representation::Smi(), INSERT_TRANSITION)
          .ToHandleChecked();
      CHECK(!map->owns_descriptors());

      return Map::TransitionToUpdatePrototype(isolate, map, prototype_);
    }
    // TODO(ishell): remove once IS_PROTO_TRANS_ISSUE_FIXED is removed.
    bool generalizes_representations() const {
      return !IS_PROTO_TRANS_ISSUE_FIXED;
    }
    bool is_non_equivalent_transition() const { return true; }
  };
  TestConfig config;
  TestGeneralizeFieldWithSpecialTransition(
      &config, {PropertyConstness::kMutable, Representation::Smi(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);

  TestGeneralizeFieldWithSpecialTransition(
      &config,
      {PropertyConstness::kMutable, Representation::Double(), any_type},
      {PropertyConstness::kMutable, Representation::HeapObject(), value_type},
      {PropertyConstness::kMutable, Representation::Tagged(), any_type},
      kFieldOwnerDependency);
}

////////////////////////////////////////////////////////////////////////////////
// A set of tests for higher level transitioning mechanics.
//

struct TransitionToDataFieldOperator {
  PropertyConstness constness_;
  Representation representation_;
  PropertyAttributes attributes_;
  Handle<FieldType> heap_type_;
  Handle<Object> value_;

  TransitionToDataFieldOperator(PropertyConstness constness,
                                Representation representation,
                                Handle<FieldType> heap_type,
                                Handle<Object> value,
                                PropertyAttributes attributes = NONE)
      : constness_(constness),
        representation_(representation),
        attributes_(attributes),
        heap_type_(heap_type),
        value_(value) {}

  Handle<Map> DoTransition(Expectations* expectations, Handle<Map> map) {
    return expectations->TransitionToDataField(
        map, attributes_, constness_, representation_, heap_type_, value_);
  }
};


struct TransitionToDataConstantOperator {
  PropertyAttributes attributes_;
  Handle<JSFunction> value_;

  TransitionToDataConstantOperator(Handle<JSFunction> value,
                                   PropertyAttributes attributes = NONE)
      : attributes_(attributes), value_(value) {}

  Handle<Map> DoTransition(Expectations* expectations, Handle<Map> map) {
    return expectations->TransitionToDataConstant(map, attributes_, value_);
  }
};


struct TransitionToAccessorConstantOperator {
  PropertyAttributes attributes_;
  Handle<AccessorPair> pair_;

  TransitionToAccessorConstantOperator(Handle<AccessorPair> pair,
                                       PropertyAttributes attributes = NONE)
      : attributes_(attributes), pair_(pair) {}

  Handle<Map> DoTransition(Expectations* expectations, Handle<Map> map) {
    return expectations->TransitionToAccessorConstant(map, attributes_, pair_);
  }
};


struct ReconfigureAsDataPropertyOperator {
  InternalIndex descriptor_;
  Representation representation_;
  PropertyAttributes attributes_;
  Handle<FieldType> heap_type_;

  ReconfigureAsDataPropertyOperator(int descriptor,
                                    Representation representation,
                                    Handle<FieldType> heap_type,
                                    PropertyAttributes attributes = NONE)
      : descriptor_(descriptor),
        representation_(representation),
        attributes_(attributes),
        heap_type_(heap_type) {}

  Handle<Map> DoTransition(Isolate* isolate, Expectations* expectations,
                           Handle<Map> map) {
    expectations->SetDataField(descriptor_.as_int(),
                               PropertyConstness::kMutable, representation_,
                               heap_type_);
    return MapUpdater::ReconfigureExistingProperty(
        isolate, map, descriptor_, PropertyKind::kData, attributes_,
        PropertyConstness::kConst);
  }
};


struct ReconfigureAsAccessorPropertyOperator {
  InternalIndex descriptor_;
  PropertyAttributes attributes_;

  ReconfigureAsAccessorPropertyOperator(int descriptor,
                                        PropertyAttributes attributes = NONE)
      : descriptor_(descriptor), attributes_(attributes) {}

  Handle<Map> DoTransition(Isolate* isolate, Expectations* expectations,
                           Handle<Map> map) {
    expectations->SetAccessorField(descriptor_.as_int());
    return MapUpdater::ReconfigureExistingProperty(
        isolate, map, descriptor_, PropertyKind::kAccessor, attributes_,
        PropertyConstness::kConst);
  }
};

// Checks that field generalization happened.
struct FieldGeneralizationChecker {
  int descriptor_;
  PropertyConstness constness_;
  Representation representation_;
  PropertyAttributes attributes_;
  Handle<FieldType> heap_type_;

  FieldGeneralizationChecker(int descriptor, PropertyConstness constness,
                             Representation representation,
                             Handle<FieldType> heap_type,
                             PropertyAttributes attributes = NONE)
      : descriptor_(descriptor),
        constness_(constness),
        representation_(representation),
        attributes_(attributes),
        heap_type_(heap_type) {}

  void Check(Isolate* isolate, Expectations* expectations, Handle<Map> map1,
             Handle<Map> map2) {
    CHECK(!map2->is_deprecated());

    CHECK(map1->is_deprecated());
    CHECK_NE(*map1, *map2);
    Handle<Map> updated_map = Map::Update(isolate, map1);
    CHECK_EQ(*map2, *updated_map);
    CheckMigrationTarget(isolate, *map1, *updated_map);

    expectations->SetDataField(descriptor_, attributes_, constness_,
                               representation_, heap_type_);
    CHECK(expectations->Check(*map2));
  }
};


// Checks that existing transition was taken as is.
struct SameMapChecker {
  void Check(Isolate* isolate, Expectations* expectations, Handle<Map> map1,
             Handle<Map> map2) {
    CHECK(!map2->is_deprecated());
    CHECK_EQ(*map1, *map2);
    CHECK(expectations->Check(*map2));
  }
};


// Checks that both |map1| and |map2| should stays non-deprecated, this is
// the case when property kind is change.
struct PropertyKindReconfigurationChecker {
  void Check(Expectations* expectations, Handle<Map> map1, Handle<Map> map2) {
    CHECK(!map1->is_deprecated());
    CHECK(!map2->is_deprecated());
    CHECK_NE(*map1, *map2);
    CHECK(expectations->Check(*map2));
  }
};


// This test transitions to various property types under different
// circumstances.
// Plan:
// 1) create a |map| with p0..p3 properties.
// 2) create |map1| by adding "p4" to |map0|.
// 3) create |map2| by transition to "p4" from |map0|.
//
//                       + - p4B: |map2|
//                       |
//  {} - p0 - p1 - pA - p3: |map|
//                       |
//                       + - p4A: |map1|
//
// where "p4A" and "p4B" differ only in the attributes.
//
template <typename TransitionOp1, typename TransitionOp2, typename Checker>
static void TestTransitionTo(TransitionOp1* transition_op1,
                             TransitionOp2* transition_op2, Checker* checker) {
  Isolate* isolate = CcTest::i_isolate();
  Handle<FieldType> any_type = FieldType::Any(isolate);

  Expectations expectations(isolate);

  // Create a map, add required properties to it and initialize expectations.
  Handle<Map> initial_map = Map::Create(isolate, 0);
  Handle<Map> map = initial_map;
  for (int i = 0; i < kPropCount - 1; i++) {
    map = expectations.AddDataField(map, NONE, PropertyConstness::kMutable,
                                    Representation::Smi(), any_type);
  }
  CHECK(expectations.Check(*map));

  Expectations expectations1 = expectations;
  Handle<Map> map1 = transition_op1->DoTransition(&expectations1, map);
  CHECK(expectations1.Check(*map1));

  Expectations expectations2 = expectations;
  Handle<Map> map2 = transition_op2->DoTransition(&expectations2, map);

  // Let the test customization do the check.
  checker->Check(isolate, &expectations2, map1, map2);
}

TEST(TransitionDataFieldToDataField) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  Handle<Object> value1 = handle(Smi::zero(), isolate);
  TransitionToDataFieldOperator transition_op1(
      PropertyConstness::kMutable, Representation::Smi(), any_type, value1);

  Handle<Object> value2 = isolate->factory()->NewHeapNumber(0);
  TransitionToDataFieldOperator transition_op2(
      PropertyConstness::kMutable, Representation::Double(), any_type, value2);

  FieldGeneralizationChecker checker(kPropCount - 1,
                                     PropertyConstness::kMutable,
                                     Representation::Double(), any_type);
  TestTransitionTo(&transition_op1, &transition_op2, &checker);
}

TEST(TransitionDataConstantToSameDataConstant) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  Handle<JSFunction> js_func =
      factory->NewFunctionForTesting(factory->empty_string());
  TransitionToDataConstantOperator transition_op(js_func);

  SameMapChecker checker;
  TestTransitionTo(&transition_op, &transition_op, &checker);
}


TEST(TransitionDataConstantToAnotherDataConstant) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  Handle<String> name = factory->empty_string();
  Handle<Map> sloppy_map =
      Map::CopyInitialMap(isolate, isolate->sloppy_function_map());
  Handle<SharedFunctionInfo> info =
      factory->NewSharedFunctionInfoForBuiltin(name, Builtin::kIllegal);
  CHECK(sloppy_map->is_stable());

  Handle<JSFunction> js_func1 =
      Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
          .set_map(sloppy_map)
          .Build();
  TransitionToDataConstantOperator transition_op1(js_func1);

  Handle<JSFunction> js_func2 =
      Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
          .set_map(sloppy_map)
          .Build();
  TransitionToDataConstantOperator transition_op2(js_func2);

  SameMapChecker checker;
  TestTransitionTo(&transition_op1, &transition_op2, &checker);
}


TEST(TransitionDataConstantToDataField) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  Handle<FieldType> any_type = FieldType::Any(isolate);

  Handle<JSFunction> js_func1 =
      factory->NewFunctionForTesting(factory->empty_string());
  TransitionToDataConstantOperator transition_op1(js_func1);

  Handle<Object> value2 = isolate->factory()->NewHeapNumber(0);
  TransitionToDataFieldOperator transition_op2(
      PropertyConstness::kMutable, Representation::Tagged(), any_type, value2);

  SameMapChecker checker;
  TestTransitionTo(&transition_op1, &transition_op2, &checker);
}


TEST(TransitionAccessorConstantToSameAccessorConstant) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  Handle<AccessorPair> pair = CreateAccessorPair(true, true);
  TransitionToAccessorConstantOperator transition_op(pair);

  SameMapChecker checker;
  TestTransitionTo(&transition_op, &transition_op, &checker);
}

// TODO(ishell): add this test once IS_ACCESSOR_FIELD_SUPPORTED is supported.
// TEST(TransitionAccessorConstantToAnotherAccessorConstant)

TEST(HoleyHeapNumber) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  auto mhn = isolate->factory()->NewHeapNumberWithHoleNaN();
  CHECK_EQ(kHoleNanInt64, mhn->value_as_bits());

  mhn = isolate->factory()->NewHeapNumber(0.0);
  CHECK_EQ(uint64_t{0}, mhn->value_as_bits());

  mhn->set_value_as_bits(kHoleNanInt64);
  CHECK_EQ(kHoleNanInt64, mhn->value_as_bits());

  // Ensure that new storage for uninitialized value or mutable heap number
  // with uninitialized sentinel (kHoleNanInt64) is a mutable heap number
  // with uninitialized sentinel.
  Handle<Object> obj =
      Object::NewStorageFor(isolate, isolate->factory()->uninitialized_value(),
                            Representation::Double());
  CHECK(IsHeapNumber(*obj));
  CHECK_EQ(kHoleNanInt64, HeapNumber::cast(*obj)->value_as_bits());

  obj = Object::NewStorageFor(isolate, mhn, Representation::Double());
  CHECK(IsHeapNumber(*obj));
  CHECK_EQ(kHoleNanInt64, HeapNumber::cast(*obj)->value_as_bits());
}

namespace {

template <class... Args>
MaybeHandle<Object> Call(Isolate* isolate, Handle<JSFunction> function,
                         Args... args) {
  Handle<Object> argv[] = {args...};
  return Execution::Call(isolate, function,
                         isolate->factory()->undefined_value(), sizeof...(args),
                         argv);
}

void TestStoreToConstantField(const char* store_func_source,
                              Handle<Object> value1, Handle<Object> value2,
                              Representation expected_rep,
                              PropertyConstness expected_constness,
                              int store_repetitions) {
  Isolate* isolate = CcTest::i_isolate();
  CompileRun(store_func_source);

  Handle<JSFunction> store_func = GetGlobal<JSFunction>("store");

  Handle<Map> initial_map = Map::Create(isolate, 4);

  // Store value1 to obj1 and check that it got property with expected
  // representation and constness.
  Handle<JSObject> obj1 = isolate->factory()->NewJSObjectFromMap(initial_map);
  for (int i = 0; i < store_repetitions; i++) {
    Call(isolate, store_func, obj1, value1).Check();
  }

  Handle<Map> map(obj1->map(), isolate);
  CHECK(!map->is_dictionary_map());
  CHECK(!map->is_deprecated());
  CHECK_EQ(1, map->NumberOfOwnDescriptors());
  InternalIndex first(0);
  CHECK(map->instance_descriptors(isolate)
            ->GetDetails(first)
            .representation()
            .Equals(expected_rep));
  CHECK_EQ(PropertyConstness::kConst,
           map->instance_descriptors(isolate)->GetDetails(first).constness());

  // Store value2 to obj2 and check that it got same map and property details
  // did not change.
  Handle<JSObject> obj2 = isolate->factory()->NewJSObjectFromMap(initial_map);
  Call(isolate, store_func, obj2, value2).Check();

  CHECK_EQ(*map, obj2->map());
  CHECK(!map->is_dictionary_map());
  CHECK(!map->is_deprecated());
  CHECK_EQ(1, map->NumberOfOwnDescriptors());

  CHECK(map->instance_descriptors(isolate)
            ->GetDetails(first)
            .representation()
            .Equals(expected_rep));
  CHECK_EQ(PropertyConstness::kConst,
           map->instance_descriptors(isolate)->GetDetails(first).constness());

  // Store value2 to obj1 and check that property became mutable.
  Call(isolate, store_func, obj1, value2).Check();

  CHECK_EQ(*map, obj1->map());
  CHECK(!map->is_dictionary_map());
  CHECK(!map->is_deprecated());
  CHECK_EQ(1, map->NumberOfOwnDescriptors());

  CHECK(map->instance_descriptors(isolate)
            ->GetDetails(first)
            .representation()
            .Equals(expected_rep));
  CHECK_EQ(expected_constness,
           map->instance_descriptors(isolate)->GetDetails(first).constness());
}

void TestStoreToConstantField_PlusMinusZero(const char* store_func_source,
                                            int store_repetitions) {
  Isolate* isolate = CcTest::i_isolate();
  CompileRun(store_func_source);

  Handle<Object> minus_zero = isolate->factory()->NewNumber(-0.0);
  Handle<Object> plus_zero = isolate->factory()->NewNumber(0.0);

  // +0 and -0 are treated as not equal upon stores.
  const PropertyConstness kExpectedFieldConstness = PropertyConstness::kMutable;

  TestStoreToConstantField(store_func_source, minus_zero, plus_zero,
                           Representation::Double(), kExpectedFieldConstness,
                           store_repetitions);
}

void TestStoreToConstantField_NaN(const char* store_func_source,
                                  int store_repetitions) {
  Isolate* isolate = CcTest::i_isolate();
  CompileRun(store_func_source);

  uint64_t nan_bits = uint64_t{0x7FF8000000000001};
  double nan_double1 = base::bit_cast<double>(nan_bits);
  double nan_double2 = base::bit_cast<double>(nan_bits | 0x12300);
  CHECK(std::isnan(nan_double1));
  CHECK(std::isnan(nan_double2));
  CHECK_NE(nan_double1, nan_double2);
  CHECK_NE(base::bit_cast<uint64_t>(nan_double1),
           base::bit_cast<uint64_t>(nan_double2));

  Handle<Object> nan1 = isolate->factory()->NewNumber(nan_double1);
  Handle<Object> nan2 = isolate->factory()->NewNumber(nan_double2);

  // NaNs with different bit patters are not treated as equal upon stores.
  TestStoreToConstantField(store_func_source, nan1, nan2,
                           Representation::Double(),
                           PropertyConstness::kMutable, store_repetitions);
}

}  // namespace

TEST(StoreToConstantField_PlusMinusZero) {
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  const char* store_func_source =
      "function store(o, v) {"
      "  %SetNamedProperty(o, 'v', v);"
      "}";

  TestStoreToConstantField_PlusMinusZero(store_func_source, 1);

  TestStoreToConstantField_NaN(store_func_source, 1);
}

TEST(StoreToConstantField_ObjectDefineProperty) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  const char* store_func_source =
      "function store(o, v) {"
      "  Object.defineProperty(o, 'v', "
      "                        {value: v, "
      "                         writable: true, "
      "                         configurable: true, "
      "                         enumerable: true});"
      "}";

  TestStoreToConstantField_PlusMinusZero(store_func_source, 1);

  TestStoreToConstantField_NaN(store_func_source, 1);
}

TEST(StoreToConstantField_ReflectSet) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  const char* store_func_source =
      "function store(o, v) {"
      "  Reflect.set(o, 'v', v);"
      "}";

  TestStoreToConstantField_PlusMinusZero(store_func_source, 1);

  TestStoreToConstantField_NaN(store_func_source, 1);
}

TEST(StoreToConstantField_StoreIC) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  const char* store_func_source =
      "function store(o, v) {"
      "  o.v = v;"
      "}";

  TestStoreToConstantField_PlusMinusZero(store_func_source, 1);

  TestStoreToConstantField_NaN(store_func_source, 1);
}

TEST(NormalizeToMigrationTarget) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();

  CHECK(
      IsNormalizedMapCache(isolate->native_context()->normalized_map_cache()));

  Handle<Map> base_map = Map::Create(isolate, 4);

  Handle<Map> existing_normalized_map = Map::Normalize(
      isolate, base_map, PropertyNormalizationMode::CLEAR_INOBJECT_PROPERTIES,
      "Test_NormalizeToMigrationTarget_ExistingMap");
  existing_normalized_map->set_is_migration_target(true);

  // Normalizing a second map should hit the normalized map cache, including it
  // being OK for the new map to be a migration target.
  CHECK(!base_map->is_migration_target());
  Handle<Map> new_normalized_map = Map::Normalize(
      isolate, base_map, PropertyNormalizationMode::CLEAR_INOBJECT_PROPERTIES,
      "Test_NormalizeToMigrationTarget_NewMap");
  CHECK_EQ(*existing_normalized_map, *new_normalized_map);
  CHECK(new_normalized_map->is_migration_target());
}

TEST(RepresentationPredicatesAreInSync) {
  static_assert(Representation::kNumRepresentations == 6);
  static Representation reps[] = {
      Representation::None(),   Representation::Smi(),
      Representation::Double(), Representation::HeapObject(),
      Representation::Tagged(), Representation::WasmValue()};

  for (Representation from : reps) {
    Representation most_generic_rep = from.MostGenericInPlaceChange();
    CHECK(from.CanBeInPlaceChangedTo(most_generic_rep));

    bool might_be_deprecated = false;

    for (Representation to : reps) {
      // Skip representation narrowing cases.
      if (!from.fits_into(to)) continue;

      if (!from.CanBeInPlaceChangedTo(to)) {
        might_be_deprecated = true;
      }
    }
    CHECK_EQ(from.MightCauseMapDeprecation(), might_be_deprecated);
  }
}

#define CHECK_SAME(object, rep, expected)                    \
  CHECK_EQ(Object::FitsRepresentation(*object, rep, true),   \
           Object::FitsRepresentation(*object, rep, false)); \
  CHECK_EQ(Object::FitsRepresentation(*object, rep, true), expected)

TEST(CheckFitsRepresentationPredicate) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  i::Factory* factory = CcTest::i_isolate()->factory();

  Handle<Smi> smi_value = factory->last_script_id();
  Handle<HeapNumber> double_value = factory->nan_value();
  Handle<OrderedHashMap> heapobject_value = factory->empty_ordered_hash_map();

  Representation rep_smi = Representation::Smi();
  Representation rep_double = Representation::Double();
  Representation rep_heapobject = Representation::HeapObject();
  Representation rep_tagged = Representation::Tagged();

  // Verify the behavior of Object::FitsRepresentation() with and
  // without coercion. A Smi can be "coerced" into a Double
  // representation by converting it to a HeapNumber. If coercion is
  // disallowed, that query should fail.
  CHECK_SAME(smi_value, rep_smi, true);
  CHECK_EQ(Object::FitsRepresentation(*smi_value, rep_double, true), true);
  CHECK_EQ(Object::FitsRepresentation(*smi_value, rep_double, false), false);
  CHECK_SAME(smi_value, rep_heapobject, false);
  CHECK_SAME(smi_value, rep_tagged, true);

  CHECK_SAME(double_value, rep_smi, false);
  CHECK_SAME(double_value, rep_double, true);
  CHECK_SAME(double_value, rep_heapobject, true);
  CHECK_SAME(double_value, rep_tagged, true);

  CHECK_SAME(heapobject_value, rep_smi, false);
  CHECK_SAME(heapobject_value, rep_double, false);
  CHECK_SAME(heapobject_value, rep_heapobject, true);
  CHECK_SAME(heapobject_value, rep_tagged, true);
}

#undef CHECK_SAME

}  // namespace test_field_type_tracking
}  // namespace compiler
}  // namespace internal
}  // namespace v8
