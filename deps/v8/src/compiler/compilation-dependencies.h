// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_COMPILATION_DEPENDENCIES_H_
#define V8_COMPILER_COMPILATION_DEPENDENCIES_H_

#include "src/compiler/js-heap-broker.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class SlackTrackingPrediction {
 public:
  SlackTrackingPrediction(MapRef initial_map, int instance_size);

  int inobject_property_count() const { return inobject_property_count_; }
  int instance_size() const { return instance_size_; }

 private:
  int instance_size_;
  int inobject_property_count_;
};

class CompilationDependency;

// Collects and installs dependencies of the code that is being generated.
class V8_EXPORT CompilationDependencies : public ZoneObject {
 public:
  CompilationDependencies(JSHeapBroker* broker, Zone* zone);

  V8_WARN_UNUSED_RESULT bool Commit(Handle<Code> code);

  // Return the initial map of {function} and record the assumption that it
  // stays the initial map.
  MapRef DependOnInitialMap(JSFunctionRef function);

  // Return the "prototype" property of the given function and record the
  // assumption that it doesn't change.
  HeapObjectRef DependOnPrototypeProperty(JSFunctionRef function);

  // Record the assumption that {map} stays stable.
  void DependOnStableMap(MapRef map);

  // Record the assumption that slack tracking for {map} doesn't change during
  // compilation. This gives no guarantees about slack tracking changes after
  // the compilation is finished (ie, it Validates the dependency, but doesn't
  // Install anything).
  void DependOnNoSlackTrackingChange(MapRef map);

  // Depend on the fact that accessing property |property_name| from
  // |receiver_map| yields the constant value |constant|, which is held by
  // |holder|. Therefore, must be invalidated if |property_name| is added to any
  // of the objects between receiver and |holder| on the prototype chain, b) any
  // of the objects on the prototype chain up to |holder| change prototypes, or
  // c) the value of |property_name| in |holder| changes.
  // If PropertyKind is kData, |constant| is the value of the property in
  // question. In case of PropertyKind::kAccessor, |constant| is the accessor
  // function (i.e., getter or setter) itself, not the overall AccessorPair.
  void DependOnConstantInDictionaryPrototypeChain(MapRef receiver_map,
                                                  NameRef property_name,
                                                  ObjectRef constant,
                                                  PropertyKind kind);

  // Return the pretenure mode of {site} and record the assumption that it does
  // not change.
  AllocationType DependOnPretenureMode(AllocationSiteRef site);

  // Return a field's constness and, if kConst, record the assumption that it
  // remains kConst. The field is identified by the arguments.
  //
  // For arrays, arguments objects and value wrappers, only consider the field
  // kConst if the map is stable (and register stability dependency in that
  // case).  This is to ensure that fast elements kind transitions cannot be
  // used to mutate fields without deoptimization of the dependent code.
  PropertyConstness DependOnFieldConstness(MapRef map, MapRef owner,
                                           InternalIndex descriptor);

  // Record the assumption that neither {cell}'s {CellType} changes, nor the
  // {IsReadOnly()} flag of {cell}'s {PropertyDetails}.
  void DependOnGlobalProperty(PropertyCellRef cell);

  // Record the assumption that a const-tracked let variable doesn't change, if
  // true.
  bool DependOnConstTrackingLet(ContextRef script_context, size_t index,
                                JSHeapBroker* broker);

  // Return the validity of the given protector and, if true, record the
  // assumption that the protector remains valid.
  bool DependOnProtector(PropertyCellRef cell);

  // Convenience wrappers around {DependOnProtector}.
  bool DependOnArrayBufferDetachingProtector();
  bool DependOnArrayIteratorProtector();
  bool DependOnArraySpeciesProtector();
  bool DependOnNoElementsProtector();
  bool DependOnPromiseHookProtector();
  bool DependOnPromiseSpeciesProtector();
  bool DependOnPromiseThenProtector();
  bool DependOnMegaDOMProtector();
  bool DependOnNoProfilingProtector();
  bool DependOnNoUndetectableObjectsProtector();

