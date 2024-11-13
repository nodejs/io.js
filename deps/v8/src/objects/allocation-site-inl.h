// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ALLOCATION_SITE_INL_H_
#define V8_OBJECTS_ALLOCATION_SITE_INL_H_

#include "src/common/globals.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/allocation-site.h"
#include "src/objects/dependent-code-inl.h"
#include "src/objects/js-objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/allocation-site-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(AllocationMemento)
OBJECT_CONSTRUCTORS_IMPL(AllocationSite, Struct)

NEVER_READ_ONLY_SPACE_IMPL(AllocationSite)

ACCESSORS(AllocationSite, transition_info_or_boilerplate, Tagged<Object>,
          kTransitionInfoOrBoilerplateOffset)
RELEASE_ACQUIRE_ACCESSORS(AllocationSite, transition_info_or_boilerplate,
                          Tagged<Object>, kTransitionInfoOrBoilerplateOffset)
ACCESSORS(AllocationSite, nested_site, Tagged<Object>, kNestedSiteOffset)
RELAXED_INT32_ACCESSORS(AllocationSite, pretenure_data, kPretenureDataOffset)
INT32_ACCESSORS(AllocationSite, pretenure_create_count,
                kPretenureCreateCountOffset)
ACCESSORS(AllocationSite, dependent_code, Tagged<DependentCode>,
          kDependentCodeOffset)
ACCESSORS_CHECKED(AllocationSite, weak_next, Tagged<Object>, kWeakNextOffset,
                  HasWeakNext())
ACCESSORS(AllocationMemento, allocation_site, Tagged<Object>,
          kAllocationSiteOffset)

Tagged<JSObject> AllocationSite::boilerplate() const {
  DCHECK(PointsToLiteral());
  return Cast<JSObject>(transition_info_or_boilerplate());
}

Tagged<JSObject> AllocationSite::boilerplate(AcquireLoadTag tag) const {
  DCHECK(PointsToLiteral());
  return Cast<JSObject>(transition_info_or_boilerplate(tag));
}

void AllocationSite::set_boilerplate(Tagged<JSObject> value,
                                     ReleaseStoreTag tag,
                                     WriteBarrierMode mode) {
  set_transition_info_or_boilerplate(value, tag, mode);
}

int AllocationSite::transition_info() const {
  DCHECK(!PointsToLiteral());
  return Cast<Smi>(transition_info_or_boilerplate(kAcquireLoad)).value();
}

void AllocationSite::set_transition_info(int value) {
  DCHECK(!PointsToLiteral());
  set_transition_info_or_boilerplate(Smi::FromInt(value), kReleaseStore,
                                     SKIP_WRITE_BARRIER);
}

bool AllocationSite::HasWeakNext() const {
  return map() == GetReadOnlyRoots().allocation_site_map();
}

void AllocationSite::Initialize() {
  set_transition_info_or_boilerplate(Smi::zero());
  SetElementsKind(GetInitialFastElementsKind());
  set_nested_site(Smi::zero());
  set_pretenure_data(0, kRelaxedStore);
  set_pretenure_create_count(0);
  set_dependent_code(DependentCode::empty_dependent_code(GetReadOnlyRoots()),
                     SKIP_WRITE_BARRIER);
}

bool AllocationSite::IsZombie() const {
  return pretenure_decision() == kZombie;
}

bool AllocationSite::IsMaybeTenure() const {
  return pretenure_decision() == kMaybeTenure;
}

bool AllocationSite::PretenuringDecisionMade() const {
  return pretenure_decision() != kUndecided;
}

void AllocationSite::MarkZombie() {
  DCHECK(!IsZombie());
  Initialize();
  set_pretenure_decision(kZombie);
}

ElementsKind AllocationSite::GetElementsKind() const {
  return ElementsKindBits::decode(transition_info());
}

void AllocationSite::SetElementsKind(ElementsKind kind) {
  set_transition_info(ElementsKindBits::update(transition_info(), kind));
}

bool AllocationSite::CanInlineCall() const {
  return DoNotInlineBit::decode(transition_info()) == 0;
}

void AllocationSite::SetDoNotInlineCall() {
  set_transition_info(DoNotInlineBit::update(transition_info(), true));
}

bool AllocationSite::PointsToLiteral() const {
  Tagged<Object> raw_value = transition_info_or_boilerplate(kAcquireLoad);
  DCHECK_EQ(!IsSmi(raw_value), IsJSArray(raw_value) || IsJSObject(raw_value));
  return !IsSmi(raw_value);
}

// Heuristic: We only need to create allocation site info if the boilerplate
// elements kind is the initial elements kind.
bool AllocationSite::ShouldTrack(ElementsKind boilerplate_elements_kind) {
  if (!V8_ALLOCATION_SITE_TRACKING_BOOL) return false;
  return IsSmiElementsKind(boilerplate_elements_kind);
}

inline bool AllocationSite::CanTrack(InstanceType type) {
  if (!V8_ALLOCATION_SITE_TRACKING_BOOL) return false;
  if (v8_flags.allocation_site_pretenuring) {
    // TurboFan doesn't care at all about String pretenuring feedback,
    // so don't bother even trying to track that.
    return type == JS_ARRAY_TYPE || type == JS_OBJECT_TYPE;
  }
  return type == JS_ARRAY_TYPE;
}

