// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/map-updater.h"

#include <optional>
#include <queue>

#include "src/base/platform/mutex.h"
#include "src/execution/frames.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/parked-scope-inl.h"
#include "src/objects/field-type.h"
#include "src/objects/keys.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/property-details.h"
#include "src/objects/transitions.h"

namespace v8::internal {

namespace {

inline bool EqualImmutableValues(Tagged<Object> obj1, Tagged<Object> obj2) {
  if (obj1 == obj2) return true;  // Valid for both kData and kAccessor kinds.
  // TODO(ishell): compare AccessorPairs.
  return false;
}

V8_WARN_UNUSED_RESULT Handle<FieldType> GeneralizeFieldType(
    Representation rep1, Handle<FieldType> type1, Representation rep2,
    Handle<FieldType> type2, Isolate* isolate) {
  if (FieldType::NowIs(*type1, type2)) return type2;
  if (FieldType::NowIs(*type2, type1)) return type1;
  return FieldType::Any(isolate);
}

void PrintGeneralization(
    Isolate* isolate, DirectHandle<Map> map, FILE* file, const char* reason,
    InternalIndex modify_index, int split, int descriptors,
    bool descriptor_to_field, Representation old_representation,
    Representation new_representation, PropertyConstness old_constness,
    PropertyConstness new_constness, MaybeHandle<FieldType> old_field_type,
    MaybeHandle<Object> old_value, MaybeHandle<FieldType> new_field_type,
    MaybeHandle<Object> new_value) {
  OFStream os(file);
  os << "[generalizing]";
  Tagged<Name> name = map->instance_descriptors(isolate)->GetKey(modify_index);
  if (IsString(name)) {
    Cast<String>(name)->PrintOn(file);
  } else {
    os << "{symbol " << reinterpret_cast<void*>(name.ptr()) << "}";
  }
  os << ":";
  if (descriptor_to_field) {
    os << "c";
  } else {
    os << old_representation.Mnemonic() << "{";
    if (old_field_type.is_null()) {
      os << Brief(*(old_value.ToHandleChecked()));
    } else {
      FieldType::PrintTo(*old_field_type.ToHandleChecked(), os);
    }
    os << ";" << old_constness << "}";
  }
  os << "->" << new_representation.Mnemonic() << "{";
  if (new_field_type.is_null()) {
    os << Brief(*(new_value.ToHandleChecked()));
  } else {
    FieldType::PrintTo(*new_field_type.ToHandleChecked(), os);
  }
  os << ";" << new_constness << "} (";
  if (strlen(reason) > 0) {
    os << reason;
  } else {
    os << "+" << (descriptors - split) << " maps";
  }
  os << ") [";
  JavaScriptFrame::PrintTop(isolate, file, false, true);
  os << "]\n";
}

}  // namespace

MapUpdater::MapUpdater(Isolate* isolate, Handle<Map> old_map)
    : isolate_(isolate),
      old_map_(old_map),
      old_descriptors_(old_map->instance_descriptors(isolate), isolate_),
      old_nof_(old_map_->NumberOfOwnDescriptors()),
      new_elements_kind_(old_map_->elements_kind()),
      is_transitionable_fast_elements_kind_(
          IsTransitionableFastElementsKind(new_elements_kind_)) {
  // We shouldn't try to update remote objects.
  DCHECK(
      !IsFunctionTemplateInfo(old_map->FindRootMap(isolate)->GetConstructor()));
}

Tagged<Name> MapUpdater::GetKey(InternalIndex descriptor) const {
  return old_descriptors_->GetKey(descriptor);
}

PropertyDetails MapUpdater::GetDetails(InternalIndex descriptor) const {
  DCHECK(descriptor.is_found());
  if (descriptor == modified_descriptor_) {
    PropertyAttributes attributes = new_attributes_;
    // If the original map was sealed or frozen, let's use the old
    // attributes so that we follow the same transition path as before.
    // Note that the user could not have changed the attributes because
    // both seal and freeze make the properties non-configurable. An exception
    // is transitioning from [[Writable]] = true to [[Writable]] = false (this
    // is allowed for frozen and sealed objects). To support it, we use the new
    // attributes if they have [[Writable]] == false.
    if ((integrity_level_ == SEALED || integrity_level_ == FROZEN) &&
        !(new_attributes_ & READ_ONLY)) {
      attributes = old_descriptors_->GetDetails(descriptor).attributes();
    }
    return PropertyDetails(new_kind_, attributes, new_location_, new_constness_,
                           new_representation_);
  }
  return old_descriptors_->GetDetails(descriptor);
}

Tagged<Object> MapUpdater::GetValue(InternalIndex descriptor) const {
  DCHECK(descriptor.is_found());
  if (descriptor == modified_descriptor_) {
    DCHECK_EQ(PropertyLocation::kDescriptor, new_location_);
    return *new_value_;
  }
  DCHECK_EQ(PropertyLocation::kDescriptor, GetDetails(descriptor).location());
  return old_descriptors_->GetStrongValue(descriptor);
}

Tagged<FieldType> MapUpdater::GetFieldType(InternalIndex descriptor) const {
  DCHECK(descriptor.is_found());
  if (descriptor == modified_descriptor_) {
    DCHECK_EQ(PropertyLocation::kField, new_location_);
    return *new_field_type_;
  }
  DCHECK_EQ(PropertyLocation::kField, GetDetails(descriptor).location());
  return old_descriptors_->GetFieldType(descriptor);
}

Handle<FieldType> MapUpdater::GetOrComputeFieldType(
    InternalIndex descriptor, PropertyLocation location,
    Representation representation) const {
  DCHECK(descriptor.is_found());
  // |location| is just a pre-fetched GetDetails(descriptor).location().
  DCHECK_EQ(location, GetDetails(descriptor).location());
  if (location == PropertyLocation::kField) {
    return handle(GetFieldType(descriptor), isolate_);
  } else {
    return Object::OptimalType(GetValue(descriptor), isolate_, representation);
  }
}

Handle<FieldType> MapUpdater::GetOrComputeFieldType(
    DirectHandle<DescriptorArray> descriptors, InternalIndex descriptor,
    PropertyLocation location, Representation representation) {
  // |location| is just a pre-fetched GetDetails(descriptor).location().
  DCHECK_EQ(descriptors->GetDetails(descriptor).location(), location);
  if (location == PropertyLocation::kField) {
    return handle(descriptors->GetFieldType(descriptor), isolate_);
  } else {
    return Object::OptimalType(descriptors->GetStrongValue(descriptor),
                               isolate_, representation);
  }
}

Handle<Map> MapUpdater::ReconfigureToDataField(InternalIndex descriptor,
                                               PropertyAttributes attributes,
                                               PropertyConstness constness,
                                               Representation representation,
                                               Handle<FieldType> field_type) {
  DCHECK_EQ(kInitialized, state_);
  DCHECK(descriptor.is_found());
  DCHECK(!old_map_->is_dictionary_map());

  ParkedSharedMutexGuardIf<base::kExclusive> mutex_guard(
      isolate_->main_thread_local_isolate(), isolate_->map_updater_access(),
      true);

  modified_descriptor_ = descriptor;
  new_kind_ = PropertyKind::kData;
  new_attributes_ = attributes;
  new_location_ = PropertyLocation::kField;

  PropertyDetails old_details =
      old_descriptors_->GetDetails(modified_descriptor_);

  // If property kind is not reconfigured merge the result with
  // representation/field type from the old descriptor.
  if (old_details.kind() == new_kind_) {
    new_constness_ = GeneralizeConstness(constness, old_details.constness());

    Representation old_representation = old_details.representation();
    new_representation_ = representation.generalize(old_representation);

    Handle<FieldType> old_field_type =
        GetOrComputeFieldType(old_descriptors_, modified_descriptor_,
                              old_details.location(), new_representation_);

    new_field_type_ =
        GeneralizeFieldType(old_representation, old_field_type,
                            new_representation_, field_type, isolate_);
  } else {
    // We don't know if this is a first property kind reconfiguration
    // and we don't know which value was in this property previously
    // therefore we can't treat such a property as constant.
    new_constness_ = PropertyConstness::kMutable;
    new_representation_ = representation;
    new_field_type_ = field_type;
  }

  Map::GeneralizeIfCanHaveTransitionableFastElementsKind(
      isolate_, old_map_->instance_type(), &new_representation_,
      &new_field_type_);

  if (TryReconfigureToDataFieldInplace() == kEnd) return result_map_;
  if (FindRootMap() == kEnd) return result_map_;
  if (FindTargetMap() == kEnd) return result_map_;
  if (ConstructNewMap() == kAtIntegrityLevelSource) {
    ConstructNewMapWithIntegrityLevelTransition();
  }
  CHECK_EQ(kEnd, state_);
  return result_map_;
}

Handle<Map> MapUpdater::ReconfigureElementsKind(ElementsKind elements_kind) {
  DCHECK_EQ(kInitialized, state_);

  new_elements_kind_ = elements_kind;
  is_transitionable_fast_elements_kind_ =
      IsTransitionableFastElementsKind(new_elements_kind_);

  return Update();
}

Handle<Map> MapUpdater::ApplyPrototypeTransition(Handle<HeapObject> prototype) {
  DCHECK(v8_flags.move_prototype_transitions_first);
  DCHECK_EQ(kInitialized, state_);
  DCHECK_NE(old_map_->prototype(), *prototype);

  // Prototype maps are replaced by deprecation when their prototype changes. No
  // need to add a transition.
  if (old_map_->is_prototype_map()) {
    return Map::CopyForPrototypeTransition(isolate_, old_map_, prototype);
  }

  new_prototype_ = prototype;

  // TODO(olivf): The updated map can have more generic field types than the
  // source map. This is ok, since UpdatePrototype also does an instance
  // migration. If we wanted to avoid the migration for most cases, we could
  // potentially back-propagate generalizations here.
  return Update();
}

// static
Handle<Map> MapUpdater::UpdateMapNoLock(Isolate* isolate, Handle<Map> map) {
  if (!map->is_deprecated()) return map;
  // TODO(ishell): support fast map updating if we enable it.
  CHECK(!v8_flags.fast_map_update);
  MapUpdater mu(isolate, map);
  // Update map without locking the Isolate::map_updater_access mutex.
  return mu.UpdateImpl();
}

Handle<Map> MapUpdater::Update() {
  base::SharedMutexGuard<base::kExclusive> mutex_guard(
      isolate_->map_updater_access());
  return UpdateImpl();
}

Handle<Map> MapUpdater::UpdateImpl() {
  DCHECK_EQ(kInitialized, state_);
  DCHECK_IMPLIES(new_prototype_.is_null() &&
                     new_elements_kind_ == old_map_->elements_kind(),
                 old_map_->is_deprecated());
  if (FindRootMap() == kEnd) return result_map_;
  if (FindTargetMap() == kEnd) return result_map_;
  if (ConstructNewMap() == kAtIntegrityLevelSource) {
    ConstructNewMapWithIntegrityLevelTransition();
  }
  CHECK_EQ(kEnd, state_);
  if (V8_UNLIKELY(v8_flags.fast_map_update && old_map_->is_deprecated())) {
    TransitionsAccessor::SetMigrationTarget(isolate_, old_map_, *result_map_);
  }
  return result_map_;
}

namespace {

struct IntegrityLevelTransitionInfo {
  explicit IntegrityLevelTransitionInfo(Tagged<Map> map)
      : integrity_level_source_map(map) {}

