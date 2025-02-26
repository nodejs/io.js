// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

let x = 42;

function foo(a) {
  x = a;
}

%PrepareFunctionForOptimization(foo);
foo(42);

%OptimizeMaglevOnNextCall(foo);
x = 4.2;
foo(5.2);

assertEquals(5.2, x);
