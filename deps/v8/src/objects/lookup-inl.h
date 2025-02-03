// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_LOOKUP_INL_H_
#define V8_OBJECTS_LOOKUP_INL_H_

#include "src/objects/lookup.h"

// Include other inline headers *after* including lookup.h, such that e.g. the
// definition of LookupIterator is available (and this comment prevents
// clang-format from merging that include into the following ones).
#include "src/handles/handles-inl.h"
#include "src/heap/factory-inl.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/internal-index.h"
#include "src/objects/map-inl.h"
#include "src/objects/name-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

LookupIterator::LookupIterator(Isolate* isolate, Handle<Object> receiver,
                               Handle<Name> name, Configuration configuration)
    : LookupIterator(isolate, Cast<JSAny>(receiver), name, kInvalidIndex,
                     Cast<JSAny>(receiver), configuration) {}

LookupIterator::LookupIterator(Isolate* isolate, Handle<Object> receiver,
                               Handle<Name> name,
                               Handle<Object> lookup_start_object,
                               Configuration configuration)
    : LookupIterator(isolate, Cast<JSAny>(receiver), name, kInvalidIndex,
                     Cast<JSAny>(lookup_start_object), configuration) {}

LookupIterator::LookupIterator(Isolate* isolate, Handle<Object> receiver,
                               size_t index, Configuration configuration)
    : LookupIterator(isolate, Cast<JSAny>(receiver), Handle<Name>(), index,
                     Cast<JSAny>(receiver), configuration) {
  DCHECK_NE(index, kInvalidIndex);
}

LookupIterator::LookupIterator(Isolate* isolate, Handle<Object> receiver,
                               size_t index, Handle<Object> lookup_start_object,
                               Configuration configuration)
    : LookupIterator(isolate, Cast<JSAny>(receiver), Handle<Name>(), index,
                     Cast<JSAny>(lookup_start_object), configuration) {
  DCHECK_NE(index, kInvalidIndex);
}

LookupIterator::LookupIterator(Isolate* isolate, Handle<Object> receiver,
                               const PropertyKey& key,
                               Configuration configuration)
    : LookupIterator(isolate, Cast<JSAny>(receiver), key.name(), key.index(),
                     Cast<JSAny>(receiver), configuration) {}

LookupIterator::LookupIterator(Isolate* isolate, Handle<Object> receiver,
                               const PropertyKey& key,
                               Handle<Object> lookup_start_object,
                               Configuration configuration)
    : LookupIterator(isolate, Cast<JSAny>(receiver), key.name(), key.index(),
                     Cast<JSAny>(lookup_start_object), configuration) {}

// This private constructor is the central bottleneck that all the other
// constructors use.
LookupIterator::LookupIterator(Isolate* isolate, Handle<JSAny> receiver,
                               Handle<Name> name, size_t index,
                               Handle<JSAny> lookup_start_object,
                               Configuration configuration)
    : configuration_(ComputeConfiguration(isolate, configuration, name)),
      isolate_(isolate),
      name_(name),
      receiver_(receiver),
      lookup_start_object_(lookup_start_object),
      index_(index) {
  if (IsElement()) {
    // If we're not looking at a TypedArray, we will need the key represented
    // as an internalized string.
    if (index_ > JSObject::kMaxElementIndex &&
        !IsJSTypedArray(*lookup_start_object, isolate_)
#if V8_ENABLE_WEBASSEMBLY
        && !IsWasmArray(*lookup_start_object, isolate_)
#endif  // V8_ENABLE_WEBASSEMBLY
    ) {
      if (name_.is_null()) {
        name_ = isolate->factory()->SizeToString(index_);
      }
      name_ = isolate->factory()->InternalizeName(name_);
    } else if (!name_.is_null() && !IsInternalizedString(*name_)) {
      // Maintain the invariant that if name_ is present, it is internalized.
      name_ = Handle<Name>();
    }
    Start<true>();
  } else {
    DCHECK(!name_.is_null());
    name_ = isolate->factory()->InternalizeName(name_);
#ifdef DEBUG
    // Assert that the name is not an index.
    // If we're not looking at the prototype chain and the lookup start object
    // is not a typed array, then this means "array index", otherwise we need to
    // ensure the full generality so that typed arrays are handled correctly.
    if (!check_prototype_chain() && !IsJSTypedArray(*lookup_start_object)) {
      uint32_t array_index;
      DCHECK(!name_->AsArrayIndex(&array_index));
    } else {
      size_t integer_index;
      DCHECK(!name_->AsIntegerIndex(&integer_index));
    }
#endif  // DEBUG
    Start<false>();
  }
}

