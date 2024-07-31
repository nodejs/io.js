// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev


const a = [1, 2, 3]

function foo(i) {
  return a[i];
}

// Setting a negative index does not violate the NoElements protector.
Array.prototype[-1] = "Uh!";

%PrepareFunctionForOptimization(foo);
foo(0);
foo(5);

%OptimizeMaglevOnNextCall(foo);
foo(0);
foo(7); // It is able to read out of bounds.
assertTrue(isOptimized(foo));

foo(-1); // Deopt, since index is negative.
assertTrue(!isOptimized(foo));
