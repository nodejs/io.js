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

#include "node_v8.h"
#include "aliased_buffer-inl.h"
#include "base_object-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node.h"
#include "node_external_reference.h"
#include "util-inl.h"
#include "v8.h"

namespace node {
namespace v8_utils {
using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::HeapCodeStatistics;
using v8::HeapSpaceStatistics;
using v8::HeapStatistics;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::ScriptCompiler;
using v8::String;
using v8::Uint32;
using v8::V8;
using v8::Value;

#define HEAP_STATISTICS_PROPERTIES(V)                                          \
  V(0, total_heap_size, kTotalHeapSizeIndex)                                   \
  V(1, total_heap_size_executable, kTotalHeapSizeExecutableIndex)              \
  V(2, total_physical_size, kTotalPhysicalSizeIndex)                           \
  V(3, total_available_size, kTotalAvailableSize)                              \
  V(4, used_heap_size, kUsedHeapSizeIndex)                                     \
  V(5, heap_size_limit, kHeapSizeLimitIndex)                                   \
  V(6, malloced_memory, kMallocedMemoryIndex)                                  \
  V(7, peak_malloced_memory, kPeakMallocedMemoryIndex)                         \
  V(8, does_zap_garbage, kDoesZapGarbageIndex)                                 \
  V(9, number_of_native_contexts, kNumberOfNativeContextsIndex)                \
  V(10, number_of_detached_contexts, kNumberOfDetachedContextsIndex)           \
  V(11, total_global_handles_size, kTotalGlobalHandlesSizeIndex)               \
  V(12, used_global_handles_size, kUsedGlobalHandlesSizeIndex)                 \
  V(13, external_memory, kExternalMemoryIndex)

#define V(a, b, c) +1
static constexpr size_t kHeapStatisticsPropertiesCount =
    HEAP_STATISTICS_PROPERTIES(V);
#undef V

#define HEAP_SPACE_STATISTICS_PROPERTIES(V)                                   \
  V(0, space_size, kSpaceSizeIndex)                                           \
  V(1, space_used_size, kSpaceUsedSizeIndex)                                  \
  V(2, space_available_size, kSpaceAvailableSizeIndex)                        \
  V(3, physical_space_size, kPhysicalSpaceSizeIndex)

#define V(a, b, c) +1
static constexpr size_t kHeapSpaceStatisticsPropertiesCount =
    HEAP_SPACE_STATISTICS_PROPERTIES(V);
#undef V

#define HEAP_CODE_STATISTICS_PROPERTIES(V)                                     \
  V(0, code_and_metadata_size, kCodeAndMetadataSizeIndex)                      \
  V(1, bytecode_and_metadata_size, kBytecodeAndMetadataSizeIndex)              \
  V(2, external_script_source_size, kExternalScriptSourceSizeIndex)            \
  V(3, cpu_profiler_metadata_size, kCPUProfilerMetaDataSizeIndex)

#define V(a, b, c) +1
static const size_t kHeapCodeStatisticsPropertiesCount =
    HEAP_CODE_STATISTICS_PROPERTIES(V);
#undef V

BindingData::BindingData(Realm* realm,
                         Local<Object> obj,
                         InternalFieldInfo* info)
    : SnapshotableObject(realm, obj, type_int),
      heap_statistics_buffer(realm->isolate(),
                             kHeapStatisticsPropertiesCount,
                             MAYBE_FIELD_PTR(info, heap_statistics_buffer)),
      heap_space_statistics_buffer(
          realm->isolate(),
          kHeapSpaceStatisticsPropertiesCount,
          MAYBE_FIELD_PTR(info, heap_space_statistics_buffer)),
      heap_code_statistics_buffer(
          realm->isolate(),
          kHeapCodeStatisticsPropertiesCount,
          MAYBE_FIELD_PTR(info, heap_code_statistics_buffer)) {
  Local<Context> context = realm->context();
  if (info == nullptr) {
    obj->Set(context,
             FIXED_ONE_BYTE_STRING(realm->isolate(), "heapStatisticsBuffer"),
             heap_statistics_buffer.GetJSArray())
        .Check();
    obj->Set(
           context,
           FIXED_ONE_BYTE_STRING(realm->isolate(), "heapCodeStatisticsBuffer"),
           heap_code_statistics_buffer.GetJSArray())
        .Check();
    obj->Set(
           context,
           FIXED_ONE_BYTE_STRING(realm->isolate(), "heapSpaceStatisticsBuffer"),
           heap_space_statistics_buffer.GetJSArray())
        .Check();
  } else {
    heap_statistics_buffer.Deserialize(realm->context());
    heap_code_statistics_buffer.Deserialize(realm->context());
    heap_space_statistics_buffer.Deserialize(realm->context());
  }
  heap_statistics_buffer.MakeWeak();
  heap_space_statistics_buffer.MakeWeak();
  heap_code_statistics_buffer.MakeWeak();
}

bool BindingData::PrepareForSerialization(Local<Context> context,
                                          v8::SnapshotCreator* creator) {
  DCHECK_NULL(internal_field_info_);
  internal_field_info_ = InternalFieldInfoBase::New<InternalFieldInfo>(type());
  internal_field_info_->heap_statistics_buffer =
      heap_statistics_buffer.Serialize(context, creator);
  internal_field_info_->heap_space_statistics_buffer =
      heap_space_statistics_buffer.Serialize(context, creator);
  internal_field_info_->heap_code_statistics_buffer =
      heap_code_statistics_buffer.Serialize(context, creator);
  // Return true because we need to maintain the reference to the binding from
  // JS land.
  return true;
}

void BindingData::Deserialize(Local<Context> context,
                              Local<Object> holder,
                              int index,
                              InternalFieldInfoBase* info) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  HandleScope scope(context->GetIsolate());
  Realm* realm = Realm::GetCurrent(context);
  // Recreate the buffer in the constructor.
  InternalFieldInfo* casted_info = static_cast<InternalFieldInfo*>(info);
  BindingData* binding =
      realm->AddBindingData<BindingData>(holder, casted_info);
  CHECK_NOT_NULL(binding);
}

InternalFieldInfoBase* BindingData::Serialize(int index) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  InternalFieldInfo* info = internal_field_info_;
  internal_field_info_ = nullptr;
  return info;
}

