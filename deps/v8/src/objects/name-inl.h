// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_NAME_INL_H_
#define V8_OBJECTS_NAME_INL_H_

#include "src/base/logging.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/name.h"
#include "src/objects/primitive-heap-object-inl.h"
#include "src/objects/string-forwarding-table.h"
#include "src/objects/string-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

Tagged<PrimitiveHeapObject> Symbol::description() const {
  return description_.load();
}
void Symbol::set_description(Tagged<PrimitiveHeapObject> value,
                             WriteBarrierMode mode) {
  SLOW_DCHECK(IsString(value) || IsUndefined(value));
  description_.store(this, value, mode);
}

BIT_FIELD_ACCESSORS(Symbol, flags, is_private, Symbol::IsPrivateBit)
BIT_FIELD_ACCESSORS(Symbol, flags, is_well_known_symbol,
                    Symbol::IsWellKnownSymbolBit)
BIT_FIELD_ACCESSORS(Symbol, flags, is_in_public_symbol_table,
                    Symbol::IsInPublicSymbolTableBit)
BIT_FIELD_ACCESSORS(Symbol, flags, is_interesting_symbol,
                    Symbol::IsInterestingSymbolBit)

bool Symbol::is_private_brand() const {
  bool value = Symbol::IsPrivateBrandBit::decode(flags());
  DCHECK_IMPLIES(value, is_private());
  return value;
}

void Symbol::set_is_private_brand() {
  set_flags(Symbol::IsPrivateBit::update(flags(), true));
  set_flags(Symbol::IsPrivateNameBit::update(flags(), true));
  set_flags(Symbol::IsPrivateBrandBit::update(flags(), true));
}

bool Symbol::is_private_name() const {
  bool value = Symbol::IsPrivateNameBit::decode(flags());
  DCHECK_IMPLIES(value, is_private());
  return value;
}

void Symbol::set_is_private_name() {
  // TODO(gsathya): Re-order the bits to have these next to each other
  // and just do the bit shifts once.
  set_flags(Symbol::IsPrivateBit::update(flags(), true));
  set_flags(Symbol::IsPrivateNameBit::update(flags(), true));
}

DEF_HEAP_OBJECT_PREDICATE(Name, IsUniqueName) {
  uint32_t type = obj->map()->instance_type();
  bool result = (type & (kIsNotStringMask | kIsNotInternalizedMask)) !=
                (kStringTag | kNotInternalizedTag);
  SLOW_DCHECK(result == IsUniqueName(Cast<HeapObject>(obj)));
  DCHECK_IMPLIES(result, obj->HasHashCode());
  return result;
}

bool Name::Equals(Tagged<Name> other) {
  if (other == this) return true;
  if ((IsInternalizedString(this) && IsInternalizedString(other)) ||
      IsSymbol(this) || IsSymbol(other)) {
    return false;
  }
  return Cast<String>(this)->SlowEquals(Cast<String>(other));
}

bool Name::Equals(Isolate* isolate, Handle<Name> one, Handle<Name> two) {
  if (one.is_identical_to(two)) return true;
  if ((IsInternalizedString(*one) && IsInternalizedString(*two)) ||
      IsSymbol(*one) || IsSymbol(*two)) {
    return false;
  }
  return String::SlowEquals(isolate, Cast<String>(one), Cast<String>(two));
}

// static
bool Name::IsHashFieldComputed(uint32_t raw_hash_field) {
  return (raw_hash_field & kHashNotComputedMask) == 0;
}

// static
bool Name::IsHash(uint32_t raw_hash_field) {
  return HashFieldTypeBits::decode(raw_hash_field) == HashFieldType::kHash;
}

// static
bool Name::IsIntegerIndex(uint32_t raw_hash_field) {
  return HashFieldTypeBits::decode(raw_hash_field) ==
         HashFieldType::kIntegerIndex;
}

// static
bool Name::IsForwardingIndex(uint32_t raw_hash_field) {
  return HashFieldTypeBits::decode(raw_hash_field) ==
         HashFieldType::kForwardingIndex;
}

// static
bool Name::IsInternalizedForwardingIndex(uint32_t raw_hash_field) {
  return HashFieldTypeBits::decode(raw_hash_field) ==
             HashFieldType::kForwardingIndex &&
         IsInternalizedForwardingIndexBit::decode(raw_hash_field);
}

// static
bool Name::IsExternalForwardingIndex(uint32_t raw_hash_field) {
  return HashFieldTypeBits::decode(raw_hash_field) ==
             HashFieldType::kForwardingIndex &&
         IsExternalForwardingIndexBit::decode(raw_hash_field);
}

// static
uint32_t Name::CreateHashFieldValue(uint32_t hash, HashFieldType type) {
  DCHECK_NE(type, HashFieldType::kForwardingIndex);
  return HashBits::encode(hash & HashBits::kMax) |
         HashFieldTypeBits::encode(type);
}
// static
uint32_t Name::CreateInternalizedForwardingIndex(uint32_t index) {
  return ForwardingIndexValueBits::encode(index) |
         IsExternalForwardingIndexBit::encode(false) |
         IsInternalizedForwardingIndexBit::encode(true) |
         HashFieldTypeBits::encode(HashFieldType::kForwardingIndex);
}

// static
uint32_t Name::CreateExternalForwardingIndex(uint32_t index) {
  return ForwardingIndexValueBits::encode(index) |
         IsExternalForwardingIndexBit::encode(true) |
         IsInternalizedForwardingIndexBit::encode(false) |
         HashFieldTypeBits::encode(HashFieldType::kForwardingIndex);
}

