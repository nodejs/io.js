// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-enable-sse4-2

(function TestSmiArray() {
  function f() {
    let arr = Array(48).fill(1, 0);
    arr[15] = -6;
    arr[16] = 153;
    arr[17] = -42;
    arr[18] = 0x3fffffff;
    arr[19] = -0x40000000;

    assertEquals(15, arr.indexOf(-6));
    assertEquals(16, arr.indexOf(153));
    assertEquals(17, arr.indexOf(-42));
    assertEquals(18, arr.indexOf(0x3fffffff));
    assertEquals(19, arr.indexOf(-0x40000000));

    assertTrue(arr.includes(-6));
    assertTrue(arr.includes(153));
    assertTrue(arr.includes(-42));
    assertTrue(arr.includes(0x3fffffff));
    assertTrue(arr.includes(-0x40000000));
  }
  %PrepareFunctionForOptimization(f);
  f();
  %OptimizeFunctionOnNextCall(f);
  f();
})();

(function TestObjectArray() {
  return;
  function f() {
    let arr = Array(48).fill(1, 0);
    arr[15] = -6;
    arr[16] = Object;
    arr[17] = 153;
    arr[18] = Array;
    arr[19] = null;

    assertEquals(15, arr.indexOf(-6));
    assertEquals(16, arr.indexOf(Object));
    assertEquals(17, arr.indexOf(153));
    assertEquals(18, arr.indexOf(Array));
    assertEquals(19, arr.indexOf(null));

    assertTrue(arr.includes(-6));
    assertTrue(arr.includes(Object));
    assertTrue(arr.includes(153));
    assertTrue(arr.includes(Array));
    assertTrue(arr.includes(null));
  }
  %PrepareFunctionForOptimization(f);
  f();
  %OptimizeFunctionOnNextCall(f);
  f();
})();