  // Record the assumption that {site}'s {ElementsKind} doesn't change.
  void DependOnElementsKind(AllocationSiteRef site);

  // Check that an object slot will not change during compilation.
  void DependOnObjectSlotValue(HeapObjectRef object, int offset,
                               ObjectRef value);

  void DependOnOwnConstantElement(JSObjectRef holder, uint32_t index,
                                  ObjectRef element);

  // Record the assumption that the {value} read from {holder} at {index} on the
  // background thread is the correct value for a given property.
  void DependOnOwnConstantDataProperty(JSObjectRef holder, MapRef map,
                                       FieldIndex index, ObjectRef value);
  void DependOnOwnConstantDoubleProperty(JSObjectRef holder, MapRef map,
                                         FieldIndex index, Float64 value);

  // Record the assumption that the {value} read from {holder} at {index} on the
  // background thread is the correct value for a given dictionary property.
  void DependOnOwnConstantDictionaryProperty(JSObjectRef holder,
                                             InternalIndex index,
                                             ObjectRef value);

  // For each given map, depend on the stability of (the maps of) all prototypes
  // up to (and including) the {last_prototype}.
  void DependOnStablePrototypeChains(
      ZoneVector<MapRef> const& receiver_maps, WhereToStart start,
      OptionalJSObjectRef last_prototype = OptionalJSObjectRef());

  // For the given map, depend on the stability of (the maps of) all prototypes
  // up to (and including) the {last_prototype}.
  void DependOnStablePrototypeChain(
      MapRef receiver_maps, WhereToStart start,
      OptionalJSObjectRef last_prototype = OptionalJSObjectRef());

  // Like DependOnElementsKind but also applies to all nested allocation sites.
  void DependOnElementsKinds(AllocationSiteRef site);

  void DependOnConsistentJSFunctionView(JSFunctionRef function);

  // Predict the final instance size for {function}'s initial map and record
  // the assumption that this prediction is correct. In addition, register
  // the initial map dependency. This method returns the {function}'s the
  // predicted minimum slack instance size count (wrapped together with
  // the corresponding in-object property count for convenience).
  SlackTrackingPrediction DependOnInitialMapInstanceSizePrediction(
      JSFunctionRef function);

  // Records {dependency} if not null.
  void RecordDependency(CompilationDependency const* dependency);

  // The methods below allow for gathering dependencies without actually
  // recording them. They can be recorded at a later time via RecordDependency
  // (or they can be ignored).

  // Gather the assumption that {target_map} can be transitioned to, i.e., that
  // it does not become deprecated.
  CompilationDependency const* TransitionDependencyOffTheRecord(
      MapRef target_map) const;

  // Gather the assumption that the field representation of a field does not
  // change. The field is identified by the arguments.
  CompilationDependency const* FieldRepresentationDependencyOffTheRecord(
      MapRef map, MapRef owner, InternalIndex descriptor,
      Representation representation) const;

  // Gather the assumption that the field type of a field does not change. The
  // field is identified by the arguments.
  CompilationDependency const* FieldTypeDependencyOffTheRecord(
      MapRef map, MapRef owner, InternalIndex descriptor,
      ObjectRef /* Contains a FieldType underneath. */ type) const;

#ifdef DEBUG
  static bool IsFieldRepresentationDependencyOnMap(
      const CompilationDependency* dep, const Handle<Map>& receiver_map);
#endif  // DEBUG

  struct CompilationDependencyHash {
    size_t operator()(const CompilationDependency* dep) const;
  };
  struct CompilationDependencyEqual {
    bool operator()(const CompilationDependency* lhs,
                    const CompilationDependency* rhs) const;
  };

 private:
  bool PrepareInstall();
  bool PrepareInstallPredictable();

  using CompilationDependencySet =
      ZoneUnorderedSet<const CompilationDependency*, CompilationDependencyHash,
                       CompilationDependencyEqual>;

  Zone* const zone_;
  JSHeapBroker* const broker_;
  CompilationDependencySet dependencies_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_COMPILATION_DEPENDENCIES_H_
