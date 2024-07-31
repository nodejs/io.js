// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_STATE_H_
#define V8_HEAP_MARKING_STATE_H_

#include "src/common/globals.h"
#include "src/heap/marking.h"
#include "src/objects/heap-object.h"

namespace v8 {
namespace internal {

class MemoryChunkMetadata;
class MutablePageMetadata;

template <typename ConcreteState, AccessMode access_mode>
class MarkingStateBase {
 public:
  explicit MarkingStateBase(PtrComprCageBase cage_base)
#if V8_COMPRESS_POINTERS
      : cage_base_(cage_base)
#endif
  {
  }

  // The pointer compression cage base value used for decompression of all
  // tagged values except references to InstructionStream objects.
  V8_INLINE PtrComprCageBase cage_base() const {
#if V8_COMPRESS_POINTERS
    return cage_base_;
#else
    return PtrComprCageBase{};
#endif  // V8_COMPRESS_POINTERS
  }

  V8_INLINE bool TryMark(Tagged<HeapObject> obj);
  // Helper method for fully marking an object and accounting its live bytes.
  // Should be used to mark individual objects in one-off cases.
  V8_INLINE bool TryMarkAndAccountLiveBytes(Tagged<HeapObject> obj);
  // Same, but does not require the object to be initialized.
  V8_INLINE bool TryMarkAndAccountLiveBytes(Tagged<HeapObject> obj,
                                            int object_size);
  V8_INLINE bool IsMarked(const Tagged<HeapObject> obj) const;
  V8_INLINE bool IsUnmarked(const Tagged<HeapObject> obj) const;

 private:
#if V8_COMPRESS_POINTERS
  const PtrComprCageBase cage_base_;
#endif  // V8_COMPRESS_POINTERS
};

// This is used by marking visitors.
class MarkingState final
    : public MarkingStateBase<MarkingState, AccessMode::ATOMIC> {
 public:
  explicit MarkingState(PtrComprCageBase cage_base)
      : MarkingStateBase(cage_base) {}
};

class NonAtomicMarkingState final
    : public MarkingStateBase<NonAtomicMarkingState, AccessMode::NON_ATOMIC> {
 public:
  explicit NonAtomicMarkingState(PtrComprCageBase cage_base)
      : MarkingStateBase(cage_base) {}
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_STATE_H_
