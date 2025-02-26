// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function cmp_float64(which, a, b) {
  if (which == 0) { return a > b; }
  if (which == 1) { return a >= b; }
  if (which == 2) { return a < b; }
  if (which == 3) { return a <= b; }
  if (which == 4) { return a == b; }
  if (which == 5) { return a != b; }
  assertUnreachable();
}

%PrepareFunctionForOptimization(cmp_float64);
// >
assertEquals(false, cmp_float64(0, 10.25, 20.25));
assertEquals(true, cmp_float64(0, 20.25, 10.25));
assertEquals(false, cmp_float64(0, 15.25, 15.25));
assertEquals(false, cmp_float64(0, NaN, 2.35));
assertEquals(false, cmp_float64(0, 2.35, NaN));
assertEquals(false, cmp_float64(0, 2.35, NaN));
// >=
assertEquals(false, cmp_float64(1, 10.25, 20.25));
assertEquals(true, cmp_float64(1, 20.25, 10.25));
assertEquals(true, cmp_float64(1, 15.25, 15.25));
assertEquals(false, cmp_float64(1, NaN, 2.35));
assertEquals(false, cmp_float64(1, 2.35, NaN));
assertEquals(false, cmp_float64(1, 2.35, NaN));
// <
assertEquals(true, cmp_float64(2, 10.25, 20.25));
assertEquals(false, cmp_float64(2, 20.25, 10.25));
assertEquals(false, cmp_float64(2, 15.25, 15.25));
assertEquals(false, cmp_float64(2, NaN, 2.35));
assertEquals(false, cmp_float64(2, 2.35, NaN));
assertEquals(false, cmp_float64(2, 2.35, NaN));
// <=
assertEquals(true, cmp_float64(3, 10.25, 20.25));
assertEquals(false, cmp_float64(3, 20.25, 10.25));
assertEquals(true, cmp_float64(3, 15.25, 15.25));
assertEquals(false, cmp_float64(3, NaN, 2.35));
assertEquals(false, cmp_float64(3, 2.35, NaN));
assertEquals(false, cmp_float64(3, 2.35, NaN));
// ==
assertEquals(true, cmp_float64(4, 10.25, 10.25));
assertEquals(false, cmp_float64(4, 10.25, 15.25));
assertEquals(false, cmp_float64(4, 10.25, NaN));
assertEquals(false, cmp_float64(4, NaN, 15.25));
assertEquals(false, cmp_float64(4, NaN, NaN));
// !=
assertEquals(false, cmp_float64(5, 10.25, 10.25));
assertEquals(true, cmp_float64(5, 10.25, 15.25));
assertEquals(true, cmp_float64(5, 10.25, NaN));
assertEquals(true, cmp_float64(5, NaN, 15.25));
assertEquals(true, cmp_float64(5, NaN, NaN));

    %OptimizeFunctionOnNextCall(cmp_float64);
// >
assertEquals(false, cmp_float64(0, 10.25, 20.25));
assertEquals(true, cmp_float64(0, 20.25, 10.25));
assertEquals(false, cmp_float64(0, 15.25, 15.25));
assertEquals(false, cmp_float64(0, NaN, 2.35));
assertEquals(false, cmp_float64(0, 2.35, NaN));
assertEquals(false, cmp_float64(0, 2.35, NaN));
// >=
assertEquals(false, cmp_float64(1, 10.25, 20.25));
assertEquals(true, cmp_float64(1, 20.25, 10.25));
assertEquals(true, cmp_float64(1, 15.25, 15.25));
assertEquals(false, cmp_float64(1, NaN, 2.35));
assertEquals(false, cmp_float64(1, 2.35, NaN));
assertEquals(false, cmp_float64(1, 2.35, NaN));
// <
assertEquals(true, cmp_float64(2, 10.25, 20.25));
assertEquals(false, cmp_float64(2, 20.25, 10.25));
assertEquals(false, cmp_float64(2, 15.25, 15.25));
assertEquals(false, cmp_float64(2, NaN, 2.35));
assertEquals(false, cmp_float64(2, 2.35, NaN));
assertEquals(false, cmp_float64(2, 2.35, NaN));
// <=
assertEquals(true, cmp_float64(3, 10.25, 20.25));
assertEquals(false, cmp_float64(3, 20.25, 10.25));
assertEquals(true, cmp_float64(3, 15.25, 15.25));
assertEquals(false, cmp_float64(3, NaN, 2.35));
assertEquals(false, cmp_float64(3, 2.35, NaN));
assertEquals(false, cmp_float64(3, 2.35, NaN));
// ==
assertEquals(true, cmp_float64(4, 10.25, 10.25));
assertEquals(false, cmp_float64(4, 10.25, 15.25));
assertEquals(false, cmp_float64(4, 10.25, NaN));
assertEquals(false, cmp_float64(4, NaN, 15.25));
assertEquals(false, cmp_float64(4, NaN, NaN));
// !=
assertEquals(false, cmp_float64(5, 10.25, 10.25));
assertEquals(true, cmp_float64(5, 10.25, 15.25));
assertEquals(true, cmp_float64(5, 10.25, NaN));
assertEquals(true, cmp_float64(5, NaN, 15.25));
assertEquals(true, cmp_float64(5, NaN, NaN));
