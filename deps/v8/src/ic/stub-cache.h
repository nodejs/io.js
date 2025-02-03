// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_STUB_CACHE_H_
#define V8_IC_STUB_CACHE_H_

#include "include/v8-callbacks.h"
#include "src/objects/name.h"
#include "src/objects/tagged-value.h"

namespace v8 {
namespace internal {

// The stub cache is used for megamorphic property accesses.
// It maps (map, name, type) to property access handlers. The cache does not
// need explicit invalidation when a prototype chain is modified, since the
// handlers verify the chain.

class SCTableReference {
 public:
  Address address() const { return address_; }

 private:
  explicit SCTableReference(Address address) : address_(address) {}

  Address address_;

  friend class StubCache;
};

class V8_EXPORT_PRIVATE StubCache {
 public:
  struct Entry {
    // {key} is a tagged Name pointer, may be cleared by setting to empty
    // string.
    StrongTaggedValue key;
    // {value} is a tagged heap object reference (weak or strong), equivalent
    // to a Tagged<MaybeObject>'s payload.
    TaggedValue value;
    // {map} is a tagged Map pointer, may be cleared by setting to Smi::zero().
    StrongTaggedValue map;
  };

  void Initialize();
  // Access cache for entry hash(name, map).
  void Set(Tagged<Name> name, Tagged<Map> map, Tagged<MaybeObject> handler);
  Tagged<MaybeObject> Get(Tagged<Name> name, Tagged<Map> map);
  // Clear the lookup table (@ mark compact collection).
  void Clear();

  enum Table { kPrimary, kSecondary };

  SCTableReference key_reference(StubCache::Table table) {
    return SCTableReference(
        reinterpret_cast<Address>(&first_entry(table)->key));
  }

  SCTableReference map_reference(StubCache::Table table) {
    return SCTableReference(
        reinterpret_cast<Address>(&first_entry(table)->map));
  }

  SCTableReference value_reference(StubCache::Table table) {
    return SCTableReference(
        reinterpret_cast<Address>(&first_entry(table)->value));
  }

  StubCache::Entry* first_entry(StubCache::Table table) {
    switch (table) {
      case StubCache::kPrimary:
        return StubCache::primary_;
      case StubCache::kSecondary:
        return StubCache::secondary_;
    }
    UNREACHABLE();
  }

  Isolate* isolate() { return isolate_; }

  // Setting kCacheIndexShift to Name::HashBits::kShift is convenient because it
  // causes the bit field inside the hash field to get shifted out implicitly.
  // Note that kCacheIndexShift must not get too large, because
  // sizeof(Entry) needs to be a multiple of 1 << kCacheIndexShift (see
  // the static_assert below, in {entry(...)}).
  static const int kCacheIndexShift = Name::HashBits::kShift;

  static const int kPrimaryTableBits = 11;
  static const int kPrimaryTableSize = (1 << kPrimaryTableBits);
  static const int kSecondaryTableBits = 9;
  static const int kSecondaryTableSize = (1 << kSecondaryTableBits);

  static int PrimaryOffsetForTesting(Tagged<Name> name, Tagged<Map> map);
  static int SecondaryOffsetForTesting(Tagged<Name> name, Tagged<Map> map);

  // The constructor is made public only for the purposes of testing.
  explicit StubCache(Isolate* isolate);
  StubCache(const StubCache&) = delete;
  StubCache& operator=(const StubCache&) = delete;

 private:
  // The stub cache has a primary and secondary level.  The two levels have
  // different hashing algorithms in order to avoid simultaneous collisions
  // in both caches.  Unlike a probing strategy (quadratic or otherwise) the
  // update strategy on updates is fairly clear and simple:  Any existing entry
  // in the primary cache is moved to the secondary cache, and secondary cache
  // entries are overwritten.

  // Hash algorithm for the primary table.  This algorithm is replicated in
  // assembler for every architecture.  Returns an index into the table that
  // is scaled by 1 << kCacheIndexShift.
  static int PrimaryOffset(Tagged<Name> name, Tagged<Map> map);

  // Hash algorithm for the secondary table.  This algorithm is replicated in
  // assembler for every architecture.  Returns an index into the table that
  // is scaled by 1 << kCacheIndexShift.
  static int SecondaryOffset(Tagged<Name> name, Tagged<Map> map);

  // Compute the entry for a given offset in exactly the same way as
  // we do in generated code.  We generate an hash code that already
  // ends in Name::HashBits::kShift 0s.  Then we multiply it so it is a multiple
  // of sizeof(Entry).  This makes it easier to avoid making mistakes
  // in the hashed offset computations.
  static Entry* entry(Entry* table, int offset) {
    // The size of {Entry} must be a multiple of 1 << kCacheIndexShift.
    static_assert((sizeof(*table) >> kCacheIndexShift) << kCacheIndexShift ==
                  sizeof(*table));
    const int multiplier = sizeof(*table) >> kCacheIndexShift;
    return reinterpret_cast<Entry*>(reinterpret_cast<Address>(table) +
                                    offset * multiplier);
  }

 private:
  Entry primary_[kPrimaryTableSize];
  Entry secondary_[kSecondaryTableSize];
  Isolate* isolate_;

  friend class Isolate;
  friend class SCTableReference;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_IC_STUB_CACHE_H_
