// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function-callback.h"
#include "include/v8-function.h"
#include "src/base/optional.h"
#include "src/base/strings.h"
#include "test/cctest/cctest.h"

using ::v8::Context;
using ::v8::External;
using ::v8::Function;
using ::v8::FunctionTemplate;
using ::v8::HandleScope;
using ::v8::Integer;
using ::v8::Isolate;
using ::v8::Local;
using ::v8::MaybeLocal;
using ::v8::Object;
using ::v8::String;
using ::v8::Value;

namespace {

void EmptyHandler(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  ApiTestFuzzer::Fuzz();
}

struct IncumbentTestExpectations {
  Local<Context> incumbent_context;
  Local<Context> function_context;
  int call_count = 0;
};

// This callback checks that the incumbent context equals to the expected one
// and returns the function's context ID (i.e. "globalThis.id").
void FunctionWithIncumbentCheck(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  ApiTestFuzzer::Fuzz();

  IncumbentTestExpectations* expected =
      reinterpret_cast<IncumbentTestExpectations*>(
          info.Data().As<External>()->Value());

  expected->call_count++;

  Isolate* isolate = info.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  CHECK_EQ(expected->function_context, context);
  CHECK_EQ(expected->incumbent_context, isolate->GetIncumbentContext());

  if (info.IsConstructCall()) {
    MaybeLocal<Object> instance = Function::New(context, EmptyHandler)
                                      .ToLocalChecked()
                                      ->NewInstance(context, 0, nullptr);
    info.GetReturnValue().Set(instance.ToLocalChecked());
  } else {
    Local<Value> value =
        context->Global()->Get(context, v8_str("id")).ToLocalChecked();
    info.GetReturnValue().Set(value);
  }
}

// Creates test contexts in the folloing way:
// 1) Each context can access other contexts,
// 2) Each context is equipped with various user-level function that call
//    other functions provided as an argument,
// 3) The helper functions in i-th context are optimized to i-th level:
//    Ignition, Sparkplug, Maglev, TurboFan.
// 4) Each callee function is supposed to be monomorphic in each testing
//    scenario so that the optimizing compiler could inling the callee.
// 5) Each context gets an Api function "f" which checks current context
//    and incumbent context against expected ones. The function can be called
//    as a constructor.
// 6) The tests should setup a call chain in such a way that function "f" is
//    called from each context, thus we can test that GetIncumbentContext
//    works correctly for each compiler.
v8::LocalVector<Context> SetupCrossContextTest(
    Isolate* isolate, IncumbentTestExpectations* expected) {
  const int n = 4;  // Ignition, Sparkplug, Maglev, TurboFan.

  i::v8_flags.allow_natives_syntax = true;
#if V8_ENABLE_SPARKPLUG
  i::v8_flags.baseline_batch_compilation = false;
#endif

  v8::LocalVector<Context> contexts(isolate);

  Local<String> token = v8_str("<security token>");

  for (int i = 0; i < n; i++) {
    Local<Context> context = Context::New(isolate);
    contexts.push_back(context);

    // Allow cross-domain access.
    context->SetSecurityToken(token);

    // Set 'id' property on the Object.prototype in each realm.
    {
      Context::Scope context_scope(context);

      v8::base::ScopedVector<char> src(30);
      v8::base::SNPrintF(src, "Object.prototype.id = %d", i);
      CompileRun(src.begin());
    }
  }

  Local<External> expected_incumbent_context_ptr =
      External::New(isolate, expected);

  // Create cross-realm references in every realm's global object and
  // a constructor function that also checks the incumbent context.
  for (int i = 0; i < n; i++) {
    Local<Context> context = contexts[i];
    Context::Scope context_scope(context);

    // Add "realmX" properties referencing contextX->global.
    for (int j = 0; j < n; j++) {
      Local<Context> another_context = contexts[j];
      v8::base::ScopedVector<char> name(30);
      v8::base::SNPrintF(name, "realm%d", j);

      CHECK(context->Global()
                ->Set(context, v8_str(name.begin()), another_context->Global())
                .FromJust());

      // Check that 'id' property matches the realm index.
      v8::base::ScopedVector<char> src(30);
      v8::base::SNPrintF(src, "realm%d.id", j);
      Local<Value> value = CompileRun(src.begin());
      CHECK_EQ(j, value.As<Integer>()->Value());
    }

    // Create some helper functions so we can chain calls:
    //   call2(call1, call0, f)
    //   call2(call1, construct0, f)
    //   ...
    // and tier them up to the i-th level.
    CompileRun(R"JS(
        // This funcs set is used for collecting the names of the helper
        // functions defined below. We have to query the property names
        // in a separate script because otherwise the function definitions
        // would be hoisted above and it wouldn't be possible to compute the
        // diff of properties set before and after the functions are defined.
        var funcs = Object.getOwnPropertyNames(globalThis);
    )JS");

    CompileRun(R"JS(
        function call_spread(g, ...args) { return g(...args); }
        function call_via_apply(g, args_array) {
          return g.apply(undefined, args_array);
        }
        function call_via_reflect(g, args_array) {
          return Reflect.apply(g, undefined, args_array);
        }
        function construct_spread(g, ...args) { return new g(...args); }
        function construct_via_reflect(g, args_array) {
          return Reflect.construct(g, args_array);
        }

        function call0(g) { return g(); }
        function call0_via_call(g, self) { return g.call(self); }

        function construct0(g) { return new g(); }

        function call1(f, arg) { return f(arg); }
        function call1_via_call(g, self, arg) { return g.call(self, arg); }

        function call2(f, arg1, arg2) { return f(arg1, arg2); }
        function call2_via_call(g, self, arg1, arg2) {
          return g.call(self, arg1, arg2);
        }

        // Get only names of the functions added above.
        funcs = (Object.getOwnPropertyNames(globalThis).filter(
          (name) => { return !funcs.includes(name); }
        ));
        if (funcs.length == 0) {
          // Sanity check that the script is not broken.
          %SystemBreak();
        }
        // Convert names to functions.
        funcs = funcs.map((name) => globalThis[name]);

        // Compile them according to current context's level ('id' value).
        if (id > 0) {
          console.log("=== #"+id);
          if (id == 1 && %IsSparkplugEnabled()) {
            funcs.forEach((f) => {
              %CompileBaseline(f);
            });
          } else if (id == 2 && %IsMaglevEnabled()) {
            funcs.forEach((f) => {
              %PrepareFunctionForOptimization(f);
              %OptimizeMaglevOnNextCall(f);
            });
          } else if (id == 3 && %IsTurbofanEnabled()) {
            funcs.forEach((f) => {
              %PrepareFunctionForOptimization(f);
              %OptimizeFunctionOnNextCall(f);
            });
          }
        }
    )JS");

    Local<Function> func = Function::New(context, FunctionWithIncumbentCheck,
                                         expected_incumbent_context_ptr)
                               .ToLocalChecked();

    v8::base::ScopedVector<char> name(30);
    v8::base::SNPrintF(name, "realm%d.f", static_cast<int>(i));
    func->SetName(v8_str(name.begin()));

    CHECK(context->Global()->Set(context, v8_str("f"), func).FromJust());
  }
  return contexts;
}

void Run(const char* source) { CHECK(!CompileRun(source).IsEmpty()); }

void IncumbentContextTest_Api(bool with_api_incumbent) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);

  IncumbentTestExpectations expected;
  auto contexts = SetupCrossContextTest(isolate, &expected);
  const int n = static_cast<int>(contexts.size());

  // Check calls and construct calls using various sequences of context to
  // context switches.
  for (int i = 0; i < n; i++) {
    Local<Context> context = contexts[i];
    Context::Scope context_scope(context);

    Local<Context> context0 = contexts[0];
    Local<Context> context2 = contexts[2];

    v8::base::Optional<Context::BackupIncumbentScope> incumbent_scope;

    if (with_api_incumbent) {
      // context -> set incumbent (context2) -> context0.
      incumbent_scope.emplace(context2);
      expected.incumbent_context = context2;
      expected.function_context = context0;
    } else {
      // context -> context0.
      expected.incumbent_context = context;
      expected.function_context = context0;
    }

    // realm0.f()
    Local<Function> realm0_f = context0->Global()
                                   ->Get(context, v8_str("f"))
                                   .ToLocalChecked()
                                   .As<Function>();
    realm0_f->Call(context, Undefined(isolate), 0, nullptr).ToLocalChecked();

    // new realm0.f()
    realm0_f->NewInstance(context).ToLocalChecked();
  }
  CHECK_LT(0, expected.call_count);
}

}  // namespace

