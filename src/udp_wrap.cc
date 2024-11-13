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

#include "udp_wrap.h"
#include "env-inl.h"
#include "handle_wrap.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_sockaddr-inl.h"
#include "req_wrap-inl.h"
#include "util-inl.h"

namespace node {

using errors::TryCatchScope;
using v8::Array;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::Boolean;
using v8::Context;
using v8::DontDelete;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::Signature;
using v8::Uint32;
using v8::Undefined;
using v8::Value;

namespace {
template <int (*fn)(uv_udp_t*, int)>
void SetLibuvInt32(const FunctionCallbackInfo<Value>& args) {
  UDPWrap* wrap = BaseObject::Unwrap<UDPWrap>(args.This());
  if (wrap == nullptr) {
    args.GetReturnValue().Set(UV_EBADF);
    return;
  }
  Environment* env = wrap->env();
  CHECK_EQ(args.Length(), 1);
  int flag;
  if (!args[0]->Int32Value(env->context()).To(&flag)) {
    return;
  }
  int err = fn(wrap->GetLibuvHandle(), flag);
  args.GetReturnValue().Set(err);
}
}  // namespace

class SendWrap : public ReqWrap<uv_udp_send_t> {
 public:
  SendWrap(Environment* env, Local<Object> req_wrap_obj, bool have_callback);
  inline bool have_callback() const;
  size_t msg_size;

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(SendWrap)
  SET_SELF_SIZE(SendWrap)

 private:
  const bool have_callback_;
};


SendWrap::SendWrap(Environment* env,
                   Local<Object> req_wrap_obj,
                   bool have_callback)
    : ReqWrap(env, req_wrap_obj, AsyncWrap::PROVIDER_UDPSENDWRAP),
      have_callback_(have_callback) {
}


bool SendWrap::have_callback() const {
  return have_callback_;
}

UDPListener::~UDPListener() {
  if (wrap_ != nullptr)
    wrap_->set_listener(nullptr);
}

UDPWrapBase::~UDPWrapBase() {
  set_listener(nullptr);
}

UDPListener* UDPWrapBase::listener() const {
  CHECK_NOT_NULL(listener_);
  return listener_;
}

void UDPWrapBase::set_listener(UDPListener* listener) {
  if (listener_ != nullptr)
    listener_->wrap_ = nullptr;
  listener_ = listener;
  if (listener_ != nullptr) {
    CHECK_NULL(listener_->wrap_);
    listener_->wrap_ = this;
  }
}

UDPWrapBase* UDPWrapBase::FromObject(Local<Object> obj) {
  CHECK_GT(obj->InternalFieldCount(), UDPWrapBase::kUDPWrapBaseField);
  return static_cast<UDPWrapBase*>(
      obj->GetAlignedPointerFromInternalField(UDPWrapBase::kUDPWrapBaseField));
}

void UDPWrapBase::AddMethods(Environment* env, Local<FunctionTemplate> t) {
  SetProtoMethod(env->isolate(), t, "recvStart", RecvStart);
  SetProtoMethod(env->isolate(), t, "recvStop", RecvStop);
}

void UDPWrapBase::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(RecvStart);
  registry->Register(RecvStop);
}

UDPWrap::UDPWrap(Environment* env, Local<Object> object)
    : HandleWrap(env,
                 object,
                 reinterpret_cast<uv_handle_t*>(&handle_),
                 AsyncWrap::PROVIDER_UDPWRAP) {
  object->SetAlignedPointerInInternalField(
      UDPWrapBase::kUDPWrapBaseField, static_cast<UDPWrapBase*>(this));

  int r = uv_udp_init(env->event_loop(), &handle_);
  CHECK_EQ(r, 0);  // can't fail anyway

  set_listener(this);
}


