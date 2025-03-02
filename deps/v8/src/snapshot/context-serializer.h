// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_CONTEXT_SERIALIZER_H_
#define V8_SNAPSHOT_CONTEXT_SERIALIZER_H_

#include "src/objects/contexts.h"
#include "src/snapshot/serializer.h"
#include "src/snapshot/snapshot-source-sink.h"

namespace v8 {
namespace internal {

class StartupSerializer;

class V8_EXPORT_PRIVATE ContextSerializer : public Serializer {
 public:
  ContextSerializer(Isolate* isolate, Snapshot::SerializerFlags flags,
                    StartupSerializer* startup_serializer,
                    SerializeEmbedderFieldsCallback callback);

  ~ContextSerializer() override;
  ContextSerializer(const ContextSerializer&) = delete;
  ContextSerializer& operator=(const ContextSerializer&) = delete;

  // Serialize the objects reachable from a single object pointer.
  void Serialize(Tagged<Context>* o, const DisallowGarbageCollection& no_gc);

  bool can_be_rehashed() const { return can_be_rehashed_; }

 private:
  void SerializeObjectImpl(Handle<HeapObject> o, SlotType slot_type) override;
  bool ShouldBeInTheStartupObjectCache(Tagged<HeapObject> o);
  bool ShouldBeInTheSharedObjectCache(Tagged<HeapObject> o);
  void CheckRehashability(Tagged<HeapObject> obj);

  template <typename V8Type, typename UserSerializerWrapper,
            typename UserCallback, typename ApiObjectType>
  void SerializeObjectWithEmbedderFields(Handle<V8Type> data_holder,
                                         int embedder_fields_count,
                                         UserSerializerWrapper wrapper,
                                         UserCallback user_callback,
                                         ApiObjectType api_obj);

  // For JS API wrapper objects we serialize embedder-controled data for each
  // object.
  void SerializeApiWrapperFields(DirectHandle<JSObject> js_object);

  StartupSerializer* startup_serializer_;
  SerializeEmbedderFieldsCallback serialize_embedder_fields_;
  // Indicates whether we only serialized hash tables that we can rehash.
  // TODO(yangguo): generalize rehashing, and remove this flag.
  bool can_be_rehashed_;
  Tagged<Context> context_;

  // Used to store serialized data for embedder fields.
  SnapshotByteSink embedder_fields_sink_;
  // Used to store serialized data for API wrappers.
  SnapshotByteSink api_wrapper_sink_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_CONTEXT_SERIALIZER_H_
