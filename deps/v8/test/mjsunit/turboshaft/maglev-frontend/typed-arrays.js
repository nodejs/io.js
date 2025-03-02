// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function typed_arr() {
  const u16_arr = new Uint16Array(10);
  u16_arr[5] = 18;
  const i8_arr = new Int8Array(13);
  i8_arr[2] = 47;
  const f32_arr = new Float32Array(15);
  f32_arr[10] = 3.362;
  let u16 = u16_arr[1];
  let i8 = i8_arr[4];
  let f32 = f32_arr[9];
  return [u16_arr, i8_arr, f32_arr, u16, i8, f32];
}

%PrepareFunctionForOptimization(typed_arr);
let a1 = typed_arr();
%OptimizeFunctionOnNextCall(typed_arr);
let a2 = typed_arr();
assertEquals(a1, a2);
assertOptimized(typed_arr);
