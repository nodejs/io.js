// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_FACTORY_INL_H_
#define V8_HEAP_LOCAL_FACTORY_INL_H_

#include "src/heap/factory-base-inl.h"
#include "src/heap/local-factory.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {

#define ACCESSOR_INFO_ACCESSOR(Type, name, CamelName)                          \
  Handle<Type> LocalFactory::name() {                                          \
    /* Do a bit of handle location magic to cast the Handle without having */  \
    /* to pull in Cast<Type>. We know the type is right by construction.   */  \
    return Handle<Type>(                                                       \
        isolate()->isolate_->root_handle(RootIndex::k##CamelName).location()); \
  }
ACCESSOR_INFO_ROOT_LIST(ACCESSOR_INFO_ACCESSOR)
#undef ACCESSOR_INFO_ACCESSOR

AllocationType LocalFactory::AllocationTypeForInPlaceInternalizableString() {
  return isolate()
      ->heap()
      ->AsHeap()
      ->allocation_type_for_in_place_internalizable_strings();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_FACTORY_INL_H_
