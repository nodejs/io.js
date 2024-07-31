// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --deopt-every-n-times=7

function foo(a, b) {
  a.forEach(name => {
    b.push(name);
  });
}
let arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 0];

%PrepareFunctionForOptimization(foo);
let arr2 = [];
foo(arr, arr2);
assertEquals(10, arr2.length);
arr2 = [];
foo(arr, arr2);
assertEquals(10, arr2.length);
%OptimizeMaglevOnNextCall(foo);
arr2 = [];
foo(arr, arr2);
assertEquals(10, arr2.length);
