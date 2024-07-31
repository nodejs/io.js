// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_LOOKUP_CACHE_H_
#define V8_OBJECTS_LOOKUP_CACHE_H_

#include "src/objects/map.h"
#include "src/objects/name.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

// Cache for mapping (map, property name) into descriptor index.
// The cache contains both positive and negative results.
// Descriptor index equals kNotFound means the property is absent.
// Cleared at startup and prior to any gc.
class DescriptorLookupCache {
 public:
  DescriptorLookupCache(const DescriptorLookupCache&) = delete;
  DescriptorLookupCache& operator=(const DescriptorLookupCache&) = delete;
  // Lookup descriptor index for (map, name).
  // If absent, kAbsent is returned.
  inline int Lookup(Tagged<Map> source, Tagged<Name> name);

  // Update an element in the cache.
  inline void Update(Tagged<Map> source, Tagged<Name> name, int result);

  // Clear the cache.
  void Clear();

  static const int kAbsent = -2;

 private:
  DescriptorLookupCache() {
    for (int i = 0; i < kLength; ++i) {
      keys_[i].source = Tagged<Map>();
      keys_[i].name = Tagged<Name>();
      results_[i] = kAbsent;
    }
  }

  static inline int Hash(Tagged<Map> source, Tagged<Name> name);

  static const int kLength = 64;
  struct Key {
    Tagged<Map> source;
    Tagged<Name> name;
  };

  Key keys_[kLength];
  int results_[kLength];

  friend class Isolate;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_LOOKUP_CACHE_H_
