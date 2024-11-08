#include "base_object.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node_messaging.h"
#include "node_realm-inl.h"

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::Object;
using v8::Value;
using v8::ValueDeserializer;
using v8::WeakCallbackInfo;
using v8::WeakCallbackType;

BaseObject::BaseObject(Realm* realm, Local<Object> object)
    : persistent_handle_(realm->isolate(), object), realm_(realm) {
  CHECK_EQ(false, object.IsEmpty());
  CHECK_GE(object->InternalFieldCount(), BaseObject::kInternalFieldCount);
  SetInternalFields(realm->isolate_data(), object, static_cast<void*>(this));
  realm->TrackBaseObject(this);
}

BaseObject::~BaseObject() {
  realm()->UntrackBaseObject(this);

  if (has_pointer_data()) [[unlikely]] {
    PointerData* metadata = pointer_data();
    CHECK_EQ(metadata->strong_ptr_count, 0);
    metadata->self = nullptr;
    if (metadata->weak_ptr_count == 0) delete metadata;
  }

  if (persistent_handle_.IsEmpty()) {
    // This most likely happened because the weak callback below cleared it.
    return;
  }

  {
    HandleScope handle_scope(realm()->isolate());
    object()->SetAlignedPointerInInternalField(BaseObject::kSlot, nullptr);
  }
}

void BaseObject::MakeWeak() {
  if (has_pointer_data()) {
    pointer_data()->wants_weak_jsobj = true;
    if (pointer_data()->strong_ptr_count > 0) return;
  }

  persistent_handle_.SetWeak(
      this,
      [](const WeakCallbackInfo<BaseObject>& data) {
        BaseObject* obj = data.GetParameter();
        // Clear the persistent handle so that ~BaseObject() doesn't attempt
        // to mess with internal fields, since the JS object may have
        // transitioned into an invalid state.
        // Refs: https://github.com/nodejs/node/issues/18897
        obj->persistent_handle_.Reset();
        CHECK_IMPLIES(obj->has_pointer_data(),
                      obj->pointer_data()->strong_ptr_count == 0);
        obj->OnGCCollect();
      },
      WeakCallbackType::kParameter);
}

void BaseObject::LazilyInitializedJSTemplateConstructor(
    const FunctionCallbackInfo<Value>& args) {
  DCHECK(args.IsConstructCall());
  CHECK_GE(args.This()->InternalFieldCount(), BaseObject::kInternalFieldCount);
  Environment* env = Environment::GetCurrent(args);
  DCHECK_NOT_NULL(env);
  SetInternalFields(env->isolate_data(), args.This(), nullptr);
}

Local<FunctionTemplate> BaseObject::MakeLazilyInitializedJSTemplate(
    Environment* env) {
  return MakeLazilyInitializedJSTemplate(env->isolate_data());
}

Local<FunctionTemplate> BaseObject::MakeLazilyInitializedJSTemplate(
    IsolateData* isolate_data) {
  Local<FunctionTemplate> t = NewFunctionTemplate(
      isolate_data->isolate(), LazilyInitializedJSTemplateConstructor);
  t->InstanceTemplate()->SetInternalFieldCount(BaseObject::kInternalFieldCount);
  return t;
}

BaseObject::TransferMode BaseObject::GetTransferMode() const {
  return TransferMode::kDisallowCloneAndTransfer;
}

std::unique_ptr<worker::TransferData> BaseObject::TransferForMessaging() {
  return {};
}

std::unique_ptr<worker::TransferData> BaseObject::CloneForMessaging() const {
  return {};
}

Maybe<std::vector<BaseObjectPtr<BaseObject>>> BaseObject::NestedTransferables()
    const {
  return Just(std::vector<BaseObjectPtr<BaseObject>>{});
}

Maybe<void> BaseObject::FinalizeTransferRead(Local<Context> context,
                                             ValueDeserializer* deserializer) {
  return JustVoid();
}

BaseObject::PointerData* BaseObject::pointer_data() {
  if (!has_pointer_data()) {
    PointerData* metadata = new PointerData();
    metadata->wants_weak_jsobj = persistent_handle_.IsWeak();
    metadata->self = this;
    pointer_data_ = metadata;
  }
  CHECK(has_pointer_data());
  return pointer_data_;
}

void BaseObject::decrease_refcount() {
  CHECK(has_pointer_data());
  PointerData* metadata = pointer_data();
  CHECK_GT(metadata->strong_ptr_count, 0);
  unsigned int new_refcount = --metadata->strong_ptr_count;
  if (new_refcount == 0) {
    if (metadata->is_detached) {
      OnGCCollect();
    } else if (metadata->wants_weak_jsobj && !persistent_handle_.IsEmpty()) {
      MakeWeak();
    }
  }
}

void BaseObject::increase_refcount() {
  unsigned int prev_refcount = pointer_data()->strong_ptr_count++;
  if (prev_refcount == 0 && !persistent_handle_.IsEmpty())
    persistent_handle_.ClearWeak();
}

void BaseObject::DeleteMe() {
  if (has_pointer_data() && pointer_data()->strong_ptr_count > 0) {
    return Detach();
  }
  delete this;
}

bool BaseObject::IsDoneInitializing() const {
  return true;
}

Local<Object> BaseObject::WrappedObject() const {
  return object();
}

bool BaseObject::IsRootNode() const {
  return !persistent_handle_.IsWeak();
}

bool BaseObject::IsNotIndicativeOfMemoryLeakAtExit() const {
  return IsWeakOrDetached();
}

void BaseObjectList::Cleanup() {
  while (!IsEmpty()) {
    BaseObject* bo = PopFront();
    bo->DeleteMe();
  }
}

void BaseObjectList::MemoryInfo(node::MemoryTracker* tracker) const {
  for (auto bo : *this) {
    if (bo->IsDoneInitializing()) tracker->Track(bo);
  }
}

}  // namespace node
