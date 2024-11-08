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

#include "tcp_wrap.h"

#include "connect_wrap.h"
#include "connection_wrap.h"
#include "env-inl.h"
#include "handle_wrap.h"
#include "node_buffer.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "stream_base-inl.h"
#include "stream_wrap.h"
#include "util-inl.h"

#include <cstdlib>


namespace node {

using v8::Boolean;
using v8::Context;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Value;

MaybeLocal<Object> TCPWrap::Instantiate(Environment* env,
                                        AsyncWrap* parent,
                                        TCPWrap::SocketType type) {
  EscapableHandleScope handle_scope(env->isolate());
  AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(parent);
  CHECK_EQ(env->tcp_constructor_template().IsEmpty(), false);
  Local<Function> constructor = env->tcp_constructor_template()
                                    ->GetFunction(env->context())
                                    .ToLocalChecked();
  CHECK_EQ(constructor.IsEmpty(), false);
  Local<Value> type_value = Int32::New(env->isolate(), type);
  return handle_scope.EscapeMaybe(
      constructor->NewInstance(env->context(), 1, &type_value));
}


void TCPWrap::Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context,
                         void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);
  t->InstanceTemplate()->SetInternalFieldCount(StreamBase::kInternalFieldCount);

  // Init properties
  t->InstanceTemplate()->Set(FIXED_ONE_BYTE_STRING(env->isolate(), "reading"),
                             Boolean::New(env->isolate(), false));
  t->InstanceTemplate()->Set(env->owner_symbol(), Null(env->isolate()));
  t->InstanceTemplate()->Set(env->onconnection_string(), Null(env->isolate()));

  t->Inherit(LibuvStreamWrap::GetConstructorTemplate(env));

  SetProtoMethod(isolate, t, "open", Open);
  SetProtoMethod(isolate, t, "bind", Bind);
  SetProtoMethod(isolate, t, "listen", Listen);
  SetProtoMethod(isolate, t, "connect", Connect);
  SetProtoMethod(isolate, t, "bind6", Bind6);
  SetProtoMethod(isolate, t, "connect6", Connect6);
  SetProtoMethod(isolate,
                 t,
                 "getsockname",
                 GetSockOrPeerName<TCPWrap, uv_tcp_getsockname>);
  SetProtoMethod(isolate,
                 t,
                 "getpeername",
                 GetSockOrPeerName<TCPWrap, uv_tcp_getpeername>);
  SetProtoMethod(isolate, t, "setNoDelay", SetNoDelay);
  SetProtoMethod(isolate, t, "setKeepAlive", SetKeepAlive);
  SetProtoMethod(isolate, t, "reset", Reset);

#ifdef _WIN32
  SetProtoMethod(isolate, t, "setSimultaneousAccepts", SetSimultaneousAccepts);
#endif

  SetConstructorFunction(context, target, "TCP", t);
  env->set_tcp_constructor_template(t);

  // Create FunctionTemplate for TCPConnectWrap.
  Local<FunctionTemplate> cwt =
      BaseObject::MakeLazilyInitializedJSTemplate(env);
  cwt->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetConstructorFunction(context, target, "TCPConnectWrap", cwt);

  // Define constants
  Local<Object> constants = Object::New(env->isolate());
  NODE_DEFINE_CONSTANT(constants, SOCKET);
  NODE_DEFINE_CONSTANT(constants, SERVER);
  NODE_DEFINE_CONSTANT(constants, UV_TCP_IPV6ONLY);
  NODE_DEFINE_CONSTANT(constants, UV_TCP_REUSEPORT);
  target->Set(context,
              env->constants_string(),
              constants).Check();
}

void TCPWrap::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(Open);
  registry->Register(Bind);
  registry->Register(Listen);
  registry->Register(Connect);
  registry->Register(Bind6);
  registry->Register(Connect6);

  registry->Register(GetSockOrPeerName<TCPWrap, uv_tcp_getsockname>);
  registry->Register(GetSockOrPeerName<TCPWrap, uv_tcp_getpeername>);
  registry->Register(SetNoDelay);
  registry->Register(SetKeepAlive);
  registry->Register(Reset);
#ifdef _WIN32
  registry->Register(SetSimultaneousAccepts);
#endif
}

void TCPWrap::New(const FunctionCallbackInfo<Value>& args) {
  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  CHECK(args.IsConstructCall());
  CHECK(args[0]->IsInt32());
  Environment* env = Environment::GetCurrent(args);

  int type_value = args[0].As<Int32>()->Value();
  TCPWrap::SocketType type = static_cast<TCPWrap::SocketType>(type_value);

  ProviderType provider;
  switch (type) {
    case SOCKET:
      provider = PROVIDER_TCPWRAP;
      break;
    case SERVER:
      provider = PROVIDER_TCPSERVERWRAP;
      break;
    default:
      UNREACHABLE();
  }

  new TCPWrap(env, args.This(), provider);
}


