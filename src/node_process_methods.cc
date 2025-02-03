#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node.h"
#include "node_dotenv.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "node_process-inl.h"
#include "path.h"
#include "util-inl.h"
#include "uv.h"
#include "v8-fast-api-calls.h"
#include "v8.h"

#include <vector>

#if HAVE_INSPECTOR
#include "inspector_io.h"
#endif

#include <climits>  // PATH_MAX
#include <cstdio>

#if defined(_MSC_VER)
#include <direct.h>
#include <io.h>
#define umask _umask
typedef int mode_t;
#else
#include <pthread.h>
#include <sys/resource.h>  // getrlimit, setrlimit
#include <termios.h>  // tcgetattr, tcsetattr
#endif

namespace node {

using v8::Array;
using v8::ArrayBuffer;
using v8::CFunction;
using v8::Context;
using v8::Float64Array;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::HeapStatistics;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Maybe;
using v8::NewStringType;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Uint32;
using v8::Value;

namespace per_process {
Mutex umask_mutex;
}   // namespace per_process

// Microseconds in a second, as a float, used in CPUUsage() below
#define MICROS_PER_SEC 1e6
// used in Hrtime() and Uptime() below
#define NANOS_PER_SEC 1000000000

static void Abort(const FunctionCallbackInfo<Value>& args) {
  ABORT();
}

// For internal testing only, not exposed to userland.
static void CauseSegfault(const FunctionCallbackInfo<Value>& args) {
  // This should crash hard all platforms.
  void* volatile* d = static_cast<void* volatile*>(nullptr);
  *d = nullptr;
}

static void Chdir(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->owns_process_state());

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value path(env->isolate(), args[0]);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, path.ToStringView());
  int err = uv_chdir(*path);
  if (err) {
    // Also include the original working directory, since that will usually
    // be helpful information when debugging a `chdir()` failure.
    char buf[PATH_MAX_BYTES];
    size_t cwd_len = sizeof(buf);
    uv_cwd(buf, &cwd_len);
    return env->ThrowUVException(err, "chdir", nullptr, buf, *path);
  }
}

inline Local<ArrayBuffer> get_fields_array_buffer(
    const FunctionCallbackInfo<Value>& args,
    size_t index,
    size_t array_length) {
  CHECK(args[index]->IsFloat64Array());
  Local<Float64Array> arr = args[index].As<Float64Array>();
  CHECK_EQ(arr->Length(), array_length);
  return arr->Buffer();
}

// CPUUsage use libuv's uv_getrusage() this-process resource usage accessor,
// to access ru_utime (user CPU time used) and ru_stime (system CPU time used),
// which are uv_timeval_t structs (long tv_sec, long tv_usec).
// Returns those values as Float64 microseconds in the elements of the array
// passed to the function.
static void CPUUsage(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  uv_rusage_t rusage;

  // Call libuv to get the values we'll return.
  int err = uv_getrusage(&rusage);
  if (err)
    return env->ThrowUVException(err, "uv_getrusage");

  // Get the double array pointer from the Float64Array argument.
  Local<ArrayBuffer> ab = get_fields_array_buffer(args, 0, 2);
  double* fields = static_cast<double*>(ab->Data());

  // Set the Float64Array elements to be user / system values in microseconds.
  fields[0] = MICROS_PER_SEC * rusage.ru_utime.tv_sec + rusage.ru_utime.tv_usec;
  fields[1] = MICROS_PER_SEC * rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec;
}

static void Cwd(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->has_run_bootstrapping_code());
  char buf[PATH_MAX_BYTES];
  size_t cwd_len = sizeof(buf);
  int err = uv_cwd(buf, &cwd_len);
  if (err)
    return env->ThrowUVException(err, "uv_cwd");

  Local<String> cwd = String::NewFromUtf8(env->isolate(),
                                          buf,
                                          NewStringType::kNormal,
                                          cwd_len).ToLocalChecked();
  args.GetReturnValue().Set(cwd);
}

