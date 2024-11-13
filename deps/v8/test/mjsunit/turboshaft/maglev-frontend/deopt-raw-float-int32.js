// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

// Testing deopt with raw floats and raw integers in the frame state.

%NeverOptimizeFunction(sum);
function sum(...args) {
  return args.reduce((a,b) => a + b, 0);
}

function f(a, b, c) {
  let x = a * 4.25;
  let y = b * 17;
  // This call to `sum` causes `x` and `y` to be part of the frame state.
  let s = sum(a, b);
  let z = b + c;
  // This call is just to use the values we computed before.
  return sum(s, x, y, z);
}

%PrepareFunctionForOptimization(f);
assertEquals(113.39, f(2.36, 5, 6));
assertEquals(113.39, f(2.36, 5, 6));

%OptimizeFunctionOnNextCall(f);
assertEquals(113.39, f(2.36, 5, 6));
assertOptimized(f);
assertEquals(113.93, f(2.36, 5, 6.54));
assertUnoptimized(f);