TCPWrap::TCPWrap(Environment* env, Local<Object> object, ProviderType provider)
    : ConnectionWrap(env, object, provider) {
  int r = uv_tcp_init(env->event_loop(), &handle_);
  CHECK_EQ(r, 0);  // How do we proxy this error up to javascript?
                   // Suggestion: uv_tcp_init() returns void.
}


void TCPWrap::SetNoDelay(const FunctionCallbackInfo<Value>& args) {
  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  int enable = static_cast<int>(args[0]->IsTrue());
  int err = uv_tcp_nodelay(&wrap->handle_, enable);
  args.GetReturnValue().Set(err);
}


void TCPWrap::SetKeepAlive(const FunctionCallbackInfo<Value>& args) {
  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  Environment* env = wrap->env();
  int enable;
  if (!args[0]->Int32Value(env->context()).To(&enable)) return;
  unsigned int delay = static_cast<unsigned int>(args[1].As<Uint32>()->Value());
  int err = uv_tcp_keepalive(&wrap->handle_, enable, delay);
  args.GetReturnValue().Set(err);
}


#ifdef _WIN32
void TCPWrap::SetSimultaneousAccepts(const FunctionCallbackInfo<Value>& args) {
  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  bool enable = args[0]->IsTrue();
  int err = uv_tcp_simultaneous_accepts(&wrap->handle_, enable);
  args.GetReturnValue().Set(err);
}
#endif


void TCPWrap::Open(const FunctionCallbackInfo<Value>& args) {
  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  int64_t val;
  if (!args[0]->IntegerValue(args.GetIsolate()->GetCurrentContext()).To(&val))
    return;
  int fd = static_cast<int>(val);
  int err = uv_tcp_open(&wrap->handle_, fd);

  if (err == 0)
    wrap->set_fd(fd);

  args.GetReturnValue().Set(err);
}

template <typename T>
void TCPWrap::Bind(
    const FunctionCallbackInfo<Value>& args,
    int family,
    std::function<int(const char* ip_address, int port, T* addr)> uv_ip_addr) {
  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  Environment* env = wrap->env();
  node::Utf8Value ip_address(env->isolate(), args[0]);
  int port;
  unsigned int flags = 0;
  if (!args[1]->Int32Value(env->context()).To(&port)) return;
  if (args.Length() >= 3 && args[2]->IsUint32()) {
    if (!args[2]->Uint32Value(env->context()).To(&flags)) return;
    // Can not set IPV6 flags on IPV4 socket
    if (family == AF_INET) {
      flags &= ~UV_TCP_IPV6ONLY;
    }
  }

  T addr;
  int err = uv_ip_addr(*ip_address, port, &addr);

  if (err == 0) {
    err = uv_tcp_bind(&wrap->handle_,
                      reinterpret_cast<const sockaddr*>(&addr),
                      flags);
  }
  args.GetReturnValue().Set(err);
}

void TCPWrap::Bind(const FunctionCallbackInfo<Value>& args) {
  Bind<sockaddr_in>(args, AF_INET, uv_ip4_addr);
}


void TCPWrap::Bind6(const FunctionCallbackInfo<Value>& args) {
  Bind<sockaddr_in6>(args, AF_INET6, uv_ip6_addr);
}


void TCPWrap::Listen(const FunctionCallbackInfo<Value>& args) {
  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));
  Environment* env = wrap->env();
  int backlog;
  if (!args[0]->Int32Value(env->context()).To(&backlog)) return;
  int err = uv_listen(reinterpret_cast<uv_stream_t*>(&wrap->handle_),
                      backlog,
                      OnConnection);
  args.GetReturnValue().Set(err);
}


void TCPWrap::Connect(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[2]->IsUint32());
  // explicit cast to fit to libuv's type expectation
  int port = static_cast<int>(args[2].As<Uint32>()->Value());
  Connect<sockaddr_in>(args,
                       [port](const char* ip_address, sockaddr_in* addr) {
      return uv_ip4_addr(ip_address, port, addr);
  });
}


void TCPWrap::Connect6(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[2]->IsUint32());
  int port;
  if (!args[2]->Int32Value(env->context()).To(&port)) return;
  Connect<sockaddr_in6>(args,
                        [port](const char* ip_address, sockaddr_in6* addr) {
      return uv_ip6_addr(ip_address, port, addr);
  });
}

