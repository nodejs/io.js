// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function load_smi_arr(arr, idx) {
  return arr[1] + arr[idx];
}

let smi_arr = [1, 2, 3, 4, {}];
%PrepareFunctionForOptimization(load_smi_arr);
assertEquals(6, load_smi_arr(smi_arr, 3));
assertEquals(6, load_smi_arr(smi_arr, 3));
%OptimizeFunctionOnNextCall(load_smi_arr);
assertEquals(6, load_smi_arr(smi_arr, 3));
assertOptimized(load_smi_arr);

// String indices currently work without requiring deopt.
assertEquals(5, load_smi_arr(smi_arr, '2'));
assertOptimized(load_smi_arr);
