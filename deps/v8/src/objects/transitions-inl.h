// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TRANSITIONS_INL_H_
#define V8_OBJECTS_TRANSITIONS_INL_H_

#include "src/objects/fixed-array-inl.h"
#include "src/objects/maybe-object-inl.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/objects/transitions.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// static
Tagged<TransitionArray> TransitionsAccessor::GetTransitionArray(
    Isolate* isolate, Tagged<MaybeObject> raw_transitions) {
  DCHECK_EQ(kFullTransitionArray, GetEncoding(isolate, raw_transitions));
  USE(isolate);
  return TransitionArray::cast(raw_transitions.GetHeapObjectAssumeStrong());
}

// static
Tagged<TransitionArray> TransitionsAccessor::GetTransitionArray(
    Isolate* isolate, Handle<Map> map) {
  Tagged<MaybeObject> raw_transitions =
      map->raw_transitions(isolate, kAcquireLoad);
  return GetTransitionArray(isolate, raw_transitions);
}

Tagged<TransitionArray> TransitionsAccessor::transitions() {
  return GetTransitionArray(isolate_, raw_transitions_);
}

OBJECT_CONSTRUCTORS_IMPL(TransitionArray, WeakFixedArray)

CAST_ACCESSOR(TransitionArray)

bool TransitionArray::HasPrototypeTransitions() {
  return get(kPrototypeTransitionsIndex) != Smi::zero();
}

Tagged<WeakFixedArray> TransitionArray::GetPrototypeTransitions() {
  DCHECK(HasPrototypeTransitions());  // Callers must check first.
  Tagged<Object> prototype_transitions =
      get(kPrototypeTransitionsIndex).GetHeapObjectAssumeStrong();
  return WeakFixedArray::cast(prototype_transitions);
}

HeapObjectSlot TransitionArray::GetKeySlot(int transition_number) {
  DCHECK(transition_number < number_of_transitions());
  return HeapObjectSlot(RawFieldOfElementAt(ToKeyIndex(transition_number)));
}

void TransitionArray::SetPrototypeTransitions(
    Tagged<WeakFixedArray> transitions) {
  DCHECK(IsWeakFixedArray(transitions));
  WeakFixedArray::set(kPrototypeTransitionsIndex, transitions);
}

int TransitionArray::NumberOfPrototypeTransitions(
    Tagged<WeakFixedArray> proto_transitions) {
  if (proto_transitions->length() == 0) return 0;
  Tagged<MaybeObject> raw =
      proto_transitions->get(kProtoTransitionNumberOfEntriesOffset);
  return raw.ToSmi().value();
}

Tagged<Name> TransitionArray::GetKey(int transition_number) {
  DCHECK(transition_number < number_of_transitions());
  return Name::cast(
      get(ToKeyIndex(transition_number)).GetHeapObjectAssumeStrong());
}

Tagged<Name> TransitionArray::GetKey(InternalIndex index) {
  return GetKey(index.as_int());
}

Tagged<Name> TransitionsAccessor::GetKey(int transition_number) {
  switch (encoding()) {
    case kPrototypeInfo:
    case kUninitialized:
    case kMigrationTarget:
      UNREACHABLE();
      return Tagged<Name>();
    case kWeakRef: {
      Tagged<Map> map = Map::cast(raw_transitions_.GetHeapObjectAssumeWeak());
      return GetSimpleTransitionKey(map);
    }
    case kFullTransitionArray:
      return transitions()->GetKey(transition_number);
  }
  UNREACHABLE();
}

void TransitionArray::SetKey(int transition_number, Tagged<Name> key) {
  DCHECK(transition_number < number_of_transitions());
  WeakFixedArray::set(ToKeyIndex(transition_number), key);
}

HeapObjectSlot TransitionArray::GetTargetSlot(int transition_number) {
  DCHECK(transition_number < number_of_transitions());
  return HeapObjectSlot(RawFieldOfElementAt(ToTargetIndex(transition_number)));
}

// static
PropertyDetails TransitionsAccessor::GetTargetDetails(Tagged<Name> name,
                                                      Tagged<Map> target) {
  DCHECK(!IsSpecialTransition(name->GetReadOnlyRoots(), name));
  InternalIndex descriptor = target->LastAdded();
  Tagged<DescriptorArray> descriptors =
      target->instance_descriptors(kRelaxedLoad);
  // Transitions are allowed only for the last added property.
  DCHECK(descriptors->GetKey(descriptor)->Equals(name));
  return descriptors->GetDetails(descriptor);
}

PropertyDetails TransitionsAccessor::GetSimpleTargetDetails(
    Tagged<Map> transition) {
  return transition->GetLastDescriptorDetails(isolate_);
}

// static
Tagged<Name> TransitionsAccessor::GetSimpleTransitionKey(
    Tagged<Map> transition) {
  InternalIndex descriptor = transition->LastAdded();
  return transition->instance_descriptors()->GetKey(descriptor);
}