void BindingData::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("heap_statistics_buffer", heap_statistics_buffer);
  tracker->TrackField("heap_space_statistics_buffer",
                      heap_space_statistics_buffer);
  tracker->TrackField("heap_code_statistics_buffer",
                      heap_code_statistics_buffer);
}

void CachedDataVersionTag(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Integer> result =
      Integer::NewFromUnsigned(env->isolate(),
                               ScriptCompiler::CachedDataVersionTag());
  args.GetReturnValue().Set(result);
}

void SetHeapSnapshotNearHeapLimit(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsUint32());
  Environment* env = Environment::GetCurrent(args);
  uint32_t limit = args[0].As<v8::Uint32>()->Value();
  CHECK_GT(limit, 0);
  env->AddHeapSnapshotNearHeapLimitCallback();
  env->set_heap_snapshot_near_heap_limit(limit);
}

void UpdateHeapStatisticsBuffer(const FunctionCallbackInfo<Value>& args) {
  BindingData* data = Realm::GetBindingData<BindingData>(args);
  HeapStatistics s;
  args.GetIsolate()->GetHeapStatistics(&s);
  AliasedFloat64Array& buffer = data->heap_statistics_buffer;
#define V(index, name, _) buffer[index] = static_cast<double>(s.name());
  HEAP_STATISTICS_PROPERTIES(V)
#undef V
}