template <typename T>
void TCPWrap::Connect(const FunctionCallbackInfo<Value>& args,
    std::function<int(const char* ip_address, T* addr)> uv_ip_addr) {
  Environment* env = Environment::GetCurrent(args);

  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsString());

  Local<Object> req_wrap_obj = args[0].As<Object>();
  node::Utf8Value ip_address(env->isolate(), args[1]);

  T addr;
  int err = uv_ip_addr(*ip_address, &addr);

  if (err == 0) {
    AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(wrap);
    ConnectWrap* req_wrap =
        new ConnectWrap(env, req_wrap_obj, AsyncWrap::PROVIDER_TCPCONNECTWRAP);
    err = req_wrap->Dispatch(uv_tcp_connect,
                             &wrap->handle_,
                             reinterpret_cast<const sockaddr*>(&addr),
                             AfterConnect);
    if (err) {
      delete req_wrap;
    } else {
      CHECK(args[2]->Uint32Value(env->context()).IsJust());
      int port = args[2]->Uint32Value(env->context()).FromJust();
      TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(TRACING_CATEGORY_NODE2(net, native),
                                        "connect",
                                        req_wrap,
                                        "ip",
                                        TRACE_STR_COPY(*ip_address),
                                        "port",
                                        port);
    }
  }

  args.GetReturnValue().Set(err);
}
void TCPWrap::Reset(const FunctionCallbackInfo<Value>& args) {
  TCPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(
      &wrap, args.This(), args.GetReturnValue().Set(UV_EBADF));

  int err = wrap->Reset(args[0]);

  args.GetReturnValue().Set(err);
}

int TCPWrap::Reset(Local<Value> close_callback) {
  if (state_ != kInitialized) return 0;

  int err = uv_tcp_close_reset(&handle_, OnClose);
  state_ = kClosing;
  if (!err & !close_callback.IsEmpty() && close_callback->IsFunction() &&
      !persistent().IsEmpty()) {
    object()
        ->Set(env()->context(), env()->handle_onclose_symbol(), close_callback)
        .Check();
  }
  return err;
}

// also used by udp_wrap.cc
MaybeLocal<Object> AddressToJS(Environment* env,
                               const sockaddr* addr,
                               Local<Object> info) {
  EscapableHandleScope scope(env->isolate());
  char ip[INET6_ADDRSTRLEN + UV_IF_NAMESIZE];
  const sockaddr_in* a4;
  const sockaddr_in6* a6;

  int port;

  if (info.IsEmpty())
    info = Object::New(env->isolate());

  switch (addr->sa_family) {
  case AF_INET6:
    a6 = reinterpret_cast<const sockaddr_in6*>(addr);
    uv_inet_ntop(AF_INET6, &a6->sin6_addr, ip, sizeof ip);
    // Add an interface identifier to a link local address.
    if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr) && a6->sin6_scope_id > 0) {
      const size_t addrlen = strlen(ip);
      CHECK_LT(addrlen, sizeof(ip));
      ip[addrlen] = '%';
      size_t scopeidlen = sizeof(ip) - addrlen - 1;
      CHECK_GE(scopeidlen, UV_IF_NAMESIZE);
      const int r = uv_if_indextoiid(a6->sin6_scope_id,
                                     ip + addrlen + 1,
                                     &scopeidlen);
      if (r) {
        env->ThrowUVException(r, "uv_if_indextoiid");
        return {};
      }
    }
    port = ntohs(a6->sin6_port);
    info->Set(env->context(),
              env->address_string(),
              OneByteString(env->isolate(), ip)).Check();
    info->Set(env->context(), env->family_string(), env->ipv6_string()).Check();
    info->Set(env->context(),
              env->port_string(),
              Integer::New(env->isolate(), port)).Check();
    break;

  case AF_INET:
    a4 = reinterpret_cast<const sockaddr_in*>(addr);
    uv_inet_ntop(AF_INET, &a4->sin_addr, ip, sizeof ip);
    port = ntohs(a4->sin_port);
    info->Set(env->context(),
              env->address_string(),
              OneByteString(env->isolate(), ip)).Check();
    info->Set(env->context(), env->family_string(), env->ipv4_string()).Check();
    info->Set(env->context(),
              env->port_string(),
              Integer::New(env->isolate(), port)).Check();
    break;

  default:
    info->Set(env->context(),
              env->address_string(),
              String::Empty(env->isolate())).Check();
  }

  return scope.Escape(info);
}


}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(tcp_wrap, node::TCPWrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(tcp_wrap,
                                node::TCPWrap::RegisterExternalReferences)
