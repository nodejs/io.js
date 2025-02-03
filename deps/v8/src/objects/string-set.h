// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_SET_H_
#define V8_OBJECTS_STRING_SET_H_

#include "src/objects/hash-table.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class StringSetShape : public BaseShape<Tagged<String>> {
 public:
  static inline bool IsMatch(Tagged<String> key, Tagged<Object> value);
  static inline uint32_t Hash(ReadOnlyRoots roots, Tagged<String> key);
  static inline uint32_t HashForObject(ReadOnlyRoots roots,
                                       Tagged<Object> object);

  static const int kPrefixSize = 0;
  static const int kEntrySize = 1;
  static const bool kMatchNeedsHoleCheck = true;
};

EXTERN_DECLARE_HASH_TABLE(StringSet, StringSetShape)

class StringSet : public HashTable<StringSet, StringSetShape> {
 public:
  V8_EXPORT_PRIVATE static Handle<StringSet> New(Isolate* isolate);
  V8_EXPORT_PRIVATE static Handle<StringSet> Add(Isolate* isolate,
                                                 Handle<StringSet> stringset,
                                                 DirectHandle<String> name);
  V8_EXPORT_PRIVATE bool Has(Isolate* isolate, DirectHandle<String> name);

  OBJECT_CONSTRUCTORS(StringSet, HashTable<StringSet, StringSetShape>);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_SET_H_
