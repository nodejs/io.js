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

#include "stream_wrap.h"
#include "stream_base-inl.h"

#include "env-inl.h"
#include "handle_wrap.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "pipe_wrap.h"
#include "req_wrap-inl.h"
#include "tcp_wrap.h"
#include "udp_wrap.h"
#include "util-inl.h"

#include <cstring>  // memcpy()
#include <climits>  // INT_MAX


namespace node {

using errors::TryCatchScope;
using v8::Context;
using v8::DontDelete;
using v8::EscapableHandleScope;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Isolate;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::Signature;
using v8::Value;

void IsConstructCallCallback(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  StreamReq::ResetObject(args.This());
}

void LibuvStreamWrap::Initialize(Local<Object> target,
                                 Local<Value> unused,
                                 Local<Context> context,
                                 void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  Local<FunctionTemplate> sw =
      NewFunctionTemplate(isolate, IsConstructCallCallback);
  sw->InstanceTemplate()->SetInternalFieldCount(StreamReq::kInternalFieldCount);

  // we need to set handle and callback to null,
  // so that those fields are created and functions
  // do not become megamorphic
  // Fields:
  // - oncomplete
  // - callback
  // - handle
  sw->InstanceTemplate()->Set(env->oncomplete_string(), v8::Null(isolate));
  sw->InstanceTemplate()->Set(FIXED_ONE_BYTE_STRING(isolate, "callback"),
                              v8::Null(isolate));
  sw->InstanceTemplate()->Set(FIXED_ONE_BYTE_STRING(isolate, "handle"),
                              v8::Null(isolate));

  sw->Inherit(AsyncWrap::GetConstructorTemplate(env));

  SetConstructorFunction(context, target, "ShutdownWrap", sw);
  env->set_shutdown_wrap_template(sw->InstanceTemplate());

  Local<FunctionTemplate> ww =
      FunctionTemplate::New(isolate, IsConstructCallCallback);
  ww->InstanceTemplate()->SetInternalFieldCount(
      StreamReq::kInternalFieldCount);
  ww->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetConstructorFunction(context, target, "WriteWrap", ww);
  env->set_write_wrap_template(ww->InstanceTemplate());

  NODE_DEFINE_CONSTANT(target, kReadBytesOrError);
  NODE_DEFINE_CONSTANT(target, kArrayBufferOffset);
  NODE_DEFINE_CONSTANT(target, kBytesWritten);
  NODE_DEFINE_CONSTANT(target, kLastWriteWasAsync);
  target
      ->Set(context,
            FIXED_ONE_BYTE_STRING(isolate, "streamBaseState"),
            env->stream_base_state().GetJSArray())
      .Check();
}

void LibuvStreamWrap::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(IsConstructCallCallback);
  registry->Register(GetWriteQueueSize);
  registry->Register(SetBlocking);
  StreamBase::RegisterExternalReferences(registry);
}

LibuvStreamWrap::LibuvStreamWrap(Environment* env,
                                 Local<Object> object,
                                 uv_stream_t* stream,
                                 AsyncWrap::ProviderType provider)
    : HandleWrap(env,
                 object,
                 reinterpret_cast<uv_handle_t*>(stream),
                 provider),
      StreamBase(env),
      stream_(stream) {
  StreamBase::AttachToObject(object);
}


Local<FunctionTemplate> LibuvStreamWrap::GetConstructorTemplate(
    Environment* env) {
  Local<FunctionTemplate> tmpl = env->libuv_stream_wrap_ctor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, nullptr);
    tmpl->SetClassName(FIXED_ONE_BYTE_STRING(isolate, "LibuvStreamWrap"));
    tmpl->Inherit(HandleWrap::GetConstructorTemplate(env));
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        StreamBase::kInternalFieldCount);
    Local<FunctionTemplate> get_write_queue_size =
        FunctionTemplate::New(isolate,
                              GetWriteQueueSize,
                              Local<Value>(),
                              Signature::New(isolate, tmpl));
    tmpl->PrototypeTemplate()->SetAccessorProperty(
        env->write_queue_size_string(),
        get_write_queue_size,
        Local<FunctionTemplate>(),
        static_cast<PropertyAttribute>(ReadOnly | DontDelete));
    SetProtoMethod(isolate, tmpl, "setBlocking", SetBlocking);
    StreamBase::AddMethods(env, tmpl);
    env->set_libuv_stream_wrap_ctor_template(tmpl);
  }
  return tmpl;
}


