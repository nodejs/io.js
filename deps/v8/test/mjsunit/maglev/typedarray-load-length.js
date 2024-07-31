// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function foo(arr) {
  // Access an element on the TypedArray, so that we have loaded its underlying
  // length.
  arr[0];

  // Access .length on the TypedArray, which is no longer the default accessor
  // returning the underlying length.
  return arr.length;
}

let x = new Int32Array(10);
Object.defineProperty(x, "length", { value: 20 });

%PrepareFunctionForOptimization(foo);
assertEquals(foo(x), 20);
assertEquals(foo(x), 20);

// Make sure that .length returns the overwritten length property value, and
// doesn't get load eliminated with the underlying length load.
%OptimizeMaglevOnNextCall(foo);
assertEquals(foo(x), 20);
