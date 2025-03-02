// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_WRITE_BARRIER_H_
#define V8_HEAP_HEAP_WRITE_BARRIER_H_

// Clients of this interface shouldn't depend on lots of heap internals.
// Do not include anything from src/heap here!

#include "include/v8-internal.h"
#include "src/common/globals.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

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

// Write barrier interface. It's preferred to use the macros defined in
// `object-macros.h`.
//
// Refer to the `ForFoo()` versions which will dispatch to all relevant barriers
// instead of emiting marking, compaction, generational, and shared barriers
// separately.
class V8_EXPORT_PRIVATE WriteBarrier final {
 public:
  // Trampolines for generated code. Have to take raw addresses.
  static void EphemeronKeyWriteBarrierFromCode(Address raw_object,
                                               Address key_slot_address,
                                               Isolate* isolate);
  static int MarkingFromCode(Address raw_host, Address raw_slot);
  static int IndirectPointerMarkingFromCode(Address raw_host, Address raw_slot,
                                            Address raw_tag);
  static int SharedMarkingFromCode(Address raw_host, Address raw_slot);
  static int SharedFromCode(Address raw_host, Address raw_slot);

  static inline WriteBarrierMode GetWriteBarrierModeForObject(
      Tagged<HeapObject> object, const DisallowGarbageCollection& promise);

