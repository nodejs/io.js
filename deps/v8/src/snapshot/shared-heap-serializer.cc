// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/shared-heap-serializer.h"

#include "src/heap/read-only-heap.h"
#include "src/objects/objects-inl.h"
#include "src/snapshot/read-only-serializer.h"

namespace v8 {
namespace internal {

// static
bool SharedHeapSerializer::CanBeInSharedOldSpace(Tagged<HeapObject> obj) {
  if (ReadOnlyHeap::Contains(obj)) return false;
  if (IsString(obj)) {
    return IsInternalizedString(obj) ||
           String::IsInPlaceInternalizable(String::cast(obj));
  }
  return false;
}

// static
bool SharedHeapSerializer::ShouldBeInSharedHeapObjectCache(
    Tagged<HeapObject> obj) {
  // To keep the shared heap object cache lean, only include objects that should
  // not be duplicated. Currently, that is only internalized strings. In-place
  // internalizable strings will still be allocated in the shared heap by the
  // deserializer, but do not need to be kept alive forever in the cache.
  if (CanBeInSharedOldSpace(obj)) {
    if (IsInternalizedString(obj)) return true;
  }
  return false;
}

SharedHeapSerializer::SharedHeapSerializer(Isolate* isolate,
                                           Snapshot::SerializerFlags flags)
    : RootsSerializer(isolate, flags, RootIndex::kFirstStrongRoot)
#ifdef DEBUG
      ,
      serialized_objects_(isolate->heap())
#endif
{
  if (ShouldReconstructSharedHeapObjectCacheForTesting()) {
    ReconstructSharedHeapObjectCacheForTesting();
  }
}

SharedHeapSerializer::~SharedHeapSerializer() {
  OutputStatistics("SharedHeapSerializer");
}

void SharedHeapSerializer::FinalizeSerialization() {
  // This is called after serialization of the startup and context snapshots
  // which entries are added to the shared heap object cache. Terminate the
  // cache with an undefined.
  Tagged<Object> undefined = ReadOnlyRoots(isolate()).undefined_value();
  VisitRootPointer(Root::kSharedHeapObjectCache, nullptr,
                   FullObjectSlot(&undefined));

  // When v8_flags.shared_string_table is true, all internalized and
  // internalizable-in-place strings are in the shared heap.
  SerializeStringTable(isolate()->string_table());
  SerializeDeferredObjects();
  Pad();

#ifdef DEBUG
  // Check that all serialized object are in shared heap and not RO. RO objects
  // should be in the RO snapshot.
  IdentityMap<int, base::DefaultAllocationPolicy>::IteratableScope it_scope(
      &serialized_objects_);
  for (auto it = it_scope.begin(); it != it_scope.end(); ++it) {
    Tagged<HeapObject> obj = HeapObject::cast(it.key());
    CHECK(CanBeInSharedOldSpace(obj));
    CHECK(!ReadOnlyHeap::Contains(obj));
  }
#endif
}

bool SharedHeapSerializer::SerializeUsingSharedHeapObjectCache(
    SnapshotByteSink* sink, Handle<HeapObject> obj) {
  if (!ShouldBeInSharedHeapObjectCache(*obj)) return false;
  int cache_index = SerializeInObjectCache(obj);

  // When testing deserialization of a snapshot from a live Isolate where there
  // is also a shared Isolate, the shared object cache needs to be extended
  // because the live isolate may have had new internalized strings that were
  // not present in the startup snapshot to be serialized.
  if (ShouldReconstructSharedHeapObjectCacheForTesting()) {
    std::vector<Tagged<Object>>* existing_cache =
        isolate()->shared_space_isolate()->shared_heap_object_cache();
    const size_t existing_cache_size = existing_cache->size();
    // This is strictly < because the existing cache contains the terminating
    // undefined value, which the reconstructed cache does not.
    DCHECK_LT(base::checked_cast<size_t>(cache_index), existing_cache_size);
    if (base::checked_cast<size_t>(cache_index) == existing_cache_size - 1) {
      ReadOnlyRoots roots(isolate());
      DCHECK(IsUndefined(existing_cache->back(), roots));
      existing_cache->back() = *obj;
      existing_cache->push_back(roots.undefined_value());
    }
  }

  sink->Put(kSharedHeapObjectCache, "SharedHeapObjectCache");
  sink->PutUint30(cache_index, "shared_heap_object_cache_index");
  return true;
}

void SharedHeapSerializer::SerializeStringTable(StringTable* string_table) {
  // A StringTable is serialized as:
  //
  //   N : int
  //   string 1
  //   string 2
  //   ...
  //   string N
  //
  // Notably, the hashmap structure, including empty and deleted elements, is
  // not serialized.

  sink_.PutUint30(string_table->NumberOfElements(),
                  "String table number of elements");

  // Custom RootVisitor which walks the string table, but only serializes the
  // string entries. This is an inline class to be able to access the non-public
  // SerializeObject method.
  class SharedHeapSerializerStringTableVisitor : public RootVisitor {
   public:
    explicit SharedHeapSerializerStringTableVisitor(
        SharedHeapSerializer* serializer)
        : serializer_(serializer) {}

    void VisitRootPointers(Root root, const char* description,
                           FullObjectSlot start, FullObjectSlot end) override {
      UNREACHABLE();
    }

    void VisitRootPointers(Root root, const char* description,
                           OffHeapObjectSlot start,
                           OffHeapObjectSlot end) override {
      DCHECK_EQ(root, Root::kStringTable);
      Isolate* isolate = serializer_->isolate();
      for (OffHeapObjectSlot current = start; current < end; ++current) {
        Tagged<Object> obj = current.load(isolate);
        if (IsHeapObject(obj)) {
          DCHECK(IsInternalizedString(obj));
          serializer_->SerializeObject(handle(HeapObject::cast(obj), isolate),
                                       SlotType::kAnySlot);
        }
      }
    }

   private:
    SharedHeapSerializer* serializer_;
  };

  SharedHeapSerializerStringTableVisitor string_table_visitor(this);
  isolate()->string_table()->IterateElements(&string_table_visitor);
}

void SharedHeapSerializer::SerializeObjectImpl(Handle<HeapObject> obj,
                                               SlotType slot_type) {
  // Objects in the shared heap cannot depend on per-Isolate roots but can
  // depend on RO roots since sharing objects requires sharing the RO space.
  DCHECK(CanBeInSharedOldSpace(*obj) || ReadOnlyHeap::Contains(*obj));
  {
    DisallowGarbageCollection no_gc;
    Tagged<HeapObject> raw = *obj;
    if (SerializeHotObject(raw)) return;
    if (IsRootAndHasBeenSerialized(raw) && SerializeRoot(raw)) return;
  }
  if (SerializeReadOnlyObjectReference(*obj, &sink_)) return;
  {
    DisallowGarbageCollection no_gc;
    Tagged<HeapObject> raw = *obj;
    if (SerializeBackReference(raw)) return;
    CheckRehashability(raw);

    DCHECK(!ReadOnlyHeap::Contains(raw));
  }

  ObjectSerializer object_serializer(this, obj, &sink_);
  object_serializer.Serialize(slot_type);

#ifdef DEBUG
  CHECK_NULL(serialized_objects_.Find(obj));
  // There's no "IdentitySet", so use an IdentityMap with a value that is
  // later ignored.
  serialized_objects_.Insert(obj, 0);
#endif
}

bool SharedHeapSerializer::ShouldReconstructSharedHeapObjectCacheForTesting()
    const {
  // When the live Isolate being serialized is not a client Isolate, there's no
  // need to reconstruct the shared heap object cache because it is not actually
  // shared.
  return reconstruct_read_only_and_shared_object_caches_for_testing() &&
         isolate()->has_shared_space();
}

void SharedHeapSerializer::ReconstructSharedHeapObjectCacheForTesting() {
  std::vector<Tagged<Object>>* cache =
      isolate()->shared_space_isolate()->shared_heap_object_cache();
  // Don't reconstruct the final element, which is always undefined and marks
  // the end of the cache, since serializing the live Isolate may extend the
  // shared object cache.
  for (size_t i = 0, size = cache->size(); i < size - 1; i++) {
    Handle<HeapObject> obj(HeapObject::cast(cache->at(i)), isolate());
    DCHECK(ShouldBeInSharedHeapObjectCache(*obj));
    int cache_index = SerializeInObjectCache(obj);
    USE(cache_index);
    DCHECK_EQ(cache_index, i);
  }
  DCHECK(IsUndefined(cache->back(), isolate()));
}

}  // namespace internal
}  // namespace v8
