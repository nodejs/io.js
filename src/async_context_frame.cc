#include "async_context_frame.h"  // NOLINT(build/include_inline)

#include "env-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "tracing/traced_value.h"
#include "util-inl.h"

#include "debug_utils-inl.h"

#include "v8.h"

using v8::Context;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

namespace node {
namespace async_context_frame {

//
// Scope helper
//
Scope::Scope(Isolate* isolate, Local<Value> object) : isolate_(isolate) {
  auto prior = exchange(isolate, object);
  prior_.Reset(isolate, prior);
}

Scope::~Scope() {
  auto value = prior_.Get(isolate_);
  set(isolate_, value);
}

Local<Value> current(Isolate* isolate) {
  return isolate->GetContinuationPreservedEmbedderData();
}

void set(Isolate* isolate, Local<Value> value) {
  auto env = Environment::GetCurrent(isolate);
  if (!env->options()->async_context_frame) {
    return;
  }

  isolate->SetContinuationPreservedEmbedderData(value);
}

// NOTE: It's generally recommended to use async_context_frame::Scope
// but sometimes (such as enterWith) a direct exchange is needed.
Local<Value> exchange(Isolate* isolate, Local<Value> value) {
  auto prior = current(isolate);
  set(isolate, value);
  return prior;
}

void CreatePerContextProperties(Local<Object> target,
                                Local<Value> unused,
                                Local<Context> context,
                                void* priv) {
  Environment* env = Environment::GetCurrent(context);

  Local<String> getContinuationPreservedEmbedderData = FIXED_ONE_BYTE_STRING(
      env->isolate(), "getContinuationPreservedEmbedderData");
  Local<String> setContinuationPreservedEmbedderData = FIXED_ONE_BYTE_STRING(
      env->isolate(), "setContinuationPreservedEmbedderData");

  // Grab the intrinsics from the binding object and expose those to our
  // binding layer.
  Local<Object> binding = context->GetExtrasBindingObject();
  target
      ->Set(context,
            getContinuationPreservedEmbedderData,
            binding->Get(context, getContinuationPreservedEmbedderData)
                .ToLocalChecked())
      .Check();
  target
      ->Set(context,
            setContinuationPreservedEmbedderData,
            binding->Get(context, setContinuationPreservedEmbedderData)
                .ToLocalChecked())
      .Check();
}

}  // namespace async_context_frame
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(
    async_context_frame, node::async_context_frame::CreatePerContextProperties)
