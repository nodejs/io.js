// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/flags/flags.h"
#include "src/objects/contexts.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "test/unittests/compiler/function-tester.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

using RunJSCallsTest = TestWithContext;

TEST_F(RunJSCallsTest, SimpleCall) {
  FunctionTester T(i_isolate(), "(function(foo,a) { return foo(a); })");
  Handle<JSFunction> foo = T.NewFunction("(function(a) { return a; })");

  T.CheckCall(T.NewNumber(3), foo, T.NewNumber(3));
  T.CheckCall(T.NewNumber(3.1), foo, T.NewNumber(3.1));
  T.CheckCall(foo, foo, foo);
  T.CheckCall(T.NewString("Abba"), foo, T.NewString("Abba"));
}

TEST_F(RunJSCallsTest, SimpleCall2) {
  FunctionTester T(i_isolate(), "(function(foo,a) { return foo(a); })");
  FunctionTester U(i_isolate(), "(function(a) { return a; })");

  T.CheckCall(T.NewNumber(3), U.function, T.NewNumber(3));
  T.CheckCall(T.NewNumber(3.1), U.function, T.NewNumber(3.1));
  T.CheckCall(U.function, U.function, U.function);
  T.CheckCall(T.NewString("Abba"), U.function, T.NewString("Abba"));
}

TEST_F(RunJSCallsTest, ConstCall) {
  FunctionTester T(i_isolate(), "(function(foo,a) { return foo(a,3); })");
  FunctionTester U(i_isolate(), "(function (a,b) { return a + b; })");

  T.CheckCall(T.NewNumber(6), U.function, T.NewNumber(3));
  T.CheckCall(T.NewNumber(6.1), U.function, T.NewNumber(3.1));
  T.CheckCall(T.NewString("function (a,b) { return a + b; }3"), U.function,
              U.function);
  T.CheckCall(T.NewString("Abba3"), U.function, T.NewString("Abba"));
}

TEST_F(RunJSCallsTest, ConstCall2) {
  FunctionTester T(i_isolate(), "(function(foo,a) { return foo(a,\"3\"); })");
  FunctionTester U(i_isolate(), "(function (a,b) { return a + b; })");

  T.CheckCall(T.NewString("33"), U.function, T.NewNumber(3));
  T.CheckCall(T.NewString("3.13"), U.function, T.NewNumber(3.1));
  T.CheckCall(T.NewString("function (a,b) { return a + b; }3"), U.function,
              U.function);
  T.CheckCall(T.NewString("Abba3"), U.function, T.NewString("Abba"));
}

TEST_F(RunJSCallsTest, PropertyNamedCall) {
  FunctionTester T(i_isolate(), "(function(a,b) { return a.foo(b,23); })");
  TryRunJS("function foo(y,z) { return this.x + y + z; }");

  T.CheckCall(T.NewNumber(32), T.NewObject("({ foo:foo, x:4 })"),
              T.NewNumber(5));
  T.CheckCall(T.NewString("xy23"), T.NewObject("({ foo:foo, x:'x' })"),
              T.NewString("y"));
  T.CheckCall(T.nan(), T.NewObject("({ foo:foo, y:0 })"), T.NewNumber(3));
}

TEST_F(RunJSCallsTest, PropertyKeyedCall) {
  FunctionTester T(i_isolate(),
                   "(function(a,b) { var f = 'foo'; return a[f](b,23); })");
  TryRunJS("function foo(y,z) { return this.x + y + z; }");

  T.CheckCall(T.NewNumber(32), T.NewObject("({ foo:foo, x:4 })"),
              T.NewNumber(5));
  T.CheckCall(T.NewString("xy23"), T.NewObject("({ foo:foo, x:'x' })"),
              T.NewString("y"));
  T.CheckCall(T.nan(), T.NewObject("({ foo:foo, y:0 })"), T.NewNumber(3));
}

TEST_F(RunJSCallsTest, GlobalCall) {
  FunctionTester T(i_isolate(), "(function(a,b) { return foo(a,b); })");
  TryRunJS("function foo(a,b) { return a + b + this.c; }");
  TryRunJS("var c = 23;");

  T.CheckCall(T.NewNumber(32), T.NewNumber(4), T.NewNumber(5));
  T.CheckCall(T.NewString("xy23"), T.NewString("x"), T.NewString("y"));
  T.CheckCall(T.nan(), T.undefined(), T.NewNumber(3));
}

TEST_F(RunJSCallsTest, LookupCall) {
  FunctionTester T(i_isolate(),
                   "(function(a,b) { with (a) { return foo(a,b); } })");

  TryRunJS("function f1(a,b) { return a.val + b; }");
  T.CheckCall(T.NewNumber(5), T.NewObject("({ foo:f1, val:2 })"),
              T.NewNumber(3));
  T.CheckCall(T.NewString("xy"), T.NewObject("({ foo:f1, val:'x' })"),
              T.NewString("y"));

  TryRunJS("function f2(a,b) { return this.val + b; }");
  T.CheckCall(T.NewNumber(9), T.NewObject("({ foo:f2, val:4 })"),
              T.NewNumber(5));
  T.CheckCall(T.NewString("xy"), T.NewObject("({ foo:f2, val:'x' })"),
              T.NewString("y"));
}

