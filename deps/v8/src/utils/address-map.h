// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_ADDRESS_MAP_H_
#define V8_UTILS_ADDRESS_MAP_H_

#include "src/base/hashmap.h"
#include "src/common/assert-scope.h"
#include "src/objects/heap-object.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

template <typename Type>
class PointerToIndexHashMap
    : public base::TemplateHashMapImpl<uintptr_t, uint32_t,
                                       base::KeyEqualityMatcher<intptr_t>,
                                       base::DefaultAllocationPolicy> {
 public:
  using Entry = base::TemplateHashMapEntry<uintptr_t, uint32_t>;

  inline void Set(Type value, uint32_t index) {
    uintptr_t key = Key(value);
    LookupOrInsert(key, Hash(key))->value = index;
  }

  inline Maybe<uint32_t> Get(Type value) const {
    uintptr_t key = Key(value);
    Entry* entry = Lookup(key, Hash(key));
    if (entry == nullptr) return Nothing<uint32_t>();
    return Just(entry->value);
  }

 private:
  static inline uintptr_t Key(Type value);

  static uint32_t Hash(uintptr_t key) { return static_cast<uint32_t>(key); }
};

template <>
inline uintptr_t PointerToIndexHashMap<Address>::Key(Address value) {
  return static_cast<uintptr_t>(value);
}

template <>
inline uintptr_t PointerToIndexHashMap<Tagged<HeapObject>>::Key(
    Tagged<HeapObject> value) {
  return value.ptr();
}

class AddressToIndexHashMap : public PointerToIndexHashMap<Address> {};
class HeapObjectToIndexHashMap
    : public PointerToIndexHashMap<Tagged<HeapObject>> {};

class RootIndexMap {
 public:
  explicit RootIndexMap(Isolate* isolate);
  RootIndexMap(const RootIndexMap&) = delete;
  RootIndexMap& operator=(const RootIndexMap&) = delete;

  // Returns true on successful lookup and sets *|out_root_list|.
  V8_EXPORT_PRIVATE bool Lookup(Tagged<HeapObject> obj,
                                RootIndex* out_root_list) const {
    Maybe<uint32_t> maybe_index = map_->Get(obj);
    if (maybe_index.IsJust()) {
      *out_root_list = static_cast<RootIndex>(maybe_index.FromJust());
      return true;
    }
    return false;
  }

 private:
  HeapObjectToIndexHashMap* map_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_ADDRESS_MAP_H_
