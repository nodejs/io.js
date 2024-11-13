// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/serializer-deserializer.h"

#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

namespace {
DISABLE_CFI_PERF
void IterateObjectCache(Isolate* isolate, std::vector<Tagged<Object>>* cache,
                        Root root_id, RootVisitor* visitor) {
  for (size_t i = 0;; ++i) {
    // Extend the array ready to get a value when deserializing.
    if (cache->size() <= i) cache->push_back(Smi::zero());
    // During deserialization, the visitor populates the object cache and
    // eventually terminates the cache with undefined.
    visitor->VisitRootPointer(root_id, nullptr, FullObjectSlot(&cache->at(i)));
    // We may see objects in trusted space here (outside of the main pointer
    // compression cage), so have to use SafeEquals.
    Tagged<Object> undefined = ReadOnlyRoots(isolate).undefined_value();
    if (cache->at(i).SafeEquals(undefined)) break;
  }
}
}  // namespace

// The startup and shared heap object caches are terminated by undefined. We
// visit these caches...
//  - during deserialization to populate it.
//  - during normal GC to keep its content alive.
//  - not during serialization. The context serializer adds to it explicitly.
void SerializerDeserializer::IterateStartupObjectCache(Isolate* isolate,
                                                       RootVisitor* visitor) {
  IterateObjectCache(isolate, isolate->startup_object_cache(),
                     Root::kStartupObjectCache, visitor);
}

void SerializerDeserializer::IterateSharedHeapObjectCache(
    Isolate* isolate, RootVisitor* visitor) {
  IterateObjectCache(isolate, isolate->shared_heap_object_cache(),
                     Root::kSharedHeapObjectCache, visitor);
}

bool SerializerDeserializer::CanBeDeferred(Tagged<HeapObject> o,
                                           SlotType slot_type) {
  // HeapObjects' map slots cannot be deferred as objects are expected to have a
  // valid map immediately.
  if (slot_type == SlotType::kMapSlot) {
    DCHECK(IsMap(o));
    return false;
  }
  // * Internalized strings cannot be deferred as they might be
  //   converted to thin strings during post processing, at which point forward
  //   references to the now-thin string will already have been written.
  // * JS objects with embedder fields cannot be deferred because the
  //   serialize/deserialize callbacks need the back reference immediately to
  //   identify the object.
  // * ByteArray cannot be deferred as JSTypedArray needs the base_pointer
  //   ByteArray immediately if it's on heap.
  // * Non-empty EmbdderDataArrays cannot be deferred because the serialize
  //   and deserialize callbacks need the back reference immediately to
  //   identify the object.
  // TODO(leszeks): Could we defer string serialization if forward references
  // were resolved after object post processing?
  return !IsInternalizedString(o) &&
         !(IsJSObject(o) && Cast<JSObject>(o)->GetEmbedderFieldCount() > 0) &&
         !IsByteArray(o) &&
         !(IsEmbedderDataArray(o) && Cast<EmbedderDataArray>(o)->length() > 0);
}

void SerializerDeserializer::RestoreExternalReferenceRedirector(
    Isolate* isolate, Tagged<AccessorInfo> accessor_info) {
  DisallowGarbageCollection no_gc;
  accessor_info->init_getter_redirection(isolate);
}

void SerializerDeserializer::RestoreExternalReferenceRedirector(
    Isolate* isolate, Tagged<FunctionTemplateInfo> function_template_info) {
  DisallowGarbageCollection no_gc;
  function_template_info->init_callback_redirection(isolate);
}

}  // namespace internal
}  // namespace v8
