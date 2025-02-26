// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function generic_key(arr, i, j) {
  arr[i] = 45;
  return arr[j];
}

let arr = new Int32Array(42);

%PrepareFunctionForOptimization(generic_key);
assertEquals(undefined, generic_key(arr, -123456, -45896));
%OptimizeFunctionOnNextCall(generic_key);
assertEquals(undefined, generic_key(arr, -123456, -45896));
assertOptimized(generic_key);
