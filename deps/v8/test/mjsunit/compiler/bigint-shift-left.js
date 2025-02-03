// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

(function OptimizeAndTest() {
  function ShiftLeft(a, b) {
    return a << b;
  }
  %PrepareFunctionForOptimization(ShiftLeft);
  assertEquals(0n, ShiftLeft(0n, 42n));
  assertEquals(-42n, ShiftLeft(-42n, 0n));
  assertEquals(2n ** 42n, ShiftLeft(1n, 42n));
  assertEquals(-2n, ShiftLeft(-(2n ** 512n), -511n));
  assertEquals(-1n, ShiftLeft(-(2n ** 512n), -513n));

  %OptimizeFunctionOnNextCall(ShiftLeft);
  assertEquals(0n, ShiftLeft(0n, 42n));
  assertEquals(-42n, ShiftLeft(-42n, 0n));
  assertEquals(2n ** 42n, ShiftLeft(1n, 42n));
  assertEquals(-2n, ShiftLeft(-(2n ** 512n), -511n));
  assertEquals(-1n, ShiftLeft(-(2n ** 512n), -513n));
  assertOptimized(ShiftLeft);

  assertThrows(() => ShiftLeft(1n, 2n ** 30n), RangeError);
  assertUnoptimized(ShiftLeft);
})();

(function OptimizeAndTest() {
  function ShiftLeftByPositive(a) {
    return BigInt.asIntN(62, a << 42n);
  }
  %PrepareFunctionForOptimization(ShiftLeftByPositive);
  assertEquals(0n, ShiftLeftByPositive(0n));

  %OptimizeFunctionOnNextCall(ShiftLeftByPositive);
  assertEquals(0n, ShiftLeftByPositive(0n));
  assertEquals(2n ** 42n, ShiftLeftByPositive(1n));
  assertEquals(2n ** 42n, ShiftLeftByPositive(1n + 2n ** 62n));
  assertEquals(-(2n ** 42n), ShiftLeftByPositive(-1n - 2n ** 64n));
  assertOptimized(ShiftLeftByPositive);

  assertThrows(() => ShiftLeftByPositive(0), TypeError);
  assertUnoptimized(ShiftLeftByPositive);
})();

(function OptimizeAndTest() {
  const minus42 = -42n;
  function ShiftLeftByNegative(a) {
    return BigInt.asIntN(62, BigInt.asUintN(64, a) << minus42);
  }
  %PrepareFunctionForOptimization(ShiftLeftByNegative);
  assertEquals(0n, ShiftLeftByNegative(0n));

  %OptimizeFunctionOnNextCall(ShiftLeftByNegative);
  assertEquals(0n, ShiftLeftByNegative(42n));
  assertEquals(4194303n, ShiftLeftByNegative(-42n));
  assertEquals(2n ** 20n, ShiftLeftByNegative(1n + 2n ** 62n));
  assertEquals(3145727n, ShiftLeftByNegative(-1n - 2n ** 62n - 2n ** 64n));
  assertOptimized(ShiftLeftByNegative);

  assertThrows(() => ShiftLeftByNegative(0), TypeError);
  if (%Is64Bit()) {
    // BigInt truncation is not inlined on 32-bit platforms so there is no
    // checks for BigInt, thus deopt will not be triggered.
    assertUnoptimized(ShiftLeftByNegative);
  }
})();

(function OptimizeAndTest() {
  function ShiftLeftBy64(a) {
    return BigInt.asIntN(62, a << 64n);
  }
  %PrepareFunctionForOptimization(ShiftLeftBy64);
  assertEquals(0n, ShiftLeftBy64(0n));

  %OptimizeFunctionOnNextCall(ShiftLeftBy64);
  assertEquals(0n, ShiftLeftBy64(0n));
  assertEquals(0n, ShiftLeftBy64(1n));
  assertEquals(0n, ShiftLeftBy64(1n + 2n ** 62n));
  assertEquals(0n, ShiftLeftBy64(-1n - 2n ** 64n));
  assertOptimized(ShiftLeftBy64);

  assertThrows(() => ShiftLeftBy64(0), TypeError);
  assertUnoptimized(ShiftLeftBy64);
})();

(function OptimizeAndTest() {
  const bi = 2n ** 62n;
  function ShiftLeftByLarge(a) {
    return BigInt.asIntN(62, a << bi);
  }
  %PrepareFunctionForOptimization(ShiftLeftByLarge);
  assertEquals(0n, ShiftLeftByLarge(0n));

  %OptimizeFunctionOnNextCall(ShiftLeftByLarge);
  assertEquals(0n, ShiftLeftByLarge(0n));
  if (%Is64Bit()) {
    // After optimization, a truncated left shift will not throw a
    // BigIntTooBig exception just as truncated addition.
    assertEquals(0n, ShiftLeftByLarge(1n));
    assertEquals(0n, ShiftLeftByLarge(1n + 2n ** 62n));
    assertEquals(0n, ShiftLeftByLarge(-1n - 2n ** 64n));
  }
  assertOptimized(ShiftLeftByLarge);

  assertThrows(() => ShiftLeftByLarge(0), TypeError);
  assertUnoptimized(ShiftLeftByLarge);
})();
