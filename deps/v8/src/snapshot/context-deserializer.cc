// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/context-deserializer.h"

#include "src/api/api-inl.h"
#include "src/base/logging.h"
#include "src/common/assert-scope.h"
#include "src/logging/counters-scopes.h"
#include "src/snapshot/serializer-deserializer.h"

namespace v8 {
namespace internal {

// static
MaybeDirectHandle<Context> ContextDeserializer::DeserializeContext(
    Isolate* isolate, const SnapshotData* data, size_t context_index,
    bool can_rehash, DirectHandle<JSGlobalProxy> global_proxy,
    DeserializeEmbedderFieldsCallback embedder_fields_deserializer) {
  TRACE_EVENT0("v8", "V8.DeserializeContext");
  RCS_SCOPE(isolate, RuntimeCallCounterId::kDeserializeContext);
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.profile_deserialization)) timer.Start();
  NestedTimedHistogramScope histogram_timer(
      isolate->counters()->snapshot_deserialize_context());

  ContextDeserializer d(isolate, data, can_rehash);
  MaybeDirectHandle<Object> maybe_result =
      d.Deserialize(isolate, global_proxy, embedder_fields_deserializer);

  if (V8_UNLIKELY(v8_flags.profile_deserialization)) {
    // ATTENTION: The Memory.json benchmark greps for this exact output. Do not
    // change it without also updating Memory.json.
    const int bytes = static_cast<int>(data->RawData().size());
    const double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Deserializing context #%zu (%d bytes) took %0.3f ms]\n",
           context_index, bytes, ms);
  }

  DirectHandle<Object> result;
  if (!maybe_result.ToHandle(&result)) return {};

  return Cast<Context>(result);
}

MaybeDirectHandle<Object> ContextDeserializer::Deserialize(
    Isolate* isolate, DirectHandle<JSGlobalProxy> global_proxy,
    DeserializeEmbedderFieldsCallback embedder_fields_deserializer) {
  // Replace serialized references to the global proxy and its map with the
  // given global proxy and its map.
  AddAttachedObject(global_proxy);
  AddAttachedObject(direct_handle(global_proxy->map(), isolate));

  DirectHandle<Object> result;
  {
    // There's no code deserialized here. If this assert fires then that's
    // changed and logging should be added to notify the profiler et al. of
    // the new code, which also has to be flushed from instruction cache.
    DisallowCodeAllocation no_code_allocation;

    result = ReadObject();
    DCHECK(IsNativeContext(*result));
    DeserializeDeferredObjects();
    DeserializeEmbedderFields(Cast<NativeContext>(result),
                              embedder_fields_deserializer);
    DeserializeApiWrapperFields(
        embedder_fields_deserializer.api_wrapper_callback);
    LogNewMapEvents();
    WeakenDescriptorArrays();
  }

  if (should_rehash()) Rehash();

  return result;
}

template <typename T>
class PlainBuffer {
 public:
  T* data() { return data_.get(); }

  void EnsureCapacity(size_t new_capacity) {
    if (new_capacity > capacity_) {
      data_.reset(new T[new_capacity]);
      capacity_ = new_capacity;
    }
  }

 private:
  std::unique_ptr<T[]> data_;
  size_t capacity_{0};
};

void ContextDeserializer::DeserializeEmbedderFields(
    DirectHandle<NativeContext> context,
    DeserializeEmbedderFieldsCallback embedder_fields_deserializer) {
  if (!source()->HasMore() || source()->Peek() != kEmbedderFieldsData) {
    return;
  }
  // Consume `kEmbedderFieldsData`.
  source()->Get();
  DisallowGarbageCollection no_gc;
  DisallowJavascriptExecution no_js(isolate());
  DisallowCompilation no_compile(isolate());
  // Buffer is reused across various deserializations. We always copy N bytes
  // into the backing and pass that N bytes to the embedder via StartupData.
  PlainBuffer<char> buffer;
  for (int code = source()->Get(); code != kSynchronize;
       code = source()->Get()) {
    HandleScope scope(isolate());
    DirectHandle<HeapObject> heap_object =
        Cast<HeapObject>(GetBackReferencedObject());
    const int index = source()->GetUint30();
    const int size = source()->GetUint30();
    buffer.EnsureCapacity(size);
    source()->CopyRaw(buffer.data(), size);
    if (IsJSObject(*heap_object)) {
      DirectHandle<JSObject> obj = Cast<JSObject>(heap_object);
      v8::DeserializeInternalFieldsCallback callback =
          embedder_fields_deserializer.js_object_callback;
      DCHECK_NOT_NULL(callback.callback);
      callback.callback(v8::Utils::ToLocal(obj), index, {buffer.data(), size},
                        callback.data);
    } else {
      DCHECK(IsEmbedderDataArray(*heap_object));
      v8::DeserializeContextDataCallback callback =
          embedder_fields_deserializer.context_callback;
      DCHECK_NOT_NULL(callback.callback);
      callback.callback(v8::Utils::ToLocal(context), index,
                        {buffer.data(), size}, callback.data);
    }
  }
}

void ContextDeserializer::DeserializeApiWrapperFields(
    const v8::DeserializeAPIWrapperCallback& api_wrapper_callback) {
  if (!source()->HasMore() || source()->Peek() != kApiWrapperFieldsData) {
    return;
  }
  // Consume `kApiWrapperFieldsData`.
  source()->Get();
  DisallowGarbageCollection no_gc;
  DisallowJavascriptExecution no_js(isolate());
  DisallowCompilation no_compile(isolate());
  // Buffer is reused across various deserializations. We always copy N bytes
  // into the backing and pass that N bytes to the embedder via StartupData.
  PlainBuffer<char> buffer;
  // The block for `kApiWrapperFieldsData` consists of consecutive `kNewObject`
  // blocks that are in the end terminated with a `kSynchronize`.
  for (int code = source()->Get(); code != kSynchronize;
       code = source()->Get()) {
    HandleScope scope(isolate());
    DirectHandle<JSObject> js_object =
        Cast<JSObject>(GetBackReferencedObject());
    const int size = source()->GetUint30();
    buffer.EnsureCapacity(size);
    source()->CopyRaw(buffer.data(), size);
    DCHECK_NOT_NULL(api_wrapper_callback.callback);
    api_wrapper_callback.callback(v8::Utils::ToLocal(js_object),
                                  {buffer.data(), size},
                                  api_wrapper_callback.data);
  }
}

}  // namespace internal
}  // namespace v8
