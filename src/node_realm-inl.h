#ifndef SRC_NODE_REALM_INL_H_
#define SRC_NODE_REALM_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_context_data.h"
#include "node_realm.h"

namespace node {

inline Realm* Realm::GetCurrent(v8::Isolate* isolate) {
  if (!isolate->InContext()) [[unlikely]] {
    return nullptr;
  }
  v8::HandleScope handle_scope(isolate);
  return GetCurrent(isolate->GetCurrentContext());
}

inline Realm* Realm::GetCurrent(v8::Local<v8::Context> context) {
  if (!ContextEmbedderTag::IsNodeContext(context)) [[unlikely]] {
    return nullptr;
  }
  return static_cast<Realm*>(
      context->GetAlignedPointerFromEmbedderData(ContextEmbedderIndex::kRealm));
}

inline Realm* Realm::GetCurrent(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  return GetCurrent(info.GetIsolate()->GetCurrentContext());
}

template <typename T>
inline Realm* Realm::GetCurrent(const v8::PropertyCallbackInfo<T>& info) {
  return GetCurrent(info.GetIsolate()->GetCurrentContext());
}

inline IsolateData* Realm::isolate_data() const {
  return env_->isolate_data();
}

inline Environment* Realm::env() const {
  return env_;
}

inline v8::Isolate* Realm::isolate() const {
  return isolate_;
}

inline Realm::Kind Realm::kind() const {
  return kind_;
}

inline bool Realm::has_run_bootstrapping_code() const {
  return has_run_bootstrapping_code_;
}

// static
template <typename T, typename U>
inline T* Realm::GetBindingData(const v8::PropertyCallbackInfo<U>& info) {
  return GetBindingData<T>(info.GetIsolate()->GetCurrentContext());
}

// static
template <typename T>
inline T* Realm::GetBindingData(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  return GetBindingData<T>(info.GetIsolate()->GetCurrentContext());
}

// static
template <typename T>
inline T* Realm::GetBindingData(v8::Local<v8::Context> context) {
  Realm* realm = GetCurrent(context);
  return realm->GetBindingData<T>();
}

template <typename T>
inline T* Realm::GetBindingData() {
  constexpr size_t binding_index = static_cast<size_t>(T::binding_type_int);
  static_assert(binding_index < std::tuple_size_v<BindingDataStore>);
  auto ptr = binding_data_store_[binding_index];
  if (!ptr) [[unlikely]] {
    return nullptr;
  }
  T* result = static_cast<T*>(ptr.get());
  DCHECK_NOT_NULL(result);
  return result;
}

template <typename T, typename... Args>
inline T* Realm::AddBindingData(v8::Local<v8::Object> target, Args&&... args) {
  // This won't compile if T is not a BaseObject subclass.
  static_assert(std::is_base_of_v<BaseObject, T>);
  // The binding data must be weak so that it won't keep the realm reachable
  // from strong GC roots indefinitely. The wrapper object of binding data
  // should be referenced from JavaScript, thus the binding data should be
  // reachable throughout the lifetime of the realm.
  BaseObjectWeakPtr<T> item =
      MakeWeakBaseObject<T>(this, target, std::forward<Args>(args)...);
  constexpr size_t binding_index = static_cast<size_t>(T::binding_type_int);
  static_assert(binding_index < std::tuple_size_v<BindingDataStore>);
  // Each slot is expected to be assigned only once.
  CHECK(!binding_data_store_[binding_index]);
  binding_data_store_[binding_index] = item;
  return item.get();
}

inline BindingDataStore* Realm::binding_data_store() {
  return &binding_data_store_;
}

template <typename T>
void Realm::ForEachBaseObject(T&& iterator) const {
  for (auto bo : base_object_list_) {
    iterator(bo);
  }
}

int64_t Realm::base_object_created_after_bootstrap() const {
  return base_object_count_ - base_object_created_by_bootstrap_;
}

int64_t Realm::base_object_count() const {
  return base_object_count_;
}

void Realm::TrackBaseObject(BaseObject* bo) {
  DCHECK_EQ(bo->realm(), this);
  base_object_list_.PushBack(bo);
  ++base_object_count_;
}

void Realm::TrackCppgcWrapper(CppgcMixin* handle) {
  DCHECK_EQ(handle->realm(), this);
  cppgc_wrapper_list_.PushFront(handle);
}

void Realm::UntrackBaseObject(BaseObject* bo) {
  DCHECK_EQ(bo->realm(), this);
  --base_object_count_;
}

bool Realm::PendingCleanup() const {
  return !base_object_list_.IsEmpty();
}

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_REALM_INL_H_
