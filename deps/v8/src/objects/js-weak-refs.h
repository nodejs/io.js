// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_WEAK_REFS_H_
#define V8_OBJECTS_JS_WEAK_REFS_H_

#include "src/objects/js-objects.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class NativeContext;
class WeakCell;

#include "torque-generated/src/objects/js-weak-refs-tq.inc"

// FinalizationRegistry object from the JS Weak Refs spec proposal:
// https://github.com/tc39/proposal-weakrefs
class JSFinalizationRegistry
    : public TorqueGeneratedJSFinalizationRegistry<JSFinalizationRegistry,
                                                   JSObject> {
 public:
  DECL_PRINTER(JSFinalizationRegistry)
  EXPORT_DECL_VERIFIER(JSFinalizationRegistry)

  DECL_BOOLEAN_ACCESSORS(scheduled_for_cleanup)

  class BodyDescriptor;

  inline static void RegisterWeakCellWithUnregisterToken(
      DirectHandle<JSFinalizationRegistry> finalization_registry,
      DirectHandle<WeakCell> weak_cell, Isolate* isolate);
  inline static bool Unregister(
      DirectHandle<JSFinalizationRegistry> finalization_registry,
      DirectHandle<HeapObject> unregister_token, Isolate* isolate);

  // RemoveUnregisterToken is called from both Unregister and during GC. Since
  // it modifies slots in key_map and WeakCells and the normal write barrier is
  // disabled during GC, we need to tell the GC about the modified slots via the
  // gc_notify_updated_slot function.
  enum RemoveUnregisterTokenMode {
    kRemoveMatchedCellsFromRegistry,
    kKeepMatchedCellsInRegistry
  };
  template <typename GCNotifyUpdatedSlotCallback>
  inline bool RemoveUnregisterToken(
      Tagged<HeapObject> unregister_token, Isolate* isolate,
      RemoveUnregisterTokenMode removal_mode,
      GCNotifyUpdatedSlotCallback gc_notify_updated_slot);

  // Returns true if the cleared_cells list is non-empty.
  inline bool NeedsCleanup() const;

  V8_EXPORT_PRIVATE Tagged<WeakCell> PopClearedCell(
      Isolate* isolate, bool* key_map_may_need_shrink);

  static void ShrinkKeyMap(
      Isolate* isolate,
      DirectHandle<JSFinalizationRegistry> finalization_registry);

  // Pop cleared cells and call their finalizers.
  static Maybe<bool> Cleanup(
      Isolate* isolate,
      DirectHandle<JSFinalizationRegistry> finalization_registry);

  // Remove the already-popped weak_cell from its unregister token linked list,
  // as well as removing the entry from the key map if it is the only WeakCell
  // with its unregister token. This method cannot GC and does not shrink the
  // key map. Asserts that weak_cell has a non-undefined unregister token.
  V8_EXPORT_PRIVATE void RemoveCellFromUnregisterTokenMap(
      Isolate* isolate, Tagged<WeakCell> weak_cell);

  // Bitfields in flags.
  DEFINE_TORQUE_GENERATED_FINALIZATION_REGISTRY_FLAGS()

  TQ_OBJECT_CONSTRUCTORS(JSFinalizationRegistry)
};

// Internal object for storing weak references in JSFinalizationRegistry.
class WeakCell : public TorqueGeneratedWeakCell<WeakCell, HeapObject> {
 public:
  EXPORT_DECL_VERIFIER(WeakCell)

  class BodyDescriptor;

  // Provide relaxed load access to target field.
  inline Tagged<HeapObject> relaxed_target() const;

  // Provide relaxed load access to the unregister token field.
  inline Tagged<HeapObject> relaxed_unregister_token() const;

  // Nullify is called during GC and it modifies the pointers in WeakCell and
  // JSFinalizationRegistry. Thus we need to tell the GC about the modified
  // slots via the gc_notify_updated_slot function. The normal write barrier is
  // not enough, since it's disabled before GC.
  template <typename GCNotifyUpdatedSlotCallback>
  inline void Nullify(Isolate* isolate,
                      GCNotifyUpdatedSlotCallback gc_notify_updated_slot);

  inline void RemoveFromFinalizationRegistryCells(Isolate* isolate);

  TQ_OBJECT_CONSTRUCTORS(WeakCell)
};

class JSWeakRef : public TorqueGeneratedJSWeakRef<JSWeakRef, JSObject> {
 public:
  DECL_PRINTER(JSWeakRef)
  EXPORT_DECL_VERIFIER(JSWeakRef)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSWeakRef)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_WEAK_REFS_H_
