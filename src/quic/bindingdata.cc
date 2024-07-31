#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#include "bindingdata.h"
#include <base_object-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <node.h>
#include <node_errors.h>
#include <node_external_reference.h>
#include <node_mem-inl.h>
#include <node_realm-inl.h>
#include <v8.h>

namespace node {

using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

namespace quic {

BindingData& BindingData::Get(Environment* env) {
  return *(env->principal_realm()->GetBindingData<BindingData>());
}

BindingData::operator ngtcp2_mem() {
  return MakeAllocator();
}

BindingData::operator nghttp3_mem() {
  ngtcp2_mem allocator = *this;
  nghttp3_mem http3_allocator = {
      allocator.user_data,
      allocator.malloc,
      allocator.free,
      allocator.calloc,
      allocator.realloc,
  };
  return http3_allocator;
}

void BindingData::CheckAllocatedSize(size_t previous_size) const {
  CHECK_GE(current_ngtcp2_memory_, previous_size);
}

void BindingData::IncreaseAllocatedSize(size_t size) {
  current_ngtcp2_memory_ += size;
}

void BindingData::DecreaseAllocatedSize(size_t size) {
  current_ngtcp2_memory_ -= size;
}

void BindingData::InitPerContext(Realm* realm, Local<Object> target) {
  SetMethod(realm->context(), target, "setCallbacks", SetCallbacks);
  SetMethod(
      realm->context(), target, "flushPacketFreelist", FlushPacketFreelist);
  Realm::GetCurrent(realm->context())->AddBindingData<BindingData>(target);
}

void BindingData::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(IllegalConstructor);
  registry->Register(SetCallbacks);
  registry->Register(FlushPacketFreelist);
}

BindingData::BindingData(Realm* realm, Local<Object> object)
    : BaseObject(realm, object) {
  MakeWeak();
}

void BindingData::MemoryInfo(MemoryTracker* tracker) const {
#define V(name, _) tracker->TrackField(#name, name##_callback());

  QUIC_JS_CALLBACKS(V)

#undef V

#define V(name, _) tracker->TrackField(#name, name##_string());

  QUIC_STRINGS(V)

#undef V
}

#define V(name)                                                                \
  void BindingData::set_##name##_constructor_template(                         \
      Local<FunctionTemplate> tmpl) {                                          \
    name##_constructor_template_.Reset(env()->isolate(), tmpl);                \
  }                                                                            \
  Local<FunctionTemplate> BindingData::name##_constructor_template() const {   \
    return PersistentToLocal::Default(env()->isolate(),                        \
                                      name##_constructor_template_);           \
  }

QUIC_CONSTRUCTORS(V)

#undef V

#define V(name, _)                                                             \
  void BindingData::set_##name##_callback(Local<Function> fn) {                \
    name##_callback_.Reset(env()->isolate(), fn);                              \
  }                                                                            \
  Local<Function> BindingData::name##_callback() const {                       \
    return PersistentToLocal::Default(env()->isolate(), name##_callback_);     \
  }

QUIC_JS_CALLBACKS(V)

#undef V

#define V(name, value)                                                         \
  Local<String> BindingData::name##_string() const {                           \
    if (name##_string_.IsEmpty())                                              \
      name##_string_.Set(env()->isolate(),                                     \
                         OneByteString(env()->isolate(), value));              \
    return name##_string_.Get(env()->isolate());                               \
  }

QUIC_STRINGS(V)

#undef V

#define V(name, value)                                                         \
  Local<String> BindingData::on_##name##_string() const {                      \
    if (on_##name##_string_.IsEmpty())                                         \
      on_##name##_string_.Set(                                                 \
          env()->isolate(),                                                    \
          FIXED_ONE_BYTE_STRING(env()->isolate(), "on" #value));               \
    return on_##name##_string_.Get(env()->isolate());                          \
  }

QUIC_JS_CALLBACKS(V)

#undef V

void BindingData::SetCallbacks(const FunctionCallbackInfo<Value>& args) {
  auto env = Environment::GetCurrent(args);
  auto isolate = env->isolate();
  auto& state = BindingData::Get(env);
  CHECK(args[0]->IsObject());
  Local<Object> obj = args[0].As<Object>();

#define V(name, key)                                                           \
  do {                                                                         \
    Local<Value> val;                                                          \
    if (!obj->Get(env->context(), state.on_##name##_string()).ToLocal(&val) || \
        !val->IsFunction()) {                                                  \
      return THROW_ERR_MISSING_ARGS(isolate, "Missing Callback: on" #key);     \
    }                                                                          \
    state.set_##name##_callback(val.As<Function>());                           \
  } while (0);

  QUIC_JS_CALLBACKS(V)

#undef V
}

void BindingData::FlushPacketFreelist(const FunctionCallbackInfo<Value>& args) {
  auto env = Environment::GetCurrent(args);
  auto& state = BindingData::Get(env);
  state.packet_freelist.clear();
}

NgTcp2CallbackScope::NgTcp2CallbackScope(Environment* env) : env(env) {
  auto& binding = BindingData::Get(env);
  CHECK(!binding.in_ngtcp2_callback_scope);
  binding.in_ngtcp2_callback_scope = true;
}

NgTcp2CallbackScope::~NgTcp2CallbackScope() {
  auto& binding = BindingData::Get(env);
  binding.in_ngtcp2_callback_scope = false;
}

bool NgTcp2CallbackScope::in_ngtcp2_callback(Environment* env) {
  auto& binding = BindingData::Get(env);
  return binding.in_ngtcp2_callback_scope;
}

NgHttp3CallbackScope::NgHttp3CallbackScope(Environment* env) : env(env) {
  auto& binding = BindingData::Get(env);
  CHECK(!binding.in_nghttp3_callback_scope);
  binding.in_nghttp3_callback_scope = true;
}

NgHttp3CallbackScope::~NgHttp3CallbackScope() {
  auto& binding = BindingData::Get(env);
  binding.in_nghttp3_callback_scope = false;
}

bool NgHttp3CallbackScope::in_nghttp3_callback(Environment* env) {
  auto& binding = BindingData::Get(env);
  return binding.in_nghttp3_callback_scope;
}

CallbackScopeBase::CallbackScopeBase(Environment* env)
    : env(env), context_scope(env->context()), try_catch(env->isolate()) {}

CallbackScopeBase::~CallbackScopeBase() {
  if (try_catch.HasCaught()) {
    if (!try_catch.HasTerminated() && env->can_call_into_js()) {
      errors::TriggerUncaughtException(env->isolate(), try_catch);
    } else {
      try_catch.ReThrow();
    }
  }
}

void IllegalConstructor(const FunctionCallbackInfo<Value>& args) {
  THROW_ERR_ILLEGAL_CONSTRUCTOR(Environment::GetCurrent(args));
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
