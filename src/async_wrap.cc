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

#include "async_wrap.h"  // NOLINT(build/include_inline)
#include "async_context_frame.h"
#include "async_wrap-inl.h"
#include "env-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "tracing/traced_value.h"
#include "util-inl.h"

#include "v8.h"

using v8::Context;
using v8::DontDelete;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::String;
using v8::Undefined;
using v8::Value;
using v8::WeakCallbackInfo;
using v8::WeakCallbackType;

using TryCatchScope = node::errors::TryCatchScope;

namespace node {

static const char* const provider_names[] = {
#define V(PROVIDER)                                                           \
  #PROVIDER,
  NODE_ASYNC_PROVIDER_TYPES(V)
#undef V
};

void AsyncWrap::DestroyAsyncIdsCallback(Environment* env) {
  Local<Function> fn = env->async_hooks_destroy_function();

  TryCatchScope try_catch(env, TryCatchScope::CatchMode::kFatal);

  do {
    std::vector<double> destroy_async_id_list;
    destroy_async_id_list.swap(*env->destroy_async_id_list());
    if (!env->can_call_into_js()) return;
    for (auto async_id : destroy_async_id_list) {
      // Want each callback to be cleaned up after itself, instead of cleaning
      // them all up after the while() loop completes.
      HandleScope scope(env->isolate());
      Local<Value> async_id_value = Number::New(env->isolate(), async_id);
      MaybeLocal<Value> ret = fn->Call(
          env->context(), Undefined(env->isolate()), 1, &async_id_value);

      if (ret.IsEmpty())
        return;
    }
  } while (!env->destroy_async_id_list()->empty());
}

void Emit(Environment* env, double async_id, AsyncHooks::Fields type,
          Local<Function> fn) {
  AsyncHooks* async_hooks = env->async_hooks();

  if (async_hooks->fields()[type] == 0 || !env->can_call_into_js())
    return;

  HandleScope handle_scope(env->isolate());
  Local<Value> async_id_value = Number::New(env->isolate(), async_id);
  TryCatchScope try_catch(env, TryCatchScope::CatchMode::kFatal);
  USE(fn->Call(env->context(), Undefined(env->isolate()), 1, &async_id_value));
}


void AsyncWrap::EmitPromiseResolve(Environment* env, double async_id) {
  Emit(env, async_id, AsyncHooks::kPromiseResolve,
       env->async_hooks_promise_resolve_function());
}


void AsyncWrap::EmitTraceEventBefore() {
  switch (provider_type()) {
#define V(PROVIDER)                                                           \
    case PROVIDER_ ## PROVIDER:                                               \
      TRACE_EVENT_NESTABLE_ASYNC_BEGIN0(                                      \
        TRACING_CATEGORY_NODE1(async_hooks),                                  \
        #PROVIDER "_CALLBACK", static_cast<int64_t>(get_async_id()));         \
      break;
    NODE_ASYNC_PROVIDER_TYPES(V)
#undef V
    default:
      UNREACHABLE();
  }
}


void AsyncWrap::EmitBefore(Environment* env, double async_id) {
  Emit(env, async_id, AsyncHooks::kBefore,
       env->async_hooks_before_function());
}


void AsyncWrap::EmitTraceEventAfter(ProviderType type, double async_id) {
  switch (type) {
#define V(PROVIDER)                                                           \
    case PROVIDER_ ## PROVIDER:                                               \
      TRACE_EVENT_NESTABLE_ASYNC_END0(                                        \
        TRACING_CATEGORY_NODE1(async_hooks),                                  \
        #PROVIDER "_CALLBACK", static_cast<int64_t>(async_id));               \
      break;
    NODE_ASYNC_PROVIDER_TYPES(V)
#undef V
    default:
      UNREACHABLE();
  }
}


void AsyncWrap::EmitAfter(Environment* env, double async_id) {
  // If the user's callback failed then the after() hooks will be called at the
  // end of _fatalException().
  Emit(env, async_id, AsyncHooks::kAfter,
       env->async_hooks_after_function());
}

static void SetupHooks(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject());

  // All of init, before, after, destroy, and promise_resolve are supplied by
  // async_hooks internally, so this should only ever be called once. At which
  // time all the functions should be set. Detect this by checking if
  // init !IsEmpty().
  CHECK(env->async_hooks_init_function().IsEmpty());

  Local<Object> fn_obj = args[0].As<Object>();

#define SET_HOOK_FN(name)                                                      \
  do {                                                                         \
    Local<Value> v =                                                           \
        fn_obj->Get(env->context(),                                            \
                    FIXED_ONE_BYTE_STRING(env->isolate(), #name))              \
            .ToLocalChecked();                                                 \
    CHECK(v->IsFunction());                                                    \
    env->set_async_hooks_##name##_function(v.As<Function>());                  \
  } while (0)

  SET_HOOK_FN(init);
  SET_HOOK_FN(before);
  SET_HOOK_FN(after);
  SET_HOOK_FN(destroy);
  SET_HOOK_FN(promise_resolve);
#undef SET_HOOK_FN
}

static void SetPromiseHooks(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  env->ResetPromiseHooks(
      args[0]->IsFunction() ? args[0].As<Function>() : Local<Function>(),
      args[1]->IsFunction() ? args[1].As<Function>() : Local<Function>(),
      args[2]->IsFunction() ? args[2].As<Function>() : Local<Function>(),
      args[3]->IsFunction() ? args[3].As<Function>() : Local<Function>());
}

static void GetPromiseHooks(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  args.GetReturnValue().Set(
      env->async_hooks()->GetPromiseHooks(args.GetIsolate()));
}

class DestroyParam {
 public:
  double asyncId;
  Environment* env;
  Global<Object> target;
  Global<Object> propBag;
};

static void DestroyParamCleanupHook(void* ptr) {
  delete static_cast<DestroyParam*>(ptr);
}

void AsyncWrap::WeakCallback(const WeakCallbackInfo<DestroyParam>& info) {
  HandleScope scope(info.GetIsolate());

  std::unique_ptr<DestroyParam> p{info.GetParameter()};
  Local<Object> prop_bag = PersistentToLocal::Default(info.GetIsolate(),
                                                      p->propBag);
  Local<Value> val;

  p->env->RemoveCleanupHook(DestroyParamCleanupHook, p.get());

  if (!prop_bag.IsEmpty() &&
      !prop_bag->Get(p->env->context(), p->env->destroyed_string())
        .ToLocal(&val)) {
    return;
  }

  if (val.IsEmpty() || val->IsFalse()) {
    AsyncWrap::EmitDestroy(p->env, p->asyncId);
  }
  // unique_ptr goes out of scope here and pointer is deleted.
}


static void RegisterDestroyHook(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsNumber());
  CHECK(args.Length() == 2 || args[2]->IsObject());

  Isolate* isolate = args.GetIsolate();
  DestroyParam* p = new DestroyParam();
  p->asyncId = args[1].As<Number>()->Value();
  p->env = Environment::GetCurrent(args);
  p->target.Reset(isolate, args[0].As<Object>());
  if (args.Length() > 2) {
    p->propBag.Reset(isolate, args[2].As<Object>());
  }
  p->target.SetWeak(p, AsyncWrap::WeakCallback, WeakCallbackType::kParameter);
  p->env->AddCleanupHook(DestroyParamCleanupHook, p);
}

void AsyncWrap::GetAsyncId(const FunctionCallbackInfo<Value>& args) {
  AsyncWrap* wrap;
  args.GetReturnValue().Set(kInvalidAsyncId);
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  args.GetReturnValue().Set(wrap->get_async_id());
}


void AsyncWrap::PushAsyncContext(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  // No need for CHECK(IsNumber()) on args because if FromJust() doesn't fail
  // then the checks in push_async_ids() and pop_async_id() will.
  double async_id = args[0]->NumberValue(env->context()).FromJust();
  double trigger_async_id = args[1]->NumberValue(env->context()).FromJust();
  env->async_hooks()->push_async_context(async_id, trigger_async_id, {});
}


void AsyncWrap::PopAsyncContext(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  double async_id = args[0]->NumberValue(env->context()).FromJust();
  args.GetReturnValue().Set(env->async_hooks()->pop_async_context(async_id));
}


void AsyncWrap::ExecutionAsyncResource(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  uint32_t index;
  if (!args[0]->Uint32Value(env->context()).To(&index)) return;
  args.GetReturnValue().Set(
      env->async_hooks()->native_execution_async_resource(index));
}


void AsyncWrap::ClearAsyncIdStack(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  env->async_hooks()->clear_async_id_stack();
}


void AsyncWrap::AsyncReset(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsObject());

  AsyncWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

  Local<Object> resource = args[0].As<Object>();
  double execution_async_id =
      args[1]->IsNumber() ? args[1].As<Number>()->Value() : kInvalidAsyncId;
  wrap->AsyncReset(resource, execution_async_id);
}


void AsyncWrap::GetProviderType(const FunctionCallbackInfo<Value>& args) {
  AsyncWrap* wrap;
  args.GetReturnValue().Set(AsyncWrap::PROVIDER_NONE);
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  args.GetReturnValue().Set(wrap->provider_type());
}


void AsyncWrap::EmitDestroy(bool from_gc) {
  AsyncWrap::EmitDestroy(env(), async_id_);
  // Ensure no double destroy is emitted via AsyncReset().
  async_id_ = kInvalidAsyncId;

  if (!persistent().IsEmpty() && !from_gc) {
    HandleScope handle_scope(env()->isolate());
    USE(object()->Set(env()->context(), env()->resource_symbol(), object()));
  }
}

void AsyncWrap::QueueDestroyAsyncId(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsNumber());
  AsyncWrap::EmitDestroy(
      Environment::GetCurrent(args),
      args[0].As<Number>()->Value());
}

void AsyncWrap::SetCallbackTrampoline(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  if (args[0]->IsFunction()) {
    env->set_async_hooks_callback_trampoline(args[0].As<Function>());
  } else {
    env->set_async_hooks_callback_trampoline(Local<Function>());
  }
}

Local<FunctionTemplate> AsyncWrap::GetConstructorTemplate(
    IsolateData* isolate_data) {
  Local<FunctionTemplate> tmpl = isolate_data->async_wrap_ctor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = isolate_data->isolate();
    tmpl = NewFunctionTemplate(isolate, nullptr);
    tmpl->SetClassName(
        FIXED_ONE_BYTE_STRING(isolate_data->isolate(), "AsyncWrap"));
    SetProtoMethod(isolate, tmpl, "getAsyncId", AsyncWrap::GetAsyncId);
    SetProtoMethod(isolate, tmpl, "asyncReset", AsyncWrap::AsyncReset);
    SetProtoMethod(
        isolate, tmpl, "getProviderType", AsyncWrap::GetProviderType);
    isolate_data->set_async_wrap_ctor_template(tmpl);
  }
  return tmpl;
}

