#include "base_object-inl.h"
#include "inspector_agent.h"
#include "inspector_io.h"
#include "memory_tracker-inl.h"
#include "node_external_reference.h"
#include "util-inl.h"
#include "v8-inspector.h"
#include "v8.h"

#include <memory>

namespace node {
namespace inspector {
namespace {

using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Value;

using v8_inspector::StringBuffer;
using v8_inspector::StringView;

std::unique_ptr<StringBuffer> ToProtocolString(Isolate* isolate,
                                               Local<Value> value) {
  TwoByteValue buffer(isolate, value);
  return StringBuffer::create(StringView(*buffer, buffer.length()));
}

struct LocalConnection {
  static std::unique_ptr<InspectorSession> Connect(
      Agent* inspector, std::unique_ptr<InspectorSessionDelegate> delegate) {
    return inspector->Connect(std::move(delegate), false);
  }

  static Local<String> GetClassName(Environment* env) {
    return FIXED_ONE_BYTE_STRING(env->isolate(), "Connection");
  }
};

struct MainThreadConnection {
  static std::unique_ptr<InspectorSession> Connect(
      Agent* inspector, std::unique_ptr<InspectorSessionDelegate> delegate) {
    return inspector->ConnectToMainThread(std::move(delegate), true);
  }

  static Local<String> GetClassName(Environment* env) {
    return FIXED_ONE_BYTE_STRING(env->isolate(), "MainThreadConnection");
  }
};

template <typename ConnectionType>
class JSBindingsConnection : public BaseObject {
 public:
  class JSBindingsSessionDelegate : public InspectorSessionDelegate {
   public:
    JSBindingsSessionDelegate(Environment* env,
                              JSBindingsConnection* connection)
                              : env_(env),
                                connection_(connection) {
    }

    void SendMessageToFrontend(const v8_inspector::StringView& message)
        override {
      Isolate* isolate = env_->isolate();
      HandleScope handle_scope(isolate);
      Context::Scope context_scope(env_->context());
      Local<Value> argument;
      if (!String::NewFromTwoByte(isolate, message.characters16(),
                                  NewStringType::kNormal,
                                  message.length()).ToLocal(&argument)) return;
      connection_->OnMessage(argument);
    }

   private:
    Environment* env_;
    BaseObjectPtr<JSBindingsConnection> connection_;
  };

  JSBindingsConnection(Environment* env,
                       Local<Object> wrap,
                       Local<Function> callback)
      : BaseObject(env, wrap), callback_(env->isolate(), callback) {
    Agent* inspector = env->inspector_agent();
    session_ = ConnectionType::Connect(
        inspector, std::make_unique<JSBindingsSessionDelegate>(env, this));
  }

  void OnMessage(Local<Value> value) {
    auto result = callback_.Get(env()->isolate())
                      ->Call(env()->context(), object(), 1, &value);
    (void)result;
  }

  static void Bind(Environment* env, Local<Object> target) {
    Isolate* isolate = env->isolate();
    Local<FunctionTemplate> tmpl =
        NewFunctionTemplate(isolate, JSBindingsConnection::New);
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        JSBindingsConnection::kInternalFieldCount);
    SetProtoMethod(isolate, tmpl, "dispatch", JSBindingsConnection::Dispatch);
    SetProtoMethod(
        isolate, tmpl, "disconnect", JSBindingsConnection::Disconnect);
    SetConstructorFunction(
        env->context(), target, ConnectionType::GetClassName(env), tmpl);
  }

  static void New(const FunctionCallbackInfo<Value>& info) {
    Environment* env = Environment::GetCurrent(info);
    CHECK(info[0]->IsFunction());
    Local<Function> callback = info[0].As<Function>();
    new JSBindingsConnection(env, info.This(), callback);
  }

  // See https://github.com/nodejs/node/pull/46942
  void Disconnect() {
    BaseObjectPtr<JSBindingsConnection> strong_ref{this};
    session_.reset();
    Detach();
  }

  static void Disconnect(const FunctionCallbackInfo<Value>& info) {
    JSBindingsConnection* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, info.This());
    session->Disconnect();
  }

  static void Dispatch(const FunctionCallbackInfo<Value>& info) {
    Environment* env = Environment::GetCurrent(info);
    JSBindingsConnection* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, info.This());
    CHECK(info[0]->IsString());

    if (session->session_) {
      session->session_->Dispatch(
          ToProtocolString(env->isolate(), info[0])->string());
    }
  }

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackField("callback", callback_);
    tracker->TrackFieldWithSize(
        "session", sizeof(*session_), "InspectorSession");
  }

  SET_MEMORY_INFO_NAME(JSBindingsConnection)
  SET_SELF_SIZE(JSBindingsConnection)

  bool IsNotIndicativeOfMemoryLeakAtExit() const override {
    return true;  // Binding connections emit events on their own.
  }

 private:
  std::unique_ptr<InspectorSession> session_;
  Global<Function> callback_;
};

