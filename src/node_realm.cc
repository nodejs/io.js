#include "node_realm.h"
#include "env-inl.h"

#include "memory_tracker-inl.h"
#include "node_builtins.h"
#include "node_process.h"
#include "util.h"

namespace node {

using v8::Context;
using v8::EscapableHandleScope;
using v8::HandleScope;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::SnapshotCreator;
using v8::String;
using v8::Value;

Realm::Realm(Environment* env, v8::Local<v8::Context> context, Kind kind)
    : env_(env), isolate_(context->GetIsolate()), kind_(kind) {
  context_.Reset(isolate_, context);
  env->AssignToContext(context, this, ContextInfo(""));
}

Realm::~Realm() {
  CHECK_EQ(base_object_count_, 0);
}

void Realm::MemoryInfo(MemoryTracker* tracker) const {
#define V(PropertyName, TypeName)                                              \
  tracker->TrackField(#PropertyName, PropertyName());
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

  tracker->TrackField("cleanup_queue", cleanup_queue_);
  tracker->TrackField("builtins_with_cache", builtins_with_cache);
  tracker->TrackField("builtins_without_cache", builtins_without_cache);
}

void Realm::CreateProperties() {
  HandleScope handle_scope(isolate_);
  Local<Context> ctx = context();

  // Store primordials setup by the per-context script in the environment.
  Local<Object> per_context_bindings =
      GetPerContextExports(ctx).ToLocalChecked();
  Local<Value> primordials =
      per_context_bindings->Get(ctx, env_->primordials_string())
          .ToLocalChecked();
  CHECK(primordials->IsObject());
  set_primordials(primordials.As<Object>());

  Local<String> prototype_string =
      FIXED_ONE_BYTE_STRING(isolate(), "prototype");

#define V(EnvPropertyName, PrimordialsPropertyName)                            \
  {                                                                            \
    Local<Value> ctor =                                                        \
        primordials.As<Object>()                                               \
            ->Get(ctx,                                                         \
                  FIXED_ONE_BYTE_STRING(isolate(), PrimordialsPropertyName))   \
            .ToLocalChecked();                                                 \
    CHECK(ctor->IsObject());                                                   \
    Local<Value> prototype =                                                   \
        ctor.As<Object>()->Get(ctx, prototype_string).ToLocalChecked();        \
    CHECK(prototype->IsObject());                                              \
    set_##EnvPropertyName(prototype.As<Object>());                             \
  }

  V(primordials_safe_map_prototype_object, "SafeMap");
  V(primordials_safe_set_prototype_object, "SafeSet");
  V(primordials_safe_weak_map_prototype_object, "SafeWeakMap");
  V(primordials_safe_weak_set_prototype_object, "SafeWeakSet");
#undef V

  // TODO(legendecas): some methods probably doesn't need to be created with
  // process. Distinguish them and create process object only in the principal
  // realm.
  Local<Object> process_object =
      node::CreateProcessObject(this).FromMaybe(Local<Object>());
  set_process_object(process_object);
}

RealmSerializeInfo Realm::Serialize(SnapshotCreator* creator) {
  RealmSerializeInfo info;
  Local<Context> ctx = context();

  // Currently all modules are compiled without cache in builtin snapshot
  // builder.
  info.builtins = std::vector<std::string>(builtins_without_cache.begin(),
                                           builtins_without_cache.end());

  uint32_t id = 0;
#define V(PropertyName, TypeName)                                              \
  do {                                                                         \
    Local<TypeName> field = PropertyName();                                    \
    if (!field.IsEmpty()) {                                                    \
      size_t index = creator->AddData(ctx, field);                             \
      info.persistent_values.push_back({#PropertyName, id, index});            \
    }                                                                          \
    id++;                                                                      \
  } while (0);
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

  // Do this after other creator->AddData() calls so that Snapshotable objects
  // can use 0 to indicate that a SnapshotIndex is invalid.
  SerializeSnapshotableObjects(this, creator, &info);

  info.context = creator->AddData(ctx, ctx);
  return info;
}

void Realm::DeserializeProperties(const RealmSerializeInfo* info) {
  Local<Context> ctx = context();

  builtins_in_snapshot = info->builtins;

  const std::vector<PropInfo>& values = info->persistent_values;
  size_t i = 0;  // index to the array
  uint32_t id = 0;
#define V(PropertyName, TypeName)                                              \
  do {                                                                         \
    if (values.size() > i && id == values[i].id) {                             \
      const PropInfo& d = values[i];                                           \
      DCHECK_EQ(d.name, #PropertyName);                                        \
      MaybeLocal<TypeName> maybe_field =                                       \
          ctx->GetDataFromSnapshotOnce<TypeName>(d.index);                     \
      Local<TypeName> field;                                                   \
      if (!maybe_field.ToLocal(&field)) {                                      \
        fprintf(stderr,                                                        \
                "Failed to deserialize realm value " #PropertyName "\n");      \
      }                                                                        \
      set_##PropertyName(field);                                               \
      i++;                                                                     \
    }                                                                          \
    id++;                                                                      \
  } while (0);

  PER_REALM_STRONG_PERSISTENT_VALUES(V);
#undef V

  MaybeLocal<Context> maybe_ctx_from_snapshot =
      ctx->GetDataFromSnapshotOnce<Context>(info->context);
  Local<Context> ctx_from_snapshot;
  if (!maybe_ctx_from_snapshot.ToLocal(&ctx_from_snapshot)) {
    fprintf(stderr,
            "Failed to deserialize context back reference from the snapshot\n");
  }
  CHECK_EQ(ctx_from_snapshot, ctx);

  DoneBootstrapping();
}

MaybeLocal<Value> Realm::ExecuteBootstrapper(const char* id) {
  EscapableHandleScope scope(isolate());
  Local<Context> ctx = context();
  MaybeLocal<Value> result =
      env()->builtin_loader()->CompileAndCall(ctx, id, this);

  // If there was an error during bootstrap, it must be unrecoverable
  // (e.g. max call stack exceeded). Clear the stack so that the
  // AsyncCallbackScope destructor doesn't fail on the id check.
  // There are only two ways to have a stack size > 1: 1) the user manually
  // called MakeCallback or 2) user awaited during bootstrap, which triggered
  // _tickCallback().
  if (result.IsEmpty()) {
    env()->async_hooks()->clear_async_id_stack();
  }

  return scope.EscapeMaybe(result);
}

MaybeLocal<Value> Realm::RunBootstrapping() {
  EscapableHandleScope scope(isolate_);

  CHECK(!has_run_bootstrapping_code());

  Local<Value> result;
  if (!ExecuteBootstrapper("internal/bootstrap/realm").ToLocal(&result) ||
      !BootstrapRealm().ToLocal(&result)) {
    return MaybeLocal<Value>();
  }

  DoneBootstrapping();

  return scope.Escape(result);
}

void Realm::DoneBootstrapping() {
  // Make sure that no request or handle is created during bootstrap -
  // if necessary those should be done in pre-execution.
  // Usually, doing so would trigger the checks present in the ReqWrap and
  // HandleWrap classes, so this is only a consistency check.

  // TODO(legendecas): track req_wrap and handle_wrap by realms instead of
  // environments.
  if (kind_ == kPrincipal) {
    CHECK(env_->req_wrap_queue()->IsEmpty());
    CHECK(env_->handle_wrap_queue()->IsEmpty());
  }

  has_run_bootstrapping_code_ = true;

  // This adjusts the return value of base_object_created_after_bootstrap() so
  // that tests that check the count do not have to account for internally
  // created BaseObjects.
  base_object_created_by_bootstrap_ = base_object_count_;
}

void Realm::RunCleanup() {
  TRACE_EVENT0(TRACING_CATEGORY_NODE1(realm), "RunCleanup");
  for (size_t i = 0; i < binding_data_store_.size(); ++i) {
    binding_data_store_[i].reset();
  }
  cleanup_queue_.Drain();
}

void Realm::PrintInfoForSnapshot() {
  fprintf(stderr, "Realm = %p\n", this);
  fprintf(stderr, "BaseObjects of the Realm:\n");
  size_t i = 0;
  ForEachBaseObject([&](BaseObject* obj) {
    std::cerr << "#" << i++ << " " << obj << ": " << obj->MemoryInfoName()
              << "\n";
  });

  fprintf(stderr, "\nBuiltins without cache:\n");
  for (const auto& s : builtins_without_cache) {
    fprintf(stderr, "%s\n", s.c_str());
  }
  fprintf(stderr, "\nBuiltins with cache:\n");
  for (const auto& s : builtins_with_cache) {
    fprintf(stderr, "%s\n", s.c_str());
  }
  fprintf(stderr, "\nStatic bindings (need to be registered):\n");
  for (const auto mod : internal_bindings) {
    fprintf(stderr, "%s:%s\n", mod->nm_filename, mod->nm_modname);
  }

  fprintf(stderr, "End of the Realm.\n");
}

void Realm::VerifyNoStrongBaseObjects() {
  // When a process exits cleanly, i.e. because the event loop ends up without
  // things to wait for, the Node.js objects that are left on the heap should
  // be:
  //
  //   1. weak, i.e. ready for garbage collection once no longer referenced, or
  //   2. detached, i.e. scheduled for destruction once no longer referenced, or
  //   3. an unrefed libuv handle, i.e. does not keep the event loop alive, or
  //   4. an inactive libuv handle (essentially the same here)
  //
  // There are a few exceptions to this rule, but generally, if there are
  // C++-backed Node.js objects on the heap that do not fall into the above
  // categories, we may be looking at a potential memory leak. Most likely,
  // the cause is a missing MakeWeak() call on the corresponding object.
  //
  // In order to avoid this kind of problem, we check the list of BaseObjects
  // for these criteria. Currently, we only do so when explicitly instructed to
  // or when in debug mode (where --verify-base-objects is always-on).

  // TODO(legendecas): introduce per-realm options.
  if (!env()->options()->verify_base_objects) return;

  ForEachBaseObject([](BaseObject* obj) {
    if (obj->IsNotIndicativeOfMemoryLeakAtExit()) return;
    fprintf(stderr,
            "Found bad BaseObject during clean exit: %s\n",
            obj->MemoryInfoName());
    fflush(stderr);
    ABORT();
  });
}

v8::Local<v8::Context> Realm::context() const {
  return PersistentToLocal::Strong(context_);
}

// Per-realm strong value accessors. The per-realm values should avoid being
// accessed across realms.
#define V(PropertyName, TypeName)                                              \
  v8::Local<TypeName> PrincipalRealm::PropertyName() const {                   \
    return PersistentToLocal::Strong(PropertyName##_);                         \
  }                                                                            \
  void PrincipalRealm::set_##PropertyName(v8::Local<TypeName> value) {         \
    DCHECK_IMPLIES(!value.IsEmpty(),                                           \
                   isolate()->GetCurrentContext() == context());               \
    PropertyName##_.Reset(isolate(), value);                                   \
  }
PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V

PrincipalRealm::PrincipalRealm(Environment* env,
                               v8::Local<v8::Context> context,
                               const RealmSerializeInfo* realm_info)
    : Realm(env, context, kPrincipal) {
  // Create properties if not deserializing from snapshot.
  // Or the properties are deserialized with DeserializeProperties() when the
  // env drained the deserialize requests.
  if (realm_info == nullptr) {
    CreateProperties();
  }
}

PrincipalRealm::~PrincipalRealm() {
  DCHECK(!context_.IsEmpty());

  HandleScope handle_scope(isolate());
  env_->UnassignFromContext(context());
}

MaybeLocal<Value> PrincipalRealm::BootstrapRealm() {
  HandleScope scope(isolate_);

  if (ExecuteBootstrapper("internal/bootstrap/node").IsEmpty()) {
    return MaybeLocal<Value>();
  }

  if (!env_->no_browser_globals()) {
    if (ExecuteBootstrapper("internal/bootstrap/web/exposed-wildcard")
            .IsEmpty() ||
        ExecuteBootstrapper("internal/bootstrap/web/exposed-window-or-worker")
            .IsEmpty()) {
      return MaybeLocal<Value>();
    }
  }

  // TODO(joyeecheung): skip these in the snapshot building for workers.
  auto thread_switch_id =
      env_->is_main_thread() ? "internal/bootstrap/switches/is_main_thread"
                             : "internal/bootstrap/switches/is_not_main_thread";
  if (ExecuteBootstrapper(thread_switch_id).IsEmpty()) {
    return MaybeLocal<Value>();
  }

  auto process_state_switch_id =
      env_->owns_process_state()
          ? "internal/bootstrap/switches/does_own_process_state"
          : "internal/bootstrap/switches/does_not_own_process_state";
  if (ExecuteBootstrapper(process_state_switch_id).IsEmpty()) {
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

  return v8::True(isolate_);
}

}  // namespace node
