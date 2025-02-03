// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function getArgs() {
  return arguments;
}
function sum(a, b, c) {
  return a + b + c;
}
function foo(a, b, c) {
  var args = getArgs(b, c, 42);
  return sum.apply(this, args);
}

%PrepareFunctionForOptimization(getArgs);
%PrepareFunctionForOptimization(sum);
%PrepareFunctionForOptimization(foo);
assertEquals(47, foo(1, 2, 3));
assertEquals(47, foo(1, 2, 3));
%OptimizeFunctionOnNextCall(foo);
assertEquals(47, foo(1, 2, 3));