// static
Tagged<Map> TransitionsAccessor::GetTargetFromRaw(Tagged<MaybeObject> raw) {
  return Map::cast(raw.GetHeapObjectAssumeWeak());
}

Tagged<MaybeObject> TransitionArray::GetRawTarget(int transition_number) {
  DCHECK(transition_number < number_of_transitions());
  return get(ToTargetIndex(transition_number));
}

Tagged<Map> TransitionArray::GetTarget(int transition_number) {
  Tagged<MaybeObject> raw = GetRawTarget(transition_number);
  return TransitionsAccessor::GetTargetFromRaw(raw);
}

Tagged<Map> TransitionsAccessor::GetTarget(int transition_number) {
  switch (encoding()) {
    case kPrototypeInfo:
    case kUninitialized:
    case kMigrationTarget:
      UNREACHABLE();
      return Map();
    case kWeakRef:
      return Map::cast(raw_transitions_.GetHeapObjectAssumeWeak());
    case kFullTransitionArray:
      return transitions()->GetTarget(transition_number);
  }
  UNREACHABLE();
}

void TransitionArray::SetRawTarget(int transition_number,
                                   Tagged<MaybeObject> value) {
  DCHECK(transition_number < number_of_transitions());
  DCHECK(value.IsWeak());
  DCHECK(IsMap(value.GetHeapObjectAssumeWeak()));
  WeakFixedArray::set(ToTargetIndex(transition_number), value);
}

bool TransitionArray::GetTargetIfExists(int transition_number, Isolate* isolate,
                                        Tagged<Map>* target) {
  Tagged<MaybeObject> raw = GetRawTarget(transition_number);
  Tagged<HeapObject> heap_object;
  // If the raw target is a Smi, then this TransitionArray is in the process of
  // being deserialized, and doesn't yet have an initialized entry for this
  // transition.
  if (raw.IsSmi()) {
    DCHECK(isolate->has_active_deserializer());
    DCHECK_EQ(raw.ToSmi(), Smi::uninitialized_deserialization_value());
    return false;
  }
  if (raw.GetHeapObjectIfStrong(&heap_object) &&
      IsUndefined(heap_object, isolate)) {
    return false;
  }
  *target = TransitionsAccessor::GetTargetFromRaw(raw);
  return true;
}

int TransitionArray::SearchNameForTesting(Tagged<Name> name,
                                          int* out_insertion_index) {
  return SearchName(name, out_insertion_index);
}

Tagged<Map> TransitionArray::SearchAndGetTargetForTesting(
    PropertyKind kind, Tagged<Name> name, PropertyAttributes attributes) {
  return SearchAndGetTarget(kind, name, attributes);
}

int TransitionArray::SearchSpecial(Tagged<Symbol> symbol,
                                   bool concurrent_search,
                                   int* out_insertion_index) {
  return SearchName(symbol, concurrent_search, out_insertion_index);
}

int TransitionArray::SearchName(Tagged<Name> name, bool concurrent_search,
                                int* out_insertion_index) {
  DCHECK(IsUniqueName(name));
  return internal::Search<ALL_ENTRIES>(this, name, number_of_entries(),
                                       out_insertion_index, concurrent_search);
}

TransitionsAccessor::TransitionsAccessor(Isolate* isolate, Tagged<Map> map,
                                         bool concurrent_access)
    : isolate_(isolate),
      map_(map),
      raw_transitions_(map->raw_transitions(isolate_, kAcquireLoad)),
      encoding_(GetEncoding(isolate_, raw_transitions_)),
      concurrent_access_(concurrent_access) {
  DCHECK_IMPLIES(encoding_ == kMigrationTarget, map_->is_deprecated());
}

int TransitionsAccessor::Capacity() { return transitions()->Capacity(); }

// static
TransitionsAccessor::Encoding TransitionsAccessor::GetEncoding(
    Isolate* isolate, Tagged<MaybeObject> raw_transitions) {
  Tagged<HeapObject> heap_object;
  if (raw_transitions.IsSmi() || raw_transitions.IsCleared()) {
    return kUninitialized;
  } else if (raw_transitions.IsWeak()) {
    return kWeakRef;
  } else if (raw_transitions.GetHeapObjectIfStrong(isolate, &heap_object)) {
    if (IsTransitionArray(heap_object)) {
      return kFullTransitionArray;
    } else if (IsPrototypeInfo(heap_object)) {
      return kPrototypeInfo;
    } else {
      DCHECK(IsMap(heap_object));
      return kMigrationTarget;
    }
  } else {
    UNREACHABLE();
  }
}

// static
TransitionsAccessor::Encoding TransitionsAccessor::GetEncoding(
    Isolate* isolate, Tagged<TransitionArray> array) {
  DCHECK_EQ(GetEncoding(isolate, Tagged<MaybeObject>(array)),
            kFullTransitionArray);
  return kFullTransitionArray;
}

