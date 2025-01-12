#include "node_worker.h"
#include "async_wrap-inl.h"
#include "debug_utils-inl.h"
#include "histogram-inl.h"
#include "memory_tracker-inl.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_options-inl.h"
#include "node_perf.h"
#include "node_snapshot_builder.h"
#include "permission/permission.h"
#include "util-inl.h"
#include "v8-cppgc.h"

#include <memory>
#include <string>
#include <vector>

using node::kAllowedInEnvvar;
using node::kDisallowedInEnvvar;
using v8::Array;
using v8::ArrayBuffer;
using v8::Boolean;
using v8::Context;
using v8::Float64Array;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Locker;
using v8::Maybe;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::ResourceConstraints;
using v8::SealHandleScope;
using v8::String;
using v8::TryCatch;
using v8::Value;

namespace node {
namespace worker {

constexpr double kMB = 1024 * 1024;

Worker::Worker(Environment* env,
               Local<Object> wrap,
               const std::string& url,
               const std::string& name,
               std::shared_ptr<PerIsolateOptions> per_isolate_opts,
               std::vector<std::string>&& exec_argv,
               std::shared_ptr<KVStore> env_vars,
               const SnapshotData* snapshot_data,
               const bool is_internal)
    : AsyncWrap(env, wrap, AsyncWrap::PROVIDER_WORKER),
      per_isolate_opts_(per_isolate_opts),
      exec_argv_(exec_argv),
      platform_(env->isolate_data()->platform()),
      thread_id_(AllocateEnvironmentThreadId()),
      name_(name),
      env_vars_(env_vars),
      embedder_preload_(env->embedder_preload()),
      snapshot_data_(snapshot_data),
      is_internal_(is_internal) {
  Debug(this, "Creating new worker instance with thread id %llu",
        thread_id_.id);

  // Set up everything that needs to be set up in the parent environment.
  MessagePort* parent_port = MessagePort::New(env, env->context());
  if (parent_port == nullptr) {
    // This can happen e.g. because execution is terminating.
    return;
  }

  child_port_data_ = std::make_unique<MessagePortData>(nullptr);
  MessagePort::Entangle(parent_port, child_port_data_.get());

  object()
      ->Set(env->context(), env->message_port_string(), parent_port->object())
      .Check();

  object()->Set(env->context(),
                env->thread_id_string(),
                Number::New(env->isolate(), static_cast<double>(thread_id_.id)))
      .Check();

  // Without this check, to use the permission model with
  // workers (--allow-worker) one would need to pass --allow-inspector as well
  if (env->permission()->is_granted(
          env, node::permission::PermissionScope::kInspector)) {
    inspector_parent_handle_ =
        GetInspectorParentHandle(env, thread_id_, url.c_str(), name.c_str());
  }

  argv_ = std::vector<std::string>{env->argv()[0]};
  // Mark this Worker object as weak until we actually start the thread.
  MakeWeak();

  Debug(this, "Preparation for worker %llu finished", thread_id_.id);
}

bool Worker::is_stopped() const {
  Mutex::ScopedLock lock(mutex_);
  if (env_ != nullptr)
    return env_->is_stopping();
  return stopped_;
}

void Worker::UpdateResourceConstraints(ResourceConstraints* constraints) {
  constraints->set_stack_limit(reinterpret_cast<uint32_t*>(stack_base_));

  if (resource_limits_[kMaxYoungGenerationSizeMb] > 0) {
    constraints->set_max_young_generation_size_in_bytes(
        static_cast<size_t>(resource_limits_[kMaxYoungGenerationSizeMb] * kMB));
  } else {
    resource_limits_[kMaxYoungGenerationSizeMb] =
        constraints->max_young_generation_size_in_bytes() / kMB;
  }

  if (resource_limits_[kMaxOldGenerationSizeMb] > 0) {
    constraints->set_max_old_generation_size_in_bytes(
        static_cast<size_t>(resource_limits_[kMaxOldGenerationSizeMb] * kMB));
  } else {
    resource_limits_[kMaxOldGenerationSizeMb] =
        constraints->max_old_generation_size_in_bytes() / kMB;
  }

  if (resource_limits_[kCodeRangeSizeMb] > 0) {
    constraints->set_code_range_size_in_bytes(
        static_cast<size_t>(resource_limits_[kCodeRangeSizeMb] * kMB));
  } else {
    resource_limits_[kCodeRangeSizeMb] =
        constraints->code_range_size_in_bytes() / kMB;
  }
}

// This class contains data that is only relevant to the child thread itself,
// and only while it is running.
// (Eventually, the Environment instance should probably also be moved here.)
class WorkerThreadData {
 public:
  explicit WorkerThreadData(Worker* w)
    : w_(w) {
    int ret = uv_loop_init(&loop_);
    if (ret != 0) {
      char err_buf[128];
      uv_err_name_r(ret, err_buf, sizeof(err_buf));
      // TODO(joyeecheung): maybe this should be kBootstrapFailure instead?
      w->Exit(ExitCode::kGenericUserError, "ERR_WORKER_INIT_FAILED", err_buf);
      return;
    }
    loop_init_failed_ = false;
    uv_loop_configure(&loop_, UV_METRICS_IDLE_TIME);

    std::shared_ptr<ArrayBufferAllocator> allocator =
        ArrayBufferAllocator::Create();
    Isolate::CreateParams params;
    SetIsolateCreateParamsForNode(&params);
    w->UpdateResourceConstraints(&params.constraints);
    params.array_buffer_allocator_shared = allocator;
    Isolate* isolate =
        NewIsolate(&params, &loop_, w->platform_, w->snapshot_data());
    if (isolate == nullptr) {
      // TODO(joyeecheung): maybe this should be kBootstrapFailure instead?
      w->Exit(ExitCode::kGenericUserError,
              "ERR_WORKER_INIT_FAILED",
              "Failed to create new Isolate");
      return;
    }

    SetIsolateUpForNode(isolate);

    // Be sure it's called before Environment::InitializeDiagnostics()
    // so that this callback stays when the callback of
    // --heapsnapshot-near-heap-limit gets is popped.
    isolate->AddNearHeapLimitCallback(Worker::NearHeapLimit, w);

    {
      Locker locker(isolate);
      Isolate::Scope isolate_scope(isolate);
      // V8 computes its stack limit the first time a `Locker` is used based on
      // --stack-size. Reset it to the correct value.
      isolate->SetStackLimit(w->stack_base_);

      HandleScope handle_scope(isolate);
      isolate_data_.reset(IsolateData::CreateIsolateData(
          isolate,
          &loop_,
          w_->platform_,
          allocator.get(),
          w->snapshot_data()->AsEmbedderWrapper().get(),
          std::move(w_->per_isolate_opts_)));
      CHECK(isolate_data_);
      CHECK(!isolate_data_->is_building_snapshot());
      isolate_data_->set_worker_context(w_);
      isolate_data_->max_young_gen_size =
          params.constraints.max_young_generation_size_in_bytes();
    }

    Mutex::ScopedLock lock(w_->mutex_);
    w_->isolate_ = isolate;
  }

