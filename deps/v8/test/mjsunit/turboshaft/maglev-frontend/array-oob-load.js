// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

let a = [1, 2, 3, 4];
let b = [5, 6, 7, 8];

function arr_oob_load(a, b) {
  let s = [0, 0, 0, 0, 0, 0];
  for (let i = 0; i < 6; i++) {
    // This will load out of bounds in {a} and {b}, which is fine.
    s[i] = a[i] + b[i];
  }
  return s;
}

%PrepareFunctionForOptimization(arr_oob_load);
assertEquals([6, 8, 10, 12, NaN, NaN], arr_oob_load(a, b));
assertEquals([6, 8, 10, 12, NaN, NaN], arr_oob_load(a, b));
%OptimizeFunctionOnNextCall(arr_oob_load);
assertEquals([6, 8, 10, 12, NaN, NaN], arr_oob_load(a, b));
assertOptimized(arr_oob_load);
