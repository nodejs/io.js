// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_CONTEXT_DESERIALIZER_H_
#define V8_SNAPSHOT_CONTEXT_DESERIALIZER_H_

#include "src/snapshot/deserializer.h"
#include "src/snapshot/snapshot-data.h"

namespace v8 {
namespace internal {

class Context;
class Isolate;

// Deserializes the context-dependent object graph rooted at a given object.
// The ContextDeserializer is not expected to deserialize any code objects.
class V8_EXPORT_PRIVATE ContextDeserializer final
    : public Deserializer<Isolate> {
 public:
  static MaybeDirectHandle<Context> DeserializeContext(
      Isolate* isolate, const SnapshotData* data, size_t context_index,
      bool can_rehash, DirectHandle<JSGlobalProxy> global_proxy,
      DeserializeEmbedderFieldsCallback embedder_fields_deserializer);

 private:
  explicit ContextDeserializer(Isolate* isolate, const SnapshotData* data,
                               bool can_rehash)
      : Deserializer(isolate, data->Payload(), data->GetMagicNumber(), false,
                     can_rehash) {}

  // Deserialize a single object and the objects reachable from it.
  MaybeDirectHandle<Object> Deserialize(
      Isolate* isolate, DirectHandle<JSGlobalProxy> global_proxy,
      DeserializeEmbedderFieldsCallback embedder_fields_deserializer);

  void DeserializeEmbedderFields(
      DirectHandle<NativeContext> context,
      DeserializeEmbedderFieldsCallback embedder_fields_deserializer);

  void DeserializeApiWrapperFields(
      const v8::DeserializeAPIWrapperCallback& api_wrapper_callback);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_CONTEXT_DESERIALIZER_H_
