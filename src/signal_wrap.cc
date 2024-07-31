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

#include "async_wrap-inl.h"
#include "env-inl.h"
#include "handle_wrap.h"
#include "node_external_reference.h"
#include "node_process-inl.h"
#include "util-inl.h"
#include "v8.h"

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Value;

void DecreaseSignalHandlerCount(int signum);

namespace {

static Mutex handled_signals_mutex;
static std::map<int, int64_t> handled_signals;  // Signal -> number of handlers

class SignalWrap : public HandleWrap {
 public:
  static void Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context,
                         void* priv) {
    Environment* env = Environment::GetCurrent(context);
    Isolate* isolate = env->isolate();
    Local<FunctionTemplate> constructor = NewFunctionTemplate(isolate, New);
    constructor->InstanceTemplate()->SetInternalFieldCount(
        SignalWrap::kInternalFieldCount);
    constructor->Inherit(HandleWrap::GetConstructorTemplate(env));

    SetProtoMethod(isolate, constructor, "start", Start);
    SetProtoMethod(isolate, constructor, "stop", Stop);

    SetConstructorFunction(context, target, "Signal", constructor);
  }

  static void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
    registry->Register(New);
    registry->Register(Start);
    registry->Register(Stop);
  }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(SignalWrap)
  SET_SELF_SIZE(SignalWrap)

 private:
  static void New(const FunctionCallbackInfo<Value>& args) {
    // This constructor should not be exposed to public javascript.
    // Therefore we assert that we are not trying to call this as a
    // normal function.
    CHECK(args.IsConstructCall());
    Environment* env = Environment::GetCurrent(args);
    new SignalWrap(env, args.This());
  }

  SignalWrap(Environment* env, Local<Object> object)
      : HandleWrap(env,
                   object,
                   reinterpret_cast<uv_handle_t*>(&handle_),
                   AsyncWrap::PROVIDER_SIGNALWRAP) {
    int r = uv_signal_init(env->event_loop(), &handle_);
    CHECK_EQ(r, 0);
  }

  void Close(v8::Local<v8::Value> close_callback) override {
    if (active_) {
      DecreaseSignalHandlerCount(handle_.signum);
      active_ = false;
    }
    HandleWrap::Close(close_callback);
  }

  static void Start(const FunctionCallbackInfo<Value>& args) {
    SignalWrap* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
    Environment* env = wrap->env();
    int signum;
    if (!args[0]->Int32Value(env->context()).To(&signum)) return;
#if defined(__POSIX__) && HAVE_INSPECTOR
    if (signum == SIGPROF) {
      Environment* env = Environment::GetCurrent(args);
      if (env->inspector_agent()->IsListening()) {
        ProcessEmitWarning(env,
                           "process.on(SIGPROF) is reserved while debugging");
        return;
      }
    }
#endif
    int err = uv_signal_start(
        &wrap->handle_,
        [](uv_signal_t* handle, int signum) {
          SignalWrap* wrap = ContainerOf(&SignalWrap::handle_, handle);
          Environment* env = wrap->env();
          HandleScope handle_scope(env->isolate());
          Context::Scope context_scope(env->context());
          Local<Value> arg = Integer::New(env->isolate(), signum);
          wrap->MakeCallback(env->onsignal_string(), 1, &arg);
        },
        signum);

    if (err == 0) {
      CHECK(!wrap->active_);
      wrap->active_ = true;
      Mutex::ScopedLock lock(handled_signals_mutex);
      handled_signals[signum]++;
    }

    args.GetReturnValue().Set(err);
  }

  static void Stop(const FunctionCallbackInfo<Value>& args) {
    SignalWrap* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

    if (wrap->active_)  {
      wrap->active_ = false;
      DecreaseSignalHandlerCount(wrap->handle_.signum);
    }

    int err = uv_signal_stop(&wrap->handle_);
    args.GetReturnValue().Set(err);
  }

  uv_signal_t handle_;
  bool active_ = false;
};


}  // anonymous namespace

void DecreaseSignalHandlerCount(int signum) {
  Mutex::ScopedLock lock(handled_signals_mutex);
  int64_t new_handler_count = --handled_signals[signum];
  CHECK_GE(new_handler_count, 0);
  if (new_handler_count == 0)
    handled_signals.erase(signum);
}

bool HasSignalJSHandler(int signum) {
  Mutex::ScopedLock lock(handled_signals_mutex);
  return handled_signals.find(signum) != handled_signals.end();
}
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(signal_wrap, node::SignalWrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(signal_wrap,
                                node::SignalWrap::RegisterExternalReferences)