void UpdateHeapSpaceStatisticsBuffer(const FunctionCallbackInfo<Value>& args) {
  BindingData* data = Realm::GetBindingData<BindingData>(args);
  HeapSpaceStatistics s;
  Isolate* const isolate = args.GetIsolate();
  CHECK(args[0]->IsUint32());
  size_t space_index = static_cast<size_t>(args[0].As<v8::Uint32>()->Value());
  isolate->GetHeapSpaceStatistics(&s, space_index);

  AliasedFloat64Array& buffer = data->heap_space_statistics_buffer;

#define V(index, name, _) buffer[index] = static_cast<double>(s.name());
  HEAP_SPACE_STATISTICS_PROPERTIES(V)
#undef V
}

void UpdateHeapCodeStatisticsBuffer(const FunctionCallbackInfo<Value>& args) {
  BindingData* data = Realm::GetBindingData<BindingData>(args);
  HeapCodeStatistics s;
  args.GetIsolate()->GetHeapCodeAndMetadataStatistics(&s);
  AliasedFloat64Array& buffer = data->heap_code_statistics_buffer;

#define V(index, name, _) buffer[index] = static_cast<double>(s.name());
  HEAP_CODE_STATISTICS_PROPERTIES(V)
#undef V
}


void SetFlagsFromString(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsString());
  String::Utf8Value flags(args.GetIsolate(), args[0]);
  V8::SetFlagsFromString(*flags, static_cast<size_t>(flags.length()));
}

static const char* GetGCTypeName(v8::GCType gc_type) {
  switch (gc_type) {
    case v8::GCType::kGCTypeScavenge:
      return "Scavenge";
    case v8::GCType::kGCTypeMarkSweepCompact:
      return "MarkSweepCompact";
    case v8::GCType::kGCTypeIncrementalMarking:
      return "IncrementalMarking";
    case v8::GCType::kGCTypeProcessWeakCallbacks:
      return "ProcessWeakCallbacks";
    default:
      return "Unknown";
  }
}

static void SetHeapStatistics(JSONWriter* writer, Isolate* isolate) {
  HeapStatistics heap_statistics;
  isolate->GetHeapStatistics(&heap_statistics);
  writer->json_objectstart("heapStatistics");
  writer->json_keyvalue("totalHeapSize", heap_statistics.total_heap_size());
  writer->json_keyvalue("totalHeapSizeExecutable",
                        heap_statistics.total_heap_size_executable());
  writer->json_keyvalue("totalPhysicalSize",
                        heap_statistics.total_physical_size());
  writer->json_keyvalue("totalAvailableSize",
                        heap_statistics.total_available_size());
  writer->json_keyvalue("totalGlobalHandlesSize",
                        heap_statistics.total_global_handles_size());
  writer->json_keyvalue("usedGlobalHandlesSize",
                        heap_statistics.used_global_handles_size());
  writer->json_keyvalue("usedHeapSize", heap_statistics.used_heap_size());
  writer->json_keyvalue("heapSizeLimit", heap_statistics.heap_size_limit());
  writer->json_keyvalue("mallocedMemory", heap_statistics.malloced_memory());
  writer->json_keyvalue("externalMemory", heap_statistics.external_memory());
  writer->json_keyvalue("peakMallocedMemory",
                        heap_statistics.peak_malloced_memory());
  writer->json_objectend();

  int space_count = isolate->NumberOfHeapSpaces();
  writer->json_arraystart("heapSpaceStatistics");
  for (int i = 0; i < space_count; i++) {
    HeapSpaceStatistics heap_space_statistics;
    isolate->GetHeapSpaceStatistics(&heap_space_statistics, i);
    writer->json_start();
    writer->json_keyvalue("spaceName", heap_space_statistics.space_name());
    writer->json_keyvalue("spaceSize", heap_space_statistics.space_size());
    writer->json_keyvalue("spaceUsedSize",
                          heap_space_statistics.space_used_size());
    writer->json_keyvalue("spaceAvailableSize",
                          heap_space_statistics.space_available_size());
    writer->json_keyvalue("physicalSpaceSize",
                          heap_space_statistics.physical_space_size());
    writer->json_end();
  }
  writer->json_arrayend();
}

