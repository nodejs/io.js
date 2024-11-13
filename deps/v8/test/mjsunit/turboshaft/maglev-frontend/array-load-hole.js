// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function load_hole(arr, idx) {
  return arr[idx];
}
let holey_arr = [ {}, 3.41, /* hole */, 4.55 ];

%PrepareFunctionForOptimization(load_hole);
assertEquals(undefined, load_hole(holey_arr, 2));
%OptimizeFunctionOnNextCall(load_hole);
assertEquals(undefined, load_hole(holey_arr, 2));
assertOptimized(load_hole);