void UDPWrap::Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context,
                         void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);
  t->InstanceTemplate()->SetInternalFieldCount(
      UDPWrapBase::kInternalFieldCount);

  enum PropertyAttribute attributes =
      static_cast<PropertyAttribute>(ReadOnly | DontDelete);

  Local<Signature> signature = Signature::New(isolate, t);

  Local<FunctionTemplate> get_fd_templ =
      FunctionTemplate::New(isolate, UDPWrap::GetFD, Local<Value>(), signature);

  t->PrototypeTemplate()->SetAccessorProperty(env->fd_string(),
                                              get_fd_templ,
                                              Local<FunctionTemplate>(),
                                              attributes);

  UDPWrapBase::AddMethods(env, t);
  SetProtoMethod(isolate, t, "open", Open);
  SetProtoMethod(isolate, t, "bind", Bind);
  SetProtoMethod(isolate, t, "connect", Connect);
  SetProtoMethod(isolate, t, "send", Send);
  SetProtoMethod(isolate, t, "bind6", Bind6);
  SetProtoMethod(isolate, t, "connect6", Connect6);
  SetProtoMethod(isolate, t, "send6", Send6);
  SetProtoMethod(isolate, t, "disconnect", Disconnect);
  SetProtoMethod(isolate,
                 t,
                 "getpeername",
                 GetSockOrPeerName<UDPWrap, uv_udp_getpeername>);
  SetProtoMethod(isolate,
                 t,
                 "getsockname",
                 GetSockOrPeerName<UDPWrap, uv_udp_getsockname>);
  SetProtoMethod(isolate, t, "addMembership", AddMembership);
  SetProtoMethod(isolate, t, "dropMembership", DropMembership);
  SetProtoMethod(
      isolate, t, "addSourceSpecificMembership", AddSourceSpecificMembership);
  SetProtoMethod(
      isolate, t, "dropSourceSpecificMembership", DropSourceSpecificMembership);
  SetProtoMethod(isolate, t, "setMulticastInterface", SetMulticastInterface);
  SetProtoMethod(
      isolate, t, "setMulticastTTL", SetLibuvInt32<uv_udp_set_multicast_ttl>);
  SetProtoMethod(isolate,
                 t,
                 "setMulticastLoopback",
                 SetLibuvInt32<uv_udp_set_multicast_loop>);
  SetProtoMethod(
      isolate, t, "setBroadcast", SetLibuvInt32<uv_udp_set_broadcast>);
  SetProtoMethod(isolate, t, "setTTL", SetLibuvInt32<uv_udp_set_ttl>);
  SetProtoMethod(isolate, t, "bufferSize", BufferSize);
  SetProtoMethodNoSideEffect(isolate, t, "getSendQueueSize", GetSendQueueSize);
  SetProtoMethodNoSideEffect(
      isolate, t, "getSendQueueCount", GetSendQueueCount);

  t->Inherit(HandleWrap::GetConstructorTemplate(env));

  SetConstructorFunction(context, target, "UDP", t);
  env->set_udp_constructor_function(t->GetFunction(context).ToLocalChecked());

  // Create FunctionTemplate for SendWrap
  Local<FunctionTemplate> swt =
      BaseObject::MakeLazilyInitializedJSTemplate(env);
  swt->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetConstructorFunction(context, target, "SendWrap", swt);

  Local<Object> constants = Object::New(isolate);
  NODE_DEFINE_CONSTANT(constants, UV_UDP_IPV6ONLY);
  NODE_DEFINE_CONSTANT(constants, UV_UDP_REUSEADDR);
  NODE_DEFINE_CONSTANT(constants, UV_UDP_REUSEPORT);
  target->Set(context,
              env->constants_string(),
              constants).Check();
}

void UDPWrap::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  UDPWrapBase::RegisterExternalReferences(registry);
  registry->Register(New);
  registry->Register(GetFD);

  registry->Register(Open);
  registry->Register(Bind);
  registry->Register(Connect);
  registry->Register(Send);
  registry->Register(Bind6);
  registry->Register(Connect6);
  registry->Register(Send6);
  registry->Register(Disconnect);
  registry->Register(GetSockOrPeerName<UDPWrap, uv_udp_getpeername>);
  registry->Register(GetSockOrPeerName<UDPWrap, uv_udp_getsockname>);
  registry->Register(AddMembership);
  registry->Register(DropMembership);
  registry->Register(AddSourceSpecificMembership);
  registry->Register(DropSourceSpecificMembership);
  registry->Register(SetMulticastInterface);
  registry->Register(SetLibuvInt32<uv_udp_set_multicast_ttl>);
  registry->Register(SetLibuvInt32<uv_udp_set_multicast_loop>);
  registry->Register(SetLibuvInt32<uv_udp_set_broadcast>);
  registry->Register(SetLibuvInt32<uv_udp_set_ttl>);
  registry->Register(BufferSize);
  registry->Register(GetSendQueueSize);
  registry->Register(GetSendQueueCount);
}

void UDPWrap::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);
  new UDPWrap(env, args.This());
}


