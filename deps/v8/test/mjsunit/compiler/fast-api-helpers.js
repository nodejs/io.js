// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan

// Helper for sequence tests.
function optimize_and_check(func, fast_count, slow_count, expected) {
  %PrepareFunctionForOptimization(func);
  let result = func();
  assertEqualsDelta(expected, result, 0.001);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(func);
  result = func();
  assertEqualsDelta(expected, result, 0.001);
  assertOptimized(func);
  assertEquals(fast_count, fast_c_api.fast_call_count());
  assertEquals(slow_count, fast_c_api.slow_call_count());
}

function ExpectFastCall(func, expected) {
  optimize_and_check(func, 1, 0, expected);
}

function ExpectSlowCall(func, expected) {
  optimize_and_check(func, 0, 1, expected);
}

function assert_throws_and_optimized(func, arg) {
  fast_c_api.reset_counts();
  assertThrows(() => func(arg));
  assertOptimized(func);
  assertEquals(1, fast_c_api.fast_call_count());
}
