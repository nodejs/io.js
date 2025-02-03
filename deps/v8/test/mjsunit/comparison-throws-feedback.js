// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

function testThrowsNoDeopt(fn) {
  %PrepareFunctionForOptimization(fn);
  for (let i = 0; i < 5; i++) assertThrows(fn, TypeError);
  %OptimizeFunctionOnNextCall(fn);
  assertThrows(fn, TypeError);
  // Assert that the function is still optimized, i.e. no deopt happened when
  // calling it.
  assertOptimized(fn);
}

function testNoDeopt(fn, expected) {
  %PrepareFunctionForOptimization(fn);
  for (let i = 0; i < 5; i++) fn();
  %OptimizeFunctionOnNextCall(fn);
  assertEquals(expected, fn());
  assertOptimized(fn);
}

let symbol = Symbol("test");
testThrowsNoDeopt(() => 2 < symbol);
testThrowsNoDeopt(() => 2 > symbol);
testThrowsNoDeopt(() => 2 <= symbol);
testThrowsNoDeopt(() => 2 >= symbol);
testNoDeopt(() => 2 == symbol, false);
let invalidValueOf = {valueOf: () => +2n};
testThrowsNoDeopt(() => 2 < invalidValueOf);
testThrowsNoDeopt(() => 2 > invalidValueOf);
testThrowsNoDeopt(() => 2 <= invalidValueOf);
testThrowsNoDeopt(() => 2 >= invalidValueOf);
testThrowsNoDeopt(() => 2 == invalidValueOf);
