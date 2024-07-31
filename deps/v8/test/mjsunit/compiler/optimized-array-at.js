// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbo-inline-array-builtins --turbofan
// Flags: --no-always-turbofan

// Out of bounds
(() => {
  const arr = [0, 1, 2.5, 3, {}, 5, "a", 7, 8, { "x": 42 }, 10];
  function testArrayAt(a, idx) {
    return a.at(idx);
  }
  %PrepareFunctionForOptimization(testArrayAt);
  testArrayAt(arr, 2);
  assertEquals(arr[2], testArrayAt(arr, 2));
  %OptimizeFunctionOnNextCall(testArrayAt);
  testArrayAt(arr, 2);
  assertOptimized(testArrayAt);
  // Checking out of bounds
  assertEquals(undefined, testArrayAt(arr, -20));
  assertOptimized(testArrayAt);
  assertEquals(undefined, testArrayAt(arr, 20));
  assertOptimized(testArrayAt);
})();

// Smi array
(() => {
  const arr = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
  function testArrayAt(a, idx) {
    return a.at(idx);
  }
  %PrepareFunctionForOptimization(testArrayAt);
  testArrayAt(arr, 2);
  assertEquals(arr[2], testArrayAt(arr, 2));
  %OptimizeFunctionOnNextCall(testArrayAt);
  testArrayAt(arr, 2);
  assertOptimized(testArrayAt);
  // Positive indices
  for (let idx = 0; idx < arr.length; idx++) {
    assertEquals(arr[idx], testArrayAt(arr, idx));
    assertOptimized(testArrayAt);
  }
  // Negative indices
  for (let idx = 1; idx <= arr.length; idx++) {
    assertEquals(arr[arr.length - idx], testArrayAt(arr, -idx));
    assertOptimized(testArrayAt);
  }
})();

// Double array
(() => {
  const arr = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 10.5];
  function testArrayAt(a, idx) {
    return a.at(idx);
  }
  %PrepareFunctionForOptimization(testArrayAt);
  testArrayAt(arr, 2);
  assertEquals(arr[2], testArrayAt(arr, 2));
  %OptimizeFunctionOnNextCall(testArrayAt);
  testArrayAt(arr, 2);
  assertOptimized(testArrayAt);
  // Positive indices
  for (let idx = 0; idx < arr.length; idx++) {
    assertEquals(arr[idx], testArrayAt(arr, idx));
    assertOptimized(testArrayAt);
  }
  // Negative indices
  for (let idx = 1; idx <= arr.length; idx++) {
    assertEquals(arr[arr.length - idx], testArrayAt(arr, -idx));
    assertOptimized(testArrayAt);
  }
})();

// Obj array
(() => {
  const arr = [0, 1, 2.5, 3, {}, 5, "a", 7, 8, { "x": 42 }, 10];
  function testArrayAt(a, idx) {
    return a.at(idx);
  }
  %PrepareFunctionForOptimization(testArrayAt);
  testArrayAt(arr, 2);
  assertEquals(arr[2], testArrayAt(arr, 2));
  %OptimizeFunctionOnNextCall(testArrayAt);
  testArrayAt(arr, 2);
  assertOptimized(testArrayAt);
  // Positive indices
  for (let idx = 0; idx < arr.length; idx++) {
    assertEquals(arr[idx], testArrayAt(arr, idx));
    assertOptimized(testArrayAt);
  }
  // Negative indices
  for (let idx = 1; idx <= arr.length; idx++) {
    assertEquals(arr[arr.length - idx], testArrayAt(arr, -idx));
    assertOptimized(testArrayAt);
  }
})();

// Smi/Double/Obj arrays. This test ensure that:
//  - When different kinds are present in the feedback, they all get properly
//    optimized: |smis|, |doubles| and |objects| will produce 3 different
//    branches in the inlined code.
//
//  - The "needs_fallback_builtin_call" mechanism works as intended: while the
//    |smis|, |doubles| and |objects| cases will be inlined, |dict| should
//    produce a call to the builtin (without deoptimizing).
(() => {
  const smis = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
  const doubles = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 10.5];
  const objects = [0, 1, 2.5, 3, {}, 5, "a", 7, 8, { "x": 42 }, 10];
  const dict = new Array();
  dict[2] = 42;
  dict[10000] = 5;
  assertEquals(smis.length, doubles.length);
  assertEquals(smis.length, objects.length);
  function testArrayAt(a, idx) {
    return a.at(idx);
  }
  %PrepareFunctionForOptimization(testArrayAt);
  testArrayAt(smis, 2);
  testArrayAt(doubles, 2);
  testArrayAt(objects, 2);
  testArrayAt(dict, 2);
  assertEquals(doubles[2], testArrayAt(doubles, 2));
  assertEquals(objects[2], testArrayAt(objects, 2));
  assertEquals(dict[2], testArrayAt(dict, 2));
  assertEquals(smis[2], testArrayAt(smis, 2));
  %OptimizeFunctionOnNextCall(testArrayAt);
  testArrayAt(smis, 2);
  assertOptimized(testArrayAt);
  testArrayAt(doubles, 2);
  assertOptimized(testArrayAt);
  testArrayAt(objects, 2);
  assertOptimized(testArrayAt);
  testArrayAt(dict, 2);
  assertOptimized(testArrayAt);
  // Positive indices
  for (let idx = 0; idx < smis.length; idx++) {
    assertEquals(smis[idx], testArrayAt(smis, idx));
    assertOptimized(testArrayAt);
    assertEquals(doubles[idx], testArrayAt(doubles, idx));
    assertOptimized(testArrayAt);
    assertEquals(objects[idx], testArrayAt(objects, idx));
    assertOptimized(testArrayAt);
    assertEquals(dict[idx], testArrayAt(dict, idx));
    assertOptimized(testArrayAt);
  }
  // Negative indices
  for (let idx = 1; idx <= smis.length; idx++) {
    assertEquals(smis[smis.length - idx], testArrayAt(smis, -idx));
    assertOptimized(testArrayAt);
    assertEquals(doubles[doubles.length - idx], testArrayAt(doubles, -idx));
    assertOptimized(testArrayAt);
    assertEquals(objects[objects.length - idx], testArrayAt(objects, -idx));
    assertOptimized(testArrayAt);
    assertEquals(dict[dict.length - idx], testArrayAt(dict, -idx));
    assertOptimized(testArrayAt);
  }
})();

// Checks that accessing holes in double arrays does not lead to deoptimization.
(() => {
  let doubles = Array();
  doubles[10] = 4.5;
  function testArrayAt(idx) {
    return doubles.at(idx);
  }
  %PrepareFunctionForOptimization(testArrayAt);
  testArrayAt(10);
  %OptimizeFunctionOnNextCall(testArrayAt);
  testArrayAt(10);
  assertOptimized(testArrayAt);
  assertEquals(doubles[10], testArrayAt(10));
  assertOptimized(testArrayAt);
  assertEquals(doubles[2], testArrayAt(2));
  assertOptimized(testArrayAt);
})();

// Checks that accessing holes in SMI array does not lead to deoptimization.
(() => {
  let smis = Array();
  smis[10] = 4;
  function testArrayAt(idx) {
    return smis.at(idx);
  }
  %PrepareFunctionForOptimization(testArrayAt);
  testArrayAt(10);
  %OptimizeFunctionOnNextCall(testArrayAt);
  testArrayAt(10);
  assertOptimized(testArrayAt);
  assertEquals(smis[10], testArrayAt(10));
  assertOptimized(testArrayAt);
  assertEquals(smis[2], testArrayAt(2));
  assertOptimized(testArrayAt);
})();
