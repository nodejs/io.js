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

#ifndef CCTEST_H_
#define CCTEST_H_

#include <memory>

#include "include/libplatform/libplatform.h"
#include "include/v8-platform.h"
#include "src/base/enum-set.h"
#include "src/codegen/register-configuration.h"
#include "src/debug/debug-interface.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "src/objects/js-function.h"
#include "src/objects/objects.h"
#include "src/zone/accounting-allocator.h"

namespace v8 {
namespace base {

class RandomNumberGenerator;

}  // namespace base

namespace internal {

const auto GetRegConfig = RegisterConfiguration::Default;

class HandleScope;
class ManualGCScope;
class Zone;

namespace compiler {

class JSHeapBroker;

}  // namespace compiler

}  // namespace internal

}  // namespace v8

#ifndef TEST
#define TEST(Name)                                                     \
  static void Test##Name();                                            \
  CcTest register_test_##Name(Test##Name, __FILE__, #Name, true, true, \
                              nullptr);                                \
  static void Test##Name()
#endif

#ifndef UNINITIALIZED_TEST
#define UNINITIALIZED_TEST(Name)                                        \
  static void Test##Name();                                             \
  CcTest register_test_##Name(Test##Name, __FILE__, #Name, true, false, \
                              nullptr);                                 \
  static void Test##Name()
#endif

#ifndef TEST_WITH_PLATFORM
#define TEST_WITH_PLATFORM(Name, PlatformClass)                            \
  static void Test##Name(PlatformClass& platform);                         \
  static void TestWithoutPlatform##Name() {                                \
    Test##Name(*static_cast<PlatformClass*>(i::V8::GetCurrentPlatform())); \
  }                                                                        \
  CcTest register_test_##Name(TestWithoutPlatform##Name, __FILE__, #Name,  \
                              true, true,                                  \
                              []() -> std::unique_ptr<TestPlatform> {      \
                                return std::make_unique<PlatformClass>();  \
                              });                                          \
  static void Test##Name(PlatformClass& platform)
#endif

#ifndef DISABLED_TEST
#define DISABLED_TEST(Name)                                             \
  static void Test##Name();                                             \
  CcTest register_test_##Name(Test##Name, __FILE__, #Name, false, true, \
                              nullptr);                                 \
  static void Test##Name()
#endif

