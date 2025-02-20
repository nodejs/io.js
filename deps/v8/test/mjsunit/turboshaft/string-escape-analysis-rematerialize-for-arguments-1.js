// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-string-concat-escape-analysis
// Flags: --turbofan

function bar() {
  return foo.arguments[0] + foo.arguments[0];
}
%NeverOptimizeFunction(bar);

function foo(x) {
  return bar();
}

function main(a, b) {
  let s = a + b;
  return foo(s, s);
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(main);
assertEquals("abab", main("a", "b"));
assertEquals("abab", main("a", "b"));

%OptimizeFunctionOnNextCall(main);
assertEquals("abab", main("a", "b"));
