// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function transition_arr(arr) {
  var object = new Object();
  arr[0] = object;
}

let smi_arr = [1, 2, 3, 4];
let holey_smi_arr = [1, 2, 3, /* hole */, 4];

%PrepareFunctionForOptimization(transition_arr);
transition_arr(smi_arr);
transition_arr(holey_smi_arr);
// Resetting the arrays to collect feedback one more time.
smi_arr = [1, 2, 3, 4];
holey_smi_arr = [1, 2, 3, /* hole */, 4];
transition_arr(smi_arr);
transition_arr(holey_smi_arr);

let expected_smi_arr = smi_arr;
let expected_holey_smi_arr = holey_smi_arr;

%OptimizeFunctionOnNextCall(transition_arr);
// Resetting the arrays
smi_arr = [1, 2, 3, 4];
holey_smi_arr = [1, 2, 3, /* hole */, 4];
// Triggering transitions
transition_arr(smi_arr);
assertEquals(expected_smi_arr, smi_arr);
transition_arr(holey_smi_arr);
assertEquals(expected_holey_smi_arr, holey_smi_arr)
assertOptimized(transition_arr);
// Not trigering transitions (because arrays already transitioned)
transition_arr(smi_arr);
transition_arr(holey_smi_arr);
assertOptimized(transition_arr);
// Triggering deopt
let double_arr = [1.5, 3.32, 6.28];
transition_arr(double_arr);
assertEquals([{}, 3.32, 6.28], double_arr);