static void Kill(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Context> context = env->context();

  if (args.Length() < 2) {
    THROW_ERR_MISSING_ARGS(env, "Bad argument.");
  }

  int pid;
  if (!args[0]->Int32Value(context).To(&pid)) return;
  int sig;
  if (!args[1]->Int32Value(context).To(&sig)) return;

  uv_pid_t own_pid = uv_os_getpid();
  if (sig > 0 &&
      (pid == 0 || pid == -1 || pid == own_pid || pid == -own_pid) &&
      !HasSignalJSHandler(sig)) {
    // This is most likely going to terminate this process.
    // It's not an exact method but it might be close enough.
    RunAtExit(env);
  }

  int err = uv_kill(pid, sig);
  args.GetReturnValue().Set(err);
}

static void Rss(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  size_t rss;
  int err = uv_resident_set_memory(&rss);
  if (err)
    return env->ThrowUVException(err, "uv_resident_set_memory");

  args.GetReturnValue().Set(static_cast<double>(rss));
}

static void MemoryUsage(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  Isolate* isolate = env->isolate();
  // V8 memory usage
  HeapStatistics v8_heap_stats;
  isolate->GetHeapStatistics(&v8_heap_stats);

  NodeArrayBufferAllocator* array_buffer_allocator =
      env->isolate_data()->node_allocator();

  // Get the double array pointer from the Float64Array argument.
  Local<ArrayBuffer> ab = get_fields_array_buffer(args, 0, 5);
  double* fields = static_cast<double*>(ab->Data());

  size_t rss;
  int err = uv_resident_set_memory(&rss);
  if (err)
    return env->ThrowUVException(err, "uv_resident_set_memory");

  fields[0] = static_cast<double>(rss);
  fields[1] = static_cast<double>(v8_heap_stats.total_heap_size());
  fields[2] = static_cast<double>(v8_heap_stats.used_heap_size());
  fields[3] = static_cast<double>(v8_heap_stats.external_memory());
  fields[4] =
      array_buffer_allocator == nullptr
          ? 0
          : static_cast<double>(array_buffer_allocator->total_mem_usage());
}

static void GetConstrainedMemory(const FunctionCallbackInfo<Value>& args) {
  uint64_t value = uv_get_constrained_memory();
  args.GetReturnValue().Set(static_cast<double>(value));
}

static void GetAvailableMemory(const FunctionCallbackInfo<Value>& args) {
  uint64_t value = uv_get_available_memory();
  args.GetReturnValue().Set(static_cast<double>(value));
}

void RawDebug(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.Length() == 1 && args[0]->IsString() &&
        "must be called with a single string");
  Utf8Value message(args.GetIsolate(), args[0]);
  FPrintF(stderr, "%s\n", message);
  fflush(stderr);
}

static void Umask(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->has_run_bootstrapping_code());
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsUndefined() || args[0]->IsUint32());
  Mutex::ScopedLock scoped_lock(per_process::umask_mutex);

  uint32_t old;
  if (args[0]->IsUndefined()) {
    old = umask(0);
    umask(static_cast<mode_t>(old));
  } else {
    int oct = args[0].As<Uint32>()->Value();
    old = umask(static_cast<mode_t>(oct));
  }

  args.GetReturnValue().Set(old);
}

static void Uptime(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  uv_update_time(env->event_loop());
  double uptime =
      static_cast<double>(uv_hrtime() - per_process::node_start_time);
  Local<Number> result = Number::New(env->isolate(), uptime / NANOS_PER_SEC);
  args.GetReturnValue().Set(result);
}

static void GetActiveRequests(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  std::vector<Local<Value>> request_v;
  for (ReqWrapBase* req_wrap : *env->req_wrap_queue()) {
    AsyncWrap* w = req_wrap->GetAsyncWrap();
    if (w->persistent().IsEmpty())
      continue;
    request_v.emplace_back(w->GetOwner());
  }

  args.GetReturnValue().Set(
      Array::New(env->isolate(), request_v.data(), request_v.size()));
}