// Similar to TEST, but used when test definitions appear as members of a
// (probably parameterized) class. This allows reusing the given tests multiple
// times. For this to work, the following conditions must hold:
//   1. The class has a template parameter named kTestFileName of type  char
//      const*, which is instantiated with __FILE__ at the *use site*, in order
//      to correctly associate the tests with the test suite using them.
//   2. To actually execute the tests, create an instance of the class
//      containing the MEMBER_TESTs.
#define MEMBER_TEST(Name)                                            \
  CcTest register_test_##Name =                                      \
      CcTest(Test##Name, kTestFileName, #Name, true, true, nullptr); \
  static void Test##Name()

#define EXTENSION_LIST(V)                                                      \
  V(GC_EXTENSION,       "v8/gc")                                               \
  V(PRINT_EXTENSION,    "v8/print")                                            \
  V(PROFILER_EXTENSION, "v8/profiler")                                         \
  V(TRACE_EXTENSION,    "v8/trace")

#define DEFINE_EXTENSION_ID(Name, Ident) Name##_ID,
enum CcTestExtensionId { EXTENSION_LIST(DEFINE_EXTENSION_ID) kMaxExtensions };
#undef DEFINE_EXTENSION_ID

using CcTestExtensionFlags = v8::base::EnumSet<CcTestExtensionId>;

#define DEFINE_EXTENSION_NAME(Name, Ident) Ident,
static constexpr const char* kExtensionName[kMaxExtensions] = {
    EXTENSION_LIST(DEFINE_EXTENSION_NAME)};
#undef DEFINE_EXTENSION_NAME

class CcTest;
class TestPlatform;

using CcTestMapType = std::map<std::string, CcTest*>;

class CcTest {
 public:
  using TestFunction = void();
  using TestPlatformFactory = std::unique_ptr<TestPlatform>();
  CcTest(TestFunction* callback, const char* file, const char* name,
         bool enabled, bool initialize,
         TestPlatformFactory* platform_factory = nullptr);
  void Run(const char* argv0);

  static v8::Isolate* isolate() {
    CHECK_NOT_NULL(isolate_);
    v8::base::Relaxed_Store(&isolate_used_, 1);
    return isolate_;
  }

  static i::Isolate* InitIsolateOnce() {
    if (!initialize_called_) InitializeVM();
    return i_isolate();
  }

  static i::Isolate* i_isolate() {
    return reinterpret_cast<i::Isolate*>(isolate());
  }

  static i::Heap* heap();
  static i::ReadOnlyHeap* read_only_heap();

  static v8::Platform* default_platform() { return default_platform_; }

  static void AddGlobalFunction(v8::Local<v8::Context> env, const char* name,
                                v8::FunctionCallback callback);

  static i::Handle<i::String> MakeString(const char* str);
  static i::Handle<i::String> MakeName(const char* str, int suffix);

  static v8::base::RandomNumberGenerator* random_number_generator();

  static v8::Local<v8::Object> global();

  static v8::ArrayBuffer::Allocator* array_buffer_allocator() {
    return allocator_;
  }

  static void set_array_buffer_allocator(
      v8::ArrayBuffer::Allocator* allocator) {
    allocator_ = allocator;
  }

  // TODO(dcarney): Remove.
  // This must be called first in a test.
  static void InitializeVM();

  // Helper function to configure a context.
  // Must be in a HandleScope.
  static v8::Local<v8::Context> NewContext(
      v8::Isolate* isolate = CcTest::isolate()) {
    return NewContext({}, isolate);
  }
  static v8::Local<v8::Context> NewContext(
      CcTestExtensionFlags extension_flags,
      v8::Isolate* isolate = CcTest::isolate());
  static v8::Local<v8::Context> NewContext(
      std::initializer_list<CcTestExtensionId> extensions,
      v8::Isolate* isolate = CcTest::isolate()) {
    return NewContext(CcTestExtensionFlags{extensions}, isolate);
  }

 private:
  static std::unordered_map<std::string, CcTest*>* tests_;
  static v8::ArrayBuffer::Allocator* allocator_;
  static v8::Isolate* isolate_;
  static v8::Platform* default_platform_;
  static bool initialize_called_;
  static v8::base::Atomic32 isolate_used_;

  TestFunction* callback_;
  bool initialize_;
  TestPlatformFactory* test_platform_factory_;

  friend int main(int argc, char** argv);
  friend class v8::internal::ManualGCScope;
};

// Switches between all the Api tests using the threading support.
// In order to get a surprising but repeatable pattern of thread
// switching it has extra semaphores to control the order in which
// the tests alternate, not relying solely on the big V8 lock.
//
// A test is augmented with calls to ApiTestFuzzer::Fuzz() in its
// callbacks.  This will have no effect when we are not running the
// thread fuzzing test.  In the thread fuzzing test it will
// pseudorandomly select a successor thread and switch execution
// to that thread, suspending the current test.
class ApiTestFuzzer: public v8::base::Thread {
 public:
  ~ApiTestFuzzer() override = default;

  void CallTest();

  // The ApiTestFuzzer is also a Thread, so it has a Run method.
  void Run() override;

  enum PartOfTest {
    FIRST_PART,
    SECOND_PART,
    THIRD_PART,
    FOURTH_PART,
    FIFTH_PART,
    SIXTH_PART,
    SEVENTH_PART,
    EIGHTH_PART,
    LAST_PART = EIGHTH_PART
  };

  static void SetUp(PartOfTest part);
  static void RunAllTests();
  static void TearDown();
  // This method switches threads if we are running the Threading test.
  // Otherwise it does nothing.
  static void Fuzz();

 private:
  explicit ApiTestFuzzer(int num)
      : Thread(Options("ApiTestFuzzer")),
        test_number_(num),
        gate_(0),
        active_(true) {}

  static bool NextThread();
  void ContextSwitch();
  static int GetNextFuzzer();

  static unsigned linear_congruential_generator;
  static std::vector<std::unique_ptr<ApiTestFuzzer>> fuzzers_;
  static bool fuzzing_;
  static v8::base::Semaphore all_tests_done_;
  static int tests_being_run_;
  static int active_tests_;
  static int current_fuzzer_;

  int test_number_;
  v8::base::Semaphore gate_;
  bool active_;
};

// In threaded cctests, control flow alternates between different threads, each
// of which runs a single test. All threaded cctests share the same isolate and
// a heap. With conservative stack scanning (CSS), whenever a thread invokes a
// GC for the common heap, the stacks of all threads are scanned. In this
// setting, it is not possible to disable CSS without losing correctness.
// Therefore, tests defined with THREADED_TEST:
//
// 1.  must not explicitly disable CSS, using the scope
//     internal::DisableConservativeStackScanningScopeForTesting, and
// 2.  cannot rely on the assumption that garbage collection will reclaim all
//     non-live objects.

#define THREADED_TEST(Name)                                          \
  static void Test##Name();                                          \
  RegisterThreadedTest register_##Name(Test##Name, #Name);           \
  /* */ TEST(Name)

class RegisterThreadedTest {
 public:
  explicit RegisterThreadedTest(CcTest::TestFunction* callback,
                                const char* name)
      : callback_(callback), name_(name) {
    tests_.push_back(this);
  }
  static int count() { return static_cast<int>(tests_.size()); }
  static const RegisterThreadedTest* nth(int i) {
    DCHECK_LE(0, i);
    DCHECK_LT(i, count());
    // Added tests used to be prepended to a linked list and therefore the last
    // one to be added was at index 0. This ensures that we keep this behavior.
    return tests_[count() - i - 1];
  }
  CcTest::TestFunction* callback() const { return callback_; }
  const char* name() const { return name_; }

 private:
  static std::vector<const RegisterThreadedTest*> tests_;
  CcTest::TestFunction* callback_;
  const char* name_;
};

// A LocalContext holds a reference to a v8::Context.
class LocalContext {
 public:
  LocalContext(v8::Isolate* isolate,
               v8::ExtensionConfiguration* extensions = nullptr,
               v8::Local<v8::ObjectTemplate> global_template =
                   v8::Local<v8::ObjectTemplate>(),
               v8::Local<v8::Value> global_object = v8::Local<v8::Value>()) {
    Initialize(isolate, extensions, global_template, global_object);
  }

  LocalContext(v8::ExtensionConfiguration* extensions = nullptr,
               v8::Local<v8::ObjectTemplate> global_template =
                   v8::Local<v8::ObjectTemplate>(),
               v8::Local<v8::Value> global_object = v8::Local<v8::Value>()) {
    Initialize(CcTest::isolate(), extensions, global_template, global_object);
  }

  virtual ~LocalContext();

  v8::Context* operator->() { return i::ValueHelper::HandleAsValue(context_); }
  v8::Context* operator*() { return operator->(); }
  bool IsReady() { return !context_.IsEmpty(); }

  v8::Local<v8::Context> local() const {
    return v8::Local<v8::Context>::New(isolate_, context_);
  }

 private:
  void Initialize(v8::Isolate* isolate, v8::ExtensionConfiguration* extensions,
                  v8::Local<v8::ObjectTemplate> global_template,
                  v8::Local<v8::Value> global_object);

  v8::Persistent<v8::Context> context_;
  v8::Isolate* isolate_;
};


static inline uint16_t* AsciiToTwoByteString(const char* source) {
  size_t array_length = strlen(source) + 1;
  uint16_t* converted = i::NewArray<uint16_t>(array_length);
  for (size_t i = 0; i < array_length; i++) converted[i] = source[i];
  return converted;
}

static inline uint16_t* AsciiToTwoByteString(const char16_t* source,
                                             size_t* length_out = nullptr) {
  size_t array_length = std::char_traits<char16_t>::length(source) + 1;
  uint16_t* converted = i::NewArray<uint16_t>(array_length);
  for (size_t i = 0; i < array_length; i++) converted[i] = source[i];
  if (length_out != nullptr) *length_out = array_length - 1;
  return converted;
}

template <typename T>
static inline i::DirectHandle<T> GetGlobal(const char* name) {
  i::Isolate* isolate = CcTest::i_isolate();
  i::DirectHandle<i::String> str_name =
      isolate->factory()->InternalizeUtf8String(name);

  i::DirectHandle<i::Object> value =
      i::Object::GetProperty(isolate, isolate->global_object(), str_name)
          .ToHandleChecked();
  return i::Cast<T>(value);
}

static inline v8::Local<v8::Boolean> v8_bool(bool val) {
  return v8::Boolean::New(v8::Isolate::GetCurrent(), val);
}

static inline v8::Local<v8::Number> v8_num(double x) {
  return v8::Number::New(v8::Isolate::GetCurrent(), x);
}

static inline v8::Local<v8::Integer> v8_int(int32_t x) {
  return v8::Integer::New(v8::Isolate::GetCurrent(), x);
}

static inline v8::Local<v8::Integer> v8_uint(uint32_t x) {
  return v8::Integer::NewFromUnsigned(v8::Isolate::GetCurrent(), x);
}

static inline v8::Local<v8::BigInt> v8_bigint(int64_t x) {
  return v8::BigInt::New(v8::Isolate::GetCurrent(), x);
}

static inline v8::Local<v8::String> v8_str(const char* x) {
  return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), x).ToLocalChecked();
}


