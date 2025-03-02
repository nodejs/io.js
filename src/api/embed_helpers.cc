#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node.h"
#include "node_snapshot_builder.h"

using v8::Context;
using v8::Function;
using v8::Global;
using v8::HandleScope;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Locker;
using v8::Maybe;
using v8::Nothing;
using v8::SealHandleScope;
using v8::SnapshotCreator;
using v8::TryCatch;

namespace node {

Maybe<ExitCode> SpinEventLoopInternal(Environment* env) {
  CHECK_NOT_NULL(env);
  MultiIsolatePlatform* platform = GetMultiIsolatePlatform(env);
  CHECK_NOT_NULL(platform);

  Isolate* isolate = env->isolate();
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(env->context());
  SealHandleScope seal(isolate);

  if (env->is_stopping()) return Nothing<ExitCode>();

  env->set_trace_sync_io(env->options()->trace_sync_io);
  {
    bool more;
    env->performance_state()->Mark(
        node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_START);
    do {
      if (env->is_stopping()) break;
      uv_run(env->event_loop(), UV_RUN_DEFAULT);
      if (env->is_stopping()) break;

      platform->DrainTasks(isolate);

      more = uv_loop_alive(env->event_loop());
      if (more && !env->is_stopping()) continue;

      if (EmitProcessBeforeExit(env).IsNothing())
        break;

      {
        HandleScope handle_scope(isolate);
        if (env->RunSnapshotSerializeCallback().IsEmpty()) {
          break;
        }
      }

      // Emit `beforeExit` if the loop became alive either after emitting
      // event, or after running some callbacks.
      more = uv_loop_alive(env->event_loop());
    } while (more == true && !env->is_stopping());
    env->performance_state()->Mark(
        node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_EXIT);
  }
  if (env->is_stopping()) return Nothing<ExitCode>();

  env->set_trace_sync_io(false);
  // Clear the serialize callback even though the JS-land queue should
  // be empty this point so that the deserialized instance won't
  // attempt to call into JS again.
  env->set_snapshot_serialize_callback(Local<Function>());

  env->PrintInfoForSnapshotIfDebug();
  env->ForEachRealm([](Realm* realm) { realm->VerifyNoStrongBaseObjects(); });
  return EmitProcessExitInternal(env);
}

struct CommonEnvironmentSetup::Impl {
  MultiIsolatePlatform* platform = nullptr;
  uv_loop_t loop;
  std::shared_ptr<ArrayBufferAllocator> allocator;
  std::optional<SnapshotCreator> snapshot_creator;
  Isolate* isolate = nullptr;
  DeleteFnPtr<IsolateData, FreeIsolateData> isolate_data;
  DeleteFnPtr<Environment, FreeEnvironment> env;
  Global<Context> main_context;
};

CommonEnvironmentSetup::CommonEnvironmentSetup(
    MultiIsolatePlatform* platform,
    std::vector<std::string>* errors,
    const EmbedderSnapshotData* snapshot_data,
    uint32_t flags,
    std::function<Environment*(const CommonEnvironmentSetup*)> make_env,
    const SnapshotConfig* snapshot_config)
    : impl_(new Impl()) {
  CHECK_NOT_NULL(platform);
  CHECK_NOT_NULL(errors);

  impl_->platform = platform;
  uv_loop_t* loop = &impl_->loop;
  // Use `data` to tell the destructor whether the loop was initialized or not.
  loop->data = nullptr;
  int ret = uv_loop_init(loop);
  if (ret != 0) {
    errors->push_back(
        SPrintF("Failed to initialize loop: %s", uv_err_name(ret)));
    return;
  }
  loop->data = this;

  Isolate* isolate;
  if (flags & Flags::kIsForSnapshotting) {
    const std::vector<intptr_t>& external_references =
        SnapshotBuilder::CollectExternalReferences();
    isolate = impl_->isolate = Isolate::Allocate();
    // Must be done before the SnapshotCreator creation so  that the
    // memory reducer can be initialized.
    platform->RegisterIsolate(isolate, loop);
    impl_->snapshot_creator.emplace(isolate, external_references.data());
    isolate->SetCaptureStackTraceForUncaughtExceptions(
        true, 10, v8::StackTrace::StackTraceOptions::kDetailed);
    SetIsolateMiscHandlers(isolate, {});
  } else {
    impl_->allocator = ArrayBufferAllocator::Create();
    isolate = impl_->isolate =
        NewIsolate(impl_->allocator, &impl_->loop, platform, snapshot_data);
  }

  {
    Locker locker(isolate);
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);

    TryCatch bootstrapCatch(isolate);
    auto print_Exception = OnScopeLeave([&]() {
      if (bootstrapCatch.HasCaught()) {
        errors->push_back(FormatCaughtException(
            isolate, isolate->GetCurrentContext(), bootstrapCatch));
      }
    });

    impl_->isolate_data.reset(CreateIsolateData(
        isolate, loop, platform, impl_->allocator.get(), snapshot_data));
    impl_->isolate_data->set_snapshot_config(snapshot_config);

    if (snapshot_data) {
      impl_->env.reset(make_env(this));
      if (impl_->env) {
        impl_->main_context.Reset(isolate, impl_->env->context());
      }
      return;
    }

    Local<Context> context = NewContext(isolate);
    impl_->main_context.Reset(isolate, context);
    if (context.IsEmpty()) {
      errors->push_back("Failed to initialize V8 Context");
      return;
    }

    Context::Scope context_scope(context);
    impl_->env.reset(make_env(this));
  }
}

