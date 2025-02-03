// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_SET_INL_H_
#define V8_OBJECTS_STRING_SET_INL_H_

#include "src/objects/string-inl.h"
#include "src/objects/string-set.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

StringSet::StringSet(Address ptr) : HashTable<StringSet, StringSetShape>(ptr) {
  SLOW_DCHECK(IsStringSet(*this));
}

bool StringSetShape::IsMatch(Tagged<String> key, Tagged<Object> value) {
  DCHECK(IsString(value));
  return key->Equals(Cast<String>(value));
}

uint32_t StringSetShape::Hash(ReadOnlyRoots roots, Tagged<String> key) {
  return key->EnsureHash();
}

uint32_t StringSetShape::HashForObject(ReadOnlyRoots roots,
                                       Tagged<Object> object) {
  return Cast<String>(object)->EnsureHash();
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_SET_INL_H_
