// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_LAYOUT_H_
#define V8_HEAP_HEAP_LAYOUT_H_

// Clients of this interface shouldn't depend on lots of heap internals.
// Do not include anything from src/heap here!

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/objects/objects.h"
#include "src/objects/tagged.h"

namespace v8::internal {

class MemoryChunk;

// Checks for heap layouts. The checks generally use Heap infrastructure (heap,
// space, page, mark bits, etc) and do not rely on instance types.
class HeapLayout final : public AllStatic {
 public:
  // Returns whether `object` is part of a read-only space.
  static V8_INLINE bool InReadOnlySpace(Tagged<HeapObject> object);

  static V8_INLINE bool InYoungGeneration(Tagged<Object> object);
  static V8_INLINE bool InYoungGeneration(Tagged<HeapObject> object);
  static V8_INLINE bool InYoungGeneration(Tagged<MaybeObject> object);
  static V8_INLINE bool InYoungGeneration(const HeapObjectLayout* object);
  static V8_INLINE bool InYoungGeneration(const MemoryChunk* chunk,
                                          Tagged<HeapObject> object);

  // Returns whether `object` is in a writable shared space. The is agnostic to
  // how the shared space itself is managed.
  static V8_INLINE bool InWritableSharedSpace(Tagged<HeapObject> object);
  // Returns whether `object` is in a shared space.
  static V8_INLINE bool InAnySharedSpace(Tagged<HeapObject> object);

  // Returns whether `object` is in code space. Note that there's various kinds
  // of different code spaces (regular, external, large object) which are all
  // covered by this check.
  static V8_INLINE bool InCodeSpace(Tagged<HeapObject> object);

  // Returns whether `object` is allocated in trusted space. See
  // src/sandbox/GLOSSARY.md for details.
  static V8_INLINE bool InTrustedSpace(Tagged<HeapObject> object);

  // Returns whether `object` is allocated on a black page (during
  // incremental/concurrent marking).
  static V8_INLINE bool InBlackAllocatedPage(Tagged<HeapObject> object);

  // Returns whether `object` is allocated on a page which is owned by some Heap
  // instance. This is equivalent to !InReadOnlySpace except during
  // serialization.
  static V8_INLINE bool IsOwnedByAnyHeap(Tagged<HeapObject> object);

  // Returns whether the map word of `object` is a self forwarding address.
  // This represents pinned objects and live large objects in Scavenger.
  static bool IsSelfForwarded(Tagged<HeapObject> object);
  static bool IsSelfForwarded(Tagged<HeapObject> object,
                              PtrComprCageBase cage_base);
  static bool IsSelfForwarded(Tagged<HeapObject> object, MapWord map_word);

 private:
  V8_EXPORT static bool InYoungGenerationForStickyMarkbits(
      const MemoryChunk* chunk, Tagged<HeapObject> object);

  V8_EXPORT static void CheckYoungGenerationConsistency(
      const MemoryChunk* chunk);
};

}  // namespace v8::internal

#endif  // V8_HEAP_HEAP_LAYOUT_H_
