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

#include "handle_wrap.h"
#include "async_wrap-inl.h"
#include "env-inl.h"
#include "node_external_reference.h"
#include "util-inl.h"

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Value;


void HandleWrap::Ref(const FunctionCallbackInfo<Value>& args) {
  HandleWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  if (IsAlive(wrap))
    uv_ref(wrap->GetHandle());
}


void HandleWrap::Unref(const FunctionCallbackInfo<Value>& args) {
  HandleWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  if (IsAlive(wrap))
    uv_unref(wrap->GetHandle());
}


void HandleWrap::HasRef(const FunctionCallbackInfo<Value>& args) {
  HandleWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  args.GetReturnValue().Set(HasRef(wrap));
}


void HandleWrap::Close(const FunctionCallbackInfo<Value>& args) {
  HandleWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  wrap->Close(args[0]);
}

void HandleWrap::Close(Local<Value> close_callback) {
  if (state_ != kInitialized)
    return;

  uv_close(handle_, OnClose);
  state_ = kClosing;

  if (!close_callback.IsEmpty() && close_callback->IsFunction() &&
      !persistent().IsEmpty()) {
    object()->Set(env()->context(),
                  env()->handle_onclose_symbol(),
                  close_callback).Check();
  }
}


void HandleWrap::OnGCCollect() {
  // When all references to a HandleWrap are lost and the object is supposed to
  // be destroyed, we first call Close() to clean up the underlying libuv
  // handle. The OnClose callback then acquires and destroys another reference
  // to that object, and when that reference is lost, we perform the default
  // action (i.e. destroying `this`).
  if (state_ != kClosed) {
    Close();
  } else {
    BaseObject::OnGCCollect();
  }
}


bool HandleWrap::IsNotIndicativeOfMemoryLeakAtExit() const {
  return IsWeakOrDetached() ||
         !HandleWrap::HasRef(this) ||
         !uv_is_active(GetHandle());
}


void HandleWrap::MarkAsInitialized() {
  env()->handle_wrap_queue()->PushBack(this);
  state_ = kInitialized;
}


void HandleWrap::MarkAsUninitialized() {
  handle_wrap_queue_.Remove();
  state_ = kClosed;
}


HandleWrap::HandleWrap(Environment* env,
                       Local<Object> object,
                       uv_handle_t* handle,
                       AsyncWrap::ProviderType provider)
    : AsyncWrap(env, object, provider),
      state_(kInitialized),
      handle_(handle) {
  handle_->data = this;
  HandleScope scope(env->isolate());
  CHECK(env->has_run_bootstrapping_code());
  env->handle_wrap_queue()->PushBack(this);
}


void HandleWrap::OnClose(uv_handle_t* handle) {
  CHECK_NOT_NULL(handle->data);
  BaseObjectPtr<HandleWrap> wrap { static_cast<HandleWrap*>(handle->data) };
  wrap->Detach();

  Environment* env = wrap->env();
  HandleScope scope(env->isolate());
  Context::Scope context_scope(env->context());

  CHECK_EQ(wrap->state_, kClosing);

  wrap->state_ = kClosed;

  wrap->OnClose();
  wrap->handle_wrap_queue_.Remove();

  if (!wrap->persistent().IsEmpty() &&
      wrap->object()
          ->Has(env->context(), env->handle_onclose_symbol())
          .FromMaybe(false)) {
    wrap->MakeCallback(env->handle_onclose_symbol(), 0, nullptr);
  }
}
Local<FunctionTemplate> HandleWrap::GetConstructorTemplate(Environment* env) {
  return GetConstructorTemplate(env->isolate_data());
}

Local<FunctionTemplate> HandleWrap::GetConstructorTemplate(
    IsolateData* isolate_data) {
  Local<FunctionTemplate> tmpl = isolate_data->handle_wrap_ctor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = isolate_data->isolate();
    tmpl = NewFunctionTemplate(isolate, nullptr);
    tmpl->SetClassName(
        FIXED_ONE_BYTE_STRING(isolate_data->isolate(), "HandleWrap"));
    tmpl->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));
    SetProtoMethod(isolate, tmpl, "close", HandleWrap::Close);
    SetProtoMethodNoSideEffect(isolate, tmpl, "hasRef", HandleWrap::HasRef);
    SetProtoMethod(isolate, tmpl, "ref", HandleWrap::Ref);
    SetProtoMethod(isolate, tmpl, "unref", HandleWrap::Unref);
    isolate_data->set_handle_wrap_ctor_template(tmpl);
  }
  return tmpl;
}

void HandleWrap::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(HandleWrap::Close);
  registry->Register(HandleWrap::HasRef);
  registry->Register(HandleWrap::Ref);
  registry->Register(HandleWrap::Unref);
}

}  // namespace node

NODE_BINDING_EXTERNAL_REFERENCE(handle_wrap,
                                node::HandleWrap::RegisterExternalReferences)
