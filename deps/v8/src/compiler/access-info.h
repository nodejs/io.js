// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ACCESS_INFO_H_
#define V8_COMPILER_ACCESS_INFO_H_

#include <optional>

#include "src/compiler/heap-refs.h"
#include "src/compiler/types.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Factory;

namespace compiler {

// Forward declarations.
class CompilationDependencies;
class CompilationDependency;
class ElementAccessFeedback;
class JSHeapBroker;
class TypeCache;
struct ConstFieldInfo;

std::ostream& operator<<(std::ostream&, AccessMode);

// This class encapsulates all information required to access a certain element.
class ElementAccessInfo final {
 public:
  ElementAccessInfo(ZoneVector<MapRef>&& lookup_start_object_maps,
                    ElementsKind elements_kind, Zone* zone);

  ElementsKind elements_kind() const { return elements_kind_; }
  ZoneVector<MapRef> const& lookup_start_object_maps() const {
    return lookup_start_object_maps_;
  }
  ZoneVector<MapRef> const& transition_sources() const {
    return transition_sources_;
  }

  void AddTransitionSource(MapRef map) {
    CHECK_EQ(lookup_start_object_maps_.size(), 1);
    transition_sources_.push_back(map);
  }

 private:
  ElementsKind elements_kind_;
  ZoneVector<MapRef> lookup_start_object_maps_;
  ZoneVector<MapRef> transition_sources_;
};

// This class encapsulates all information required to access a certain
// object property, either on the object itself or on the prototype chain.
class PropertyAccessInfo final {
 public:
  enum Kind {
    kInvalid,
    kNotFound,
    kDataField,
    kFastDataConstant,
    kDictionaryProtoDataConstant,
    kFastAccessorConstant,
    kDictionaryProtoAccessorConstant,
    kModuleExport,
    kStringLength
  };

  static PropertyAccessInfo NotFound(Zone* zone, MapRef receiver_map,
                                     OptionalJSObjectRef holder);
  static PropertyAccessInfo DataField(
      JSHeapBroker* broker, Zone* zone, MapRef receiver_map,
      ZoneVector<CompilationDependency const*>&& unrecorded_dependencies,
      FieldIndex field_index, Representation field_representation,
      Type field_type, MapRef field_owner_map, OptionalMapRef field_map,
      OptionalJSObjectRef holder, OptionalMapRef transition_map);
  static PropertyAccessInfo FastDataConstant(
      Zone* zone, MapRef receiver_map,
      ZoneVector<CompilationDependency const*>&& unrecorded_dependencies,
      FieldIndex field_index, Representation field_representation,
      Type field_type, MapRef field_owner_map, OptionalMapRef field_map,
      OptionalJSObjectRef holder, OptionalMapRef transition_map);
  static PropertyAccessInfo FastAccessorConstant(
      Zone* zone, MapRef receiver_map, OptionalJSObjectRef holder,
      OptionalObjectRef constant, OptionalJSObjectRef api_holder);
  static PropertyAccessInfo ModuleExport(Zone* zone, MapRef receiver_map,
                                         CellRef cell);
  static PropertyAccessInfo StringLength(Zone* zone, MapRef receiver_map);
  static PropertyAccessInfo Invalid(Zone* zone);
  static PropertyAccessInfo DictionaryProtoDataConstant(
      Zone* zone, MapRef receiver_map, JSObjectRef holder,
      InternalIndex dict_index, NameRef name);
  static PropertyAccessInfo DictionaryProtoAccessorConstant(
      Zone* zone, MapRef receiver_map, OptionalJSObjectRef holder,
      ObjectRef constant, OptionalJSObjectRef api_holder, NameRef name);

  bool Merge(PropertyAccessInfo const* that, AccessMode access_mode,
             Zone* zone) V8_WARN_UNUSED_RESULT;

  void RecordDependencies(CompilationDependencies* dependencies);

