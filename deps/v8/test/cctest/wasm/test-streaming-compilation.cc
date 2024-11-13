// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libplatform/libplatform.h"
#include "src/api/api-inl.h"
#include "src/base/vector.h"
#include "src/handles/global-handles-inl.h"
#include "src/init/v8.h"
#include "src/objects/managed.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-serialization.h"
#include "test/cctest/cctest.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8::internal::wasm {

class MockPlatform final : public TestPlatform {
 public:
  MockPlatform() : task_runner_(std::make_shared<MockTaskRunner>()) {}

  ~MockPlatform() {
    for (auto* job_handle : job_handles_) job_handle->ResetPlatform();
  }

  std::unique_ptr<v8::JobHandle> CreateJobImpl(
      v8::TaskPriority priority, std::unique_ptr<v8::JobTask> job_task,
      const v8::SourceLocation& location) override {
    auto orig_job_handle = v8::platform::NewDefaultJobHandle(
        this, priority, std::move(job_task), 1);
    auto job_handle =
        std::make_unique<MockJobHandle>(std::move(orig_job_handle), this);
    job_handles_.insert(job_handle.get());
    return job_handle;
  }

  std::shared_ptr<TaskRunner> GetForegroundTaskRunner(
      v8::Isolate* isolate) override {
    return task_runner_;
  }

  void PostTaskOnWorkerThreadImpl(v8::TaskPriority priority,
                                  std::unique_ptr<v8::Task> task,
                                  const v8::SourceLocation& location) override {
    task_runner_->PostTask(std::move(task));
  }

  bool IdleTasksEnabled(v8::Isolate* isolate) override { return false; }

  void ExecuteTasks() { task_runner_->ExecuteTasks(); }

 private:
  class MockTaskRunner final : public TaskRunner {
   public:
    void PostTaskImpl(std::unique_ptr<v8::Task> task,
                      const SourceLocation& location) override {
      base::MutexGuard lock_scope(&tasks_lock_);
      tasks_.push(std::move(task));
    }

    void PostNonNestableTaskImpl(std::unique_ptr<Task> task,
                                 const SourceLocation& location) override {
      PostTask(std::move(task));
    }

    void PostDelayedTaskImpl(std::unique_ptr<Task> task,
                             double delay_in_seconds,
                             const SourceLocation& location) override {
      base::MutexGuard lock_scope(&tasks_lock_);
      delayed_tasks_.emplace_back(
          std::move(task), base::TimeTicks::Now() +
                               base::TimeDelta::FromSecondsD(delay_in_seconds));
    }

    void PostNonNestableDelayedTaskImpl(
        std::unique_ptr<Task> task, double delay_in_seconds,
        const SourceLocation& location) override {
      PostDelayedTask(std::move(task), delay_in_seconds);
    }

    void PostIdleTaskImpl(std::unique_ptr<IdleTask> task,
                          const SourceLocation& location) override {
      UNREACHABLE();
    }

    bool IdleTasksEnabled() override { return false; }
    bool NonNestableTasksEnabled() const override { return true; }
    bool NonNestableDelayedTasksEnabled() const override { return true; }

    // The test must call this repeatedly if delayed tasks were posted, until
    // all such tasks have been executed.
    void ExecuteTasks() {
      std::queue<std::unique_ptr<v8::Task>> tasks;
      while (true) {
        {
          base::MutexGuard lock_scope(&tasks_lock_);
          tasks.swap(tasks_);
          // Move all delayed tasks which are ready for execution to {tasks_}.
          base::TimeTicks now = base::TimeTicks::Now();
          for (auto it = delayed_tasks_.begin(), end = delayed_tasks_.end();
               it != end;) {
            if (it->second > now) {
              ++it;
              continue;
            }
            tasks.push(std::move(it->first));
            it = delayed_tasks_.erase(it);
          }
        }
        // Stop if there are no tasks to execute. Otherwise execute the tasks,
        // then check again.
        if (tasks.empty()) break;
        while (!tasks.empty()) {
          std::unique_ptr<Task> task = std::move(tasks.front());
          tasks.pop();
          task->Run();
        }
      }
    }

   private:
    base::Mutex tasks_lock_;
    // We do not execute tasks concurrently, so we only need one list of tasks.
    std::queue<std::unique_ptr<v8::Task>> tasks_;
    std::list<std::pair<std::unique_ptr<v8::Task>, base::TimeTicks>>
        delayed_tasks_;
  };

  class MockJobHandle : public JobHandle {
   public:
    explicit MockJobHandle(std::unique_ptr<JobHandle> orig_handle,
                           MockPlatform* platform)
        : orig_handle_(std::move(orig_handle)), platform_(platform) {}

    ~MockJobHandle() {
      if (platform_) platform_->job_handles_.erase(this);
    }

    void ResetPlatform() { platform_ = nullptr; }

    void NotifyConcurrencyIncrease() override {
      orig_handle_->NotifyConcurrencyIncrease();
    }
    void Join() override { orig_handle_->Join(); }
    void Cancel() override { orig_handle_->Cancel(); }
    void CancelAndDetach() override { orig_handle_->CancelAndDetach(); }
    bool IsActive() override { return orig_handle_->IsActive(); }
    bool IsValid() override { return orig_handle_->IsValid(); }

   private:
    std::unique_ptr<JobHandle> orig_handle_;
    MockPlatform* platform_;
  };

  std::shared_ptr<MockTaskRunner> task_runner_;
  std::unordered_set<MockJobHandle*> job_handles_;
};

namespace {

enum class CompilationState {
  kPending,
  kFinished,
  kFailed,
};

class TestResolver : public CompilationResultResolver {
 public:
  TestResolver(i::Isolate* isolate, CompilationState* state,
               std::string* error_message,
               Handle<WasmModuleObject>* module_object)
      : isolate_(isolate),
        state_(state),
        error_message_(error_message),
        module_object_(module_object) {}

  void OnCompilationSucceeded(i::Handle<i::WasmModuleObject> module) override {
    *state_ = CompilationState::kFinished;
    *module_object_ = isolate_->global_handles()->Create(*module);
  }

  void OnCompilationFailed(i::Handle<i::Object> error_reason) override {
    *state_ = CompilationState::kFailed;
    DirectHandle<String> str =
        Object::ToString(isolate_, error_reason).ToHandleChecked();
    error_message_->assign(str->ToCString().get());
    // Print the error message, for easier debugging on tests that unexpectedly
    // fail compilation.
    PrintF("Compilation failed: %s\n", error_message_->c_str());
  }