CommonEnvironmentSetup::CommonEnvironmentSetup(
    MultiIsolatePlatform* platform,
    std::vector<std::string>* errors,
    std::function<Environment*(const CommonEnvironmentSetup*)> make_env)
    : CommonEnvironmentSetup(platform, errors, nullptr, false, make_env) {}

std::unique_ptr<CommonEnvironmentSetup>
CommonEnvironmentSetup::CreateForSnapshotting(
    MultiIsolatePlatform* platform,
    std::vector<std::string>* errors,
    const std::vector<std::string>& args,
    const std::vector<std::string>& exec_args,
    const SnapshotConfig& snapshot_config) {
  // It's not guaranteed that a context that goes through
  // v8_inspector::V8Inspector::contextCreated() is runtime-independent,
  // so do not start the inspector on the main context when building
  // the default snapshot.
  uint64_t env_flags =
      EnvironmentFlags::kDefaultFlags | EnvironmentFlags::kNoCreateInspector;

  auto ret = std::unique_ptr<CommonEnvironmentSetup>(new CommonEnvironmentSetup(
      platform,
      errors,
      nullptr,
      true,
      [&](const CommonEnvironmentSetup* setup) -> Environment* {
        return CreateEnvironment(
            setup->isolate_data(),
            setup->context(),
            args,
            exec_args,
            static_cast<EnvironmentFlags::Flags>(env_flags));
      },
      &snapshot_config));
  if (!errors->empty()) ret.reset();
  return ret;
}

CommonEnvironmentSetup::~CommonEnvironmentSetup() {
  if (impl_->isolate != nullptr) {
    Isolate* isolate = impl_->isolate;
    {
      Locker locker(isolate);
      Isolate::Scope isolate_scope(isolate);

      impl_->main_context.Reset();
      impl_->env.reset();
      impl_->isolate_data.reset();
    }

    bool platform_finished = false;
    impl_->platform->AddIsolateFinishedCallback(isolate, [](void* data) {
      *static_cast<bool*>(data) = true;
    }, &platform_finished);
    impl_->platform->UnregisterIsolate(isolate);
    if (impl_->snapshot_creator.has_value())
      impl_->snapshot_creator.reset();
    else
      isolate->Dispose();

    // Wait until the platform has cleaned up all relevant resources.
    while (!platform_finished)
      uv_run(&impl_->loop, UV_RUN_ONCE);
  }

  if (impl_->isolate || impl_->loop.data != nullptr)
    CheckedUvLoopClose(&impl_->loop);

  delete impl_;
}

