#include "node_shadow_realm.h"
#include "env-inl.h"
#include "node_errors.h"
#include "node_process.h"

namespace node {
namespace shadow_realm {
using v8::Context;
using v8::EscapableHandleScope;
using v8::HandleScope;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::String;
using v8::Value;

using TryCatchScope = node::errors::TryCatchScope;

// static
ShadowRealm* ShadowRealm::New(Environment* env) {
  ShadowRealm* realm = new ShadowRealm(env);
  // TODO(legendecas): required by node::PromiseRejectCallback.
  // Remove this once promise rejection doesn't need to be handled across
  // realms.
  realm->context()->SetSecurityToken(
      env->principal_realm()->context()->GetSecurityToken());

  // We do not expect the realm bootstrapping to throw any
  // exceptions. If it does, exit the current Node.js instance.
  TryCatchScope try_catch(env, TryCatchScope::CatchMode::kFatal);
  if (realm->RunBootstrapping().IsEmpty()) {
    delete realm;
    return nullptr;
  }
  return realm;
}

// static
MaybeLocal<Context> HostCreateShadowRealmContextCallback(
    Local<Context> initiator_context) {
  Environment* env = Environment::GetCurrent(initiator_context);
  EscapableHandleScope scope(env->isolate());

  // We do not expect the realm bootstrapping to throw any
  // exceptions. If it does, exit the current Node.js instance.
  TryCatchScope try_catch(env, TryCatchScope::CatchMode::kFatal);
  ShadowRealm* realm = ShadowRealm::New(env);
  if (realm != nullptr) {
    return scope.Escape(realm->context());
  }
  return MaybeLocal<Context>();
}

// static
void ShadowRealm::WeakCallback(const v8::WeakCallbackInfo<ShadowRealm>& data) {
  ShadowRealm* realm = data.GetParameter();
  realm->context_.Reset();

  // Yield to pending weak callbacks before deleting the realm.
  // This is necessary to avoid cleaning up base objects before their scheduled
  // weak callbacks are invoked, which can lead to accessing to v8 apis during
  // the first pass of the weak callback.
  realm->env()->SetImmediate([realm](Environment* env) { delete realm; });
  // Remove the cleanup hook to avoid deleting the realm again.
  realm->env()->RemoveCleanupHook(DeleteMe, realm);
}

// static
void ShadowRealm::DeleteMe(void* data) {
  ShadowRealm* realm = static_cast<ShadowRealm*>(data);
  // Clear the context handle to avoid invoking the weak callback again.
  // Also, the context internal slots are cleared and the context is no longer
  // reference to the realm.
  delete realm;
}

ShadowRealm::ShadowRealm(Environment* env)
    : Realm(env, NewContext(env->isolate()), kShadowRealm) {
  context_.SetWeak(this, WeakCallback, v8::WeakCallbackType::kParameter);
  CreateProperties();

  env->TrackShadowRealm(this);
  env->AddCleanupHook(DeleteMe, this);
}

ShadowRealm::~ShadowRealm() {
  while (HasCleanupHooks()) {
    RunCleanup();
  }

  env_->UntrackShadowRealm(this);

  if (context_.IsEmpty()) {
    // This most likely happened because the weak callback cleared it.
    return;
  }

  {
    HandleScope handle_scope(isolate());
    env_->UnassignFromContext(context());
  }
}

v8::Local<v8::Context> ShadowRealm::context() const {
  Local<Context> ctx = PersistentToLocal::Default(isolate_, context_);
  DCHECK(!ctx.IsEmpty());
  return ctx;
}

// V8 can not infer reference cycles between global persistent handles, e.g.
// the Realm's Context handle and the per-realm function handles.
// Attach the per-realm strong persistent values' lifetime to the context's
// global object to avoid the strong global references to the per-realm objects
// keep the context alive indefinitely.
#define V(PropertyName, TypeName)                                              \
  v8::Local<TypeName> ShadowRealm::PropertyName() const {                      \
    return PersistentToLocal::Default(isolate_, PropertyName##_);              \
  }                                                                            \
  void ShadowRealm::set_##PropertyName(v8::Local<TypeName> value) {            \
    HandleScope scope(isolate_);                                               \
    PropertyName##_.Reset(isolate_, value);                                    \
    v8::Local<v8::Context> ctx = context();                                    \
    if (value.IsEmpty()) {                                                     \
      ctx->Global()                                                            \
          ->SetPrivate(ctx,                                                    \
                       isolate_data()->per_realm_##PropertyName(),             \
                       v8::Undefined(isolate_))                                \
          .ToChecked();                                                        \
    } else {                                                                   \
      PropertyName##_.SetWeak();                                               \
      ctx->Global()                                                            \
          ->SetPrivate(ctx, isolate_data()->per_realm_##PropertyName(), value) \
          .ToChecked();                                                        \
    }                                                                          \
  }
PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

v8::MaybeLocal<v8::Value> ShadowRealm::BootstrapRealm() {
  HandleScope scope(isolate_);

  // Skip "internal/bootstrap/node" as it installs node globals and per-isolate
  // callbacks.

  if (!env_->no_browser_globals()) {
    if (ExecuteBootstrapper("internal/bootstrap/web/exposed-wildcard")
            .IsEmpty()) {
      return MaybeLocal<Value>();
    }
  }

  // The process object is not exposed globally in ShadowRealm yet.
  // However, the process properties need to be setup for built-in modules.
  // Specifically, process.cwd() is needed by the ESM loader.
  if (ExecuteBootstrapper(
          "internal/bootstrap/switches/does_not_own_process_state")
          .IsEmpty()) {
    return MaybeLocal<Value>();
  }

  // Setup process.env proxy.
  Local<String> env_string = FIXED_ONE_BYTE_STRING(isolate_, "env");
  Local<Object> env_proxy;
  if (!isolate_data()->env_proxy_template()->NewInstance(context()).ToLocal(
          &env_proxy) ||
      process_object()->Set(context(), env_string, env_proxy).IsNothing()) {
    return MaybeLocal<Value>();
  }

  if (ExecuteBootstrapper("internal/bootstrap/shadow_realm").IsEmpty()) {
    return MaybeLocal<Value>();
  }

  return v8::True(isolate_);
}

}  // namespace shadow_realm
}  // namespace node
