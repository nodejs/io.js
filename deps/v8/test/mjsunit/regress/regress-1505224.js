// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(arr) {
  arr.forEach(() => {});
  return arr.length;
}

let arr = [,1];
%PrepareFunctionForOptimization(foo);
assertEquals(2, foo(arr));
%OptimizeFunctionOnNextCall(foo);
assertEquals(2, foo(arr));
