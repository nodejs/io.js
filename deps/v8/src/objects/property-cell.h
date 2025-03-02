// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROPERTY_CELL_H_
#define V8_OBJECTS_PROPERTY_CELL_H_

#include "src/objects/dependent-code.h"
#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class FixedArray;
class WeakFixedArray;

#include "torque-generated/src/objects/property-cell-tq.inc"

class PropertyCell
    : public TorqueGeneratedPropertyCell<PropertyCell, HeapObject> {
 public:
  // [name]: the name of the global property.
  DECL_GETTER(name, Tagged<Name>)

  // [property_details]: details of the global property.
  DECL_GETTER(property_details_raw, Tagged<Smi>)
  DECL_ACQUIRE_GETTER(property_details_raw, Tagged<Smi>)
  inline PropertyDetails property_details() const;
  inline PropertyDetails property_details(AcquireLoadTag tag) const;
  inline void UpdatePropertyDetailsExceptCellType(PropertyDetails details);

  // [value]: value of the global property.
  DECL_GETTER(value, Tagged<Object>)
  DECL_ACQUIRE_GETTER(value, Tagged<Object>)

  // [dependent_code]: code that depends on the type of the global property.
  DECL_ACCESSORS(dependent_code, Tagged<DependentCode>)

  // Changes the value and/or property details.
  // For global properties:
  inline void Transition(PropertyDetails new_details,
                         DirectHandle<Object> new_value);
  // For protectors:
  void InvalidateProtector();

  static PropertyCellType InitialType(Isolate* isolate, Tagged<Object> value);

  // Computes the new type of the cell's contents for the given value, but
  // without actually modifying the details.
  static PropertyCellType UpdatedType(Isolate* isolate,
                                      Tagged<PropertyCell> cell,
                                      Tagged<Object> value,
                                      PropertyDetails details);

  // Prepares property cell at given entry for receiving given value and sets
  // that value.  As a result the old cell could be invalidated and/or dependent
  // code could be deoptimized. Returns the (possibly new) property cell.
  static Handle<PropertyCell> PrepareForAndSetValue(
      Isolate* isolate, DirectHandle<GlobalDictionary> dictionary,
      InternalIndex entry, DirectHandle<Object> value, PropertyDetails details);

  void ClearAndInvalidate(Isolate* isolate);
  static Handle<PropertyCell> InvalidateAndReplaceEntry(
      Isolate* isolate, DirectHandle<GlobalDictionary> dictionary,
      InternalIndex entry, PropertyDetails new_details,
      DirectHandle<Object> new_value);

  // Whether or not the {details} and {value} fit together. This is an
  // approximation with false positives.
  static bool CheckDataIsCompatible(PropertyDetails details,
                                    Tagged<Object> value);

  DECL_PRINTER(PropertyCell)
  DECL_VERIFIER(PropertyCell)

  using BodyDescriptor = FixedBodyDescriptor<kNameOffset, kSize, kSize>;

  TQ_OBJECT_CONSTRUCTORS(PropertyCell)

 private:
  friend class Factory;

  DECL_SETTER(name, Tagged<Name>)
  DECL_SETTER(value, Tagged<Object>)
  DECL_RELEASE_SETTER(value, Tagged<Object>)
  DECL_SETTER(property_details_raw, Tagged<Smi>)
  DECL_RELEASE_SETTER(property_details_raw, Tagged<Smi>)

#ifdef DEBUG
  // Whether the property cell can transition to the given state. This is an
  // approximation with false positives.
  bool CanTransitionTo(PropertyDetails new_details,
                       Tagged<Object> new_value) const;
#endif  // DEBUG
};

class ContextSidePropertyCell
    : public TorqueGeneratedContextSidePropertyCell<ContextSidePropertyCell,
                                                    HeapObject> {
 public:
  // Keep in sync with property-cell.tq.
  // This enum tracks a property of a ScriptContext slot.
  // The property determines how the slot's value can be accessed and modified.
  enum Property {
    kOther = 0,  // The slot holds an arbitrary tagged value. kOther is a sink
                 // state and cannot transition to any other state.
    kConst = 1,  // The slot holds a constant value. kConst can transition to
                 // any other state.
    kSmi = 2,    // The slot holds a Smi. kSmi can transition to kMutableInt32,
                 // kMutableHeapNumber, or kOther.
    kMutableInt32 =
        3,  // // The slot holds a HeapNumber that can be mutated in-place by
            // optimized code. This HeapNumber should never leak from the slot.
            // It contains a Int32 value as double. kMutableInt32 can transition
            // to kMutableHeapNumber or kOther.
    kMutableHeapNumber =
        4,  // The slot holds a HeapNumber that can be mutated in-place by
            // optimized code. This HeapNumber should never leak from the slot.
            // kMutableHeapNumber can only transition to kOther.
  };

  static Tagged<Smi> Const() { return Smi::FromInt(Property::kConst); }
  static Tagged<Smi> SmiMarker() { return Smi::FromInt(Property::kSmi); }
  static Tagged<Smi> MutableInt32() {
    return Smi::FromInt(Property::kMutableInt32);
  }
  static Tagged<Smi> MutableHeapNumber() {
    return Smi::FromInt(Property::kMutableHeapNumber);
  }
  static Tagged<Smi> Other() { return Smi::FromInt(Property::kOther); }

  static Property FromSmi(Tagged<Smi> smi) {
    int value = smi.value();
    DCHECK_GE(value, 0);
    DCHECK_LE(value, kMutableHeapNumber);
    return static_cast<Property>(value);
  }

  inline Property context_side_property() const;

  // [context_side_property_raw]: details of the context slot property.
  DECL_RELEASE_ACQUIRE_ACCESSORS(context_side_property_raw, Tagged<Smi>)

  // [dependent_code]: code that depends on the constness of the value.
  DECL_ACCESSORS(dependent_code, Tagged<DependentCode>)

  DECL_PRINTER(ContextSidePropertyCell)
  DECL_VERIFIER(ContextSidePropertyCell)

  using BodyDescriptor =
      FixedBodyDescriptor<kDependentCodeOffset, kSize, kSize>;

  TQ_OBJECT_CONSTRUCTORS(ContextSidePropertyCell)

 private:
  friend class Factory;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_PROPERTY_CELL_H_