THREADED_TEST(IncumbentContextTest_Api) { IncumbentContextTest_Api(false); }

THREADED_TEST(IncumbentContextTest_ApiWithIncumbent) {
  IncumbentContextTest_Api(true);
}

THREADED_TEST(IncumbentContextTest_Basic1) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);

  IncumbentTestExpectations expected;
  auto contexts = SetupCrossContextTest(isolate, &expected);
  const int n = static_cast<int>(contexts.size());

  // Check calls and construct calls using various sequences of context to
  // context switches.
  for (int i = 0; i < n; i++) {
    Local<Context> context = contexts[i];
    Context::Scope context_scope(context);

    // context -> context0.
    expected.incumbent_context = context;
    expected.function_context = contexts[0];

    Run("realm0.f()");
    Run("realm0.f.call(realm0)");
    Run("realm0.f.apply(realm0)");
    Run("Reflect.apply(realm0.f, undefined, [])");
    Run("call_spread(realm0.f)");

    Run("new realm0.f()");
  }
  CHECK_LT(0, expected.call_count);
}

THREADED_TEST(IncumbentContextTest_Basic2) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);

  IncumbentTestExpectations expected;
  auto contexts = SetupCrossContextTest(isolate, &expected);
  const int n = static_cast<int>(contexts.size());

  // Check calls and construct calls using various sequences of context to
  // context switches.
  for (int i = 0; i < n; i++) {
    Local<Context> context = contexts[i];
    Context::Scope context_scope(context);

    // context -> context -> context0.
    expected.incumbent_context = context;
    expected.function_context = contexts[0];

    Run("call0(realm0.f)");
    Run("call0_via_call(realm0.f, realm0)");
    Run("call_via_apply(realm0.f, [realm0])");
    Run("call_via_reflect(realm0.f, [realm0])");
    Run("call_spread(realm0.f, /* args */ 1, 2, 3)");

    Run("construct0(realm0.f)");
    Run("Reflect.construct(realm0.f, [])");
    Run("construct_spread(realm0.f, /* args */ 1, 2, 3)");
  }
  CHECK_LT(0, expected.call_count);
}

