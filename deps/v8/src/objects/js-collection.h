// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_COLLECTION_H_
#define V8_OBJECTS_JS_COLLECTION_H_

#include "src/objects/js-collection-iterator.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class OrderedHashSet;
class OrderedHashMap;

#include "torque-generated/src/objects/js-collection-tq.inc"

class JSCollection
    : public TorqueGeneratedJSCollection<JSCollection, JSObject> {
 public:
  static const int kAddFunctionDescriptorIndex = 3;

  TQ_OBJECT_CONSTRUCTORS(JSCollection)
};

// The JSSet describes EcmaScript Harmony sets
class JSSet : public TorqueGeneratedJSSet<JSSet, JSCollection> {
 public:
  static void Initialize(DirectHandle<JSSet> set, Isolate* isolate);
  static void Clear(Isolate* isolate, DirectHandle<JSSet> set);
  void Rehash(Isolate* isolate);

  // Dispatched behavior.
  DECL_PRINTER(JSSet)
  DECL_VERIFIER(JSSet)

  TQ_OBJECT_CONSTRUCTORS(JSSet)
};

class JSSetIterator
    : public OrderedHashTableIterator<JSSetIterator, OrderedHashSet> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(JSSetIterator)
  DECL_VERIFIER(JSSetIterator)

  OBJECT_CONSTRUCTORS(JSSetIterator,
                      OrderedHashTableIterator<JSSetIterator, OrderedHashSet>);
};

// The JSMap describes EcmaScript Harmony maps
class JSMap : public TorqueGeneratedJSMap<JSMap, JSCollection> {
 public:
  static void Initialize(DirectHandle<JSMap> map, Isolate* isolate);
  static void Clear(Isolate* isolate, DirectHandle<JSMap> map);
  void Rehash(Isolate* isolate);

  // Dispatched behavior.
  DECL_PRINTER(JSMap)
  DECL_VERIFIER(JSMap)

  TQ_OBJECT_CONSTRUCTORS(JSMap)
};

class JSMapIterator
    : public OrderedHashTableIterator<JSMapIterator, OrderedHashMap> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(JSMapIterator)
  DECL_VERIFIER(JSMapIterator)

  // Returns the current value of the iterator. This should only be called when
  // |HasMore| returns true.
  inline Tagged<Object> CurrentValue();

  OBJECT_CONSTRUCTORS(JSMapIterator,
                      OrderedHashTableIterator<JSMapIterator, OrderedHashMap>);
};

// Base class for both JSWeakMap and JSWeakSet
class JSWeakCollection
    : public TorqueGeneratedJSWeakCollection<JSWeakCollection, JSObject> {
 public:
  static void Initialize(DirectHandle<JSWeakCollection> collection,
                         Isolate* isolate);
  V8_EXPORT_PRIVATE static void Set(DirectHandle<JSWeakCollection> collection,
                                    Handle<Object> key,
                                    DirectHandle<Object> value, int32_t hash);
  static bool Delete(DirectHandle<JSWeakCollection> collection,
                     Handle<Object> key, int32_t hash);
  static Handle<JSArray> GetEntries(DirectHandle<JSWeakCollection> holder,
                                    int max_entries);

  static const int kAddFunctionDescriptorIndex = 3;

  // Iterates the function object according to the visiting policy.
  class BodyDescriptorImpl;

  // Visit the whole object.
  using BodyDescriptor = BodyDescriptorImpl;

  static const int kHeaderSizeOfAllWeakCollections = kHeaderSize;

  TQ_OBJECT_CONSTRUCTORS(JSWeakCollection)
};

// The JSWeakMap describes EcmaScript Harmony weak maps
class JSWeakMap : public TorqueGeneratedJSWeakMap<JSWeakMap, JSWeakCollection> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(JSWeakMap)
  DECL_VERIFIER(JSWeakMap)

  static_assert(kHeaderSize == kHeaderSizeOfAllWeakCollections);
  TQ_OBJECT_CONSTRUCTORS(JSWeakMap)
};

// The JSWeakSet describes EcmaScript Harmony weak sets
class JSWeakSet : public TorqueGeneratedJSWeakSet<JSWeakSet, JSWeakCollection> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(JSWeakSet)
  DECL_VERIFIER(JSWeakSet)

  static_assert(kHeaderSize == kHeaderSizeOfAllWeakCollections);
  TQ_OBJECT_CONSTRUCTORS(JSWeakSet)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_COLLECTION_H_