 private:
  i::Isolate* isolate_;
  CompilationState* const state_;
  std::string* const error_message_;
  Handle<WasmModuleObject>* const module_object_;
};

class StreamTester {
 public:
  explicit StreamTester(v8::Isolate* isolate)
      : zone_(&allocator_, "StreamTester") {
    Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    WasmEnabledFeatures features = WasmEnabledFeatures::FromIsolate(i_isolate);
    stream_ = GetWasmEngine()->StartStreamingCompilation(
        i_isolate, features, CompileTimeImports{},
        v8::Utils::OpenHandle(*context), "WebAssembly.compileStreaming()",
        std::make_shared<TestResolver>(i_isolate, &state_, &error_message_,
                                       &module_object_));
  }

  std::shared_ptr<StreamingDecoder> stream() const { return stream_; }

  // Compiled module object, valid after successful compile.
  Handle<WasmModuleObject> module_object() const {
    CHECK(!module_object_.is_null());
    return module_object_;
  }

  // Compiled native module, valid after successful compile.
  NativeModule* native_module() const {
    return module_object()->native_module();
  }
  std::shared_ptr<NativeModule> shared_native_module() const {
    return module_object()->shared_native_module();
  }

  // Run all compiler tasks, both foreground and background tasks.
  void RunCompilerTasks() {
    static_cast<MockPlatform*>(i::V8::GetCurrentPlatform())->ExecuteTasks();
  }

  bool IsPromiseFulfilled() { return state_ == CompilationState::kFinished; }

  bool IsPromiseRejected() { return state_ == CompilationState::kFailed; }

  bool IsPromisePending() { return state_ == CompilationState::kPending; }

  void OnBytesReceived(const uint8_t* start, size_t length) {
    stream_->OnBytesReceived(base::Vector<const uint8_t>(start, length));
  }

  void FinishStream() { stream_->Finish(); }

  void SetCompiledModuleBytes(base::Vector<const uint8_t> bytes) {
    stream_->SetCompiledModuleBytes(bytes);
  }

  Zone* zone() { return &zone_; }

  const std::string& error_message() const { return error_message_; }

 private:
  AccountingAllocator allocator_;
  Zone zone_;
  CompilationState state_ = CompilationState::kPending;
  std::string error_message_;
  Handle<WasmModuleObject> module_object_;
  std::shared_ptr<StreamingDecoder> stream_;
};
}  // namespace

#define RUN_STREAM(name)                                                     \
  v8::Isolate* isolate = CcTest::isolate();                                  \
  v8::HandleScope handle_scope(isolate);                                     \
  v8::Local<v8::Context> context = v8::Context::New(isolate);                \
  v8::Context::Scope context_scope(context);                                 \
  /* Reduce tiering budget so we do not need to execute too long. */         \
  i::FlagScope<int> reduced_tiering_budget(&i::v8_flags.wasm_tiering_budget, \
                                           1);                               \
  RunStream_##name(&platform, isolate);

#define STREAM_TEST(name)                                                  \
  void RunStream_##name(MockPlatform*, v8::Isolate*);                      \
  TEST_WITH_PLATFORM(Async##name, MockPlatform) {                          \
    if (i::v8_flags.memory_balancer) return;                               \
    RUN_STREAM(name);                                                      \
  }                                                                        \
                                                                           \
  TEST_WITH_PLATFORM(SingleThreaded##name, MockPlatform) {                 \
    if (i::v8_flags.memory_balancer) return;                               \
    i::FlagScope<bool> single_threaded_scope(&i::v8_flags.single_threaded, \
                                             true);                        \
    RUN_STREAM(name);                                                      \
  }                                                                        \
  void RunStream_##name(MockPlatform* platform, v8::Isolate* isolate)

constexpr const char* kExportNames[] = {"a", "b", "c"};

// Create a valid module with 3 functions.
ZoneBuffer GetValidModuleBytes(Zone* zone) {
  ZoneBuffer buffer(zone);
  TestSignatures sigs;
  WasmModuleBuilder builder(zone);
  uint8_t i = 0;
  for (const char* export_name : kExportNames) {
    WasmFunctionBuilder* f = builder.AddFunction(sigs.i_iii());
    uint8_t code[] = {kExprLocalGet, i, kExprEnd};
    f->EmitCode(code, arraysize(code));
    CHECK_GE(3, ++i);
    builder.AddExport(base::CStrVector(export_name), f);
  }
  builder.WriteTo(&buffer);
  return buffer;
}

// Create the same valid module as above and serialize it to test streaming
// with compiled module caching.
ZoneBuffer GetValidCompiledModuleBytes(v8::Isolate* isolate, Zone* zone,
                                       ZoneBuffer wire_bytes) {
  // Use a tester to compile to a NativeModule.
  StreamTester tester(isolate);
  tester.OnBytesReceived(wire_bytes.begin(), wire_bytes.size());
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());

  NativeModule* native_module = tester.native_module();
  CHECK_NOT_NULL(native_module);

  auto* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower{i_isolate, "GetValidCompiledModuleBytes"};
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, tester.module_object(), {}, {})
          .ToHandleChecked();
  CHECK(!thrower.error());

  // Call the exported functions repeatedly until they are all tiered up.
  std::vector<Handle<WasmExportedFunction>> exported_functions;
  for (const char* export_name : kExportNames) {
    exported_functions.push_back(
        testing::GetExportedFunction(i_isolate, instance, export_name)
            .ToHandleChecked());
  }
  while (true) {
    WasmCodeRefScope code_ref_scope;
    std::vector<WasmCode*> all_code = native_module->SnapshotCodeTable().first;
    if (std::all_of(all_code.begin(), all_code.end(), [](const WasmCode* code) {
          return code && code->tier() == ExecutionTier::kTurbofan;
        })) {
      break;
    }
    for (Handle<WasmExportedFunction> exported_function : exported_functions) {
      DirectHandle<Object> return_value =
          Execution::Call(i_isolate, exported_function,
                          ReadOnlyRoots{i_isolate}.undefined_value_handle(), 0,
                          nullptr)
              .ToHandleChecked();
      CHECK(IsSmi(*return_value));
      CHECK_EQ(0, Cast<Smi>(*return_value).value());
    }
    tester.RunCompilerTasks();
  }

  // Serialize the NativeModule.
  i::wasm::WasmSerializer serializer(native_module);
  size_t size = serializer.GetSerializedNativeModuleSize();
  std::vector<uint8_t> buffer(size);
  CHECK(serializer.SerializeNativeModule(base::VectorOf(buffer)));
  ZoneBuffer result(zone, size);
  result.write(buffer.data(), size);
  return result;
}