THREADED_TEST(IncumbentContextTest_WithBuiltins3) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);

  IncumbentTestExpectations expected;
  auto contexts = SetupCrossContextTest(isolate, &expected);
  const int n = static_cast<int>(contexts.size());

  // Check calls and construct calls using various sequences of context to
  // context switches.
  for (int i = 0; i < n; i++) {
    Local<Context> context = contexts[i];
    Context::Scope context_scope(context);

    // context -> context2 -> context1 -> context.
    expected.incumbent_context = contexts[1];
    expected.function_context = context;

    Run("realm2.call1(realm1.call0, f)");
    Run("realm2.call1_via_call(realm1.call0, realm1, f)");
    Run("realm2.call_via_apply(realm1.call0, [f])");
    Run("realm2.call_via_reflect(realm1.call0, [f])");
    Run("realm2.call_spread(realm1.call_spread, f, 1, 2, 3)");

    Run("realm2.call1(realm1.construct0, f)");
    Run("realm2.call_spread(realm1.construct_via_reflect, f, [1, 2, 3])");
    Run("realm2.call_spread(realm1.construct_spread, f, 1, 2, 3)");
  }
  CHECK_LT(0, expected.call_count);
}

THREADED_TEST(IncumbentContextTest_WithBuiltins4) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);

  IncumbentTestExpectations expected;
  auto contexts = SetupCrossContextTest(isolate, &expected);
  const int n = static_cast<int>(contexts.size());

  // Check calls and construct calls using various sequences of context to
  // context switches.
  for (int i = 0; i < n; i++) {
    Local<Context> context = contexts[i];
    Context::Scope context_scope(context);

    // context -> context0 -> context -> context1.
    expected.incumbent_context = context;
    expected.function_context = contexts[1];

    Run("realm0.call1(call0, realm1.f)");
    Run("realm0.call1_via_call(call0, undefined, realm1.f)");
    Run("realm0.call_via_apply(call0, [realm1.f])");
    Run("realm0.call_via_reflect(call0, [realm1.f])");
    Run("realm0.call_spread(call_spread, realm1.f, 1, 2, 3)");

    Run("realm0.call1(construct0, realm1.f)");
    Run("realm0.call_spread(construct_via_reflect, realm1.f, [1, 2])");
    Run("realm0.call_spread(construct_spread, realm1.f, 1, 2)");
  }
  CHECK_LT(0, expected.call_count);
}