  template <typename T>
  static inline void ForValue(Tagged<HeapObject> host, MaybeObjectSlot slot,
                              Tagged<T> value, WriteBarrierMode mode);
  template <typename T>
  static inline void ForValue(HeapObjectLayout* host, TaggedMemberBase* slot,
                              Tagged<T> value, WriteBarrierMode mode);
  static inline void ForEphemeronHashTable(Tagged<EphemeronHashTable> host,
                                           ObjectSlot slot,
                                           Tagged<Object> value,
                                           WriteBarrierMode mode);
  static inline void ForRelocInfo(Tagged<InstructionStream> host,
                                  RelocInfo* rinfo, Tagged<HeapObject> value,
                                  WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  static inline void ForDescriptorArray(Tagged<DescriptorArray>,
                                        int number_of_own_descriptors);
  static inline void ForArrayBufferExtension(Tagged<JSArrayBuffer> host,
                                             ArrayBufferExtension* extension);
  static inline void ForExternalPointer(
      Tagged<HeapObject> host, ExternalPointerSlot slot,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  static inline void ForIndirectPointer(
      Tagged<HeapObject> host, IndirectPointerSlot slot,
      Tagged<HeapObject> value, WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  static inline void ForProtectedPointer(
      Tagged<TrustedObject> host, ProtectedPointerSlot slot,
      Tagged<TrustedObject> value,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  static inline void ForCppHeapPointer(Tagged<JSObject> host,
                                       CppHeapPointerSlot slot, void* value);
  static inline void ForJSDispatchHandle(
      Tagged<HeapObject> host, JSDispatchHandle handle,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  // Executes generational and/or marking write barrier for a [start, end) range
  // of non-weak slots inside |object|.
  template <typename TSlot>
  static void ForRange(Heap* heap, Tagged<HeapObject> object, TSlot start,
                       TSlot end);

  static MarkingBarrier* SetForThread(MarkingBarrier* marking_barrier);
  static MarkingBarrier* CurrentMarkingBarrier(
      Tagged<HeapObject> verification_candidate);

  // Invoked from traced handles where no host object is available.
  static inline void MarkingFromTracedHandle(Tagged<Object> value);

  static inline void GenerationalForRelocInfo(Tagged<InstructionStream> host,
                                              RelocInfo* rinfo,
                                              Tagged<HeapObject> object);
  static inline void SharedForRelocInfo(Tagged<InstructionStream> host,
                                        RelocInfo*, Tagged<HeapObject> value);

  static inline void MarkingForTesting(Tagged<HeapObject> host, ObjectSlot,
                                       Tagged<Object> value);

#ifdef ENABLE_SLOW_DCHECKS
  template <typename T>
  static inline bool IsRequired(Tagged<HeapObject> host, T value);
  template <typename T>
  static inline bool IsRequired(const HeapObjectLayout* host, T value);
  static bool VerifyDispatchHandleMarkingState(Tagged<HeapObject> host,
                                               JSDispatchHandle value,
                                               WriteBarrierMode mode);
#endif

 private:
  static bool PageFlagsAreConsistent(Tagged<HeapObject> object);

  static inline bool IsImmortalImmovableHeapObject(Tagged<HeapObject> object);

  static inline bool IsMarking(Tagged<HeapObject> object);

  static inline void Marking(Tagged<HeapObject> host, HeapObjectSlot,
                             Tagged<HeapObject> value);
  static inline void Marking(Tagged<HeapObject> host, MaybeObjectSlot,
                             Tagged<MaybeObject> value);
  static inline void MarkingForRelocInfo(Tagged<InstructionStream> host,
                                         RelocInfo*, Tagged<HeapObject> value);
  static inline void Marking(Tagged<HeapObject> host, ExternalPointerSlot slot);
  static inline void Marking(Tagged<HeapObject> host, IndirectPointerSlot slot);
  static inline void Marking(Tagged<TrustedObject> host,
                             ProtectedPointerSlot slot,
                             Tagged<TrustedObject> value);
  static inline void Marking(Tagged<HeapObject> host, JSDispatchHandle handle);

  static void MarkingSlow(Tagged<HeapObject> host, HeapObjectSlot,
                          Tagged<HeapObject> value);
  static void MarkingSlow(Tagged<InstructionStream> host, RelocInfo*,
                          Tagged<HeapObject> value);
  static void MarkingSlow(Tagged<JSArrayBuffer> host, ArrayBufferExtension*);
  static void MarkingSlow(Tagged<DescriptorArray>,
                          int number_of_own_descriptors);
  static void MarkingSlow(Tagged<HeapObject> host, ExternalPointerSlot slot);
  static void MarkingSlow(Tagged<HeapObject> host, IndirectPointerSlot slot);
  static void MarkingSlow(Tagged<TrustedObject> host, ProtectedPointerSlot slot,
                          Tagged<TrustedObject> value);
  static void MarkingSlow(Tagged<HeapObject> host, JSDispatchHandle handle);
  static void MarkingSlowFromTracedHandle(Tagged<HeapObject> value);
  static void MarkingSlowFromCppHeapWrappable(Heap* heap, Tagged<JSObject> host,
                                              CppHeapPointerSlot slot,
                                              void* object);

  static void GenerationalBarrierSlow(Tagged<HeapObject> object, Address slot,
                                      Tagged<HeapObject> value);
  static inline void GenerationalBarrierForCppHeapPointer(Tagged<JSObject> host,
                                                          void* value);

  static void SharedSlow(Tagged<TrustedObject> host, ProtectedPointerSlot slot,
                         Tagged<TrustedObject> value);
  static void SharedSlow(Tagged<InstructionStream> host, RelocInfo*,
                         Tagged<HeapObject> value);
  static void SharedHeapBarrierSlow(Tagged<HeapObject> object, Address slot);

  static inline void CombinedWriteBarrierInternal(Tagged<HeapObject> host,
                                                  HeapObjectSlot slot,
                                                  Tagged<HeapObject> value,
                                                  WriteBarrierMode mode);

  static void CombinedGenerationalAndSharedBarrierSlow(
      Tagged<HeapObject> object, Address slot, Tagged<HeapObject> value);
  static void CombinedGenerationalAndSharedEphemeronBarrierSlow(
      Tagged<EphemeronHashTable> table, Address slot, Tagged<HeapObject> value);
  static void GenerationalBarrierForCodeSlow(Tagged<InstructionStream> host,
                                             RelocInfo* rinfo,
                                             Tagged<HeapObject> value);
};

}  // namespace v8::internal

#endif  // V8_HEAP_HEAP_WRITE_BARRIER_H_
