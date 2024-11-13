// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

let a = [1, 2, 3, 4];
let b = [5, 6, 7, 8];

function make_fast_arr(a, b) {
  let s = [0, 0, 0, 0];
  for (let i = 0; i < 4; i++) {
    s[i] = a[i] + b[i];
  }
  return s;
}

%PrepareFunctionForOptimization(make_fast_arr);
assertEquals([6, 8, 10, 12], make_fast_arr(a, b));
%OptimizeFunctionOnNextCall(make_fast_arr);
assertEquals([6, 8, 10, 12], make_fast_arr(a, b));
assertOptimized(make_fast_arr);
