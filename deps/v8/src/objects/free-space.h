// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FREE_SPACE_H_
#define V8_OBJECTS_FREE_SPACE_H_

#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/free-space-tq.inc"

// FreeSpace are fixed-size free memory blocks used by the heap and GC.
// They look like heap objects (are heap object tagged and have a map) so that
// the heap remains iterable.  They have a size and a next pointer.
// The next pointer is the raw address of the next FreeSpace object (or NULL)
// in the free list.
//
// When external code space is enabled next pointer is stored as Smi values
// representing a diff from current FreeSpace object address in kObjectAlignment
// chunks. Terminating FreeSpace value is represented as Smi zero.
// Such a representation has the following properties:
// a) it can hold both positive an negative diffs for full pointer compression
//    cage size (HeapObject address has only valuable 30 bits while Smis have
//    31 bits),
// b) it's independent of the pointer compression base and pointer compression
//    scheme.
class FreeSpace : public TorqueGeneratedFreeSpace<FreeSpace, HeapObject> {
 public:
  // [size]: size of the free space including the header.
  DECL_RELAXED_INT_ACCESSORS(size)
  static inline void SetSize(const WritableFreeSpace& writable_free_space,
                             int size, RelaxedStoreTag);
  inline int Size();

  // Accessors for the next field.
  inline Tagged<FreeSpace> next() const;
  inline void SetNext(const WritableFreeSpace& writable_free_space,
                      Tagged<FreeSpace> next);

  // Dispatched behavior.
  DECL_PRINTER(FreeSpace)

  class BodyDescriptor;

 private:
  inline bool IsValid() const;

  TQ_OBJECT_CONSTRUCTORS(FreeSpace)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FREE_SPACE_H_