LookupIterator::LookupIterator(Isolate* isolate, Configuration configuration,
                               Handle<Object> receiver, Handle<Symbol> name)
    : configuration_(configuration),
      isolate_(isolate),
      name_(name),
      receiver_(Cast<JSAny>(receiver)),
      lookup_start_object_(Cast<JSAny>(receiver)),
      index_(kInvalidIndex) {
  // This is the only lookup configuration allowed by this constructor because
  // it's special case allowing lookup of the private symbols on the prototype
  // chain. Usually private symbols are limited to OWN_SKIP_INTERCEPTOR lookups.
  DCHECK(*name_ == *isolate->factory()->error_stack_symbol() ||
         *name_ == *isolate->factory()->error_message_symbol());
  DCHECK_EQ(configuration, PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  Start<false>();
}

PropertyKey::PropertyKey(Isolate* isolate, double index) {
  DCHECK_EQ(index, static_cast<uint64_t>(index));
#if V8_TARGET_ARCH_32_BIT
  if (index <= JSObject::kMaxElementIndex) {
    static_assert(JSObject::kMaxElementIndex <=
                  std::numeric_limits<size_t>::max());
    index_ = static_cast<size_t>(index);
  } else {
    index_ = LookupIterator::kInvalidIndex;
    name_ = isolate->factory()->InternalizeString(
        isolate->factory()->HeapNumberToString(
            isolate->factory()->NewHeapNumber(index), index));
  }
#else
  index_ = static_cast<size_t>(index);
#endif
}

PropertyKey::PropertyKey(Isolate* isolate, Handle<Name> name, size_t index)
    : name_(name), index_(index) {
  DCHECK_IMPLIES(index_ == LookupIterator::kInvalidIndex, !name_.is_null());
#if V8_TARGET_ARCH_32_BIT
  DCHECK_IMPLIES(index_ != LookupIterator::kInvalidIndex,
                 index_ <= JSObject::kMaxElementIndex);
#endif
#if DEBUG
  if (index_ != LookupIterator::kInvalidIndex && !name_.is_null()) {
    // If both valid index and name are given then the name is a string
    // representation of the same index.
    size_t integer_index;
    CHECK(name_->AsIntegerIndex(&integer_index));
    CHECK_EQ(index_, integer_index);
  } else if (index_ == LookupIterator::kInvalidIndex) {
    // If only name is given it must not be a string representing an integer
    // index.
    size_t integer_index;
    CHECK(!name_->AsIntegerIndex(&integer_index));
  }
#endif
}

PropertyKey::PropertyKey(Isolate* isolate, Handle<Name> name) {
  if (name->AsIntegerIndex(&index_)) {
    name_ = name;
  } else {
    index_ = LookupIterator::kInvalidIndex;
    name_ = isolate->factory()->InternalizeName(name);
  }
}

PropertyKey::PropertyKey(Isolate* isolate, Handle<Object> valid_key) {
  DCHECK(IsName(*valid_key) || IsNumber(*valid_key));
  if (Object::ToIntegerIndex(*valid_key, &index_)) return;
  if (IsNumber(*valid_key)) {
    // Negative or out of range -> treat as named property.
    valid_key = isolate->factory()->NumberToString(valid_key);
  }
  DCHECK(IsName(*valid_key));
  name_ = Cast<Name>(valid_key);
  if (!name_->AsIntegerIndex(&index_)) {
    index_ = LookupIterator::kInvalidIndex;
    name_ = isolate->factory()->InternalizeName(name_);
  }
}

bool PropertyKey::is_element() const {
  return index_ != LookupIterator::kInvalidIndex;
}

Handle<Name> PropertyKey::GetName(Isolate* isolate) {
  if (name_.is_null()) {
    DCHECK(is_element());
    name_ = isolate->factory()->SizeToString(index_);
  }
  return name_;
}

Handle<Name> LookupIterator::name() const {
  DCHECK_IMPLIES(!holder_.is_null(), !IsElement(*holder_));
  return name_;
}

Handle<Name> LookupIterator::GetName() {
  if (name_.is_null()) {
    DCHECK(IsElement());
    name_ = factory()->SizeToString(index_);
  }
  return name_;
}

PropertyKey LookupIterator::GetKey() const {
  return PropertyKey(isolate_, name_, index_);
}

bool LookupIterator::IsElement(Tagged<JSReceiver> object) const {
  return index_ <= JSObject::kMaxElementIndex ||
         (index_ != kInvalidIndex &&
          object->map()->has_any_typed_array_or_wasm_array_elements());
}

bool LookupIterator::IsPrivateName() const {
  return !IsElement() && name()->IsPrivateName();
}

bool LookupIterator::is_dictionary_holder() const {
  return !holder_->HasFastProperties(isolate_);
}

Handle<Map> LookupIterator::transition_map() const {
  DCHECK_EQ(TRANSITION, state_);
  return Cast<Map>(transition_);
}

Handle<PropertyCell> LookupIterator::transition_cell() const {
  DCHECK_EQ(TRANSITION, state_);
  return Cast<PropertyCell>(transition_);
}

template <class T>
Handle<T> LookupIterator::GetHolder() const {
  DCHECK(IsFound());
  return Cast<T>(holder_);
}

bool LookupIterator::ExtendingNonExtensible(Handle<JSReceiver> receiver) {
  DCHECK(receiver.is_identical_to(GetStoreTarget<JSReceiver>()));
  // Shared objects have fixed layout. No properties may be added to them, not
  // even private symbols.
  return !receiver->map(isolate_)->is_extensible() &&
         (IsElement() ||
          (!name_->IsPrivate() || IsAlwaysSharedSpaceJSObject(*receiver)));
}

bool LookupIterator::IsCacheableTransition() {
  DCHECK_EQ(TRANSITION, state_);
  return IsPropertyCell(*transition_, isolate_) ||
         (transition_map()->is_dictionary_map() &&
          !GetStoreTarget<JSReceiver>()->HasFastProperties(isolate_)) ||
         IsMap(transition_map()->GetBackPointer(isolate_), isolate_);
}

// static
void LookupIterator::UpdateProtector(Isolate* isolate, Handle<Object> receiver,
                                     DirectHandle<Name> name) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kUpdateProtector);
  DCHECK(IsInternalizedString(*name) || IsSymbol(*name));

  // This check must be kept in sync with
  // CodeStubAssembler::CheckForAssociatedProtector!
  ReadOnlyRoots roots(isolate);
  bool maybe_protector = roots.IsNameForProtector(*name);

