// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function array_buffer_load(arr) {
  return arr[1];
}

let float32_arr = new Float32Array(10);
float32_arr[1] = 16.625;
%PrepareFunctionForOptimization(array_buffer_load);
assertEquals(16.625, array_buffer_load(float32_arr));
%OptimizeFunctionOnNextCall(array_buffer_load);
assertEquals(16.625, array_buffer_load(float32_arr));
assertOptimized(array_buffer_load);

// Invalidating ArrayBufferDetachingProtector.
let int32_arr = new Int32Array(10);
%ArrayBufferDetach(int32_arr.buffer);
// This should have triggered a deopt of `array_buffer_load`.
assertUnoptimized(array_buffer_load);

// Reopting without the protector valid.
%OptimizeFunctionOnNextCall(array_buffer_load);
assertEquals(16.625, array_buffer_load(float32_arr));
assertOptimized(array_buffer_load);
