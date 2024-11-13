// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan --no-maglev-loop-peeling

// Creating an object with 2 Smi fields.
let o = { x : 42, y : 19 };

// Making {o}'s fields non-const.
o.x = 43;
o.y = 20;

let holey_double_arr = [ -0.0, 2.2, , 4.4 ];

function foo(x, y) {
  let holey_f64_1 = holey_double_arr[0];
  let phi_1 = holey_f64_1; // HoleyFloat64 input to the phi
  for (let i = 0.5; i < x; i += 1) {
    i += phi_1; // Float64 use to the phi
    phi_1 = 51; // Smi input to the phi
  }
  o.x = phi_1;

  let holey_f64_2 = holey_double_arr[1];
  let phi_2 = holey_f64_2; // HoleyFloat64 input to the phi
  for (let i = 0.5; i < y; i += 1) {
    i += phi_2; // Float64 use to the phi
    phi_2 = 40; // Smi input to the phi
  }
  o.y = phi_2;
}

%PrepareFunctionForOptimization(foo);
foo(4, 5);
assertEquals({x : 51, y : 40}, o);

%OptimizeFunctionOnNextCall(foo);
foo(4, 5);
assertEquals({x : 51, y : 40}, o);
assertOptimized(foo);

// Triggering a deopt because of -0.0
foo(0, 5);
assertEquals({x : -0.0, y : 40}, o);
assertSame(o.x, -0.0);
assertUnoptimized(foo);

%OptimizeFunctionOnNextCall(foo);
foo(4, 5);
assertEquals({x : 51, y : 40}, o);
assertOptimized(foo);

// Triggering deopt because of regular non-smi float64
foo(4, 0);
assertEquals({x : 51, y : 2.2}, o);
assertUnoptimized(foo);
