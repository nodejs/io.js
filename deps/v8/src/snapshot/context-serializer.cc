// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/context-serializer.h"

#include "src/api/api-inl.h"
#include "src/execution/microtask-queue.h"
#include "src/heap/combined-heap.h"
#include "src/numbers/math-random.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/snapshot/serializer-deserializer.h"
#include "src/snapshot/startup-serializer.h"

namespace v8 {
namespace internal {

namespace {

// During serialization, puts the native context into a state understood by the
// serializer (e.g. by clearing lists of InstructionStream objects).  After
// serialization, the original state is restored.
class V8_NODISCARD SanitizeNativeContextScope final {
 public:
  SanitizeNativeContextScope(Isolate* isolate,
                             Tagged<NativeContext> native_context,
                             bool allow_active_isolate_for_testing,
                             const DisallowGarbageCollection& no_gc)
      : native_context_(native_context), no_gc_(no_gc) {
#ifdef DEBUG
    if (!allow_active_isolate_for_testing) {
      // Microtasks.
      MicrotaskQueue* microtask_queue = native_context_->microtask_queue();
      DCHECK_EQ(0, microtask_queue->size());
      DCHECK(!microtask_queue->HasMicrotasksSuppressions());
      DCHECK_EQ(0, microtask_queue->GetMicrotasksScopeDepth());
      DCHECK(microtask_queue->DebugMicrotasksScopeDepthIsZero());
    }
#endif
    microtask_queue_external_pointer_ =
        native_context
            ->RawExternalPointerField(NativeContext::kMicrotaskQueueOffset,
                                      kNativeContextMicrotaskQueueTag)
            .GetAndClearContentForSerialization(no_gc);
  }

  ~SanitizeNativeContextScope() {
    // Restore saved fields.
    native_context_
        ->RawExternalPointerField(NativeContext::kMicrotaskQueueOffset,
                                  kNativeContextMicrotaskQueueTag)
        .RestoreContentAfterSerialization(microtask_queue_external_pointer_,
                                          no_gc_);
  }

