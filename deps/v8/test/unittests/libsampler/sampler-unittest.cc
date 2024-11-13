// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Tests of sampler functionalities.

#include "src/libsampler/sampler.h"

#include "include/v8-external.h"
#include "include/v8-function.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using SamplerTest = TestWithContext;

namespace sampler {

namespace {

class TestSamplingThread : public base::Thread {
 public:
  static const int kSamplerThreadStackSize = 64 * 1024;

  explicit TestSamplingThread(Sampler* sampler)
      : Thread(base::Thread::Options("TestSamplingThread",
                                     kSamplerThreadStackSize)),
        sampler_(sampler) {}

  // Implement Thread::Run().
  void Run() override {
    while (sampler_->IsActive()) {
      sampler_->DoSample();
      base::OS::Sleep(base::TimeDelta::FromMilliseconds(1));
    }
  }

 private:
  Sampler* sampler_;
};

class TestSampler : public Sampler {
 public:
  explicit TestSampler(Isolate* isolate) : Sampler(isolate) {}

  void SampleStack(const v8::RegisterState& regs) override {
    void* frames[kMaxFramesCount];
    SampleInfo sample_info;
    isolate()->GetStackSample(regs, frames, kMaxFramesCount, &sample_info);
    if (is_counting_samples_) {
      if (sample_info.vm_state == JS) ++js_sample_count_;
      if (sample_info.vm_state == EXTERNAL) ++external_sample_count_;
    }
  }
};

class TestApiCallbacks {
 public:
  TestApiCallbacks() = default;

  static void Getter(v8::Local<v8::Name> name,
                     const v8::PropertyCallbackInfo<v8::Value>& info) {}

  static void Setter(v8::Local<v8::Name> name, v8::Local<v8::Value> value,
                     const v8::PropertyCallbackInfo<void>& info) {}
};

static void RunSampler(v8::Local<v8::Context> env,
                       v8::Local<v8::Function> function,
                       v8::Local<v8::Value> argv[], int argc,
                       unsigned min_js_samples = 0,
                       unsigned min_external_samples = 0) {
  TestSampler sampler(env->GetIsolate());
  TestSamplingThread thread(&sampler);
  sampler.Start();
  sampler.StartCountingSamples();
  thread.StartSynchronously();
  do {
    function->Call(env, env->Global(), argc, argv).ToLocalChecked();
  } while (sampler.js_sample_count() < min_js_samples ||
           sampler.external_sample_count() < min_external_samples);
  sampler.Stop();
  thread.Join();
}

}  // namespace

static const char* sampler_test_source =
    "function start(count) {\n"
    "  for (var i = 0; i < count; i++) {\n"
    "    var o = instance.foo;\n"
    "    instance.foo = o + 1;\n"
    "  }\n"
    "}\n";

static v8::Local<v8::Function> GetFunction(v8::Local<v8::Context> env,
                                           const char* name) {
  return env->Global()
      ->Get(env, String::NewFromUtf8(env->GetIsolate(), name).ToLocalChecked())
      .ToLocalChecked()
      .As<v8::Function>();
}

TEST_F(SamplerTest, LibSamplerCollectSample) {
  v8::HandleScope scope(isolate());

  v8::Local<v8::FunctionTemplate> func_template =
      v8::FunctionTemplate::New(isolate());
  v8::Local<v8::ObjectTemplate> instance_template =
      func_template->InstanceTemplate();

  TestApiCallbacks accessors;
  v8::Local<v8::External> data = v8::External::New(isolate(), &accessors);
  instance_template->SetNativeDataProperty(NewString("foo"),
                                           &TestApiCallbacks::Getter,
                                           &TestApiCallbacks::Setter, data);
  v8::Local<v8::Function> func =
      func_template->GetFunction(context()).ToLocalChecked();
  v8::Local<v8::Object> instance =
      func->NewInstance(context()).ToLocalChecked();
  context()
      ->Global()
      ->Set(context(), NewString("instance"), instance)
      .FromJust();

  RunJS(sampler_test_source);
  v8::Local<v8::Function> function = GetFunction(context(), "start");

  int32_t repeat_count = 100;
  v8::Local<v8::Value> args[] = {v8::Integer::New(isolate(), repeat_count)};
  RunSampler(context(), function, args, arraysize(args), 100, 100);
}

#ifdef USE_SIGNALS

class CountingSampler : public Sampler {
 public:
  explicit CountingSampler(Isolate* isolate) : Sampler(isolate) {}

  void SampleStack(const v8::RegisterState& regs) override { sample_count_++; }

  int sample_count() { return sample_count_; }
  void set_active(bool active) { SetActive(active); }
  void set_should_record_sample() { SetShouldRecordSample(); }

 private:
  int sample_count_ = 0;
};

TEST_F(SamplerTest, SamplerManager_AddRemoveSampler) {
  SamplerManager* manager = SamplerManager::instance();
  CountingSampler sampler1(isolate());
  sampler1.set_active(true);
  sampler1.set_should_record_sample();
  CHECK_EQ(0, sampler1.sample_count());

  manager->AddSampler(&sampler1);

  RegisterState state;
  manager->DoSample(state);
  CHECK_EQ(1, sampler1.sample_count());

  sampler1.set_active(true);
  sampler1.set_should_record_sample();
  manager->RemoveSampler(&sampler1);
  sampler1.set_active(false);

  manager->DoSample(state);
  CHECK_EQ(1, sampler1.sample_count());
}

TEST_F(SamplerTest, SamplerManager_DoesNotReAdd) {
  // Add the same sampler twice, but check we only get one sample for it.
  SamplerManager* manager = SamplerManager::instance();
  CountingSampler sampler1(isolate());
  sampler1.set_active(true);
  sampler1.set_should_record_sample();
  manager->AddSampler(&sampler1);
  manager->AddSampler(&sampler1);

  RegisterState state;
  manager->DoSample(state);
  CHECK_EQ(1, sampler1.sample_count());
  sampler1.set_active(false);
}

TEST_F(SamplerTest, AtomicGuard_GetNonBlockingSuccess) {
  std::atomic_bool atomic{false};
  {
    AtomicGuard guard(&atomic, false);
    CHECK(guard.is_success());

    AtomicGuard guard2(&atomic, false);
    CHECK(!guard2.is_success());
  }
  AtomicGuard guard(&atomic, false);
  CHECK(guard.is_success());
}

TEST_F(SamplerTest, AtomicGuard_GetBlockingSuccess) {
  std::atomic_bool atomic{false};
  {
    AtomicGuard guard(&atomic);
    CHECK(guard.is_success());

    AtomicGuard guard2(&atomic, false);
    CHECK(!guard2.is_success());
  }
  AtomicGuard guard(&atomic);
  CHECK(guard.is_success());
}

#endif  // USE_SIGNALS

}  // namespace sampler
}  // namespace v8
