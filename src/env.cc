#include "env.h"
#include "async_wrap.h"
#include "base_object-inl.h"
#include "debug_utils-inl.h"
#include "diagnosticfilename-inl.h"
#include "memory_tracker-inl.h"
#include "module_wrap.h"
#include "node_buffer.h"
#include "node_context_data.h"
#include "node_contextify.h"
#include "node_errors.h"
#include "node_internals.h"
#include "node_options-inl.h"
#include "node_process-inl.h"
#include "node_shadow_realm.h"
#include "node_snapshotable.h"
#include "node_v8_platform-inl.h"
#include "node_worker.h"
#include "req_wrap-inl.h"
#include "stream_base.h"
#include "tracing/agent.h"
#include "tracing/traced_value.h"
#include "util-inl.h"
#include "v8-cppgc.h"
#include "v8-profiler.h"
#include "v8-sandbox.h"  // v8::Object::Wrap(), v8::Object::Unwrap()

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>

namespace node {

using errors::TryCatchScope;
using v8::Array;
using v8::Boolean;
using v8::Context;
using v8::CppHeap;
using v8::CppHeapCreateParams;
using v8::EmbedderGraph;
using v8::EscapableHandleScope;
using v8::Function;
using v8::HandleScope;
using v8::HeapProfiler;
using v8::HeapSpaceStatistics;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::Private;
using v8::Promise;
using v8::PromiseHookType;
using v8::Script;
using v8::SnapshotCreator;
using v8::StackTrace;
using v8::String;
using v8::Symbol;
using v8::TracingController;
using v8::TryCatch;
using v8::Uint32;
using v8::Undefined;
using v8::Value;
using worker::Worker;

int const ContextEmbedderTag::kNodeContextTag = 0x6e6f64;
void* const ContextEmbedderTag::kNodeContextTagPtr = const_cast<void*>(
    static_cast<const void*>(&ContextEmbedderTag::kNodeContextTag));

void AsyncHooks::ResetPromiseHooks(Local<Function> init,
                                   Local<Function> before,
                                   Local<Function> after,
                                   Local<Function> resolve) {
  js_promise_hooks_[0].Reset(env()->isolate(), init);
  js_promise_hooks_[1].Reset(env()->isolate(), before);
  js_promise_hooks_[2].Reset(env()->isolate(), after);
  js_promise_hooks_[3].Reset(env()->isolate(), resolve);
}

Local<Array> AsyncHooks::GetPromiseHooks(Isolate* isolate) const {
  v8::LocalVector<Value> values(isolate, js_promise_hooks_.size());
  for (size_t i = 0; i < js_promise_hooks_.size(); ++i) {
    if (js_promise_hooks_[i].IsEmpty()) {
      values[i] = Undefined(isolate);
    } else {
      values[i] = js_promise_hooks_[i].Get(isolate);
    }
  }
  return Array::New(isolate, values.data(), values.size());
}

void Environment::ResetPromiseHooks(Local<Function> init,
                                    Local<Function> before,
                                    Local<Function> after,
                                    Local<Function> resolve) {
  async_hooks()->ResetPromiseHooks(init, before, after, resolve);

  for (auto it = contexts_.begin(); it != contexts_.end(); it++) {
    if (it->IsEmpty()) {
      contexts_.erase(it--);
      continue;
    }
    PersistentToLocal::Weak(isolate_, *it)
        ->SetPromiseHooks(init, before, after, resolve);
  }
}

// Remember to keep this code aligned with pushAsyncContext() in JS.
void AsyncHooks::push_async_context(double async_id,
                                    double trigger_async_id,
                                    Local<Object> resource) {
  // Since async_hooks is experimental, do only perform the check
  // when async_hooks is enabled.
  if (fields_[kCheck] > 0) {
    CHECK_GE(async_id, -1);
    CHECK_GE(trigger_async_id, -1);
  }

  uint32_t offset = fields_[kStackLength];
  if (offset * 2 >= async_ids_stack_.Length()) grow_async_ids_stack();
  async_ids_stack_[2 * offset] = async_id_fields_[kExecutionAsyncId];
  async_ids_stack_[2 * offset + 1] = async_id_fields_[kTriggerAsyncId];
  fields_[kStackLength] += 1;
  async_id_fields_[kExecutionAsyncId] = async_id;
  async_id_fields_[kTriggerAsyncId] = trigger_async_id;

#ifdef DEBUG
  for (uint32_t i = offset; i < native_execution_async_resources_.size(); i++)
    CHECK(native_execution_async_resources_[i].IsEmpty());
#endif

  // When this call comes from JS (as a way of increasing the stack size),
  // `resource` will be empty, because JS caches these values anyway.
  if (!resource.IsEmpty()) {
    native_execution_async_resources_.resize(offset + 1);
    // Caveat: This is a v8::Local<> assignment, we do not keep a v8::Global<>!
    native_execution_async_resources_[offset] = resource;
  }
}

// Remember to keep this code aligned with popAsyncContext() in JS.
bool AsyncHooks::pop_async_context(double async_id) {
  // In case of an exception then this may have already been reset, if the
  // stack was multiple MakeCallback()'s deep.
  if (fields_[kStackLength] == 0) [[unlikely]]
    return false;

  // Ask for the async_id to be restored as a check that the stack
  // hasn't been corrupted.
  if (fields_[kCheck] > 0 && async_id_fields_[kExecutionAsyncId] != async_id)
      [[unlikely]] {
    FailWithCorruptedAsyncStack(async_id);
  }

  uint32_t offset = fields_[kStackLength] - 1;
  async_id_fields_[kExecutionAsyncId] = async_ids_stack_[2 * offset];
  async_id_fields_[kTriggerAsyncId] = async_ids_stack_[2 * offset + 1];
  fields_[kStackLength] = offset;

  if (offset < native_execution_async_resources_.size() &&
      !native_execution_async_resources_[offset].IsEmpty()) [[likely]] {
#ifdef DEBUG
    for (uint32_t i = offset + 1; i < native_execution_async_resources_.size();
         i++) {
      CHECK(native_execution_async_resources_[i].IsEmpty());
    }
#endif
    native_execution_async_resources_.resize(offset);
    native_execution_async_resources_.shrink_to_fit();
  }

  if (js_execution_async_resources()->Length() > offset) [[unlikely]] {
    HandleScope handle_scope(env()->isolate());
    USE(js_execution_async_resources()->Set(
        env()->context(),
        env()->length_string(),
        Integer::NewFromUnsigned(env()->isolate(), offset)));
  }

  return fields_[kStackLength] > 0;
}

void AsyncHooks::clear_async_id_stack() {
  if (!js_execution_async_resources_.IsEmpty() && env()->can_call_into_js()) {
    Isolate* isolate = env()->isolate();
    HandleScope handle_scope(isolate);
    USE(PersistentToLocal::Strong(js_execution_async_resources_)
            ->Set(env()->context(),
                  env()->length_string(),
                  Integer::NewFromUnsigned(isolate, 0)));
  }

  native_execution_async_resources_.clear();
  native_execution_async_resources_.shrink_to_fit();

  async_id_fields_[kExecutionAsyncId] = 0;
  async_id_fields_[kTriggerAsyncId] = 0;
  fields_[kStackLength] = 0;
}

void AsyncHooks::InstallPromiseHooks(Local<Context> ctx) {
  ctx->SetPromiseHooks(js_promise_hooks_[0].IsEmpty()
                           ? Local<Function>()
                           : PersistentToLocal::Strong(js_promise_hooks_[0]),
                       js_promise_hooks_[1].IsEmpty()
                           ? Local<Function>()
                           : PersistentToLocal::Strong(js_promise_hooks_[1]),
                       js_promise_hooks_[2].IsEmpty()
                           ? Local<Function>()
                           : PersistentToLocal::Strong(js_promise_hooks_[2]),
                       js_promise_hooks_[3].IsEmpty()
                           ? Local<Function>()
                           : PersistentToLocal::Strong(js_promise_hooks_[3]));
}

void Environment::PurgeTrackedEmptyContexts() {
  std::erase_if(contexts_, [&](auto&& el) { return el.IsEmpty(); });
}

void Environment::TrackContext(Local<Context> context) {
  PurgeTrackedEmptyContexts();
  size_t id = contexts_.size();
  contexts_.resize(id + 1);
  contexts_[id].Reset(isolate_, context);
  contexts_[id].SetWeak();
}

void Environment::UntrackContext(Local<Context> context) {
  HandleScope handle_scope(isolate_);
  PurgeTrackedEmptyContexts();
  for (auto it = contexts_.begin(); it != contexts_.end(); it++) {
    if (Local<Context> saved_context = PersistentToLocal::Weak(isolate_, *it);
        saved_context == context) {
      it->Reset();
      contexts_.erase(it);
      break;
    }
  }
}

void Environment::TrackShadowRealm(shadow_realm::ShadowRealm* realm) {
  shadow_realms_.insert(realm);
}

void Environment::UntrackShadowRealm(shadow_realm::ShadowRealm* realm) {
  shadow_realms_.erase(realm);
}

AsyncHooks::DefaultTriggerAsyncIdScope::DefaultTriggerAsyncIdScope(
    Environment* env, double default_trigger_async_id)
    : async_hooks_(env->async_hooks()) {
  if (env->async_hooks()->fields()[AsyncHooks::kCheck] > 0) {
    CHECK_GE(default_trigger_async_id, 0);
  }

  old_default_trigger_async_id_ =
      async_hooks_->async_id_fields()[AsyncHooks::kDefaultTriggerAsyncId];
  async_hooks_->async_id_fields()[AsyncHooks::kDefaultTriggerAsyncId] =
      default_trigger_async_id;
}

AsyncHooks::DefaultTriggerAsyncIdScope::~DefaultTriggerAsyncIdScope() {
  async_hooks_->async_id_fields()[AsyncHooks::kDefaultTriggerAsyncId] =
      old_default_trigger_async_id_;
}

AsyncHooks::DefaultTriggerAsyncIdScope::DefaultTriggerAsyncIdScope(
    AsyncWrap* async_wrap)
    : DefaultTriggerAsyncIdScope(async_wrap->env(),
                                 async_wrap->get_async_id()) {}

std::ostream& operator<<(std::ostream& output,
                         const std::vector<SnapshotIndex>& v) {
  output << "{ ";
  for (const SnapshotIndex i : v) {
    output << i << ", ";
  }
  output << " }";
  return output;
}

std::ostream& operator<<(std::ostream& output,
                         const IsolateDataSerializeInfo& i) {
  output << "{\n"
         << "// -- primitive begins --\n"
         << i.primitive_values << ",\n"
         << "// -- primitive ends --\n"
         << "// -- template_values begins --\n"
         << i.template_values << ",\n"
         << "// -- template_values ends --\n"
         << "}";
  return output;
}

std::ostream& operator<<(std::ostream& output, const SnapshotFlags& flags) {
  output << "static_cast<SnapshotFlags>(" << static_cast<uint32_t>(flags)
         << ")";
  return output;
}

std::ostream& operator<<(std::ostream& output, const SnapshotMetadata& i) {
  output << "{\n"
         << "  "
         << (i.type == SnapshotMetadata::Type::kDefault
                 ? "SnapshotMetadata::Type::kDefault"
                 : "SnapshotMetadata::Type::kFullyCustomized")
         << ", // type\n"
         << "  \"" << i.node_version << "\", // node_version\n"
         << "  \"" << i.node_arch << "\", // node_arch\n"
         << "  \"" << i.node_platform << "\", // node_platform\n"
         << "  " << i.flags << ", // flags\n"
         << "}";
  return output;
}

IsolateDataSerializeInfo IsolateData::Serialize(SnapshotCreator* creator) {
  Isolate* isolate = creator->GetIsolate();
  IsolateDataSerializeInfo info;
  HandleScope handle_scope(isolate);
  // XXX(joyeecheung): technically speaking, the indexes here should be
  // consecutive and we could just return a range instead of an array,
  // but that's not part of the V8 API contract so we use an array
  // just to be safe.

#define VP(PropertyName, StringValue) V(Private, PropertyName)
#define VY(PropertyName, StringValue) V(Symbol, PropertyName)
#define VS(PropertyName, StringValue) V(String, PropertyName)
#define VR(PropertyName, TypeName) V(Private, per_realm_##PropertyName)
#define V(TypeName, PropertyName)                                              \
  info.primitive_values.push_back(                                             \
      creator->AddData(PropertyName##_.Get(isolate)));
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_SYMBOL_PROPERTIES(VY)
  PER_ISOLATE_STRING_PROPERTIES(VS)
  PER_REALM_STRONG_PERSISTENT_VALUES(VR)
#undef V
#undef VR
#undef VY
#undef VS
#undef VP

  info.primitive_values.reserve(info.primitive_values.size() +
                                AsyncWrap::PROVIDERS_LENGTH);
  for (size_t i = 0; i < AsyncWrap::PROVIDERS_LENGTH; i++) {
    info.primitive_values.push_back(creator->AddData(async_wrap_provider(i)));
  }
  uint32_t id = 0;
#define VM(PropertyName) V(PropertyName##_binding_template, ObjectTemplate)
#define V(PropertyName, TypeName)                                              \
  do {                                                                         \
    Local<TypeName> field = PropertyName();                                    \
    if (!field.IsEmpty()) {                                                    \
      size_t index = creator->AddData(field);                                  \
      info.template_values.push_back({#PropertyName, id, index});              \
    }                                                                          \
    id++;                                                                      \
  } while (0);
  PER_ISOLATE_TEMPLATE_PROPERTIES(V)
  NODE_BINDINGS_WITH_PER_ISOLATE_INIT(VM)
#undef V

  return info;
}

void IsolateData::DeserializeProperties(const IsolateDataSerializeInfo* info) {
  size_t i = 0;

  Isolate::Scope isolate_scope(isolate_);
  HandleScope handle_scope(isolate_);

  if (per_process::enabled_debug_list.enabled(DebugCategory::MKSNAPSHOT)) {
    fprintf(stderr, "deserializing IsolateDataSerializeInfo...\n");
    std::cerr << *info << "\n";
  }

#define VP(PropertyName, StringValue) V(Private, PropertyName)
#define VY(PropertyName, StringValue) V(Symbol, PropertyName)
#define VS(PropertyName, StringValue) V(String, PropertyName)
#define VR(PropertyName, TypeName) V(Private, per_realm_##PropertyName)
#define V(TypeName, PropertyName)                                              \
  do {                                                                         \
    MaybeLocal<TypeName> maybe_field =                                         \
        isolate_->GetDataFromSnapshotOnce<TypeName>(                           \
            info->primitive_values[i++]);                                      \
    Local<TypeName> field;                                                     \
    if (!maybe_field.ToLocal(&field)) {                                        \
      fprintf(stderr, "Failed to deserialize " #PropertyName "\n");            \
    }                                                                          \
    PropertyName##_.Set(isolate_, field);                                      \
  } while (0);
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_SYMBOL_PROPERTIES(VY)
  PER_ISOLATE_STRING_PROPERTIES(VS)
  PER_REALM_STRONG_PERSISTENT_VALUES(VR)
#undef V
#undef VR
#undef VY
#undef VS
#undef VP

  for (size_t j = 0; j < AsyncWrap::PROVIDERS_LENGTH; j++) {
    MaybeLocal<String> maybe_field =
        isolate_->GetDataFromSnapshotOnce<String>(info->primitive_values[i++]);
    Local<String> field;
    if (!maybe_field.ToLocal(&field)) {
      fprintf(stderr, "Failed to deserialize AsyncWrap provider %zu\n", j);
    }
    async_wrap_providers_[j].Set(isolate_, field);
  }

  const std::vector<PropInfo>& values = info->template_values;
  i = 0;  // index to the array
  uint32_t id = 0;
#define VM(PropertyName) V(PropertyName##_binding_template, ObjectTemplate)
#define V(PropertyName, TypeName)                                              \
  do {                                                                         \
    if (values.size() > i && id == values[i].id) {                             \
      const PropInfo& d = values[i];                                           \
      DCHECK_EQ(d.name, #PropertyName);                                        \
      MaybeLocal<TypeName> maybe_field =                                       \
          isolate_->GetDataFromSnapshotOnce<TypeName>(d.index);                \
      Local<TypeName> field;                                                   \
      if (!maybe_field.ToLocal(&field)) {                                      \
        fprintf(stderr,                                                        \
                "Failed to deserialize isolate data template " #PropertyName   \
                "\n");                                                         \
      }                                                                        \
      set_##PropertyName(field);                                               \
      i++;                                                                     \
    }                                                                          \
    id++;                                                                      \
  } while (0);

  PER_ISOLATE_TEMPLATE_PROPERTIES(V);
  NODE_BINDINGS_WITH_PER_ISOLATE_INIT(VM);
#undef V
}

void IsolateData::CreateProperties() {
  // Create string and private symbol properties as internalized one byte
  // strings after the platform is properly initialized.
  //
  // Internalized because it makes property lookups a little faster and
  // because the string is created in the old space straight away.  It's going
  // to end up in the old space sooner or later anyway but now it doesn't go
  // through v8::Eternal's new space handling first.
  //
  // One byte because our strings are ASCII and we can safely skip V8's UTF-8
  // decoding step.

  v8::Isolate::Scope isolate_scope(isolate_);
  HandleScope handle_scope(isolate_);

#define V(PropertyName, StringValue)                                           \
  PropertyName##_.Set(                                                         \
      isolate_,                                                                \
      Private::New(isolate_,                                                   \
                   String::NewFromOneByte(                                     \
                       isolate_,                                               \
                       reinterpret_cast<const uint8_t*>(StringValue),          \
                       NewStringType::kInternalized,                           \
                       sizeof(StringValue) - 1)                                \
                       .ToLocalChecked()));
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(V)
#undef V
#define V(PropertyName, TypeName)                                              \
  per_realm_##PropertyName##_.Set(                                             \
      isolate_,                                                                \
      Private::New(                                                            \
          isolate_,                                                            \
          String::NewFromOneByte(                                              \
              isolate_,                                                        \
              reinterpret_cast<const uint8_t*>("per_realm_" #PropertyName),    \
              NewStringType::kInternalized,                                    \
              sizeof("per_realm_" #PropertyName) - 1)                          \
              .ToLocalChecked()));
  PER_REALM_STRONG_PERSISTENT_VALUES(V)
#undef V
#define V(PropertyName, StringValue)                                           \
  PropertyName##_.Set(                                                         \
      isolate_,                                                                \
      Symbol::New(isolate_,                                                    \
                  String::NewFromOneByte(                                      \
                      isolate_,                                                \
                      reinterpret_cast<const uint8_t*>(StringValue),           \
                      NewStringType::kInternalized,                            \
                      sizeof(StringValue) - 1)                                 \
                      .ToLocalChecked()));
  PER_ISOLATE_SYMBOL_PROPERTIES(V)
#undef V
#define V(PropertyName, StringValue)                                           \
  PropertyName##_.Set(                                                         \
      isolate_,                                                                \
      String::NewFromOneByte(isolate_,                                         \
                             reinterpret_cast<const uint8_t*>(StringValue),    \
                             NewStringType::kInternalized,                     \
                             sizeof(StringValue) - 1)                          \
          .ToLocalChecked());
  PER_ISOLATE_STRING_PROPERTIES(V)
#undef V

  // Create all the provider strings that will be passed to JS. Place them in
  // an array so the array index matches the PROVIDER id offset. This way the
  // strings can be retrieved quickly.
#define V(Provider)                                                           \
  async_wrap_providers_[AsyncWrap::PROVIDER_ ## Provider].Set(                \
      isolate_,                                                               \
      String::NewFromOneByte(                                                 \
        isolate_,                                                             \
        reinterpret_cast<const uint8_t*>(#Provider),                          \
        NewStringType::kInternalized,                                         \
        sizeof(#Provider) - 1).ToLocalChecked());
  NODE_ASYNC_PROVIDER_TYPES(V)
#undef V

  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate());
  templ->SetInternalFieldCount(BaseObject::kInternalFieldCount);
  set_binding_data_default_template(templ);
  binding::CreateInternalBindingTemplates(this);

  contextify::ContextifyContext::InitializeGlobalTemplates(this);
  CreateEnvProxyTemplate(this);
}

// Previously, the general convention of the wrappable layout for cppgc in
// the ecosystem is:
// [  0  ] -> embedder id
// [  1  ] -> wrappable instance
// Now V8 has deprecated this layout-based tracing enablement, embedders
// should simply use v8::Object::Wrap() and v8::Object::Unwrap(). We preserve
// this layout only to distinguish internally how the memory of a Node.js
// wrapper is managed or whether a wrapper is managed by Node.js.
constexpr uint16_t kDefaultCppGCEmbedderID = 0x90de;
Mutex IsolateData::isolate_data_mutex_;
std::unordered_map<uint16_t, std::unique_ptr<PerIsolateWrapperData>>
    IsolateData::wrapper_data_map_;

IsolateData* IsolateData::CreateIsolateData(
    Isolate* isolate,
    uv_loop_t* loop,
    MultiIsolatePlatform* platform,
    ArrayBufferAllocator* allocator,
    const EmbedderSnapshotData* embedder_snapshot_data,
    std::shared_ptr<PerIsolateOptions> options) {
  const SnapshotData* snapshot_data =
      SnapshotData::FromEmbedderWrapper(embedder_snapshot_data);
  if (options == nullptr) {
    options = per_process::cli_options->per_isolate->Clone();
  }
  return new IsolateData(
      isolate, loop, platform, allocator, snapshot_data, options);
}

IsolateData::IsolateData(Isolate* isolate,
                         uv_loop_t* event_loop,
                         MultiIsolatePlatform* platform,
                         ArrayBufferAllocator* node_allocator,
                         const SnapshotData* snapshot_data,
                         std::shared_ptr<PerIsolateOptions> options)
    : isolate_(isolate),
      event_loop_(event_loop),
      node_allocator_(node_allocator == nullptr ? nullptr
                                                : node_allocator->GetImpl()),
      platform_(platform),
      snapshot_data_(snapshot_data),
      options_(std::move(options)) {
  v8::CppHeap* cpp_heap = isolate->GetCppHeap();

  uint16_t cppgc_id = kDefaultCppGCEmbedderID;
  // We do not care about overflow since we just want this to be different
  // from the cppgc id.
  uint16_t non_cppgc_id = cppgc_id + 1;
  if (cpp_heap == nullptr) {
    cpp_heap_ = CppHeap::Create(platform, v8::CppHeapCreateParams{{}});
    // TODO(joyeecheung): pass it into v8::Isolate::CreateParams and let V8
    // own it when we can keep the isolate registered/task runner discoverable
    // during isolate disposal.
    isolate->AttachCppHeap(cpp_heap_.get());
  }

  {
    // GC could still be run after the IsolateData is destroyed, so we store
    // the ids in a static map to ensure pointers to them are still valid
    // then. In practice there should be very few variants of the cppgc id
    // in one process so the size of this map should be very small.
    node::Mutex::ScopedLock lock(isolate_data_mutex_);
    auto it = wrapper_data_map_.find(cppgc_id);
    if (it == wrapper_data_map_.end()) {
      auto pair = wrapper_data_map_.emplace(
          cppgc_id, new PerIsolateWrapperData{cppgc_id, non_cppgc_id});
      it = pair.first;
    }
    wrapper_data_ = it->second.get();
  }

  if (snapshot_data == nullptr) {
    CreateProperties();
  } else {
    DeserializeProperties(&snapshot_data->isolate_data_info);
  }
}

IsolateData::~IsolateData() {
  if (cpp_heap_ != nullptr) {
    // The CppHeap must be detached before being terminated.
    isolate_->DetachCppHeap();
    cpp_heap_->Terminate();
  }
}

// Deprecated API, embedders should use v8::Object::Wrap() directly instead.
void SetCppgcReference(Isolate* isolate,
                       Local<Object> object,
                       void* wrappable) {
  v8::Object::Wrap<v8::CppHeapPointerTag::kDefaultTag>(
      isolate, object, wrappable);
}

void IsolateData::MemoryInfo(MemoryTracker* tracker) const {
#define V(PropertyName, StringValue)                                           \
  tracker->TrackField(#PropertyName, PropertyName());
  PER_ISOLATE_SYMBOL_PROPERTIES(V)

  PER_ISOLATE_STRING_PROPERTIES(V)
#undef V

  tracker->TrackField("async_wrap_providers", async_wrap_providers_);

  if (node_allocator_ != nullptr) {
    tracker->TrackFieldWithSize(
        "node_allocator", sizeof(*node_allocator_), "NodeArrayBufferAllocator");
  }
  tracker->TrackFieldWithSize(
      "platform", sizeof(*platform_), "MultiIsolatePlatform");
  // TODO(joyeecheung): implement MemoryRetainer in the option classes.
}

void TrackingTraceStateObserver::UpdateTraceCategoryState() {
  if (!env_->owns_process_state() || !env_->can_call_into_js()) {
    // Ideally, we’d have a consistent story that treats all threads/Environment
    // instances equally here. However, tracing is essentially global, and this
    // callback is called from whichever thread calls `StartTracing()` or
    // `StopTracing()`. The only way to do this in a threadsafe fashion
    // seems to be only tracking this from the main thread, and only allowing
    // these state modifications from the main thread.
    return;
  }

  if (env_->principal_realm() == nullptr) {
    return;
  }

  bool async_hooks_enabled = (*(TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(
                                 TRACING_CATEGORY_NODE1(async_hooks)))) != 0;

  Isolate* isolate = env_->isolate();
  HandleScope handle_scope(isolate);
  Local<Function> cb = env_->trace_category_state_function();
  if (cb.IsEmpty())
    return;
  TryCatchScope try_catch(env_);
  try_catch.SetVerbose(true);
  Local<Value> args[] = {Boolean::New(isolate, async_hooks_enabled)};
  USE(cb->Call(env_->context(), Undefined(isolate), arraysize(args), args));
}

void Environment::AssignToContext(Local<v8::Context> context,
                                  Realm* realm,
                                  const ContextInfo& info) {
  context->SetAlignedPointerInEmbedderData(ContextEmbedderIndex::kEnvironment,
                                           this);
  context->SetAlignedPointerInEmbedderData(ContextEmbedderIndex::kRealm, realm);

  // ContextifyContexts will update this to a pointer to the native object.
  context->SetAlignedPointerInEmbedderData(
      ContextEmbedderIndex::kContextifyContext, nullptr);

  // This must not be done before other context fields are initialized.
  ContextEmbedderTag::TagNodeContext(context);

#if HAVE_INSPECTOR
  inspector_agent()->ContextCreated(context, info);
#endif  // HAVE_INSPECTOR

  this->async_hooks()->InstallPromiseHooks(context);
  TrackContext(context);
}

void Environment::UnassignFromContext(Local<v8::Context> context) {
  if (!context.IsEmpty()) {
    context->SetAlignedPointerInEmbedderData(ContextEmbedderIndex::kEnvironment,
                                             nullptr);
    context->SetAlignedPointerInEmbedderData(ContextEmbedderIndex::kRealm,
                                             nullptr);
    context->SetAlignedPointerInEmbedderData(
        ContextEmbedderIndex::kContextifyContext, nullptr);
  }
  UntrackContext(context);
}

void Environment::TryLoadAddon(
    const char* filename,
    int flags,
    const std::function<bool(binding::DLib*)>& was_loaded) {
  loaded_addons_.emplace_back(filename, flags);
  if (!was_loaded(&loaded_addons_.back())) {
    loaded_addons_.pop_back();
  }
}

std::string Environment::GetCwd(const std::string& exec_path) {
  char cwd[PATH_MAX_BYTES];
  size_t size = PATH_MAX_BYTES;

  if (uv_cwd(cwd, &size) == 0) {
    CHECK_GT(size, 0);
    return cwd;
  }

  // This can fail if the cwd is deleted. In that case, fall back to
  // exec_path.
  return exec_path.substr(0, exec_path.find_last_of(kPathSeparator));
}

void Environment::add_refs(int64_t diff) {
  task_queues_async_refs_ += diff;
  CHECK_GE(task_queues_async_refs_, 0);
  if (task_queues_async_refs_ == 0)
    uv_unref(reinterpret_cast<uv_handle_t*>(&task_queues_async_));
  else
    uv_ref(reinterpret_cast<uv_handle_t*>(&task_queues_async_));
}

uv_buf_t Environment::allocate_managed_buffer(const size_t suggested_size) {
  NoArrayBufferZeroFillScope no_zero_fill_scope(isolate_data());
  std::unique_ptr<v8::BackingStore> bs =
      v8::ArrayBuffer::NewBackingStore(isolate(), suggested_size);
  uv_buf_t buf = uv_buf_init(static_cast<char*>(bs->Data()), bs->ByteLength());
  released_allocated_buffers_.emplace(buf.base, std::move(bs));
  return buf;
}

std::unique_ptr<v8::BackingStore> Environment::release_managed_buffer(
    const uv_buf_t& buf) {
  std::unique_ptr<v8::BackingStore> bs;
  if (buf.base != nullptr) {
    auto it = released_allocated_buffers_.find(buf.base);
    CHECK_NE(it, released_allocated_buffers_.end());
    bs = std::move(it->second);
    released_allocated_buffers_.erase(it);
  }
  return bs;
}

std::string Environment::GetExecPath(const std::vector<std::string>& argv) {
  char exec_path_buf[2 * PATH_MAX];
  size_t exec_path_len = sizeof(exec_path_buf);
  std::string exec_path;
  if (uv_exepath(exec_path_buf, &exec_path_len) == 0) {
    exec_path = std::string(exec_path_buf, exec_path_len);
  } else if (!argv.empty()) {
    exec_path = argv[0];
  }

  // On OpenBSD process.execPath will be relative unless we
  // get the full path before process.execPath is used.
#if defined(__OpenBSD__)
  uv_fs_t req;
  req.ptr = nullptr;
  if (0 ==
      uv_fs_realpath(nullptr, &req, exec_path.c_str(), nullptr)) {
    CHECK_NOT_NULL(req.ptr);
    exec_path = std::string(static_cast<char*>(req.ptr));
  }
  uv_fs_req_cleanup(&req);
#endif

  return exec_path;
}

Environment::Environment(IsolateData* isolate_data,
                         Isolate* isolate,
                         const std::vector<std::string>& args,
                         const std::vector<std::string>& exec_args,
                         const EnvSerializeInfo* env_info,
                         EnvironmentFlags::Flags flags,
                         ThreadId thread_id)
    : isolate_(isolate),
      isolate_data_(isolate_data),
      async_hooks_(isolate, MAYBE_FIELD_PTR(env_info, async_hooks)),
      immediate_info_(isolate, MAYBE_FIELD_PTR(env_info, immediate_info)),
      timeout_info_(isolate_, 1, MAYBE_FIELD_PTR(env_info, timeout_info)),
      tick_info_(isolate, MAYBE_FIELD_PTR(env_info, tick_info)),
      timer_base_(uv_now(isolate_data->event_loop())),
      exec_argv_(exec_args),
      argv_(args),
      exec_path_(Environment::GetExecPath(args)),
      exit_info_(
          isolate_, kExitInfoFieldCount, MAYBE_FIELD_PTR(env_info, exit_info)),
      should_abort_on_uncaught_toggle_(
          isolate_,
          1,
          MAYBE_FIELD_PTR(env_info, should_abort_on_uncaught_toggle)),
      stream_base_state_(isolate_,
                         StreamBase::kNumStreamBaseStateFields,
                         MAYBE_FIELD_PTR(env_info, stream_base_state)),
      time_origin_(performance::performance_process_start),
      time_origin_timestamp_(performance::performance_process_start_timestamp),
      environment_start_(PERFORMANCE_NOW()),
      flags_(flags),
      thread_id_(thread_id.id == static_cast<uint64_t>(-1)
                     ? AllocateEnvironmentThreadId().id
                     : thread_id.id) {
  constexpr bool is_shared_ro_heap =
#ifdef NODE_V8_SHARED_RO_HEAP
      true;
#else
      false;
#endif
  if (is_shared_ro_heap && !is_main_thread()) {
    // If this is a Worker thread and we are in shared-readonly-heap mode,
    // we can always safely use the parent's Isolate's code cache.
    CHECK_NOT_NULL(isolate_data->worker_context());
    builtin_loader()->CopySourceAndCodeCacheReferenceFrom(
        isolate_data->worker_context()->env()->builtin_loader());
  } else if (isolate_data->snapshot_data() != nullptr) {
    // ... otherwise, if a snapshot was provided, use its code cache.
    size_t cache_size = isolate_data->snapshot_data()->code_cache.size();
    per_process::Debug(DebugCategory::CODE_CACHE,
                       "snapshot contains %zu code cache\n",
                       cache_size);
    if (cache_size > 0) {
      builtin_loader()->RefreshCodeCache(
          isolate_data->snapshot_data()->code_cache);
    }
  }

  // Compile builtins eagerly when building the snapshot so that inner functions
  // of essential builtins that are loaded in the snapshot can have faster first
  // invocation.
  if (isolate_data->is_building_snapshot()) {
    builtin_loader()->SetEagerCompile();
  }

  // We'll be creating new objects so make sure we've entered the context.
  HandleScope handle_scope(isolate);

  // Set some flags if only kDefaultFlags was passed. This can make API version
  // transitions easier for embedders.
  if (flags_ & EnvironmentFlags::kDefaultFlags) {
    flags_ = flags_ |
        EnvironmentFlags::kOwnsProcessState |
        EnvironmentFlags::kOwnsInspector;
  }

  // We create new copies of the per-Environment option sets, so that it is
  // easier to modify them after Environment creation. The defaults are
  // part of the per-Isolate option set, for which in turn the defaults are
  // part of the per-process option set.
  options_ = std::make_shared<EnvironmentOptions>(
      *isolate_data->options()->per_env);
  inspector_host_port_ = std::make_shared<ExclusiveAccess<HostPort>>(
      options_->debug_options().host_port);

  set_env_vars(per_process::system_environment);
  // This should be done after options is created, so that --trace-env can be
  // checked when parsing NODE_DEBUG_NATIVE. It should also be done after
  // env_vars() is set so that the parser uses values from env->env_vars()
  // which may or may not be the system environment variable store.
  enabled_debug_list_.Parse(this);

  heap_snapshot_near_heap_limit_ =
      static_cast<uint32_t>(options_->heap_snapshot_near_heap_limit);

  if (!(flags_ & EnvironmentFlags::kOwnsProcessState)) {
    set_abort_on_uncaught_exception(false);
  }

#if HAVE_INSPECTOR
  // We can only create the inspector agent after having cloned the options.
  inspector_agent_ = std::make_unique<inspector::Agent>(this);
#endif

  if (tracing::AgentWriterHandle* writer = GetTracingAgentWriter()) {
    trace_state_observer_ = std::make_unique<TrackingTraceStateObserver>(this);
    if (TracingController* tracing_controller = writer->GetTracingController())
      tracing_controller->AddTraceStateObserver(trace_state_observer_.get());
  }

  destroy_async_id_list_.reserve(512);

  performance_state_ = std::make_unique<performance::PerformanceState>(
      isolate,
      time_origin_,
      time_origin_timestamp_,
      MAYBE_FIELD_PTR(env_info, performance_state));

  if (*TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(
          TRACING_CATEGORY_NODE1(environment)) != 0) {
    auto traced_value = tracing::TracedValue::Create();
    traced_value->BeginArray("args");
    for (const std::string& arg : args) traced_value->AppendString(arg);
    traced_value->EndArray();
    traced_value->BeginArray("exec_args");
    for (const std::string& arg : exec_args) traced_value->AppendString(arg);
    traced_value->EndArray();
    TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(TRACING_CATEGORY_NODE1(environment),
                                      "Environment",
                                      this,
                                      "args",
                                      std::move(traced_value));
  }

  if (options_->permission) {
    permission()->EnablePermissions();
    // The process shouldn't be able to neither
    // spawn/worker nor use addons or enable inspector
    // unless explicitly allowed by the user
    if (!options_->allow_addons) {
      options_->allow_native_addons = false;
    }
    flags_ = flags_ | EnvironmentFlags::kNoCreateInspector;
    permission()->Apply(this, {"*"}, permission::PermissionScope::kInspector);
    if (!options_->allow_child_process) {
      permission()->Apply(
          this, {"*"}, permission::PermissionScope::kChildProcess);
    }
    if (!options_->allow_worker_threads) {
      permission()->Apply(
          this, {"*"}, permission::PermissionScope::kWorkerThreads);
    }
    if (!options_->allow_wasi) {
      permission()->Apply(this, {"*"}, permission::PermissionScope::kWASI);
    }

    if (!options_->allow_fs_read.empty()) {
      permission()->Apply(this,
                          options_->allow_fs_read,
                          permission::PermissionScope::kFileSystemRead);
    }

    if (!options_->allow_fs_write.empty()) {
      permission()->Apply(this,
                          options_->allow_fs_write,
                          permission::PermissionScope::kFileSystemWrite);
    }
  }
}

void Environment::InitializeMainContext(Local<Context> context,
                                        const EnvSerializeInfo* env_info) {
  principal_realm_ = std::make_unique<PrincipalRealm>(
      this, context, MAYBE_FIELD_PTR(env_info, principal_realm));
  if (env_info != nullptr) {
    DeserializeProperties(env_info);
  }

  if (!options_->force_async_hooks_checks) {
    async_hooks_.no_force_checks();
  }

  // By default, always abort when --abort-on-uncaught-exception was passed.
  should_abort_on_uncaught_toggle_[0] = 1;

  // The process is not exiting by default.
  set_exiting(false);

  performance_state_->Mark(performance::NODE_PERFORMANCE_MILESTONE_ENVIRONMENT,
                           environment_start_);
  performance_state_->Mark(performance::NODE_PERFORMANCE_MILESTONE_NODE_START,
                           per_process::node_start_time);

  if (per_process::v8_initialized) {
    performance_state_->Mark(performance::NODE_PERFORMANCE_MILESTONE_V8_START,
                            performance::performance_v8_start);
  }
}

Environment::~Environment() {
  HandleScope handle_scope(isolate());
  Local<Context> ctx = context();

  if (Environment** interrupt_data = interrupt_data_.load()) {
    // There are pending RequestInterrupt() callbacks. Tell them not to run,
    // then force V8 to run interrupts by compiling and running an empty script
    // so as not to leak memory.
    *interrupt_data = nullptr;

    Isolate::AllowJavascriptExecutionScope allow_js_here(isolate());
    TryCatch try_catch(isolate());
    Context::Scope context_scope(ctx);

#ifdef DEBUG
    bool consistency_check = false;
    isolate()->RequestInterrupt([](Isolate*, void* data) {
      *static_cast<bool*>(data) = true;
    }, &consistency_check);
#endif

    Local<Script> script;
    if (Script::Compile(ctx, String::Empty(isolate())).ToLocal(&script))
      USE(script->Run(ctx));

    DCHECK(consistency_check);
  }

  // FreeEnvironment() should have set this.
  CHECK(is_stopping());

  if (heapsnapshot_near_heap_limit_callback_added_) {
    RemoveHeapSnapshotNearHeapLimitCallback(0);
  }

  isolate()->GetHeapProfiler()->RemoveBuildEmbedderGraphCallback(
      BuildEmbedderGraph, this);

#if HAVE_INSPECTOR
  // Destroy inspector agent before erasing the context. The inspector
  // destructor depends on the context still being accessible.
  inspector_agent_.reset();
#endif

  // Sub-realms should have been cleared with Environment's cleanup.
  DCHECK_EQ(shadow_realms_.size(), 0);
  principal_realm_.reset();

  if (trace_state_observer_) {
    tracing::AgentWriterHandle* writer = GetTracingAgentWriter();
    CHECK_NOT_NULL(writer);
    if (TracingController* tracing_controller = writer->GetTracingController())
      tracing_controller->RemoveTraceStateObserver(trace_state_observer_.get());
  }

  TRACE_EVENT_NESTABLE_ASYNC_END0(
    TRACING_CATEGORY_NODE1(environment), "Environment", this);

  // Do not unload addons on the main thread. Some addons need to retain memory
  // beyond the Environment's lifetime, and unloading them early would break
  // them; with Worker threads, we have the opportunity to be stricter.
  // Also, since the main thread usually stops just before the process exits,
  // this is far less relevant here.
  if (!is_main_thread()) {
    // Dereference all addons that were loaded into this environment.
    for (binding::DLib& addon : loaded_addons_) {
      addon.Close();
    }
  }
}

void Environment::InitializeLibuv() {
  HandleScope handle_scope(isolate());
  Context::Scope context_scope(context());

  CHECK_EQ(0, uv_timer_init(event_loop(), timer_handle()));
  uv_unref(reinterpret_cast<uv_handle_t*>(timer_handle()));

  CHECK_EQ(0, uv_check_init(event_loop(), immediate_check_handle()));
  uv_unref(reinterpret_cast<uv_handle_t*>(immediate_check_handle()));

  CHECK_EQ(0, uv_idle_init(event_loop(), immediate_idle_handle()));

  CHECK_EQ(0, uv_check_start(immediate_check_handle(), CheckImmediate));

  // Inform V8's CPU profiler when we're idle.  The profiler is sampling-based
  // but not all samples are created equal; mark the wall clock time spent in
  // epoll_wait() and friends so profiling tools can filter it out.  The samples
  // still end up in v8.log but with state=IDLE rather than state=EXTERNAL.
  CHECK_EQ(0, uv_prepare_init(event_loop(), &idle_prepare_handle_));
  CHECK_EQ(0, uv_check_init(event_loop(), &idle_check_handle_));

  CHECK_EQ(0, uv_async_init(
      event_loop(),
      &task_queues_async_,
      [](uv_async_t* async) {
        Environment* env = ContainerOf(
            &Environment::task_queues_async_, async);
        HandleScope handle_scope(env->isolate());
        Context::Scope context_scope(env->context());
        env->RunAndClearNativeImmediates();
      }));
  uv_unref(reinterpret_cast<uv_handle_t*>(&idle_prepare_handle_));
  uv_unref(reinterpret_cast<uv_handle_t*>(&idle_check_handle_));
  uv_unref(reinterpret_cast<uv_handle_t*>(&task_queues_async_));

  {
    Mutex::ScopedLock lock(native_immediates_threadsafe_mutex_);
    task_queues_async_initialized_ = true;
    if (native_immediates_threadsafe_.size() > 0 ||
        native_immediates_interrupts_.size() > 0) {
      uv_async_send(&task_queues_async_);
    }
  }

  StartProfilerIdleNotifier();
  env_handle_initialized_ = true;
}

void Environment::InitializeCompileCache() {
  std::string dir_from_env;
  if (!credentials::SafeGetenv("NODE_COMPILE_CACHE", &dir_from_env, this) ||
      dir_from_env.empty()) {
    return;
  }
  EnableCompileCache(dir_from_env);
}

CompileCacheEnableResult Environment::EnableCompileCache(
    const std::string& cache_dir) {
  CompileCacheEnableResult result;
  std::string disable_env;
  if (credentials::SafeGetenv(
          "NODE_DISABLE_COMPILE_CACHE", &disable_env, this)) {
    result.status = CompileCacheEnableStatus::DISABLED;
    result.message = "Disabled by NODE_DISABLE_COMPILE_CACHE";
    Debug(this,
          DebugCategory::COMPILE_CACHE,
          "[compile cache] %s.\n",
          result.message);
    return result;
  }

  if (!compile_cache_handler_) {
    std::unique_ptr<CompileCacheHandler> handler =
        std::make_unique<CompileCacheHandler>(this);
    result = handler->Enable(this, cache_dir);
    if (result.status == CompileCacheEnableStatus::ENABLED) {
      compile_cache_handler_ = std::move(handler);
      AtExit(
          [](void* env) {
            static_cast<Environment*>(env)->FlushCompileCache();
          },
          this);
    }
    if (!result.message.empty()) {
      Debug(this,
            DebugCategory::COMPILE_CACHE,
            "[compile cache] %s\n",
            result.message);
    }
  } else {
    result.status = CompileCacheEnableStatus::ALREADY_ENABLED;
    result.cache_directory = compile_cache_handler_->cache_dir();
  }
  return result;
}

void Environment::FlushCompileCache() {
  if (!compile_cache_handler_ || compile_cache_handler_->cache_dir().empty()) {
    return;
  }
  compile_cache_handler_->Persist();
}

void Environment::ExitEnv(StopFlags::Flags flags) {
  // Should not access non-thread-safe methods here.
  set_stopping(true);

#if HAVE_INSPECTOR
  if (inspector_agent_) {
    inspector_agent_->StopIfWaitingForConnect();
  }
#endif

  if ((flags & StopFlags::kDoNotTerminateIsolate) == 0)
    isolate_->TerminateExecution();
  SetImmediateThreadsafe([](Environment* env) {
    env->set_can_call_into_js(false);
    uv_stop(env->event_loop());
  });
}

void Environment::ClosePerEnvHandles() {
  // If LoadEnvironment and InitializeLibuv are not called, like when building
  // snapshots, skip closing the per environment handles.
  if (!env_handle_initialized_) {
    return;
  }

  auto close_and_finish = [&](uv_handle_t* handle) {
    CloseHandle(handle, [](uv_handle_t* handle) {
#ifdef DEBUG
      memset(handle, 0xab, uv_handle_size(handle->type));
#endif
    });
  };

  close_and_finish(reinterpret_cast<uv_handle_t*>(timer_handle()));
  close_and_finish(reinterpret_cast<uv_handle_t*>(immediate_check_handle()));
  close_and_finish(reinterpret_cast<uv_handle_t*>(immediate_idle_handle()));
  close_and_finish(reinterpret_cast<uv_handle_t*>(&idle_prepare_handle_));
  close_and_finish(reinterpret_cast<uv_handle_t*>(&idle_check_handle_));
  close_and_finish(reinterpret_cast<uv_handle_t*>(&task_queues_async_));
}

void Environment::CleanupHandles() {
  {
    Mutex::ScopedLock lock(native_immediates_threadsafe_mutex_);
    task_queues_async_initialized_ = false;
  }

  Isolate::DisallowJavascriptExecutionScope disallow_js(isolate(),
      Isolate::DisallowJavascriptExecutionScope::THROW_ON_FAILURE);

  RunAndClearNativeImmediates(true /* skip unrefed SetImmediate()s */);

  for (ReqWrapBase* request : req_wrap_queue_)
    request->Cancel();

  for (HandleWrap* handle : handle_wrap_queue_)
    handle->Close();

  while (handle_cleanup_waiting_ != 0 ||
         request_waiting_ != 0 ||
         !handle_wrap_queue_.IsEmpty()) {
    uv_run(event_loop(), UV_RUN_ONCE);
  }
}

void Environment::StartProfilerIdleNotifier() {
  uv_prepare_start(&idle_prepare_handle_, [](uv_prepare_t* handle) {
    Environment* env = ContainerOf(&Environment::idle_prepare_handle_, handle);
    env->isolate()->SetIdle(true);
  });
  uv_check_start(&idle_check_handle_, [](uv_check_t* handle) {
    Environment* env = ContainerOf(&Environment::idle_check_handle_, handle);
    env->isolate()->SetIdle(false);
  });
}

void Environment::PrintSyncTrace() const {
  if (!trace_sync_io_) [[likely]]
    return;

  HandleScope handle_scope(isolate());

  fprintf(
      stderr, "(node:%d) WARNING: Detected use of sync API\n", uv_os_getpid());
  PrintStackTrace(
      isolate(),
      StackTrace::CurrentStackTrace(isolate(),
                                    static_cast<int>(stack_trace_limit()),
                                    StackTrace::kDetailed));
}

MaybeLocal<Value> Environment::RunSnapshotSerializeCallback() const {
  EscapableHandleScope handle_scope(isolate());
  if (!snapshot_serialize_callback().IsEmpty()) {
    Context::Scope context_scope(context());
    return handle_scope.EscapeMaybe(snapshot_serialize_callback()->Call(
        context(), v8::Undefined(isolate()), 0, nullptr));
  }
  return handle_scope.Escape(Undefined(isolate()));
}

MaybeLocal<Value> Environment::RunSnapshotDeserializeMain() const {
  EscapableHandleScope handle_scope(isolate());
  if (!snapshot_deserialize_main().IsEmpty()) {
    Context::Scope context_scope(context());
    return handle_scope.EscapeMaybe(snapshot_deserialize_main()->Call(
        context(), v8::Undefined(isolate()), 0, nullptr));
  }
  return handle_scope.Escape(Undefined(isolate()));
}

void Environment::RunCleanup() {
  started_cleanup_ = true;
  TRACE_EVENT0(TRACING_CATEGORY_NODE1(environment), "RunCleanup");
  ClosePerEnvHandles();
  // Only BaseObject's cleanups are registered as per-realm cleanup hooks now.
  // Defer the BaseObject cleanup after handles are cleaned up.
  CleanupHandles();

  while (!cleanable_queue_.IsEmpty()) {
    Cleanable* cleanable = cleanable_queue_.PopFront();
    cleanable->Clean();
  }

  while (!cleanup_queue_.empty() || principal_realm_->PendingCleanup() ||
         native_immediates_.size() > 0 ||
         native_immediates_threadsafe_.size() > 0 ||
         native_immediates_interrupts_.size() > 0) {
    // TODO(legendecas): cleanup handles in per-realm cleanup hooks as well.
    principal_realm_->RunCleanup();
    cleanup_queue_.Drain();
    CleanupHandles();
  }

  for (const int fd : unmanaged_fds_) {
    uv_fs_t close_req;
    uv_fs_close(nullptr, &close_req, fd, nullptr);
    uv_fs_req_cleanup(&close_req);
  }
}

void Environment::RunAtExitCallbacks() {
  TRACE_EVENT0(TRACING_CATEGORY_NODE1(environment), "AtExit");
  for (ExitCallback at_exit : at_exit_functions_) {
    at_exit.cb_(at_exit.arg_);
  }
  at_exit_functions_.clear();
}

void Environment::AtExit(void (*cb)(void* arg), void* arg) {
  at_exit_functions_.push_front(ExitCallback{cb, arg});
}

Maybe<bool> Environment::CheckUnsettledTopLevelAwait() const {
  HandleScope scope(isolate_);
  Local<Context> ctx = context();
  Local<Value> value;

  Local<Value> entry_point_promise;
  if (!ctx->Global()
           ->GetPrivate(ctx, entry_point_promise_private_symbol())
           .ToLocal(&entry_point_promise)) {
    return v8::Nothing<bool>();
  }
  if (!entry_point_promise->IsPromise()) {
    return v8::Just(true);
  }
  if (entry_point_promise.As<Promise>()->State() !=
      Promise::PromiseState::kPending) {
    return v8::Just(true);
  }

  if (!ctx->Global()
           ->GetPrivate(ctx, entry_point_module_private_symbol())
           .ToLocal(&value)) {
    return v8::Nothing<bool>();
  }
  if (!value->IsObject()) {
    return v8::Just(true);
  }
  Local<Object> object = value.As<Object>();
  CHECK(BaseObject::IsBaseObject(isolate_data_, object));
  CHECK_EQ(object->InternalFieldCount(),
           loader::ModuleWrap::kInternalFieldCount);
  auto* wrap = BaseObject::FromJSObject<loader::ModuleWrap>(object);
  return wrap->CheckUnsettledTopLevelAwait();
}

void Environment::RunAndClearInterrupts() {
  while (native_immediates_interrupts_.size() > 0) {
    NativeImmediateQueue queue;
    {
      Mutex::ScopedLock lock(native_immediates_threadsafe_mutex_);
      queue.ConcatMove(std::move(native_immediates_interrupts_));
    }
    DebugSealHandleScope seal_handle_scope(isolate());

    while (auto head = queue.Shift())
      head->Call(this);
  }
}

void Environment::RunAndClearNativeImmediates(bool only_refed) {
  TRACE_EVENT0(TRACING_CATEGORY_NODE1(environment),
               "RunAndClearNativeImmediates");
  HandleScope handle_scope(isolate_);
  // In case the Isolate is no longer accessible just use an empty Local. This
  // is not an issue for InternalCallbackScope as this case is already handled
  // in its constructor but we avoid calls into v8 which can crash the process
  // in debug builds.
  Local<Object> obj =
      can_call_into_js() ? Object::New(isolate_) : Local<Object>();
  InternalCallbackScope cb_scope(this, obj, {0, 0});

  size_t ref_count = 0;

  // Handle interrupts first. These functions are not allowed to throw
  // exceptions, so we do not need to handle that.
  RunAndClearInterrupts();

  auto drain_list = [&](NativeImmediateQueue* queue) {
    TryCatchScope try_catch(this);
    DebugSealHandleScope seal_handle_scope(isolate());
    while (auto head = queue->Shift()) {
      bool is_refed = head->flags() & CallbackFlags::kRefed;
      if (is_refed)
        ref_count++;

      if (is_refed || !only_refed)
        head->Call(this);

      head.reset();  // Destroy now so that this is also observed by try_catch.

      if (try_catch.HasCaught()) [[unlikely]] {
        if (!try_catch.HasTerminated() && can_call_into_js())
          errors::TriggerUncaughtException(isolate(), try_catch);

        return true;
      }
    }
    return false;
  };
  while (drain_list(&native_immediates_)) {}

  immediate_info()->ref_count_dec(ref_count);

  if (immediate_info()->ref_count() == 0)
    ToggleImmediateRef(false);

  // It is safe to check .size() first, because there is a causal relationship
  // between pushes to the threadsafe immediate list and this function being
  // called. For the common case, it's worth checking the size first before
  // establishing a mutex lock.
  // This is intentionally placed after the `ref_count` handling, because when
  // refed threadsafe immediates are created, they are not counted towards the
  // count in immediate_info() either.
  NativeImmediateQueue threadsafe_immediates;
  if (native_immediates_threadsafe_.size() > 0) {
    Mutex::ScopedLock lock(native_immediates_threadsafe_mutex_);
    threadsafe_immediates.ConcatMove(std::move(native_immediates_threadsafe_));
  }
  while (drain_list(&threadsafe_immediates)) {}
}

void Environment::RequestInterruptFromV8() {
  // The Isolate may outlive the Environment, so some logic to handle the
  // situation in which the Environment is destroyed before the handler runs
  // is required.

  // We allocate a new pointer to a pointer to this Environment instance, and
  // try to set it as interrupt_data_. If interrupt_data_ was already set, then
  // callbacks are already scheduled to run and we can delete our own pointer
  // and just return. If it was nullptr previously, the Environment** is stored;
  // ~Environment sets the Environment* contained in it to nullptr, so that
  // the callback can check whether ~Environment has already run and it is thus
  // not safe to access the Environment instance itself.
  Environment** interrupt_data = new Environment*(this);
  Environment** dummy = nullptr;
  if (!interrupt_data_.compare_exchange_strong(dummy, interrupt_data)) {
    delete interrupt_data;
    return;  // Already scheduled.
  }

  isolate()->RequestInterrupt([](Isolate* isolate, void* data) {
    std::unique_ptr<Environment*> env_ptr { static_cast<Environment**>(data) };
    Environment* env = *env_ptr;
    if (env == nullptr) {
      // The Environment has already been destroyed. That should be okay; any
      // callback added before the Environment shuts down would have been
      // handled during cleanup.
      return;
    }
    env->interrupt_data_.store(nullptr);
    env->RunAndClearInterrupts();
  }, interrupt_data);
}

void Environment::ScheduleTimer(int64_t duration_ms) {
  if (started_cleanup_) return;
  uv_timer_start(timer_handle(), RunTimers, duration_ms, 0);
}

void Environment::ToggleTimerRef(bool ref) {
  if (started_cleanup_) return;

  if (ref) {
    uv_ref(reinterpret_cast<uv_handle_t*>(timer_handle()));
  } else {
    uv_unref(reinterpret_cast<uv_handle_t*>(timer_handle()));
  }
}

void Environment::RunTimers(uv_timer_t* handle) {
  Environment* env = Environment::from_timer_handle(handle);
  TRACE_EVENT0(TRACING_CATEGORY_NODE1(environment), "RunTimers");

  if (!env->can_call_into_js())
    return;

  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Object> process = env->process_object();
  InternalCallbackScope scope(env, process, {0, 0});

  Local<Function> cb = env->timers_callback_function();
  MaybeLocal<Value> ret;
  Local<Value> arg = env->GetNow();
  // This code will loop until all currently due timers will process. It is
  // impossible for us to end up in an infinite loop due to how the JS-side
  // is structured.
  do {
    TryCatchScope try_catch(env);
    try_catch.SetVerbose(true);
    ret = cb->Call(env->context(), process, 1, &arg);
  } while (ret.IsEmpty() && env->can_call_into_js());

  // NOTE(apapirovski): If it ever becomes possible that `call_into_js` above
  // is reset back to `true` after being previously set to `false` then this
  // code becomes invalid and needs to be rewritten. Otherwise catastrophic
  // timers corruption will occur and all timers behaviour will become
  // entirely unpredictable.
  if (ret.IsEmpty())
    return;

  // To allow for less JS-C++ boundary crossing, the value returned from JS
  // serves a few purposes:
  // 1. If it's 0, no more timers exist and the handle should be unrefed
  // 2. If it's > 0, the value represents the next timer's expiry and there
  //    is at least one timer remaining that is refed.
  // 3. If it's < 0, the absolute value represents the next timer's expiry
  //    and there are no timers that are refed.
  int64_t expiry_ms =
      ret.ToLocalChecked()->IntegerValue(env->context()).FromJust();

  auto* h = reinterpret_cast<uv_handle_t*>(handle);

  if (expiry_ms != 0) {
    int64_t duration_ms =
        llabs(expiry_ms) - (uv_now(env->event_loop()) - env->timer_base());

    env->ScheduleTimer(duration_ms > 0 ? duration_ms : 1);

    if (expiry_ms > 0)
      uv_ref(h);
    else
      uv_unref(h);
  } else {
    uv_unref(h);
  }
}


void Environment::CheckImmediate(uv_check_t* handle) {
  Environment* env = Environment::from_immediate_check_handle(handle);
  TRACE_EVENT0(TRACING_CATEGORY_NODE1(environment), "CheckImmediate");

  HandleScope scope(env->isolate());
  Context::Scope context_scope(env->context());

  env->RunAndClearNativeImmediates();

  if (env->immediate_info()->count() == 0 || !env->can_call_into_js())
    return;

  do {
    MakeCallback(env->isolate(),
                 env->process_object(),
                 env->immediate_callback_function(),
                 0,
                 nullptr,
                 {0, 0}).ToLocalChecked();
  } while (env->immediate_info()->has_outstanding() && env->can_call_into_js());

  if (env->immediate_info()->ref_count() == 0)
    env->ToggleImmediateRef(false);
}

void Environment::ToggleImmediateRef(bool ref) {
  if (started_cleanup_) return;

  if (ref) {
    // Idle handle is needed only to stop the event loop from blocking in poll.
    uv_idle_start(immediate_idle_handle(), [](uv_idle_t*){ });
  } else {
    uv_idle_stop(immediate_idle_handle());
  }
}

uint64_t Environment::GetNowUint64() {
  uv_update_time(event_loop());
  uint64_t now = uv_now(event_loop());
  CHECK_GE(now, timer_base());
  now -= timer_base();
  return now;
}

Local<Value> Environment::GetNow() {
  uint64_t now = GetNowUint64();
  if (now <= 0xffffffff)
    return Integer::NewFromUnsigned(isolate(), static_cast<uint32_t>(now));
  return Number::New(isolate(), static_cast<double>(now));
}

void CollectExceptionInfo(Environment* env,
                          Local<Object> obj,
                          int errorno,
                          const char* err_string,
                          const char* syscall,
                          const char* message,
                          const char* path,
                          const char* dest) {
  obj->Set(env->context(),
           env->errno_string(),
           Integer::New(env->isolate(), errorno)).Check();

  obj->Set(env->context(), env->code_string(),
           OneByteString(env->isolate(), err_string)).Check();

  if (message != nullptr) {
    obj->Set(env->context(), env->message_string(),
             OneByteString(env->isolate(), message)).Check();
  }

  Local<Value> path_buffer;
  if (path != nullptr) {
    path_buffer =
      Buffer::Copy(env->isolate(), path, strlen(path)).ToLocalChecked();
    obj->Set(env->context(), env->path_string(), path_buffer).Check();
  }

  Local<Value> dest_buffer;
  if (dest != nullptr) {
    dest_buffer =
      Buffer::Copy(env->isolate(), dest, strlen(dest)).ToLocalChecked();
    obj->Set(env->context(), env->dest_string(), dest_buffer).Check();
  }

  if (syscall != nullptr) {
    obj->Set(env->context(), env->syscall_string(),
             OneByteString(env->isolate(), syscall)).Check();
  }
}

void Environment::CollectUVExceptionInfo(Local<Value> object,
                                         int errorno,
                                         const char* syscall,
                                         const char* message,
                                         const char* path,
                                         const char* dest) {
  if (!object->IsObject() || errorno == 0)
    return;

  Local<Object> obj = object.As<Object>();
  const char* err_string = uv_err_name(errorno);

  if (message == nullptr || message[0] == '\0') {
    message = uv_strerror(errorno);
  }

  CollectExceptionInfo(
      this, obj, errorno, err_string, syscall, message, path, dest);
}

ImmediateInfo::ImmediateInfo(Isolate* isolate, const SerializeInfo* info)
    : fields_(isolate, kFieldsCount, MAYBE_FIELD_PTR(info, fields)) {}

ImmediateInfo::SerializeInfo ImmediateInfo::Serialize(
    Local<Context> context, SnapshotCreator* creator) {
  return {fields_.Serialize(context, creator)};
}

void ImmediateInfo::Deserialize(Local<Context> context) {
  fields_.Deserialize(context);
}

std::ostream& operator<<(std::ostream& output,
                         const ImmediateInfo::SerializeInfo& i) {
  output << "{ " << i.fields << " }";
  return output;
}

void ImmediateInfo::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("fields", fields_);
}

TickInfo::SerializeInfo TickInfo::Serialize(Local<Context> context,
                                            SnapshotCreator* creator) {
  return {fields_.Serialize(context, creator)};
}

void TickInfo::Deserialize(Local<Context> context) {
  fields_.Deserialize(context);
}

std::ostream& operator<<(std::ostream& output,
                         const TickInfo::SerializeInfo& i) {
  output << "{ " << i.fields << " }";
  return output;
}

void TickInfo::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("fields", fields_);
}

TickInfo::TickInfo(Isolate* isolate, const SerializeInfo* info)
    : fields_(
          isolate, kFieldsCount, info == nullptr ? nullptr : &(info->fields)) {}

AsyncHooks::AsyncHooks(Isolate* isolate, const SerializeInfo* info)
    : async_ids_stack_(isolate, 16 * 2, MAYBE_FIELD_PTR(info, async_ids_stack)),
      fields_(isolate, kFieldsCount, MAYBE_FIELD_PTR(info, fields)),
      async_id_fields_(
          isolate, kUidFieldsCount, MAYBE_FIELD_PTR(info, async_id_fields)),
      native_execution_async_resources_(isolate),
      info_(info) {
  HandleScope handle_scope(isolate);
  if (info == nullptr) {
    clear_async_id_stack();

    // Always perform async_hooks checks, not just when async_hooks is enabled.
    // TODO(AndreasMadsen): Consider removing this for LTS releases.
    // See discussion in https://github.com/nodejs/node/pull/15454
    // When removing this, do it by reverting the commit. Otherwise the test
    // and flag changes won't be included.
    fields_[kCheck] = 1;

    // kDefaultTriggerAsyncId should be -1, this indicates that there is no
    // specified default value and it should fallback to the executionAsyncId.
    // 0 is not used as the magic value, because that indicates a missing
    // context which is different from a default context.
    async_id_fields_[AsyncHooks::kDefaultTriggerAsyncId] = -1;

    // kAsyncIdCounter should start at 1 because that'll be the id the execution
    // context during bootstrap (code that runs before entering uv_run()).
    async_id_fields_[AsyncHooks::kAsyncIdCounter] = 1;
  }
}

void AsyncHooks::Deserialize(Local<Context> context) {
  async_ids_stack_.Deserialize(context);
  fields_.Deserialize(context);
  async_id_fields_.Deserialize(context);

  Local<Array> js_execution_async_resources;
  if (info_->js_execution_async_resources != 0) {
    js_execution_async_resources =
        context->GetDataFromSnapshotOnce<Array>(
            info_->js_execution_async_resources).ToLocalChecked();
  } else {
    js_execution_async_resources = Array::New(context->GetIsolate());
  }
  js_execution_async_resources_.Reset(
      context->GetIsolate(), js_execution_async_resources);

  // The native_execution_async_resources_ field requires v8::Local<> instances
  // for async calls whose resources were on the stack as JS objects when they
  // were entered. We cannot recreate this here; however, storing these values
  // on the JS equivalent gives the same result, so we do that instead.
  for (size_t i = 0; i < info_->native_execution_async_resources.size(); ++i) {
    if (info_->native_execution_async_resources[i] == SIZE_MAX)
      continue;
    Local<Object> obj = context->GetDataFromSnapshotOnce<Object>(
                                   info_->native_execution_async_resources[i])
                               .ToLocalChecked();
    js_execution_async_resources->Set(context, i, obj).Check();
  }
  info_ = nullptr;
}

std::ostream& operator<<(std::ostream& output,
                         const AsyncHooks::SerializeInfo& i) {
  output << "{\n"
         << "  " << i.async_ids_stack << ",  // async_ids_stack\n"
         << "  " << i.fields << ",  // fields\n"
         << "  " << i.async_id_fields << ",  // async_id_fields\n"
         << "  " << i.js_execution_async_resources
         << ",  // js_execution_async_resources\n"
         << "  " << i.native_execution_async_resources
         << ",  // native_execution_async_resources\n"
         << "}";
  return output;
}

AsyncHooks::SerializeInfo AsyncHooks::Serialize(Local<Context> context,
                                                SnapshotCreator* creator) {
  SerializeInfo info;
  // TODO(joyeecheung): some of these probably don't need to be serialized.
  info.async_ids_stack = async_ids_stack_.Serialize(context, creator);
  info.fields = fields_.Serialize(context, creator);
  info.async_id_fields = async_id_fields_.Serialize(context, creator);
  if (!js_execution_async_resources_.IsEmpty()) {
    info.js_execution_async_resources = creator->AddData(
        context, js_execution_async_resources_.Get(context->GetIsolate()));
    CHECK_NE(info.js_execution_async_resources, 0);
  } else {
    info.js_execution_async_resources = 0;
  }

  info.native_execution_async_resources.resize(
      native_execution_async_resources_.size());
  for (size_t i = 0; i < native_execution_async_resources_.size(); i++) {
    info.native_execution_async_resources[i] =
        native_execution_async_resources_[i].IsEmpty() ? SIZE_MAX :
            creator->AddData(
                context,
                native_execution_async_resources_[i]);
  }

  // At the moment, promise hooks are not supported in the startup snapshot.
  // TODO(joyeecheung): support promise hooks in the startup snapshot.
  CHECK(js_promise_hooks_[0].IsEmpty());
  CHECK(js_promise_hooks_[1].IsEmpty());
  CHECK(js_promise_hooks_[2].IsEmpty());
  CHECK(js_promise_hooks_[3].IsEmpty());

  return info;
}

void AsyncHooks::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("async_ids_stack", async_ids_stack_);
  tracker->TrackField("fields", fields_);
  tracker->TrackField("async_id_fields", async_id_fields_);
  tracker->TrackField("js_promise_hooks", js_promise_hooks_);
}

void AsyncHooks::grow_async_ids_stack() {
  async_ids_stack_.reserve(async_ids_stack_.Length() * 3);

  env()
      ->principal_realm()
      ->async_hooks_binding()
      ->Set(env()->context(),
            env()->async_ids_stack_string(),
            async_ids_stack_.GetJSArray())
      .Check();
}

void AsyncHooks::FailWithCorruptedAsyncStack(double expected_async_id) {
  fprintf(stderr,
          "Error: async hook stack has become corrupted ("
          "actual: %.f, expected: %.f)\n",
          async_id_fields_.GetValue(kExecutionAsyncId),
          expected_async_id);
  DumpNativeBacktrace(stderr);
  DumpJavaScriptBacktrace(stderr);
  fflush(stderr);
  // TODO(joyeecheung): should this exit code be more specific?
  if (!env()->abort_on_uncaught_exception()) Exit(ExitCode::kGenericUserError);
  fprintf(stderr, "\n");
  fflush(stderr);
  ABORT_NO_BACKTRACE();
}

void Environment::Exit(ExitCode exit_code) {
  if (options()->trace_exit) {
    HandleScope handle_scope(isolate());
    Isolate::DisallowJavascriptExecutionScope disallow_js(
        isolate(), Isolate::DisallowJavascriptExecutionScope::CRASH_ON_FAILURE);

    if (is_main_thread()) {
      fprintf(stderr, "(node:%d) ", uv_os_getpid());
    } else {
      fprintf(stderr, "(node:%d, thread:%" PRIu64 ") ",
              uv_os_getpid(), thread_id());
    }

    fprintf(stderr,
            "WARNING: Exited the environment with code %d\n",
            static_cast<int>(exit_code));
    PrintStackTrace(
        isolate(),
        StackTrace::CurrentStackTrace(isolate(),
                                      static_cast<int>(stack_trace_limit()),
                                      StackTrace::kDetailed));
  }
  process_exit_handler_(this, exit_code);
}

void Environment::stop_sub_worker_contexts() {
  DCHECK_EQ(Isolate::GetCurrent(), isolate());

  while (!sub_worker_contexts_.empty()) {
    Worker* w = *sub_worker_contexts_.begin();
    remove_sub_worker_context(w);
    w->Exit(ExitCode::kGenericUserError);
    w->JoinThread();
  }
}

Environment* Environment::worker_parent_env() const {
  if (worker_context() == nullptr) return nullptr;
  return worker_context()->env();
}

void Environment::AddUnmanagedFd(int fd) {
  if (!tracks_unmanaged_fds()) return;
  auto result = unmanaged_fds_.insert(fd);
  if (!result.second) {
    ProcessEmitWarning(
        this, "File descriptor %d opened in unmanaged mode twice", fd);
  }
}

void Environment::RemoveUnmanagedFd(int fd) {
  if (!tracks_unmanaged_fds()) return;
  size_t removed_count = unmanaged_fds_.erase(fd);
  if (removed_count == 0) {
    ProcessEmitWarning(
        this, "File descriptor %d closed but not opened in unmanaged mode", fd);
  }
}

void Environment::PrintInfoForSnapshotIfDebug() {
  if (enabled_debug_list()->enabled(DebugCategory::MKSNAPSHOT)) {
    fprintf(stderr, "At the exit of the Environment:\n");
    principal_realm()->PrintInfoForSnapshot();
  }
}

EnvSerializeInfo Environment::Serialize(SnapshotCreator* creator) {
  EnvSerializeInfo info;
  Local<Context> ctx = context();

  info.async_hooks = async_hooks_.Serialize(ctx, creator);
  info.immediate_info = immediate_info_.Serialize(ctx, creator);
  info.timeout_info = timeout_info_.Serialize(ctx, creator);
  info.tick_info = tick_info_.Serialize(ctx, creator);
  info.performance_state = performance_state_->Serialize(ctx, creator);
  info.exit_info = exit_info_.Serialize(ctx, creator);
  info.stream_base_state = stream_base_state_.Serialize(ctx, creator);
  info.should_abort_on_uncaught_toggle =
      should_abort_on_uncaught_toggle_.Serialize(ctx, creator);

  info.principal_realm = principal_realm_->Serialize(creator);
  // For now we only support serialization of the main context.
  // TODO(joyeecheung): support de/serialization of vm contexts.
  CHECK_EQ(contexts_.size(), 1);
  CHECK_EQ(contexts_[0], context());
  return info;
}

void Environment::EnqueueDeserializeRequest(DeserializeRequestCallback cb,
                                            Local<Object> holder,
                                            int index,
                                            InternalFieldInfoBase* info) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  DeserializeRequest request{cb, {isolate(), holder}, index, info};
  deserialize_requests_.push_back(std::move(request));
}

void Environment::RunDeserializeRequests() {
  HandleScope scope(isolate());
  Local<Context> ctx = context();
  Isolate* is = isolate();
  while (!deserialize_requests_.empty()) {
    DeserializeRequest request(std::move(deserialize_requests_.front()));
    deserialize_requests_.pop_front();
    Local<Object> holder = request.holder.Get(is);
    request.cb(ctx, holder, request.index, request.info);
    request.holder.Reset();
    request.info->Delete();
  }
}

void Environment::DeserializeProperties(const EnvSerializeInfo* info) {
  Local<Context> ctx = context();

  if (enabled_debug_list_.enabled(DebugCategory::MKSNAPSHOT)) {
    fprintf(stderr, "deserializing EnvSerializeInfo...\n");
    std::cerr << *info << "\n";
  }

  // Deserialize the realm's properties before running the deserialize
  // requests as the requests may need to access the realm's properties.
  principal_realm_->DeserializeProperties(&info->principal_realm);
  RunDeserializeRequests();

  async_hooks_.Deserialize(ctx);
  immediate_info_.Deserialize(ctx);
  timeout_info_.Deserialize(ctx);
  tick_info_.Deserialize(ctx);
  performance_state_->Deserialize(ctx, time_origin_, time_origin_timestamp_);
  exit_info_.Deserialize(ctx);
  stream_base_state_.Deserialize(ctx);
  should_abort_on_uncaught_toggle_.Deserialize(ctx);
}

void Environment::BuildEmbedderGraph(Isolate* isolate,
                                     EmbedderGraph* graph,
                                     void* data) {
  MemoryTracker tracker(isolate, graph);
  auto* env = static_cast<Environment*>(data);
  // Start traversing embedder objects from the root Environment object.
  tracker.Track(env);
}

std::optional<uint32_t> GetPromiseId(Environment* env, Local<Promise> promise) {
  Local<Value> id_val;
  if (!promise->GetPrivate(env->context(), env->promise_trace_id())
           .ToLocal(&id_val) ||
      !id_val->IsUint32()) {
    return std::nullopt;
  }
  return id_val.As<Uint32>()->Value();
}

void Environment::TracePromises(PromiseHookType type,
                                Local<Promise> promise,
                                Local<Value> parent) {
  // We don't care about the execution of promises, just the
  // creation/resolution.
  if (type == PromiseHookType::kBefore || type == PromiseHookType::kAfter) {
    return;
  }
  Isolate* isolate = Isolate::GetCurrent();
  Local<Context> context = isolate->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);
  if (env == nullptr) return;

  std::optional<uint32_t> parent_id;
  if (!parent.IsEmpty() && parent->IsPromise()) {
    parent_id = GetPromiseId(env, parent.As<Promise>());
  }

  uint32_t id = 0;
  std::string action;
  if (type == PromiseHookType::kInit) {
    id = env->trace_promise_id_counter_++;
    promise->SetPrivate(
        context, env->promise_trace_id(), Uint32::New(isolate, id));
    action = "created";
  } else if (type == PromiseHookType::kResolve) {
    auto opt = GetPromiseId(env, promise);
    if (!opt.has_value()) return;
    id = opt.value();
    action = "resolved";
  } else {
    UNREACHABLE();
  }

  FPrintF(stderr, "[--trace-promises] ");
  if (parent_id.has_value()) {
    FPrintF(stderr, "promise #%d ", parent_id.value());
  }
  FPrintF(stderr, "%s promise #%d\n", action, id);
  // TODO(joyeecheung): we can dump the native stack trace too if the
  // JS stack trace is empty i.e. it may be resolved on the native side.
  PrintCurrentStackTrace(isolate);
}

size_t Environment::NearHeapLimitCallback(void* data,
                                          size_t current_heap_limit,
                                          size_t initial_heap_limit) {
  auto* env = static_cast<Environment*>(data);

  Debug(env,
        DebugCategory::DIAGNOSTICS,
        "Invoked NearHeapLimitCallback, processing=%d, "
        "current_limit=%" PRIu64 ", "
        "initial_limit=%" PRIu64 "\n",
        env->is_in_heapsnapshot_heap_limit_callback_,
        static_cast<uint64_t>(current_heap_limit),
        static_cast<uint64_t>(initial_heap_limit));

  size_t max_young_gen_size = env->isolate_data()->max_young_gen_size;
  size_t young_gen_size = 0;
  size_t old_gen_size = 0;

  HeapSpaceStatistics stats;
  size_t num_heap_spaces = env->isolate()->NumberOfHeapSpaces();
  for (size_t i = 0; i < num_heap_spaces; ++i) {
    env->isolate()->GetHeapSpaceStatistics(&stats, i);
    if (strcmp(stats.space_name(), "new_space") == 0 ||
        strcmp(stats.space_name(), "new_large_object_space") == 0) {
      young_gen_size += stats.space_used_size();
    } else {
      old_gen_size += stats.space_used_size();
    }
  }

  Debug(env,
        DebugCategory::DIAGNOSTICS,
        "max_young_gen_size=%" PRIu64 ", "
        "young_gen_size=%" PRIu64 ", "
        "old_gen_size=%" PRIu64 ", "
        "total_size=%" PRIu64 "\n",
        static_cast<uint64_t>(max_young_gen_size),
        static_cast<uint64_t>(young_gen_size),
        static_cast<uint64_t>(old_gen_size),
        static_cast<uint64_t>(young_gen_size + old_gen_size));

  uint64_t available = uv_get_available_memory();
  // TODO(joyeecheung): get a better estimate about the native memory
  // usage into the overhead, e.g. based on the count of objects.
  uint64_t estimated_overhead = max_young_gen_size;
  Debug(env,
        DebugCategory::DIAGNOSTICS,
        "Estimated available memory=%" PRIu64 ", "
        "estimated overhead=%" PRIu64 "\n",
        available,
        estimated_overhead);

  // This might be hit when the snapshot is being taken in another
  // NearHeapLimitCallback invocation.
  // When taking the snapshot, objects in the young generation may be
  // promoted to the old generation, result in increased heap usage,
  // but it should be no more than the young generation size.
  // Ideally, this should be as small as possible - the heap limit
  // can only be restored when the heap usage falls down below the
  // new limit, so in a heap with unbounded growth the isolate
  // may eventually crash with this new limit - effectively raising
  // the heap limit to the new one.
  size_t new_limit = current_heap_limit + max_young_gen_size;
  if (env->is_in_heapsnapshot_heap_limit_callback_) {
    Debug(env,
          DebugCategory::DIAGNOSTICS,
          "Not generating snapshots in nested callback. "
          "new_limit=%" PRIu64 "\n",
          static_cast<uint64_t>(new_limit));
    return new_limit;
  }

  // Estimate whether the snapshot is going to use up all the memory
  // available to the process. If so, just give up to prevent the system
  // from killing the process for a system OOM.
  if (estimated_overhead > available) {
    Debug(env,
          DebugCategory::DIAGNOSTICS,
          "Not generating snapshots because it's too risky.\n");
    env->RemoveHeapSnapshotNearHeapLimitCallback(0);
    // The new limit must be higher than current_heap_limit or V8 might
    // crash.
    return new_limit;
  }

  // Take the snapshot synchronously.
  env->is_in_heapsnapshot_heap_limit_callback_ = true;

  std::string dir = env->options()->diagnostic_dir;
  if (dir.empty()) {
    dir = Environment::GetCwd(env->exec_path_);
  }
  DiagnosticFilename name(env, "Heap", "heapsnapshot");
  std::string filename = dir + kPathSeparator + (*name);

  Debug(env, DebugCategory::DIAGNOSTICS, "Start generating %s...\n", *name);

  HeapProfiler::HeapSnapshotOptions options;
  options.numerics_mode = HeapProfiler::NumericsMode::kExposeNumericValues;
  options.snapshot_mode = HeapProfiler::HeapSnapshotMode::kExposeInternals;
  heap::WriteSnapshot(env, filename.c_str(), options);
  env->heap_limit_snapshot_taken_ += 1;

  Debug(env,
        DebugCategory::DIAGNOSTICS,
        "%" PRIu32 "/%" PRIu32 " snapshots taken.\n",
        env->heap_limit_snapshot_taken_,
        env->heap_snapshot_near_heap_limit_);

  // Don't take more snapshots than the limit specified.
  if (env->heap_limit_snapshot_taken_ == env->heap_snapshot_near_heap_limit_) {
    Debug(env,
          DebugCategory::DIAGNOSTICS,
          "Removing the near heap limit callback");
    env->RemoveHeapSnapshotNearHeapLimitCallback(0);
  }

  FPrintF(stderr, "Wrote snapshot to %s\n", filename.c_str());
  // Tell V8 to reset the heap limit once the heap usage falls down to
  // 95% of the initial limit.
  env->isolate()->AutomaticallyRestoreInitialHeapLimit(0.95);

  env->is_in_heapsnapshot_heap_limit_callback_ = false;

  // The new limit must be higher than current_heap_limit or V8 might
  // crash.
  return new_limit;
}

inline size_t Environment::SelfSize() const {
  size_t size = sizeof(*this);
  // Remove non pointer fields that will be tracked in MemoryInfo()
  // TODO(joyeecheung): refactor the MemoryTracker interface so
  // this can be done for common types within the Track* calls automatically
  // if a certain scope is entered.
  size -= sizeof(async_hooks_);
  size -= sizeof(cleanup_queue_);
  size -= sizeof(tick_info_);
  size -= sizeof(immediate_info_);
  return size;
}

void Environment::MemoryInfo(MemoryTracker* tracker) const {
  // Iterable STLs have their own sizes subtracted from the parent
  // by default.
  tracker->TrackField("isolate_data", isolate_data_);
  tracker->TrackField("destroy_async_id_list", destroy_async_id_list_);
  tracker->TrackField("exec_argv", exec_argv_);
  tracker->TrackField("exit_info", exit_info_);
  tracker->TrackField("should_abort_on_uncaught_toggle",
                      should_abort_on_uncaught_toggle_);
  tracker->TrackField("stream_base_state", stream_base_state_);
  tracker->TrackField("cleanup_queue", cleanup_queue_);
  tracker->TrackField("async_hooks", async_hooks_);
  tracker->TrackField("immediate_info", immediate_info_);
  tracker->TrackField("timeout_info", timeout_info_);
  tracker->TrackField("tick_info", tick_info_);
  tracker->TrackField("principal_realm", principal_realm_);
  tracker->TrackField("shadow_realms", shadow_realms_);

  // FIXME(joyeecheung): track other fields in Environment.
  // Currently MemoryTracker is unable to track these
  // correctly:
  // - Internal types that do not implement MemoryRetainer yet
  // - STL containers with MemoryRetainer* inside
  // - STL containers with numeric types inside that should not have their
  //   nodes elided e.g. numeric keys in maps.
  // We also need to make sure that when we add a non-pointer field as its own
  // node, we shift its sizeof() size out of the Environment node.
}

void Environment::RunWeakRefCleanup() {
  isolate()->ClearKeptObjects();
}
}  // namespace node
