// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function foo(b) {
  let arr = [0.5, 1.5, /* hole */, 2.5];
  // Turboshaft's Late Load Elimination will replace the following load by a NaN
  // hole constant.
  let val = arr[2];
  if (b) {
    // The NaN-hole constant will flow into the frame state here.
    %DeoptimizeNow();
  }
  // After deopt, {val} should be undefined rather than NaN.
  return val;
}

%PrepareFunctionForOptimization(foo);
assertEquals(undefined, foo(false));

%OptimizeFunctionOnNextCall(foo);
assertEquals(undefined, foo(false));
assertEquals(undefined, foo(true));