static inline v8::Local<v8::String> v8_str(v8::Isolate* isolate,
                                           const char* x) {
  return v8::String::NewFromUtf8(isolate, x).ToLocalChecked();
}


static inline v8::Local<v8::Symbol> v8_symbol(const char* name) {
  return v8::Symbol::New(v8::Isolate::GetCurrent(), v8_str(name));
}


static inline v8::Local<v8::Script> v8_compile(v8::Local<v8::String> x) {
  v8::Local<v8::Script> result;
  CHECK(v8::Script::Compile(v8::Isolate::GetCurrent()->GetCurrentContext(), x)
            .ToLocal(&result));
  return result;
}

static inline v8::Local<v8::Script> v8_compile(const char* x) {
  return v8_compile(v8_str(x));
}

static inline v8::MaybeLocal<v8::Script> v8_try_compile(
    v8::Local<v8::String> x) {
  return v8::Script::Compile(v8::Isolate::GetCurrent()->GetCurrentContext(), x);
}

static inline v8::MaybeLocal<v8::Script> v8_try_compile(const char* x) {
  return v8_try_compile(v8_str(x));
}

static inline int32_t v8_run_int32value(v8::Local<v8::Script> script) {
  v8::Local<v8::Context> context = CcTest::isolate()->GetCurrentContext();
  return script->Run(context).ToLocalChecked()->Int32Value(context).FromJust();
}

