// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

let uint32_arr = new Uint32Array(5);
uint32_arr[1] = 168;
uint32_arr[2] = 5896;
let uint8_clamped_arr = new Uint8ClampedArray(10);

function store_clamped_array(x, small_num, large_num, nan) {
  // Storing Float64 (< 255)
  let f64_small = x + 3.45;
  uint8_clamped_arr[0] = f64_small;
  // Storing Float64 (> 255);
  let f64_large = x + 18956.586;
  uint8_clamped_arr[1] = f64_large;

  // Storing Int32 (< 255)
  let i32_small = x + 17;
  uint8_clamped_arr[2] = i32_small;
  // Storing Int32 (> 255)
  let i32_large = x + 25896;
  uint8_clamped_arr[3] = i32_large;

  // Storing uint32 (< 255)
  let uint32_small = uint32_arr[1];
  uint8_clamped_arr[4] = uint32_small;
  let uint32_large = uint32_arr[2];
  uint8_clamped_arr[5] = uint32_large;

  // Storing number (small)
  uint8_clamped_arr[6] = small_num;
  // Storing number (large)
  uint8_clamped_arr[7] = large_num;
  // Storing number (NaN)
  uint8_clamped_arr[8] = nan;
}

let expected = new Uint8ClampedArray([7,255,21,255,168,255,6,255,0,0]);

%PrepareFunctionForOptimization(store_clamped_array);
store_clamped_array(4, 6, 1896.365, NaN);
assertEquals(expected, uint8_clamped_arr);

// Reseting the array
uint8_clamped_arr = new Uint8ClampedArray(10);

%OptimizeFunctionOnNextCall(store_clamped_array);
store_clamped_array(4, 6, 1896.365, NaN);
assertEquals(expected, uint8_clamped_arr);
assertOptimized(store_clamped_array);

// Triggering deopt when trying to store a non-number
store_clamped_array(4, "abc", 1896.365);
expected[6] = 0;
assertEquals(expected, uint8_clamped_arr);
// Note: we don't AssertUnoptimized here because in some configurations where
// some clamped Uint8 operations are not supported, the stores of
// `store_clamped_array` are compiled to SetKeyedGeneric by Maglev, and thus
// do not deopt for non-numbers. If the result is correct, then deopt or not,
// the correct thing probably happened.