// Non-static, friend of HandleWrap. Could have been a HandleWrap method but
// implemented here for consistency with GetActiveRequests().
void GetActiveHandles(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  std::vector<Local<Value>> handle_v;
  for (auto w : *env->handle_wrap_queue()) {
    if (!HandleWrap::HasRef(w))
      continue;
    handle_v.emplace_back(w->GetOwner());
  }
  args.GetReturnValue().Set(
      Array::New(env->isolate(), handle_v.data(), handle_v.size()));
}

static void GetActiveResourcesInfo(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  std::vector<Local<Value>> resources_info;

  // Active requests
  for (ReqWrapBase* req_wrap : *env->req_wrap_queue()) {
    AsyncWrap* w = req_wrap->GetAsyncWrap();
    if (w->persistent().IsEmpty()) continue;
    resources_info.emplace_back(
        OneByteString(env->isolate(), w->MemoryInfoName()));
  }

  // Active handles
  for (HandleWrap* w : *env->handle_wrap_queue()) {
    if (w->persistent().IsEmpty() || !HandleWrap::HasRef(w)) continue;
    resources_info.emplace_back(
        OneByteString(env->isolate(), w->MemoryInfoName()));
  }

  // Active timeouts
  resources_info.insert(resources_info.end(),
                        env->timeout_info()[0],
                        FIXED_ONE_BYTE_STRING(env->isolate(), "Timeout"));

  // Active immediates
  resources_info.insert(resources_info.end(),
                        env->immediate_info()->ref_count(),
                        FIXED_ONE_BYTE_STRING(env->isolate(), "Immediate"));

  args.GetReturnValue().Set(
      Array::New(env->isolate(), resources_info.data(), resources_info.size()));
}

static void ResourceUsage(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  uv_rusage_t rusage;
  int err = uv_getrusage(&rusage);
  if (err)
    return env->ThrowUVException(err, "uv_getrusage");

  Local<ArrayBuffer> ab = get_fields_array_buffer(args, 0, 16);
  double* fields = static_cast<double*>(ab->Data());

  fields[0] = MICROS_PER_SEC * rusage.ru_utime.tv_sec + rusage.ru_utime.tv_usec;
  fields[1] = MICROS_PER_SEC * rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec;
  fields[2] = static_cast<double>(rusage.ru_maxrss);
  fields[3] = static_cast<double>(rusage.ru_ixrss);
  fields[4] = static_cast<double>(rusage.ru_idrss);
  fields[5] = static_cast<double>(rusage.ru_isrss);
  fields[6] = static_cast<double>(rusage.ru_minflt);
  fields[7] = static_cast<double>(rusage.ru_majflt);
  fields[8] = static_cast<double>(rusage.ru_nswap);
  fields[9] = static_cast<double>(rusage.ru_inblock);
  fields[10] = static_cast<double>(rusage.ru_oublock);
  fields[11] = static_cast<double>(rusage.ru_msgsnd);
  fields[12] = static_cast<double>(rusage.ru_msgrcv);
  fields[13] = static_cast<double>(rusage.ru_nsignals);
  fields[14] = static_cast<double>(rusage.ru_nvcsw);
  fields[15] = static_cast<double>(rusage.ru_nivcsw);
}

#ifdef __POSIX__
static void DebugProcess(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  if (args.Length() < 1) {
    return THROW_ERR_MISSING_ARGS(env, "Invalid number of arguments.");
  }

  CHECK(args[0]->IsNumber());
  pid_t pid = args[0].As<Integer>()->Value();
  int r = kill(pid, SIGUSR1);

  if (r != 0) {
    return env->ThrowErrnoException(errno, "kill");
  }
}
#endif  // __POSIX__

#ifdef _WIN32
static int GetDebugSignalHandlerMappingName(DWORD pid,
                                            wchar_t* buf,
                                            size_t buf_len) {
  return _snwprintf(buf, buf_len, L"node-debug-handler-%u", pid);
}