static inline v8::Local<v8::Script> CompileWithOrigin(
    v8::Local<v8::String> source, v8::Local<v8::String> origin_url,
    bool is_shared_cross_origin) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::ScriptOrigin origin(origin_url, 0, 0, is_shared_cross_origin);
  v8::ScriptCompiler::Source script_source(source, origin);
  return v8::ScriptCompiler::Compile(isolate->GetCurrentContext(),
                                     &script_source)
      .ToLocalChecked();
}

static inline v8::Local<v8::Script> CompileWithOrigin(
    v8::Local<v8::String> source, const char* origin_url,
    bool is_shared_cross_origin) {
  return CompileWithOrigin(source, v8_str(origin_url), is_shared_cross_origin);
}

static inline v8::Local<v8::Script> CompileWithOrigin(
    const char* source, const char* origin_url, bool is_shared_cross_origin) {
  return CompileWithOrigin(v8_str(source), v8_str(origin_url),
                           is_shared_cross_origin);
}

// Helper functions that compile and run the source.
static inline v8::MaybeLocal<v8::Value> CompileRun(
    v8::Local<v8::Context> context, const char* source) {
  return v8::Script::Compile(context, v8_str(source))
      .ToLocalChecked()
      ->Run(context);
}


static inline v8::Local<v8::Value> CompileRunChecked(v8::Isolate* isolate,
                                                     const char* source) {
  v8::Local<v8::String> source_string =
      v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, source_string).ToLocalChecked();
  return script->Run(context).ToLocalChecked();
}