  ~WorkerThreadData() {
    Debug(w_, "Worker %llu dispose isolate", w_->thread_id_.id);
    Isolate* isolate;
    {
      Mutex::ScopedLock lock(w_->mutex_);
      isolate = w_->isolate_;
      w_->isolate_ = nullptr;
    }

    if (isolate != nullptr) {
      CHECK(!loop_init_failed_);
      bool platform_finished = false;

      // https://github.com/nodejs/node/issues/51129 - IsolateData destructor
      // can kick off GC before teardown, so ensure the isolate is entered.
      {
        Locker locker(isolate);
        Isolate::Scope isolate_scope(isolate);
        isolate_data_.reset();
      }

      w_->platform_->AddIsolateFinishedCallback(isolate, [](void* data) {
        *static_cast<bool*>(data) = true;
      }, &platform_finished);

      // The order of these calls is important; if the Isolate is first disposed
      // and then unregistered, there is a race condition window in which no
      // new Isolate at the same address can successfully be registered with
      // the platform.
      // (Refs: https://github.com/nodejs/node/issues/30846)
      w_->platform_->UnregisterIsolate(isolate);
      isolate->Dispose();

      // Wait until the platform has cleaned up all relevant resources.
      while (!platform_finished) {
        uv_run(&loop_, UV_RUN_ONCE);
      }
    }
    if (!loop_init_failed_) {
      CheckedUvLoopClose(&loop_);
    }
  }

  bool loop_is_usable() const { return !loop_init_failed_; }

