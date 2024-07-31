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

#include "tty_wrap.h"

#include "env-inl.h"
#include "handle_wrap.h"
#include "node_buffer.h"
#include "node_external_reference.h"
#include "stream_base-inl.h"
#include "stream_wrap.h"
#include "util-inl.h"

namespace node {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

void TTYWrap::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(GetWindowSize);
  registry->Register(SetRawMode);
  registry->Register(IsTTY);
}

void TTYWrap::Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context,
                         void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  Local<String> ttyString = FIXED_ONE_BYTE_STRING(env->isolate(), "TTY");

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);
  t->SetClassName(ttyString);
  t->InstanceTemplate()->SetInternalFieldCount(StreamBase::kInternalFieldCount);
  t->Inherit(LibuvStreamWrap::GetConstructorTemplate(env));

  SetProtoMethodNoSideEffect(
      isolate, t, "getWindowSize", TTYWrap::GetWindowSize);
  SetProtoMethod(isolate, t, "setRawMode", SetRawMode);

  SetMethodNoSideEffect(context, target, "isTTY", IsTTY);

  Local<Value> func;
  if (t->GetFunction(context).ToLocal(&func) &&
      target->Set(context, ttyString, func).IsJust()) {
    env->set_tty_constructor_template(t);
  }
}


void TTYWrap::IsTTY(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  int fd;
  if (!args[0]->Int32Value(env->context()).To(&fd)) return;
  CHECK_GE(fd, 0);
  bool rc = uv_guess_handle(fd) == UV_TTY;
  args.GetReturnValue().Set(rc);
}


void TTYWrap::GetWindowSize(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  TTYWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  CHECK(args[0]->IsArray());

  int width, height;
  int err = uv_tty_get_winsize(&wrap->handle_, &width, &height);

  if (err == 0) {
    Local<Array> a = args[0].As<Array>();
    a->Set(env->context(), 0, Integer::New(env->isolate(), width)).Check();
    a->Set(env->context(), 1, Integer::New(env->isolate(), height)).Check();
  }

  args.GetReturnValue().Set(err);
}


void TTYWrap::SetRawMode(const FunctionCallbackInfo<Value>& args) {
  TTYWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  int err = uv_tty_set_mode(&wrap->handle_, args[0]->IsTrue());
  args.GetReturnValue().Set(err);
}


void TTYWrap::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  CHECK(args.IsConstructCall());

  int fd;
  if (!args[0]->Int32Value(env->context()).To(&fd)) return;
  CHECK_GE(fd, 0);

  int err = 0;
  new TTYWrap(env, args.This(), fd, &err);
  if (err != 0) {
    env->CollectUVExceptionInfo(args[1], err, "uv_tty_init");
    args.GetReturnValue().SetUndefined();
  }
}


TTYWrap::TTYWrap(Environment* env,
                 Local<Object> object,
                 int fd,
                 int* init_err)
    : LibuvStreamWrap(env,
                      object,
                      reinterpret_cast<uv_stream_t*>(&handle_),
                      AsyncWrap::PROVIDER_TTYWRAP) {
  *init_err = uv_tty_init(env->event_loop(), &handle_, fd, 0);
  set_fd(fd);
  if (*init_err != 0)
    MarkAsUninitialized();
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(tty_wrap, node::TTYWrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(tty_wrap,
                                node::TTYWrap::RegisterExternalReferences)
