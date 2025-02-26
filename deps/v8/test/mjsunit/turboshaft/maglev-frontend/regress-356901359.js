// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function foo(a, b) {
  return a == b;
}

%PrepareFunctionForOptimization(foo);
assertTrue(foo(1, true));

%OptimizeFunctionOnNextCall(foo);
assertTrue(foo(1, true));
assertFalse(foo(0, true));
assertOptimized(foo);

// Passing non-boolean non-number to {foo} will make it deopt.
assertTrue(foo());
assertUnoptimized(foo);
