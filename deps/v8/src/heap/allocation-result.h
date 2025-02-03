// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ALLOCATION_RESULT_H_
#define V8_HEAP_ALLOCATION_RESULT_H_

#include "src/common/globals.h"
#include "src/objects/casting.h"
#include "src/objects/heap-object.h"

namespace v8 {
namespace internal {

enum class AllocationOrigin {
  kGeneratedCode = 0,
  kRuntime = 1,
  kGC = 2,
  kFirstAllocationOrigin = kGeneratedCode,
  kLastAllocationOrigin = kGC,
  kNumberOfAllocationOrigins = kLastAllocationOrigin + 1
};

// The result of an allocation attempt. Either represents a successful
// allocation that can be turned into an object or a failed attempt.
class AllocationResult final {
 public:
  static AllocationResult Failure() { return AllocationResult(); }

  static AllocationResult FromObject(Tagged<HeapObject> heap_object) {
    return AllocationResult(heap_object);
  }

  // Empty constructor creates a failed result. The callsite determines which
  // GC to invoke based on the requested allocation.
  AllocationResult() = default;

  bool IsFailure() const { return object_.is_null(); }

  template <typename T>
  bool To(Tagged<T>* obj) const {
    if (IsFailure()) return false;
    *obj = Cast<T>(object_);
    return true;
  }

  Tagged<HeapObject> ToObjectChecked() const {
    CHECK(!IsFailure());
    return Cast<HeapObject>(object_);
  }

  Tagged<HeapObject> ToObject() const {
    DCHECK(!IsFailure());
    return Cast<HeapObject>(object_);
  }

  Address ToAddress() const {
    DCHECK(!IsFailure());
    return Cast<HeapObject>(object_).address();
  }

 private:
  explicit AllocationResult(Tagged<HeapObject> heap_object)
      : object_(heap_object) {}

  Tagged<HeapObject> object_;
};

static_assert(sizeof(AllocationResult) == kSystemPointerSize);

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATION_RESULT_H_
