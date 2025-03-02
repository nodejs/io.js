// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_OBJECT_LOCK_INL_H_
#define V8_HEAP_OBJECT_LOCK_INL_H_

#include "src/heap/mutable-page-metadata-inl.h"
#include "src/heap/object-lock.h"

namespace v8 {
namespace internal {

// static
void ObjectLock::Lock(Tagged<HeapObject> heap_object) {
  MutablePageMetadata::FromHeapObject(heap_object)->object_mutex().Lock();
}

// static
void ObjectLock::Unlock(Tagged<HeapObject> heap_object) {
  MutablePageMetadata::FromHeapObject(heap_object)->object_mutex().Unlock();
}

ObjectLockGuard::ObjectLockGuard(Tagged<HeapObject> object)
    : raw_object_(object) {
  ObjectLock::Lock(object);
}

ObjectLockGuard::~ObjectLockGuard() { ObjectLock::Unlock(raw_object_); }

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_OBJECT_LOCK_INL_H_
