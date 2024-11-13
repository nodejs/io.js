// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_COLLECTION_INL_H_
#define V8_OBJECTS_JS_COLLECTION_INL_H_

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/js-collection-iterator-inl.h"
#include "src/objects/js-collection.h"
#include "src/objects/objects-inl.h"
#include "src/objects/ordered-hash-table-inl.h"
#include "src/roots/roots-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-collection-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSCollection)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSMap)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSSet)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSWeakCollection)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSWeakMap)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSWeakSet)

template <class Derived, class TableType>
OrderedHashTableIterator<Derived, TableType>::OrderedHashTableIterator(
    Address ptr)
    : JSCollectionIterator(ptr) {}

JSMapIterator::JSMapIterator(Address ptr)
    : OrderedHashTableIterator<JSMapIterator, OrderedHashMap>(ptr) {
  SLOW_DCHECK(IsJSMapIterator(*this));
}

JSSetIterator::JSSetIterator(Address ptr)
    : OrderedHashTableIterator<JSSetIterator, OrderedHashSet>(ptr) {
  SLOW_DCHECK(IsJSSetIterator(*this));
}

Tagged<Object> JSMapIterator::CurrentValue() {
  Tagged<OrderedHashMap> table = Cast<OrderedHashMap>(this->table());
  int index = Smi::ToInt(this->index());
  DCHECK_GE(index, 0);
  InternalIndex entry(index);
  Tagged<Object> value = table->ValueAt(entry);
  DCHECK(!IsHashTableHole(value));
  return value;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_COLLECTION_INL_H_