 private:
  Worker* const w_;
  uv_loop_t loop_;
  bool loop_init_failed_ = true;
  DeleteFnPtr<IsolateData, FreeIsolateData> isolate_data_;
  const SnapshotData* snapshot_data_ = nullptr;
  friend class Worker;
};

size_t Worker::NearHeapLimit(void* data, size_t current_heap_limit,
                             size_t initial_heap_limit) {
  Worker* worker = static_cast<Worker*>(data);
  // Give the current GC some extra leeway to let it finish rather than
  // crash hard. We are not going to perform further allocations anyway.
  constexpr size_t kExtraHeapAllowance = 16 * 1024 * 1024;
  size_t new_limit = current_heap_limit + kExtraHeapAllowance;
  Environment* env = worker->env();
  if (env != nullptr) {
    DCHECK(!env->is_in_heapsnapshot_heap_limit_callback());
    Debug(env,
          DebugCategory::DIAGNOSTICS,
          "Throwing ERR_WORKER_OUT_OF_MEMORY, "
          "new_limit=%" PRIu64 "\n",
          static_cast<uint64_t>(new_limit));
  }
  // TODO(joyeecheung): maybe this should be kV8FatalError instead?
  worker->Exit(ExitCode::kGenericUserError,
               "ERR_WORKER_OUT_OF_MEMORY",
               "JS heap out of memory");
  return new_limit;
}

void Worker::Run() {
  std::string trace_name = "[worker " + std::to_string(thread_id_.id) + "]" +
                           (name_ == "" ? "" : " " + name_);
  TRACE_EVENT_METADATA1(
      "__metadata", "thread_name", "name", TRACE_STR_COPY(trace_name.c_str()));
  CHECK_NOT_NULL(platform_);

  Debug(this, "Creating isolate for worker with id %llu", thread_id_.id);

  WorkerThreadData data(this);
  if (isolate_ == nullptr) return;
  CHECK(data.loop_is_usable());

  Debug(this, "Starting worker with id %llu", thread_id_.id);
  {
    Locker locker(isolate_);
    Isolate::Scope isolate_scope(isolate_);
    SealHandleScope outer_seal(isolate_);

    DeleteFnPtr<Environment, FreeEnvironment> env_;
    auto cleanup_env = OnScopeLeave([&]() {
      // TODO(addaleax): This call is harmless but should not be necessary.
      // Figure out why V8 is raising a DCHECK() here without it
      // (in test/parallel/test-async-hooks-worker-asyncfn-terminate-4.js).
      isolate_->CancelTerminateExecution();

      if (!env_) return;
      env_->set_can_call_into_js(false);

      {
        Mutex::ScopedLock lock(mutex_);
        stopped_ = true;
        this->env_ = nullptr;
      }

      env_.reset();
    });

    if (is_stopped()) return;
    {
      HandleScope handle_scope(isolate_);
      Local<Context> context;
      {
        // We create the Context object before we have an Environment* in place
        // that we could use for error handling. If creation fails due to
        // resource constraints, we need something in place to handle it,
        // though.
        TryCatch try_catch(isolate_);
        if (snapshot_data_ != nullptr) {
          Debug(this,
                "Worker %llu uses context from snapshot %d\n",
                thread_id_.id,
                static_cast<int>(SnapshotData::kNodeBaseContextIndex));
          context = Context::FromSnapshot(isolate_,
                                          SnapshotData::kNodeBaseContextIndex)
                        .ToLocalChecked();
          if (!context.IsEmpty() &&
              !InitializeContextRuntime(context).IsJust()) {
            context = Local<Context>();
          }
        } else {
          Debug(
              this, "Worker %llu builds context from scratch\n", thread_id_.id);
          context = NewContext(isolate_);
        }
        if (context.IsEmpty()) {
          // TODO(joyeecheung): maybe this should be kBootstrapFailure instead?
          Exit(ExitCode::kGenericUserError,
               "ERR_WORKER_INIT_FAILED",
               "Failed to create new Context");
          return;
        }
      }

      if (is_stopped()) return;
      CHECK(!context.IsEmpty());
      Context::Scope context_scope(context);
      {
#if HAVE_INSPECTOR
        environment_flags_ |= EnvironmentFlags::kNoWaitForInspectorFrontend;
#endif
        env_.reset(CreateEnvironment(
            data.isolate_data_.get(),
            context,
            std::move(argv_),
            std::move(exec_argv_),
            static_cast<EnvironmentFlags::Flags>(environment_flags_),
            thread_id_,
            std::move(inspector_parent_handle_)));
        if (is_stopped()) return;
        CHECK_NOT_NULL(env_);
        env_->set_env_vars(std::move(env_vars_));
        SetProcessExitHandler(env_.get(), [this](Environment*, int exit_code) {
          Exit(static_cast<ExitCode>(exit_code));
        });
      }
      {
        Mutex::ScopedLock lock(mutex_);
        if (stopped_) return;
        this->env_ = env_.get();
      }
      Debug(this, "Created Environment for worker with id %llu", thread_id_.id);

#if HAVE_INSPECTOR
      this->env_->WaitForInspectorFrontendByOptions();
#endif
      if (is_stopped()) return;
      {
        if (!CreateEnvMessagePort(env_.get())) {
          return;
        }

        Debug(this, "Created message port for worker %llu", thread_id_.id);
        if (LoadEnvironment(env_.get(),
                            StartExecutionCallback{},
                            std::move(embedder_preload_))
                .IsEmpty()) {
          return;
        }

        Debug(this, "Loaded environment for worker %llu", thread_id_.id);
      }
    }

    {
      Maybe<ExitCode> exit_code = SpinEventLoopInternal(env_.get());
      Mutex::ScopedLock lock(mutex_);
      if (exit_code_ == ExitCode::kNoFailure && exit_code.IsJust()) {
        exit_code_ = exit_code.FromJust();
      }

      Debug(this,
            "Exiting thread for worker %llu with exit code %d",
            thread_id_.id,
            static_cast<int>(exit_code_));
    }
  }

  Debug(this, "Worker %llu thread stops", thread_id_.id);
}

bool Worker::CreateEnvMessagePort(Environment* env) {
  HandleScope handle_scope(isolate_);
  std::unique_ptr<MessagePortData> data;
  {
    Mutex::ScopedLock lock(mutex_);
    data = std::move(child_port_data_);
  }

  // Set up the message channel for receiving messages in the child.
  MessagePort* child_port = MessagePort::New(env,
                                             env->context(),
                                             std::move(data));
  // MessagePort::New() may return nullptr if execution is terminated
  // within it.
  if (child_port != nullptr)
    env->set_message_port(child_port->object(isolate_));

  return child_port;
}

void Worker::JoinThread() {
  if (!tid_.has_value())
    return;
  CHECK_EQ(uv_thread_join(&tid_.value()), 0);
  tid_.reset();

  env()->remove_sub_worker_context(this);

  // Join may happen after the worker exits and disposes the isolate
  if (!env()->can_call_into_js()) return;

  {
    HandleScope handle_scope(env()->isolate());
    Context::Scope context_scope(env()->context());

    // Reset the parent port as we're closing it now anyway.
    object()->Set(env()->context(),
                  env()->message_port_string(),
                  Undefined(env()->isolate())).Check();

    Local<Value> args[] = {
        Integer::New(env()->isolate(), static_cast<int>(exit_code_)),
        custom_error_ != nullptr
            ? OneByteString(env()->isolate(), custom_error_).As<Value>()
            : Null(env()->isolate()).As<Value>(),
        !custom_error_str_.empty()
            ? OneByteString(env()->isolate(), custom_error_str_.c_str())
                  .As<Value>()
            : Null(env()->isolate()).As<Value>(),
    };

    MakeCallback(env()->onexit_string(), arraysize(args), args);
  }

  // If we get here, the tid_.has_value() condition at the top of the function
  // implies that the thread was running. In that case, its final action will
  // be to schedule a callback on the parent thread which will delete this
  // object, so there's nothing more to do here.
}

Worker::~Worker() {
  Mutex::ScopedLock lock(mutex_);

  CHECK(stopped_);
  CHECK_NULL(env_);
  CHECK(!tid_.has_value());

  Debug(this, "Worker %llu destroyed", thread_id_.id);
}

void Worker::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  bool is_internal = args[5]->IsTrue();
  if (!is_internal) {
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kWorkerThreads, "");
  }
  Isolate* isolate = args.GetIsolate();

