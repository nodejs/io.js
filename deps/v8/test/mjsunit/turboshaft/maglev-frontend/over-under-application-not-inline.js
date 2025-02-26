// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

// Testing function over- and under-application, where the callee is not
// inlined.

%NeverOptimizeFunction(sum3);
function sum3(a, b, c) {
  return a + b + c;
}

function under_apply(a, b) {
  return sum3(a, b);
}

%PrepareFunctionForOptimization(under_apply);
assertEquals(NaN, under_apply(2.35, 5));

%OptimizeFunctionOnNextCall(under_apply);
assertEquals(NaN, under_apply(2.35, 5));
assertOptimized(under_apply);

function over_apply(a, b) {
  return sum3(a, b, a, b);
}

%PrepareFunctionForOptimization(over_apply);
assertEquals(9.7, over_apply(2.35, 5));

%OptimizeFunctionOnNextCall(over_apply);
assertEquals(9.7, over_apply(2.35, 5));
assertOptimized(over_apply);
