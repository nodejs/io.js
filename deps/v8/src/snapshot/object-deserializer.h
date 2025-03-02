// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_OBJECT_DESERIALIZER_H_
#define V8_SNAPSHOT_OBJECT_DESERIALIZER_H_

#include "src/snapshot/deserializer.h"

namespace v8 {
namespace internal {

class SerializedCodeData;
class SharedFunctionInfo;

// Deserializes the object graph rooted at a given object.
class ObjectDeserializer final : public Deserializer<Isolate> {
 public:
  static MaybeDirectHandle<SharedFunctionInfo> DeserializeSharedFunctionInfo(
      Isolate* isolate, const SerializedCodeData* data,
      DirectHandle<String> source);

 private:
  explicit ObjectDeserializer(Isolate* isolate, const SerializedCodeData* data);

  // Deserialize an object graph. Fail gracefully.
  MaybeDirectHandle<HeapObject> Deserialize();

  void LinkAllocationSites();
  void CommitPostProcessedObjects();
};

// Deserializes the object graph rooted at a given object.
class OffThreadObjectDeserializer final : public Deserializer<LocalIsolate> {
 public:
  static MaybeDirectHandle<SharedFunctionInfo> DeserializeSharedFunctionInfo(
      LocalIsolate* isolate, const SerializedCodeData* data,
      std::vector<IndirectHandle<Script>>* deserialized_scripts);

 private:
  explicit OffThreadObjectDeserializer(LocalIsolate* isolate,
                                       const SerializedCodeData* data);

  // Deserialize an object graph. Fail gracefully.
  MaybeDirectHandle<HeapObject> Deserialize(
      std::vector<IndirectHandle<Script>>* deserialized_scripts);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_OBJECT_DESERIALIZER_H_
