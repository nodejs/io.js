// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_STARTUP_SERIALIZER_H_
#define V8_SNAPSHOT_STARTUP_SERIALIZER_H_

#include <unordered_set>

#include "src/handles/global-handles.h"
#include "src/snapshot/roots-serializer.h"

namespace v8 {
namespace internal {

class HeapObject;
class SnapshotByteSink;
class SharedHeapSerializer;

class V8_EXPORT_PRIVATE StartupSerializer : public RootsSerializer {
 public:
  StartupSerializer(Isolate* isolate, Snapshot::SerializerFlags flags,
                    SharedHeapSerializer* shared_heap_serializer);
  ~StartupSerializer() override;
  StartupSerializer(const StartupSerializer&) = delete;
  StartupSerializer& operator=(const StartupSerializer&) = delete;

  // Serialize the current state of the heap.  The order is:
  // 1) Strong roots
  // 2) Builtins and bytecode handlers
  // 3) Startup object cache
  // 4) Weak references (e.g. the string table)
  void SerializeStrongReferences(const DisallowGarbageCollection& no_gc);
  void SerializeWeakReferencesAndDeferred();

  // If |obj| can be serialized in the shared heap snapshot then add it to the
  // shareable object cache if not already present and emits a
  // SharedHeapObjectCache bytecode into |sink|. Returns whether this was
  // successful.
  bool SerializeUsingSharedHeapObjectCache(SnapshotByteSink* sink,
                                           Handle<HeapObject> obj);

  // Adds |obj| to the startup object object cache if not already present and
  // emits a StartupObjectCache bytecode into |sink|.
  void SerializeUsingStartupObjectCache(SnapshotByteSink* sink,
                                        Handle<HeapObject> obj);

  // The per-heap dirty FinalizationRegistry list is weak and not serialized. No
  // JSFinalizationRegistries should be used during startup.
  void CheckNoDirtyFinalizationRegistries();

 private:
  void SerializeObjectImpl(Handle<HeapObject> o, SlotType slot_type) override;

  SharedHeapSerializer* const shared_heap_serializer_;
  GlobalHandleVector<AccessorInfo> accessor_infos_;
  GlobalHandleVector<FunctionTemplateInfo> function_template_infos_;
};

class SerializedHandleChecker : public RootVisitor {
 public:
  SerializedHandleChecker(Isolate* isolate,
                          std::vector<Tagged<Context>>* contexts);
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override;
  bool CheckGlobalAndEternalHandles();

 private:
  void AddToSet(Tagged<FixedArray> serialized);

  Isolate* isolate_;
  std::unordered_set<Tagged<Object>, Object::Hasher> serialized_;
  bool ok_ = true;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_STARTUP_SERIALIZER_H_
