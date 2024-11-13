// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(x, y, z) {
  return x + y + z;
}
%NeverOptimizeFunction(f);
let arr = [17, 13, 5, 23];

function f_spread(arr) {
  return f(...arr);
}

%PrepareFunctionForOptimization(f_spread);
assertEquals(35, f_spread(arr));
%OptimizeFunctionOnNextCall(f_spread);
assertEquals(35, f_spread(arr));
assertOptimized(f_spread);

let small_arr = [3, 5];
assertEquals(NaN, f_spread(small_arr));
assertOptimized(f_spread);