void UDPWrap::GetFD(const FunctionCallbackInfo<Value>& args) {
  int fd = UV_EBADF;
#if !defined(_WIN32)
  UDPWrap* wrap = Unwrap<UDPWrap>(args.This());
  if (wrap != nullptr)
    uv_fileno(reinterpret_cast<uv_handle_t*>(&wrap->handle_), &fd);
#endif
  args.GetReturnValue().Set(fd);
}

int sockaddr_for_family(int address_family,
                        const char* address,
                        const unsigned short port,
                        struct sockaddr_storage* addr) {
  switch (address_family) {
    case AF_INET:
      return uv_ip4_addr(address, port, reinterpret_cast<sockaddr_in*>(addr));
    case AF_INET6:
      return uv_ip6_addr(address, port, reinterpret_cast<sockaddr_in6*>(addr));
    default:
      UNREACHABLE("unexpected address family");
  }
}

void UDPWrap::DoBind(const FunctionCallbackInfo<Value>& args, int family) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  // bind(ip, port, flags)
  CHECK_EQ(args.Length(), 3);

  node::Utf8Value address(args.GetIsolate(), args[0]);
  Local<Context> ctx = args.GetIsolate()->GetCurrentContext();
  uint32_t port, flags;
  if (!args[1]->Uint32Value(ctx).To(&port) ||
      !args[2]->Uint32Value(ctx).To(&flags))
    return;
  struct sockaddr_storage addr_storage;
  int err = sockaddr_for_family(family, address.out(), port, &addr_storage);
  if (err == 0) {
    err = uv_udp_bind(&wrap->handle_,
                      reinterpret_cast<const sockaddr*>(&addr_storage),
                      flags);
  }

  if (err == 0)
    wrap->listener()->OnAfterBind();

  args.GetReturnValue().Set(err);
}


void UDPWrap::DoConnect(const FunctionCallbackInfo<Value>& args, int family) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK_EQ(args.Length(), 2);

  node::Utf8Value address(args.GetIsolate(), args[0]);
  Local<Context> ctx = args.GetIsolate()->GetCurrentContext();
  uint32_t port;
  if (!args[1]->Uint32Value(ctx).To(&port))
    return;
  struct sockaddr_storage addr_storage;
  int err = sockaddr_for_family(family, address.out(), port, &addr_storage);
  if (err == 0) {
    err = uv_udp_connect(&wrap->handle_,
                         reinterpret_cast<const sockaddr*>(&addr_storage));
  }

  args.GetReturnValue().Set(err);
}


void UDPWrap::Open(const FunctionCallbackInfo<Value>& args) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  CHECK(args[0]->IsNumber());
  int fd = static_cast<int>(args[0].As<Integer>()->Value());
  int err = uv_udp_open(&wrap->handle_, fd);

  args.GetReturnValue().Set(err);
}


void UDPWrap::Bind(const FunctionCallbackInfo<Value>& args) {
  DoBind(args, AF_INET);
}


void UDPWrap::Bind6(const FunctionCallbackInfo<Value>& args) {
  DoBind(args, AF_INET6);
}


void UDPWrap::BufferSize(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK(args[0]->IsUint32());
  CHECK(args[1]->IsBoolean());
  bool is_recv = args[1].As<Boolean>()->Value();
  const char* uv_func_name = is_recv ? "uv_recv_buffer_size" :
                                       "uv_send_buffer_size";

  if (!args[0]->IsInt32()) {
    env->CollectUVExceptionInfo(args[2], UV_EINVAL, uv_func_name);
    return args.GetReturnValue().SetUndefined();
  }

  uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&wrap->handle_);
  int size = static_cast<int>(args[0].As<Uint32>()->Value());
  int err;

  if (is_recv)
    err = uv_recv_buffer_size(handle, &size);
  else
    err = uv_send_buffer_size(handle, &size);

  if (err != 0) {
    env->CollectUVExceptionInfo(args[2], err, uv_func_name);
    return args.GetReturnValue().SetUndefined();
  }

  args.GetReturnValue().Set(size);
}


void UDPWrap::Connect(const FunctionCallbackInfo<Value>& args) {
  DoConnect(args, AF_INET);
}


void UDPWrap::Connect6(const FunctionCallbackInfo<Value>& args) {
  DoConnect(args, AF_INET6);
}


void UDPWrap::Disconnect(const FunctionCallbackInfo<Value>& args) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK_EQ(args.Length(), 0);

  int err = uv_udp_connect(&wrap->handle_, nullptr);

  args.GetReturnValue().Set(err);
}

