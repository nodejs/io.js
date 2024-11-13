// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(a, b, c) {
  return a + b + c;
}

let short_arr = [11, 27];
function f_spread_plus_args(short_arr, x) {
  return f(...short_arr, x);
}

%PrepareFunctionForOptimization(f_spread_plus_args);
assertEquals(41, f_spread_plus_args(short_arr, 3));
%OptimizeFunctionOnNextCall(f_spread_plus_args);
assertEquals(41, f_spread_plus_args(short_arr, 3));
assertOptimized(f_spread_plus_args);
