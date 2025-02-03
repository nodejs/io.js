// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

let holey_double_arr = [1.1, , 3.3];

let u;
function load_for_return(i) {
  u = undefined;
  return holey_double_arr[i] === u;
}

%PrepareFunctionForOptimization(load_for_return);
assertEquals(false, load_for_return(0));
assertEquals(true, load_for_return(1));
%OptimizeFunctionOnNextCall(load_for_return);
assertEquals(false, load_for_return(0));
assertEquals(true, load_for_return(1));
assertOptimized(load_for_return);
