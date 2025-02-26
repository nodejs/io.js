// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function throw_if_true(b) {
  if (b) { throw "a" }
  return 17;
}

function f(b) {
  try {
    return throw_if_true(b);
  } catch (e) {
    return 42;
  }
}

%PrepareFunctionForOptimization(f);
assertEquals(17, f(false));
assertEquals(17, f(false));
%OptimizeFunctionOnNextCall(f);
assertEquals(17, f(false));
assertOptimized(f);

// Triggering an exception for the 1st time will cause a lazy deopt.
assertEquals(42, f(true));
assertUnoptimized(f);

// When reoptimizing, subsequent exceptions should not deopt the function.
%OptimizeFunctionOnNextCall(f);
assertEquals(17, f(false));
assertEquals(42, f(true));
assertOptimized(f);
