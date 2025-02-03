// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

const arr = new Int32Array(10);
arr[4] = 42;

function load_arr_double_index(x) {
  let index = x + 1.5;
  return arr[index];
}

%PrepareFunctionForOptimization(load_arr_double_index);
assertEquals(42, load_arr_double_index(2.5));
%OptimizeFunctionOnNextCall(load_arr_double_index);
assertEquals(42, load_arr_double_index(2.5));
assertOptimized(load_arr_double_index);

// Should deopt for negative indices.
assertEquals(undefined, load_arr_double_index(-2.5));
assertUnoptimized(load_arr_double_index);