bool Name::HasHashCode() const {
  uint32_t field = raw_hash_field();
  return IsHashFieldComputed(field) || IsForwardingIndex(field);
}
bool Name::HasForwardingIndex(AcquireLoadTag) const {
  return IsForwardingIndex(raw_hash_field(kAcquireLoad));
}
bool Name::HasInternalizedForwardingIndex(AcquireLoadTag) const {
  return IsInternalizedForwardingIndex(raw_hash_field(kAcquireLoad));
}
bool Name::HasExternalForwardingIndex(AcquireLoadTag) const {
  return IsExternalForwardingIndex(raw_hash_field(kAcquireLoad));
}

uint32_t Name::GetRawHashFromForwardingTable(uint32_t raw_hash) const {
  DCHECK(IsForwardingIndex(raw_hash));
  // TODO(pthier): Add parameter for isolate so we don't need to calculate it.
  Isolate* isolate = GetIsolateFromWritableObject(this);
  const int index = ForwardingIndexValueBits::decode(raw_hash);
  return isolate->string_forwarding_table()->GetRawHash(isolate, index);
}

uint32_t Name::EnsureRawHash() {
  // Fast case: has hash code already been computed?
  uint32_t field = raw_hash_field(kAcquireLoad);
  if (IsHashFieldComputed(field)) return field;
  // The computed hash might be stored in the forwarding table.
  if (V8_UNLIKELY(IsForwardingIndex(field))) {
    return GetRawHashFromForwardingTable(field);
  }
  // Slow case: compute hash code and set it. Has to be a string.
  return Cast<String>(this)->ComputeAndSetRawHash();
}

uint32_t Name::EnsureRawHash(
    const SharedStringAccessGuardIfNeeded& access_guard) {
  // Fast case: has hash code already been computed?
  uint32_t field = raw_hash_field(kAcquireLoad);
  if (IsHashFieldComputed(field)) return field;
  // The computed hash might be stored in the forwarding table.
  if (V8_UNLIKELY(IsForwardingIndex(field))) {
    return GetRawHashFromForwardingTable(field);
  }
  // Slow case: compute hash code and set it. Has to be a string.
  return Cast<String>(this)->ComputeAndSetRawHash(access_guard);
}

uint32_t Name::RawHash() {
  uint32_t field = raw_hash_field(kAcquireLoad);
  if (V8_UNLIKELY(IsForwardingIndex(field))) {
    return GetRawHashFromForwardingTable(field);
  }
  return field;
}

uint32_t Name::EnsureHash() { return HashBits::decode(EnsureRawHash()); }

uint32_t Name::EnsureHash(const SharedStringAccessGuardIfNeeded& access_guard) {
  return HashBits::decode(EnsureRawHash(access_guard));
}

void Name::set_raw_hash_field_if_empty(uint32_t hash) {
  uint32_t field_value = kEmptyHashField;
  bool result = raw_hash_field_.compare_exchange_strong(field_value, hash);
  USE(result);
  // CAS can only fail if the string is shared or we use the forwarding table
  // for all strings and the hash was already set (by another thread) or it is
  // a forwarding index (that overwrites the previous hash).
  // In all cases we don't want overwrite the old value, so we don't handle the
  // failure case.
  DCHECK_IMPLIES(!result, (Cast<String>(this)->IsShared() ||
                           v8_flags.always_use_string_forwarding_table) &&
                              (field_value == hash || IsForwardingIndex(hash)));
}

uint32_t Name::hash() const {
  uint32_t field = raw_hash_field(kAcquireLoad);
  if (V8_UNLIKELY(!IsHashFieldComputed(field))) {
    DCHECK(IsForwardingIndex(field));
    return HashBits::decode(GetRawHashFromForwardingTable(field));
  }
  return HashBits::decode(field);
}

bool Name::TryGetHash(uint32_t* hash) const {
  uint32_t field = raw_hash_field(kAcquireLoad);
  if (IsHashFieldComputed(field)) {
    *hash = HashBits::decode(field);
    return true;
  }
  if (V8_UNLIKELY(IsForwardingIndex(field))) {
    *hash = HashBits::decode(GetRawHashFromForwardingTable(field));
    return true;
  }
  return false;
}

bool Name::IsInteresting(Isolate* isolate) {
  // TODO(ishell): consider using ReadOnlyRoots::IsNameForProtector() trick for
  // these strings and interesting symbols.
  return (IsSymbol(this) && Cast<Symbol>(this)->is_interesting_symbol()) ||
         this == *isolate->factory()->toJSON_string() ||
         this == *isolate->factory()->get_string();
}

bool Name::IsPrivate() {
  return IsSymbol(this) && Cast<Symbol>(this)->is_private();
}

bool Name::IsPrivateName() {
  bool is_private_name =
      IsSymbol(this) && Cast<Symbol>(this)->is_private_name();
  DCHECK_IMPLIES(is_private_name, IsPrivate());
  return is_private_name;
}

bool Name::IsPrivateBrand() {
  bool is_private_brand =
      IsSymbol(this) && Cast<Symbol>(this)->is_private_brand();
  DCHECK_IMPLIES(is_private_brand, IsPrivateName());
  return is_private_brand;
}

bool Name::AsArrayIndex(uint32_t* index) {
  return IsString(this) && Cast<String>(this)->AsArrayIndex(index);
}

bool Name::AsIntegerIndex(size_t* index) {
  return IsString(this) && Cast<String>(this)->AsIntegerIndex(index);
}

// static
bool Name::ContainsCachedArrayIndex(uint32_t raw_hash_field) {
  return (raw_hash_field & Name::kDoesNotContainCachedArrayIndexMask) == 0;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_NAME_INL_H_