  CHECK(args.IsConstructCall());

  if (env->isolate_data()->platform() == nullptr) {
    THROW_ERR_MISSING_PLATFORM_FOR_WORKER(env);
    return;
  }
  CHECK(!env->isolate_data()->is_building_snapshot());

  std::string url;
  std::string name;
  std::shared_ptr<PerIsolateOptions> per_isolate_opts = nullptr;
  std::shared_ptr<KVStore> env_vars = nullptr;

  std::vector<std::string> exec_argv_out;

  // Argument might be a string or URL
  if (!args[0]->IsNullOrUndefined()) {
    Utf8Value value(
        isolate, args[0]->ToString(env->context()).FromMaybe(Local<String>()));
    url.append(value.out(), value.length());
  }

  if (!args[6]->IsNullOrUndefined()) {
    Utf8Value value(
        isolate, args[6]->ToString(env->context()).FromMaybe(Local<String>()));
    name.append(value.out(), value.length());
  }

  if (args[1]->IsNull()) {
    // Means worker.env = { ...process.env }.
    env_vars = env->env_vars()->Clone(isolate);
  } else if (args[1]->IsObject()) {
    // User provided env.
    env_vars = KVStore::CreateMapKVStore();
    if (env_vars
            ->AssignFromObject(isolate->GetCurrentContext(),
                               args[1].As<Object>())
            .IsNothing()) {
      return;
    }
  } else {
    // Env is shared.
    env_vars = env->env_vars();
  }