AllocationSite::PretenureDecision AllocationSite::pretenure_decision() const {
  return PretenureDecisionBits::decode(pretenure_data(kRelaxedLoad));
}

void AllocationSite::set_pretenure_decision(PretenureDecision decision) {
  int32_t value = pretenure_data(kRelaxedLoad);
  set_pretenure_data(PretenureDecisionBits::update(value, decision),
                     kRelaxedStore);
}

bool AllocationSite::deopt_dependent_code() const {
  return DeoptDependentCodeBit::decode(pretenure_data(kRelaxedLoad));
}

void AllocationSite::set_deopt_dependent_code(bool deopt) {
  int32_t value = pretenure_data(kRelaxedLoad);
  set_pretenure_data(DeoptDependentCodeBit::update(value, deopt),
                     kRelaxedStore);
}

int AllocationSite::memento_found_count() const {
  return MementoFoundCountBits::decode(pretenure_data(kRelaxedLoad));
}

inline void AllocationSite::set_memento_found_count(int count) {
  int32_t value = pretenure_data(kRelaxedLoad);
  // Verify that we can count more mementos than we can possibly find in one
  // new space collection.
  DCHECK((GetHeap()->MaxSemiSpaceSize() /
          (Heap::kMinObjectSizeInTaggedWords * kTaggedSize +
           AllocationMemento::kSize)) < MementoFoundCountBits::kMax);
  DCHECK_LT(count, MementoFoundCountBits::kMax);
  set_pretenure_data(MementoFoundCountBits::update(value, count),
                     kRelaxedStore);
}

int AllocationSite::memento_create_count() const {
  return pretenure_create_count();
}

void AllocationSite::set_memento_create_count(int count) {
  set_pretenure_create_count(count);
}

int AllocationSite::IncrementMementoFoundCount(int increment) {
  DCHECK(!IsZombie());

  int new_value = memento_found_count() + increment;
  set_memento_found_count(new_value);
  return new_value;
}

inline void AllocationSite::IncrementMementoCreateCount() {
  DCHECK(v8_flags.allocation_site_pretenuring);
  int value = memento_create_count();
  set_memento_create_count(value + 1);
}

bool AllocationMemento::IsValid() const {
  return IsAllocationSite(allocation_site()) &&
         !Cast<AllocationSite>(allocation_site())->IsZombie();
}

Tagged<AllocationSite> AllocationMemento::GetAllocationSite() const {
  DCHECK(IsValid());
  return Cast<AllocationSite>(allocation_site());
}

Address AllocationMemento::GetAllocationSiteUnchecked() const {
  return allocation_site().ptr();
}

template <AllocationSiteUpdateMode update_or_check>
bool AllocationSite::DigestTransitionFeedback(DirectHandle<AllocationSite> site,
                                              ElementsKind to_kind) {
  Isolate* isolate = site->GetIsolate();
  bool result = false;

  if (site->PointsToLiteral() && IsJSArray(site->boilerplate())) {
    Handle<JSArray> boilerplate(Cast<JSArray>(site->boilerplate()), isolate);
    ElementsKind kind = boilerplate->GetElementsKind();
    // if kind is holey ensure that to_kind is as well.
    if (IsHoleyElementsKind(kind)) {
      to_kind = GetHoleyElementsKind(to_kind);
    }
    if (IsMoreGeneralElementsKindTransition(kind, to_kind)) {
      // If the array is huge, it's not likely to be defined in a local
      // function, so we shouldn't make new instances of it very often.
      uint32_t length = 0;
      CHECK(Object::ToArrayLength(boilerplate->length(), &length));
      if (length <= kMaximumArrayBytesToPretransition) {
        if (update_or_check == AllocationSiteUpdateMode::kCheckOnly) {
          return true;
        }
        if (v8_flags.trace_track_allocation_sites) {
          bool is_nested = site->IsNested();
          PrintF("AllocationSite: JSArray %p boilerplate %supdated %s->%s\n",
                 reinterpret_cast<void*>(site->ptr()),
                 is_nested ? "(nested)" : " ", ElementsKindToString(kind),
                 ElementsKindToString(to_kind));
        }
        CHECK_NE(to_kind, DICTIONARY_ELEMENTS);
        JSObject::TransitionElementsKind(boilerplate, to_kind);
        DependentCode::DeoptimizeDependencyGroups(
            isolate, *site,
            DependentCode::kAllocationSiteTransitionChangedGroup);
        result = true;
      }
    }
  } else {
    // The AllocationSite is for a constructed Array.
    ElementsKind kind = site->GetElementsKind();
    // if kind is holey ensure that to_kind is as well.
    if (IsHoleyElementsKind(kind)) {
      to_kind = GetHoleyElementsKind(to_kind);
    }
    if (IsMoreGeneralElementsKindTransition(kind, to_kind)) {
      if (update_or_check == AllocationSiteUpdateMode::kCheckOnly) return true;
      if (v8_flags.trace_track_allocation_sites) {
        PrintF("AllocationSite: JSArray %p site updated %s->%s\n",
               reinterpret_cast<void*>(site->ptr()), ElementsKindToString(kind),
               ElementsKindToString(to_kind));
      }
      site->SetElementsKind(to_kind);
      DependentCode::DeoptimizeDependencyGroups(
          isolate, *site, DependentCode::kAllocationSiteTransitionChangedGroup);
      result = true;
    }
  }
  return result;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ALLOCATION_SITE_INL_H_
