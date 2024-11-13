// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(a, b, c, d) {
  let x1 = a + b;
  let x2 = a - b;
  let x3 = a * b;
  let x4 = a / b;
  let x5 = a % b;
  let x6 = a ** b;
  let x7 = a & b;
  let x8 = a | b;
  let x9 = a ^ b;
  let x10 = a << b;
  let x11 = a >> b;
  let x12 = a >>> b;
  let x13 = ~a;
  let x14 = -a;
  let x15 = a < b;
  let x16 = a <= b;
  let x17 = a > b;
  let x18 = a >= b;
  let x19 = a == b;
  let x20 = a === b;
  // It's important to do the increment/decrement last, because they lead to
  // having numeric alternatives for {a} and {b} in Maglev, which leads Maglev
  // to compiling subsequent <, <=, >, and >= to Float64 comparisons despite
  // their generic feedback.
  let x21 = a++;
  let x22 = b--;
  let x23 = ++c;
  let x24 = --d;

  return [
    x1,  x2,  x3,  x4,  x5,  x6,  x7,  x8,  x9,  x10, x11, x12,
    x13, x14, x15, x16, x17, x18, x19, x20, x21, x22, x23, x24
  ];
}

%PrepareFunctionForOptimization(f);
let expected_1 = f(1, 2, 1, 2);
let expected_2 = f("1", 2, "1", 2);
let expected_3 = f(2, "1", 2, "1");
let expected_4 = f([], 1, [], 1);
let expected_5 = f({}, 1, {}, 1);
%OptimizeFunctionOnNextCall(f);
assertEquals(expected_1, f(1, 2, 1, 2));
assertEquals(expected_2, f("1", 2, "1", 2));
assertEquals(expected_3, f(2, "1", 2, "1"));
assertEquals(expected_4, f([], 1, [], 1));
assertEquals(expected_5, f({}, 1, {}, 1));
assertOptimized(f);