  bool has_integrity_level_transition = false;
  PropertyAttributes integrity_level = NONE;
  Tagged<Map> integrity_level_source_map;
  Tagged<Symbol> integrity_level_symbol;
};

IntegrityLevelTransitionInfo DetectIntegrityLevelTransitions(
    Tagged<Map> map, Isolate* isolate, DisallowGarbageCollection* no_gc,
    ConcurrencyMode cmode) {
  IntegrityLevelTransitionInfo info(map);

  // Figure out the most restrictive integrity level transition (it should
  // be the last one in the transition tree).
  DCHECK(!map->is_extensible());
  Tagged<Map> previous = Cast<Map>(map->GetBackPointer(isolate));
  TransitionsAccessor last_transitions(isolate, previous, IsConcurrent(cmode));
  if (!last_transitions.HasIntegrityLevelTransitionTo(
          map, &info.integrity_level_symbol, &info.integrity_level)) {
    // The last transition was not integrity level transition - just bail out.
    // This can happen in the following cases:
    // - there are private symbol transitions following the integrity level
    //   transitions (see crbug.com/v8/8854).
    // - there is a getter added in addition to an existing setter (or a setter
    //   in addition to an existing getter).
    return info;
  }

  Tagged<Map> source_map = previous;
  // Now walk up the back pointer chain and skip all integrity level
  // transitions. If we encounter any non-integrity level transition interleaved
  // with integrity level transitions, just bail out.
  while (!source_map->is_extensible()) {
    previous = Cast<Map>(source_map->GetBackPointer(isolate));
    TransitionsAccessor transitions(isolate, previous, IsConcurrent(cmode));
    if (!transitions.HasIntegrityLevelTransitionTo(source_map)) {
      return info;
    }
    source_map = previous;
  }

  // Integrity-level transitions never change number of descriptors.
  CHECK_EQ(map->NumberOfOwnDescriptors(), source_map->NumberOfOwnDescriptors());

  info.has_integrity_level_transition = true;
  info.integrity_level_source_map = source_map;
  return info;
}

}  // namespace

// static
std::optional<Tagged<Map>> MapUpdater::TryUpdateNoLock(Isolate* isolate,
                                                       Tagged<Map> old_map,
                                                       ConcurrencyMode cmode) {
  DisallowGarbageCollection no_gc;

  // Check the state of the root map.
  Tagged<Map> root_map = old_map->FindRootMap(isolate);
  if (root_map->is_deprecated()) {
    Tagged<JSFunction> constructor =
        Cast<JSFunction>(root_map->GetConstructor());
    DCHECK(constructor->has_initial_map());
    DCHECK(constructor->initial_map()->is_dictionary_map());
    if (constructor->initial_map()->elements_kind() !=
        old_map->elements_kind()) {
      return {};
    }
    return constructor->initial_map();
  }

  if (v8_flags.move_prototype_transitions_first &&
      root_map->prototype() != old_map->prototype()) {
    auto maybe_transition = TransitionsAccessor::GetPrototypeTransition(
        isolate, root_map, old_map->prototype());
    if (!maybe_transition) {
      return {};
    }
    root_map = *maybe_transition;
  }

  if (!old_map->EquivalentToForTransition(root_map, cmode)) return {};

  ElementsKind from_kind = root_map->elements_kind();
  ElementsKind to_kind = old_map->elements_kind();

  IntegrityLevelTransitionInfo info(old_map);
  if (root_map->is_extensible() != old_map->is_extensible()) {
    DCHECK(!old_map->is_extensible());
    DCHECK(root_map->is_extensible());
    info = DetectIntegrityLevelTransitions(old_map, isolate, &no_gc, cmode);
    // Bail out if there were some private symbol transitions mixed up
    // with the integrity level transitions.
    if (!info.has_integrity_level_transition) return {};
    // Make sure to replay the original elements kind transitions, before
    // the integrity level transition sets the elements to dictionary mode.
    DCHECK(to_kind == DICTIONARY_ELEMENTS ||
           to_kind == SLOW_STRING_WRAPPER_ELEMENTS ||
           IsTypedArrayOrRabGsabTypedArrayElementsKind(to_kind) ||
           IsAnyHoleyNonextensibleElementsKind(to_kind));
    to_kind = info.integrity_level_source_map->elements_kind();
  }
  if (from_kind != to_kind) {
    // Try to follow existing elements kind transitions.
    root_map = root_map->LookupElementsTransitionMap(isolate, to_kind, cmode);
    if (root_map.is_null()) return {};
    // From here on, use the map with correct elements kind as root map.
  }

  // Replay the transitions as they were before the integrity level transition.
  Tagged<Map> result = root_map->TryReplayPropertyTransitions(
      isolate, info.integrity_level_source_map, cmode);
  if (result.is_null()) return {};

  if (info.has_integrity_level_transition) {
    // Now replay the integrity level transition.
    result = TransitionsAccessor(isolate, *result, IsConcurrent(cmode))
                 .SearchSpecial(info.integrity_level_symbol);
  }
  if (result.is_null()) return {};

  CHECK_EQ(old_map->elements_kind(), (*result)->elements_kind());
  CHECK_EQ(old_map->instance_type(), (*result)->instance_type());
  return result;
}

void MapUpdater::GeneralizeField(DirectHandle<Map> map,
                                 InternalIndex modify_index,
                                 PropertyConstness new_constness,
                                 Representation new_representation,
                                 Handle<FieldType> new_field_type) {
  GeneralizeField(isolate_, map, modify_index, new_constness,
                  new_representation, new_field_type);

  DCHECK(*old_descriptors_ == old_map_->instance_descriptors(isolate_) ||
         *old_descriptors_ ==
             integrity_source_map_->instance_descriptors(isolate_));
}

MapUpdater::State MapUpdater::Normalize(const char* reason) {
  result_map_ =
      Map::Normalize(isolate_, old_map_, new_elements_kind_, new_prototype_,
                     CLEAR_INOBJECT_PROPERTIES, reason);
  state_ = kEnd;
  return state_;  // Done.
}

// static
void MapUpdater::CompleteInobjectSlackTracking(Isolate* isolate,
                                               Tagged<Map> initial_map) {
  // Has to be an initial map.
  CHECK(IsUndefined(initial_map->GetBackPointer(), isolate));

  const int slack = initial_map->ComputeMinObjectSlack(isolate);
  DCHECK_GE(slack, 0);

  TransitionsAccessor transitions(isolate, initial_map);
  TransitionsAccessor::TraverseCallback callback;
  if (slack != 0) {
    // Resize the initial map and all maps in its transition tree.
    callback = [slack](Tagged<Map> map) {
#ifdef DEBUG
      int old_visitor_id = Map::GetVisitorId(map);
      int new_unused = map->UnusedPropertyFields() - slack;
#endif
      map->set_instance_size(map->InstanceSizeFromSlack(slack));
      map->set_construction_counter(Map::kNoSlackTracking);
      DCHECK_EQ(old_visitor_id, Map::GetVisitorId(map));
      DCHECK_EQ(new_unused, map->UnusedPropertyFields());
    };
  } else {
    // Stop slack tracking for this map.
    callback = [&](Tagged<Map> map) {
      map->set_construction_counter(Map::kNoSlackTracking);
      DCHECK(!TransitionsAccessor(isolate, map).HasSideStepTransitions());
    };
  }

  {
    // The map_updater_access lock is taken here to guarantee atomicity of all
    // related map changes (instead of guaranteeing only atomicity of each
    // single map change). This is needed e.g. by InstancesNeedsRewriting,
    // which expects certain relations between maps to hold.
    //
    // Note: Avoid locking the full_transition_array_access lock inside this
    // call to TraverseTransitionTree to prevent dependencies between the two
    // locks.
    base::SharedMutexGuard<base::kExclusive> mutex_guard(
        isolate->map_updater_access());
    transitions.TraverseTransitionTree(callback);
  }
}

MapUpdater::State MapUpdater::TryReconfigureToDataFieldInplace() {
  // Updating deprecated maps in-place doesn't make sense.
  if (old_map_->is_deprecated()) return state_;

  if (new_representation_.IsNone()) return state_;  // Not done yet.

  PropertyDetails old_details =
      old_descriptors_->GetDetails(modified_descriptor_);

  if (old_details.attributes() != new_attributes_ ||
      old_details.kind() != new_kind_ ||
      old_details.location() != new_location_) {
    // These changes can't be done in-place.
    return state_;  // Not done yet.
  }

  Representation old_representation = old_details.representation();
  if (!old_representation.CanBeInPlaceChangedTo(new_representation_)) {
    return state_;  // Not done yet.
  }

  DCHECK_EQ(new_kind_, old_details.kind());
  DCHECK_EQ(new_attributes_, old_details.attributes());
  DCHECK_EQ(PropertyLocation::kField, old_details.location());
  if (v8_flags.trace_generalization) {
    PrintGeneralization(
        isolate_, old_map_, stdout, "uninitialized field", modified_descriptor_,
        old_nof_, old_nof_, false, old_representation, new_representation_,
        old_details.constness(), new_constness_,
        handle(old_descriptors_->GetFieldType(modified_descriptor_), isolate_),
        MaybeHandle<Object>(), new_field_type_, MaybeHandle<Object>());
  }
  GeneralizeField(old_map_, modified_descriptor_, new_constness_,
                  new_representation_, new_field_type_);
  // Check that the descriptor array was updated.
  DCHECK(old_descriptors_->GetDetails(modified_descriptor_)
             .representation()
             .Equals(new_representation_));
  DCHECK(FieldType::NowIs(old_descriptors_->GetFieldType(modified_descriptor_),
                          new_field_type_));

  result_map_ = old_map_;
  state_ = kEnd;
  return state_;  // Done.
}

bool MapUpdater::TrySaveIntegrityLevelTransitions() {
  // Figure out the most restrictive integrity level transition (it should
  // be the last one in the transition tree).
  Handle<Map> previous =
      handle(Cast<Map>(old_map_->GetBackPointer()), isolate_);
  Tagged<Symbol> integrity_level_symbol;
  TransitionsAccessor last_transitions(isolate_, *previous);
  if (!last_transitions.HasIntegrityLevelTransitionTo(
          *old_map_, &integrity_level_symbol, &integrity_level_)) {
    // The last transition was not integrity level transition - just bail out.
    // This can happen in the following cases:
    // - there are private symbol transitions following the integrity level
    //   transitions (see crbug.com/v8/8854).
    // - there is a getter added in addition to an existing setter (or a setter
    //   in addition to an existing getter).
    return false;
  }
  integrity_level_symbol_ = handle(integrity_level_symbol, isolate_);
  integrity_source_map_ = previous;

  // Now walk up the back pointer chain and skip all integrity level
  // transitions. If we encounter any non-integrity level transition interleaved
  // with integrity level transitions, just bail out.
  while (!integrity_source_map_->is_extensible()) {
    previous =
        handle(Cast<Map>(integrity_source_map_->GetBackPointer()), isolate_);
    TransitionsAccessor transitions(isolate_, *previous);
    if (!transitions.HasIntegrityLevelTransitionTo(*integrity_source_map_)) {
      return false;
    }
    integrity_source_map_ = previous;
  }

  // Integrity-level transitions never change number of descriptors.
  CHECK_EQ(old_map_->NumberOfOwnDescriptors(),
           integrity_source_map_->NumberOfOwnDescriptors());

  has_integrity_level_transition_ = true;
  old_descriptors_ =
      handle(integrity_source_map_->instance_descriptors(isolate_), isolate_);
  return true;
}

MapUpdater::State MapUpdater::FindRootMap() {
  DCHECK_EQ(kInitialized, state_);

  if (new_prototype_.is_null()) {
    new_prototype_ = handle(old_map_->prototype(), isolate_);
  }

  // Check the state of the root map.
  root_map_ = handle(old_map_->FindRootMap(isolate_), isolate_);
  ElementsKind from_kind = root_map_->elements_kind();
  ElementsKind to_kind = new_elements_kind_;

  if (root_map_->is_deprecated()) {
    state_ = kEnd;
    result_map_ = handle(
        Cast<JSFunction>(root_map_->GetConstructor())->initial_map(), isolate_);
    result_map_ = Map::AsElementsKind(isolate_, result_map_, to_kind);
    DCHECK(result_map_->is_dictionary_map());
    return state_;
  }

  // In this first check allow the root map to have the wrong prototype, as we
  // will deal with prototype transitions later.
  if (!old_map_->EquivalentToForTransition(
          *root_map_, ConcurrencyMode::kSynchronous,
          v8_flags.move_prototype_transitions_first
              ? handle(root_map_->prototype(), isolate_)
              : Handle<HeapObject>())) {
    return Normalize("Normalize_NotEquivalent");
  } else if (old_map_->is_extensible() != root_map_->is_extensible()) {
    DCHECK(!old_map_->is_extensible());
    DCHECK(root_map_->is_extensible());
    // We have an integrity level transition in the tree, let us make a note
    // of that transition to be able to replay it later.
    if (!TrySaveIntegrityLevelTransitions()) {
      return Normalize("Normalize_PrivateSymbolsOnNonExtensible");
    }

    // We want to build transitions to the original element kind (before
    // the seal transitions), so change {to_kind} accordingly.
    DCHECK(to_kind == DICTIONARY_ELEMENTS ||
           to_kind == SLOW_STRING_WRAPPER_ELEMENTS ||
           IsTypedArrayOrRabGsabTypedArrayElementsKind(to_kind) ||
           IsAnyNonextensibleElementsKind(to_kind));
    to_kind = integrity_source_map_->elements_kind();
  }

  // TODO(ishell): Add a test for SLOW_SLOPPY_ARGUMENTS_ELEMENTS.
  if (from_kind != to_kind && to_kind != DICTIONARY_ELEMENTS &&
      to_kind != SLOW_STRING_WRAPPER_ELEMENTS &&
      to_kind != SLOW_SLOPPY_ARGUMENTS_ELEMENTS &&
      !(IsTransitionableFastElementsKind(from_kind) &&
        IsMoreGeneralElementsKindTransition(from_kind, to_kind))) {
    return Normalize("Normalize_InvalidElementsTransition");
  }

  int root_nof = root_map_->NumberOfOwnDescriptors();
  if (modified_descriptor_.is_found() &&
      modified_descriptor_.as_int() < root_nof) {
    PropertyDetails old_details =
        old_descriptors_->GetDetails(modified_descriptor_);
    if (old_details.kind() != new_kind_ ||
        old_details.attributes() != new_attributes_) {
      return Normalize("Normalize_RootModification1");
    }
    if (old_details.location() != PropertyLocation::kField) {
      return Normalize("Normalize_RootModification2");
    }
    if (!new_representation_.fits_into(old_details.representation())) {
      return Normalize("Normalize_RootModification4");
    }

    DCHECK_EQ(PropertyKind::kData, old_details.kind());
    DCHECK_EQ(PropertyKind::kData, new_kind_);
    DCHECK_EQ(PropertyLocation::kField, new_location_);

    // Modify root map in-place. The GeneralizeField method is a no-op
    // if the {old_map_} is already general enough to hold the requested
    // {new_constness_} and {new_field_type_}.
    GeneralizeField(old_map_, modified_descriptor_, new_constness_,
                    old_details.representation(), new_field_type_);
  }

  // From here on, use the map with correct elements kind and prototype as root
  // map.
  if (root_map_->prototype() != *new_prototype_) {
    DCHECK(v8_flags.move_prototype_transitions_first);
    Handle<Map> new_root_map_ =
        Map::TransitionToUpdatePrototype(isolate_, root_map_, new_prototype_);

    root_map_ = new_root_map_;

    if (!old_map_->EquivalentToForTransition(
            *root_map_, ConcurrencyMode::kSynchronous, new_prototype_)) {
      return Normalize("Normalize_NotEquivalent");
    }
  }
  root_map_ = Map::AsElementsKind(isolate_, root_map_, to_kind);
  DCHECK(old_map_->EquivalentToForTransition(
      *root_map_, ConcurrencyMode::kSynchronous, new_prototype_));

  state_ = kAtRootMap;
  return state_;  // Not done yet.
}

MapUpdater::State MapUpdater::FindTargetMap() {
  DCHECK_EQ(kAtRootMap, state_);
  target_map_ = root_map_;

  int root_nof = root_map_->NumberOfOwnDescriptors();
  for (InternalIndex i : InternalIndex::Range(root_nof, old_nof_)) {
    PropertyDetails old_details = GetDetails(i);
    Handle<Map> tmp_map;
    MaybeHandle<Map> maybe_tmp_map = TransitionsAccessor::SearchTransition(
        isolate_, target_map_, GetKey(i), old_details.kind(),
        old_details.attributes());
    if (!maybe_tmp_map.ToHandle(&tmp_map)) break;
    DirectHandle<DescriptorArray> tmp_descriptors(
        tmp_map->instance_descriptors(isolate_), isolate_);

    // Check if target map is incompatible.
    PropertyDetails tmp_details = tmp_descriptors->GetDetails(i);
    DCHECK_EQ(old_details.kind(), tmp_details.kind());
    DCHECK_EQ(old_details.attributes(), tmp_details.attributes());
    if (old_details.kind() == PropertyKind::kAccessor &&
        !EqualImmutableValues(GetValue(i),
                              tmp_descriptors->GetStrongValue(i))) {
      // TODO(ishell): mutable accessors are not implemented yet.
      return Normalize("Normalize_Incompatible");
    }
    if (!IsGeneralizableTo(old_details.location(), tmp_details.location())) {
      break;
    }
    Representation tmp_representation = tmp_details.representation();
    if (!old_details.representation().fits_into(tmp_representation)) {
      // Try updating the field in-place to a generalized type.
      Representation generalized =
          tmp_representation.generalize(old_details.representation());
      if (!tmp_representation.CanBeInPlaceChangedTo(generalized)) {
        break;
      }
      tmp_representation = generalized;
    }

    if (tmp_details.location() == PropertyLocation::kField) {
      Handle<FieldType> old_field_type =
          GetOrComputeFieldType(i, old_details.location(), tmp_representation);
      GeneralizeField(tmp_map, i, old_details.constness(), tmp_representation,
                      old_field_type);
    } else {
      // kDescriptor: Check that the value matches.
      if (!EqualImmutableValues(GetValue(i),
                                tmp_descriptors->GetStrongValue(i))) {
        break;
      }
    }
    DCHECK(!tmp_map->is_deprecated());
    target_map_ = tmp_map;
  }

  // Directly change the map if the target map is more general.
  int target_nof = target_map_->NumberOfOwnDescriptors();
  if (target_nof == old_nof_) {
#ifdef DEBUG
    if (modified_descriptor_.is_found()) {
      Tagged<DescriptorArray> target_descriptors =
          target_map_->instance_descriptors(isolate_);
      PropertyDetails details =
          target_descriptors->GetDetails(modified_descriptor_);
      DCHECK_EQ(new_kind_, details.kind());
      DCHECK_EQ(GetDetails(modified_descriptor_).attributes(),
                details.attributes());
      DCHECK(IsGeneralizableTo(new_constness_, details.constness()));
      DCHECK_EQ(new_location_, details.location());
      DCHECK(new_representation_.fits_into(details.representation()));
      if (new_location_ == PropertyLocation::kField) {
        DCHECK_EQ(PropertyLocation::kField, details.location());
        DCHECK(FieldType::NowIs(
            *new_field_type_,
            target_descriptors->GetFieldType(modified_descriptor_)));
      } else {
        DCHECK(details.location() == PropertyLocation::kField ||
               EqualImmutableValues(
                   *new_value_,
                   target_descriptors->GetStrongValue(modified_descriptor_)));
      }
    }
#endif
    if (*target_map_ != *old_map_) {
      old_map_->NotifyLeafMapLayoutChange(isolate_);
    }
    if (!has_integrity_level_transition_) {
      result_map_ = target_map_;
      state_ = kEnd;
      return state_;  // Done.
    }

    // We try to replay the integrity level transition here.
    MaybeHandle<Map> maybe_transition = TransitionsAccessor::SearchSpecial(
        isolate_, target_map_, *integrity_level_symbol_);
    if (maybe_transition.ToHandle(&result_map_)) {
      state_ = kEnd;
      return state_;  // Done.
    }
  }

  // Find the last compatible target map in the transition tree.
  for (InternalIndex i : InternalIndex::Range(target_nof, old_nof_)) {
    PropertyDetails old_details = GetDetails(i);
    Handle<Map> tmp_map;
    MaybeHandle<Map> maybe_tmp_map = TransitionsAccessor::SearchTransition(
        isolate_, target_map_, GetKey(i), old_details.kind(),
        old_details.attributes());
    if (!maybe_tmp_map.ToHandle(&tmp_map)) break;
    DirectHandle<DescriptorArray> tmp_descriptors(
        tmp_map->instance_descriptors(isolate_), isolate_);
#ifdef DEBUG
    // Check that target map is compatible.
    PropertyDetails tmp_details = tmp_descriptors->GetDetails(i);
    DCHECK_EQ(old_details.kind(), tmp_details.kind());
    DCHECK_EQ(old_details.attributes(), tmp_details.attributes());
#endif
    if (old_details.kind() == PropertyKind::kAccessor &&
        !EqualImmutableValues(GetValue(i),
                              tmp_descriptors->GetStrongValue(i))) {
      return Normalize("Normalize_Incompatible");
    }
    DCHECK(!tmp_map->is_deprecated());
    target_map_ = tmp_map;
  }

  state_ = kAtTargetMap;
  return state_;  // Not done yet.
}

Handle<DescriptorArray> MapUpdater::BuildDescriptorArray() {
  InstanceType instance_type = old_map_->instance_type();
  int target_nof = target_map_->NumberOfOwnDescriptors();
  DirectHandle<DescriptorArray> target_descriptors(
      target_map_->instance_descriptors(isolate_), isolate_);

  // Allocate a new descriptor array large enough to hold the required
  // descriptors, with minimally the exact same size as the old descriptor
  // array.
  int new_slack =
      std::max<int>(old_nof_, old_descriptors_->number_of_descriptors()) -
      old_nof_;
  Handle<DescriptorArray> new_descriptors =
      DescriptorArray::Allocate(isolate_, old_nof_, new_slack);
  DCHECK(new_descriptors->number_of_all_descriptors() >
             target_descriptors->number_of_all_descriptors() ||
         new_descriptors->number_of_slack_descriptors() > 0 ||
         new_descriptors->number_of_descriptors() ==
             old_descriptors_->number_of_descriptors());
  DCHECK(new_descriptors->number_of_descriptors() == old_nof_);

  int root_nof = root_map_->NumberOfOwnDescriptors();

  // Given that we passed root modification check in FindRootMap() so
  // the root descriptors are either not modified at all or already more
  // general than we requested. Take |root_nof| entries as is.
  // 0 -> |root_nof|
  int current_offset = 0;
  for (InternalIndex i : InternalIndex::Range(root_nof)) {
    PropertyDetails old_details = old_descriptors_->GetDetails(i);
    if (old_details.location() == PropertyLocation::kField) {
      current_offset += old_details.field_width_in_words();
    }
#ifdef DEBUG
    // Ensuring FindRootMap gave us a compatible root map.
    // TODO(olivf): In some cases it might be nice to be able to generalize the
    // root map (for instance if the prototype transitions overflowed). For that
    // we'd need to generalize old_details with the root_details here.
    PropertyDetails root_details =
        root_map_->instance_descriptors()->GetDetails(i);
    DCHECK_EQ(
        old_details.representation().generalize(root_details.representation()),
        root_details.representation());
    if (!root_map_->IsDetached(isolate_)) {
      DCHECK(old_details.representation().IsCompatibleForLoad(
          root_details.representation()));
    }
    DCHECK_LE(old_details.constness(), root_details.constness());
    DCHECK_EQ(old_details.attributes(), root_details.attributes());
#endif  // DEBUG
    new_descriptors->Set(i, GetKey(i), old_descriptors_->GetValue(i),
                         old_details);
  }

  // Merge "updated" old_descriptor entries with target_descriptor entries.
  // |root_nof| -> |target_nof|
  for (InternalIndex i : InternalIndex::Range(root_nof, target_nof)) {
    Handle<Name> key(GetKey(i), isolate_);
    PropertyDetails old_details = GetDetails(i);
    PropertyDetails target_details = target_descriptors->GetDetails(i);

    PropertyKind next_kind = old_details.kind();
    PropertyAttributes next_attributes = old_details.attributes();
    DCHECK_EQ(next_kind, target_details.kind());
    DCHECK_EQ(next_attributes, target_details.attributes());

    PropertyConstness next_constness = GeneralizeConstness(
        old_details.constness(), target_details.constness());

    // Note: failed values equality check does not invalidate per-object
    // property constness.
    PropertyLocation next_location =
        old_details.location() == PropertyLocation::kField ||
                target_details.location() == PropertyLocation::kField ||
                !EqualImmutableValues(target_descriptors->GetStrongValue(i),
                                      GetValue(i))
            ? PropertyLocation::kField
            : PropertyLocation::kDescriptor;

    // Ensure that mutable values are stored in fields.
    DCHECK_IMPLIES(next_constness == PropertyConstness::kMutable,
                   next_location == PropertyLocation::kField);

    Representation next_representation =
        old_details.representation().generalize(
            target_details.representation());

    if (next_location == PropertyLocation::kField) {
      Handle<FieldType> old_field_type =
          GetOrComputeFieldType(i, old_details.location(), next_representation);

      Handle<FieldType> target_field_type =
          GetOrComputeFieldType(target_descriptors, i,
                                target_details.location(), next_representation);

      Handle<FieldType> next_field_type =
          GeneralizeFieldType(old_details.representation(), old_field_type,
                              next_representation, target_field_type, isolate_);

      Map::GeneralizeIfCanHaveTransitionableFastElementsKind(
          isolate_, instance_type, &next_representation, &next_field_type);

      MaybeObjectHandle wrapped_type(Map::WrapFieldType(next_field_type));
      Descriptor d;
      if (next_kind == PropertyKind::kData) {
        d = Descriptor::DataField(key, current_offset, next_attributes,
                                  next_constness, next_representation,
                                  wrapped_type);
      } else {
        // TODO(ishell): mutable accessors are not implemented yet.
        UNIMPLEMENTED();
      }
      current_offset += d.GetDetails().field_width_in_words();
      new_descriptors->Set(i, &d);
    } else {
      DCHECK_EQ(PropertyLocation::kDescriptor, next_location);
      DCHECK_EQ(PropertyConstness::kConst, next_constness);

      Handle<Object> value(GetValue(i), isolate_);
      DCHECK_EQ(PropertyKind::kAccessor, next_kind);
      Descriptor d = Descriptor::AccessorConstant(key, value, next_attributes);
      new_descriptors->Set(i, &d);
    }
  }

  // Take "updated" old_descriptor entries.
  // |target_nof| -> |old_nof|
  for (InternalIndex i : InternalIndex::Range(target_nof, old_nof_)) {
    PropertyDetails old_details = GetDetails(i);
    Handle<Name> key(GetKey(i), isolate_);

    PropertyKind next_kind = old_details.kind();
    PropertyAttributes next_attributes = old_details.attributes();
    PropertyConstness next_constness = old_details.constness();
    PropertyLocation next_location = old_details.location();
    Representation next_representation = old_details.representation();

    if (next_location == PropertyLocation::kField) {
      Handle<FieldType> next_field_type =
          GetOrComputeFieldType(i, old_details.location(), next_representation);

      // If the |new_elements_kind_| is still transitionable then the old map's
      // elements kind is also transitionable and therefore the old descriptors
      // array must already have generalized field type.
      CHECK_IMPLIES(
          is_transitionable_fast_elements_kind_,
          Map::IsMostGeneralFieldType(next_representation, *next_field_type));

      MaybeObjectHandle wrapped_type(Map::WrapFieldType(next_field_type));
      Descriptor d;
      if (next_kind == PropertyKind::kData) {
        d = Descriptor::DataField(key, current_offset, next_attributes,
                                  next_constness, next_representation,
                                  wrapped_type);
      } else {
        // TODO(ishell): mutable accessors are not implemented yet.
        UNIMPLEMENTED();
      }
      current_offset += d.GetDetails().field_width_in_words();
      new_descriptors->Set(i, &d);
    } else {
      DCHECK_EQ(PropertyLocation::kDescriptor, next_location);
      DCHECK_EQ(PropertyConstness::kConst, next_constness);

      Handle<Object> value(GetValue(i), isolate_);
      Descriptor d;
      if (next_kind == PropertyKind::kData) {
        d = Descriptor::DataConstant(key, value, next_attributes);
      } else {
        DCHECK_EQ(PropertyKind::kAccessor, next_kind);
        d = Descriptor::AccessorConstant(key, value, next_attributes);
      }
      new_descriptors->Set(i, &d);
    }
  }

  new_descriptors->Sort();
  return new_descriptors;
}

Handle<Map> MapUpdater::FindSplitMap(
    DirectHandle<DescriptorArray> descriptors) {
  int root_nof = root_map_->NumberOfOwnDescriptors();
  Tagged<Map> current = *root_map_;
  for (InternalIndex i : InternalIndex::Range(root_nof, old_nof_)) {
    Tagged<Name> name = descriptors->GetKey(i);
    PropertyDetails details = descriptors->GetDetails(i);
    Tagged<Map> next =
        TransitionsAccessor(isolate_, current)
            .SearchTransition(name, details.kind(), details.attributes());
    if (next.is_null()) break;
    Tagged<DescriptorArray> next_descriptors =
        next->instance_descriptors(isolate_);

    PropertyDetails next_details = next_descriptors->GetDetails(i);
    DCHECK_EQ(details.kind(), next_details.kind());
    DCHECK_EQ(details.attributes(), next_details.attributes());
    if (details.constness() != next_details.constness()) break;
    if (details.location() != next_details.location()) break;
    if (!details.representation().Equals(next_details.representation())) break;

    if (next_details.location() == PropertyLocation::kField) {
      Tagged<FieldType> next_field_type = next_descriptors->GetFieldType(i);
      if (!FieldType::NowIs(descriptors->GetFieldType(i), next_field_type)) {
        break;
      }
    } else {
      if (!EqualImmutableValues(descriptors->GetStrongValue(i),
                                next_descriptors->GetStrongValue(i))) {
        break;
      }
    }
    current = next;
  }
  return handle(current, isolate_);
}

MapUpdater::State MapUpdater::ConstructNewMap() {
#ifdef DEBUG
  DirectHandle<EnumCache> old_enum_cache(
      old_map_->instance_descriptors()->enum_cache(), isolate_);
#endif
  DirectHandle<DescriptorArray> new_descriptors = BuildDescriptorArray();

  Handle<Map> split_map = FindSplitMap(new_descriptors);
  int split_nof = split_map->NumberOfOwnDescriptors();
  if (old_nof_ == split_nof) {
    CHECK(has_integrity_level_transition_);
    state_ = kAtIntegrityLevelSource;
    return state_;
  }
  InternalIndex split_index(split_nof);
  PropertyDetails split_details = GetDetails(split_index);

  // Invalidate a transition target at |key|.
  MaybeHandle<Map> maybe_transition = TransitionsAccessor::SearchTransition(
      isolate_, split_map, GetKey(split_index), split_details.kind(),
      split_details.attributes());
  if (!maybe_transition.is_null()) {
    maybe_transition.ToHandleChecked()->DeprecateTransitionTree(isolate_);
  }

  // If |maybe_transition| is not nullptr then the transition array already
  // contains entry for given descriptor. This means that the transition
  // could be inserted regardless of whether transitions array is full or not.
  if (maybe_transition.is_null() &&
      !TransitionsAccessor::CanHaveMoreTransitions(isolate_, split_map)) {
    return Normalize("Normalize_CantHaveMoreTransitions");
  }

  old_map_->NotifyLeafMapLayoutChange(isolate_);

  if (v8_flags.trace_generalization && modified_descriptor_.is_found()) {
    PropertyDetails old_details =
        old_descriptors_->GetDetails(modified_descriptor_);
    PropertyDetails new_details =
        new_descriptors->GetDetails(modified_descriptor_);
    MaybeHandle<FieldType> old_field_type;
    MaybeHandle<FieldType> new_field_type;
    MaybeHandle<Object> old_value;
    MaybeHandle<Object> new_value;
    if (old_details.location() == PropertyLocation::kField) {
      old_field_type = handle(
          old_descriptors_->GetFieldType(modified_descriptor_), isolate_);
    } else {
      old_value = handle(old_descriptors_->GetStrongValue(modified_descriptor_),
                         isolate_);
    }
    if (new_details.location() == PropertyLocation::kField) {
      new_field_type =
          handle(new_descriptors->GetFieldType(modified_descriptor_), isolate_);
    } else {
      new_value = handle(new_descriptors->GetStrongValue(modified_descriptor_),
                         isolate_);
    }

    PrintGeneralization(
        isolate_, old_map_, stdout, "", modified_descriptor_, split_nof,
        old_nof_,
        old_details.location() == PropertyLocation::kDescriptor &&
            new_location_ == PropertyLocation::kField,
        old_details.representation(), new_details.representation(),
        old_details.constness(), new_details.constness(), old_field_type,
        old_value, new_field_type, new_value);
  }

  Handle<Map> new_map =
      Map::AddMissingTransitions(isolate_, split_map, new_descriptors);

  bool had_any_enum_cache =
      split_map->instance_descriptors(isolate_)
              ->enum_cache()
              ->keys()
              ->length() > 0 ||
      old_descriptors_->enum_cache()->keys()->length() > 0;

  // Deprecated part of the transition tree is no longer reachable, so replace
  // current instance descriptors in the "survived" part of the tree with
  // the new descriptors to maintain descriptors sharing invariant.
  split_map->ReplaceDescriptors(isolate_, *new_descriptors);

  // If the old descriptors had an enum cache (or if {split_map}'s descriptors
  // had one), make sure the new ones do too.
  if (had_any_enum_cache && new_map->NumberOfEnumerableProperties() > 0) {
    FastKeyAccumulator::InitializeFastPropertyEnumCache(
        isolate_, new_map, new_map->NumberOfEnumerableProperties());
  }

  // The old map has to still point to the old enum cache. This is because we
  // might have cached the enum indices, for iterating over objects with the old
  // map -- we don't want this enum cache to move ownership to the new branch,
  // because then it might get trimmed past the old map's field count.
  DCHECK_EQ(old_map_->instance_descriptors()->enum_cache(), *old_enum_cache);

  if (has_integrity_level_transition_) {
    target_map_ = new_map;
    state_ = kAtIntegrityLevelSource;
  } else {
    result_map_ = new_map;
    state_ = kEnd;
  }
  return state_;  // Done.
}

MapUpdater::State MapUpdater::ConstructNewMapWithIntegrityLevelTransition() {
  DCHECK_EQ(kAtIntegrityLevelSource, state_);

  if (!TransitionsAccessor::CanHaveMoreTransitions(isolate_, target_map_)) {
    return Normalize("Normalize_CantHaveMoreTransitions");
  }

  result_map_ = Map::CopyForPreventExtensions(
      isolate_, target_map_, integrity_level_, integrity_level_symbol_,
      "CopyForPreventExtensions",
      old_map_->elements_kind() == DICTIONARY_ELEMENTS);
  DCHECK_IMPLIES(old_map_->elements_kind() == DICTIONARY_ELEMENTS,
                 result_map_->elements_kind() == DICTIONARY_ELEMENTS);

  state_ = kEnd;
  return state_;
}

namespace {

void PrintReconfiguration(Isolate* isolate, DirectHandle<Map> map, FILE* file,
                          InternalIndex modify_index, PropertyKind kind,
                          PropertyAttributes attributes) {
  OFStream os(file);
  os << "[reconfiguring]";
  Tagged<Name> name = map->instance_descriptors(isolate)->GetKey(modify_index);
  if (IsString(name)) {
    Cast<String>(name)->PrintOn(file);
  } else {
    os << "{symbol " << reinterpret_cast<void*>(name.ptr()) << "}";
  }
  os << ": " << (kind == PropertyKind::kData ? "kData" : "ACCESSORS")
     << ", attrs: ";
  os << attributes << " [";
  JavaScriptFrame::PrintTop(isolate, file, false, true);
  os << "]\n";
}

}  // namespace

// static
Handle<Map> MapUpdater::ReconfigureExistingProperty(
    Isolate* isolate, Handle<Map> map, InternalIndex descriptor,
    PropertyKind kind, PropertyAttributes attributes,
    PropertyConstness constness) {
  // Dictionaries have to be reconfigured in-place.
  DCHECK(!map->is_dictionary_map());
  DCHECK_EQ(PropertyKind::kData, kind);  // Only kData case is supported so far.

  if (!IsMap(map->GetBackPointer())) {
    // There is no benefit from reconstructing transition tree for maps without
    // back pointers, normalize and try to hit the map cache instead.
    return Map::Normalize(isolate, map, CLEAR_INOBJECT_PROPERTIES,
                          "Normalize_AttributesMismatchProtoMap");
  }

  if (v8_flags.trace_generalization) {
    PrintReconfiguration(isolate, map, stdout, descriptor, kind, attributes);
  }

  return MapUpdater{isolate, map}.ReconfigureToDataField(
      descriptor, attributes, constness, Representation::None(),
      FieldType::None(isolate));
}

// static
void MapUpdater::UpdateFieldType(Isolate* isolate, DirectHandle<Map> map,
                                 InternalIndex descriptor, Handle<Name> name,
                                 PropertyConstness new_constness,
                                 Representation new_representation,
                                 Handle<FieldType> new_type) {
  // We store raw pointers in the queue, so no allocations are allowed.
  DisallowGarbageCollection no_gc;
  PropertyDetails details =
      map->instance_descriptors(isolate)->GetDetails(descriptor);
  if (details.location() != PropertyLocation::kField) return;
  CHECK_EQ(PropertyKind::kData, details.kind());

  if (new_constness != details.constness() && map->is_prototype_map()) {
    JSObject::InvalidatePrototypeChains(*map);
  }

  std::queue<Tagged<Map>> backlog;
  backlog.push(*map);
  std::vector<Tagged<Map>> sidestep_transition;

  ReadOnlyRoots roots(isolate);
  while (!backlog.empty()) {
    Tagged<Map> current = backlog.front();
    backlog.pop();

    TransitionsAccessor transitions(isolate, current);
    transitions.ForEachTransition(
        &no_gc, [&](Tagged<Map> target) { backlog.push(target); },
        [&](Tagged<Map> target) {
          if (v8_flags.move_prototype_transitions_first) {
            backlog.push(target);
          }
        },
        [&](Tagged<Object> target) {
          if (!target.IsSmi() && !Cast<Map>(target)->is_deprecated()) {
            sidestep_transition.push_back(Cast<Map>(target));
          }
        });

    Tagged<DescriptorArray> descriptors =
        current->instance_descriptors(isolate);
    details = descriptors->GetDetails(descriptor);

    // It is allowed to change representation here only from None
    // to something or from Smi or HeapObject to Tagged.
    CHECK(details.representation().Equals(new_representation) ||
          details.representation().CanBeInPlaceChangedTo(new_representation));

    // Skip if we already updated the shared descriptor or the target was more
    // general in the first place.
    if (new_constness == details.constness() &&
        new_representation.Equals(details.representation()) &&
        FieldType::Equals(descriptors->GetFieldType(descriptor), *new_type)) {
      continue;
    }

    DCHECK_IMPLIES(IsClass(*new_type), new_representation.IsHeapObject());
    MaybeObjectHandle wrapped_type(Map::WrapFieldType(new_type));
    Descriptor d = Descriptor::DataField(
        name, descriptors->GetFieldIndex(descriptor), details.attributes(),
        new_constness, new_representation, wrapped_type);
    DCHECK_EQ(descriptors->GetKey(descriptor), *d.key_);
    descriptors->Replace(descriptor, &d);
  }

  for (Tagged<Map> current : sidestep_transition) {
    Tagged<DescriptorArray> descriptors =
        current->instance_descriptors(isolate);
    details = descriptors->GetDetails(descriptor);
    // Through side-steps we can reach transition trees which are already more
    // generalized. Ensure we don't re-concretize them.
    PropertyConstness cur_new_constness =
        GeneralizeConstness(new_constness, details.constness());
    Representation cur_new_representation =
        new_representation.generalize(details.representation());
    Handle<FieldType> cur_new_type = GeneralizeFieldType(
        details.representation(),
        handle(descriptors->GetFieldType(descriptor), isolate),
        cur_new_representation, new_type, isolate);
    CHECK(new_representation.fits_into(cur_new_representation));
    // Skip if we already updated the shared descriptor or the target was more
    // general in the first place.
    if (cur_new_constness != details.constness() ||
        !cur_new_representation.Equals(details.representation()) ||
        !FieldType::Equals(descriptors->GetFieldType(descriptor),
                           *cur_new_type)) {
      GeneralizeField(isolate, handle(current, isolate), descriptor,
                      cur_new_constness, cur_new_representation, cur_new_type);
    }
  }
}

// TODO(jgruber): Lock the map-updater mutex.
// static
void MapUpdater::GeneralizeField(Isolate* isolate, DirectHandle<Map> map,
                                 InternalIndex modify_index,
                                 PropertyConstness new_constness,
                                 Representation new_representation,
                                 Handle<FieldType> new_field_type) {
  CHECK(!map->is_deprecated());

  // Check if we actually need to generalize the field type at all.
  DirectHandle<DescriptorArray> old_descriptors(
      map->instance_descriptors(isolate), isolate);
  PropertyDetails old_details = old_descriptors->GetDetails(modify_index);
  PropertyConstness old_constness = old_details.constness();
  Representation old_representation = old_details.representation();
  Handle<FieldType> old_field_type(old_descriptors->GetFieldType(modify_index),
                                   isolate);
  CHECK_IMPLIES(IsClass(*old_field_type), old_representation.IsHeapObject());

  // Return if the current map is general enough to hold requested constness and
  // representation/field type.
  if (IsGeneralizableTo(new_constness, old_constness) &&
      old_representation.Equals(new_representation) &&
      FieldType::NowIs(*new_field_type, old_field_type)) {
    DCHECK(FieldType::NowIs(
        *GeneralizeFieldType(old_representation, old_field_type,
                             new_representation, new_field_type, isolate),
        old_field_type));
    return;
  }

  // Determine the field owner.
  DirectHandle<Map> field_owner(map->FindFieldOwner(isolate, modify_index),
                                isolate);
  DirectHandle<DescriptorArray> descriptors(
      field_owner->instance_descriptors(isolate), isolate);
  DCHECK_EQ(*old_field_type, descriptors->GetFieldType(modify_index));

  new_field_type =
      GeneralizeFieldType(old_representation, old_field_type,
                          new_representation, new_field_type, isolate);

  new_constness = GeneralizeConstness(old_constness, new_constness);

  PropertyDetails details = descriptors->GetDetails(modify_index);
  Handle<Name> name(descriptors->GetKey(modify_index), isolate);

  UpdateFieldType(isolate, field_owner, modify_index, name, new_constness,
                  new_representation, new_field_type);

  DCHECK_IMPLIES(IsClass(*new_field_type), new_representation.IsHeapObject());

  DependentCode::DependencyGroups dep_groups;
  if (new_constness != old_constness) {
    dep_groups |= DependentCode::kFieldConstGroup;
  }
  if (!FieldType::Equals(*new_field_type, *old_field_type)) {
    dep_groups |= DependentCode::kFieldTypeGroup;
  }
  if (!new_representation.Equals(old_representation)) {
    dep_groups |= DependentCode::kFieldRepresentationGroup;
  }

  DependentCode::DeoptimizeDependencyGroups(isolate, *field_owner, dep_groups);

  if (v8_flags.trace_generalization) {
    PrintGeneralization(
        isolate, map, stdout, "field type generalization", modify_index,
        map->NumberOfOwnDescriptors(), map->NumberOfOwnDescriptors(), false,
        details.representation(),
        descriptors->GetDetails(modify_index).representation(), old_constness,
        new_constness, old_field_type, MaybeHandle<Object>(), new_field_type,
        MaybeHandle<Object>());
  }
}

}  // namespace v8::internal
