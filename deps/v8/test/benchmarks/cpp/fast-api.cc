// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-context.h"
#include "include/v8-fast-api-calls.h"
#include "include/v8-internal.h"
#include "include/v8-local-handle.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-template.h"
#include "src/base/macros.h"
#include "test/benchmarks/cpp/benchmark-utils.h"
#include "third_party/google_benchmark_chrome/src/include/benchmark/benchmark.h"

namespace {

v8::Local<v8::String> v8_str(const char* x) {
  return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), x).ToLocalChecked();
}

class FastApiBenchmark : public v8::benchmarking::BenchmarkWithIsolate {
 public:
  static void RegularCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  }

  static void FastCallback(v8::Local<v8::Value> receiver,
                           v8::FastApiCallbackOptions& options) {}

  static uint32_t FastStringValueView(v8::Local<v8::Value> receiver,
                                      v8::Local<v8::Value> value,
                                      v8::FastApiCallbackOptions& options) {
    v8::Local<v8::String> str = value.As<v8::String>();
    v8::String::ValueView view(options.isolate, str);
    return view.length();
  }

  static uint32_t FastOneByteString(v8::Local<v8::Value> receiver,
                                    const v8::FastOneByteString& str,
                                    v8::FastApiCallbackOptions& options) {
    return str.length;
  }

  static void RegularString(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::String> str = info[0].As<v8::String>();
    v8::String::ValueView view(info.GetIsolate(), str);
    info.GetReturnValue().Set(view.length());
  }

  void SetUp(::benchmark::State& state) override {
    auto* isolate = v8_isolate();
    v8::HandleScope handle_scope(isolate);

    auto proxy_template_function = v8::FunctionTemplate::New(isolate);
    auto object_template = proxy_template_function->InstanceTemplate();
    {
      v8::CFunction fast_callback =
          v8::CFunction::Make(FastApiBenchmark::FastCallback);

      object_template->Set(
          isolate, "fastApiCall",
          v8::FunctionTemplate::New(
              isolate, FastApiBenchmark::RegularCallback,
              v8::Local<v8::Value>(), v8::Local<v8::Signature>(), 1,
              v8::ConstructorBehavior::kThrow,
              v8::SideEffectType::kHasSideEffect, &fast_callback));
    }
    {
      object_template->Set(isolate, "regularApiCall",
                           v8::FunctionTemplate::New(
                               isolate, FastApiBenchmark::RegularCallback));
    }
    {
      v8::CFunction fast_callback =
          v8::CFunction::Make(FastApiBenchmark::FastOneByteString);

      object_template->Set(
          isolate, "fastOneByteString",
          v8::FunctionTemplate::New(
              isolate, FastApiBenchmark::RegularString, v8::Local<v8::Value>(),
              v8::Local<v8::Signature>(), 1, v8::ConstructorBehavior::kThrow,
              v8::SideEffectType::kHasSideEffect, &fast_callback));
    }
    {
      v8::CFunction fast_callback =
          v8::CFunction::Make(FastApiBenchmark::FastStringValueView);

      object_template->Set(
          isolate, "fastStringValueView",
          v8::FunctionTemplate::New(
              isolate, FastApiBenchmark::RegularString, v8::Local<v8::Value>(),
              v8::Local<v8::Signature>(), 1, v8::ConstructorBehavior::kThrow,
              v8::SideEffectType::kHasSideEffect, &fast_callback));
    }
    {
      object_template->Set(
          isolate, "regularString",
          v8::FunctionTemplate::New(isolate, FastApiBenchmark::RegularString));
    }

    v8::Local<v8::Context> context =
        v8::Context::New(isolate, nullptr, object_template);

    context_.Reset(isolate, context);
    context->Enter();
  }

  void TearDown(::benchmark::State& state) override {
    auto* isolate = v8_isolate();
    v8::HandleScope handle_scope(isolate);
    auto context = context_.Get(isolate);
    context->Exit();
    context_.Reset();
  }

  v8::Local<v8::Script> CompileBenchmarkScript(const char* source) {
    v8::EscapableHandleScope handle_scope(v8_isolate());
    v8::Local<v8::Context> context = v8_context();
    v8::Local<v8::String> v8_source = v8_str(source);
    v8::Local<v8::Script> script =
        v8::Script::Compile(context, v8_source).ToLocalChecked();
    return handle_scope.Escape(script);
  }

 protected:
  v8::Local<v8::Context> v8_context() { return context_.Get(v8_isolate()); }

  v8::Global<v8::Context> context_;
};

}  // namespace

