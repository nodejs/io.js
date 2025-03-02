// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-string-concat-escape-analysis
// Flags: --turbofan

function bar() {
  return foo.arguments[0] + foo.arguments[0] + foo.arguments[1].x + foo.arguments[1].x;
}
%NeverOptimizeFunction(bar);

function foo(x, y) {
  return bar();
}

function main(a, b) {
  let s = a + b;
  let o = { x : a };
  return foo(s, o, s, o);
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(main);
assertEquals("ababaa", main("a", "b"));
assertEquals("ababaa", main("a", "b"));

%OptimizeFunctionOnNextCall(main);
assertEquals("ababaa", main("a", "b"));
