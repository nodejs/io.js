// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/compiler/function-tester.h"

namespace v8 {
namespace internal {
namespace compiler {

static const char* throws = nullptr;

static const char* load_tests[] = {"var x = a; r = x",
                                   "123",
                                   "0",
                                   "var x = (r = x)",
                                   "undefined",
                                   "undefined",
                                   "var x = (a?1:2); r = x",
                                   "1",
                                   "2",
                                   "const x = a; r = x",
                                   "123",
                                   "0",
                                   "const x = (a?3:4); r = x",
                                   "3",
                                   "4",
                                   "'use strict'; const x = a; r = x",
                                   "123",
                                   "0",
                                   "'use strict'; const x = (r = x)",
                                   throws,
                                   throws,
                                   "'use strict'; const x = (a?5:6); r = x",
                                   "5",
                                   "6",
                                   "'use strict'; let x = a; r = x",
                                   "123",
                                   "0",
                                   "'use strict'; let x = (r = x)",
                                   throws,
                                   throws,
                                   "'use strict'; let x = (a?7:8); r = x",
                                   "7",
                                   "8",
                                   nullptr};

static const char* store_tests[] = {
    "var x = 1; x = a; r = x", "123", "0", "var x = (a?(x=4,2):3); r = x", "2",
    "3", "var x = (a?4:5); x = a; r = x", "123", "0",
    // Assignments to 'const' are SyntaxErrors, handled by the parser,
    // hence we cannot test them here because they are early errors.
    "'use strict'; let x = 1; x = a; r = x", "123", "0",
    "'use strict'; let x = (a?(x=4,2):3); r = x", throws, "3",
    "'use strict'; let x = (a?4:5); x = a; r = x", "123", "0", nullptr};

static void RunVariableTests(const char* source, const char* tests[]) {
  base::EmbeddedVector<char, 512> buffer;

  for (int i = 0; tests[i] != nullptr; i += 3) {
    SNPrintF(buffer, source, tests[i]);
    PrintF("#%d: %s\n", i / 3, buffer.begin());
    FunctionTester T(buffer.begin());

    // Check function with non-falsey parameter.
    if (tests[i + 1] != throws) {
      DirectHandle<Object> r =
          v8::Utils::OpenDirectHandle(*CompileRun(tests[i + 1]));
      T.CheckCall(r, T.Val(123), T.Val("result"));
    } else {
      T.CheckThrows(T.Val(123), T.Val("result"));
    }

    // Check function with falsey parameter.
    if (tests[i + 2] != throws) {
      DirectHandle<Object> r =
          v8::Utils::OpenDirectHandle(*CompileRun(tests[i + 2]));
      T.CheckCall(r, T.Val(0.0), T.Val("result"));
    } else {
      T.CheckThrows(T.Val(0.0), T.Val("result"));
    }
  }
}


TEST(StackLoadVariables) {
  const char* source = "(function(a,r) { %s; return r; })";
  RunVariableTests(source, load_tests);
}


TEST(ContextLoadVariables) {
  const char* source = "(function(a,r) { %s; function f() {x} return r; })";
  RunVariableTests(source, load_tests);
}


TEST(StackStoreVariables) {
  const char* source = "(function(a,r) { %s; return r; })";
  RunVariableTests(source, store_tests);
}


TEST(ContextStoreVariables) {
  const char* source = "(function(a,r) { %s; function f() {x} return r; })";
  RunVariableTests(source, store_tests);
}


TEST(SelfReferenceVariable) {
  FunctionTester T("(function self() { return self; })");

  T.CheckCall(T.function);
  CompileRun("var self = 'not a function'");
  T.CheckCall(T.function);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
