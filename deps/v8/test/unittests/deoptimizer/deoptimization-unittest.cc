// Copyright 2012 the V8 project authors. All rights reserved.
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

#include <stdlib.h>

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/base/platform/platform.h"
#include "src/base/strings.h"
#include "src/codegen/compilation-cache.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/isolate.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using ::v8::base::EmbeddedVector;
using ::v8::base::OS;

class DeoptimizationTest : public TestWithContext {
 public:
  DirectHandle<JSFunction> GetJSFunction(const char* property_name) {
    v8::Local<v8::Function> fun = v8::Local<v8::Function>::Cast(
        context()
            ->Global()
            ->Get(context(), NewString(property_name))
            .ToLocalChecked());
    return i::Cast<i::JSFunction>(v8::Utils::OpenHandle(*fun));
  }
};

// Size of temp buffer for formatting small strings.
#define SMALL_STRING_BUFFER_SIZE 80

// Utility class to set the following runtime flags when constructed and return
// to their default state when destroyed:
//   --minimum-invocations-before-optimization --allow-natives-syntax
//   --always-turbofan --noturbo-inlining
class AlwaysOptimizeAllowNativesSyntaxNoInlining {
 public:
  AlwaysOptimizeAllowNativesSyntaxNoInlining()
      : minimum_invocations_(
            i::v8_flags.minimum_invocations_before_optimization),
        always_turbofan_(i::v8_flags.always_turbofan),
        allow_natives_syntax_(i::v8_flags.allow_natives_syntax),
        turbo_inlining_(i::v8_flags.turbo_inlining) {
    i::v8_flags.minimum_invocations_before_optimization = 0;
    i::v8_flags.always_turbofan = true;
    i::v8_flags.allow_natives_syntax = true;
    i::v8_flags.turbo_inlining = false;
  }

  ~AlwaysOptimizeAllowNativesSyntaxNoInlining() {
    i::v8_flags.minimum_invocations_before_optimization = minimum_invocations_;
    i::v8_flags.always_turbofan = always_turbofan_;
    i::v8_flags.allow_natives_syntax = allow_natives_syntax_;
    i::v8_flags.turbo_inlining = turbo_inlining_;
  }

 private:
  int minimum_invocations_;
  bool always_turbofan_;
  bool allow_natives_syntax_;
  bool turbo_inlining_;
};

// Utility class to set the following runtime flags when constructed and return
// to their default state when destroyed:
//   --minimum-invocations-before-optimization --allow-natives-syntax
//   --noturbo-inlining
class AllowNativesSyntaxNoInlining {
 public:
  AllowNativesSyntaxNoInlining()
      : minimum_invocations_(
            i::v8_flags.minimum_invocations_before_optimization),
        allow_natives_syntax_(i::v8_flags.allow_natives_syntax),
        turbo_inlining_(i::v8_flags.turbo_inlining) {
    i::v8_flags.minimum_invocations_before_optimization = 0;
    i::v8_flags.allow_natives_syntax = true;
    i::v8_flags.turbo_inlining = false;
  }

  ~AllowNativesSyntaxNoInlining() {
    i::v8_flags.minimum_invocations_before_optimization = minimum_invocations_;
    i::v8_flags.allow_natives_syntax = allow_natives_syntax_;
    i::v8_flags.turbo_inlining = turbo_inlining_;
  }

 private:
  int minimum_invocations_;
  bool allow_natives_syntax_;
  bool turbo_inlining_;
};

namespace {
void CheckJsInt32(int expected, const char* variable_name,
                  v8::Local<v8::Context> context) {
  v8::Local<v8::String> str =
      v8::String::NewFromUtf8(context->GetIsolate(), variable_name)
          .ToLocalChecked();
  CHECK_EQ(expected, context->Global()
                         ->Get(context, str)
                         .ToLocalChecked()
                         ->Int32Value(context)
                         .FromJust());
}
}  // namespace

