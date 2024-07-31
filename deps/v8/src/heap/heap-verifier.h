// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_VERIFIER_H_
#define V8_HEAP_HEAP_VERIFIER_H_

#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/map.h"

namespace v8 {
namespace internal {

class Heap;
class ReadOnlyHeap;

// Interface for verifying spaces in the heap.
class SpaceVerificationVisitor {
 public:
  virtual ~SpaceVerificationVisitor() = default;

  // This method will be invoked for every object in the space.
  virtual void VerifyObject(Tagged<HeapObject> object) = 0;

  // This method will be invoked for each page in the space before verifying an
  // object on it.
  virtual void VerifyPage(const MemoryChunkMetadata* chunk) = 0;

  // This method will be invoked after verifying all objects on that page.
  virtual void VerifyPageDone(const MemoryChunkMetadata* chunk) = 0;
};

class HeapVerifier final {
 public:
#ifdef VERIFY_HEAP
  // Verify the heap is in its normal state before or after a GC.
  V8_EXPORT_PRIVATE static void VerifyHeap(Heap* heap);

  // Verify the read-only heap after all read-only heap objects have been
  // created.
  V8_EXPORT_PRIVATE static void VerifyReadOnlyHeap(Heap* heap);

  // Checks that this is a safe map transition.
  V8_EXPORT_PRIVATE static void VerifySafeMapTransition(
      Heap* heap, Tagged<HeapObject> object, Tagged<Map> new_map);

  // This function checks that either
  // - the map transition is safe,
  // - or it was communicated to GC using NotifyObjectLayoutChange.
  V8_EXPORT_PRIVATE static void VerifyObjectLayoutChange(
      Heap* heap, Tagged<HeapObject> object, Tagged<Map> new_map);

  // Verifies that that the object is allowed to change layout. Checks that if
  // the object is in shared space, it must be a string as no other objects in
  // shared space change layouts.
  static void VerifyObjectLayoutChangeIsAllowed(Heap* heap,
                                                Tagged<HeapObject> object);

  static void SetPendingLayoutChangeObject(Heap* heap,
                                           Tagged<HeapObject> object);

#else
  static void VerifyHeap(Heap* heap) {}
  static void VerifyReadOnlyHeap(Heap* heap) {}
  static void VerifySharedHeap(Heap* heap, Isolate* initiator) {}
  static void VerifyRememberedSetFor(Heap* heap, Tagged<HeapObject> object) {}
  static void VerifySafeMapTransition(Heap* heap, Tagged<HeapObject> object,
                                      Tagged<Map> new_map) {}
  static void VerifyObjectLayoutChange(Heap* heap, Tagged<HeapObject> object,
                                       Tagged<Map> new_map) {}
  static void VerifyObjectLayoutChangeIsAllowed(Heap* heap,
                                                Tagged<HeapObject> object) {}
#endif

  V8_INLINE static void VerifyHeapIfEnabled(Heap* heap) {
    if (v8_flags.verify_heap) {
      VerifyHeap(heap);
    }
  }

 private:
  HeapVerifier();
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_VERIFIER_H_
