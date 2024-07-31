// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "test/cctest/cctest.h"

#include "include/cppgc/platform.h"
#include "include/libplatform/libplatform.h"
#include "include/v8-array-buffer.h"
#include "include/v8-context.h"
#include "include/v8-function.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-locker.h"
#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "src/base/strings.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/init/v8.h"
#ifdef V8_ENABLE_TURBOFAN
#include "src/compiler/pipeline.h"
#endif  // V8_ENABLE_TURBOFAN
#include "src/flags/flags.h"
#include "src/objects/objects-inl.h"
#include "src/trap-handler/trap-handler.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/cctest/print-extension.h"
#include "test/cctest/profiler-extension.h"
#include "test/cctest/trace-extension.h"

#ifdef V8_USE_PERFETTO
#include "src/tracing/trace-event.h"
#endif  // V8_USE_PERFETTO

#if V8_OS_WIN
#include <windows.h>
#if V8_CC_MSVC
#include <crtdbg.h>
#endif
#endif

enum InitializationState { kUnset, kUninitialized, kInitialized };
static InitializationState initialization_state_ = kUnset;

static v8::base::LazyInstance<CcTestMapType>::type g_cctests =
    LAZY_INSTANCE_INITIALIZER;

std::unordered_map<std::string, CcTest*>* tests_ =
    new std::unordered_map<std::string, CcTest*>();
bool CcTest::initialize_called_ = false;
v8::base::Atomic32 CcTest::isolate_used_ = 0;
v8::ArrayBuffer::Allocator* CcTest::allocator_ = nullptr;
v8::Isolate* CcTest::isolate_ = nullptr;
v8::Platform* CcTest::default_platform_ = nullptr;

CcTest::CcTest(TestFunction* callback, const char* file, const char* name,
               bool enabled, bool initialize,
               TestPlatformFactory* test_platform_factory)
    : callback_(callback),
      initialize_(initialize),
      test_platform_factory_(test_platform_factory) {
  // Find the base name of this test (const_cast required on Windows).
  char *basename = strrchr(const_cast<char *>(file), '/');
  if (!basename) {
    basename = strrchr(const_cast<char *>(file), '\\');
  }
  if (!basename) {
    basename = v8::internal::StrDup(file);
  } else {
    basename = v8::internal::StrDup(basename + 1);
  }
  // Drop the extension, if there is one.
  char *extension = strrchr(basename, '.');
  if (extension) *extension = 0;
  // Install this test in the list of tests

  if (enabled) {
    auto it =
        g_cctests.Pointer()->emplace(std::string(basename) + "/" + name, this);
    CHECK_WITH_MSG(it.second, "Test with same name already exists");
  }
  v8::internal::DeleteArray(basename);
}

void CcTest::Run(const char* snapshot_directory) {
  v8::V8::InitializeICUDefaultLocation(snapshot_directory);
  std::unique_ptr<v8::Platform> underlying_default_platform(
      v8::platform::NewDefaultPlatform());
  default_platform_ = underlying_default_platform.get();
  std::unique_ptr<v8::Platform> platform;
  if (test_platform_factory_) {
    platform = test_platform_factory_();
  } else {
    platform = std::move(underlying_default_platform);
  }
  i::V8::InitializePlatformForTesting(platform.get());
  cppgc::InitializeProcess(platform->GetPageAllocator());

  // Allow changing flags in cctests.
  // TODO(12887): Fix tests to avoid changing flag values after initialization.
  i::v8_flags.freeze_flags_after_init = false;

  v8::V8::Initialize();
  v8::V8::InitializeExternalStartupData(snapshot_directory);

#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
  constexpr bool kUseDefaultTrapHandler = true;
  CHECK(v8::V8::EnableWebAssemblyTrapHandler(kUseDefaultTrapHandler));
#endif  // V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED

  CcTest::set_array_buffer_allocator(
      v8::ArrayBuffer::Allocator::NewDefaultAllocator());

  v8::RegisterExtension(std::make_unique<i::PrintExtension>());
  v8::RegisterExtension(std::make_unique<i::ProfilerExtension>());
  v8::RegisterExtension(std::make_unique<i::TraceExtension>());

  if (!initialize_) {
    CHECK_NE(initialization_state_, kInitialized);
    initialization_state_ = kUninitialized;
    CHECK_NULL(isolate_);
  } else {
    CHECK_NE(initialization_state_, kUninitialized);
    initialization_state_ = kInitialized;
    CHECK_NULL(isolate_);
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator_;
    isolate_ = v8::Isolate::New(create_params);
    isolate_->Enter();
  }
#ifdef DEBUG
  const size_t active_isolates = i::Isolate::non_disposed_isolates();
#endif  // DEBUG
  callback_();
#ifdef DEBUG
  // This DCHECK ensures that all Isolates are properly disposed after finishing
  // the test. Stray Isolates lead to stray tasks in the platform which can
  // interact weirdly when swapping in new platforms (for testing) or during
  // shutdown.
  DCHECK_EQ(active_isolates, i::Isolate::non_disposed_isolates());
#endif  // DEBUG
  if (initialize_) {
    if (i_isolate()->was_locker_ever_used()) {
      v8::Locker locker(isolate_);
      EmptyMessageQueues(isolate_);
    } else {
      EmptyMessageQueues(isolate_);
    }
    isolate_->Exit();
    isolate_->Dispose();
    isolate_ = nullptr;
  } else {
    CHECK_NULL(isolate_);
  }

  v8::V8::Dispose();
  cppgc::ShutdownProcess();
  v8::V8::DisposePlatform();
}

