#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <base_object-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <node_realm-inl.h>
#include <node_sockaddr-inl.h>
#include <v8.h>
#include "bindingdata.h"
#include "endpoint.h"
#include "node_external_reference.h"

namespace node {

using v8::Context;
using v8::Local;
using v8::Object;
using v8::ObjectTemplate;
using v8::Value;

namespace quic {

int DebugIndentScope::indent_ = 0;

void CreatePerIsolateProperties(IsolateData* isolate_data,
                                Local<ObjectTemplate> target) {
  Endpoint::InitPerIsolate(isolate_data, target);
  Session::InitPerIsolate(isolate_data, target);
  Stream::InitPerIsolate(isolate_data, target);
}

void CreatePerContextProperties(Local<Object> target,
                                Local<Value> unused,
                                Local<Context> context,
                                void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  BindingData::InitPerContext(realm, target);
  Endpoint::InitPerContext(realm, target);
  Session::InitPerContext(realm, target);
  Stream::InitPerContext(realm, target);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  BindingData::RegisterExternalReferences(registry);
  Endpoint::RegisterExternalReferences(registry);
  Session::RegisterExternalReferences(registry);
  Stream::RegisterExternalReferences(registry);
}

}  // namespace quic
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(quic,
                                    node::quic::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(quic, node::quic::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(quic, node::quic::RegisterExternalReferences)

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
