#include "udp_wrap.h"
#include "async_wrap-inl.h"
#include "node_errors.h"
#include "node_sockaddr-inl.h"

#include <algorithm>

// TODO(RaisinTen): Replace all uses with empty `v8::Maybe`s.
#define JS_EXCEPTION_PENDING UV_EPROTO

namespace node {

using errors::TryCatchScope;
using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Value;

// JSUDPWrap is a testing utility used by test/common/udppair.js
// to simulate UDP traffic deterministically in Node.js tests.
class JSUDPWrap final : public UDPWrapBase, public AsyncWrap {
 public:
  JSUDPWrap(Environment* env, Local<Object> obj);

  int RecvStart() override;
  int RecvStop() override;
  ssize_t Send(uv_buf_t* bufs,
               size_t nbufs,
               const sockaddr* addr) override;
  SocketAddress GetPeerName() override;
  SocketAddress GetSockName() override;
  AsyncWrap* GetAsyncWrap() override { return this; }

  static void New(const FunctionCallbackInfo<Value>& args);
  static void EmitReceived(const FunctionCallbackInfo<Value>& args);
  static void OnSendDone(const FunctionCallbackInfo<Value>& args);
  static void OnAfterBind(const FunctionCallbackInfo<Value>& args);

  static void Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context,
                         void* priv);
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(JSUDPWrap)
  SET_SELF_SIZE(JSUDPWrap)
};

JSUDPWrap::JSUDPWrap(Environment* env, Local<Object> obj)
  : AsyncWrap(env, obj, PROVIDER_JSUDPWRAP) {
  MakeWeak();

  obj->SetAlignedPointerInInternalField(
      kUDPWrapBaseField, static_cast<UDPWrapBase*>(this));
}

int JSUDPWrap::RecvStart() {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());
  TryCatchScope try_catch(env());
  Local<Value> value;
  int32_t value_int = JS_EXCEPTION_PENDING;
  if (!MakeCallback(env()->onreadstart_string(), 0, nullptr).ToLocal(&value) ||
      !value->Int32Value(env()->context()).To(&value_int)) {
    if (try_catch.HasCaught() && !try_catch.HasTerminated())
      errors::TriggerUncaughtException(env()->isolate(), try_catch);
  }
  return value_int;
}

int JSUDPWrap::RecvStop() {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());
  TryCatchScope try_catch(env());
  Local<Value> value;
  int32_t value_int = JS_EXCEPTION_PENDING;
  if (!MakeCallback(env()->onreadstop_string(), 0, nullptr).ToLocal(&value) ||
      !value->Int32Value(env()->context()).To(&value_int)) {
    if (try_catch.HasCaught() && !try_catch.HasTerminated())
      errors::TriggerUncaughtException(env()->isolate(), try_catch);
  }
  return value_int;
}

ssize_t JSUDPWrap::Send(uv_buf_t* bufs,
                        size_t nbufs,
                        const sockaddr* addr) {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());
  TryCatchScope try_catch(env());
  Local<Value> value;
  int64_t value_int = JS_EXCEPTION_PENDING;
  size_t total_len = 0;

  MaybeStackBuffer<Local<Value>, 16> buffers(nbufs);
  for (size_t i = 0; i < nbufs; i++) {
    buffers[i] = Buffer::Copy(env(), bufs[i].base, bufs[i].len)
        .ToLocalChecked();
    total_len += bufs[i].len;
  }

  Local<Object> address;
  if (!AddressToJS(env(), addr).ToLocal(&address)) return value_int;

  Local<Value> args[] = {
    listener()->CreateSendWrap(total_len)->object(),
    Array::New(env()->isolate(), buffers.out(), nbufs),
    address,
  };

  if (!MakeCallback(env()->onwrite_string(), arraysize(args), args)
          .ToLocal(&value) ||
      !value->IntegerValue(env()->context()).To(&value_int)) {
    if (try_catch.HasCaught() && !try_catch.HasTerminated())
      errors::TriggerUncaughtException(env()->isolate(), try_catch);
  }
  return value_int;
}

SocketAddress JSUDPWrap::GetPeerName() {
  SocketAddress ret;
  CHECK(SocketAddress::New(AF_INET, "127.0.0.1", 1337, &ret));
  return ret;
}

SocketAddress JSUDPWrap::GetSockName() {
  SocketAddress ret;
  CHECK(SocketAddress::New(AF_INET, "127.0.0.1", 1337, &ret));
  return ret;
}

void JSUDPWrap::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args.IsConstructCall());
  new JSUDPWrap(env, args.This());
}

void JSUDPWrap::EmitReceived(const FunctionCallbackInfo<Value>& args) {
  JSUDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  Environment* env = wrap->env();

  ArrayBufferViewContents<char> buffer(args[0]);
  const char* data = buffer.data();
  int len = buffer.length();

  CHECK(args[1]->IsInt32());   // family
  CHECK(args[2]->IsString());  // address
  CHECK(args[3]->IsInt32());   // port
  CHECK(args[4]->IsInt32());   // flags
  int family = args[1].As<Int32>()->Value() == 4 ? AF_INET : AF_INET6;
  Utf8Value address(env->isolate(), args[2]);
  int port = args[3].As<Int32>()->Value();
  int flags = args[3].As<Int32>()->Value();

  sockaddr_storage addr;
  CHECK_EQ(sockaddr_for_family(family, *address, port, &addr), 0);

  // Repeatedly ask the stream's owner for memory, copy the data that we
  // just read from JS into those buffers and emit them as reads.
  while (len != 0) {
    uv_buf_t buf = wrap->listener()->OnAlloc(len);
    ssize_t avail = std::min<size_t>(buf.len, len);
    memcpy(buf.base, data, avail);
    data += avail;
    len -= static_cast<int>(avail);
    wrap->listener()->OnRecv(
        avail, buf, reinterpret_cast<sockaddr*>(&addr), flags);
  }
}

void JSUDPWrap::OnSendDone(const FunctionCallbackInfo<Value>& args) {
  JSUDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsInt32());
  ReqWrap<uv_udp_send_t>* req_wrap;
  ASSIGN_OR_RETURN_UNWRAP(&req_wrap, args[0].As<Object>());
  int status = args[1].As<Int32>()->Value();

  wrap->listener()->OnSendDone(req_wrap, status);
}

void JSUDPWrap::OnAfterBind(const FunctionCallbackInfo<Value>& args) {
  JSUDPWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  wrap->listener()->OnAfterBind();
}

void JSUDPWrap::Initialize(Local<Object> target,
                           Local<Value> unused,
                           Local<Context> context,
                           void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);
  t->InstanceTemplate()
    ->SetInternalFieldCount(UDPWrapBase::kUDPWrapBaseField + 1);
  t->Inherit(AsyncWrap::GetConstructorTemplate(env));

  UDPWrapBase::AddMethods(env, t);
  SetProtoMethod(isolate, t, "emitReceived", EmitReceived);
  SetProtoMethod(isolate, t, "onSendDone", OnSendDone);
  SetProtoMethod(isolate, t, "onAfterBind", OnAfterBind);

  SetConstructorFunction(context, target, "JSUDPWrap", t);
}


}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(js_udp_wrap, node::JSUDPWrap::Initialize)