i::Heap* CcTest::heap() { return i_isolate()->heap(); }
i::ReadOnlyHeap* CcTest::read_only_heap() {
  return i_isolate()->read_only_heap();
}

void CcTest::AddGlobalFunction(v8::Local<v8::Context> env, const char* name,
                               v8::FunctionCallback callback) {
  v8::Local<v8::FunctionTemplate> func_template =
      v8::FunctionTemplate::New(isolate_, callback);
  v8::Local<v8::Function> func =
      func_template->GetFunction(env).ToLocalChecked();
  func->SetName(v8_str(name));
  env->Global()->Set(env, v8_str(name), func).FromJust();
}

i::Handle<i::String> CcTest::MakeString(const char* str) {
  i::Isolate* isolate = CcTest::i_isolate();
  i::Factory* factory = isolate->factory();
  return factory->InternalizeUtf8String(str);
}

i::Handle<i::String> CcTest::MakeName(const char* str, int suffix) {
  v8::base::EmbeddedVector<char, 128> buffer;
  v8::base::SNPrintF(buffer, "%s%d", str, suffix);
  return CcTest::MakeString(buffer.begin());
}

v8::base::RandomNumberGenerator* CcTest::random_number_generator() {
  return InitIsolateOnce()->random_number_generator();
}

v8::Local<v8::Object> CcTest::global() {
  return isolate()->GetCurrentContext()->Global();
}

void CcTest::InitializeVM() {
  CHECK(!v8::base::Relaxed_Load(&isolate_used_));
  CHECK(!initialize_called_);
  initialize_called_ = true;
  v8::HandleScope handle_scope(CcTest::isolate());
  v8::Context::New(CcTest::isolate())->Enter();
}

v8::Local<v8::Context> CcTest::NewContext(CcTestExtensionFlags extension_flags,
                                          v8::Isolate* isolate) {
  const char* extension_names[kMaxExtensions];
  int extension_count = 0;
  for (int i = 0; i < kMaxExtensions; ++i) {
    if (!extension_flags.contains(static_cast<CcTestExtensionId>(i))) continue;
    extension_names[extension_count] = kExtensionName[i];
    ++extension_count;
  }
  v8::ExtensionConfiguration config(extension_count, extension_names);
  v8::Local<v8::Context> context = v8::Context::New(isolate, &config);
  CHECK(!context.IsEmpty());
  return context;
}

LocalContext::~LocalContext() {
  v8::HandleScope scope(isolate_);
  v8::Local<v8::Context>::New(isolate_, context_)->Exit();
  context_.Reset();
}

void LocalContext::Initialize(v8::Isolate* isolate,
                              v8::ExtensionConfiguration* extensions,
                              v8::Local<v8::ObjectTemplate> global_template,
                              v8::Local<v8::Value> global_object) {
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context =
      v8::Context::New(isolate, extensions, global_template, global_object);
  context_.Reset(isolate, context);
  context->Enter();
  // We can't do this later perhaps because of a fatal error.
  isolate_ = isolate;
}

