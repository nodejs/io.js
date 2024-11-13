// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function load_holey_fixed_double(arr, idx) {
  return arr[idx];
}
let holey_double_arr = [2.58,3.41,,4.55];

%PrepareFunctionForOptimization(load_holey_fixed_double);
assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
%OptimizeFunctionOnNextCall(load_holey_fixed_double);
assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
assertOptimized(load_holey_fixed_double);
// Loading a hole should trigger a deopt
assertEquals(undefined, load_holey_fixed_double(holey_double_arr, 2));
assertUnoptimized(load_holey_fixed_double);

// Reoptimizing, holes should now be handled
%OptimizeMaglevOnNextCall(load_holey_fixed_double);
assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
%OptimizeFunctionOnNextCall(load_holey_fixed_double);
assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
assertEquals(undefined, load_holey_fixed_double(holey_double_arr, 2));
assertOptimized(load_holey_fixed_double);