#if DEBUG
  bool debug_maybe_protector =
      *name == roots.constructor_string() || *name == roots.next_string() ||
      *name == roots.resolve_string() || *name == roots.then_string() ||
      *name == roots.is_concat_spreadable_symbol() ||
      *name == roots.iterator_symbol() || *name == roots.species_symbol() ||
      *name == roots.match_all_symbol() || *name == roots.replace_symbol() ||
      *name == roots.split_symbol() || *name == roots.to_primitive_symbol() ||
      *name == roots.valueOf_string();
  DCHECK_EQ(maybe_protector, debug_maybe_protector);
#endif  // DEBUG

  if (maybe_protector) {
    InternalUpdateProtector(isolate, receiver, name);
  }
}

void LookupIterator::UpdateProtector() {
  if (IsElement()) return;
  UpdateProtector(isolate_, receiver_, name_);
}

InternalIndex LookupIterator::descriptor_number() const {
  DCHECK(!holder_.is_null());
  DCHECK(!IsElement(*holder_));
  DCHECK(has_property_);
  DCHECK(holder_->HasFastProperties(isolate_));
  return number_;
}

InternalIndex LookupIterator::dictionary_entry() const {
  DCHECK(!holder_.is_null());
  DCHECK(!IsElement(*holder_));
  DCHECK(has_property_);
  DCHECK(!holder_->HasFastProperties(isolate_));
  return number_;
}

// static
LookupIterator::Configuration LookupIterator::ComputeConfiguration(
    Isolate* isolate, Configuration configuration, Handle<Name> name) {
  return (!name.is_null() && name->IsPrivate()) ? OWN_SKIP_INTERCEPTOR
                                                : configuration;
}

// static
MaybeHandle<JSReceiver> LookupIterator::GetRoot(
    Isolate* isolate, Handle<JSAny> lookup_start_object, size_t index,
    Configuration configuration) {
  if (IsJSReceiver(*lookup_start_object, isolate)) {
    return Cast<JSReceiver>(lookup_start_object);
  }
  return GetRootForNonJSReceiver(
      isolate, Cast<JSPrimitive>(lookup_start_object), index, configuration);
}

template <class T>
Handle<T> LookupIterator::GetStoreTarget() const {
  DCHECK(IsJSReceiver(*receiver_, isolate_));
  if (IsJSGlobalProxy(*receiver_, isolate_)) {
    Tagged<HeapObject> prototype =
        Cast<JSGlobalProxy>(*receiver_)->map(isolate_)->prototype(isolate_);
    if (IsJSGlobalObject(prototype, isolate_)) {
      return handle(Cast<JSGlobalObject>(prototype), isolate_);
    }
  }
  return Cast<T>(receiver_);
}

template <bool is_element>
Tagged<InterceptorInfo> LookupIterator::GetInterceptor(
    Tagged<JSObject> holder) const {
  if (is_element && index_ <= JSObject::kMaxElementIndex) {
    return holder->GetIndexedInterceptor(isolate_);
  } else {
    return holder->GetNamedInterceptor(isolate_);
  }
}

inline Handle<InterceptorInfo> LookupIterator::GetInterceptor() const {
  DCHECK_EQ(INTERCEPTOR, state_);
  Tagged<JSObject> holder = Cast<JSObject>(*holder_);
  Tagged<InterceptorInfo> result = IsElement(holder)
                                       ? GetInterceptor<true>(holder)
                                       : GetInterceptor<false>(holder);
  return handle(result, isolate_);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_LOOKUP_INL_H_
