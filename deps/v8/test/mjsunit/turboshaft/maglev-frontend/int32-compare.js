// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function cmp_int32(which, a, b) {
  if (which == 0) { return a > b; }
  if (which == 1) { return a >= b; }
  if (which == 2) { return a < b; }
  if (which == 3) { return a <= b; }
}

%PrepareFunctionForOptimization(cmp_int32);
// >
assertEquals(false, cmp_int32(0, 10, 20));
assertEquals(true, cmp_int32(0, 20, 10));
assertEquals(false, cmp_int32(0, 15, 15));
// >=
assertEquals(false, cmp_int32(1, 10, 20));
assertEquals(true, cmp_int32(1, 20, 10));
assertEquals(true, cmp_int32(1, 15, 15));
// <
assertEquals(true, cmp_int32(2, 10, 20));
assertEquals(false, cmp_int32(2, 20, 10));
assertEquals(false, cmp_int32(2, 15, 15));
// <=
assertEquals(true, cmp_int32(3, 10, 20));
assertEquals(false, cmp_int32(3, 20, 10));
assertEquals(true, cmp_int32(3, 15, 15));

%OptimizeFunctionOnNextCall(cmp_int32);
// >
assertEquals(false, cmp_int32(0, 10, 20));
assertEquals(true, cmp_int32(0, 20, 10));
assertEquals(false, cmp_int32(0, 15, 15));
// >=
assertEquals(false, cmp_int32(1, 10, 20));
assertEquals(true, cmp_int32(1, 20, 10));
assertEquals(true, cmp_int32(1, 15, 15));
// <
assertEquals(true, cmp_int32(2, 10, 20));
assertEquals(false, cmp_int32(2, 20, 10));
assertEquals(false, cmp_int32(2, 15, 15));
// <=
assertEquals(true, cmp_int32(3, 10, 20));
assertEquals(false, cmp_int32(3, 20, 10));
assertEquals(true, cmp_int32(3, 15, 15));
assertOptimized(cmp_int32);

// Number equality should deopt when passed an Oddball (because undefined's
// value is NaN, which leads to undefined != undefined without the deopt).
function equal_num(a, b) { return a == b; }

%PrepareFunctionForOptimization(equal_num);
assertEquals(true, equal_num(.5, .5));
%OptimizeFunctionOnNextCall(equal_num);
assertEquals(true, equal_num(.5, .5));
assertOptimized(equal_num);
assertEquals(true, equal_num(undefined, undefined));
assertUnoptimized(equal_num);