  bool IsInvalid() const { return kind() == kInvalid; }
  bool IsNotFound() const { return kind() == kNotFound; }
  bool IsDataField() const { return kind() == kDataField; }
  bool IsFastDataConstant() const { return kind() == kFastDataConstant; }
  bool IsFastAccessorConstant() const {
    return kind() == kFastAccessorConstant;
  }
  bool IsModuleExport() const { return kind() == kModuleExport; }
  bool IsStringLength() const { return kind() == kStringLength; }
  bool IsDictionaryProtoDataConstant() const {
    return kind() == kDictionaryProtoDataConstant;
  }
  bool IsDictionaryProtoAccessorConstant() const {
    return kind() == kDictionaryProtoAccessorConstant;
  }

  bool HasTransitionMap() const { return transition_map().has_value(); }
  bool HasDictionaryHolder() const {
    return kind_ == kDictionaryProtoDataConstant ||
           kind_ == kDictionaryProtoAccessorConstant;
  }
  ConstFieldInfo GetConstFieldInfo() const;

  Kind kind() const { return kind_; }

  // The object where the property definition was found.
  OptionalJSObjectRef holder() const {
    // TODO(neis): There was a CHECK here that tries to protect against
    // using the access info without recording its dependencies first.
    // Find a more suitable place for it.
    return holder_;
  }
  // For accessor properties when the callback is an API function with a
  // signature, this is the value that will be passed to the callback as
  // FunctionCallbackInfo::Holder().
  // Don't mix it up with holder in a "object where the property was found"
  // sense.
  OptionalJSObjectRef api_holder() const { return api_holder_; }
  OptionalMapRef transition_map() const {
    DCHECK(!HasDictionaryHolder());
    return transition_map_;
  }
  OptionalObjectRef constant() const {
    DCHECK_IMPLIES(constant_.has_value(),
                   IsModuleExport() || IsFastAccessorConstant() ||
                       IsDictionaryProtoAccessorConstant());
    return constant_;
  }
  FieldIndex field_index() const {
    DCHECK(!HasDictionaryHolder());
    return field_index_;
  }

  Type field_type() const {
    DCHECK(!HasDictionaryHolder());
    return field_type_;
  }
  Representation field_representation() const {
    DCHECK(!HasDictionaryHolder());
    return field_representation_;
  }
  OptionalMapRef field_map() const {
    DCHECK(!HasDictionaryHolder());
    return field_map_;
  }
  ZoneVector<MapRef> const& lookup_start_object_maps() const {
    return lookup_start_object_maps_;
  }

  InternalIndex dictionary_index() const {
    DCHECK(HasDictionaryHolder());
    return dictionary_index_;
  }

  NameRef name() const {
    DCHECK(HasDictionaryHolder());
    return name_.value();
  }

 private:
  explicit PropertyAccessInfo(Zone* zone);
  PropertyAccessInfo(Zone* zone, Kind kind, OptionalJSObjectRef holder,
                     ZoneVector<MapRef>&& lookup_start_object_maps);
  PropertyAccessInfo(Zone* zone, Kind kind, OptionalJSObjectRef holder,
                     OptionalObjectRef constant, OptionalJSObjectRef api_holder,
                     OptionalNameRef name,
                     ZoneVector<MapRef>&& lookup_start_object_maps);
  PropertyAccessInfo(Kind kind, OptionalJSObjectRef holder,
                     OptionalMapRef transition_map, FieldIndex field_index,
                     Representation field_representation, Type field_type,
                     MapRef field_owner_map, OptionalMapRef field_map,
                     ZoneVector<MapRef>&& lookup_start_object_maps,
                     ZoneVector<CompilationDependency const*>&& dependencies);
  PropertyAccessInfo(Zone* zone, Kind kind, OptionalJSObjectRef holder,
                     ZoneVector<MapRef>&& lookup_start_object_maps,
                     InternalIndex dictionary_index, NameRef name);

  // Members used for fast and dictionary mode holders:
  Kind kind_;
  ZoneVector<MapRef> lookup_start_object_maps_;
  OptionalObjectRef constant_;
  OptionalJSObjectRef holder_;
  OptionalJSObjectRef api_holder_;