  if (args[1]->IsObject() || args[2]->IsArray()) {
    per_isolate_opts.reset(new PerIsolateOptions());

    HandleEnvOptions(per_isolate_opts->per_env, [&env_vars](const char* name) {
      return env_vars->Get(name).value_or("");
    });

#ifndef NODE_WITHOUT_NODE_OPTIONS
    std::optional<std::string> node_options = env_vars->Get("NODE_OPTIONS");
    if (node_options.has_value()) {
      std::vector<std::string> errors{};
      std::vector<std::string> env_argv =
          ParseNodeOptionsEnvVar(node_options.value(), &errors);
      // [0] is expected to be the program name, add dummy string.
      env_argv.insert(env_argv.begin(), "");
      std::vector<std::string> invalid_args{};

      std::optional<std::string> parent_node_options =
          env->env_vars()->Get("NODE_OPTIONS");

      // If the worker code passes { env: { ...process.env, ... } } or
      // the NODE_OPTIONS is otherwise character-for-character equal to the
      // original NODE_OPTIONS, allow per-process options inherited into
      // the worker since worker spawning code is not usually in charge of
      // how the NODE_OPTIONS is configured for the parent.
      // TODO(joyeecheung): a more intelligent filter may be more desirable.
      // but a string comparison is good enough(TM) for the case where the
      // worker spawning code just wants to pass the parent configuration down
      // and does not intend to modify NODE_OPTIONS.
      if (parent_node_options == node_options) {
        // Creates a wrapper per-process option over the per_isolate_opts
        // to allow per-process options copied from the parent.
        std::unique_ptr<PerProcessOptions> per_process_opts =
            std::make_unique<PerProcessOptions>();
        per_process_opts->per_isolate = per_isolate_opts;
        options_parser::Parse(&env_argv,
                              nullptr,
                              &invalid_args,
                              per_process_opts.get(),
                              kAllowedInEnvvar,
                              &errors);
      } else {
        options_parser::Parse(&env_argv,
                              nullptr,
                              &invalid_args,
                              per_isolate_opts.get(),
                              kAllowedInEnvvar,
                              &errors);
      }

      if (!errors.empty() && args[1]->IsObject()) {
        // Only fail for explicitly provided env, this protects from failures
        // when NODE_OPTIONS from parent's env is used (which is the default).
        Local<Value> error;
        if (!ToV8Value(env->context(), errors).ToLocal(&error)) return;
        Local<String> key =
            FIXED_ONE_BYTE_STRING(env->isolate(), "invalidNodeOptions");
        // Ignore the return value of Set() because exceptions bubble up to JS
        // when we return anyway.
        USE(args.This()->Set(env->context(), key, error));
        return;
      }
    }
#endif  // NODE_WITHOUT_NODE_OPTIONS

    // The first argument is reserved for program name, but we don't need it
    // in workers.
    std::vector<std::string> exec_argv = {""};
    if (args[2]->IsArray()) {
      Local<Array> array = args[2].As<Array>();
      uint32_t length = array->Length();
      for (uint32_t i = 0; i < length; i++) {
        Local<Value> arg;
        if (!array->Get(env->context(), i).ToLocal(&arg)) {
          return;
        }
        Local<String> arg_v8;
        if (!arg->ToString(env->context()).ToLocal(&arg_v8)) {
          return;
        }
        Utf8Value arg_utf8_value(args.GetIsolate(), arg_v8);
        std::string arg_string(arg_utf8_value.out(), arg_utf8_value.length());
        exec_argv.push_back(arg_string);
      }
    } else {
      exec_argv.insert(
          exec_argv.end(), env->exec_argv().begin(), env->exec_argv().end());
    }

    std::vector<std::string> invalid_args{};
    std::vector<std::string> errors{};
    // Using invalid_args as the v8_args argument as it stores unknown
    // options for the per isolate parser.
    options_parser::Parse(&exec_argv,
                          &exec_argv_out,
                          &invalid_args,
                          per_isolate_opts.get(),
                          kDisallowedInEnvvar,
                          &errors);

    // The first argument is program name.
    invalid_args.erase(invalid_args.begin());
    // Only fail for explicitly provided execArgv, this protects from failures
    // when execArgv from parent's execArgv is used (which is the default).
    if (errors.size() > 0 || (invalid_args.size() > 0 && args[2]->IsArray())) {
      Local<Value> error;
      if (!ToV8Value(env->context(), errors.size() > 0 ? errors : invalid_args)
               .ToLocal(&error)) {
        return;
      }
      Local<String> key =
          FIXED_ONE_BYTE_STRING(env->isolate(), "invalidExecArgv");
      // Ignore the return value of Set() because exceptions bubble up to JS
      // when we return anyway.
      USE(args.This()->Set(env->context(), key, error));
      return;
    }
  } else {
    // Copy the parent's execArgv.
    exec_argv_out = env->exec_argv();
    per_isolate_opts = env->isolate_data()->options()->Clone();
  }

  // Internal workers should not wait for inspector frontend to connect or
  // break on the first line of internal scripts. Module loader threads are
  // essential to load user codes and must not be blocked by the inspector
  // for internal scripts.
  // Still, `--inspect-node` can break on the first line of internal scripts.
  if (is_internal) {
    per_isolate_opts->per_env->get_debug_options()
        ->DisableWaitOrBreakFirstLine();
  }