// This indirection is needed because HandleScopes cannot be heap-allocated, and
// we don't want any unnecessary #includes in cctest.h.
class V8_NODISCARD InitializedHandleScopeImpl {
 public:
  explicit InitializedHandleScopeImpl(i::Isolate* isolate)
      : handle_scope_(isolate) {}

 private:
  i::HandleScope handle_scope_;
};

InitializedHandleScope::InitializedHandleScope(i::Isolate* isolate)
    : main_isolate_(isolate ? isolate : CcTest::InitIsolateOnce()),
      initialized_handle_scope_impl_(
          new InitializedHandleScopeImpl(main_isolate_)) {}

InitializedHandleScope::~InitializedHandleScope() = default;

HandleAndZoneScope::HandleAndZoneScope(bool support_zone_compression)
    : main_zone_(
          new i::Zone(&allocator_, ZONE_NAME, support_zone_compression)) {}

HandleAndZoneScope::~HandleAndZoneScope() = default;

#ifdef V8_ENABLE_TURBOFAN
i::Handle<i::JSFunction> Optimize(i::Handle<i::JSFunction> function,
                                  i::Zone* zone, i::Isolate* isolate,
                                  uint32_t flags) {
  i::Handle<i::SharedFunctionInfo> shared(function->shared(), isolate);
  i::IsCompiledScope is_compiled_scope(shared->is_compiled_scope(isolate));
  CHECK(is_compiled_scope.is_compiled() ||
        i::Compiler::Compile(isolate, function, i::Compiler::CLEAR_EXCEPTION,
                             &is_compiled_scope));

  CHECK_NOT_NULL(zone);

  i::OptimizedCompilationInfo info(zone, isolate, shared, function,
                                   i::CodeKind::TURBOFAN);

  if (flags & ~i::OptimizedCompilationInfo::kInlining) UNIMPLEMENTED();
  if (flags & i::OptimizedCompilationInfo::kInlining) {
    info.set_inlining();
  }

  CHECK(info.shared_info()->HasBytecodeArray());
  i::JSFunction::EnsureFeedbackVector(isolate, function, &is_compiled_scope);

  i::Handle<i::Code> code =
      i::compiler::Pipeline::GenerateCodeForTesting(&info, isolate)
          .ToHandleChecked();
  function->set_code(*code, v8::kReleaseStore);
  return function;
}
#endif  // V8_ENABLE_TURBOFAN

static void PrintTestList() {
  int test_num = 0;
  for (const auto& entry : g_cctests.Get()) {
    printf("**>Test: %s\n", entry.first.c_str());
    test_num++;
  }
  printf("\nTotal number of tests: %d\n", test_num);
}

int main(int argc, char* argv[]) {
#if V8_OS_WIN
  UINT new_flags =
      SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
  UINT existing_flags = SetErrorMode(new_flags);
  SetErrorMode(existing_flags | new_flags);
#if V8_CC_MSVC
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _set_error_mode(_OUT_TO_STDERR);
#endif  // V8_CC_MSVC
#endif  // V8_OS_WIN

  std::string usage = "Usage: " + std::string(argv[0]) + " [--list]" +
                      " [[V8_FLAGS] CCTEST]\n\n" + "Options:\n" +
                      "  --list:   list all cctests\n" +
                      "  CCTEST:   cctest identfier returned by --list\n" +
                      "  V8_FLAGS: see V8 options below\n\n\n";

#ifdef V8_USE_PERFETTO
  // Set up the in-process backend that the tracing controller will connect to.
  perfetto::TracingInitArgs init_args;
  init_args.backends = perfetto::BackendType::kInProcessBackend;
  perfetto::Tracing::Initialize(init_args);
#endif  // V8_USE_PERFETTO

  using HelpOptions = v8::internal::FlagList::HelpOptions;
  v8::internal::FlagList::SetFlagsFromCommandLine(
      &argc, argv, true, HelpOptions(HelpOptions::kExit, usage.c_str()));

  const char* test_arg = nullptr;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (strcmp(arg, "--list") == 0) {
      PrintTestList();
      return 0;
    }
    if (*arg == '-') {
      // Ignore flags that weren't removed by SetFlagsFromCommandLine
      continue;
    }
    if (test_arg != nullptr) {
      fprintf(stderr,
              "Running multiple tests in sequence is not allowed. Use "
              "tools/run-tests.py instead.\n");
      return 1;
    }
    test_arg = arg;
  }

  if (test_arg == nullptr) {
    printf("Ran 0 tests.\n");
    return 0;
  }

  auto it = g_cctests.Get().find(test_arg);
  if (it == g_cctests.Get().end()) {
    fprintf(stderr, "ERROR: Did not find test %s.\n", test_arg);
    return 1;
  }

  CcTest* test = it->second;
  test->Run(argv[0]);

  return 0;
}

