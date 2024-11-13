// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(x, y, z) {
  return x + y + z;
}
%NeverOptimizeFunction(f);
let arr = [17, 13, 5, 23];

function f_apply(arr) {
  return f.apply(null, arr);
}

%PrepareFunctionForOptimization(f_apply);
assertEquals(35, f_apply(arr));
%OptimizeFunctionOnNextCall(f_apply);
assertEquals(35, f_apply(arr));
assertOptimized(f_apply);