static inline v8::Local<v8::Value> CompileRun(v8::Local<v8::String> source) {
  v8::Local<v8::Value> result;
  if (v8_compile(source)
          ->Run(v8::Isolate::GetCurrent()->GetCurrentContext())
          .ToLocal(&result)) {
    return result;
  }
  return v8::Local<v8::Value>();
}


// Helper functions that compile and run the source.
static inline v8::Local<v8::Value> CompileRun(const char* source) {
  return CompileRun(v8_str(source));
}


static inline v8::Local<v8::Value> CompileRun(
    v8::Local<v8::Context> context, v8::ScriptCompiler::Source* script_source,
    v8::ScriptCompiler::CompileOptions options) {
  v8::Local<v8::Value> result;
  if (v8::ScriptCompiler::Compile(context, script_source, options)
          .ToLocalChecked()
          ->Run(context)
          .ToLocal(&result)) {
    return result;
  }
  return v8::Local<v8::Value>();
}


// Helper functions that compile and run the source with given origin.
static inline v8::Local<v8::Value> CompileRunWithOrigin(const char* source,
                                                        const char* origin_url,
                                                        int line_number,
                                                        int column_number) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::ScriptOrigin origin(v8_str(origin_url), line_number, column_number);
  v8::ScriptCompiler::Source script_source(v8_str(source), origin);
  return CompileRun(context, &script_source,
                    v8::ScriptCompiler::CompileOptions());
}


static inline v8::Local<v8::Value> CompileRunWithOrigin(
    v8::Local<v8::String> source, const char* origin_url) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::ScriptCompiler::Source script_source(
      source, v8::ScriptOrigin(v8_str(origin_url)));
  return CompileRun(context, &script_source,
                    v8::ScriptCompiler::CompileOptions());
}


static inline v8::Local<v8::Value> CompileRunWithOrigin(
    const char* source, const char* origin_url) {
  return CompileRunWithOrigin(v8_str(source), origin_url);
}

// Run a ScriptStreamingTask in a separate thread.
class StreamerThread : public v8::base::Thread {
 public:
  static void StartThreadForTaskAndJoin(
      v8::ScriptCompiler::ScriptStreamingTask* task) {
    StreamerThread thread(task);
    CHECK(thread.Start());
    thread.Join();
  }

  explicit StreamerThread(v8::ScriptCompiler::ScriptStreamingTask* task)
      : Thread(Thread::Options()), task_(task) {}

  void Run() override { task_->Run(); }

 private:
  v8::ScriptCompiler::ScriptStreamingTask* task_;
};

// Takes a JSFunction and runs it through the test version of the optimizing
// pipeline, allocating the temporary compilation artifacts in a given Zone.
// For possible {flags} values, look at OptimizedCompilationInfo::Flag.
i::Handle<i::JSFunction> Optimize(i::Handle<i::JSFunction> function,
                                  i::Zone* zone, i::Isolate* isolate,
                                  uint32_t flags);