std::vector<const RegisterThreadedTest*> RegisterThreadedTest::tests_;

bool IsValidUnwrapObject(v8::Object* object) {
  i::Address addr = i::ValueHelper::ValueAsAddress(object);
  auto instance_type = i::Internals::GetInstanceType(addr);
  return (v8::base::IsInRange(instance_type,
                              i::Internals::kFirstJSApiObjectType,
                              i::Internals::kLastJSApiObjectType) ||
          instance_type == i::Internals::kJSObjectType ||
          instance_type == i::Internals::kJSSpecialApiObjectType);
}

v8::PageAllocator* TestPlatform::GetPageAllocator() {
  return CcTest::default_platform()->GetPageAllocator();
}

void TestPlatform::OnCriticalMemoryPressure() {
  CcTest::default_platform()->OnCriticalMemoryPressure();
}

int TestPlatform::NumberOfWorkerThreads() {
  return CcTest::default_platform()->NumberOfWorkerThreads();
}

std::shared_ptr<v8::TaskRunner> TestPlatform::GetForegroundTaskRunner(
    v8::Isolate* isolate) {
  return CcTest::default_platform()->GetForegroundTaskRunner(isolate);
}

void TestPlatform::PostTaskOnWorkerThreadImpl(
    v8::TaskPriority priority, std::unique_ptr<v8::Task> task,
    const v8::SourceLocation& location) {
  CcTest::default_platform()->CallOnWorkerThread(std::move(task));
}

void TestPlatform::PostDelayedTaskOnWorkerThreadImpl(
    v8::TaskPriority priority, std::unique_ptr<v8::Task> task,
    double delay_in_seconds, const v8::SourceLocation& location) {
  CcTest::default_platform()->CallDelayedOnWorkerThread(std::move(task),
                                                        delay_in_seconds);
}

std::unique_ptr<v8::JobHandle> TestPlatform::CreateJobImpl(
    v8::TaskPriority priority, std::unique_ptr<v8::JobTask> job_task,
    const v8::SourceLocation& location) {
  return CcTest::default_platform()->CreateJob(priority, std::move(job_task),
                                               location);
}

double TestPlatform::MonotonicallyIncreasingTime() {
  return CcTest::default_platform()->MonotonicallyIncreasingTime();
}

double TestPlatform::CurrentClockTimeMillis() {
  return CcTest::default_platform()->CurrentClockTimeMillis();
}

bool TestPlatform::IdleTasksEnabled(v8::Isolate* isolate) {
  return CcTest::default_platform()->IdleTasksEnabled(isolate);
}

v8::TracingController* TestPlatform::GetTracingController() {
  return CcTest::default_platform()->GetTracingController();
}

namespace {

class ShutdownTask final : public v8::Task {
 public:
  ShutdownTask(v8::base::Semaphore* destruction_barrier,
               v8::base::Mutex* destruction_mutex,
               v8::base::ConditionVariable* destruction_condition,
               bool* can_destruct)
      : destruction_barrier_(destruction_barrier),
        destruction_mutex_(destruction_mutex),
        destruction_condition_(destruction_condition),
        can_destruct_(can_destruct)

  {}

  void Run() final {
    destruction_barrier_->Signal();
    {
      v8::base::MutexGuard guard(destruction_mutex_);
      while (!*can_destruct_) {
        destruction_condition_->Wait(destruction_mutex_);
      }
    }
    destruction_barrier_->Signal();
  }

 private:
  v8::base::Semaphore* const destruction_barrier_;
  v8::base::Mutex* const destruction_mutex_;
  v8::base::ConditionVariable* const destruction_condition_;
  bool* const can_destruct_;
};

}  // namespace