  const SnapshotData* snapshot_data = env->isolate_data()->snapshot_data();

  Worker* worker = new Worker(env,
                              args.This(),
                              url,
                              name,
                              per_isolate_opts,
                              std::move(exec_argv_out),
                              env_vars,
                              snapshot_data,
                              is_internal);

  CHECK(args[3]->IsFloat64Array());
  Local<Float64Array> limit_info = args[3].As<Float64Array>();
  CHECK_EQ(limit_info->Length(), kTotalResourceLimitCount);
  limit_info->CopyContents(worker->resource_limits_,
                           sizeof(worker->resource_limits_));

  CHECK(args[4]->IsBoolean());
  if (args[4]->IsTrue() || env->tracks_unmanaged_fds())
    worker->environment_flags_ |= EnvironmentFlags::kTrackUnmanagedFds;
  if (env->hide_console_windows())
    worker->environment_flags_ |= EnvironmentFlags::kHideConsoleWindows;
  if (env->no_native_addons())
    worker->environment_flags_ |= EnvironmentFlags::kNoNativeAddons;
  if (env->no_global_search_paths())
    worker->environment_flags_ |= EnvironmentFlags::kNoGlobalSearchPaths;
  if (env->no_browser_globals())
    worker->environment_flags_ |= EnvironmentFlags::kNoBrowserGlobals;
}

void Worker::StartThread(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());
  Mutex::ScopedLock lock(w->mutex_);

  w->stopped_ = false;

  if (w->resource_limits_[kStackSizeMb] > 0) {
    if (w->resource_limits_[kStackSizeMb] * kMB < kStackBufferSize) {
      w->resource_limits_[kStackSizeMb] = kStackBufferSize / kMB;
      w->stack_size_ = kStackBufferSize;
    } else {
      w->stack_size_ =
          static_cast<size_t>(w->resource_limits_[kStackSizeMb] * kMB);
    }
  } else {
    w->resource_limits_[kStackSizeMb] = w->stack_size_ / kMB;
  }

  uv_thread_options_t thread_options;
  thread_options.flags = UV_THREAD_HAS_STACK_SIZE;
  thread_options.stack_size = w->stack_size_;

  uv_thread_t* tid = &w->tid_.emplace();  // Create uv_thread_t instance
  int ret = uv_thread_create_ex(tid, &thread_options, [](void* arg) {
    // XXX: This could become a std::unique_ptr, but that makes at least
    // gcc 6.3 detect undefined behaviour when there shouldn't be any.
    // gcc 7+ handles this well.
    Worker* w = static_cast<Worker*>(arg);
    const uintptr_t stack_top = reinterpret_cast<uintptr_t>(&arg);

    // Leave a few kilobytes just to make sure we're within limits and have
    // some space to do work in C++ land.
    w->stack_base_ = stack_top - (w->stack_size_ - kStackBufferSize);

    w->Run();

    Mutex::ScopedLock lock(w->mutex_);
    w->env()->SetImmediateThreadsafe(
        [w = std::unique_ptr<Worker>(w)](Environment* env) {
          if (w->has_ref_)
            env->add_refs(-1);
          w->JoinThread();
          // implicitly delete w
        });
  }, static_cast<void*>(w));

  if (ret == 0) {
    // The object now owns the created thread and should not be garbage
    // collected until that finishes.
    w->ClearWeak();

    if (w->has_ref_)
      w->env()->add_refs(1);

    w->env()->add_sub_worker_context(w);
  } else {
    w->stopped_ = true;
    w->tid_.reset();

    char err_buf[128];
    uv_err_name_r(ret, err_buf, sizeof(err_buf));
    {
      Isolate* isolate = w->env()->isolate();
      HandleScope handle_scope(isolate);
      THROW_ERR_WORKER_INIT_FAILED(isolate, err_buf);
    }
  }
}

void Worker::StopThread(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());

  Debug(w, "Worker %llu is getting stopped by parent", w->thread_id_.id);
  w->Exit(ExitCode::kGenericUserError);
}

void Worker::Ref(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());
  if (!w->has_ref_ && w->tid_.has_value()) {
    w->has_ref_ = true;
    w->env()->add_refs(1);
  }
}

void Worker::HasRef(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());
  args.GetReturnValue().Set(w->has_ref_);
}

void Worker::Unref(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());
  if (w->has_ref_ && w->tid_.has_value()) {
    w->has_ref_ = false;
    w->env()->add_refs(-1);
  }
}

void Worker::GetResourceLimits(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());
  args.GetReturnValue().Set(w->GetResourceLimits(args.GetIsolate()));
}