EmbedderSnapshotData::Pointer CommonEnvironmentSetup::CreateSnapshot() {
  CHECK_NOT_NULL(snapshot_creator());
  SnapshotData* snapshot_data = new SnapshotData();
  EmbedderSnapshotData::Pointer result{
      new EmbedderSnapshotData(snapshot_data, true)};

  auto exit_code = SnapshotBuilder::CreateSnapshot(snapshot_data, this);
  if (exit_code != ExitCode::kNoFailure) return {};

  return result;
}

Maybe<int> SpinEventLoop(Environment* env) {
  Maybe<ExitCode> result = SpinEventLoopInternal(env);
  if (result.IsNothing()) {
    return Nothing<int>();
  }
  return Just(static_cast<int>(result.FromJust()));
}

uv_loop_t* CommonEnvironmentSetup::event_loop() const {
  return &impl_->loop;
}

std::shared_ptr<ArrayBufferAllocator>
CommonEnvironmentSetup::array_buffer_allocator() const {
  return impl_->allocator;
}

Isolate* CommonEnvironmentSetup::isolate() const {
  return impl_->isolate;
}

IsolateData* CommonEnvironmentSetup::isolate_data() const {
  return impl_->isolate_data.get();
}

Environment* CommonEnvironmentSetup::env() const {
  return impl_->env.get();
}

v8::Local<v8::Context> CommonEnvironmentSetup::context() const {
  return impl_->main_context.Get(impl_->isolate);
}

v8::SnapshotCreator* CommonEnvironmentSetup::snapshot_creator() {
  return impl_->snapshot_creator ? &impl_->snapshot_creator.value() : nullptr;
}

void EmbedderSnapshotData::DeleteSnapshotData::operator()(
    const EmbedderSnapshotData* data) const {
  CHECK_IMPLIES(data->owns_impl_, data->impl_);
  if (data->owns_impl_ &&
      data->impl_->data_ownership == SnapshotData::DataOwnership::kOwned) {
    delete data->impl_;
  }
  delete data;
}

EmbedderSnapshotData::Pointer EmbedderSnapshotData::BuiltinSnapshotData() {
  return EmbedderSnapshotData::Pointer{new EmbedderSnapshotData(
      SnapshotBuilder::GetEmbeddedSnapshotData(), false)};
}

EmbedderSnapshotData::Pointer EmbedderSnapshotData::FromBlob(
    const std::vector<char>& in) {
  return FromBlob(std::string_view(in.data(), in.size()));
}

EmbedderSnapshotData::Pointer EmbedderSnapshotData::FromBlob(
    std::string_view in) {
  SnapshotData* snapshot_data = new SnapshotData();
  CHECK_EQ(snapshot_data->data_ownership, SnapshotData::DataOwnership::kOwned);
  EmbedderSnapshotData::Pointer result{
      new EmbedderSnapshotData(snapshot_data, true)};
  if (!SnapshotData::FromBlob(snapshot_data, in)) {
    return {};
  }
  return result;
}

EmbedderSnapshotData::Pointer EmbedderSnapshotData::FromFile(FILE* in) {
  return FromBlob(ReadFileSync(in));
}

std::vector<char> EmbedderSnapshotData::ToBlob() const {
  return impl_->ToBlob();
}

void EmbedderSnapshotData::ToFile(FILE* out) const {
  impl_->ToFile(out);
}

EmbedderSnapshotData::EmbedderSnapshotData(const SnapshotData* impl,
                                           bool owns_impl)
    : impl_(impl), owns_impl_(owns_impl) {}

bool EmbedderSnapshotData::CanUseCustomSnapshotPerIsolate() {
#ifdef NODE_V8_SHARED_RO_HEAP
  return false;
#else
  return true;
#endif
}

}  // namespace node
