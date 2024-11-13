// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

// Testing function over- and under-application, where the callee is inlined.

function h() { return 42; }
function g(x, y) {
  if (x == 0) {
    %DeoptimizeNow();
  }
  return x + (y | 17);
}

function f(x, y) {
  return h(x) + g(x) * y;
}

%PrepareFunctionForOptimization(h);
%PrepareFunctionForOptimization(g);
%PrepareFunctionForOptimization(f);
assertEquals(108, f(5, 3));
%OptimizeFunctionOnNextCall(f);
assertEquals(108, f(5, 3));
assertOptimized(f);
assertEquals(93, f(0, 3));
assertUnoptimized(f);
