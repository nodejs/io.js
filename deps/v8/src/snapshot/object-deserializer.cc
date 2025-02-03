// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/object-deserializer.h"

#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"
#include "src/heap/local-factory-inl.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/objects.h"
#include "src/snapshot/code-serializer.h"

namespace v8 {
namespace internal {

ObjectDeserializer::ObjectDeserializer(Isolate* isolate,
                                       const SerializedCodeData* data)
    : Deserializer(isolate, data->Payload(), data->GetMagicNumber(), true,
                   false) {}

MaybeHandle<SharedFunctionInfo>
ObjectDeserializer::DeserializeSharedFunctionInfo(
    Isolate* isolate, const SerializedCodeData* data, Handle<String> source) {
  ObjectDeserializer d(isolate, data);

  d.AddAttachedObject(source);

  Handle<HeapObject> result;
  return d.Deserialize().ToHandle(&result) ? Cast<SharedFunctionInfo>(result)
                                           : MaybeHandle<SharedFunctionInfo>();
}

MaybeHandle<HeapObject> ObjectDeserializer::Deserialize() {
  DCHECK(deserializing_user_code());
  HandleScope scope(isolate());
  Handle<HeapObject> result;
  {
    result = ReadObject();
    DeserializeDeferredObjects();
    CHECK(new_code_objects().empty());
    LinkAllocationSites();
    CHECK(new_maps().empty());
    WeakenDescriptorArrays();
  }

  Rehash();
  CommitPostProcessedObjects();
  return scope.CloseAndEscape(result);
}

void ObjectDeserializer::CommitPostProcessedObjects() {
  for (Handle<Script> script : new_scripts()) {
    // Assign a new script id to avoid collision.
    script->set_id(isolate()->GetNextScriptId());
    LogScriptEvents(*script);
    // Add script to list.
    Handle<WeakArrayList> list = isolate()->factory()->script_list();
    list = WeakArrayList::AddToEnd(isolate(), list,
                                   MaybeObjectHandle::Weak(script));
    isolate()->heap()->SetRootScriptList(*list);
  }
}

void ObjectDeserializer::LinkAllocationSites() {
  DisallowGarbageCollection no_gc;
  Heap* heap = isolate()->heap();
  // Allocation sites are present in the snapshot, and must be linked into
  // a list at deserialization time.
  for (DirectHandle<AllocationSite> site : new_allocation_sites()) {
    if (!site->HasWeakNext()) continue;
    // TODO(mvstanton): consider treating the heap()->allocation_sites_list()
    // as a (weak) root. If this root is relocated correctly, this becomes
    // unnecessary.
    if (heap->allocation_sites_list() == Smi::zero()) {
      site->set_weak_next(ReadOnlyRoots(heap).undefined_value());
    } else {
      site->set_weak_next(heap->allocation_sites_list());
    }
    heap->set_allocation_sites_list(*site);
  }
}

OffThreadObjectDeserializer::OffThreadObjectDeserializer(
    LocalIsolate* isolate, const SerializedCodeData* data)
    : Deserializer(isolate, data->Payload(), data->GetMagicNumber(), true,
                   false) {}

MaybeHandle<SharedFunctionInfo>
OffThreadObjectDeserializer::DeserializeSharedFunctionInfo(
    LocalIsolate* isolate, const SerializedCodeData* data,
    std::vector<Handle<Script>>* deserialized_scripts) {
  OffThreadObjectDeserializer d(isolate, data);

  // Attach the empty string as the source.
  d.AddAttachedObject(isolate->factory()->empty_string());

  Handle<HeapObject> result;
  if (!d.Deserialize(deserialized_scripts).ToHandle(&result)) {
    return MaybeHandle<SharedFunctionInfo>();
  }
  return Cast<SharedFunctionInfo>(result);
}

MaybeHandle<HeapObject> OffThreadObjectDeserializer::Deserialize(
    std::vector<Handle<Script>>* deserialized_scripts) {
  DCHECK(deserializing_user_code());
  LocalHandleScope scope(isolate());
  Handle<HeapObject> result;
  {
    result = ReadObject();
    DeserializeDeferredObjects();
    CHECK(new_code_objects().empty());
    CHECK(new_allocation_sites().empty());
    CHECK(new_maps().empty());
    WeakenDescriptorArrays();
  }

  Rehash();

  // TODO(leszeks): Figure out a better way of dealing with scripts.
  CHECK_EQ(new_scripts().size(), 1);
  for (Handle<Script> script : new_scripts()) {
    // Assign a new script id to avoid collision.
    script->set_id(isolate()->GetNextScriptId());
    LogScriptEvents(*script);
    deserialized_scripts->push_back(
        isolate()->heap()->NewPersistentHandle(script));
  }

  return scope.CloseAndEscape(result);
}

}  // namespace internal
}  // namespace v8