// Test that all bytes arrive before doing any compilation. FinishStream is
// called immediately.
STREAM_TEST(TestAllBytesArriveImmediatelyStreamFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetValidModuleBytes(tester.zone());

  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseFulfilled());
}

// Test that all bytes arrive before doing any compilation. FinishStream is
// called after the compilation is done.
STREAM_TEST(TestAllBytesArriveAOTCompilerFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetValidModuleBytes(tester.zone());

  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());

  tester.RunCompilerTasks();
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseFulfilled());
}

size_t GetFunctionOffset(i::Isolate* isolate, base::Vector<const uint8_t> bytes,
                         size_t index) {
  ModuleResult result = DecodeWasmModule(WasmEnabledFeatures::All(), bytes,
                                         false, ModuleOrigin::kWasmOrigin);
  CHECK(result.ok());
  const WasmFunction* func = &result.value()->functions[index];
  return func->code.offset();
}

// Test that some functions come in the beginning, some come after some
// functions already got compiled.
STREAM_TEST(TestCutAfterOneFunctionStreamFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetValidModuleBytes(tester.zone());

  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  size_t offset = GetFunctionOffset(i_isolate, base::VectorOf(buffer), 1);
  tester.OnBytesReceived(buffer.begin(), offset);
  tester.RunCompilerTasks();
  CHECK(tester.IsPromisePending());
  tester.OnBytesReceived(buffer.begin() + offset, buffer.size() - offset);
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());
}

// Test that some functions come in the beginning, some come after some
// functions already got compiled. Call FinishStream after the compilation is
// done.
STREAM_TEST(TestCutAfterOneFunctionCompilerFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetValidModuleBytes(tester.zone());

  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  size_t offset = GetFunctionOffset(i_isolate, base::VectorOf(buffer), 1);
  tester.OnBytesReceived(buffer.begin(), offset);
  tester.RunCompilerTasks();
  CHECK(tester.IsPromisePending());
  tester.OnBytesReceived(buffer.begin() + offset, buffer.size() - offset);
  tester.RunCompilerTasks();
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());
}

// Create a module with an invalid global section.
ZoneBuffer GetModuleWithInvalidSection(Zone* zone) {
  ZoneBuffer buffer(zone);
  TestSignatures sigs;
  WasmModuleBuilder builder(zone);
  // Add an invalid global to the module. The decoder will fail there.
  builder.AddGlobal(kWasmVoid, true, WasmInitExpr::GlobalGet(12));
  {
    WasmFunctionBuilder* f = builder.AddFunction(sigs.i_iii());
    uint8_t code[] = {kExprLocalGet, 0, kExprEnd};
    f->EmitCode(code, arraysize(code));
  }
  {
    WasmFunctionBuilder* f = builder.AddFunction(sigs.i_iii());
    uint8_t code[] = {kExprLocalGet, 1, kExprEnd};
    f->EmitCode(code, arraysize(code));
  }
  {
    WasmFunctionBuilder* f = builder.AddFunction(sigs.i_iii());
    uint8_t code[] = {kExprLocalGet, 2, kExprEnd};
    f->EmitCode(code, arraysize(code));
  }
  builder.WriteTo(&buffer);
  return buffer;
}

