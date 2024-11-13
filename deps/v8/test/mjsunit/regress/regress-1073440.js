// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --allow-natives-syntax --no-always-turbofan

function foo(n) {
  try {
    let v = 0n;
    for (let i = 0; i < n; ++i) {
      v = 3n + v;
      v = i;
    }
  } catch (e) {
    return 1;
  }
  return 0;
}

%PrepareFunctionForOptimization(foo);
assertEquals(foo(1), 0);
assertEquals(foo(1), 0);
%OptimizeFunctionOnNextCall(foo);
assertEquals(foo(1), 0);
assertOptimized(foo);
%PrepareFunctionForOptimization(foo);
assertEquals(foo(2), 1);
if (%Is64Bit()) {
  assertUnoptimized(foo);
}
// Check that we learned something and do not loop deoptimizations.
%OptimizeFunctionOnNextCall(foo);
assertEquals(foo(1), 0);
assertOptimized(foo);
assertEquals(foo(2), 1);