static bool InspectorEnabled(Environment* env) {
  Agent* agent = env->inspector_agent();
  return agent->IsActive();
}

void SetConsoleExtensionInstaller(const FunctionCallbackInfo<Value>& info) {
  Realm* realm = Realm::GetCurrent(info);

  CHECK_EQ(info.Length(), 1);
  CHECK(info[0]->IsFunction());

  realm->set_inspector_console_extension_installer(info[0].As<Function>());
}

void CallAndPauseOnStart(const FunctionCallbackInfo<v8::Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  THROW_IF_INSUFFICIENT_PERMISSIONS(env,
                                    permission::PermissionScope::kInspector,
                                    "PauseOnNextJavascriptStatement");
  CHECK_GT(args.Length(), 1);
  CHECK(args[0]->IsFunction());
  SlicedArguments call_args(args, /* start */ 2);
  env->inspector_agent()->PauseOnNextJavascriptStatement("Break on start");
  v8::MaybeLocal<v8::Value> retval =
      args[0].As<v8::Function>()->Call(env->context(), args[1],
                                       call_args.length(), call_args.out());
  if (!retval.IsEmpty()) {
    args.GetReturnValue().Set(retval.ToLocalChecked());
  }
}

void InspectorConsoleCall(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  Isolate* isolate = env->isolate();
  Local<Context> context = isolate->GetCurrentContext();
  CHECK_GE(info.Length(), 2);
  SlicedArguments call_args(info, /* start */ 2);
  if (InspectorEnabled(env)) {
    Local<Value> inspector_method = info[0];
    CHECK(inspector_method->IsFunction());
    if (!env->is_in_inspector_console_call()) {
      env->set_is_in_inspector_console_call(true);
      MaybeLocal<Value> ret = inspector_method.As<Function>()->Call(
          context, info.This(), call_args.length(), call_args.out());
      env->set_is_in_inspector_console_call(false);
      if (ret.IsEmpty())
        return;
    }
  }

  Local<Value> node_method = info[1];
  CHECK(node_method->IsFunction());
  USE(node_method.As<Function>()->Call(
      context, info.This(), call_args.length(), call_args.out()));
}

static void* GetAsyncTask(int64_t asyncId) {
  // The inspector assumes that when other clients use its asyncTask* API,
  // they use real pointers, or at least something aligned like real pointer.
  // In general it means that our task_id should always be even.
  //
  // On 32bit platforms, the 64bit asyncId would get truncated when converted
  // to a 32bit pointer. However, the javascript part will never enable
  // the async_hook on 32bit platforms, therefore the truncation will never
  // happen in practice.
  return reinterpret_cast<void*>(asyncId << 1);
}

template <void (Agent::*asyncTaskFn)(void*)>
static void InvokeAsyncTaskFnWithId(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsNumber());
  int64_t task_id = args[0]->IntegerValue(env->context()).FromJust();
  (env->inspector_agent()->*asyncTaskFn)(GetAsyncTask(task_id));
}

static void AsyncTaskScheduledWrapper(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsString());
  Local<String> task_name = args[0].As<String>();
  String::Value task_name_value(args.GetIsolate(), task_name);
  StringView task_name_view(*task_name_value, task_name_value.length());

  CHECK(args[1]->IsNumber());
  int64_t task_id = args[1]->IntegerValue(env->context()).FromJust();
  void* task = GetAsyncTask(task_id);

  CHECK(args[2]->IsBoolean());
  bool recurring = args[2]->BooleanValue(args.GetIsolate());

  env->inspector_agent()->AsyncTaskScheduled(task_name_view, task, recurring);
}

static void RegisterAsyncHookWrapper(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsFunction());
  Local<Function> enable_function = args[0].As<Function>();
  CHECK(args[1]->IsFunction());
  Local<Function> disable_function = args[1].As<Function>();
  env->inspector_agent()->RegisterAsyncHook(env->isolate(),
    enable_function, disable_function);
}

void EmitProtocolEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsString());
  Local<String> eventName = args[0].As<String>();
  CHECK(args[1]->IsString());
  Local<String> params = args[1].As<String>();

  env->inspector_agent()->EmitProtocolEvent(
      ToProtocolString(env->isolate(), eventName)->string(),
      ToProtocolString(env->isolate(), params)->string());
}

void SetupNetworkTracking(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsFunction());
  Local<Function> enable_function = args[0].As<Function>();
  CHECK(args[1]->IsFunction());
  Local<Function> disable_function = args[1].As<Function>();

  env->inspector_agent()->SetupNetworkTracking(enable_function,
                                               disable_function);
}

void IsEnabled(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  args.GetReturnValue().Set(env->inspector_agent()->IsListening());
}