LibuvStreamWrap* LibuvStreamWrap::From(Environment* env, Local<Object> object) {
  Local<FunctionTemplate> sw = env->libuv_stream_wrap_ctor_template();
  CHECK(!sw.IsEmpty() && sw->HasInstance(object));
  return Unwrap<LibuvStreamWrap>(object);
}


int LibuvStreamWrap::GetFD() {
#ifdef _WIN32
  return fd_;
#else
  int fd = -1;
  if (stream() != nullptr)
    uv_fileno(reinterpret_cast<uv_handle_t*>(stream()), &fd);
  return fd;
#endif
}


bool LibuvStreamWrap::IsAlive() {
  return HandleWrap::IsAlive(this);
}


bool LibuvStreamWrap::IsClosing() {
  return uv_is_closing(reinterpret_cast<uv_handle_t*>(stream()));
}


AsyncWrap* LibuvStreamWrap::GetAsyncWrap() {
  return static_cast<AsyncWrap*>(this);
}


bool LibuvStreamWrap::IsIPCPipe() {
  return is_named_pipe_ipc();
}

int LibuvStreamWrap::ReadStart() {
  return uv_read_start(
      stream(),
      [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
        static_cast<LibuvStreamWrap*>(handle->data)
            ->OnUvAlloc(suggested_size, buf);
      },
      [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
        LibuvStreamWrap* wrap = static_cast<LibuvStreamWrap*>(stream->data);
        TryCatchScope try_catch(wrap->env());
        try_catch.SetVerbose(true);
        wrap->OnUvRead(nread, buf);
      });
}


int LibuvStreamWrap::ReadStop() {
  return uv_read_stop(stream());
}


void LibuvStreamWrap::OnUvAlloc(size_t suggested_size, uv_buf_t* buf) {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());

  *buf = EmitAlloc(suggested_size);
}

template <class WrapType>
static MaybeLocal<Object> AcceptHandle(Environment* env,
                                       LibuvStreamWrap* parent) {
  static_assert(std::is_base_of<LibuvStreamWrap, WrapType>::value ||
                std::is_base_of<UDPWrap, WrapType>::value,
                "Can only accept stream handles");

  EscapableHandleScope scope(env->isolate());
  Local<Object> wrap_obj;

  if (!WrapType::Instantiate(env, parent, WrapType::SOCKET).ToLocal(&wrap_obj))
    return Local<Object>();

  HandleWrap* wrap = BaseObject::Unwrap<HandleWrap>(wrap_obj);
  CHECK_NOT_NULL(wrap);
  uv_stream_t* stream = reinterpret_cast<uv_stream_t*>(wrap->GetHandle());
  CHECK_NOT_NULL(stream);

  if (uv_accept(parent->stream(), stream))
    ABORT();

  return scope.Escape(wrap_obj);
}

Maybe<void> LibuvStreamWrap::OnUvRead(ssize_t nread, const uv_buf_t* buf) {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());
  uv_handle_type type = UV_UNKNOWN_HANDLE;

  if (is_named_pipe_ipc() &&
      uv_pipe_pending_count(reinterpret_cast<uv_pipe_t*>(stream())) > 0) {
    type = uv_pipe_pending_type(reinterpret_cast<uv_pipe_t*>(stream()));
  }

  // We should not be getting this callback if someone has already called
  // uv_close() on the handle.
  CHECK_EQ(persistent().IsEmpty(), false);

  if (nread > 0) {
    MaybeLocal<Object> pending_obj;

    if (type == UV_TCP) {
      pending_obj = AcceptHandle<TCPWrap>(env(), this);
    } else if (type == UV_NAMED_PIPE) {
      pending_obj = AcceptHandle<PipeWrap>(env(), this);
    } else if (type == UV_UDP) {
      pending_obj = AcceptHandle<UDPWrap>(env(), this);
    } else {
      CHECK_EQ(type, UV_UNKNOWN_HANDLE);
    }

    Local<Object> local_pending_obj;
    if (type != UV_UNKNOWN_HANDLE &&
        (!pending_obj.ToLocal(&local_pending_obj) ||
         object()
             ->Set(env()->context(),
                   env()->pending_handle_string(),
                   local_pending_obj)
             .IsNothing())) {
      return Nothing<void>();
    }
  }

  EmitRead(nread, *buf);
  return JustVoid();
}