// Test an error in a section, found by the ModuleDecoder.
STREAM_TEST(TestErrorInSectionStreamFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetModuleWithInvalidSection(tester.zone());

  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

STREAM_TEST(TestErrorInSectionCompilerFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetModuleWithInvalidSection(tester.zone());

  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.RunCompilerTasks();
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

STREAM_TEST(TestErrorInSectionWithCuts) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetModuleWithInvalidSection(tester.zone());

  const uint8_t* current = buffer.begin();
  size_t remaining = buffer.end() - buffer.begin();
  while (current < buffer.end()) {
    size_t size = std::min(remaining, size_t{10});
    tester.OnBytesReceived(current, size);
    tester.RunCompilerTasks();
    current += 10;
    remaining -= size;
  }
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

ZoneBuffer GetModuleWithInvalidSectionSize(Zone* zone) {
  // We get a valid module and overwrite the size of the first section with an
  // invalid value.
  ZoneBuffer buffer = GetValidModuleBytes(zone);
  // 9 == 4 (wasm magic) + 4 (version) + 1 (section code)
  uint8_t* section_size_address = const_cast<uint8_t*>(buffer.begin()) + 9;
  // 0x808080800F is an invalid module size in leb encoding.
  section_size_address[0] = 0x80;
  section_size_address[1] = 0x80;
  section_size_address[2] = 0x80;
  section_size_address[3] = 0x80;
  section_size_address[4] = 0x0F;
  return buffer;
}

STREAM_TEST(TestErrorInSectionSizeStreamFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetModuleWithInvalidSectionSize(tester.zone());
  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

STREAM_TEST(TestErrorInSectionSizeCompilerFinishesFirst) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetModuleWithInvalidSectionSize(tester.zone());
  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.RunCompilerTasks();
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

STREAM_TEST(TestErrorInSectionSizeWithCuts) {
  StreamTester tester(isolate);
  ZoneBuffer buffer = GetModuleWithInvalidSectionSize(tester.zone());
  const uint8_t* current = buffer.begin();
  size_t remaining = buffer.end() - buffer.begin();
  while (current < buffer.end()) {
    size_t size = std::min(remaining, size_t{10});
    tester.OnBytesReceived(current, size);
    tester.RunCompilerTasks();
    current += 10;
    remaining -= size;
  }
  tester.RunCompilerTasks();
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

// Test an error in the code section, found by the ModuleDecoder. The error is a
// functions count in the code section which differs from the functions count in
// the function section.
STREAM_TEST(TestErrorInCodeSectionDetectedByModuleDecoder) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // body size
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 2),    // section size
      U32V_1(2),                          // !!! invalid function count !!!
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

STREAM_TEST(TestSectionOrderErrorWithEmptyCodeSection) {
  // Valid: Export, then Code.
  const uint8_t valid[] = {WASM_MODULE_HEADER, SECTION(Export, ENTRY_COUNT(0)),
                           SECTION(Code, ENTRY_COUNT(0))};
  // Invalid: Code, then Export.
  const uint8_t invalid[] = {WASM_MODULE_HEADER, SECTION(Code, ENTRY_COUNT(0)),
                             SECTION(Export, ENTRY_COUNT(0))};

  StreamTester tester_valid(isolate);
  tester_valid.OnBytesReceived(valid, arraysize(valid));
  tester_valid.FinishStream();
  tester_valid.RunCompilerTasks();
  CHECK(tester_valid.IsPromiseFulfilled());

  StreamTester tester_invalid(isolate);
  tester_invalid.OnBytesReceived(invalid, arraysize(invalid));
  tester_invalid.FinishStream();
  tester_invalid.RunCompilerTasks();
  CHECK(tester_invalid.IsPromiseRejected());
  CHECK_NE(std::string::npos,
           tester_invalid.error_message().find("unexpected section <Export>"));
}

STREAM_TEST(TestSectionOrderErrorWithNonEmptyCodeSection) {
  // Valid: Export, then Code.
  const uint8_t valid[] = {
      WASM_MODULE_HEADER, SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),
      SECTION(Function, ENTRY_COUNT(1), SIG_INDEX(0)),
      SECTION(Export, ENTRY_COUNT(0)),
      SECTION(Code, ENTRY_COUNT(1), ADD_COUNT(WASM_NO_LOCALS, kExprEnd))};
  // Invalid: Code, then Export.
  const uint8_t invalid[] = {
      WASM_MODULE_HEADER, SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),
      SECTION(Function, ENTRY_COUNT(1), SIG_INDEX(0)),
      SECTION(Code, ENTRY_COUNT(1), ADD_COUNT(WASM_NO_LOCALS, kExprEnd)),
      SECTION(Export, ENTRY_COUNT(0))};

  StreamTester tester_valid(isolate);
  tester_valid.OnBytesReceived(valid, arraysize(valid));
  tester_valid.FinishStream();
  tester_valid.RunCompilerTasks();
  CHECK(tester_valid.IsPromiseFulfilled());

  StreamTester tester_invalid(isolate);
  tester_invalid.OnBytesReceived(invalid, arraysize(invalid));
  tester_invalid.FinishStream();
  tester_invalid.RunCompilerTasks();
  CHECK(tester_invalid.IsPromiseRejected());
  CHECK_NE(std::string::npos,
           tester_invalid.error_message().find("unexpected section <Export>"));
}

// Test an error in the code section, found by the StreamingDecoder. The error
// is an invalid function body size, so that there are not enough bytes in the
// code section for the function body.
STREAM_TEST(TestErrorInCodeSectionDetectedByStreamingDecoder) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(26),                 // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 3),    // section size
      U32V_1(3),                          // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

// Test an error in the code section, found by the Compiler. The error is an
// invalid return type.
STREAM_TEST(TestErrorInCodeSectionDetectedByCompiler) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  uint8_t invalid_code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprI64Const, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 2 +
             arraysize(invalid_code)),  // section size
      U32V_1(3),                        // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.RunCompilerTasks();
  tester.OnBytesReceived(code, arraysize(code));
  tester.RunCompilerTasks();
  tester.OnBytesReceived(invalid_code, arraysize(invalid_code));
  tester.RunCompilerTasks();
  tester.OnBytesReceived(code, arraysize(code));
  tester.RunCompilerTasks();
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

// Test Abort before any bytes arrive.
STREAM_TEST(TestAbortImmediately) {
  StreamTester tester(isolate);
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort within a section.
STREAM_TEST(TestAbortWithinSection1) {
  StreamTester tester(isolate);
  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                // module header
      kTypeSectionCode,                  // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),  // section size
      U32V_1(1)                          // type count
                                         // Type section is not yet complete.
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.RunCompilerTasks();
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort within a section.
STREAM_TEST(TestAbortWithinSection2) {
  StreamTester tester(isolate);
  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      // Function section is not yet complete.
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.RunCompilerTasks();
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort just before the code section.
STREAM_TEST(TestAbortAfterSection) {
  StreamTester tester(isolate);
  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.RunCompilerTasks();
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort after the function count in the code section. The compiler tasks
// execute before the abort.
STREAM_TEST(TestAbortAfterFunctionsCount1) {
  StreamTester tester(isolate);
  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(20),                         // section size
      U32V_1(3),                          // functions count
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.RunCompilerTasks();
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort after the function count in the code section. The compiler tasks
// do not execute before the abort.
STREAM_TEST(TestAbortAfterFunctionsCount2) {
  StreamTester tester(isolate);
  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(20),                         // section size
      U32V_1(3),                          // functions count
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort after some functions got compiled. The compiler tasks execute
// before the abort.
STREAM_TEST(TestAbortAfterFunctionGotCompiled1) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(20),                         // section size
      U32V_1(3),                          // functions count
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.RunCompilerTasks();
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort after some functions got compiled. The compiler tasks execute
// before the abort.
STREAM_TEST(TestAbortAfterFunctionGotCompiled2) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(20),                         // section size
      U32V_1(3),                          // functions count
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort after all functions got compiled.
STREAM_TEST(TestAbortAfterCodeSection1) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // body size
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 3),    // section size
      U32V_1(3),                          // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.RunCompilerTasks();
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

// Test Abort after all functions got compiled.
STREAM_TEST(TestAbortAfterCodeSection2) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // body size
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 3),    // section size
      U32V_1(3),                          // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

STREAM_TEST(TestAbortAfterCompilationError1) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  uint8_t invalid_code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprI64Const, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 2 +
             arraysize(invalid_code)),  // section size
      U32V_1(3),                        // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(invalid_code, arraysize(invalid_code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.RunCompilerTasks();
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

STREAM_TEST(TestAbortAfterCompilationError2) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  uint8_t invalid_code[] = {
      U32V_1(4),                  // !!! invalid body size !!!
      U32V_1(0),                  // locals count
      kExprI64Const, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 2 +
             arraysize(invalid_code)),  // section size
      U32V_1(3),                        // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(invalid_code, arraysize(invalid_code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.stream()->Abort();
  tester.RunCompilerTasks();
}

STREAM_TEST(TestOnlyModuleHeader) {
  StreamTester tester(isolate);

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,  // module header
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.FinishStream();
  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseFulfilled());
}

STREAM_TEST(TestModuleWithZeroFunctions) {
  StreamTester tester(isolate);

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,    // module header
      kTypeSectionCode,      // section code
      U32V_1(1),             // section size
      U32V_1(0),             // type count
      kFunctionSectionCode,  // section code
      U32V_1(1),             // section size
      U32V_1(0),             // functions count
      kCodeSectionCode,      // section code
      U32V_1(1),             // section size
      U32V_1(0),             // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());
}

STREAM_TEST(TestModuleWithMultipleFunctions) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // body size
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 3),    // section size
      U32V_1(3),                          // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.RunCompilerTasks();
  tester.OnBytesReceived(code, arraysize(code));
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());
}