void UDPWrap::SetMulticastInterface(const FunctionCallbackInfo<Value>& args) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsString());

  Utf8Value iface(args.GetIsolate(), args[0]);

  const char* iface_cstr = *iface;

  int err = uv_udp_set_multicast_interface(&wrap->handle_, iface_cstr);
  args.GetReturnValue().Set(err);
}

void UDPWrap::SetMembership(const FunctionCallbackInfo<Value>& args,
                            uv_membership membership) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK_EQ(args.Length(), 2);

  node::Utf8Value address(args.GetIsolate(), args[0]);
  node::Utf8Value iface(args.GetIsolate(), args[1]);

  const char* iface_cstr = *iface;
  if (args[1]->IsUndefined() || args[1]->IsNull()) {
      iface_cstr = nullptr;
  }

  int err = uv_udp_set_membership(&wrap->handle_,
                                  *address,
                                  iface_cstr,
                                  membership);
  args.GetReturnValue().Set(err);
}


void UDPWrap::AddMembership(const FunctionCallbackInfo<Value>& args) {
  SetMembership(args, UV_JOIN_GROUP);
}


void UDPWrap::DropMembership(const FunctionCallbackInfo<Value>& args) {
  SetMembership(args, UV_LEAVE_GROUP);
}

void UDPWrap::SetSourceMembership(const FunctionCallbackInfo<Value>& args,
                                  uv_membership membership) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK_EQ(args.Length(), 3);

  node::Utf8Value source_address(args.GetIsolate(), args[0]);
  node::Utf8Value group_address(args.GetIsolate(), args[1]);
  node::Utf8Value iface(args.GetIsolate(), args[2]);

  if (*iface == nullptr) return;
  const char* iface_cstr = *iface;
  if (args[2]->IsUndefined() || args[2]->IsNull()) {
    iface_cstr = nullptr;
  }

  int err = uv_udp_set_source_membership(&wrap->handle_,
                                         *group_address,
                                         iface_cstr,
                                         *source_address,
                                         membership);
  args.GetReturnValue().Set(err);
}

void UDPWrap::AddSourceSpecificMembership(
  const FunctionCallbackInfo<Value>& args) {
  SetSourceMembership(args, UV_JOIN_GROUP);
}


void UDPWrap::DropSourceSpecificMembership(
  const FunctionCallbackInfo<Value>& args) {
  SetSourceMembership(args, UV_LEAVE_GROUP);
}


void UDPWrap::DoSend(const FunctionCallbackInfo<Value>& args, int family) {
  Environment* env = Environment::GetCurrent(args);

  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK(args.Length() == 4 || args.Length() == 6);
  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsArray());
  CHECK(args[2]->IsUint32());

  bool sendto = args.Length() == 6;
  if (sendto) {
    // send(req, list, list.length, port, address, hasCallback)
    CHECK(args[3]->IsUint32());
    CHECK(args[4]->IsString());
    CHECK(args[5]->IsBoolean());
  } else {
    // send(req, list, list.length, hasCallback)
    CHECK(args[3]->IsBoolean());
  }

  Local<Array> chunks = args[1].As<Array>();
  // it is faster to fetch the length of the
  // array in js-land
  size_t count = args[2].As<Uint32>()->Value();

  MaybeStackBuffer<uv_buf_t, 16> bufs(count);

  // construct uv_buf_t array
  for (size_t i = 0; i < count; i++) {
    Local<Value> chunk;
    if (!chunks->Get(env->context(), i).ToLocal(&chunk)) return;

    size_t length = Buffer::Length(chunk);

    bufs[i] = uv_buf_init(Buffer::Data(chunk), length);
  }

  int err = 0;
  struct sockaddr_storage addr_storage;
  sockaddr* addr = nullptr;
  if (sendto) {
    const unsigned short port = args[3].As<Uint32>()->Value();
    node::Utf8Value address(env->isolate(), args[4]);
    err = sockaddr_for_family(family, address.out(), port, &addr_storage);
    if (err == 0)
      addr = reinterpret_cast<sockaddr*>(&addr_storage);
  }

  if (err == 0) {
    wrap->current_send_req_wrap_ = args[0].As<Object>();
    wrap->current_send_has_callback_ =
        sendto ? args[5]->IsTrue() : args[3]->IsTrue();

    err = static_cast<int>(wrap->Send(*bufs, count, addr));

    wrap->current_send_req_wrap_.Clear();
    wrap->current_send_has_callback_ = false;
  }

  args.GetReturnValue().Set(err);
}

