// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROTOTYPE_INFO_H_
#define V8_OBJECTS_PROTOTYPE_INFO_H_

#include "src/objects/fixed-array.h"
#include "src/objects/objects.h"
#include "src/objects/struct.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/prototype-info-tq.inc"

// Container for metadata stored on each prototype map.
class PrototypeInfo
    : public TorqueGeneratedPrototypeInfo<PrototypeInfo, Struct> {
 public:
  static const int UNREGISTERED = -1;

  // For caching derived maps for Object.create, Reflect.construct and proxies.
  DECL_GETTER(derived_maps, Tagged<HeapObject>)
  DECL_RELEASE_ACQUIRE_ACCESSORS(derived_maps, Tagged<HeapObject>)

  static inline void SetObjectCreateMap(Handle<PrototypeInfo> info,
                                        Handle<Map> map, Isolate* isolate);
  inline Tagged<MaybeObject> ObjectCreateMap(AcquireLoadTag);
  inline Tagged<MaybeObject> ObjectCreateMap();

  static inline void AddDerivedMap(Handle<PrototypeInfo> info, Handle<Map> to,
                                   Isolate* isolate);
  inline Tagged<MaybeObject> GetDerivedMap(Handle<Map> from);

  static inline bool IsPrototypeInfoFast(Tagged<Object> object);

  DECL_BOOLEAN_ACCESSORS(should_be_fast_map)

  // Dispatched behavior.
  DECL_PRINTER(PrototypeInfo)
  DECL_VERIFIER(PrototypeInfo)

  // Bit field usage.
  DEFINE_TORQUE_GENERATED_PROTOTYPE_INFO_FLAGS()

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(PrototypeInfo)
};

// A growing array with an additional API for marking slots "empty". When adding
// new elements, we reuse the empty slots instead of growing the array.
class V8_EXPORT_PRIVATE PrototypeUsers : public WeakArrayList {
 public:
  static Handle<WeakArrayList> Add(Isolate* isolate,
                                   Handle<WeakArrayList> array,
                                   Handle<Map> value, int* assigned_index);

  static inline void MarkSlotEmpty(Tagged<WeakArrayList> array, int index);

  // The callback is called when a weak pointer to HeapObject "object" is moved
  // from index "from_index" to index "to_index" during compaction. The callback
  // must not cause GC.
  using CompactionCallback = void (*)(Tagged<HeapObject> object, int from_index,
                                      int to_index);
  static Tagged<WeakArrayList> Compact(
      Handle<WeakArrayList> array, Heap* heap, CompactionCallback callback,
      AllocationType allocation = AllocationType::kYoung);

#ifdef VERIFY_HEAP
  static void Verify(Tagged<WeakArrayList> array);
#endif  // VERIFY_HEAP

  static const int kEmptySlotIndex = 0;
  static const int kFirstIndex = 1;

  static const int kNoEmptySlotsMarker = 0;

 private:
  static inline Tagged<Smi> empty_slot_index(Tagged<WeakArrayList> array);
  static inline void set_empty_slot_index(Tagged<WeakArrayList> array,
                                          int index);

  static void ScanForEmptySlots(Tagged<WeakArrayList> array);

  DISALLOW_IMPLICIT_CONSTRUCTORS(PrototypeUsers);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_PROTOTYPE_INFO_H_
