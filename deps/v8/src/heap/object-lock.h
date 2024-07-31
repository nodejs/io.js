// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_OBJECT_LOCK_H_
#define V8_HEAP_OBJECT_LOCK_H_

#include "src/heap/mutable-page.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

class ExclusiveObjectLock final {
 public:
  static void Lock(Tagged<HeapObject> heap_object) {
    MutablePageMetadata::FromHeapObject(heap_object)
        ->shared_mutex()
        ->LockExclusive();
  }
  static void Unlock(Tagged<HeapObject> heap_object) {
    MutablePageMetadata::FromHeapObject(heap_object)
        ->shared_mutex()
        ->UnlockExclusive();
  }
};

class SharedObjectLock final {
 public:
  static void Lock(Tagged<HeapObject> heap_object) {
    MutablePageMetadata::FromHeapObject(heap_object)
        ->shared_mutex()
        ->LockShared();
  }
  static void Unlock(Tagged<HeapObject> heap_object) {
    MutablePageMetadata::FromHeapObject(heap_object)
        ->shared_mutex()
        ->UnlockShared();
  }
};

template <typename LockType>
class ObjectLockGuard final {
 public:
  explicit ObjectLockGuard(Tagged<HeapObject> object) : raw_object_(object) {
    LockType::Lock(object);
  }
  ~ObjectLockGuard() { LockType::Unlock(raw_object_); }

 private:
  Tagged<HeapObject> raw_object_;
};

using ExclusiveObjectLockGuard = ObjectLockGuard<ExclusiveObjectLock>;
using SharedObjectLockGuard = ObjectLockGuard<SharedObjectLock>;

}  // namespace v8::internal

#endif  // V8_HEAP_OBJECT_LOCK_H_