static inline void ExpectString(const char* code, const char* expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsString());
  v8::String::Utf8Value utf8(v8::Isolate::GetCurrent(), result);
  CHECK_EQ(0, strcmp(expected, *utf8));
}

static inline void ExpectInt32(const char* code, int expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsInt32());
  CHECK_EQ(expected,
           result->Int32Value(v8::Isolate::GetCurrent()->GetCurrentContext())
               .FromJust());
}


static inline void ExpectBoolean(const char* code, bool expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsBoolean());
  CHECK_EQ(expected, result->BooleanValue(v8::Isolate::GetCurrent()));
}


static inline void ExpectTrue(const char* code) {
  ExpectBoolean(code, true);
}


static inline void ExpectFalse(const char* code) {
  ExpectBoolean(code, false);
}


static inline void ExpectObject(const char* code,
                                v8::Local<v8::Value> expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->SameValue(expected));
}


static inline void ExpectUndefined(const char* code) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsUndefined());
}


static inline void ExpectNull(const char* code) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsNull());
}


static inline void CheckDoubleEquals(double expected, double actual) {
  const double kEpsilon = 1e-10;
  CHECK_LE(expected, actual + kEpsilon);
  CHECK_GE(expected, actual - kEpsilon);
}

static v8::debug::DebugDelegate dummy_delegate;

static inline void EnableDebugger(v8::Isolate* isolate) {
  v8::debug::SetDebugDelegate(isolate, &dummy_delegate);
}


static inline void DisableDebugger(v8::Isolate* isolate) {
  v8::debug::SetDebugDelegate(isolate, nullptr);
}


static inline void EmptyMessageQueues(v8::Isolate* isolate) {
  while (v8::platform::PumpMessageLoop(CcTest::default_platform(), isolate)) {
  }
}

class InitializedHandleScopeImpl;

class V8_NODISCARD InitializedHandleScope {
 public:
  explicit InitializedHandleScope(i::Isolate* isolate = nullptr);
  ~InitializedHandleScope();

  // Prefixing the below with main_ reduces a lot of naming clashes.
  i::Isolate* main_isolate() { return main_isolate_; }

 private:
  i::Isolate* main_isolate_;
  std::unique_ptr<InitializedHandleScopeImpl> initialized_handle_scope_impl_;
};

class V8_NODISCARD HandleAndZoneScope : public InitializedHandleScope {
 public:
  explicit HandleAndZoneScope(bool support_zone_compression = false);
  ~HandleAndZoneScope();

  // Prefixing the below with main_ reduces a lot of naming clashes.
  i::Zone* main_zone() { return main_zone_.get(); }

 private:
  v8::internal::AccountingAllocator allocator_;
  std::unique_ptr<i::Zone> main_zone_;
};

class StaticOneByteResource : public v8::String::ExternalOneByteStringResource {
 public:
  explicit StaticOneByteResource(const char* data) : data_(data) {}

  ~StaticOneByteResource() override = default;

  const char* data() const override { return data_; }

  size_t length() const override { return strlen(data_); }

 private:
  const char* data_;
};

// This is a base class that can be overridden to implement a test platform. It
// delegates all operations to the default platform.
class TestPlatform : public v8::Platform {
 public:
  ~TestPlatform() override = default;