Local<Float64Array> Worker::GetResourceLimits(Isolate* isolate) const {
  Local<ArrayBuffer> ab = ArrayBuffer::New(isolate, sizeof(resource_limits_));

  memcpy(ab->Data(), resource_limits_, sizeof(resource_limits_));
  return Float64Array::New(ab, 0, kTotalResourceLimitCount);
}

void Worker::Exit(ExitCode code,
                  const char* error_code,
                  const char* error_message) {
  Mutex::ScopedLock lock(mutex_);
  Debug(this,
        "Worker %llu called Exit(%d, %s, %s)",
        thread_id_.id,
        static_cast<int>(code),
        error_code,
        error_message);

  if (error_code != nullptr) {
    custom_error_ = error_code;
    custom_error_str_ = error_message;
  }

  if (env_ != nullptr) {
    exit_code_ = code;
    Stop(env_);
  } else {
    stopped_ = true;
  }
}

bool Worker::IsNotIndicativeOfMemoryLeakAtExit() const {
  // Worker objects always stay alive as long as the child thread, regardless
  // of whether they are being referenced in the parent thread.
  return true;
}

class WorkerHeapSnapshotTaker : public AsyncWrap {
 public:
  WorkerHeapSnapshotTaker(Environment* env, Local<Object> obj)
    : AsyncWrap(env, obj, AsyncWrap::PROVIDER_WORKERHEAPSNAPSHOT) {}

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(WorkerHeapSnapshotTaker)
  SET_SELF_SIZE(WorkerHeapSnapshotTaker)
};

void Worker::TakeHeapSnapshot(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());
  CHECK_EQ(args.Length(), 1);
  auto options = heap::GetHeapSnapshotOptions(args[0]);

  Debug(w, "Worker %llu taking heap snapshot", w->thread_id_.id);

  Environment* env = w->env();
  AsyncHooks::DefaultTriggerAsyncIdScope trigger_id_scope(w);
  Local<Object> wrap;
  if (!env->worker_heap_snapshot_taker_template()
      ->NewInstance(env->context()).ToLocal(&wrap)) {
    return;
  }

  // The created WorkerHeapSnapshotTaker is an object owned by main
  // thread's Isolate, it can not be accessed by worker thread
  std::unique_ptr<BaseObjectPtr<WorkerHeapSnapshotTaker>> taker =
      std::make_unique<BaseObjectPtr<WorkerHeapSnapshotTaker>>(
          MakeDetachedBaseObject<WorkerHeapSnapshotTaker>(env, wrap));

  // Interrupt the worker thread and take a snapshot, then schedule a call
  // on the parent thread that turns that snapshot into a readable stream.
  bool scheduled = w->RequestInterrupt([taker = std::move(taker), env, options](
                                           Environment* worker_env) mutable {
    heap::HeapSnapshotPointer snapshot{
        worker_env->isolate()->GetHeapProfiler()->TakeHeapSnapshot(options)};
    CHECK(snapshot);

    // Here, the worker thread temporarily owns the WorkerHeapSnapshotTaker
    // object.

    env->SetImmediateThreadsafe(
        [taker = std::move(taker),
         snapshot = std::move(snapshot)](Environment* env) mutable {
          HandleScope handle_scope(env->isolate());
          Context::Scope context_scope(env->context());

          AsyncHooks::DefaultTriggerAsyncIdScope trigger_id_scope(taker->get());
          BaseObjectPtr<AsyncWrap> stream =
              heap::CreateHeapSnapshotStream(env, std::move(snapshot));
          Local<Value> args[] = {stream->object()};
          taker->get()->MakeCallback(
              env->ondone_string(), arraysize(args), args);
          // implicitly delete `taker`
        },
        CallbackFlags::kUnrefed);

    // Now, the lambda is delivered to the main thread, as a result, the
    // WorkerHeapSnapshotTaker object is delivered to the main thread, too.
  });

  if (scheduled) {
    args.GetReturnValue().Set(wrap);
  } else {
    args.GetReturnValue().Set(Local<Object>());
  }
}

void Worker::LoopIdleTime(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());

  Mutex::ScopedLock lock(w->mutex_);
  // Using w->is_stopped() here leads to a deadlock, and checking is_stopped()
  // before locking the mutex is a race condition. So manually do the same
  // check.
  if (w->stopped_ || w->env_ == nullptr)
    return args.GetReturnValue().Set(-1);

  uint64_t idle_time = uv_metrics_idle_time(w->env_->event_loop());
  args.GetReturnValue().Set(1.0 * idle_time / 1e6);
}

