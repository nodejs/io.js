#include "cppgc_helpers.h"
#include "env-inl.h"

namespace node {

template <typename T>
void CppgcMixin::Wrap(T* ptr, Realm* realm, v8::Local<v8::Object> obj) {
  CHECK_GE(obj->InternalFieldCount(), T::kInternalFieldCount);
  ptr->realm_ = realm;
  v8::Isolate* isolate = realm->isolate();
  ptr->traced_reference_ = v8::TracedReference<v8::Object>(isolate, obj);
  // Note that ptr must be of concrete type T in Wrap.
  v8::Object::Wrap<v8::CppHeapPointerTag::kDefaultTag>(isolate, obj, ptr);
  // Keep the layout consistent with BaseObjects.
  obj->SetAlignedPointerInInternalField(
      kEmbedderType, realm->isolate_data()->embedder_id_for_cppgc());
  obj->SetAlignedPointerInInternalField(kSlot, ptr);
  realm->TrackCppgcWrapper(ptr);
}

template <typename T>
void CppgcMixin::Wrap(T* ptr, Environment* env, v8::Local<v8::Object> obj) {
  Wrap(ptr, env->principal_realm(), obj);
}

template <typename T>
T* CppgcMixin::Unwrap(v8::Local<v8::Object> obj) {
  // We are not using v8::Object::Unwrap currently because that requires
  // access to isolate which the ASSIGN_OR_RETURN_UNWRAP macro that we'll shim
  // with ASSIGN_OR_RETURN_UNWRAP_GC doesn't take, and we also want a
  // signature consistent with BaseObject::Unwrap() to avoid churn. Since
  // cppgc-managed objects share the same layout as BaseObjects, just unwrap
  // from the pointer in the internal field, which should be valid as long as
  // the object is still alive.
  if (obj->InternalFieldCount() != T::kInternalFieldCount) {
    return nullptr;
  }
  T* ptr = static_cast<T*>(obj->GetAlignedPointerFromInternalField(T::kSlot));
  return ptr;
}

v8::Local<v8::Object> CppgcMixin::object() const {
  return traced_reference_.Get(realm_->isolate());
}

Environment* CppgcMixin::env() const {
  return realm_->env();
}

v8::Local<v8::Object> CppgcMixin::WrappedObject() const {
  return object();
}

}  // namespace node