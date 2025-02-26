// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

const arr = new Uint32Array(2);
function uint_to_i32_f64() {
  let v1 = arr[0] + 7; // requires Uint32->Int32 conversion
  let v2 = arr[1] + 3.47; // requires Uint32->Float64 conversion
  return v1 + v2;
}

%PrepareFunctionForOptimization(uint_to_i32_f64);
assertEquals(10.47, uint_to_i32_f64());
%OptimizeFunctionOnNextCall(uint_to_i32_f64),
assertEquals(10.47, uint_to_i32_f64());
assertOptimized(uint_to_i32_f64);

arr[0] = 0xffffff35; // Does not fit in a signed Int32
assertEquals(0xffffff35+7+3.47, uint_to_i32_f64());
assertUnoptimized(uint_to_i32_f64);