void Open(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Agent* agent = env->inspector_agent();

  if (args.Length() > 0 && args[0]->IsUint32()) {
    uint32_t port = args[0].As<Uint32>()->Value();
    CHECK_LE(port, std::numeric_limits<uint16_t>::max());
    ExclusiveAccess<HostPort>::Scoped host_port(agent->host_port());
    host_port->set_port(static_cast<int>(port));
  }

  if (args.Length() > 1 && args[1]->IsString()) {
    Utf8Value host(env->isolate(), args[1].As<String>());
    ExclusiveAccess<HostPort>::Scoped host_port(agent->host_port());
    host_port->set_host(*host);
  }

  agent->StartIoThread();
}

void WaitForDebugger(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Agent* agent = env->inspector_agent();
  if (agent->IsActive())
    agent->WaitForConnect();
  args.GetReturnValue().Set(agent->IsActive());
}

void Url(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  std::string url = env->inspector_agent()->GetWsUrl();
  if (url.empty()) {
    return;
  }
  args.GetReturnValue().Set(OneByteString(env->isolate(), url));
}

void Initialize(Local<Object> target, Local<Value> unused,
                Local<Context> context, void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  v8::Local<v8::Function> consoleCallFunc =
      NewFunctionTemplate(isolate,
                          InspectorConsoleCall,
                          v8::Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasSideEffect)
          ->GetFunction(context)
          .ToLocalChecked();
  auto name_string = FIXED_ONE_BYTE_STRING(isolate, "consoleCall");
  target->Set(context, name_string, consoleCallFunc).Check();
  consoleCallFunc->SetName(name_string);

  SetMethod(context,
            target,
            "setConsoleExtensionInstaller",
            SetConsoleExtensionInstaller);
  SetMethod(context, target, "callAndPauseOnStart", CallAndPauseOnStart);
  SetMethod(context, target, "open", Open);
  SetMethodNoSideEffect(context, target, "url", Url);
  SetMethod(context, target, "waitForDebugger", WaitForDebugger);

  SetMethod(context, target, "asyncTaskScheduled", AsyncTaskScheduledWrapper);
  SetMethod(context,
            target,
            "asyncTaskCanceled",
            InvokeAsyncTaskFnWithId<&Agent::AsyncTaskCanceled>);
  SetMethod(context,
            target,
            "asyncTaskStarted",
            InvokeAsyncTaskFnWithId<&Agent::AsyncTaskStarted>);
  SetMethod(context,
            target,
            "asyncTaskFinished",
            InvokeAsyncTaskFnWithId<&Agent::AsyncTaskFinished>);

  SetMethod(context, target, "registerAsyncHook", RegisterAsyncHookWrapper);
  SetMethodNoSideEffect(context, target, "isEnabled", IsEnabled);
  SetMethod(context, target, "emitProtocolEvent", EmitProtocolEvent);
  SetMethod(context, target, "setupNetworkTracking", SetupNetworkTracking);

  Local<String> console_string = FIXED_ONE_BYTE_STRING(isolate, "console");

  // Grab the console from the binding object and expose those to our binding
  // layer.
  Local<Object> binding = context->GetExtrasBindingObject();
  target
      ->Set(context,
            console_string,
            binding->Get(context, console_string).ToLocalChecked())
      .Check();

  JSBindingsConnection<LocalConnection>::Bind(env, target);
  JSBindingsConnection<MainThreadConnection>::Bind(env, target);
}

}  // namespace

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(InspectorConsoleCall);
  registry->Register(SetConsoleExtensionInstaller);
  registry->Register(CallAndPauseOnStart);
  registry->Register(Open);
  registry->Register(Url);
  registry->Register(WaitForDebugger);

  registry->Register(AsyncTaskScheduledWrapper);
  registry->Register(InvokeAsyncTaskFnWithId<&Agent::AsyncTaskCanceled>);
  registry->Register(InvokeAsyncTaskFnWithId<&Agent::AsyncTaskStarted>);
  registry->Register(InvokeAsyncTaskFnWithId<&Agent::AsyncTaskFinished>);

  registry->Register(RegisterAsyncHookWrapper);
  registry->Register(IsEnabled);
  registry->Register(EmitProtocolEvent);
  registry->Register(SetupNetworkTracking);

  registry->Register(JSBindingsConnection<LocalConnection>::New);
  registry->Register(JSBindingsConnection<LocalConnection>::Dispatch);
  registry->Register(JSBindingsConnection<LocalConnection>::Disconnect);
  registry->Register(JSBindingsConnection<MainThreadConnection>::New);
  registry->Register(JSBindingsConnection<MainThreadConnection>::Dispatch);
  registry->Register(JSBindingsConnection<MainThreadConnection>::Disconnect);
}

}  // namespace inspector
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(inspector, node::inspector::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(inspector,
                                node::inspector::RegisterExternalReferences)
