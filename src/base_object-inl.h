// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_BASE_OBJECT_INL_H_
#define SRC_BASE_OBJECT_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object.h"
#include "env-inl.h"
#include "util.h"

#include "v8.h"

namespace node {

BaseObject::BaseObject(Environment* env, v8::Local<v8::Object> object)
    : BaseObject(env->principal_realm(), object) {
  // TODO(legendecas): Check the shorthand is only used in the principal realm
  // while allowing to create a BaseObject in a vm context.
}

void BaseObject::Detach() {
  CHECK_GT(pointer_data()->strong_ptr_count, 0);
  pointer_data()->is_detached = true;
}

v8::Global<v8::Object>& BaseObject::persistent() {
  return persistent_handle_;
}


v8::Local<v8::Object> BaseObject::object() const {
  return PersistentToLocal::Default(env()->isolate(), persistent_handle_);
}

v8::Local<v8::Object> BaseObject::object(v8::Isolate* isolate) const {
  v8::Local<v8::Object> handle = object();

  DCHECK_EQ(handle->GetCreationContextChecked()->GetIsolate(), isolate);
  DCHECK_EQ(env()->isolate(), isolate);

  return handle;
}

Environment* BaseObject::env() const {
  return realm_->env();
}

Realm* BaseObject::realm() const {
  return realm_;
}

bool BaseObject::IsBaseObject(IsolateData* isolate_data,
                              v8::Local<v8::Object> obj) {
  if (obj->InternalFieldCount() < BaseObject::kInternalFieldCount) {
    return false;
  }

  uint16_t* ptr = static_cast<uint16_t*>(
      obj->GetAlignedPointerFromInternalField(BaseObject::kEmbedderType));
  return ptr == isolate_data->embedder_id_for_non_cppgc();
}

void BaseObject::TagBaseObject(IsolateData* isolate_data,
                               v8::Local<v8::Object> object) {
  DCHECK_GE(object->InternalFieldCount(), BaseObject::kInternalFieldCount);
  object->SetAlignedPointerInInternalField(
      BaseObject::kEmbedderType, isolate_data->embedder_id_for_non_cppgc());
}

void BaseObject::SetInternalFields(IsolateData* isolate_data,
                                   v8::Local<v8::Object> object,
                                   void* slot) {
  TagBaseObject(isolate_data, object);
  object->SetAlignedPointerInInternalField(BaseObject::kSlot, slot);
}

BaseObject* BaseObject::FromJSObject(v8::Local<v8::Value> value) {
  v8::Local<v8::Object> obj = value.As<v8::Object>();
  DCHECK_GE(obj->InternalFieldCount(), BaseObject::kInternalFieldCount);
  return static_cast<BaseObject*>(
      obj->GetAlignedPointerFromInternalField(BaseObject::kSlot));
}

template <typename T>
T* BaseObject::FromJSObject(v8::Local<v8::Value> object) {
  return static_cast<T*>(FromJSObject(object));
}

void BaseObject::OnGCCollect() {
  delete this;
}

void BaseObject::ClearWeak() {
  if (has_pointer_data())
    pointer_data()->wants_weak_jsobj = false;

  persistent_handle_.ClearWeak();
}

bool BaseObject::IsWeakOrDetached() const {
  if (persistent_handle_.IsWeak()) return true;

  if (!has_pointer_data()) return false;
  const PointerData* pd = const_cast<BaseObject*>(this)->pointer_data();
  return pd->wants_weak_jsobj || pd->is_detached;
}

v8::EmbedderGraph::Node::Detachedness BaseObject::GetDetachedness() const {
  return IsWeakOrDetached() ? v8::EmbedderGraph::Node::Detachedness::kDetached
                            : v8::EmbedderGraph::Node::Detachedness::kUnknown;
}

template <int Field>
void BaseObject::InternalFieldGet(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  args.GetReturnValue().Set(
      args.This()->GetInternalField(Field).As<v8::Value>());
}

template <int Field, bool (v8::Value::*typecheck)() const>
void BaseObject::InternalFieldSet(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Local<v8::Value> value = args[0];
  // This could be e.g. value->IsFunction().
  CHECK(((*value)->*typecheck)());
  args.This()->SetInternalField(Field, value);
}

bool BaseObject::has_pointer_data() const {
  return pointer_data_ != nullptr;
}

template <typename T, bool kIsWeak>
BaseObject::PointerData*
BaseObjectPtrImpl<T, kIsWeak>::pointer_data() const {
  if constexpr (kIsWeak) {
    return data_.pointer_data;
  }
  if (get_base_object() == nullptr) {
    return nullptr;
  }
  return get_base_object()->pointer_data();
}

template <typename T, bool kIsWeak>
BaseObject* BaseObjectPtrImpl<T, kIsWeak>::get_base_object() const {
  if constexpr (kIsWeak) {
    if (pointer_data() == nullptr) {
      return nullptr;
    }
    return pointer_data()->self;
  }
  return data_.target;
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>::~BaseObjectPtrImpl() {
  if constexpr (kIsWeak) {
    if (pointer_data() != nullptr &&
        --pointer_data()->weak_ptr_count == 0 &&
        pointer_data()->self == nullptr) {
      delete pointer_data();
    }
  } else if (get() != nullptr) {
    get()->decrease_refcount();
  }
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>::BaseObjectPtrImpl() {
  data_.target = nullptr;
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>::BaseObjectPtrImpl(T* target)
  : BaseObjectPtrImpl() {
  if (target == nullptr) return;
  if constexpr (kIsWeak) {
    data_.pointer_data = target->pointer_data();
    CHECK_NOT_NULL(pointer_data());
    pointer_data()->weak_ptr_count++;
  } else {
    data_.target = target;
    CHECK_NOT_NULL(pointer_data());
    get()->increase_refcount();
  }
}

template <typename T, bool kIsWeak>
template <typename U, bool kW>
BaseObjectPtrImpl<T, kIsWeak>::BaseObjectPtrImpl(
    const BaseObjectPtrImpl<U, kW>& other)
  : BaseObjectPtrImpl(other.get()) {}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>::BaseObjectPtrImpl(const BaseObjectPtrImpl& other)
  : BaseObjectPtrImpl(other.get()) {}

template <typename T, bool kIsWeak>
template <typename U, bool kW>
BaseObjectPtrImpl<T, kIsWeak>& BaseObjectPtrImpl<T, kIsWeak>::operator=(
    const BaseObjectPtrImpl<U, kW>& other) {
  if (other.get() == get()) return *this;
  this->~BaseObjectPtrImpl();
  return *new (this) BaseObjectPtrImpl(other);
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>& BaseObjectPtrImpl<T, kIsWeak>::operator=(
    const BaseObjectPtrImpl& other) {
  if (other.get() == get()) return *this;
  this->~BaseObjectPtrImpl();
  return *new (this) BaseObjectPtrImpl(other);
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>::BaseObjectPtrImpl(BaseObjectPtrImpl&& other)
  : data_(other.data_) {
  if constexpr (kIsWeak)
    other.data_.target = nullptr;
  else
    other.data_.pointer_data = nullptr;
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>& BaseObjectPtrImpl<T, kIsWeak>::operator=(
    BaseObjectPtrImpl&& other) {
  if (&other == this) return *this;
  this->~BaseObjectPtrImpl();
  return *new (this) BaseObjectPtrImpl(std::move(other));
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>::BaseObjectPtrImpl(std::nullptr_t)
    : BaseObjectPtrImpl() {}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>& BaseObjectPtrImpl<T, kIsWeak>::operator=(
    std::nullptr_t) {
  this->~BaseObjectPtrImpl();
  return *new (this) BaseObjectPtrImpl();
}

template <typename T, bool kIsWeak>
void BaseObjectPtrImpl<T, kIsWeak>::reset(T* ptr) {
  *this = BaseObjectPtrImpl(ptr);
}

template <typename T, bool kIsWeak>
T* BaseObjectPtrImpl<T, kIsWeak>::get() const {
  return static_cast<T*>(get_base_object());
}

template <typename T, bool kIsWeak>
T& BaseObjectPtrImpl<T, kIsWeak>::operator*() const {
  return *get();
}

template <typename T, bool kIsWeak>
T* BaseObjectPtrImpl<T, kIsWeak>::operator->() const {
  return get();
}

template <typename T, bool kIsWeak>
BaseObjectPtrImpl<T, kIsWeak>::operator bool() const {
  return get() != nullptr;
}

template <typename T, bool kIsWeak>
template <typename U, bool kW>
bool BaseObjectPtrImpl<T, kIsWeak>::operator ==(
    const BaseObjectPtrImpl<U, kW>& other) const {
  return get() == other.get();
}

template <typename T, bool kIsWeak>
template <typename U, bool kW>
bool BaseObjectPtrImpl<T, kIsWeak>::operator !=(
    const BaseObjectPtrImpl<U, kW>& other) const {
  return get() != other.get();
}

template <typename T, bool kIsWeak>
bool operator==(const BaseObjectPtrImpl<T, kIsWeak> ptr, const std::nullptr_t) {
  return ptr.get() == nullptr;
}

template <typename T, bool kIsWeak>
bool operator==(const std::nullptr_t, const BaseObjectPtrImpl<T, kIsWeak> ptr) {
  return ptr.get() == nullptr;
}

template <typename T, typename... Args>
BaseObjectPtr<T> MakeBaseObject(Args&&... args) {
  return BaseObjectPtr<T>(new T(std::forward<Args>(args)...));
}
template <typename T, typename... Args>
BaseObjectWeakPtr<T> MakeWeakBaseObject(Args&&... args) {
  T* target = new T(std::forward<Args>(args)...);
  target->MakeWeak();
  return BaseObjectWeakPtr<T>(target);
}

template <typename T, typename... Args>
BaseObjectPtr<T> MakeDetachedBaseObject(Args&&... args) {
  BaseObjectPtr<T> target = MakeBaseObject<T>(std::forward<Args>(args)...);
  target->Detach();
  return target;
}

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_BASE_OBJECT_INL_H_
