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

#include "pipe_wrap.h"

#include "async_wrap.h"
#include "connect_wrap.h"
#include "connection_wrap.h"
#include "env-inl.h"
#include "handle_wrap.h"
#include "node.h"
#include "node_buffer.h"
#include "node_external_reference.h"
#include "stream_base-inl.h"
#include "stream_wrap.h"
#include "util-inl.h"

namespace node {

using v8::Context;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::Value;

MaybeLocal<Object> PipeWrap::Instantiate(Environment* env,
                                         AsyncWrap* parent,
                                         PipeWrap::SocketType type) {
  EscapableHandleScope handle_scope(env->isolate());
  AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(parent);
  CHECK_EQ(false, env->pipe_constructor_template().IsEmpty());
  Local<Function> constructor = env->pipe_constructor_template()
                                    ->GetFunction(env->context())
                                    .ToLocalChecked();
  CHECK_EQ(false, constructor.IsEmpty());
  Local<Value> type_value = Int32::New(env->isolate(), type);
  return handle_scope.EscapeMaybe(
      constructor->NewInstance(env->context(), 1, &type_value));
}

void PipeWrap::Initialize(Local<Object> target,
                          Local<Value> unused,
                          Local<Context> context,
                          void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);
  t->InstanceTemplate()->SetInternalFieldCount(StreamBase::kInternalFieldCount);

  t->Inherit(LibuvStreamWrap::GetConstructorTemplate(env));

  SetProtoMethod(isolate, t, "bind", Bind);
  SetProtoMethod(isolate, t, "listen", Listen);
  SetProtoMethod(isolate, t, "connect", Connect);
  SetProtoMethod(isolate, t, "open", Open);

#ifdef _WIN32
  SetProtoMethod(isolate, t, "setPendingInstances", SetPendingInstances);
#endif

  SetProtoMethod(isolate, t, "fchmod", Fchmod);

  SetConstructorFunction(context, target, "Pipe", t);
  env->set_pipe_constructor_template(t);

  // Create FunctionTemplate for PipeConnectWrap.
  auto cwt = BaseObject::MakeLazilyInitializedJSTemplate(env);
  cwt->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetConstructorFunction(context, target, "PipeConnectWrap", cwt);

  // Define constants
  Local<Object> constants = Object::New(env->isolate());
  NODE_DEFINE_CONSTANT(constants, SOCKET);
  NODE_DEFINE_CONSTANT(constants, SERVER);
  NODE_DEFINE_CONSTANT(constants, IPC);
  NODE_DEFINE_CONSTANT(constants, UV_READABLE);
  NODE_DEFINE_CONSTANT(constants, UV_WRITABLE);
  target->Set(context, env->constants_string(), constants).Check();
}

void PipeWrap::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(Bind);
  registry->Register(Listen);
  registry->Register(Connect);
  registry->Register(Open);
#ifdef _WIN32
  registry->Register(SetPendingInstances);
#endif
  registry->Register(Fchmod);
}

void PipeWrap::New(const FunctionCallbackInfo<Value>& args) {
  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  CHECK(args.IsConstructCall());
  CHECK(args[0]->IsInt32());
  Environment* env = Environment::GetCurrent(args);

  int type_value = args[0].As<Int32>()->Value();
  PipeWrap::SocketType type = static_cast<PipeWrap::SocketType>(type_value);

  bool ipc;
  ProviderType provider;
  switch (type) {
    case SOCKET:
      provider = PROVIDER_PIPEWRAP;
      ipc = false;
      break;
    case SERVER:
      provider = PROVIDER_PIPESERVERWRAP;
      ipc = false;
      break;
    case IPC:
      provider = PROVIDER_PIPEWRAP;
      ipc = true;
      break;
    default:
      UNREACHABLE();
  }

  new PipeWrap(env, args.This(), provider, ipc);
}

PipeWrap::PipeWrap(Environment* env,
                   Local<Object> object,
                   ProviderType provider,
                   bool ipc)
    : ConnectionWrap(env, object, provider) {
  int r = uv_pipe_init(env->event_loop(), &handle_, ipc);
  CHECK_EQ(r, 0);  // How do we proxy this error up to javascript?
                   // Suggestion: uv_pipe_init() returns void.
}

void PipeWrap::Bind(const FunctionCallbackInfo<Value>& args) {
  PipeWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  node::Utf8Value name(args.GetIsolate(), args[0]);
  int err =
      uv_pipe_bind2(&wrap->handle_, *name, name.length(), UV_PIPE_NO_TRUNCATE);
  args.GetReturnValue().Set(err);
}

#ifdef _WIN32
void PipeWrap::SetPendingInstances(const FunctionCallbackInfo<Value>& args) {
  PipeWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  CHECK(args[0]->IsInt32());
  int instances = args[0].As<Int32>()->Value();
  uv_pipe_pending_instances(&wrap->handle_, instances);
}
#endif

void PipeWrap::Fchmod(const v8::FunctionCallbackInfo<v8::Value>& args) {
  PipeWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  CHECK(args[0]->IsInt32());
  int mode = args[0].As<Int32>()->Value();
  int err = uv_pipe_chmod(&wrap->handle_, mode);
  args.GetReturnValue().Set(err);
}

void PipeWrap::Listen(const FunctionCallbackInfo<Value>& args) {
  PipeWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  Environment* env = wrap->env();
  int backlog;
  if (!args[0]->Int32Value(env->context()).To(&backlog)) return;
  int err = uv_listen(
      reinterpret_cast<uv_stream_t*>(&wrap->handle_), backlog, OnConnection);
  args.GetReturnValue().Set(err);
}

void PipeWrap::Open(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  PipeWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  int fd;
  if (!args[0]->Int32Value(env->context()).To(&fd)) return;

  int err = uv_pipe_open(&wrap->handle_, fd);
  if (err == 0) wrap->set_fd(fd);

  args.GetReturnValue().Set(err);
}

void PipeWrap::Connect(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  PipeWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsString());

  Local<Object> req_wrap_obj = args[0].As<Object>();
  node::Utf8Value name(env->isolate(), args[1]);

  ConnectWrap* req_wrap =
      new ConnectWrap(env, req_wrap_obj, AsyncWrap::PROVIDER_PIPECONNECTWRAP);
  int err = req_wrap->Dispatch(uv_pipe_connect2,
                               &wrap->handle_,
                               *name,
                               name.length(),
                               UV_PIPE_NO_TRUNCATE,
                               AfterConnect);
  if (err) {
    delete req_wrap;
  } else {
    const char* path_type = (*name)[0] == '\0' ? "abstract socket" : "file";
    const char* pipe_path = (*name)[0] == '\0' ? (*name) + 1 : *name;
    TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(TRACING_CATEGORY_NODE2(net, native),
                                      "connect",
                                      req_wrap,
                                      "path_type",
                                      path_type,
                                      "pipe_path",
                                      TRACE_STR_COPY(pipe_path));
  }

  args.GetReturnValue().Set(err);
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(pipe_wrap, node::PipeWrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(pipe_wrap,
                                node::PipeWrap::RegisterExternalReferences)