  // v8::Platform implementation.
  v8::PageAllocator* GetPageAllocator() override;
  void OnCriticalMemoryPressure() override;
  int NumberOfWorkerThreads() override;
  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(
      v8::Isolate* isolate, v8::TaskPriority priority) override;
  void PostTaskOnWorkerThreadImpl(v8::TaskPriority priority,
                                  std::unique_ptr<v8::Task> task,
                                  const v8::SourceLocation& location) override;
  void PostDelayedTaskOnWorkerThreadImpl(
      v8::TaskPriority priority, std::unique_ptr<v8::Task> task,
      double delay_in_seconds, const v8::SourceLocation& location) override;
  std::unique_ptr<v8::JobHandle> CreateJobImpl(
      v8::TaskPriority priority, std::unique_ptr<v8::JobTask> job_task,
      const v8::SourceLocation& location) override;
  double MonotonicallyIncreasingTime() override;
  double CurrentClockTimeMillis() override;
  bool IdleTasksEnabled(v8::Isolate* isolate) override;
  v8::TracingController* GetTracingController() override;

 protected:
  TestPlatform() = default;
};

#if defined(USE_SIMULATOR)
class SimulatorHelper {
 public:
  inline bool Init(v8::Isolate* isolate) {
    simulator_ = reinterpret_cast<v8::internal::Isolate*>(isolate)
                     ->thread_local_top()
                     ->simulator_;
    // Check if there is active simulator.
    return simulator_ != nullptr;
  }

  inline void FillRegisters(v8::RegisterState* state) {
#if V8_TARGET_ARCH_ARM
    state->pc = reinterpret_cast<void*>(simulator_->get_pc());
    state->sp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::sp));
    state->fp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::r11));
    state->lr = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::lr));
#elif V8_TARGET_ARCH_ARM64
    if (simulator_->sp() == 0 || simulator_->fp() == 0) {
      // It's possible that the simulator is interrupted while it is updating
      // the sp or fp register. ARM64 simulator does this in two steps:
      // first setting it to zero and then setting it to a new value.
      // Bailout if sp/fp doesn't contain the new value.
      return;
    }
    state->pc = reinterpret_cast<void*>(simulator_->pc());
    state->sp = reinterpret_cast<void*>(simulator_->sp());
    state->fp = reinterpret_cast<void*>(simulator_->fp());
    state->lr = reinterpret_cast<void*>(simulator_->lr());
#elif V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_LOONG64
    state->pc = reinterpret_cast<void*>(simulator_->get_pc());
    state->sp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::sp));
    state->fp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::fp));
#elif V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_RISCV32
    state->pc = reinterpret_cast<void*>(simulator_->get_pc());
    state->sp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::sp));
    state->fp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::fp));
    state->lr = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::ra));
#elif V8_TARGET_ARCH_PPC64
    state->pc = reinterpret_cast<void*>(simulator_->get_pc());
    state->sp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::sp));
    state->fp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::fp));
    state->lr = reinterpret_cast<void*>(simulator_->get_lr());
#elif V8_TARGET_ARCH_S390X
    state->pc = reinterpret_cast<void*>(simulator_->get_pc());
    state->sp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::sp));
    state->fp = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::fp));
    state->lr = reinterpret_cast<void*>(
        simulator_->get_register(v8::internal::Simulator::ra));
#endif
  }

 private:
  v8::internal::Simulator* simulator_;
};
#endif  // USE_SIMULATOR

// The following should correspond to Chromium's kV8DOMWrapperTypeIndex and
// kV8DOMWrapperObjectIndex.
static const int kV8WrapperTypeIndex = 0;
static const int kV8WrapperObjectIndex = 1;

enum class ApiCheckerResult : uint8_t {
  kNotCalled = 0,
  kSlowCalled = 1 << 0,
  kFastCalled = 1 << 1,
};
using ApiCheckerResultFlags = v8::base::Flags<ApiCheckerResult>;
DEFINE_OPERATORS_FOR_FLAGS(ApiCheckerResultFlags)

bool IsValidUnwrapObject(v8::Object* object);

template <typename T>
T* GetInternalField(v8::Object* wrapper) {
  assert(kV8WrapperObjectIndex < wrapper->InternalFieldCount());
  return reinterpret_cast<T*>(
      wrapper->GetAlignedPointerFromInternalField(kV8WrapperObjectIndex));
}

#endif  // ifndef CCTEST_H_
