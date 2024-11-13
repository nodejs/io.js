// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_WRITE_BARRIER_H_
#define V8_HEAP_HEAP_WRITE_BARRIER_H_

#include "include/v8-internal.h"
#include "src/common/globals.h"
#include "src/objects/heap-object.h"

namespace v8 {
namespace internal {

class ArrayBufferExtension;
class InstructionStream;
class DescriptorArray;
class EphemeronHashTable;
class FixedArray;
class Heap;
class JSArrayBuffer;
class Map;
class MarkCompactCollector;
class MarkingBarrier;
class RelocInfo;

// Note: In general it is preferred to use the macros defined in
// object-macros.h.

// Combined write barriers.
void WriteBarrierForCode(Tagged<InstructionStream> host, RelocInfo* rinfo,
                         Tagged<Object> value,
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
void WriteBarrierForCode(Tagged<InstructionStream> host, RelocInfo* rinfo,
                         Tagged<HeapObject> value,
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

void CombinedWriteBarrier(Tagged<HeapObject> object, ObjectSlot slot,
                          Tagged<Object> value, WriteBarrierMode mode);
void CombinedWriteBarrier(Tagged<HeapObject> object, MaybeObjectSlot slot,
                          Tagged<MaybeObject> value, WriteBarrierMode mode);

void CombinedWriteBarrier(HeapObjectLayout* object, TaggedMemberBase* slot,
                          Tagged<Object> value, WriteBarrierMode mode);

void CombinedEphemeronWriteBarrier(Tagged<EphemeronHashTable> object,
                                   ObjectSlot slot, Tagged<Object> value,
                                   WriteBarrierMode mode);

void IndirectPointerWriteBarrier(Tagged<HeapObject> host,
                                 IndirectPointerSlot slot,
                                 Tagged<HeapObject> value,
                                 WriteBarrierMode mode);

void JSDispatchHandleWriteBarrier(Tagged<HeapObject> host,
                                  JSDispatchHandle handle,
                                  WriteBarrierMode mode);

// Generational write barrier.
void GenerationalBarrierForCode(Tagged<InstructionStream> host,
                                RelocInfo* rinfo, Tagged<HeapObject> object);

inline bool IsReadOnlyHeapObject(Tagged<HeapObject> object);

class V8_EXPORT_PRIVATE WriteBarrier {
 public:
  static inline void Marking(Tagged<HeapObject> host, ObjectSlot,
                             Tagged<Object> value);
  static inline void Marking(Tagged<HeapObject> host, HeapObjectSlot,
                             Tagged<HeapObject> value);
  static inline void Marking(Tagged<HeapObject> host, MaybeObjectSlot,
                             Tagged<MaybeObject> value);
  static inline void Marking(Tagged<InstructionStream> host, RelocInfo*,
                             Tagged<HeapObject> value);
  static inline void Marking(Tagged<JSArrayBuffer> host, ArrayBufferExtension*);
  static inline void Marking(Tagged<DescriptorArray>,
                             int number_of_own_descriptors);
  static inline void Marking(Tagged<HeapObject> host, IndirectPointerSlot slot);
  static inline void Marking(Tagged<TrustedObject> host,
                             ProtectedPointerSlot slot,
                             Tagged<TrustedObject> value);
  static inline void Marking(Tagged<HeapObject> host, JSDispatchHandle handle);

  static inline void Shared(Tagged<InstructionStream> host, RelocInfo*,
                            Tagged<HeapObject> value);
  static void Shared(Tagged<TrustedObject> host, ProtectedPointerSlot slot,
                     Tagged<TrustedObject> value);

  // It is invoked from generated code and has to take raw addresses.
  static int MarkingFromCode(Address raw_host, Address raw_slot);
  static int IndirectPointerMarkingFromCode(Address raw_host, Address raw_slot,
                                            Address raw_tag);
  static int SharedMarkingFromCode(Address raw_host, Address raw_slot);
  static int SharedFromCode(Address raw_host, Address raw_slot);

  // Invoked from global handles where no host object is available.
  static inline void MarkingFromGlobalHandle(Tagged<Object> value);

  static inline void CombinedBarrierForCppHeapPointer(Tagged<JSObject> host,
                                                      void* value);

  static MarkingBarrier* SetForThread(MarkingBarrier*);

  static MarkingBarrier* CurrentMarkingBarrier(
      Tagged<HeapObject> verification_candidate);

#ifdef ENABLE_SLOW_DCHECKS
  template <typename T>
  static inline bool IsRequired(Tagged<HeapObject> host, T value);
  template <typename T>
  static inline bool IsRequired(const HeapObjectLayout* host, T value);
  static bool IsImmortalImmovableHeapObject(Tagged<HeapObject> object);
#endif

  static void MarkingSlow(Tagged<HeapObject> host, HeapObjectSlot,
                          Tagged<HeapObject> value);

 private:
  static inline bool IsMarking(Tagged<HeapObject> object);

  static void MarkingSlow(Tagged<InstructionStream> host, RelocInfo*,
                          Tagged<HeapObject> value);
  static void MarkingSlow(Tagged<JSArrayBuffer> host, ArrayBufferExtension*);
  static void MarkingSlow(Tagged<DescriptorArray>,
                          int number_of_own_descriptors);
  static void MarkingSlow(Tagged<HeapObject> host, IndirectPointerSlot slot);
  static void MarkingSlow(Tagged<TrustedObject> host, ProtectedPointerSlot slot,
                          Tagged<TrustedObject> value);
  static void MarkingSlow(Tagged<HeapObject> host, JSDispatchHandle handle);
  static void MarkingSlowFromGlobalHandle(Tagged<HeapObject> value);
  static void MarkingSlowFromCppHeapWrappable(Heap* heap, void* object);

  static inline void GenerationalBarrierForCppHeapPointer(Tagged<JSObject> host,
                                                          void* value);

  static void SharedSlow(Tagged<InstructionStream> host, RelocInfo*,
                         Tagged<HeapObject> value);

  friend class Heap;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_WRITE_BARRIER_H_
