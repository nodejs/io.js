// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-measurement-inl.h"
#include "src/heap/memory-measurement.h"
#include "src/objects/smi.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace heap {

namespace {
DirectHandle<NativeContext> GetNativeContext(
    Isolate* isolate, v8::Local<v8::Context> v8_context) {
  DirectHandle<Context> context = v8::Utils::OpenDirectHandle(*v8_context);
  return direct_handle(context->native_context(), isolate);
}
}  // anonymous namespace

TEST(NativeContextInferrerGlobalObject) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);
  DirectHandle<NativeContext> native_context =
      GetNativeContext(isolate, env.local());
  DirectHandle<JSGlobalObject> global(native_context->global_object(), isolate);
  NativeContextInferrer inferrer;
  Address inferred_context = 0;
  CHECK(inferrer.Infer(isolate, global->map(), *global, &inferred_context));
  CHECK_EQ(native_context->ptr(), inferred_context);
}

TEST(NativeContextInferrerJSFunction) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  DirectHandle<NativeContext> native_context =
      GetNativeContext(isolate, env.local());
  v8::Local<v8::Value> result = CompileRun("(function () { return 1; })");
  DirectHandle<Object> object = Utils::OpenDirectHandle(*result);
  DirectHandle<HeapObject> function = Cast<HeapObject>(object);
  NativeContextInferrer inferrer;
  Address inferred_context = 0;
  CHECK(inferrer.Infer(isolate, function->map(), *function, &inferred_context));
  CHECK_EQ(native_context->ptr(), inferred_context);
}

TEST(NativeContextInferrerJSObject) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  DirectHandle<NativeContext> native_context =
      GetNativeContext(isolate, env.local());
  v8::Local<v8::Value> result = CompileRun("({a : 10})");
  DirectHandle<Object> object = Utils::OpenDirectHandle(*result);
  DirectHandle<HeapObject> function = Cast<HeapObject>(object);
  NativeContextInferrer inferrer;
  Address inferred_context = 0;
  CHECK(inferrer.Infer(isolate, function->map(), *function, &inferred_context));
  CHECK_EQ(native_context->ptr(), inferred_context);
}

TEST(NativeContextStatsMerge) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  DirectHandle<NativeContext> native_context =
      GetNativeContext(isolate, env.local());
  v8::Local<v8::Value> result = CompileRun("({a : 10})");
  DirectHandle<HeapObject> object =
      Cast<HeapObject>(Utils::OpenDirectHandle(*result));
  NativeContextStats stats1, stats2;
  stats1.IncrementSize(native_context->ptr(), object->map(), *object, 10);
  stats2.IncrementSize(native_context->ptr(), object->map(), *object, 20);
  stats1.Merge(stats2);
  CHECK_EQ(30, stats1.Get(native_context->ptr()));
}

TEST(NativeContextStatsArrayBuffers) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  DirectHandle<NativeContext> native_context =
      GetNativeContext(isolate, env.local());
  v8::Local<v8::ArrayBuffer> array_buffer =
      v8::ArrayBuffer::New(CcTest::isolate(), 1000);
  DirectHandle<JSArrayBuffer> i_array_buffer =
      Utils::OpenDirectHandle(*array_buffer);
  NativeContextStats stats;
  stats.IncrementSize(native_context->ptr(), i_array_buffer->map(),
                      *i_array_buffer, 10);
  CHECK_EQ(1010, stats.Get(native_context->ptr()));
}

namespace {

class TestResource : public v8::String::ExternalStringResource {
 public:
  explicit TestResource(uint16_t* data) : data_(data), length_(0) {
    while (data[length_]) ++length_;
  }

  ~TestResource() override { i::DeleteArray(data_); }

  const uint16_t* data() const override { return data_; }

  size_t length() const override { return length_; }

 private:
  uint16_t* data_;
  size_t length_;
};

}  // anonymous namespace

TEST(NativeContextStatsExternalString) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  DirectHandle<NativeContext> native_context =
      GetNativeContext(isolate, env.local());
  const char* c_source = "0123456789";
  uint16_t* two_byte_source = AsciiToTwoByteString(c_source);
  TestResource* resource = new TestResource(two_byte_source);
  Local<v8::String> string =
      v8::String::NewExternalTwoByte(CcTest::isolate(), resource)
          .ToLocalChecked();
  DirectHandle<String> i_string = Utils::OpenDirectHandle(*string);
  NativeContextStats stats;
  stats.IncrementSize(native_context->ptr(), i_string->map(), *i_string, 10);
  CHECK_EQ(10 + 10 * 2, stats.Get(native_context->ptr()));
}

namespace {

class MockPlatform : public TestPlatform {
 public:
  MockPlatform() : mock_task_runner_(new MockTaskRunner()) {}

  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(
      v8::Isolate*, v8::TaskPriority priority) override {
    return mock_task_runner_;
  }

  double Delay() { return mock_task_runner_->Delay(); }