STREAM_TEST(TestModuleWithDataSection) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(4),                  // body size
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(1 + arraysize(code) * 3),    // section size
      U32V_1(3),                          // functions count
  };

  const uint8_t data_section[] = {
      kDataSectionCode,  // section code
      U32V_1(1),         // section size
      U32V_1(0),         // data segment count
  };
  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.RunCompilerTasks();
  tester.OnBytesReceived(data_section, arraysize(data_section));
  tester.RunCompilerTasks();
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());
}
// Test that all bytes arrive before doing any compilation. FinishStream is
// called immediately.
STREAM_TEST(TestModuleWithImportedFunction) {
  StreamTester tester(isolate);
  ZoneBuffer buffer(tester.zone());
  TestSignatures sigs;
  WasmModuleBuilder builder(tester.zone());
  builder.AddImport(base::ArrayVector("Test"), sigs.i_iii());
  {
    WasmFunctionBuilder* f = builder.AddFunction(sigs.i_iii());
    uint8_t code[] = {kExprLocalGet, 0, kExprEnd};
    f->EmitCode(code, arraysize(code));
  }
  builder.WriteTo(&buffer);

  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseFulfilled());
}

STREAM_TEST(TestIncrementalCaching) {
  FLAG_VALUE_SCOPE(wasm_tier_up, false);
  constexpr int threshold = 10;  // 10 bytes
  FlagScope<int> caching_threshold(&v8_flags.wasm_caching_threshold, threshold);
  FlagScope<int> caching_hard_threshold(&v8_flags.wasm_caching_hard_threshold,
                                        threshold);
  StreamTester tester(isolate);
  int call_cache_counter = 0;
  tester.stream()->SetMoreFunctionsCanBeSerializedCallback(
      [&call_cache_counter](
          const std::shared_ptr<i::wasm::NativeModule>& native_module) {
        call_cache_counter++;
      });

  ZoneBuffer buffer(tester.zone());
  TestSignatures sigs;
  WasmModuleBuilder builder(tester.zone());
  builder.AddMemory(1);

  base::Vector<const char> function_names[] = {
      base::CStrVector("f0"), base::CStrVector("f1"), base::CStrVector("f2")};
  for (int i = 0; i < 3; ++i) {
    WasmFunctionBuilder* f = builder.AddFunction(sigs.v_v());

    constexpr int64_t val = 0x123456789abc;
    constexpr int index = 0x1234;
    uint8_t store_mem[] = {
        WASM_STORE_MEM(MachineType::Int64(), WASM_I32V(index), WASM_I64V(val))};
    constexpr uint32_t kStoreLength = 20;
    CHECK_EQ(kStoreLength, arraysize(store_mem));

    // Produce a store {threshold} many times to reach the caching threshold.
    constexpr uint32_t kCodeLength = kStoreLength * threshold + 1;
    uint8_t code[kCodeLength];
    for (int j = 0; j < threshold; ++j) {
      memcpy(code + (j * kStoreLength), store_mem, kStoreLength);
    }
    code[kCodeLength - 1] = WasmOpcode::kExprEnd;
    f->EmitCode(code, kCodeLength);
    builder.AddExport(function_names[i], f);
  }
  builder.WriteTo(&buffer);
  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());
  tester.native_module();
  constexpr base::Vector<const char> kNoSourceUrl{"", 0};
  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  DirectHandle<Script> script = GetWasmEngine()->GetOrCreateScript(
      i_isolate, tester.shared_native_module(), kNoSourceUrl);
  Handle<WasmModuleObject> module_object =
      WasmModuleObject::New(i_isolate, tester.shared_native_module(), script);
  ErrorThrower thrower(i_isolate, "Instantiation");
  // We instantiated before, so the second instantiation must also succeed:
  DirectHandle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, module_object, {}, {})
          .ToHandleChecked();
  CHECK(!thrower.error());

  WasmCodeRefScope code_scope;
  NativeModule* module = tester.native_module();
  CHECK(module->GetCode(0) == nullptr || module->GetCode(0)->is_liftoff());
  CHECK(module->GetCode(1) == nullptr || module->GetCode(1)->is_liftoff());
  CHECK(module->GetCode(2) == nullptr || module->GetCode(2)->is_liftoff());
  // No TurboFan compilation happened yet, and therefore no call to the cache.
  CHECK_EQ(0, call_cache_counter);
  i::wasm::TriggerTierUp(i_isolate, instance->trusted_data(i_isolate), 0);
  tester.RunCompilerTasks();
  CHECK(!module->GetCode(0)->is_liftoff());
  CHECK(module->GetCode(1) == nullptr || module->GetCode(1)->is_liftoff());
  CHECK(module->GetCode(2) == nullptr || module->GetCode(2)->is_liftoff());
  CHECK_EQ(1, call_cache_counter);
  size_t serialized_size;
  {
    i::wasm::WasmSerializer serializer(tester.native_module());
    serialized_size = serializer.GetSerializedNativeModuleSize();
  }
  i::wasm::TriggerTierUp(i_isolate, instance->trusted_data(i_isolate), 1);
  tester.RunCompilerTasks();
  CHECK(!module->GetCode(0)->is_liftoff());
  CHECK(!module->GetCode(1)->is_liftoff());
  CHECK(module->GetCode(2) == nullptr || module->GetCode(2)->is_liftoff());
  CHECK_EQ(2, call_cache_counter);
  {
    i::wasm::WasmSerializer serializer(tester.native_module());
    CHECK_LT(serialized_size, serializer.GetSerializedNativeModuleSize());
  }
}