static void DebugProcess(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();

  if (args.Length() < 1) {
    return THROW_ERR_MISSING_ARGS(env, "Invalid number of arguments.");
  }

  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  HANDLE mapping = nullptr;
  wchar_t mapping_name[32];
  LPTHREAD_START_ROUTINE* handler = nullptr;
  DWORD pid = 0;

  auto cleanup = OnScopeLeave([&]() {
    if (process != nullptr) CloseHandle(process);
    if (thread != nullptr) CloseHandle(thread);
    if (handler != nullptr) UnmapViewOfFile(handler);
    if (mapping != nullptr) CloseHandle(mapping);
  });

  CHECK(args[0]->IsNumber());
  pid = static_cast<DWORD>(args[0].As<Integer>()->Value());

  process =
      OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                      PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                  FALSE,
                  pid);
  if (process == nullptr) {
    isolate->ThrowException(
        WinapiErrnoException(isolate, GetLastError(), "OpenProcess"));
    return;
  }

  if (GetDebugSignalHandlerMappingName(
          pid, mapping_name, arraysize(mapping_name)) < 0) {
    env->ThrowErrnoException(errno, "sprintf");
    return;
  }

  mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name);
  if (mapping == nullptr) {
    isolate->ThrowException(
        WinapiErrnoException(isolate, GetLastError(), "OpenFileMappingW"));
    return;
  }

  handler = reinterpret_cast<LPTHREAD_START_ROUTINE*>(
      MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof *handler));
  if (handler == nullptr || *handler == nullptr) {
    isolate->ThrowException(
        WinapiErrnoException(isolate, GetLastError(), "MapViewOfFile"));
    return;
  }

  thread =
      CreateRemoteThread(process, nullptr, 0, *handler, nullptr, 0, nullptr);
  if (thread == nullptr) {
    isolate->ThrowException(
        WinapiErrnoException(isolate, GetLastError(), "CreateRemoteThread"));
    return;
  }

  // Wait for the thread to terminate
  if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
    isolate->ThrowException(
        WinapiErrnoException(isolate, GetLastError(), "WaitForSingleObject"));
    return;
  }
}
#endif  // _WIN32

static void DebugEnd(const FunctionCallbackInfo<Value>& args) {
#if HAVE_INSPECTOR
  Environment* env = Environment::GetCurrent(args);
  if (env->inspector_agent()->IsListening()) {
    env->inspector_agent()->Stop();
  }
#endif
}

static void ReallyExit(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  RunAtExit(env);
  ExitCode code = ExitCode::kNoFailure;
  Maybe<int32_t> code_int = args[0]->Int32Value(env->context());
  if (!code_int.IsNothing()) {
    code = static_cast<ExitCode>(code_int.FromJust());
  }
  env->Exit(code);
}

static void LoadEnvFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  std::string path = ".env";
  if (args.Length() == 1) {
    BufferValue path_value(args.GetIsolate(), args[0]);
    ToNamespacedPath(env, &path_value);
    path = path_value.ToString();
  }

  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, path);

  Dotenv dotenv{};

  switch (dotenv.ParsePath(path)) {
    case dotenv.ParseResult::Valid: {
      dotenv.SetEnvironment(env);
      break;
    }
    case dotenv.ParseResult::InvalidContent: {
      THROW_ERR_INVALID_ARG_TYPE(
          env, "Contents of '%s' should be a valid string.", path.c_str());
      break;
    }
    case dotenv.ParseResult::FileError: {
      env->ThrowUVException(UV_ENOENT, "open", nullptr, path.c_str());
      break;
    }
    default:
      UNREACHABLE();
  }
}

