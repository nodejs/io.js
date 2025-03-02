// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-weak-refs.h"

#include "src/execution/execution.h"
#include "src/objects/js-weak-refs-inl.h"

namespace v8 {
namespace internal {

Tagged<WeakCell> JSFinalizationRegistry::PopClearedCell(
    Isolate* isolate, bool* key_map_may_need_shrink) {
  DisallowGarbageCollection no_gc;
  Tagged<Undefined> undefined = ReadOnlyRoots(isolate).undefined_value();

  Tagged<WeakCell> head = Cast<WeakCell>(cleared_cells());
  DCHECK(IsUndefined(head->prev(), isolate));
  Tagged<Union<Undefined, WeakCell>> tail = head->next();
  head->set_next(undefined);
  if (IsWeakCell(tail)) Cast<WeakCell>(tail)->set_prev(undefined);
  set_cleared_cells(tail);

  // If the WeakCell has an unregister token, remove the cell from the
  // unregister token linked lists and and the unregister token from key_map.
  // This doesn't shrink key_map, which is done manually after the cleanup loop.
  if (!IsUndefined(head->unregister_token(), isolate)) {
    RemoveCellFromUnregisterTokenMap(isolate, head);
    *key_map_may_need_shrink = true;
  }

  return head;
}

void JSFinalizationRegistry::ShrinkKeyMap(
    Isolate* isolate,
    DirectHandle<JSFinalizationRegistry> finalization_registry) {
  if (!IsUndefined(finalization_registry->key_map(), isolate)) {
    Handle<SimpleNumberDictionary> key_map =
        handle(Cast<SimpleNumberDictionary>(finalization_registry->key_map()),
               isolate);
    key_map = SimpleNumberDictionary::Shrink(isolate, key_map);
    finalization_registry->set_key_map(*key_map);
  }
}

// ES#sec-cleanup-finalization-registry
// static
Maybe<bool> JSFinalizationRegistry::Cleanup(
    Isolate* isolate,
    DirectHandle<JSFinalizationRegistry> finalization_registry) {
  // 1. Assert: finalizationRegistry has [[Cells]] and [[CleanupCallback]]
  //    internal slots.
  // (By construction.)

  // 2. Let callback be finalizationRegistry.[[CleanupCallback]].
  DirectHandle<Object> callback(finalization_registry->cleanup(), isolate);

  // 3. While finalizationRegistry.[[Cells]] contains a Record cell such that
  //    cell.[[WeakRefTarget]] is empty, an implementation may perform the
  //    following steps:
  bool key_map_may_need_shrink = false;
  while (finalization_registry->NeedsCleanup()) {
    HandleScope scope(isolate);

    // a. Choose any such cell.
    // b. Remove cell from finalizationRegistry.[[Cells]].
    DirectHandle<WeakCell> weak_cell(finalization_registry->PopClearedCell(
                                         isolate, &key_map_may_need_shrink),
                                     isolate);

    // c. Perform ? HostCallJobCallback(callback, undefined,
    //    « cell.[[HeldValue]] »).
    DirectHandle<Object> args[] = {
        direct_handle(weak_cell->holdings(), isolate)};
    if (Execution::Call(isolate, callback,
                        isolate->factory()->undefined_value(),
                        base::VectorOf(args))
            .is_null()) {
      if (key_map_may_need_shrink) ShrinkKeyMap(isolate, finalization_registry);
      return Nothing<bool>();
    }
  }

  if (key_map_may_need_shrink) ShrinkKeyMap(isolate, finalization_registry);
  return Just(true);
}

void JSFinalizationRegistry::RemoveCellFromUnregisterTokenMap(
    Isolate* isolate, Tagged<WeakCell> weak_cell) {
  DisallowGarbageCollection no_gc;
  DCHECK(!IsUndefined(weak_cell->unregister_token(), isolate));
  Tagged<Undefined> undefined = ReadOnlyRoots(isolate).undefined_value();

  // Remove weak_cell from the linked list of other WeakCells with the same
  // unregister token and remove its unregister token from key_map if necessary
  // without shrinking it. Since shrinking may allocate, it is performed by the
  // caller after looping, or on exception.
  if (IsUndefined(weak_cell->key_list_prev(), isolate)) {
    Tagged<SimpleNumberDictionary> key_map =
        Cast<SimpleNumberDictionary>(this->key_map());
    Tagged<HeapObject> unregister_token = weak_cell->unregister_token();
    uint32_t key = Smi::ToInt(Object::GetHash(unregister_token));
    InternalIndex entry = key_map->FindEntry(isolate, key);
    CHECK(entry.is_found());

    if (IsUndefined(weak_cell->key_list_next(), isolate)) {
      // weak_cell is the only one associated with its key; remove the key
      // from the hash table.
      key_map->ClearEntry(entry);
      key_map->ElementRemoved();
    } else {
      // weak_cell is the list head for its key; we need to change the value
      // of the key in the hash table.
      Tagged<WeakCell> next = Cast<WeakCell>(weak_cell->key_list_next());
      DCHECK_EQ(next->key_list_prev(), weak_cell);
      next->set_key_list_prev(undefined);
      key_map->ValueAtPut(entry, next);
    }
  } else {
    // weak_cell is somewhere in the middle of its key list.
    Tagged<WeakCell> prev = Cast<WeakCell>(weak_cell->key_list_prev());
    prev->set_key_list_next(weak_cell->key_list_next());
    if (!IsUndefined(weak_cell->key_list_next())) {
      Tagged<WeakCell> next = Cast<WeakCell>(weak_cell->key_list_next());
      next->set_key_list_prev(weak_cell->key_list_prev());
    }
  }

  // weak_cell is now removed from the unregister token map, so clear its
  // unregister token-related fields.
  weak_cell->set_unregister_token(undefined);
  weak_cell->set_key_list_prev(undefined);
  weak_cell->set_key_list_next(undefined);
}

}  // namespace internal
}  // namespace v8