static void BeforeGCCallback(Isolate* isolate,
                             v8::GCType gc_type,
                             v8::GCCallbackFlags flags,
                             void* data) {
  GCProfiler* profiler = static_cast<GCProfiler*>(data);
  if (profiler->current_gc_type != 0) {
    return;
  }
  JSONWriter* writer = profiler->writer();
  writer->json_start();
  writer->json_keyvalue("gcType", GetGCTypeName(gc_type));
  writer->json_objectstart("beforeGC");
  SetHeapStatistics(writer, isolate);
  writer->json_objectend();
  profiler->current_gc_type = gc_type;
  profiler->start_time = uv_hrtime();
}

static void AfterGCCallback(Isolate* isolate,
                            v8::GCType gc_type,
                            v8::GCCallbackFlags flags,
                            void* data) {
  GCProfiler* profiler = static_cast<GCProfiler*>(data);
  if (profiler->current_gc_type != gc_type) {
    return;
  }
  JSONWriter* writer = profiler->writer();
  profiler->current_gc_type = 0;
  writer->json_keyvalue("cost", (uv_hrtime() - profiler->start_time) / 1e3);
  profiler->start_time = 0;
  writer->json_objectstart("afterGC");
  SetHeapStatistics(writer, isolate);
  writer->json_objectend();
  writer->json_end();
}

GCProfiler::GCProfiler(Environment* env, Local<Object> object)
    : BaseObject(env, object),
      start_time(0),
      current_gc_type(0),
      state(GCProfilerState::kInitialized),
      writer_(out_stream_, false) {
  MakeWeak();
}

// This function will be called when
// 1. StartGCProfile and StopGCProfile are called and
//    JS land does not keep the object anymore.
// 2. StartGCProfile is called then the env exits before
//    StopGCProfile is called.
GCProfiler::~GCProfiler() {
  if (state != GCProfiler::GCProfilerState::kInitialized) {
    env()->isolate()->RemoveGCPrologueCallback(BeforeGCCallback, this);
    env()->isolate()->RemoveGCEpilogueCallback(AfterGCCallback, this);
  }
}

JSONWriter* GCProfiler::writer() {
  return &writer_;
}

std::ostringstream* GCProfiler::out_stream() {
  return &out_stream_;
}

void GCProfiler::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);
  new GCProfiler(env, args.This());
}

void GCProfiler::Start(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  GCProfiler* profiler;
  ASSIGN_OR_RETURN_UNWRAP(&profiler, args.This());
  if (profiler->state != GCProfiler::GCProfilerState::kInitialized) {
    return;
  }
  profiler->writer()->json_start();
  profiler->writer()->json_keyvalue("version", 1);

  uv_timeval64_t ts;
  if (uv_gettimeofday(&ts) == 0) {
    profiler->writer()->json_keyvalue("startTime",
                                      ts.tv_sec * 1000 + ts.tv_usec / 1000);
  } else {
    profiler->writer()->json_keyvalue("startTime", 0);
  }
  profiler->writer()->json_arraystart("statistics");
  env->isolate()->AddGCPrologueCallback(BeforeGCCallback,
                                        static_cast<void*>(profiler));
  env->isolate()->AddGCEpilogueCallback(AfterGCCallback,
                                        static_cast<void*>(profiler));
  profiler->state = GCProfiler::GCProfilerState::kStarted;
}