TEST_F(RunJSCallsTest, MismatchCallTooFew) {
  FunctionTester T(i_isolate(), "(function(a,b) { return foo(a,b); })");
  TryRunJS("function foo(a,b,c) { return a + b + c; }");

  T.CheckCall(T.nan(), T.NewNumber(23), T.NewNumber(42));
  T.CheckCall(T.nan(), T.NewNumber(4.2), T.NewNumber(2.3));
  T.CheckCall(T.NewString("abundefined"), T.NewString("a"), T.NewString("b"));
}

TEST_F(RunJSCallsTest, MismatchCallTooMany) {
  FunctionTester T(i_isolate(), "(function(a,b) { return foo(a,b); })");
  TryRunJS("function foo(a) { return a; }");

  T.CheckCall(T.NewNumber(23), T.NewNumber(23), T.NewNumber(42));
  T.CheckCall(T.NewNumber(4.2), T.NewNumber(4.2), T.NewNumber(2.3));
  T.CheckCall(T.NewString("a"), T.NewString("a"), T.NewString("b"));
}

TEST_F(RunJSCallsTest, ConstructorCall) {
  FunctionTester T(i_isolate(),
                   "(function(a,b) { return new foo(a,b).value; })");
  TryRunJS("function foo(a,b) { return { value: a + b + this.c }; }");
  TryRunJS("foo.prototype.c = 23;");

  T.CheckCall(T.NewNumber(32), T.NewNumber(4), T.NewNumber(5));
  T.CheckCall(T.NewString("xy23"), T.NewString("x"), T.NewString("y"));
  T.CheckCall(T.nan(), T.undefined(), T.NewNumber(3));
}

TEST_F(RunJSCallsTest, RuntimeCall) {
  v8_flags.allow_natives_syntax = true;
  FunctionTester T(i_isolate(), "(function(a) { return %IsJSReceiver(a); })");

  T.CheckCall(T.false_value(), T.NewNumber(23), T.undefined());
  T.CheckCall(T.false_value(), T.NewNumber(4.2), T.undefined());
  T.CheckCall(T.false_value(), T.NewString("str"), T.undefined());
  T.CheckCall(T.false_value(), T.true_value(), T.undefined());
  T.CheckCall(T.false_value(), T.false_value(), T.undefined());
  T.CheckCall(T.false_value(), T.undefined(), T.undefined());
  T.CheckCall(T.true_value(), T.NewObject("({})"), T.undefined());
  T.CheckCall(T.true_value(), T.NewObject("([])"), T.undefined());
}

TEST_F(RunJSCallsTest, EvalCall) {
  FunctionTester T(i_isolate(), "(function(a,b) { return eval(a); })");
  DirectHandle<JSObject> g(T.function->context()->global_proxy(), T.isolate);

  T.CheckCall(T.NewNumber(23), T.NewString("17 + 6"), T.undefined());
  T.CheckCall(T.NewString("'Y'; a"), T.NewString("'Y'; a"),
              T.NewString("b-val"));
  T.CheckCall(T.NewString("b-val"), T.NewString("'Y'; b"),
              T.NewString("b-val"));
  T.CheckCall(g, T.NewString("this"), T.undefined());
  T.CheckCall(g, T.NewString("'use strict'; this"), T.undefined());

  TryRunJS("eval = function(x) { return x; }");
  T.CheckCall(T.NewString("17 + 6"), T.NewString("17 + 6"), T.undefined());

  TryRunJS("eval = function(x) { return this; }");
  T.CheckCall(g, T.NewString("17 + 6"), T.undefined());

  TryRunJS("eval = function(x) { 'use strict'; return this; }");
  T.CheckCall(T.undefined(), T.NewString("17 + 6"), T.undefined());
}

TEST_F(RunJSCallsTest, ReceiverPatching) {
  // TODO(turbofan): Note that this test only checks that the function prologue
  // patches an undefined receiver to the global receiver. If this starts to
  // fail once we fix the calling protocol, just remove this test.
  FunctionTester T(i_isolate(), "(function(a) { return this; })");
  DirectHandle<JSObject> g(T.function->context()->global_proxy(), T.isolate);
  T.CheckCall(g, T.undefined());
}

TEST_F(RunJSCallsTest, CallEval) {
  FunctionTester T(i_isolate(),
                   "var x = 42;"
                   "(function () {"
                   "function bar() { return eval('x') };"
                   "return bar;"
                   "})();");

  T.CheckCall(T.NewNumber(42), T.NewString("x"), T.undefined());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
