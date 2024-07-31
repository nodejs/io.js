// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/stub-cache.h"

#include "src/ast/ast.h"
#include "src/base/bits.h"
#include "src/heap/heap-inl.h"  // For InYoungGeneration().
#include "src/ic/ic-inl.h"
#include "src/logging/counters.h"
#include "src/objects/tagged-value-inl.h"

namespace v8 {
namespace internal {

StubCache::StubCache(Isolate* isolate) : isolate_(isolate) {
  // Ensure the nullptr (aka Smi::zero()) which StubCache::Get() returns
  // when the entry is not found is not considered as a handler.
  DCHECK(!IC::IsHandler(Tagged<MaybeObject>()));
}

void StubCache::Initialize() {
  DCHECK(base::bits::IsPowerOfTwo(kPrimaryTableSize));
  DCHECK(base::bits::IsPowerOfTwo(kSecondaryTableSize));
  Clear();
}

// Hash algorithm for the primary table. This algorithm is replicated in
// the AccessorAssembler.  Returns an index into the table that
// is scaled by 1 << kCacheIndexShift.
int StubCache::PrimaryOffset(Tagged<Name> name, Tagged<Map> map) {
  // Compute the hash of the name (use entire hash field).
  uint32_t field = name->RawHash();
  DCHECK(Name::IsHashFieldComputed(field));
  // Using only the low bits in 64-bit mode is unlikely to increase the
  // risk of collision even if the heap is spread over an area larger than
  // 4Gb (and not at all if it isn't).
  uint32_t map_low32bits =
      static_cast<uint32_t>(map.ptr() ^ (map.ptr() >> kPrimaryTableBits));
  // Base the offset on a simple combination of name and map.
  uint32_t key = map_low32bits + field;
  return key & ((kPrimaryTableSize - 1) << kCacheIndexShift);
}

// Hash algorithm for the secondary table.  This algorithm is replicated in
// assembler. This hash should be sufficiently different from the primary one
// in order to avoid collisions for minified code with short names.
// Returns an index into the table that is scaled by 1 << kCacheIndexShift.
int StubCache::SecondaryOffset(Tagged<Name> name, Tagged<Map> old_map) {
  uint32_t name_low32bits = static_cast<uint32_t>(name.ptr());
  uint32_t map_low32bits = static_cast<uint32_t>(old_map.ptr());
  uint32_t key = (map_low32bits + name_low32bits);
  key = key + (key >> kSecondaryTableBits);
  return key & ((kSecondaryTableSize - 1) << kCacheIndexShift);
}

int StubCache::PrimaryOffsetForTesting(Tagged<Name> name, Tagged<Map> map) {
  return PrimaryOffset(name, map);
}

int StubCache::SecondaryOffsetForTesting(Tagged<Name> name, Tagged<Map> map) {
  return SecondaryOffset(name, map);
}

#ifdef DEBUG
namespace {

bool CommonStubCacheChecks(StubCache* stub_cache, Tagged<Name> name,
                           Tagged<Map> map, Tagged<MaybeObject> handler) {
  // Validate that the name and handler do not move on scavenge, and that we
  // can use identity checks instead of structural equality checks.
  DCHECK(!Heap::InYoungGeneration(name));
  DCHECK(!Heap::InYoungGeneration(handler));
  DCHECK(IsUniqueName(name));
  if (handler.ptr() != kNullAddress) DCHECK(IC::IsHandler(handler));
  return true;
}

}  // namespace
#endif

void StubCache::Set(Tagged<Name> name, Tagged<Map> map,
                    Tagged<MaybeObject> handler) {
  DCHECK(CommonStubCacheChecks(this, name, map, handler));

  // Compute the primary entry.
  int primary_offset = PrimaryOffset(name, map);
  Entry* primary = entry(primary_, primary_offset);
  Tagged<MaybeObject> old_handler(
      TaggedValue::ToMaybeObject(isolate(), primary->value));
  // If the primary entry has useful data in it, we retire it to the
  // secondary cache before overwriting it.
  // We need SafeEquals here while Builtin Code objects still live in the RO
  // space inside the sandbox.
  static_assert(!kAllCodeObjectsLiveInTrustedSpace);
  if (!old_handler.SafeEquals(isolate()->builtins()->code(Builtin::kIllegal)) &&
      !primary->map.IsSmi()) {
    Tagged<Map> old_map =
        Map::cast(StrongTaggedValue::ToObject(isolate(), primary->map));
    Tagged<Name> old_name =
        Name::cast(StrongTaggedValue::ToObject(isolate(), primary->key));
    int secondary_offset = SecondaryOffset(old_name, old_map);
    Entry* secondary = entry(secondary_, secondary_offset);
    *secondary = *primary;
  }

  // Update primary cache.
  primary->key = StrongTaggedValue(name);
  primary->value = TaggedValue(handler);
  primary->map = StrongTaggedValue(map);
  isolate()->counters()->megamorphic_stub_cache_updates()->Increment();
}

Tagged<MaybeObject> StubCache::Get(Tagged<Name> name, Tagged<Map> map) {
  DCHECK(CommonStubCacheChecks(this, name, map, Tagged<MaybeObject>()));
  int primary_offset = PrimaryOffset(name, map);
  Entry* primary = entry(primary_, primary_offset);
  if (primary->key == name && primary->map == map) {
    return TaggedValue::ToMaybeObject(isolate(), primary->value);
  }
  int secondary_offset = SecondaryOffset(name, map);
  Entry* secondary = entry(secondary_, secondary_offset);
  if (secondary->key == name && secondary->map == map) {
    return TaggedValue::ToMaybeObject(isolate(), secondary->value);
  }
  return Tagged<MaybeObject>();
}

void StubCache::Clear() {
  Tagged<MaybeObject> empty = isolate_->builtins()->code(Builtin::kIllegal);
  Tagged<Name> empty_string = ReadOnlyRoots(isolate()).empty_string();
  for (int i = 0; i < kPrimaryTableSize; i++) {
    primary_[i].key = StrongTaggedValue(empty_string);
    primary_[i].map = StrongTaggedValue(Smi::zero());
    primary_[i].value = TaggedValue(empty);
  }
  for (int j = 0; j < kSecondaryTableSize; j++) {
    secondary_[j].key = StrongTaggedValue(empty_string);
    secondary_[j].map = StrongTaggedValue(Smi::zero());
    secondary_[j].value = TaggedValue(empty);
  }
}

}  // namespace internal
}  // namespace v8
