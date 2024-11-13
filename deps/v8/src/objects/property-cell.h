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

  void ClearAndInvalidate(ReadOnlyRoots roots);
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

class ConstTrackingLetCell
    : public TorqueGeneratedConstTrackingLetCell<ConstTrackingLetCell,
                                                 HeapObject> {
 public:
  static constexpr Tagged<Smi> kConstMarker = Smi::FromInt(1);
  static constexpr Tagged<Smi> kNonConstMarker = Smi::FromInt(0);

  static inline bool IsNotConst(Tagged<Object> object);

  // [dependent_code]: code that depends on the constness of the value.
  DECL_ACCESSORS(dependent_code, Tagged<DependentCode>)

  DECL_PRINTER(ConstTrackingLetCell)
  DECL_VERIFIER(ConstTrackingLetCell)

  using BodyDescriptor =
      FixedBodyDescriptor<kDependentCodeOffset, kSize, kSize>;

  TQ_OBJECT_CONSTRUCTORS(ConstTrackingLetCell)

 private:
  friend class Factory;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_PROPERTY_CELL_H_
