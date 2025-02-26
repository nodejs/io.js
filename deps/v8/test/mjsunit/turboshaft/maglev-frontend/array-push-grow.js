// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function array_push_grow_once(arr, x, y) {
  // The 1st push will have to grow the array.
  arr.push(x);
  arr.push(y);
  return arr.at(-1) + arr.at(-2) + arr.at(-3);
}

arr = [0, 1, 2, 3, 4, 5];

%PrepareFunctionForOptimization(array_push_grow_once);
assertEquals(29, array_push_grow_once(arr, 11, 13));

arr = [0, 1, 2, 3, 4, 5];
%OptimizeFunctionOnNextCall(array_push_grow_once);
assertEquals(29, array_push_grow_once(arr, 11, 13));
assertOptimized(array_push_grow_once);
