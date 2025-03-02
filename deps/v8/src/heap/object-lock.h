// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_OBJECT_LOCK_H_
#define V8_HEAP_OBJECT_LOCK_H_

#include "src/heap/mutable-page-metadata.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

class ObjectLock final {
 public:
  V8_INLINE static void Lock(Tagged<HeapObject> heap_object);
  V8_INLINE static void Unlock(Tagged<HeapObject> heap_object);
};

class ObjectLockGuard final {
 public:
  V8_INLINE explicit ObjectLockGuard(Tagged<HeapObject> object);
  V8_INLINE ~ObjectLockGuard();

 private:
  Tagged<HeapObject> raw_object_;
};

}  // namespace v8::internal

#endif  // V8_HEAP_OBJECT_LOCK_H_
