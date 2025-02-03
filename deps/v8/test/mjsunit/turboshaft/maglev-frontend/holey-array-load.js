// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function ret_from_holey_arr(i) {
  let arr = new Array(10);
  arr[0] = 42;
  return arr[i];
}

%PrepareFunctionForOptimization(ret_from_holey_arr);
assertEquals(42, ret_from_holey_arr(0));
%OptimizeFunctionOnNextCall(ret_from_holey_arr);
assertEquals(42, ret_from_holey_arr(0));
assertOptimized(ret_from_holey_arr);

// Triggering deopt by trying to return a hole.
assertEquals(undefined, ret_from_holey_arr(1));
assertUnoptimized(ret_from_holey_arr);

// Reopting
%OptimizeFunctionOnNextCall(ret_from_holey_arr);
assertEquals(undefined, ret_from_holey_arr(1));
// This time the hole is converted to undefined without deopting.
assertOptimized(ret_from_holey_arr);