STREAM_TEST(TestModuleWithErrorAfterDataSection) {
  StreamTester tester(isolate);

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 1),                      // section size
      U32V_1(1),                          // functions count
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(6),                          // section size
      U32V_1(1),                          // functions count
      U32V_1(4),                          // body size
      U32V_1(0),                          // locals count
      kExprLocalGet,                      // some code
      0,                                  // some code
      kExprEnd,                           // some code
      kDataSectionCode,                   // section code
      U32V_1(1),                          // section size
      U32V_1(0),                          // data segment count
      kUnknownSectionCode,                // section code
      U32V_1(1),                          // invalid section size
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseRejected());
}

// Test that cached bytes work.
STREAM_TEST(TestDeserializationBypassesCompilation) {
  StreamTester tester(isolate);
  ZoneBuffer wire_bytes = GetValidModuleBytes(tester.zone());
  ZoneBuffer module_bytes =
      GetValidCompiledModuleBytes(isolate, tester.zone(), wire_bytes);
  tester.SetCompiledModuleBytes(base::VectorOf(module_bytes));
  tester.OnBytesReceived(wire_bytes.begin(), wire_bytes.size());
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseFulfilled());
}

// Test that bad cached bytes don't cause compilation of wire bytes to fail.
STREAM_TEST(TestDeserializationFails) {
  StreamTester tester(isolate);
  ZoneBuffer wire_bytes = GetValidModuleBytes(tester.zone());
  ZoneBuffer module_bytes =
      GetValidCompiledModuleBytes(isolate, tester.zone(), wire_bytes);
  // corrupt header
  uint8_t first_byte = *module_bytes.begin();
  module_bytes.patch_u8(0, first_byte + 1);
  tester.SetCompiledModuleBytes(base::VectorOf(module_bytes));
  tester.OnBytesReceived(wire_bytes.begin(), wire_bytes.size());
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseFulfilled());
}

// Test that a non-empty function section with a missing code section fails.
STREAM_TEST(TestFunctionSectionWithoutCodeSection) {
  StreamTester tester(isolate);

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 3),                      // section size
      U32V_1(3),                          // functions count
      0,                                  // signature index
      0,                                  // signature index
      0,                                  // signature index
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.FinishStream();

  tester.RunCompilerTasks();

  CHECK(tester.IsPromiseRejected());
}

STREAM_TEST(TestMoreFunctionsCanBeSerializedCallback) {
  // The "more functions can be serialized" callback will only be triggered with
  // dynamic tiering, so skip this test if dynamic tiering is disabled.
  if (!v8_flags.wasm_dynamic_tiering) return;

  // Reduce the caching threshold to 10 bytes so that our three small functions
  // trigger caching.
  FlagScope<int> caching_threshold(&v8_flags.wasm_caching_threshold, 10);
  FlagScope<int> caching_hard_threshold(&v8_flags.wasm_caching_hard_threshold,
                                        10);
  StreamTester tester(isolate);
  bool callback_called = false;
  tester.stream()->SetMoreFunctionsCanBeSerializedCallback(
      [&callback_called](const std::shared_ptr<NativeModule> module) {
        callback_called = true;
      });

  uint8_t code[] = {
      ADD_COUNT(U32V_1(0),                   // locals count
                kExprLocalGet, 0, kExprEnd)  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,  // module header
      SECTION(Type,
              ENTRY_COUNT(1),                      // type count
              SIG_ENTRY_x_x(kI32Code, kI32Code)),  // signature entry
      SECTION(Function, ENTRY_COUNT(3), SIG_INDEX(0), SIG_INDEX(0),
              SIG_INDEX(0)),
      SECTION(Export, ENTRY_COUNT(3),                             // 3 exports
              ADD_COUNT('a'), kExternalFunction, FUNC_INDEX(0),   // "a" (0)
              ADD_COUNT('b'), kExternalFunction, FUNC_INDEX(1),   // "b" (1)
              ADD_COUNT('c'), kExternalFunction, FUNC_INDEX(2)),  // "c" (2)
      kCodeSectionCode,                 // section code
      U32V_1(1 + arraysize(code) * 3),  // section size
      U32V_1(3),                        // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));

  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());

  // Continue executing functions (eventually triggering tier-up) until the
  // callback is called at least once.
  auto* i_isolate = CcTest::i_isolate();
  ErrorThrower thrower{i_isolate, "TestMoreFunctionsCanBeSerializedCallback"};
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, tester.module_object(), {}, {})
          .ToHandleChecked();
  CHECK(!thrower.error());

  Handle<WasmExportedFunction> exported_functions[]{
      testing::GetExportedFunction(i_isolate, instance, "a").ToHandleChecked(),
      testing::GetExportedFunction(i_isolate, instance, "b").ToHandleChecked(),
      testing::GetExportedFunction(i_isolate, instance, "c").ToHandleChecked()};

  // If Liftoff is enabled, then the callback should only be called after
  // tiering up.
  CHECK_IMPLIES(v8_flags.liftoff, !callback_called);
  while (!callback_called) {
    for (Handle<WasmExportedFunction> exported_function : exported_functions) {
      Execution::Call(i_isolate, exported_function,
                      ReadOnlyRoots{i_isolate}.undefined_value_handle(), 0,
                      nullptr)
          .Check();
    }
    tester.RunCompilerTasks();
  }
}