  void PerformTask() { mock_task_runner_->PerformTask(); }

  bool TaskPosted() { return mock_task_runner_->TaskPosted(); }

 private:
  class MockTaskRunner : public v8::TaskRunner {
   public:
    void PostTaskImpl(std::unique_ptr<v8::Task> task,
                      const SourceLocation&) override {}

    void PostDelayedTaskImpl(std::unique_ptr<Task> task,
                             double delay_in_seconds,
                             const SourceLocation&) override {
      task_ = std::move(task);
      delay_ = delay_in_seconds;
    }

    void PostIdleTaskImpl(std::unique_ptr<IdleTask> task,
                          const SourceLocation&) override {
      UNREACHABLE();
    }

    bool NonNestableTasksEnabled() const override { return true; }

    bool NonNestableDelayedTasksEnabled() const override { return true; }

    bool IdleTasksEnabled() override { return false; }

    double Delay() { return delay_; }

    void PerformTask() {
      std::unique_ptr<Task> task = std::move(task_);
      task->Run();
    }

    bool TaskPosted() { return task_.get(); }

   private:
    double delay_ = -1;
    std::unique_ptr<Task> task_;
  };
  std::shared_ptr<MockTaskRunner> mock_task_runner_;
};

class MockMeasureMemoryDelegate : public v8::MeasureMemoryDelegate {
 public:
  bool ShouldMeasure(v8::Local<v8::Context> context) override { return true; }

  void MeasurementComplete(Result result) override {
    // Empty.
  }
};

}  // namespace

TEST_WITH_PLATFORM(RandomizedTimeout, MockPlatform) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = CcTest::isolate();
  std::vector<double> delays;
  for (int i = 0; i < 10; i++) {
    isolate->MeasureMemory(std::make_unique<MockMeasureMemoryDelegate>());
    delays.push_back(platform.Delay());
    platform.PerformTask();
  }
  std::sort(delays.begin(), delays.end());
  CHECK_LT(delays[0], delays.back());
}

TEST(LazyMemoryMeasurement) {
  CcTest::InitializeVM();
  MockPlatform platform;
  CcTest::isolate()->MeasureMemory(
      std::make_unique<MockMeasureMemoryDelegate>(),
      v8::MeasureMemoryExecution::kLazy);
  CHECK(!platform.TaskPosted());
}

TEST(PartiallyInitializedJSFunction) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  DirectHandle<JSFunction> js_function = factory->NewFunctionForTesting(
      factory->NewStringFromAsciiChecked("test"));
  DirectHandle<Context> context(js_function->context(), isolate);

  // 1. Start simulating deserializaiton.
  isolate->RegisterDeserializerStarted();
  // 2. Set the context field to the uninitialized sentintel.
  TaggedField<Object, JSFunction::kContextOffset>::store(
      *js_function, Smi::uninitialized_deserialization_value());
  // 3. Request memory measurement and run all tasks. GC that runs as part
  // of the measurement should not crash.
  CcTest::isolate()->MeasureMemory(
      std::make_unique<MockMeasureMemoryDelegate>(),
      v8::MeasureMemoryExecution::kEager);
  while (v8::platform::PumpMessageLoop(v8::internal::V8::GetCurrentPlatform(),
                                       CcTest::isolate())) {
  }
  // 4. Restore the value and complete deserialization.
  TaggedField<Object, JSFunction::kContextOffset>::store(*js_function,
                                                         *context);
  isolate->RegisterDeserializerFinished();
}

TEST(PartiallyInitializedContext) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  DirectHandle<ScopeInfo> scope_info =
      factory->global_this_binding_scope_info();
  DirectHandle<Context> context = factory->NewScriptContext(
      GetNativeContext(isolate, env.local()), scope_info);
  DirectHandle<Map> map(context->map(), isolate);
  DirectHandle<NativeContext> native_context(map->native_context(), isolate);
  // 1. Start simulating deserializaiton.
  isolate->RegisterDeserializerStarted();
  // 2. Set the native context field to the uninitialized sentintel.
  TaggedField<Object, Map::kConstructorOrBackPointerOrNativeContextOffset>::
      store(*map, Smi::uninitialized_deserialization_value());
  // 3. Request memory measurement and run all tasks. GC that runs as part
  // of the measurement should not crash.
  CcTest::isolate()->MeasureMemory(
      std::make_unique<MockMeasureMemoryDelegate>(),
      v8::MeasureMemoryExecution::kEager);
  while (v8::platform::PumpMessageLoop(v8::internal::V8::GetCurrentPlatform(),
                                       CcTest::isolate())) {
  }
  // 4. Restore the value and complete deserialization.
  TaggedField<Object, Map::kConstructorOrBackPointerOrNativeContextOffset>::
      store(*map, *native_context);
  isolate->RegisterDeserializerFinished();
}

}  // namespace heap
}  // namespace internal
}  // namespace v8