void Worker::LoopStartTime(const FunctionCallbackInfo<Value>& args) {
  Worker* w;
  ASSIGN_OR_RETURN_UNWRAP(&w, args.This());

  Mutex::ScopedLock lock(w->mutex_);
  // Using w->is_stopped() here leads to a deadlock, and checking is_stopped()
  // before locking the mutex is a race condition. So manually do the same
  // check.
  if (w->stopped_ || w->env_ == nullptr)
    return args.GetReturnValue().Set(-1);

  double loop_start_time = w->env_->performance_state()->milestones[
      node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_START];
  CHECK_GE(loop_start_time, 0);
  args.GetReturnValue().Set(loop_start_time / 1e6);
}

namespace {

// Return the MessagePort that is global for this Environment and communicates
// with the internal [kPort] port of the JS Worker class in the parent thread.
void GetEnvMessagePort(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Object> port = env->message_port();
  CHECK_IMPLIES(!env->is_main_thread(), !port.IsEmpty());
  if (!port.IsEmpty()) {
    CHECK_EQ(port->GetCreationContextChecked()->GetIsolate(),
             args.GetIsolate());
    args.GetReturnValue().Set(port);
  }
}

void CreateWorkerPerIsolateProperties(IsolateData* isolate_data,
                                      Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  {
    Local<FunctionTemplate> w = NewFunctionTemplate(isolate, Worker::New);

    w->InstanceTemplate()->SetInternalFieldCount(
        Worker::kInternalFieldCount);
    w->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));

    SetProtoMethod(isolate, w, "startThread", Worker::StartThread);
    SetProtoMethod(isolate, w, "stopThread", Worker::StopThread);
    SetProtoMethod(isolate, w, "hasRef", Worker::HasRef);
    SetProtoMethod(isolate, w, "ref", Worker::Ref);
    SetProtoMethod(isolate, w, "unref", Worker::Unref);
    SetProtoMethod(isolate, w, "getResourceLimits", Worker::GetResourceLimits);
    SetProtoMethod(isolate, w, "takeHeapSnapshot", Worker::TakeHeapSnapshot);
    SetProtoMethod(isolate, w, "loopIdleTime", Worker::LoopIdleTime);
    SetProtoMethod(isolate, w, "loopStartTime", Worker::LoopStartTime);

    SetConstructorFunction(isolate, target, "Worker", w);
  }

  {
    Local<FunctionTemplate> wst = NewFunctionTemplate(isolate, nullptr);

    wst->InstanceTemplate()->SetInternalFieldCount(
        WorkerHeapSnapshotTaker::kInternalFieldCount);
    wst->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));

    Local<String> wst_string =
        FIXED_ONE_BYTE_STRING(isolate, "WorkerHeapSnapshotTaker");
    wst->SetClassName(wst_string);
    isolate_data->set_worker_heap_snapshot_taker_template(
        wst->InstanceTemplate());
  }

  SetMethod(isolate, target, "getEnvMessagePort", GetEnvMessagePort);
}

void CreateWorkerPerContextProperties(Local<Object> target,
                                      Local<Value> unused,
                                      Local<Context> context,
                                      void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  target
      ->Set(env->context(),
            env->thread_id_string(),
            Number::New(isolate, static_cast<double>(env->thread_id())))
      .Check();

  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "isMainThread"),
            Boolean::New(isolate, env->is_main_thread()))
      .Check();

  Worker* worker = env->isolate_data()->worker_context();
  bool is_internal = worker != nullptr && worker->is_internal();

  // Set the is_internal property
  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "isInternalThread"),
            Boolean::New(isolate, is_internal))
      .Check();

  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "ownsProcessState"),
            Boolean::New(isolate, env->owns_process_state()))
      .Check();

  if (!env->is_main_thread()) {
    target
        ->Set(env->context(),
              FIXED_ONE_BYTE_STRING(isolate, "resourceLimits"),
              env->worker_context()->GetResourceLimits(isolate))
        .Check();
  }

  NODE_DEFINE_CONSTANT(target, kMaxYoungGenerationSizeMb);
  NODE_DEFINE_CONSTANT(target, kMaxOldGenerationSizeMb);
  NODE_DEFINE_CONSTANT(target, kCodeRangeSizeMb);
  NODE_DEFINE_CONSTANT(target, kStackSizeMb);
  NODE_DEFINE_CONSTANT(target, kTotalResourceLimitCount);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(GetEnvMessagePort);
  registry->Register(Worker::New);
  registry->Register(Worker::StartThread);
  registry->Register(Worker::StopThread);
  registry->Register(Worker::HasRef);
  registry->Register(Worker::Ref);
  registry->Register(Worker::Unref);
  registry->Register(Worker::GetResourceLimits);
  registry->Register(Worker::TakeHeapSnapshot);
  registry->Register(Worker::LoopIdleTime);
  registry->Register(Worker::LoopStartTime);
}

}  // anonymous namespace
}  // namespace worker
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(
    worker, node::worker::CreateWorkerPerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(worker,
                              node::worker::CreateWorkerPerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(worker,
                                node::worker::RegisterExternalReferences)