BENCHMARK_F(FastApiBenchmark, FastCallToEmptyCallback)(benchmark::State& st) {
  const char* kScript =
      "function invoke() { globalThis.fastApiCall(); }"
      "\%PrepareFunctionForOptimization(invoke);"
      "invoke();"
      "\%OptimizeFunctionOnNextCall(invoke);"
      "for (var i =0; i < 1_000_000; i++) invoke();";

  v8::HandleScope handle_scope(v8_isolate());
  v8::Local<v8::Context> context = v8_context();
  v8::Local<v8::Script> script = CompileBenchmarkScript(kScript);
  v8::HandleScope benchmark_handle_scope(v8_isolate());
  for (auto _ : st) {
    USE(_);
    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK_F(FastApiBenchmark, RegularCallToEmptyCallback)
(benchmark::State& st) {
  const char* kScript =
      "function invoke() { globalThis.regularApiCall(); }"
      "\%PrepareFunctionForOptimization(invoke);"
      "invoke();"
      "\%OptimizeFunctionOnNextCall(invoke);"
      "for (var i =0; i < 1_000_000; i++) invoke();";

  v8::HandleScope handle_scope(v8_isolate());
  v8::Local<v8::Context> context = v8_context();
  v8::Local<v8::Script> script = CompileBenchmarkScript(kScript);
  v8::HandleScope benchmark_handle_scope(v8_isolate());
  for (auto _ : st) {
    USE(_);
    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK_F(FastApiBenchmark, FastOneByteString)(benchmark::State& st) {
  const char* kScript =
      "function invoke() { globalThis.fastOneByteString('Hello World'); }"
      "\%PrepareFunctionForOptimization(invoke);"
      "invoke();"
      "\%OptimizeFunctionOnNextCall(invoke);"
      "for (var i =0; i < 1_000_000; i++) invoke();";

  v8::HandleScope handle_scope(v8_isolate());
  v8::Local<v8::Context> context = v8_context();
  v8::Local<v8::Script> script = CompileBenchmarkScript(kScript);
  v8::HandleScope benchmark_handle_scope(v8_isolate());
  for (auto _ : st) {
    USE(_);
    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK_F(FastApiBenchmark, FastStringValueView)(benchmark::State& st) {
  const char* kScript =
      "function invoke() { globalThis.fastStringValueView('Hello World'); }"
      "\%PrepareFunctionForOptimization(invoke);"
      "invoke();"
      "\%OptimizeFunctionOnNextCall(invoke);"
      "for (var i =0; i < 1_000_000; i++) invoke();";

  v8::HandleScope handle_scope(v8_isolate());
  v8::Local<v8::Context> context = v8_context();
  v8::Local<v8::Script> script = CompileBenchmarkScript(kScript);
  v8::HandleScope benchmark_handle_scope(v8_isolate());
  for (auto _ : st) {
    USE(_);
    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK_F(FastApiBenchmark, RegularString)(benchmark::State& st) {
  const char* kScript =
      "function invoke() { globalThis.regularString('Hello World'); }"
      "\%PrepareFunctionForOptimization(invoke);"
      "invoke();"
      "\%OptimizeFunctionOnNextCall(invoke);"
      "for (var i =0; i < 1_000_000; i++) invoke();";

  v8::HandleScope handle_scope(v8_isolate());
  v8::Local<v8::Context> context = v8_context();
  v8::Local<v8::Script> script = CompileBenchmarkScript(kScript);
  v8::HandleScope benchmark_handle_scope(v8_isolate());
  for (auto _ : st) {
    USE(_);
    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
    benchmark::DoNotOptimize(result);
  }
}