ssize_t UDPWrap::Send(uv_buf_t* bufs_ptr,
                      size_t count,
                      const sockaddr* addr) {
  if (IsHandleClosing()) return UV_EBADF;

  size_t msg_size = 0;
  for (size_t i = 0; i < count; i++)
    msg_size += bufs_ptr[i].len;

  int err = 0;
  if (!env()->options()->test_udp_no_try_send) [[unlikely]] {
    err = uv_udp_try_send(&handle_, bufs_ptr, count, addr);
    if (err == UV_ENOSYS || err == UV_EAGAIN) {
      err = 0;
    } else if (err >= 0) {
      size_t sent = err;
      while (count > 0 && bufs_ptr->len <= sent) {
        sent -= bufs_ptr->len;
        bufs_ptr++;
        count--;
      }
      if (count > 0) {
        CHECK_LT(sent, bufs_ptr->len);
        bufs_ptr->base += sent;
        bufs_ptr->len -= sent;
      } else {
        CHECK_EQ(static_cast<size_t>(err), msg_size);
        // + 1 so that the JS side can distinguish 0-length async sends from
        // 0-length sync sends.
        return msg_size + 1;
      }
    }
  }

  if (err == 0) {
    AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(this);
    ReqWrap<uv_udp_send_t>* req_wrap = listener()->CreateSendWrap(msg_size);
    if (req_wrap == nullptr) return UV_ENOSYS;

    err = req_wrap->Dispatch(
        uv_udp_send,
        &handle_,
        bufs_ptr,
        count,
        addr,
        uv_udp_send_cb{[](uv_udp_send_t* req, int status) {
          UDPWrap* self = ContainerOf(&UDPWrap::handle_, req->handle);
          self->listener()->OnSendDone(
              ReqWrap<uv_udp_send_t>::from_req(req), status);
        }});
    if (err)
      delete req_wrap;
  }

  return err;
}


ReqWrap<uv_udp_send_t>* UDPWrap::CreateSendWrap(size_t msg_size) {
  SendWrap* req_wrap = new SendWrap(env(),
                                    current_send_req_wrap_,
                                    current_send_has_callback_);
  req_wrap->msg_size = msg_size;
  return req_wrap;
}


void UDPWrap::Send(const FunctionCallbackInfo<Value>& args) {
  DoSend(args, AF_INET);
}


void UDPWrap::Send6(const FunctionCallbackInfo<Value>& args) {
  DoSend(args, AF_INET6);
}


AsyncWrap* UDPWrap::GetAsyncWrap() {
  return this;
}

SocketAddress UDPWrap::GetPeerName() {
  return SocketAddress::FromPeerName(handle_);
}

SocketAddress UDPWrap::GetSockName() {
  return SocketAddress::FromSockName(handle_);
}

void UDPWrapBase::RecvStart(const FunctionCallbackInfo<Value>& args) {
  UDPWrapBase* wrap = UDPWrapBase::FromObject(args.This());
  args.GetReturnValue().Set(wrap == nullptr ? UV_EBADF : wrap->RecvStart());
}

int UDPWrap::RecvStart() {
  if (IsHandleClosing()) return UV_EBADF;
  int err = uv_udp_recv_start(&handle_, OnAlloc, OnRecv);
  // UV_EALREADY means that the socket is already bound but that's okay
  if (err == UV_EALREADY)
    err = 0;
  return err;
}


void UDPWrapBase::RecvStop(const FunctionCallbackInfo<Value>& args) {
  UDPWrapBase* wrap = UDPWrapBase::FromObject(args.This());
  args.GetReturnValue().Set(wrap == nullptr ? UV_EBADF : wrap->RecvStop());
}

int UDPWrap::RecvStop() {
  if (IsHandleClosing()) return UV_EBADF;
  return uv_udp_recv_stop(&handle_);
}


void UDPWrap::OnSendDone(ReqWrap<uv_udp_send_t>* req, int status) {
  BaseObjectPtr<SendWrap> req_wrap{static_cast<SendWrap*>(req)};
  if (req_wrap->have_callback()) {
    Environment* env = req_wrap->env();
    HandleScope handle_scope(env->isolate());
    Context::Scope context_scope(env->context());
    Local<Value> arg[] = {
      Integer::New(env->isolate(), status),
      Integer::New(env->isolate(), req_wrap->msg_size),
    };
    req_wrap->MakeCallback(env->oncomplete_string(), 2, arg);
  }
}


