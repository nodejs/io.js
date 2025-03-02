// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function array_push_pop(arr, x) {
  let v = arr.pop();
  arr.push(x);  // This doesn't require growing the array.
  return v + arr[5];
}

let arr = [0, 1, 2, 3, 4, 5];

%PrepareFunctionForOptimization(array_push_pop);
assertEquals(16, array_push_pop(arr, 11));

arr[5] = 5;
%OptimizeFunctionOnNextCall(array_push_pop);
assertEquals(16, array_push_pop(arr, 11));
assertOptimized(array_push_pop);