void LibuvStreamWrap::GetWriteQueueSize(
    const FunctionCallbackInfo<Value>& info) {
  LibuvStreamWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, info.This());

  if (wrap->stream() == nullptr) {
    info.GetReturnValue().Set(0);
    return;
  }

  uint32_t write_queue_size = wrap->stream()->write_queue_size;
  info.GetReturnValue().Set(write_queue_size);
}


void LibuvStreamWrap::SetBlocking(const FunctionCallbackInfo<Value>& args) {
  LibuvStreamWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  CHECK_GT(args.Length(), 0);
  if (!wrap->IsAlive())
    return args.GetReturnValue().Set(UV_EINVAL);

  bool enable = args[0]->IsTrue();
  args.GetReturnValue().Set(uv_stream_set_blocking(wrap->stream(), enable));
}

typedef SimpleShutdownWrap<ReqWrap<uv_shutdown_t>> LibuvShutdownWrap;
typedef SimpleWriteWrap<ReqWrap<uv_write_t>> LibuvWriteWrap;

ShutdownWrap* LibuvStreamWrap::CreateShutdownWrap(Local<Object> object) {
  return new LibuvShutdownWrap(this, object);
}

WriteWrap* LibuvStreamWrap::CreateWriteWrap(Local<Object> object) {
  return new LibuvWriteWrap(this, object);
}


int LibuvStreamWrap::DoShutdown(ShutdownWrap* req_wrap_) {
  LibuvShutdownWrap* req_wrap = static_cast<LibuvShutdownWrap*>(req_wrap_);
  return req_wrap->Dispatch(uv_shutdown, stream(), AfterUvShutdown);
}


void LibuvStreamWrap::AfterUvShutdown(uv_shutdown_t* req, int status) {
  LibuvShutdownWrap* req_wrap = static_cast<LibuvShutdownWrap*>(
      LibuvShutdownWrap::from_req(req));
  CHECK_NOT_NULL(req_wrap);
  HandleScope scope(req_wrap->env()->isolate());
  Context::Scope context_scope(req_wrap->env()->context());
  req_wrap->Done(status);
}


// NOTE: Call to this function could change both `buf`'s and `count`'s
// values, shifting their base and decrementing their length. This is
// required in order to skip the data that was successfully written via
// uv_try_write().
int LibuvStreamWrap::DoTryWrite(uv_buf_t** bufs, size_t* count) {
  int err;
  size_t written;
  uv_buf_t* vbufs = *bufs;
  size_t vcount = *count;

  err = uv_try_write(stream(), vbufs, vcount);
  if (err == UV_ENOSYS || err == UV_EAGAIN)
    return 0;
  if (err < 0)
    return err;

  // Slice off the buffers: skip all written buffers and slice the one that
  // was partially written.
  written = err;
  for (; vcount > 0; vbufs++, vcount--) {
    // Slice
    if (vbufs[0].len > written) {
      vbufs[0].base += written;
      vbufs[0].len -= written;
      written = 0;
      break;

    // Discard
    } else {
      written -= vbufs[0].len;
    }
  }

  *bufs = vbufs;
  *count = vcount;

  return 0;
}


int LibuvStreamWrap::DoWrite(WriteWrap* req_wrap,
                             uv_buf_t* bufs,
                             size_t count,
                             uv_stream_t* send_handle) {
  LibuvWriteWrap* w = static_cast<LibuvWriteWrap*>(req_wrap);
  return w->Dispatch(uv_write2,
                     stream(),
                     bufs,
                     count,
                     send_handle,
                     AfterUvWrite);
}



void LibuvStreamWrap::AfterUvWrite(uv_write_t* req, int status) {
  LibuvWriteWrap* req_wrap = static_cast<LibuvWriteWrap*>(
      LibuvWriteWrap::from_req(req));
  CHECK_NOT_NULL(req_wrap);
  HandleScope scope(req_wrap->env()->isolate());
  Context::Scope context_scope(req_wrap->env()->context());
  req_wrap->Done(status);
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(stream_wrap,
                                    node::LibuvStreamWrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(
    stream_wrap, node::LibuvStreamWrap::RegisterExternalReferences)