void GCProfiler::Stop(const FunctionCallbackInfo<v8::Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  GCProfiler* profiler;
  ASSIGN_OR_RETURN_UNWRAP(&profiler, args.This());
  if (profiler->state != GCProfiler::GCProfilerState::kStarted) {
    return;
  }
  profiler->writer()->json_arrayend();
  uv_timeval64_t ts;
  if (uv_gettimeofday(&ts) == 0) {
    profiler->writer()->json_keyvalue("endTime",
                                      ts.tv_sec * 1000 + ts.tv_usec / 1000);
  } else {
    profiler->writer()->json_keyvalue("endTime", 0);
  }
  profiler->writer()->json_end();
  profiler->state = GCProfiler::GCProfilerState::kStopped;
  auto string = profiler->out_stream()->str();
  args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(),
                                                string.data(),
                                                v8::NewStringType::kNormal,
                                                string.size())
                                .ToLocalChecked());
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  Environment* env = realm->env();
  BindingData* const binding_data = realm->AddBindingData<BindingData>(target);
  if (binding_data == nullptr) return;

  SetMethodNoSideEffect(
      context, target, "cachedDataVersionTag", CachedDataVersionTag);
  SetMethodNoSideEffect(context,
                        target,
                        "setHeapSnapshotNearHeapLimit",
                        SetHeapSnapshotNearHeapLimit);
  SetMethod(context,
            target,
            "updateHeapStatisticsBuffer",
            UpdateHeapStatisticsBuffer);

  SetMethod(context,
            target,
            "updateHeapCodeStatisticsBuffer",
            UpdateHeapCodeStatisticsBuffer);

  size_t number_of_heap_spaces = env->isolate()->NumberOfHeapSpaces();

  // Heap space names are extracted once and exposed to JavaScript to
  // avoid excessive creation of heap space name Strings.
  HeapSpaceStatistics s;
  MaybeStackBuffer<Local<Value>, 16> heap_spaces(number_of_heap_spaces);
  for (size_t i = 0; i < number_of_heap_spaces; i++) {
    env->isolate()->GetHeapSpaceStatistics(&s, i);
    heap_spaces[i] = String::NewFromUtf8(env->isolate(), s.space_name())
                                             .ToLocalChecked();
  }
  target
      ->Set(
          context,
          FIXED_ONE_BYTE_STRING(env->isolate(), "kHeapSpaces"),
          Array::New(env->isolate(), heap_spaces.out(), number_of_heap_spaces))
      .Check();

  SetMethod(context,
            target,
            "updateHeapSpaceStatisticsBuffer",
            UpdateHeapSpaceStatisticsBuffer);

#define V(i, _, name)                                                          \
  target                                                                       \
      ->Set(context,                                                           \
            FIXED_ONE_BYTE_STRING(env->isolate(), #name),                      \
            Uint32::NewFromUnsigned(env->isolate(), i))                        \
      .Check();

  HEAP_STATISTICS_PROPERTIES(V)
  HEAP_CODE_STATISTICS_PROPERTIES(V)
  HEAP_SPACE_STATISTICS_PROPERTIES(V)
#undef V

  // Export symbols used by v8.setFlagsFromString()
  SetMethod(context, target, "setFlagsFromString", SetFlagsFromString);

  // GCProfiler
  Local<FunctionTemplate> t =
      NewFunctionTemplate(env->isolate(), GCProfiler::New);
  t->InstanceTemplate()->SetInternalFieldCount(BaseObject::kInternalFieldCount);
  SetProtoMethod(env->isolate(), t, "start", GCProfiler::Start);
  SetProtoMethod(env->isolate(), t, "stop", GCProfiler::Stop);
  SetConstructorFunction(context, target, "GCProfiler", t);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(CachedDataVersionTag);
  registry->Register(UpdateHeapStatisticsBuffer);
  registry->Register(UpdateHeapCodeStatisticsBuffer);
  registry->Register(UpdateHeapSpaceStatisticsBuffer);
  registry->Register(SetFlagsFromString);
  registry->Register(SetHeapSnapshotNearHeapLimit);
  registry->Register(GCProfiler::New);
  registry->Register(GCProfiler::Start);
  registry->Register(GCProfiler::Stop);
}

}  // namespace v8_utils
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(v8, node::v8_utils::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(v8, node::v8_utils::RegisterExternalReferences)