namespace process {

BindingData::BindingData(Realm* realm,
                         v8::Local<v8::Object> object,
                         InternalFieldInfo* info)
    : SnapshotableObject(realm, object, type_int),
      hrtime_buffer_(realm->isolate(),
                     kHrTimeBufferLength,
                     MAYBE_FIELD_PTR(info, hrtime_buffer)) {
  Isolate* isolate = realm->isolate();
  Local<Context> context = realm->context();

  if (info == nullptr) {
    object
        ->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "hrtimeBuffer"),
              hrtime_buffer_.GetJSArray())
        .ToChecked();
  } else {
    hrtime_buffer_.Deserialize(realm->context());
  }

  // The hrtime buffer is referenced from the binding data js object.
  // Make the native handle weak to avoid keeping the realm alive.
  hrtime_buffer_.MakeWeak();
}

v8::CFunction BindingData::fast_number_(v8::CFunction::Make(FastNumber));
v8::CFunction BindingData::fast_bigint_(v8::CFunction::Make(FastBigInt));

void BindingData::AddMethods(Isolate* isolate, Local<ObjectTemplate> target) {
  SetFastMethodNoSideEffect(
      isolate, target, "hrtime", SlowNumber, &fast_number_);
  SetFastMethodNoSideEffect(
      isolate, target, "hrtimeBigInt", SlowBigInt, &fast_bigint_);
}

void BindingData::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(SlowNumber);
  registry->Register(SlowBigInt);
  registry->Register(FastNumber);
  registry->Register(FastBigInt);
  registry->Register(fast_number_.GetTypeInfo());
  registry->Register(fast_bigint_.GetTypeInfo());
}

BindingData* BindingData::FromV8Value(Local<Value> value) {
  Local<Object> v8_object = value.As<Object>();
  return static_cast<BindingData*>(
      v8_object->GetAlignedPointerFromInternalField(BaseObject::kSlot));
}

void BindingData::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("hrtime_buffer", hrtime_buffer_);
}

// This is the legacy version of hrtime before BigInt was introduced in
// JavaScript.
// The value returned by uv_hrtime() is a 64-bit int representing nanoseconds,
// so this function instead fills in an Uint32Array with 3 entries,
// to avoid any integer overflow possibility.
// The first two entries contain the second part of the value
// broken into the upper/lower 32 bits to be converted back in JS,
// because there is no Uint64Array in JS.
// The third entry contains the remaining nanosecond part of the value.
void BindingData::NumberImpl(BindingData* receiver) {
  uint64_t t = uv_hrtime();
  receiver->hrtime_buffer_[0] = (t / NANOS_PER_SEC) >> 32;
  receiver->hrtime_buffer_[1] = (t / NANOS_PER_SEC) & 0xffffffff;
  receiver->hrtime_buffer_[2] = t % NANOS_PER_SEC;
}

void BindingData::BigIntImpl(BindingData* receiver) {
  uint64_t t = uv_hrtime();
  // The buffer is a Uint32Array, so we need to reinterpret it as a
  // Uint64Array to write the value. The buffer is valid at this scope so we
  // can safely cast away the constness.
  uint64_t* fields = reinterpret_cast<uint64_t*>(
      const_cast<uint32_t*>(receiver->hrtime_buffer_.GetNativeBuffer()));
  fields[0] = t;
}

void BindingData::SlowBigInt(const FunctionCallbackInfo<Value>& args) {
  BigIntImpl(FromJSObject<BindingData>(args.This()));
}

void BindingData::SlowNumber(const v8::FunctionCallbackInfo<v8::Value>& args) {
  NumberImpl(FromJSObject<BindingData>(args.This()));
}

bool BindingData::PrepareForSerialization(Local<Context> context,
                                          v8::SnapshotCreator* creator) {
  DCHECK_NULL(internal_field_info_);
  internal_field_info_ = InternalFieldInfoBase::New<InternalFieldInfo>(type());
  internal_field_info_->hrtime_buffer =
      hrtime_buffer_.Serialize(context, creator);
  // Return true because we need to maintain the reference to the binding from
  // JS land.
  return true;
}

InternalFieldInfoBase* BindingData::Serialize(int index) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  InternalFieldInfo* info = internal_field_info_;
  internal_field_info_ = nullptr;
  return info;
}