STREAM_TEST(TestMoreFunctionsCanBeSerializedCallbackWithTimeout) {
  // The "more functions can be serialized" callback will only be triggered with
  // dynamic tiering, so skip this test if dynamic tiering is disabled.
  if (!v8_flags.wasm_dynamic_tiering) return;

  // Reduce the caching threshold to 10 bytes so that our three small functions
  // trigger caching.
  FlagScope<int> caching_threshold(&v8_flags.wasm_caching_threshold, 10);
  FlagScope<int> caching_hard_threshold(&v8_flags.wasm_caching_hard_threshold,
                                        10);
  // Set the caching timeout to 10ms.
  constexpr int kCachingTimeoutMs = 10;
  FlagScope<int> caching_timeout(&v8_flags.wasm_caching_timeout_ms,
                                 kCachingTimeoutMs);
  // Timeouts used in the test below.
  // 1) A very generous timeout during which we expect the caching callback to
  // be called. Some bots are really slow here, especially when executing other
  // tests in parallel, so choose a really large timeout. As we do not expect to
  // run into this timeout, this does not increase test execution time.
  constexpr int caching_expected_timeout_ms = 10'000;
  // 2) A smaller timeout during which we *do not* expect another caching event.
  // We expect to run into this timeout, so do not choose it too long. Also,
  // running into this timeout because it was chosen too small will only make
  // the test pass (flakily), so it is not too critical.
  constexpr int no_caching_expected_timeout_ms = 2 * kCachingTimeoutMs;

  // Use a semaphore to wait for the caching event on the main thread.
  base::Semaphore caching_was_triggered{0};
  StreamTester tester(isolate);
  base::TimeTicks last_time_callback_was_called;
  tester.stream()->SetMoreFunctionsCanBeSerializedCallback(
      [&](const std::shared_ptr<NativeModule> module) {
        base::TimeTicks now = base::TimeTicks::Now();
        int64_t ms_since_last_time =
            (now - last_time_callback_was_called).InMilliseconds();
        // The timeout should have been respected.
        CHECK_LE(kCachingTimeoutMs, ms_since_last_time);
        last_time_callback_was_called = now;
        caching_was_triggered.Signal();
      });

  // This is used when waiting for the semaphore to be signalled. We need to
  // continue running compiler tasks while waiting.
  auto WaitForCaching = [&caching_was_triggered, &tester](int ms) {
    constexpr base::TimeDelta oneMs = base::TimeDelta::FromMilliseconds(1);
    for (int waited_ms = 0; waited_ms < ms; ++waited_ms) {
      if (caching_was_triggered.WaitFor(oneMs)) return true;
      tester.RunCompilerTasks();
    }
    return false;
  };

  uint8_t code[] = {
      ADD_COUNT(U32V_1(0),                   // locals count
                kExprLocalGet, 0, kExprEnd)  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,  // module header
      SECTION(Type,
              ENTRY_COUNT(1),                      // type count
              SIG_ENTRY_x_x(kI32Code, kI32Code)),  // signature entry
      SECTION(Function, ENTRY_COUNT(3), SIG_INDEX(0), SIG_INDEX(0),
              SIG_INDEX(0)),
      SECTION(Export, ENTRY_COUNT(3),                             // 3 exports
              ADD_COUNT('a'), kExternalFunction, FUNC_INDEX(0),   // "a" (0)
              ADD_COUNT('b'), kExternalFunction, FUNC_INDEX(1),   // "b" (1)
              ADD_COUNT('c'), kExternalFunction, FUNC_INDEX(2)),  // "c" (2)
      kCodeSectionCode,                 // section code
      U32V_1(1 + arraysize(code) * 3),  // section size
      U32V_1(3),                        // functions count
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));
  tester.OnBytesReceived(code, arraysize(code));

  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());

  // Create an instance.
  auto* i_isolate = CcTest::i_isolate();
  ErrorThrower thrower{i_isolate, "TestMoreFunctionsCanBeSerializedCallback"};
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, tester.module_object(), {}, {})
          .ToHandleChecked();
  CHECK(!thrower.error());

  // Execute the first function 100 times (which triggers tier-up and hence
  // caching).
  Handle<WasmExportedFunction> func_a =
      testing::GetExportedFunction(i_isolate, instance, "a").ToHandleChecked();
  Handle<Object> receiver = ReadOnlyRoots{i_isolate}.undefined_value_handle();
  for (int i = 0; i < 100; ++i) {
    Execution::Call(i_isolate, func_a, receiver, 0, nullptr).Check();
  }

  // Ensure that background compilation is being executed.
  tester.RunCompilerTasks();

  // The caching callback should be called within the next second (be generous).
  CHECK(WaitForCaching(caching_expected_timeout_ms));

  // There should be no other caching happening within the next 20ms.
  CHECK(!WaitForCaching(no_caching_expected_timeout_ms));

  // Now execute the other two functions 100 times and validate that this
  // triggers another event (but not two).
  Handle<WasmExportedFunction> func_b_and_c[]{
      testing::GetExportedFunction(i_isolate, instance, "b").ToHandleChecked(),
      testing::GetExportedFunction(i_isolate, instance, "c").ToHandleChecked()};
  for (int i = 0; i < 100; ++i) {
    for (auto func : func_b_and_c) {
      Execution::Call(i_isolate, func, receiver, 0, nullptr).Check();
    }
  }

  // Ensure that background compilation is being executed.
  tester.RunCompilerTasks();

  // The caching callback should be called within the next second (be generous).
  CHECK(WaitForCaching(caching_expected_timeout_ms));

  // There should be no other caching happening within the next 20ms.
  CHECK(!WaitForCaching(no_caching_expected_timeout_ms));
}

STREAM_TEST(TestHardCachingThreshold) {
  // The "more functions can be serialized" callback will only be triggered with
  // dynamic tiering, so skip this test if dynamic tiering is disabled.
  if (!v8_flags.wasm_dynamic_tiering) return;

  // Reduce the caching threshold to 1 byte and set the hard threshold to 10
  // bytes so that one small function hits both thresholds.
  FlagScope<int> caching_threshold(&v8_flags.wasm_caching_threshold, 1);
  FlagScope<int> caching_hard_threshold(&v8_flags.wasm_caching_hard_threshold,
                                        10);
  // Set a caching timeout such that the hard threshold has any meaning. This
  // timeout should never be reached.
  constexpr int kCachingTimeoutMs = 1000;
  FlagScope<int> caching_timeout(&v8_flags.wasm_caching_timeout_ms,
                                 kCachingTimeoutMs);

  // Use a semaphore to wait for the caching event on the main thread.
  std::atomic<bool> caching_was_triggered{false};
  StreamTester tester(isolate);
  tester.stream()->SetMoreFunctionsCanBeSerializedCallback(
      [&](const std::shared_ptr<NativeModule>& module) {
        caching_was_triggered = true;
      });

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,  // module header
      SECTION(Type,
              ENTRY_COUNT(1),                      // type count
              SIG_ENTRY_x_x(kI32Code, kI32Code)),  // signature entry
      SECTION(Function, ENTRY_COUNT(1), SIG_INDEX(0)),
      SECTION(Export, ENTRY_COUNT(1),                             // 1 export
              ADD_COUNT('a'), kExternalFunction, FUNC_INDEX(0)),  // "a" (0)
      SECTION(Code,
              U32V_1(1),                              // functions count
              ADD_COUNT(U32V_1(0),                    // locals count
                        kExprLocalGet, 0, kExprEnd))  // body
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK(tester.IsPromiseFulfilled());

  CHECK(!caching_was_triggered);

  // Create an instance.
  auto* i_isolate = CcTest::i_isolate();
  ErrorThrower thrower{i_isolate, "TestMoreFunctionsCanBeSerializedCallback"};
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, tester.module_object(), {}, {})
          .ToHandleChecked();
  CHECK(!thrower.error());
  CHECK(!caching_was_triggered);

  // Execute the function 100 times (which triggers tier-up and hence caching).
  Handle<WasmExportedFunction> func_a =
      testing::GetExportedFunction(i_isolate, instance, "a").ToHandleChecked();
  Handle<Object> receiver = ReadOnlyRoots{i_isolate}.undefined_value_handle();
  for (int i = 0; i < 100; ++i) {
    Execution::Call(i_isolate, func_a, receiver, 0, nullptr).Check();
  }

  // Ensure that background compilation is being executed.
  tester.RunCompilerTasks();

  // Caching should have been triggered now.
  CHECK(caching_was_triggered);
}