TEST_F(DeoptimizationTest, DeoptimizeSimple) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  // Test lazy deoptimization of a simple function.
  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "function h() { %DeoptimizeFunction(f); }"
        "function g() { count++; h(); }"
        "function f() { g(); };"
        "f();");
  }
  InvokeMajorGC();
  CheckJsInt32(1, "count", context());

  CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));

  // Test lazy deoptimization of a simple function. Call the function after the
  // deoptimization while it is still activated further down the stack.
  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "function g() { count++; %DeoptimizeFunction(f); f(false); }"
        "function f(x) { if (x) { g(); } else { return } };"
        "f(true);");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));
}

TEST_F(DeoptimizationTest, DeoptimizeSimpleWithArguments) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  // Test lazy deoptimization of a simple function with some arguments.
  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "function h(x) { %DeoptimizeFunction(f); }"
        "function g(x, y) { count++; h(x); }"
        "function f(x, y, z) { g(1,x); y+z; };"
        "f(1, \"2\", false);");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));

  // Test lazy deoptimization of a simple function with some arguments. Call the
  // function after the deoptimization while it is still activated further down
  // the stack.
  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "function g(x, y) { count++; %DeoptimizeFunction(f); f(false, 1, y); }"
        "function f(x, y, z) { if (x) { g(x, y); } else { return y + z; } };"
        "f(true, 1, \"2\");");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));
}

TEST_F(DeoptimizationTest, DeoptimizeSimpleNested) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  // Test lazy deoptimization of a simple function. Have a nested function call
  // do the deoptimization.
  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "function h(x, y, z) { return x + y + z; }"
        "function g(z) { count++; %DeoptimizeFunction(f); return z;}"
        "function f(x,y,z) { return h(x, y, g(z)); };"
        "result = f(1, 2, 3);");
    InvokeMajorGC();

    CheckJsInt32(1, "count", context());
    CheckJsInt32(6, "result", context());
    CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));
  }
}

TEST_F(DeoptimizationTest, DeoptimizeRecursive) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  {
    // Test lazy deoptimization of a simple function called recursively. Call
    // the function recursively a number of times before deoptimizing it.
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "var calls = 0;"
        "function g() { count++; %DeoptimizeFunction(f); }"
        "function f(x) { calls++; if (x > 0) { f(x - 1); } else { g(); } };"
        "f(10);");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CheckJsInt32(11, "calls", context());

  v8::Local<v8::Function> fun = v8::Local<v8::Function>::Cast(
      context()->Global()->Get(context(), NewString("f")).ToLocalChecked());
  CHECK(!fun.IsEmpty());
}

TEST_F(DeoptimizationTest, DeoptimizeMultiple) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "function g() { count++;"
        "               %DeoptimizeFunction(f1);"
        "               %DeoptimizeFunction(f2);"
        "               %DeoptimizeFunction(f3);"
        "               %DeoptimizeFunction(f4);}"
        "function f4(x) { g(); };"
        "function f3(x, y, z) { f4(); return x + y + z; };"
        "function f2(x, y) { return x + f3(y + 1, y + 1, y + 1) + y; };"
        "function f1(x) { return f2(x + 1, x + 1) + x; };"
        "result = f1(1);");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CheckJsInt32(14, "result", context());
}

TEST_F(DeoptimizationTest, DeoptimizeConstructor) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "function g() { count++;"
        "               %DeoptimizeFunction(f); }"
        "function f() {  g(); };"
        "result = new f() instanceof f;");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CHECK(context()
            ->Global()
            ->Get(context(), NewString("result"))
            .ToLocalChecked()
            ->IsTrue());

  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "function g() { count++;"
        "               %DeoptimizeFunction(f); }"
        "function f(x, y) { this.x = x; g(); this.y = y; };"
        "result = new f(1, 2);"
        "result = result.x + result.y;");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CheckJsInt32(3, "result", context());
}

TEST_F(DeoptimizationTest, DeoptimizeConstructorMultiple) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  {
    AlwaysOptimizeAllowNativesSyntaxNoInlining options;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "function g() { count++;"
        "               %DeoptimizeFunction(f1);"
        "               %DeoptimizeFunction(f2);"
        "               %DeoptimizeFunction(f3);"
        "               %DeoptimizeFunction(f4);}"
        "function f4(x) { this.result = x; g(); };"
        "function f3(x, y, z) { this.result = new f4(x + y + z).result; };"
        "function f2(x, y) {"
        "    this.result = x + new f3(y + 1, y + 1, y + 1).result + y; };"
        "function f1(x) { this.result = new f2(x + 1, x + 1).result + x; };"
        "result = new f1(1).result;");
  }
  InvokeMajorGC();

  CheckJsInt32(1, "count", context());
  CheckJsInt32(14, "result", context());
}

