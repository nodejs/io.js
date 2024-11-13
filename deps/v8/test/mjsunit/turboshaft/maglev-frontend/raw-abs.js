// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f64_abs(n) {
  let v = n + 3.125;
  return Math.abs(v);
}

%PrepareFunctionForOptimization(f64_abs);
assertEquals(20.625, f64_abs(17.5));
assertEquals(3.375, f64_abs(-6.5));
%OptimizeFunctionOnNextCall(f64_abs);
assertEquals(20.625, f64_abs(17.5));
assertEquals(3.375, f64_abs(-6.5));
assertOptimized(f64_abs);


function i32_abs(n) {
  let v = n * 10 - 28;
  return Math.abs(v);
}

%PrepareFunctionForOptimization(i32_abs);
assertEquals(8, i32_abs(2));
assertEquals(22, i32_abs(5));
%OptimizeFunctionOnNextCall(i32_abs);
assertEquals(8, i32_abs(2));
assertEquals(22, i32_abs(5));
assertOptimized(i32_abs);

// Triggering a deopt in `i32_abs` by calling it with a value that will lead
// doing Int32Abs on min_int, which overflows because the result is not
// representable as a int32.
let min_int = -0x80000000;
let val_for_deopt = ((min_int + 28) / 10) & 0xffffffff;
assertTrue(%IsSmi(val_for_deopt));
assertEquals(-min_int, i32_abs(val_for_deopt));