  // Members only used for fast mode holders:
  ZoneVector<CompilationDependency const*> unrecorded_dependencies_;
  OptionalMapRef transition_map_;
  FieldIndex field_index_;
  Representation field_representation_;
  Type field_type_;
  OptionalMapRef field_owner_map_;
  OptionalMapRef field_map_;

  // Members only used for dictionary mode holders:
  InternalIndex dictionary_index_;
  OptionalNameRef name_;
};

// Factory class for {ElementAccessInfo}s and {PropertyAccessInfo}s.
class AccessInfoFactory final {
 public:
  AccessInfoFactory(JSHeapBroker* broker, Zone* zone);

  std::optional<ElementAccessInfo> ComputeElementAccessInfo(
      MapRef map, AccessMode access_mode) const;
  bool ComputeElementAccessInfos(
      ElementAccessFeedback const& feedback,
      ZoneVector<ElementAccessInfo>* access_infos) const;

  PropertyAccessInfo ComputePropertyAccessInfo(MapRef map, NameRef name,
                                               AccessMode access_mode) const;

  PropertyAccessInfo ComputeDictionaryProtoAccessInfo(
      MapRef receiver_map, NameRef name, JSObjectRef holder,
      InternalIndex dict_index, AccessMode access_mode,
      PropertyDetails details) const;

  // Merge as many of the given {infos} as possible and record any dependencies.
  // Return false iff any of them was invalid, in which case no dependencies are
  // recorded.
  // TODO(neis): Make access_mode part of access info?
  bool FinalizePropertyAccessInfos(
      ZoneVector<PropertyAccessInfo> infos, AccessMode access_mode,
      ZoneVector<PropertyAccessInfo>* result) const;

  // Merge the given {infos} to a single one and record any dependencies. If the
  // merge is not possible, the result has kind {kInvalid} and no dependencies
  // are recorded.
  PropertyAccessInfo FinalizePropertyAccessInfosAsOne(
      ZoneVector<PropertyAccessInfo> infos, AccessMode access_mode) const;

 private:
  std::optional<ElementAccessInfo> ConsolidateElementLoad(
      ElementAccessFeedback const& feedback) const;
  PropertyAccessInfo LookupSpecialFieldAccessor(MapRef map, NameRef name) const;
  PropertyAccessInfo LookupTransition(MapRef map, NameRef name,
                                      OptionalJSObjectRef holder,
                                      PropertyAttributes attrs) const;
  PropertyAccessInfo ComputeDataFieldAccessInfo(MapRef receiver_map, MapRef map,
                                                NameRef name,
                                                OptionalJSObjectRef holder,
                                                InternalIndex descriptor,
                                                AccessMode access_mode) const;
  PropertyAccessInfo ComputeAccessorDescriptorAccessInfo(
      MapRef receiver_map, NameRef name, MapRef map, OptionalJSObjectRef holder,
      InternalIndex descriptor, AccessMode access_mode) const;

  PropertyAccessInfo Invalid() const {
    return PropertyAccessInfo::Invalid(zone());
  }

  void MergePropertyAccessInfos(ZoneVector<PropertyAccessInfo> infos,
                                AccessMode access_mode,
                                ZoneVector<PropertyAccessInfo>* result) const;

  bool TryLoadPropertyDetails(MapRef map, OptionalJSObjectRef maybe_holder,
                              NameRef name, InternalIndex* index_out,
                              PropertyDetails* details_out) const;

  CompilationDependencies* dependencies() const;
  JSHeapBroker* broker() const { return broker_; }
  Isolate* isolate() const;
  Zone* zone() const { return zone_; }

  JSHeapBroker* const broker_;
  TypeCache const* const type_cache_;
  Zone* const zone_;

  AccessInfoFactory(const AccessInfoFactory&) = delete;
  AccessInfoFactory& operator=(const AccessInfoFactory&) = delete;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ACCESS_INFO_H_
