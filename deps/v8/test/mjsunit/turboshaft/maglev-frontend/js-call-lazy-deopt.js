// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

%NeverOptimizeFunction(h);
function h(x, d) {
  if (d == 2) { return f(x, d-1); }
  if (d == 1) {
    // Calling `f` with a string as input will trigger an eager deopt of `f`,
    // which will also trigger a lazy deopt of all instances `f` on the caller
    // stack.
    return f("str", d-1);
  }
  return x;
}

function g(x, d) {
  let tmp = x * 12;
  let v = h(x, d);
  return tmp + v;
}

function f(x, d) {
  let a = x + 2;
  return g(a, d);
}

%PrepareFunctionForOptimization(f);
%PrepareFunctionForOptimization(g);
assertEquals(572, f(42, 0));
assertEquals(572, f(42, 0));

%OptimizeFunctionOnNextCall(f);
assertEquals(572, f(42, 0));
assertOptimized(f);
assertEquals("528552NaNstr2", f(42, 2));
assertUnoptimized(f);
