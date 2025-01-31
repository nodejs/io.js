#include "async_context_frame.h"
#include "env-inl.h"
#include "node.h"

namespace node {

using v8::Function;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::String;
using v8::Value;

AsyncResource::AsyncResource(Isolate* isolate,
                             Local<Object> resource,
                             const char* name,
                             async_id trigger_async_id)
    : env_(Environment::GetCurrent(isolate)),
      resource_(isolate, resource),
      context_frame_(isolate, async_context_frame::current(isolate)) {
  CHECK_NOT_NULL(env_);
  async_context_ = EmitAsyncInit(isolate, resource, name, trigger_async_id);
}

AsyncResource::~AsyncResource() {
  CHECK_NOT_NULL(env_);
  EmitAsyncDestroy(env_, async_context_);
}

MaybeLocal<Value> AsyncResource::MakeCallback(Local<Function> callback,
                                              int argc,
                                              Local<Value>* argv) {
  auto isolate = env_->isolate();
  async_context_frame::Scope async_context_frame_scope(
      isolate, context_frame_.Get(isolate));

  return node::MakeCallback(
      isolate, get_resource(), callback, argc, argv, async_context_);
}

MaybeLocal<Value> AsyncResource::MakeCallback(const char* method,
                                              int argc,
                                              Local<Value>* argv) {
  auto isolate = env_->isolate();
  async_context_frame::Scope async_context_frame_scope(
      isolate, context_frame_.Get(isolate));

  return node::MakeCallback(
      isolate, get_resource(), method, argc, argv, async_context_);
}

MaybeLocal<Value> AsyncResource::MakeCallback(Local<String> symbol,
                                              int argc,
                                              Local<Value>* argv) {
  auto isolate = env_->isolate();
  async_context_frame::Scope async_context_frame_scope(
      isolate, context_frame_.Get(isolate));

  return node::MakeCallback(
      isolate, get_resource(), symbol, argc, argv, async_context_);
}

Local<Object> AsyncResource::get_resource() {
  return resource_.Get(env_->isolate());
}

async_id AsyncResource::get_async_id() const {
  return async_context_.async_id;
}

async_id AsyncResource::get_trigger_async_id() const {
  return async_context_.trigger_async_id;
}

AsyncResource::CallbackScope::CallbackScope(AsyncResource* res)
    : node::CallbackScope(res->env_,
                          res->resource_.Get(res->env_->isolate()),
                          res->async_context_) {}

}  // namespace node