 private:
  Tagged<NativeContext> native_context_;
  ExternalPointerSlot::RawContent microtask_queue_external_pointer_;
  const DisallowGarbageCollection& no_gc_;
};

}  // namespace

ContextSerializer::ContextSerializer(Isolate* isolate,
                                     Snapshot::SerializerFlags flags,
                                     StartupSerializer* startup_serializer,
                                     SerializeEmbedderFieldsCallback callback)
    : Serializer(isolate, flags),
      startup_serializer_(startup_serializer),
      serialize_embedder_fields_(callback),
      can_be_rehashed_(true) {
  InitializeCodeAddressMap();
}

ContextSerializer::~ContextSerializer() {
  OutputStatistics("ContextSerializer");
}

void ContextSerializer::Serialize(Tagged<Context>* o,
                                  const DisallowGarbageCollection& no_gc) {
  context_ = *o;
  DCHECK(IsNativeContext(context_));

  // Upon deserialization, references to the global proxy and its map will be
  // replaced.
  reference_map()->AddAttachedReference(context_->global_proxy());
  reference_map()->AddAttachedReference(context_->global_proxy()->map());

  // The bootstrap snapshot has a code-stub context. When serializing the
  // context snapshot, it is chained into the weak context list on the isolate
  // and it's next context pointer may point to the code-stub context.  Clear
  // it before serializing, it will get re-added to the context list
  // explicitly when it's loaded.
  // TODO(v8:10416): These mutations should not observably affect the running
  // context.
  context_->set(Context::NEXT_CONTEXT_LINK,
                ReadOnlyRoots(isolate()).undefined_value());
  DCHECK(!IsUndefined(context_->global_object()));
  // Reset math random cache to get fresh random numbers.
  MathRandom::ResetContext(context_);

  SanitizeNativeContextScope sanitize_native_context(
      isolate(), context_->native_context(), allow_active_isolate_for_testing(),
      no_gc);

  VisitRootPointer(Root::kStartupObjectCache, nullptr, FullObjectSlot(o));
  SerializeDeferredObjects();

  // Add section for embedder-serialized embedder fields.
  if (!embedder_fields_sink_.data()->empty()) {
    sink_.Put(kEmbedderFieldsData, "embedder fields data");
    sink_.Append(embedder_fields_sink_);
    sink_.Put(kSynchronize, "Finished with embedder fields data");
  }

  // Add section for embedder-serializer API wrappers.
  if (!api_wrapper_sink_.data()->empty()) {
    sink_.Put(kApiWrapperFieldsData, "api wrapper fields data");
    sink_.Append(api_wrapper_sink_);
    sink_.Put(kSynchronize, "Finished with api wrapper fields data");
  }

  Pad();
}

v8::StartupData InternalFieldSerializeWrapper(
    int index, bool field_is_nullptr,
    v8::SerializeInternalFieldsCallback user_callback,
    v8::Local<v8::Object> api_obj) {
  // If no serializer is provided and the field was empty, we
  // serialize it by default to nullptr.
  if (user_callback.callback == nullptr && field_is_nullptr) {
    return StartupData{nullptr, 0};
  }

  DCHECK(user_callback.callback);
  return user_callback.callback(api_obj, index, user_callback.data);
}

v8::StartupData ContextDataSerializeWrapper(
    int index, bool field_is_nullptr,
    v8::SerializeContextDataCallback user_callback,
    v8::Local<v8::Context> api_obj) {
  // For compatibility, we do not require all non-null context pointer
  // fields to be serialized by a proper user callback. Instead, if no
  // user callback is provided, we serialize it verbatim, which was
  // the old behavior before we introduce context data callbacks.
  if (user_callback.callback == nullptr) {
    return StartupData{nullptr, 0};
  }

  return user_callback.callback(api_obj, index, user_callback.data);
}

void ContextSerializer::SerializeObjectImpl(Handle<HeapObject> obj,
                                            SlotType slot_type) {
  DCHECK(!ObjectIsBytecodeHandler(*obj));  // Only referenced in dispatch table.

  if (!allow_active_isolate_for_testing()) {
    // When serializing a snapshot intended for real use, we should not end up
    // at another native context.
    // But in test scenarios there is no way to avoid this. Since we only
    // serialize a single context in these cases, and this context does not
    // have to be executable, we can simply ignore this.
    DCHECK_IMPLIES(IsNativeContext(*obj), *obj == context_);
  }

  {
    DisallowGarbageCollection no_gc;
    Tagged<HeapObject> raw = *obj;
    if (SerializeHotObject(raw)) return;
    if (SerializeRoot(raw)) return;
    if (SerializeBackReference(raw)) return;
    if (SerializeReadOnlyObjectReference(raw, &sink_)) return;
  }

  if (startup_serializer_->SerializeUsingSharedHeapObjectCache(&sink_, obj)) {
    return;
  }

  if (ShouldBeInTheStartupObjectCache(*obj)) {
    startup_serializer_->SerializeUsingStartupObjectCache(&sink_, obj);
    return;
  }

  // Pointers from the context snapshot to the objects in the startup snapshot
  // should go through the root array or through the startup object cache.
  // If this is not the case you may have to add something to the root array.
  DCHECK(!startup_serializer_->ReferenceMapContains(obj));
  // All the internalized strings that the context snapshot needs should be
  // either in the root table or in the shared heap object cache.
  DCHECK(!IsInternalizedString(*obj));
  // Function and object templates are not context specific.
  DCHECK(!IsTemplateInfo(*obj));

  InstanceType instance_type = obj->map()->instance_type();
  if (InstanceTypeChecker::IsFeedbackVector(instance_type)) {
    // Clear literal boilerplates and feedback.
    Cast<FeedbackVector>(obj)->ClearSlots(isolate());
  } else if (InstanceTypeChecker::IsJSObject(instance_type)) {
    Handle<JSObject> js_obj = Cast<JSObject>(obj);
    int embedder_fields_count = js_obj->GetEmbedderFieldCount();
    if (embedder_fields_count > 0) {
      DCHECK(!js_obj->NeedsRehashing(cage_base()));
      v8::Local<v8::Object> api_obj = v8::Utils::ToLocal(js_obj);
      v8::SerializeInternalFieldsCallback user_callback =
          serialize_embedder_fields_.js_object_callback;
      SerializeObjectWithEmbedderFields(js_obj, embedder_fields_count,
                                        InternalFieldSerializeWrapper,
                                        user_callback, api_obj);
      if (IsJSApiWrapperObject(*js_obj)) {
        SerializeApiWrapperFields(js_obj);
      }
      return;
    }
    if (InstanceTypeChecker::IsJSFunction(instance_type)) {
      DisallowGarbageCollection no_gc;
      // Unconditionally reset the JSFunction to its SFI's code, since we can't
      // serialize optimized code anyway.
      Tagged<JSFunction> closure = Cast<JSFunction>(*obj);
      if (closure->shared()->HasBytecodeArray()) {
        closure->SetInterruptBudget(isolate(), BudgetModification::kReset);
      }
      closure->ResetIfCodeFlushed(isolate());
      if (closure->is_compiled(isolate())) {
        if (closure->shared()->HasBaselineCode()) {
          closure->shared()->FlushBaselineCode();
        }
        Tagged<Code> sfi_code = closure->shared()->GetCode(isolate());
        if (!sfi_code.SafeEquals(closure->code(isolate()))) {
          closure->UpdateCode(sfi_code);
        }
      }
    }
  } else if (InstanceTypeChecker::IsEmbedderDataArray(instance_type) &&
             !allow_active_isolate_for_testing()) {
    DCHECK_EQ(*obj, context_->embedder_data());
    Handle<EmbedderDataArray> embedder_data = Cast<EmbedderDataArray>(obj);
    int embedder_fields_count = embedder_data->length();
    if (embedder_data->length() > 0) {
      DirectHandle<Context> context_handle(context_, isolate());
      v8::Local<v8::Context> api_obj =
          v8::Utils::ToLocal(Cast<NativeContext>(context_handle));
      v8::SerializeContextDataCallback user_callback =
          serialize_embedder_fields_.context_callback;
      SerializeObjectWithEmbedderFields(embedder_data, embedder_fields_count,
                                        ContextDataSerializeWrapper,
                                        user_callback, api_obj);
      return;
    }
  }

  CheckRehashability(*obj);

  // Object has not yet been serialized.  Serialize it here.
  ObjectSerializer serializer(this, obj, &sink_);
  serializer.Serialize(slot_type);
  if (IsJSApiWrapperObject(obj->map())) {
    SerializeApiWrapperFields(Cast<JSObject>(obj));
  }
}

bool ContextSerializer::ShouldBeInTheStartupObjectCache(Tagged<HeapObject> o) {
  // We can't allow scripts to be part of the context snapshot because they
  // contain a unique ID, and deserializing several context snapshots containing
  // script would cause dupes.
  return IsName(o) || IsScript(o) || IsSharedFunctionInfo(o) ||
         IsHeapNumber(o) || IsCode(o) || IsInstructionStream(o) ||
         IsScopeInfo(o) || IsAccessorInfo(o) || IsTemplateInfo(o) ||
         IsClassPositions(o) ||
         o->map() == ReadOnlyRoots(isolate()).fixed_cow_array_map();
}

bool ContextSerializer::ShouldBeInTheSharedObjectCache(Tagged<HeapObject> o) {
  // v8_flags.shared_string_table may be true during deserialization, so put
  // internalized strings into the shared object snapshot.
  return IsInternalizedString(o);
}

namespace {
bool DataIsEmpty(const StartupData& data) { return data.raw_size == 0; }
}  // anonymous namespace

void ContextSerializer::SerializeApiWrapperFields(
    DirectHandle<JSObject> js_object) {
  DCHECK(IsJSApiWrapperObject(*js_object));
  auto* cpp_heap_pointer =
      JSApiWrapper(*js_object)
          .GetCppHeapWrappable(isolate(), kAnyCppHeapPointer);
  const auto& callback_data = serialize_embedder_fields_.api_wrapper_callback;
  if (callback_data.callback == nullptr && cpp_heap_pointer == nullptr) {
    // No need to serialize anything as empty handles or handles pointing to
    // null objects will be preserved.
    return;
  }
  DCHECK_NOT_NULL(callback_data.callback);
  const auto data = callback_data.callback(
      v8::Utils::ToLocal(js_object), cpp_heap_pointer, callback_data.data);
  if (DataIsEmpty(data)) {
    return;
  }
  const SerializerReference* reference =
      reference_map()->LookupReference(*js_object);
  DCHECK_NOT_NULL(reference);
  DCHECK(reference->is_back_reference());
  api_wrapper_sink_.Put(kNewObject, "api wrapper field holder");
  api_wrapper_sink_.PutUint30(reference->back_ref_index(), "BackRefIndex");
  api_wrapper_sink_.PutUint30(data.raw_size, "api wrapper raw field data size");
  api_wrapper_sink_.PutRaw(reinterpret_cast<const uint8_t*>(data.data),
                           data.raw_size, "api wrapper raw field data");
}

template <typename V8Type, typename UserSerializerWrapper,
          typename UserCallback, typename ApiObjectType>
void ContextSerializer::SerializeObjectWithEmbedderFields(
    Handle<V8Type> data_holder, int embedder_fields_count,
    UserSerializerWrapper wrapper, UserCallback user_callback,
    ApiObjectType api_obj) {
  DisallowGarbageCollection no_gc;
  CHECK_GT(embedder_fields_count, 0);
  DisallowJavascriptExecution no_js(isolate());
  DisallowCompilation no_compile(isolate());

  auto raw_obj = *data_holder;

  std::vector<EmbedderDataSlot::RawData> original_embedder_values;
  std::vector<StartupData> serialized_data;
  std::vector<bool> should_clear_slot;

  // 1) Iterate embedder fields. Hold onto the original value of the fields.
  //    Ignore references to heap objects since these are to be handled by the
  //    serializer. For aligned pointers, call the serialize callback. Hold
  //    onto the result.
  for (int i = 0; i < embedder_fields_count; i++) {
    EmbedderDataSlot slot(raw_obj, i);
    original_embedder_values.emplace_back(slot.load_raw(isolate(), no_gc));
    Tagged<Object> object = slot.load_tagged();
    if (IsHeapObject(object)) {
      DCHECK(IsValidHeapObject(isolate()->heap(), Cast<HeapObject>(object)));
      serialized_data.push_back({nullptr, 0});
      should_clear_slot.push_back(false);
    } else {
      StartupData data =
          wrapper(i, object == Smi::zero(), user_callback, api_obj);
      serialized_data.push_back(data);
      bool clear_slot =
          !DataIsEmpty(data) || slot.MustClearDuringSerialization(no_gc);
      should_clear_slot.push_back(clear_slot);
    }
  }

  // 2) Prevent embedder fields that are not V8 objects from ending up in the
  //    blob.  This is done separately to step 1 so as to not interleave with
  //    embedder callbacks.
  for (int i = 0; i < embedder_fields_count; i++) {
    if (should_clear_slot[i]) {
      EmbedderDataSlot(raw_obj, i).store_raw(isolate(), kNullAddress, no_gc);
    }
  }

  // 3) Serialize the object. References from embedder fields to heap objects or
  //    smis are serialized regularly.
  {
    AllowGarbageCollection allow_gc;
    ObjectSerializer(this, data_holder, &sink_).Serialize(SlotType::kAnySlot);
    // Reload raw pointer.
    raw_obj = *data_holder;
  }

  // 4) Obtain back reference for the serialized object.
  const SerializerReference* reference =
      reference_map()->LookupReference(raw_obj);
  DCHECK_NOT_NULL(reference);
  DCHECK(reference->is_back_reference());

  // 5) Write data returned by the embedder callbacks into a separate sink,
  //    headed by the back reference. Restore the original embedder fields.
  for (int i = 0; i < embedder_fields_count; i++) {
    StartupData data = serialized_data[i];
    if (!should_clear_slot[i]) continue;
    // Restore original values from cleared fields.
    EmbedderDataSlot(raw_obj, i)
        .store_raw(isolate(), original_embedder_values[i], no_gc);
    if (DataIsEmpty(data)) continue;
    embedder_fields_sink_.Put(kNewObject, "embedder field holder");
    embedder_fields_sink_.PutUint30(reference->back_ref_index(),
                                    "BackRefIndex");
    embedder_fields_sink_.PutUint30(i, "embedder field index");
    embedder_fields_sink_.PutUint30(data.raw_size, "embedder fields data size");
    embedder_fields_sink_.PutRaw(reinterpret_cast<const uint8_t*>(data.data),
                                 data.raw_size, "embedder fields data");
    delete[] data.data;
  }

  // 6) The content of the separate sink is appended eventually to the default
  //    sink. The ensures that during deserialization, we call the deserializer
  //    callback at the end, and can guarantee that the deserialized objects are
  //    in a consistent state. See ContextSerializer::Serialize.
}

void ContextSerializer::CheckRehashability(Tagged<HeapObject> obj) {
  if (!can_be_rehashed_) return;
  if (!obj->NeedsRehashing(cage_base())) return;
  if (obj->CanBeRehashed(cage_base())) return;
  can_be_rehashed_ = false;
}

}  // namespace internal
}  // namespace v8
