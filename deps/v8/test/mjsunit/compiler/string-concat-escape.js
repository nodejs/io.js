// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-string-concat-escape-analysis

function g() {}
%NeverOptimizeFunction(g);

function foo(s1, s2, deopt_n) {
  let str1 = "> " + s1;
  if (deopt_n == 0) {
    g();
    return str1;
  }
  let str2 = str1 + " 23";
  if (deopt_n == 1) {
    g();
    return str2;
  }
  let str3 = str2 + " 45";
  if (deopt_n == 2) {
    g();
    return str3;
  }
  return s1 + s2;
}

%PrepareFunctionForOptimization(foo);
assertEquals("a bc d", foo("a b", "c d", -1));

%OptimizeFunctionOnNextCall(foo);
assertEquals("a bc d", foo("a b", "c d", -1));
assertEquals("> a b", foo("a b", "c d", 0));


%ClearFunctionFeedback(foo);
%PrepareFunctionForOptimization(foo);
assertEquals("a bc d", foo("a b", "c d", -1));

%OptimizeFunctionOnNextCall(foo);
assertEquals("a bc d", foo("a b", "c d", -1));
assertEquals("> a b 23", foo("a b", "c d", 1));


%ClearFunctionFeedback(foo);
%PrepareFunctionForOptimization(foo);
assertEquals("a bc d", foo("a b", "c d", -1));

%OptimizeFunctionOnNextCall(foo);
assertEquals("a bc d", foo("a b", "c d", -1));
assertEquals("> a b 23 45", foo("a b", "c d", 2));