void UDPWrap::OnAlloc(uv_handle_t* handle,
                      size_t suggested_size,
                      uv_buf_t* buf) {
  UDPWrap* wrap = ContainerOf(&UDPWrap::handle_,
                              reinterpret_cast<uv_udp_t*>(handle));
  *buf = wrap->listener()->OnAlloc(suggested_size);
}

uv_buf_t UDPWrap::OnAlloc(size_t suggested_size) {
  return env()->allocate_managed_buffer(suggested_size);
}

void UDPWrap::OnRecv(uv_udp_t* handle,
                     ssize_t nread,
                     const uv_buf_t* buf,
                     const sockaddr* addr,
                     unsigned int flags) {
  UDPWrap* wrap = ContainerOf(&UDPWrap::handle_, handle);
  wrap->listener()->OnRecv(nread, *buf, addr, flags);
}

void UDPWrap::OnRecv(ssize_t nread,
                     const uv_buf_t& buf_,
                     const sockaddr* addr,
                     unsigned int flags) {
  Environment* env = this->env();
  Isolate* isolate = env->isolate();
  std::unique_ptr<BackingStore> bs = env->release_managed_buffer(buf_);
  if (nread == 0 && addr == nullptr) {
    return;
  }

  HandleScope handle_scope(isolate);
  Context::Scope context_scope(env->context());

  Local<Value> argv[] = {
      Integer::New(isolate, static_cast<int32_t>(nread)),
      object(),
      Undefined(isolate),
      Undefined(isolate)};

  if (nread < 0) {
    MakeCallback(env->onmessage_string(), arraysize(argv), argv);
    return;
  } else if (nread == 0) {
    bs = ArrayBuffer::NewBackingStore(isolate, 0);
  } else if (static_cast<size_t>(nread) != bs->ByteLength()) {
    CHECK_LE(static_cast<size_t>(nread), bs->ByteLength());
    std::unique_ptr<BackingStore> old_bs = std::move(bs);
    bs = ArrayBuffer::NewBackingStore(isolate, nread);
    memcpy(static_cast<char*>(bs->Data()),
           static_cast<char*>(old_bs->Data()),
           nread);
  }

  Local<Object> address;
  {
    bool has_caught = false;
    {
      TryCatchScope try_catch(env);
      if (!AddressToJS(env, addr).ToLocal(&address)) {
        DCHECK(try_catch.HasCaught() && !try_catch.HasTerminated());
        argv[2] = try_catch.Exception();
        DCHECK(!argv[2].IsEmpty());
        has_caught = true;
      }
    }
    if (has_caught) {
      DCHECK(!argv[2].IsEmpty());
      MakeCallback(env->onerror_string(), arraysize(argv), argv);
      return;
    }
  }

  Local<ArrayBuffer> ab = ArrayBuffer::New(isolate, std::move(bs));
  {
    bool has_caught = false;
    {
      TryCatchScope try_catch(env);
      if (!Buffer::New(env, ab, 0, ab->ByteLength()).ToLocal(&argv[2])) {
        DCHECK(try_catch.HasCaught() && !try_catch.HasTerminated());
        argv[2] = try_catch.Exception();
        DCHECK(!argv[2].IsEmpty());
        has_caught = true;
      }
    }
    if (has_caught) {
      DCHECK(!argv[2].IsEmpty());
      MakeCallback(env->onerror_string(), arraysize(argv), argv);
      return;
    }
  }

  argv[3] = address;
  MakeCallback(env->onmessage_string(), arraysize(argv), argv);
}

MaybeLocal<Object> UDPWrap::Instantiate(Environment* env,
                                        AsyncWrap* parent,
                                        UDPWrap::SocketType type) {
  AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(parent);

  // If this assert fires then Initialize hasn't been called yet.
  CHECK_EQ(env->udp_constructor_function().IsEmpty(), false);
  return env->udp_constructor_function()->NewInstance(env->context());
}

void UDPWrap::GetSendQueueSize(const FunctionCallbackInfo<Value>& args) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  size_t size = uv_udp_get_send_queue_size(&wrap->handle_);
  args.GetReturnValue().Set(static_cast<double>(size));
}

void UDPWrap::GetSendQueueCount(const FunctionCallbackInfo<Value>& args) {
  UDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  size_t count = uv_udp_get_send_queue_count(&wrap->handle_);
  args.GetReturnValue().Set(static_cast<double>(count));
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(udp_wrap, node::UDPWrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(udp_wrap,
                                node::UDPWrap::RegisterExternalReferences)
