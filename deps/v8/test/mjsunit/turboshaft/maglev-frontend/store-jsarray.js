// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function store_arr(obj_arr, double_arr) {
  obj_arr[0] = 42; // FixedArray, no write barrier
  obj_arr[1] = double_arr; // FixedArray, with write barrier
  double_arr[1] = 42.25; // DoubleFixedArray
}

let obj_arr = [0, {}, 2];
let double_arr = [1.56, 2.68, 3.51];

%PrepareFunctionForOptimization(store_arr);
store_arr(obj_arr, double_arr);
assertEquals([42, double_arr, 2], obj_arr);
assertEquals([1.56, 42.25, 3.51], double_arr);

// Resetting {obj_arr} and {double_arr}
obj_arr[0] = 0;
obj_arr[1] = {};
double_arr[1] = 2.68;

%OptimizeFunctionOnNextCall(store_arr);
store_arr(obj_arr, double_arr);
assertEquals([42, double_arr, 2], obj_arr);
assertEquals([1.56, 42.25, 3.51], double_arr);
assertOptimized(store_arr);
