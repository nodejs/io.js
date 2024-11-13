// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_OBJECT_LOCK_H_
#define V8_HEAP_OBJECT_LOCK_H_

#include "src/heap/mutable-page-metadata.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

class ExclusiveObjectLock final {
 public:
  V8_INLINE static void Lock(Tagged<HeapObject> heap_object);
  V8_INLINE static void Unlock(Tagged<HeapObject> heap_object);
};

class SharedObjectLock final {
 public:
  V8_INLINE static void Lock(Tagged<HeapObject> heap_object);
  V8_INLINE static void Unlock(Tagged<HeapObject> heap_object);
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
