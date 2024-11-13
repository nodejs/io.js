// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/weak-object-worklists.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/objects/hash-table.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-function.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/js-weak-refs.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/transitions.h"

namespace v8 {

namespace internal {

WeakObjects::Local::Local(WeakObjects* weak_objects)
    : WeakObjects::UnusedBase()
#define INIT_LOCAL_WORKLIST(_, name, __) , name##_local(weak_objects->name)
          WEAK_OBJECT_WORKLISTS(INIT_LOCAL_WORKLIST)
#undef INIT_LOCAL_WORKLIST
{
}

void WeakObjects::Local::Publish() {
#define INVOKE_PUBLISH(_, name, __) name##_local.Publish();
  WEAK_OBJECT_WORKLISTS(INVOKE_PUBLISH)
#undef INVOKE_PUBLISH
}

void WeakObjects::UpdateAfterScavenge() {
#define INVOKE_UPDATE(_, name, Name) Update##Name(name);
  WEAK_OBJECT_WORKLISTS(INVOKE_UPDATE)
#undef INVOKE_UPDATE
}

void WeakObjects::Clear() {
#define INVOKE_CLEAR(_, name, __) name.Clear();
  WEAK_OBJECT_WORKLISTS(INVOKE_CLEAR)
#undef INVOKE_CLEAR
}

// static
void WeakObjects::UpdateTransitionArrays(
    WeakObjectWorklist<Tagged<TransitionArray>>& transition_arrays) {
  DCHECK(!ContainsYoungObjects(transition_arrays));
}

// static
void WeakObjects::UpdateEphemeronHashTables(
    WeakObjectWorklist<Tagged<EphemeronHashTable>>& ephemeron_hash_tables) {
  ephemeron_hash_tables.Update(
      [](Tagged<EphemeronHashTable> slot_in,
         Tagged<EphemeronHashTable>* slot_out) -> bool {
        Tagged<EphemeronHashTable> forwarded = ForwardingAddress(slot_in);

        if (!forwarded.is_null()) {
          *slot_out = forwarded;
          return true;
        }

        return false;
      });
}

namespace {
bool EphemeronUpdater(Ephemeron slot_in, Ephemeron* slot_out) {
  Tagged<HeapObject> key = slot_in.key;
  Tagged<HeapObject> value = slot_in.value;
  Tagged<HeapObject> forwarded_key = ForwardingAddress(key);
  Tagged<HeapObject> forwarded_value = ForwardingAddress(value);

  if (!forwarded_key.is_null() && !forwarded_value.is_null()) {
    *slot_out = Ephemeron{forwarded_key, forwarded_value};
    return true;
  }

  return false;
}
}  // anonymous namespace

// static
void WeakObjects::UpdateCurrentEphemerons(
    WeakObjectWorklist<Ephemeron>& current_ephemerons) {
  current_ephemerons.Update(EphemeronUpdater);
}

// static
void WeakObjects::UpdateNextEphemerons(
    WeakObjectWorklist<Ephemeron>& next_ephemerons) {
  next_ephemerons.Update(EphemeronUpdater);
}

// static
void WeakObjects::UpdateDiscoveredEphemerons(
    WeakObjectWorklist<Ephemeron>& discovered_ephemerons) {
  discovered_ephemerons.Update(EphemeronUpdater);
}

namespace {
void UpdateWeakReferencesHelper(
    WeakObjects::WeakObjectWorklist<HeapObjectAndSlot>& weak_references) {
  weak_references.Update(
      [](HeapObjectAndSlot slot_in, HeapObjectAndSlot* slot_out) -> bool {
        Tagged<HeapObject> heap_obj = slot_in.heap_object;
        Tagged<HeapObject> forwarded = ForwardingAddress(heap_obj);

        if (!forwarded.is_null()) {
          ptrdiff_t distance_to_slot =
              slot_in.slot.address() - slot_in.heap_object.ptr();
          Address new_slot = forwarded.ptr() + distance_to_slot;
          slot_out->heap_object = forwarded;
          slot_out->slot = HeapObjectSlot(new_slot);
          return true;
        }

        return false;
      });
}
}  // anonymous namespace

// static
void WeakObjects::UpdateWeakReferencesTrivial(
    WeakObjectWorklist<HeapObjectAndSlot>& weak_references) {
  UpdateWeakReferencesHelper(weak_references);
}

// static
void WeakObjects::UpdateWeakReferencesNonTrivial(
    WeakObjectWorklist<HeapObjectAndSlot>& weak_references) {
  UpdateWeakReferencesHelper(weak_references);
}

// static
void WeakObjects::UpdateWeakReferencesNonTrivialUnmarked(
    WeakObjectWorklist<HeapObjectAndSlot>& weak_references) {
  UpdateWeakReferencesHelper(weak_references);
}

// static
void WeakObjects::UpdateWeakObjectsInCode(
    WeakObjectWorklist<HeapObjectAndCode>& weak_objects_in_code) {
  weak_objects_in_code.Update(
      [](HeapObjectAndCode slot_in, HeapObjectAndCode* slot_out) -> bool {
        Tagged<HeapObject> heap_obj = slot_in.heap_object;
        Tagged<HeapObject> forwarded = ForwardingAddress(heap_obj);

        if (!forwarded.is_null()) {
          slot_out->heap_object = forwarded;
          slot_out->code = slot_in.code;
          return true;
        }

        return false;
      });
}

// static
void WeakObjects::UpdateJSWeakRefs(
    WeakObjectWorklist<Tagged<JSWeakRef>>& js_weak_refs) {
  js_weak_refs.Update([](Tagged<JSWeakRef> js_weak_ref_in,
                         Tagged<JSWeakRef>* js_weak_ref_out) -> bool {
    Tagged<JSWeakRef> forwarded = ForwardingAddress(js_weak_ref_in);

    if (!forwarded.is_null()) {
      *js_weak_ref_out = forwarded;
      return true;
    }

    return false;
  });
}

// static
void WeakObjects::UpdateWeakCells(
    WeakObjectWorklist<Tagged<WeakCell>>& weak_cells) {
  // TODO(syg, marja): Support WeakCells in the young generation.
  DCHECK(!ContainsYoungObjects(weak_cells));
}

// static
void WeakObjects::UpdateCodeFlushingCandidates(
    WeakObjectWorklist<Tagged<SharedFunctionInfo>>& code_flushing_candidates) {
  DCHECK(!ContainsYoungObjects(code_flushing_candidates));
}

// static
void WeakObjects::UpdateFlushedJSFunctions(
    WeakObjectWorklist<Tagged<JSFunction>>& flushed_js_functions) {
  flushed_js_functions.Update(
      [](Tagged<JSFunction> slot_in, Tagged<JSFunction>* slot_out) -> bool {
        Tagged<JSFunction> forwarded = ForwardingAddress(slot_in);

        if (!forwarded.is_null()) {
          *slot_out = forwarded;
          return true;
        }

        return false;
      });
}

// static
void WeakObjects::UpdateBaselineFlushingCandidates(
    WeakObjectWorklist<Tagged<JSFunction>>& baseline_flush_candidates) {
  baseline_flush_candidates.Update(
      [](Tagged<JSFunction> slot_in, Tagged<JSFunction>* slot_out) -> bool {
        Tagged<JSFunction> forwarded = ForwardingAddress(slot_in);

        if (!forwarded.is_null()) {
          *slot_out = forwarded;
          return true;
        }

        return false;
      });
}

#ifdef DEBUG
// static
template <typename Type>
bool WeakObjects::ContainsYoungObjects(
    WeakObjectWorklist<Tagged<Type>>& worklist) {
  bool result = false;
  worklist.Iterate([&result](Tagged<Type> candidate) {
    if (Heap::InYoungGeneration(candidate)) {
      result = true;
    }
  });
  return result;
}
#endif

}  // namespace internal
}  // namespace v8