class DeoptimizationDisableConcurrentRecompilationTest
    : public DeoptimizationTest {
 public:
  void CompileConstructorWithDeoptimizingValueOf() {
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "var deopt = false;"
        "function X() { };"
        "X.prototype.valueOf = function () {"
        "  if (deopt) { count++; %DeoptimizeFunction(f); } return 8"
        "};");
  }
  static void SetUpTestSuite() { i::v8_flags.concurrent_recompilation = false; }
  void TestDeoptimizeBinaryOp(const char* binary_op) {
    v8::base::EmbeddedVector<char, SMALL_STRING_BUFFER_SIZE> f_source_buffer;
    v8::base::SNPrintF(f_source_buffer, "function f(x, y) { return x %s y; };",
                       binary_op);
    char* f_source = f_source_buffer.begin();

    AllowNativesSyntaxNoInlining options;
    // Compile function f and collect to type feedback to insert binary op stub
    // call in the optimized code.
    i::v8_flags.prepare_always_turbofan = true;
    i::v8_flags.minimum_invocations_before_optimization = 0;
    CompileConstructorWithDeoptimizingValueOf();
    RunJS(f_source);
    RunJS(
        "for (var i = 0; i < 5; i++) {"
        "  f(8, new X());"
        "};");

    // Compile an optimized version of f.
    i::v8_flags.always_turbofan = true;
    RunJS(f_source);
    RunJS("f(7, new X());");
    CHECK(!i_isolate()->use_optimizer() ||
          GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));

    // Call f and force deoptimization while processing the binary operation.
    RunJS(
        "deopt = true;"
        "var result = f(7, new X());");
    InvokeMajorGC();
    CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));
  }
};

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeBinaryOperationADDString) {
  ManualGCScope manual_gc_scope(i_isolate());
  AllowNativesSyntaxNoInlining options;

  v8::HandleScope scope(isolate());

  const char* f_source = "function f(x, y) { return x + y; };";

  {
    // Compile function f and collect to type feedback to insert binary op
    // stub call in the optimized code.
    i::v8_flags.prepare_always_turbofan = true;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "var deopt = false;"
        "function X() { };"
        "X.prototype.toString = function () {"
        "  if (deopt) { count++; %DeoptimizeFunction(f); } return 'an X'"
        "};");
    RunJS(f_source);
    RunJS(
        "for (var i = 0; i < 5; i++) {"
        "  f('a+', new X());"
        "};");

    // Compile an optimized version of f.
    i::v8_flags.always_turbofan = true;
    RunJS(f_source);
    RunJS("f('a+', new X());");
    CHECK(!i_isolate()->use_optimizer() ||
          GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));

    // Call f and force deoptimization while processing the binary operation.
    RunJS(
        "deopt = true;"
        "var result = f('a+', new X());");
  }
  InvokeMajorGC();

  CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));
  CheckJsInt32(1, "count", context());
  v8::Local<v8::Value> result =
      context()->Global()->Get(context(), NewString("result")).ToLocalChecked();
  CHECK(result->IsString());
  v8::String::Utf8Value utf8(isolate(), result);
  CHECK_EQ(0, strcmp("a+an X", *utf8));
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeBinaryOperationADD) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  TestDeoptimizeBinaryOp("+");

  CheckJsInt32(1, "count", context());
  CheckJsInt32(15, "result", context());
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeBinaryOperationSUB) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  TestDeoptimizeBinaryOp("-");

  CheckJsInt32(1, "count", context());
  CheckJsInt32(-1, "result", context());
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeBinaryOperationMUL) {
  ManualGCScope manual_gc_scope(i_isolate());

  v8::HandleScope scope(isolate());

  TestDeoptimizeBinaryOp("*");

  CheckJsInt32(1, "count", context());
  CheckJsInt32(56, "result", context());
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeBinaryOperationDIV) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  TestDeoptimizeBinaryOp("/");

  CheckJsInt32(1, "count", context());
  CheckJsInt32(0, "result", context());
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeBinaryOperationMOD) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  TestDeoptimizeBinaryOp("%");

  CheckJsInt32(1, "count", context());
  CheckJsInt32(7, "result", context());
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest, DeoptimizeCompare) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  const char* f_source = "function f(x, y) { return x < y; };";

  {
    AllowNativesSyntaxNoInlining options;
    // Compile function f and collect to type feedback to insert compare ic
    // call in the optimized code.
    i::v8_flags.prepare_always_turbofan = true;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "var deopt = false;"
        "function X() { };"
        "X.prototype.toString = function () {"
        "  if (deopt) { count++; %DeoptimizeFunction(f); } return 'b'"
        "};");
    RunJS(f_source);
    RunJS(
        "for (var i = 0; i < 5; i++) {"
        "  f('a', new X());"
        "};");

    // Compile an optimized version of f.
    i::v8_flags.always_turbofan = true;
    RunJS(f_source);
    RunJS("f('a', new X());");
    CHECK(!i_isolate()->use_optimizer() ||
          GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));

    // Call f and force deoptimization while processing the comparison.
    RunJS(
        "deopt = true;"
        "var result = f('a', new X());");
  }
  InvokeMajorGC();

  CHECK(!GetJSFunction("f")->HasAttachedOptimizedCode(i_isolate()));
  CheckJsInt32(1, "count", context());
  CheckJsInt32(1, "result", context());
  CHECK(context()
            ->Global()
            ->Get(context(), NewString("result"))
            .ToLocalChecked()
            ->IsTrue());
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeLoadICStoreIC) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  // Functions to generate load/store/keyed load/keyed store IC calls.
  const char* f1_source = "function f1(x) { return x.y; };";
  const char* g1_source = "function g1(x) { x.y = 1; };";
  const char* f2_source = "function f2(x, y) { return x[y]; };";
  const char* g2_source = "function g2(x, y) { x[y] = 1; };";

  {
    AllowNativesSyntaxNoInlining options;
    // Compile functions and collect to type feedback to insert ic
    // calls in the optimized code.
    i::v8_flags.prepare_always_turbofan = true;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "var deopt = false;"
        "function X() { };"
        "X.prototype.__defineGetter__('y', function () {"
        "  if (deopt) { count++; %DeoptimizeFunction(f1); };"
        "  return 13;"
        "});"
        "X.prototype.__defineSetter__('y', function () {"
        "  if (deopt) { count++; %DeoptimizeFunction(g1); };"
        "});"
        "X.prototype.__defineGetter__('z', function () {"
        "  if (deopt) { count++; %DeoptimizeFunction(f2); };"
        "  return 13;"
        "});"
        "X.prototype.__defineSetter__('z', function () {"
        "  if (deopt) { count++; %DeoptimizeFunction(g2); };"
        "});");
    RunJS(f1_source);
    RunJS(g1_source);
    RunJS(f2_source);
    RunJS(g2_source);
    RunJS(
        "for (var i = 0; i < 5; i++) {"
        "  f1(new X());"
        "  g1(new X());"
        "  f2(new X(), 'z');"
        "  g2(new X(), 'z');"
        "};");

    // Compile an optimized version of the functions.
    i::v8_flags.always_turbofan = true;
    RunJS(f1_source);
    RunJS(g1_source);
    RunJS(f2_source);
    RunJS(g2_source);
    RunJS("f1(new X());");
    RunJS("g1(new X());");
    RunJS("f2(new X(), 'z');");
    RunJS("g2(new X(), 'z');");
    if (i_isolate()->use_optimizer()) {
      CHECK(GetJSFunction("f1")->HasAttachedOptimizedCode(i_isolate()));
      CHECK(GetJSFunction("g1")->HasAttachedOptimizedCode(i_isolate()));
      CHECK(GetJSFunction("f2")->HasAttachedOptimizedCode(i_isolate()));
      CHECK(GetJSFunction("g2")->HasAttachedOptimizedCode(i_isolate()));
    }

    // Call functions and force deoptimization while processing the ics.
    RunJS(
        "deopt = true;"
        "var result = f1(new X());"
        "g1(new X());"
        "f2(new X(), 'z');"
        "g2(new X(), 'z');");
  }
  InvokeMajorGC();

  CHECK(!GetJSFunction("f1")->HasAttachedOptimizedCode(i_isolate()));
  CHECK(!GetJSFunction("g1")->HasAttachedOptimizedCode(i_isolate()));
  CHECK(!GetJSFunction("f2")->HasAttachedOptimizedCode(i_isolate()));
  CHECK(!GetJSFunction("g2")->HasAttachedOptimizedCode(i_isolate()));
  CheckJsInt32(4, "count", context());
  CheckJsInt32(13, "result", context());
}

