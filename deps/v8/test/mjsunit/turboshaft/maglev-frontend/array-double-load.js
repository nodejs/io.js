// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function load_double_arr(arr, idx) {
  return arr[2] + arr[idx];
}
let double_arr = [1.552, 2.425, 3.526, 4.596, 5.986, 6.321];
%PrepareFunctionForOptimization(load_double_arr);
assertEquals(8.122, load_double_arr(double_arr, 3));
assertEquals(8.122, load_double_arr(double_arr, 3));
%OptimizeFunctionOnNextCall(load_double_arr);
assertEquals(8.122, load_double_arr(double_arr, 3));
assertOptimized(load_double_arr);

// String indices currently work without requiring deopt.
assertEquals(5.951, load_double_arr(double_arr, '1'));
assertOptimized(load_double_arr);
