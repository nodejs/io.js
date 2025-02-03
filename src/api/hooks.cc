#include "env-inl.h"
#include "node_internals.h"
#include "node_process-inl.h"
#include "async_wrap.h"

namespace node {

using v8::Context;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::NewStringType;
using v8::Nothing;
using v8::Object;
using v8::String;

void RunAtExit(Environment* env) {
  env->RunAtExitCallbacks();
}

void AtExit(Environment* env, void (*cb)(void* arg), void* arg) {
  CHECK_NOT_NULL(env);
  env->AtExit(cb, arg);
}

void EmitBeforeExit(Environment* env) {
  USE(EmitProcessBeforeExit(env));
}

Maybe<bool> EmitProcessBeforeExit(Environment* env) {
  TRACE_EVENT0(TRACING_CATEGORY_NODE1(environment), "BeforeExit");
  if (!env->destroy_async_id_list()->empty())
    AsyncWrap::DestroyAsyncIdsCallback(env);

  Isolate* isolate = env->isolate();
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(env->context());

  if (!env->can_call_into_js()) {
    return Nothing<bool>();
  }

  Local<Integer> exit_code = Integer::New(
      isolate, static_cast<int32_t>(env->exit_code(ExitCode::kNoFailure)));

  return ProcessEmit(env, "beforeExit", exit_code).IsEmpty() ? Nothing<bool>()
                                                             : Just(true);
}

static ExitCode EmitExitInternal(Environment* env) {
  return EmitProcessExitInternal(env).FromMaybe(ExitCode::kGenericUserError);
}

int EmitExit(Environment* env) {
  return static_cast<int>(EmitExitInternal(env));
}

Maybe<ExitCode> EmitProcessExitInternal(Environment* env) {
  // process.emit('exit')
  Isolate* isolate = env->isolate();
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(env->context());

  env->set_exiting(true);

  if (!env->can_call_into_js()) {
    return Nothing<ExitCode>();
  }

  Local<Integer> exit_code = Integer::New(
      isolate, static_cast<int32_t>(env->exit_code(ExitCode::kNoFailure)));

  if (ProcessEmit(env, "exit", exit_code).IsEmpty()) {
    return Nothing<ExitCode>();
  }
  // Reload exit code, it may be changed by `emit('exit')`
  return Just(env->exit_code(ExitCode::kNoFailure));
}

Maybe<int> EmitProcessExit(Environment* env) {
  Maybe<ExitCode> result = EmitProcessExitInternal(env);
  if (result.IsNothing()) {
    return Nothing<int>();
  }
  return Just(static_cast<int>(result.FromJust()));
}

typedef void (*CleanupHook)(void* arg);
typedef void (*AsyncCleanupHook)(void* arg, void(*)(void*), void*);

struct AsyncCleanupHookInfo final {
  Environment* env;
  AsyncCleanupHook fun;
  void* arg;
  bool started = false;
  // Use a self-reference to make sure the storage is kept alive while the
  // cleanup hook is registered but not yet finished.
  std::shared_ptr<AsyncCleanupHookInfo> self;
};

// Opaque type that is basically an alias for `shared_ptr<AsyncCleanupHookInfo>`
// (but not publicly so for easier ABI/API changes). In particular,
// std::shared_ptr does not generally maintain a consistent ABI even on a
// specific platform.
struct ACHHandle final {
  std::shared_ptr<AsyncCleanupHookInfo> info;
};
// This is implemented as an operator on a struct because otherwise you can't
// default-initialize AsyncCleanupHookHandle, because in C++ for a
// std::unique_ptr to be default-initializable the deleter type also needs
// to be default-initializable; in particular, function types don't satisfy
// this.
void DeleteACHHandle::operator ()(ACHHandle* handle) const { delete handle; }

void AddEnvironmentCleanupHook(Isolate* isolate,
                               CleanupHook fun,
                               void* arg) {
  Environment* env = Environment::GetCurrent(isolate);
  CHECK_NOT_NULL(env);
  env->AddCleanupHook(fun, arg);
}

void RemoveEnvironmentCleanupHook(Isolate* isolate,
                                  CleanupHook fun,
                                  void* arg) {
  Environment* env = Environment::GetCurrent(isolate);
  CHECK_NOT_NULL(env);
  env->RemoveCleanupHook(fun, arg);
}

static void FinishAsyncCleanupHook(void* arg) {
  AsyncCleanupHookInfo* info = static_cast<AsyncCleanupHookInfo*>(arg);
  std::shared_ptr<AsyncCleanupHookInfo> keep_alive = info->self;

  info->env->DecreaseWaitingRequestCounter();
  info->self.reset();
}

static void RunAsyncCleanupHook(void* arg) {
  AsyncCleanupHookInfo* info = static_cast<AsyncCleanupHookInfo*>(arg);
  info->env->IncreaseWaitingRequestCounter();
  info->started = true;
  info->fun(info->arg, FinishAsyncCleanupHook, info);
}

ACHHandle* AddEnvironmentCleanupHookInternal(
    Isolate* isolate,
    AsyncCleanupHook fun,
    void* arg) {
  Environment* env = Environment::GetCurrent(isolate);
  CHECK_NOT_NULL(env);
  auto info = std::make_shared<AsyncCleanupHookInfo>();
  info->env = env;
  info->fun = fun;
  info->arg = arg;
  info->self = info;
  env->AddCleanupHook(RunAsyncCleanupHook, info.get());
  return new ACHHandle { info };
}

void RemoveEnvironmentCleanupHookInternal(
    ACHHandle* handle) {
  if (handle->info->started) return;
  handle->info->self.reset();
  handle->info->env->RemoveCleanupHook(RunAsyncCleanupHook, handle->info.get());
}

void RequestInterrupt(Environment* env, void (*fun)(void* arg), void* arg) {
  env->RequestInterrupt([fun, arg](Environment* env) {
    // Disallow JavaScript execution during interrupt.
    Isolate::DisallowJavascriptExecutionScope scope(
        env->isolate(),
        Isolate::DisallowJavascriptExecutionScope::CRASH_ON_FAILURE);
    fun(arg);
  });
}

async_id AsyncHooksGetExecutionAsyncId(Isolate* isolate) {
  Environment* env = Environment::GetCurrent(isolate);
  if (env == nullptr) return -1;
  return env->execution_async_id();
}

async_id AsyncHooksGetTriggerAsyncId(Isolate* isolate) {
  Environment* env = Environment::GetCurrent(isolate);
  if (env == nullptr) return -1;
  return env->trigger_async_id();
}


async_context EmitAsyncInit(Isolate* isolate,
                            Local<Object> resource,
                            const char* name,
                            async_id trigger_async_id) {
  HandleScope handle_scope(isolate);
  Local<String> type =
      String::NewFromUtf8(isolate, name, NewStringType::kInternalized)
          .ToLocalChecked();
  return EmitAsyncInit(isolate, resource, type, trigger_async_id);
}

async_context EmitAsyncInit(Isolate* isolate,
                            Local<Object> resource,
                            Local<String> name,
                            async_id trigger_async_id) {
  DebugSealHandleScope handle_scope(isolate);
  Environment* env = Environment::GetCurrent(isolate);
  CHECK_NOT_NULL(env);

  // Initialize async context struct
  if (trigger_async_id == -1)
    trigger_async_id = env->get_default_trigger_async_id();

  async_context context = {
    env->new_async_id(),  // async_id_
    trigger_async_id  // trigger_async_id_
  };

  // Run init hooks
  AsyncWrap::EmitAsyncInit(env, resource, name, context.async_id,
                           context.trigger_async_id);

  return context;
}

void EmitAsyncDestroy(Isolate* isolate, async_context asyncContext) {
  EmitAsyncDestroy(Environment::GetCurrent(isolate), asyncContext);
}

void EmitAsyncDestroy(Environment* env, async_context asyncContext) {
  AsyncWrap::EmitDestroy(env, asyncContext.async_id);
}

}  // namespace node