void AsyncWrap::CreatePerIsolateProperties(IsolateData* isolate_data,
                                           Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  SetMethod(isolate, target, "setupHooks", SetupHooks);
  SetMethod(isolate, target, "setCallbackTrampoline", SetCallbackTrampoline);
  SetMethod(isolate, target, "pushAsyncContext", PushAsyncContext);
  SetMethod(isolate, target, "popAsyncContext", PopAsyncContext);
  SetMethod(isolate, target, "executionAsyncResource", ExecutionAsyncResource);
  SetMethod(isolate, target, "clearAsyncIdStack", ClearAsyncIdStack);
  SetMethod(isolate, target, "queueDestroyAsyncId", QueueDestroyAsyncId);
  SetMethod(isolate, target, "setPromiseHooks", SetPromiseHooks);
  SetMethod(isolate, target, "getPromiseHooks", GetPromiseHooks);
  SetMethod(isolate, target, "registerDestroyHook", RegisterDestroyHook);
  AsyncWrap::GetConstructorTemplate(isolate_data);
}

void AsyncWrap::CreatePerContextProperties(Local<Object> target,
                                           Local<Value> unused,
                                           Local<Context> context,
                                           void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  Environment* env = realm->env();
  Isolate* isolate = realm->isolate();
  HandleScope scope(isolate);

  PropertyAttribute ReadOnlyDontDelete =
      static_cast<PropertyAttribute>(ReadOnly | DontDelete);

#define FORCE_SET_TARGET_FIELD(obj, str, field)                               \
  (obj)->DefineOwnProperty(context,                                           \
                           FIXED_ONE_BYTE_STRING(isolate, str),               \
                           field,                                             \
                           ReadOnlyDontDelete).FromJust()

  // Attach the uint32_t[] where each slot contains the count of the number of
  // callbacks waiting to be called on a particular event. It can then be
  // incremented/decremented from JS quickly to communicate to C++ if there are
  // any callbacks waiting to be called.
  FORCE_SET_TARGET_FIELD(target,
                         "async_hook_fields",
                         env->async_hooks()->fields().GetJSArray());

  // The following v8::Float64Array has 5 fields. These fields are shared in
  // this way to allow JS and C++ to read/write each value as quickly as
  // possible. The fields are represented as follows:
  //
  // kAsyncIdCounter: Maintains the state of the next unique id to be assigned.
  //
  // kDefaultTriggerAsyncId: Write the id of the resource responsible for a
  //   handle's creation just before calling the new handle's constructor.
  //   After the new handle is constructed kDefaultTriggerAsyncId is set back
  //   to kInvalidAsyncId.
  FORCE_SET_TARGET_FIELD(target,
                         "async_id_fields",
                         env->async_hooks()->async_id_fields().GetJSArray());

  FORCE_SET_TARGET_FIELD(target,
                         "execution_async_resources",
                         env->async_hooks()->js_execution_async_resources());

  target->Set(context,
              env->async_ids_stack_string(),
              env->async_hooks()->async_ids_stack().GetJSArray()).Check();

  Local<Object> constants = Object::New(isolate);
#define SET_HOOKS_CONSTANT(name)                                              \
  FORCE_SET_TARGET_FIELD(                                                     \
      constants, #name, Integer::New(isolate, AsyncHooks::name))

  SET_HOOKS_CONSTANT(kInit);
  SET_HOOKS_CONSTANT(kBefore);
  SET_HOOKS_CONSTANT(kAfter);
  SET_HOOKS_CONSTANT(kDestroy);
  SET_HOOKS_CONSTANT(kPromiseResolve);
  SET_HOOKS_CONSTANT(kTotals);
  SET_HOOKS_CONSTANT(kCheck);
  SET_HOOKS_CONSTANT(kExecutionAsyncId);
  SET_HOOKS_CONSTANT(kTriggerAsyncId);
  SET_HOOKS_CONSTANT(kAsyncIdCounter);
  SET_HOOKS_CONSTANT(kDefaultTriggerAsyncId);
  SET_HOOKS_CONSTANT(kUsesExecutionAsyncResource);
  SET_HOOKS_CONSTANT(kStackLength);
#undef SET_HOOKS_CONSTANT
  FORCE_SET_TARGET_FIELD(target, "constants", constants);

  Local<Object> async_providers = Object::New(isolate);
#define V(p)                                                                  \
  FORCE_SET_TARGET_FIELD(                                                     \
      async_providers, #p, Integer::New(isolate, AsyncWrap::PROVIDER_ ## p));
  NODE_ASYNC_PROVIDER_TYPES(V)
#undef V
  FORCE_SET_TARGET_FIELD(target, "Providers", async_providers);

#undef FORCE_SET_TARGET_FIELD

  // TODO(legendecas): async hook functions are not realm-aware yet.
  // This simply avoid overriding principal realm's functions when a
  // ShadowRealm initializes the binding.
  realm->set_async_hooks_init_function(Local<Function>());
  realm->set_async_hooks_before_function(Local<Function>());
  realm->set_async_hooks_after_function(Local<Function>());
  realm->set_async_hooks_destroy_function(Local<Function>());
  realm->set_async_hooks_promise_resolve_function(Local<Function>());
  realm->set_async_hooks_callback_trampoline(Local<Function>());
  realm->set_async_hooks_binding(target);
}

void AsyncWrap::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(SetupHooks);
  registry->Register(SetCallbackTrampoline);
  registry->Register(PushAsyncContext);
  registry->Register(PopAsyncContext);
  registry->Register(ExecutionAsyncResource);
  registry->Register(ClearAsyncIdStack);
  registry->Register(QueueDestroyAsyncId);
  registry->Register(SetPromiseHooks);
  registry->Register(GetPromiseHooks);
  registry->Register(RegisterDestroyHook);
  registry->Register(AsyncWrap::GetAsyncId);
  registry->Register(AsyncWrap::AsyncReset);
  registry->Register(AsyncWrap::GetProviderType);
}

AsyncWrap::AsyncWrap(Environment* env,
                     Local<Object> object,
                     ProviderType provider,
                     double execution_async_id)
    : AsyncWrap(env, object) {
  CHECK_NE(provider, PROVIDER_NONE);
  provider_type_ = provider;

  // Use AsyncReset() call to execute the init() callbacks.
  AsyncReset(object, execution_async_id);
  init_hook_ran_ = true;
}

AsyncWrap::AsyncWrap(Environment* env, Local<Object> object)
    : BaseObject(env, object),
      context_frame_(env->isolate(),
                     async_context_frame::current(env->isolate())) {}

// This method is necessary to work around one specific problem:
// Before the init() hook runs, if there is one, the BaseObject() constructor
// registers this object with the Environment for finalization and debugging
// purposes.
// If the Environment decides to inspect this object for debugging, it tries to
// call virtual methods on this object that are only (meaningfully) implemented
// by the subclasses of AsyncWrap.
// This could, with bad luck, happen during the AsyncWrap() constructor,
// because we run JS code as part of it and that in turn can lead to a heapdump
// being taken, either through the inspector or our programmatic API for it.
// The object being initialized is not fully constructed at that point, and
// in particular its virtual function table points to the AsyncWrap one
// (as the subclass constructor has not yet begun execution at that point).
// This means that the functions that are used for heap dump memory tracking
// are not yet available, and trying to call them would crash the process.
// We use this particular `IsDoneInitializing()` method to tell the Environment
// that such debugging methods are not yet available.
// This may be somewhat unreliable when it comes to future changes, because
// at this point it *only* protects AsyncWrap subclasses, and *only* for cases
// where heap dumps are being taken while the init() hook is on the call stack.
// For now, it seems like the best solution, though.
bool AsyncWrap::IsDoneInitializing() const {
  return init_hook_ran_;
}

AsyncWrap::~AsyncWrap() {
  EmitTraceEventDestroy();
  EmitDestroy(true /* from gc */);
}

void AsyncWrap::EmitTraceEventDestroy() {
  switch (provider_type()) {
  #define V(PROVIDER)                                                         \
    case PROVIDER_ ## PROVIDER:                                               \
      TRACE_EVENT_NESTABLE_ASYNC_END0(                                        \
        TRACING_CATEGORY_NODE1(async_hooks),                                  \
        #PROVIDER, static_cast<int64_t>(get_async_id()));                     \
      break;
    NODE_ASYNC_PROVIDER_TYPES(V)
  #undef V
    default:
      UNREACHABLE();
  }
}

void AsyncWrap::EmitDestroy(Environment* env, double async_id) {
  if (env->async_hooks()->fields()[AsyncHooks::kDestroy] == 0 ||
      !env->can_call_into_js()) {
    return;
  }

  if (env->destroy_async_id_list()->empty()) {
    env->SetImmediate(&DestroyAsyncIdsCallback, CallbackFlags::kUnrefed);
  }

  // If the list gets very large empty it faster using a Microtask.
  // Microtasks can't be added in GC context therefore we use an
  // interrupt to get this Microtask scheduled as fast as possible.
  if (env->destroy_async_id_list()->size() == 16384) {
    env->RequestInterrupt([](Environment* env) {
      env->context()->GetMicrotaskQueue()->EnqueueMicrotask(
        env->isolate(),
        [](void* arg) {
          DestroyAsyncIdsCallback(static_cast<Environment*>(arg));
        }, env);
      });
  }

  env->destroy_async_id_list()->push_back(async_id);
}

// Generalized call for both the constructor and for handles that are pooled
// and reused over their lifetime. This way a new uid can be assigned when
// the resource is pulled out of the pool and put back into use.
void AsyncWrap::AsyncReset(Local<Object> resource, double execution_async_id) {
  CHECK_NE(provider_type(), PROVIDER_NONE);

  if (async_id_ != kInvalidAsyncId) {
    // This instance was in use before, we have already emitted an init with
    // its previous async_id and need to emit a matching destroy for that
    // before generating a new async_id.
    EmitDestroy();
  }

  // Now we can assign a new async_id_ to this instance.
  async_id_ = execution_async_id == kInvalidAsyncId ? env()->new_async_id()
                                                     : execution_async_id;
  trigger_async_id_ = env()->get_default_trigger_async_id();

  Isolate* isolate = env()->isolate();
  {
    HandleScope handle_scope(isolate);
    Local<Object> obj = object();
    CHECK(!obj.IsEmpty());
    if (resource != obj) {
      USE(obj->Set(env()->context(), env()->resource_symbol(), resource));
    }
  }

  switch (provider_type()) {
#define V(PROVIDER)                                                           \
    case PROVIDER_ ## PROVIDER:                                               \
      if (*TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(                        \
          TRACING_CATEGORY_NODE1(async_hooks))) {                             \
        auto data = tracing::TracedValue::Create();                           \
        data->SetInteger("executionAsyncId",                                  \
                         static_cast<int64_t>(env()->execution_async_id()));  \
        data->SetInteger("triggerAsyncId",                                    \
                         static_cast<int64_t>(get_trigger_async_id()));       \
        TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(                                    \
          TRACING_CATEGORY_NODE1(async_hooks),                                \
          #PROVIDER, static_cast<int64_t>(get_async_id()),                    \
          "data", std::move(data));                                           \
        }                                                                     \
      break;
    NODE_ASYNC_PROVIDER_TYPES(V)
#undef V
    default:
      UNREACHABLE();
  }

  context_frame_.Reset(isolate, async_context_frame::current(isolate));

  EmitAsyncInit(env(), resource,
                env()->async_hooks()->provider_string(provider_type()),
                async_id_, trigger_async_id_);
}

void AsyncWrap::EmitAsyncInit(Environment* env,
                              Local<Object> object,
                              Local<String> type,
                              double async_id,
                              double trigger_async_id) {
  CHECK(!object.IsEmpty());
  CHECK(!type.IsEmpty());
  AsyncHooks* async_hooks = env->async_hooks();

  // Nothing to execute, so can continue normally.
  if (async_hooks->fields()[AsyncHooks::kInit] == 0) {
    return;
  }

  HandleScope scope(env->isolate());
  Local<Function> init_fn = env->async_hooks_init_function();

  Local<Value> argv[] = {
    Number::New(env->isolate(), async_id),
    type,
    Number::New(env->isolate(), trigger_async_id),
    object,
  };

  TryCatchScope try_catch(env, TryCatchScope::CatchMode::kFatal);
  USE(init_fn->Call(env->context(), object, arraysize(argv), argv));
}


MaybeLocal<Value> AsyncWrap::MakeCallback(const Local<Function> cb,
                                          int argc,
                                          Local<Value>* argv) {
  EmitTraceEventBefore();

  ProviderType provider = provider_type();
  async_context context { get_async_id(), get_trigger_async_id() };
  MaybeLocal<Value> ret =
      InternalMakeCallback(env(),
                           object(),
                           object(),
                           cb,
                           argc,
                           argv,
                           context,
                           context_frame_.Get(env()->isolate()));

  // This is a static call with cached values because the `this` object may
  // no longer be alive at this point.
  EmitTraceEventAfter(provider, context.async_id);

  return ret;
}

const char* AsyncWrap::MemoryInfoName() const {
  return provider_names[provider_type()];
}

std::string AsyncWrap::diagnostic_name() const {
  char buf[64];
  snprintf(buf,
           sizeof(buf),
           "%s(%" PRIu64 ":%.0f)",
           MemoryInfoName(),
           env()->thread_id(),
           async_id_);
  return buf;
}

Local<Object> AsyncWrap::GetOwner() {
  return GetOwner(env(), object());
}

Local<Object> AsyncWrap::GetOwner(Environment* env, Local<Object> obj) {
  EscapableHandleScope handle_scope(env->isolate());
  CHECK(!obj.IsEmpty());

  TryCatchScope ignore_exceptions(env);
  while (true) {
    Local<Value> owner;
    if (!obj->Get(env->context(),
                  env->owner_symbol()).ToLocal(&owner) ||
        !owner->IsObject()) {
      return handle_scope.Escape(obj);
    }

    obj = owner.As<Object>();
  }
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(async_wrap,
                                    node::AsyncWrap::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(async_wrap,
                              node::AsyncWrap::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(async_wrap,
                                node::AsyncWrap::RegisterExternalReferences)