void BindingData::Deserialize(Local<Context> context,
                              Local<Object> holder,
                              int index,
                              InternalFieldInfoBase* info) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  v8::HandleScope scope(context->GetIsolate());
  Realm* realm = Realm::GetCurrent(context);
  // Recreate the buffer in the constructor.
  InternalFieldInfo* casted_info = static_cast<InternalFieldInfo*>(info);
  BindingData* binding =
      realm->AddBindingData<BindingData>(holder, casted_info);
  CHECK_NOT_NULL(binding);
}

static void SetEmitWarningSync(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsFunction());
  Environment* env = Environment::GetCurrent(args);
  env->set_process_emit_warning_sync(args[0].As<Function>());
}

static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                       Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  BindingData::AddMethods(isolate, target);
  // define various internal methods
  SetMethod(isolate, target, "_debugProcess", DebugProcess);
  SetMethod(isolate, target, "abort", Abort);
  SetMethod(isolate, target, "causeSegfault", CauseSegfault);
  SetMethod(isolate, target, "chdir", Chdir);

  SetMethod(isolate, target, "umask", Umask);
  SetMethod(isolate, target, "memoryUsage", MemoryUsage);
  SetMethod(isolate, target, "constrainedMemory", GetConstrainedMemory);
  SetMethod(isolate, target, "availableMemory", GetAvailableMemory);
  SetMethod(isolate, target, "rss", Rss);
  SetMethod(isolate, target, "cpuUsage", CPUUsage);
  SetMethod(isolate, target, "resourceUsage", ResourceUsage);

  SetMethod(isolate, target, "_debugEnd", DebugEnd);
  SetMethod(isolate, target, "_getActiveRequests", GetActiveRequests);
  SetMethod(isolate, target, "_getActiveHandles", GetActiveHandles);
  SetMethod(isolate, target, "getActiveResourcesInfo", GetActiveResourcesInfo);
  SetMethod(isolate, target, "_kill", Kill);
  SetMethod(isolate, target, "_rawDebug", RawDebug);

  SetMethodNoSideEffect(isolate, target, "cwd", Cwd);
  SetMethod(isolate, target, "dlopen", binding::DLOpen);
  SetMethod(isolate, target, "reallyExit", ReallyExit);
  SetMethodNoSideEffect(isolate, target, "uptime", Uptime);
  SetMethod(isolate, target, "patchProcessObject", PatchProcessObject);

  SetMethod(isolate, target, "loadEnvFile", LoadEnvFile);

  SetMethod(isolate, target, "setEmitWarningSync", SetEmitWarningSync);
}

static void CreatePerContextProperties(Local<Object> target,
                                       Local<Value> unused,
                                       Local<Context> context,
                                       void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  realm->AddBindingData<BindingData>(target);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  BindingData::RegisterExternalReferences(registry);

  registry->Register(DebugProcess);
  registry->Register(DebugEnd);
  registry->Register(Abort);
  registry->Register(CauseSegfault);
  registry->Register(Chdir);

  registry->Register(Umask);
  registry->Register(RawDebug);
  registry->Register(MemoryUsage);
  registry->Register(GetConstrainedMemory);
  registry->Register(GetAvailableMemory);
  registry->Register(Rss);
  registry->Register(CPUUsage);
  registry->Register(ResourceUsage);

  registry->Register(GetActiveRequests);
  registry->Register(GetActiveHandles);
  registry->Register(GetActiveResourcesInfo);
  registry->Register(Kill);

  registry->Register(Cwd);
  registry->Register(binding::DLOpen);
  registry->Register(ReallyExit);
  registry->Register(Uptime);
  registry->Register(PatchProcessObject);

  registry->Register(LoadEnvFile);

  registry->Register(SetEmitWarningSync);
}

}  // namespace process
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(process_methods,
                                    node::process::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(process_methods,
                              node::process::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(process_methods,
                                node::process::RegisterExternalReferences)
