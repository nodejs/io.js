// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function float_f(x) {
  let x1 = Math.abs(x);
  let x2 = Math.acos(x);
  let x3 = Math.acosh(x);
  let x4 = Math.asin(x);
  let x5 = Math.asinh(x);
  let x6 = Math.atan(x);
  let x7 = Math.atanh(x);
  let x8 = Math.cbrt(x);
  let x9 = Math.cos(x);
  let x10 = Math.cosh(x);
  let x11 = Math.exp(x);
  let x12 = Math.expm1(x);
  let x13 = Math.log(x);
  let x14 = Math.log1p(x);
  let x15 = Math.log10(x);
  let x16 = Math.log2(x);
  let x17 = Math.sin(x);
  let x18 = Math.sinh(x);
  let x19 = Math.tan(x);
  let x20 = Math.tanh(x);

  return [
    x1,  x2,  x3,  x4,  x5,  x6,  x7,  x8,  x9,  x10,
    x11, x12, x13, x14, x15, x16, x17, x18, x19, x20
  ];
}

%PrepareFunctionForOptimization(float_f);
let expected_1 = float_f(0.758);
let expected_2 = float_f(2);

%OptimizeFunctionOnNextCall(float_f);
assertEquals(expected_1, float_f(0.758));
assertEquals(expected_2, float_f(2));
assertOptimized(float_f);