TEST_F(DeoptimizationDisableConcurrentRecompilationTest,
       DeoptimizeLoadICStoreICNested) {
  ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  // Functions to generate load/store/keyed load/keyed store IC calls.
  const char* f1_source = "function f1(x) { return x.y; };";
  const char* g1_source = "function g1(x) { x.y = 1; };";
  const char* f2_source = "function f2(x, y) { return x[y]; };";
  const char* g2_source = "function g2(x, y) { x[y] = 1; };";

  {
    AllowNativesSyntaxNoInlining options;
    // Compile functions and collect to type feedback to insert ic
    // calls in the optimized code.
    i::v8_flags.prepare_always_turbofan = true;
    RunJS(
        "var count = 0;"
        "var result = 0;"
        "var deopt = false;"
        "function X() { };"
        "X.prototype.__defineGetter__('y', function () {"
        "  g1(this);"
        "  return 13;"
        "});"
        "X.prototype.__defineSetter__('y', function () {"
        "  f2(this, 'z');"
        "});"
        "X.prototype.__defineGetter__('z', function () {"
        "  g2(this, 'z');"
        "});"
        "X.prototype.__defineSetter__('z', function () {"
        "  if (deopt) {"
        "    count++;"
        "    %DeoptimizeFunction(f1);"
        "    %DeoptimizeFunction(g1);"
        "    %DeoptimizeFunction(f2);"
        "    %DeoptimizeFunction(g2); };"
        "});");
    RunJS(f1_source);
    RunJS(g1_source);
    RunJS(f2_source);
    RunJS(g2_source);
    RunJS(
        "for (var i = 0; i < 5; i++) {"
        "  f1(new X());"
        "  g1(new X());"
        "  f2(new X(), 'z');"
        "  g2(new X(), 'z');"
        "};");

    // Compile an optimized version of the functions.
    i::v8_flags.always_turbofan = true;
    RunJS(f1_source);
    RunJS(g1_source);
    RunJS(f2_source);
    RunJS(g2_source);
    RunJS("f1(new X());");
    RunJS("g1(new X());");
    RunJS("f2(new X(), 'z');");
    RunJS("g2(new X(), 'z');");
    if (i_isolate()->use_optimizer()) {
      CHECK(GetJSFunction("f1")->HasAttachedOptimizedCode(i_isolate()));
      CHECK(GetJSFunction("g1")->HasAttachedOptimizedCode(i_isolate()));
      CHECK(GetJSFunction("f2")->HasAttachedOptimizedCode(i_isolate()));
      CHECK(GetJSFunction("g2")->HasAttachedOptimizedCode(i_isolate()));
    }

    // Call functions and force deoptimization while processing the ics.
    RunJS(
        "deopt = true;"
        "var result = f1(new X());");
  }
  InvokeMajorGC();

  CHECK(!GetJSFunction("f1")->HasAttachedOptimizedCode(i_isolate()));
  CHECK(!GetJSFunction("g1")->HasAttachedOptimizedCode(i_isolate()));
  CHECK(!GetJSFunction("f2")->HasAttachedOptimizedCode(i_isolate()));
  CHECK(!GetJSFunction("g2")->HasAttachedOptimizedCode(i_isolate()));
  CheckJsInt32(1, "count", context());
  CheckJsInt32(13, "result", context());
}

}  // namespace internal
}  // namespace v8