// Test that a compile error contains the name of the function, even if the name
// section is not present at the time the error is detected.
STREAM_TEST(TestCompileErrorFunctionName) {
  const uint8_t bytes_module_with_code[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(2),                          // section size
      U32V_1(1),                          // functions count
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(4),                          // section size
      U32V_1(1),                          // functions count
      2,                                  // body size
      0,                                  // local definitions count
      kExprNop,                           // body
  };

  const uint8_t bytes_names[] = {
      kUnknownSectionCode,                 // section code
      U32V_1(11),                          // section size
      4,                                   // section name length
      'n',                                 // section name
      'a',                                 // section name
      'm',                                 // section name
      'e',                                 // section name
      NameSectionKindCode::kFunctionCode,  // name section kind
      4,                                   // name section kind length
      1,                                   // num function names
      0,                                   // function index
      1,                                   // function name length
      'f',                                 // function name
  };

  for (bool late_names : {false, true}) {
    StreamTester tester(isolate);

    tester.OnBytesReceived(bytes_module_with_code,
                           arraysize(bytes_module_with_code));
    if (late_names) tester.RunCompilerTasks();
    tester.OnBytesReceived(bytes_names, arraysize(bytes_names));
    tester.FinishStream();

    tester.RunCompilerTasks();

    CHECK(tester.IsPromiseRejected());
    CHECK_EQ(
        "CompileError: WebAssembly.compileStreaming(): Compiling function "
        "#0:\"f\" failed: function body must end with \"end\" opcode @+26",
        tester.error_message());
  }
}

STREAM_TEST(TestSetModuleCodeSection) {
  StreamTester tester(isolate);

  uint8_t code[] = {
      U32V_1(1),                  // functions count
      U32V_1(4),                  // body size
      U32V_1(0),                  // locals count
      kExprLocalGet, 0, kExprEnd  // body
  };

  const uint8_t bytes[] = {
      WASM_MODULE_HEADER,                 // module header
      kTypeSectionCode,                   // section code
      U32V_1(1 + SIZEOF_SIG_ENTRY_x_x),   // section size
      U32V_1(1),                          // type count
      SIG_ENTRY_x_x(kI32Code, kI32Code),  // signature entry
      kFunctionSectionCode,               // section code
      U32V_1(1 + 1),                      // section size
      U32V_1(1),                          // functions count
      0,                                  // signature index
      kCodeSectionCode,                   // section code
      U32V_1(arraysize(code)),            // section size
  };

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.OnBytesReceived(code, arraysize(code));
  tester.FinishStream();
  tester.RunCompilerTasks();
  CHECK_EQ(tester.native_module()->module()->code.offset(), arraysize(bytes));
  CHECK_EQ(tester.native_module()->module()->code.length(), arraysize(code));
  CHECK(tester.IsPromiseFulfilled());
}

// Test that profiler does not crash when module is only partly compiled.
STREAM_TEST(TestProfilingMidStreaming) {
  StreamTester tester(isolate);
  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  Zone* zone = tester.zone();

  // Build module with one exported (named) function.
  ZoneBuffer buffer(zone);
  {
    TestSignatures sigs;
    WasmModuleBuilder builder(zone);
    WasmFunctionBuilder* f = builder.AddFunction(sigs.v_v());
    uint8_t code[] = {kExprEnd};
    f->EmitCode(code, arraysize(code));
    builder.AddExport(base::VectorOf("foo", 3), f);
    builder.WriteTo(&buffer);
  }

  // Start profiler to force code logging.
  v8::CpuProfiler* cpu_profiler = v8::CpuProfiler::New(isolate);
  cpu_profiler->StartProfiling(v8::String::Empty(isolate),
                               v8::CpuProfilingOptions{});

  // Send incomplete wire bytes and start compilation.
  tester.OnBytesReceived(buffer.begin(), buffer.end() - buffer.begin());
  tester.RunCompilerTasks();

  // Trigger code logging explicitly like the profiler would do.
  CHECK(WasmCode::ShouldBeLogged(i_isolate));
  GetWasmEngine()->LogOutstandingCodesForIsolate(i_isolate);
  CHECK(tester.IsPromisePending());

  // Finalize stream, stop profiler and clean up.
  tester.FinishStream();
  CHECK(tester.IsPromiseFulfilled());
  v8::CpuProfile* profile =
      cpu_profiler->StopProfiling(v8::String::Empty(isolate));
  profile->Delete();
  cpu_profiler->Dispose();
}

STREAM_TEST(TierDownWithError) {
  // https://crbug.com/1160031
  StreamTester tester(isolate);
  Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  Zone* zone = tester.zone();

  ZoneBuffer buffer(zone);
  {
    TestSignatures sigs;
    WasmModuleBuilder builder(zone);
    // Type error at i32.add.
    builder.AddFunction(sigs.v_v())->Emit(kExprI32Add);
    builder.WriteTo(&buffer);
  }

  GetWasmEngine()->EnterDebuggingForIsolate(i_isolate);

  tester.OnBytesReceived(buffer.begin(), buffer.size());
  tester.FinishStream();
  tester.RunCompilerTasks();
}

STREAM_TEST(Regress1334651) {
  StreamTester tester(isolate);

  const uint8_t bytes[] = {WASM_MODULE_HEADER, SECTION(Code, ENTRY_COUNT(0)),
                           SECTION(Unknown, 0)};

  tester.OnBytesReceived(bytes, arraysize(bytes));
  tester.FinishStream();
  tester.RunCompilerTasks();
}

#undef STREAM_TEST

}  // namespace v8::internal::wasm