// static
TransitionsAccessor::Encoding TransitionsAccessor::GetEncoding(
    Isolate* isolate, Handle<Map> map) {
  Tagged<MaybeObject> raw_transitions =
      map->raw_transitions(isolate, kAcquireLoad);
  return GetEncoding(isolate, raw_transitions);
}

// static
MaybeHandle<Map> TransitionsAccessor::SearchTransition(
    Isolate* isolate, Handle<Map> map, Tagged<Name> name, PropertyKind kind,
    PropertyAttributes attributes) {
  Tagged<Map> result = TransitionsAccessor(isolate, *map)
                           .SearchTransition(name, kind, attributes);
  if (result.is_null()) return MaybeHandle<Map>();
  return MaybeHandle<Map>(result, isolate);
}

// static
MaybeHandle<Map> TransitionsAccessor::SearchSpecial(Isolate* isolate,
                                                    Handle<Map> map,
                                                    Tagged<Symbol> name) {
  Tagged<Map> result = TransitionsAccessor(isolate, *map).SearchSpecial(name);
  if (result.is_null()) return MaybeHandle<Map>();
  return MaybeHandle<Map>(result, isolate);
}

int TransitionArray::number_of_transitions() const {
  if (length() < kFirstIndex) return 0;
  return get(kTransitionLengthIndex).ToSmi().value();
}

int TransitionArray::CompareKeys(Tagged<Name> key1, uint32_t hash1,
                                 PropertyKind kind1,
                                 PropertyAttributes attributes1,
                                 Tagged<Name> key2, uint32_t hash2,
                                 PropertyKind kind2,
                                 PropertyAttributes attributes2) {
  int cmp = CompareNames(key1, hash1, key2, hash2);
  if (cmp != 0) return cmp;

  return CompareDetails(kind1, attributes1, kind2, attributes2);
}

int TransitionArray::CompareNames(Tagged<Name> key1, uint32_t hash1,
                                  Tagged<Name> key2, uint32_t hash2) {
  if (key1 != key2) {
    // In case of hash collisions key1 is always "less" than key2.
    return hash1 <= hash2 ? -1 : 1;
  }

  return 0;
}

int TransitionArray::CompareDetails(PropertyKind kind1,
                                    PropertyAttributes attributes1,
                                    PropertyKind kind2,
                                    PropertyAttributes attributes2) {
  if (kind1 != kind2) {
    return static_cast<int>(kind1) < static_cast<int>(kind2) ? -1 : 1;
  }

  if (attributes1 != attributes2) {
    return static_cast<int>(attributes1) < static_cast<int>(attributes2) ? -1
                                                                         : 1;
  }

  return 0;
}

void TransitionArray::Set(int transition_number, Tagged<Name> key,
                          Tagged<MaybeObject> target) {
  WeakFixedArray::set(ToKeyIndex(transition_number), key);
  WeakFixedArray::set(ToTargetIndex(transition_number), target);
}

Tagged<Name> TransitionArray::GetSortedKey(int transition_number) {
  return GetKey(transition_number);
}

int TransitionArray::number_of_entries() const {
  return number_of_transitions();
}

int TransitionArray::Capacity() {
  if (length() <= kFirstIndex) return 0;
  return (length() - kFirstIndex) / kEntrySize;
}

void TransitionArray::SetNumberOfTransitions(int number_of_transitions) {
  DCHECK(number_of_transitions <= Capacity());
  WeakFixedArray::set(kTransitionLengthIndex,
                      Smi::FromInt(number_of_transitions));
}

Handle<String> TransitionsAccessor::ExpectedTransitionKey() {
  DisallowGarbageCollection no_gc;
  switch (encoding()) {
    case kPrototypeInfo:
    case kUninitialized:
    case kMigrationTarget:
    case kFullTransitionArray:
      return Handle<String>::null();
    case kWeakRef: {
      Tagged<Map> target =
          Map::cast(raw_transitions_.GetHeapObjectAssumeWeak());
      PropertyDetails details = GetSimpleTargetDetails(target);
      if (details.location() != PropertyLocation::kField)
        return Handle<String>::null();
      DCHECK_EQ(PropertyKind::kData, details.kind());
      if (details.attributes() != NONE) return Handle<String>::null();
      Tagged<Name> name = GetSimpleTransitionKey(target);
      if (!IsString(name)) return Handle<String>::null();
      return handle(String::cast(name), isolate_);
    }
  }
  UNREACHABLE();
}

Handle<Map> TransitionsAccessor::ExpectedTransitionTarget() {
  DCHECK(!ExpectedTransitionKey().is_null());
  return handle(GetTarget(0), isolate_);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TRANSITIONS_INL_H_
