// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

var u = new Uint32Array(2);
u[0] = 1;
u[1] = 0xEE6B2800;

// Creating an object with a Smi field `x`.
let o = { x : 42 };

function foo(i) {
  // Storing a Uint32 to a Smi field.
  o.x = u[i];
};

%PrepareFunctionForOptimization(foo);
foo(0);
assertEquals(u[0], o.x);

%OptimizeFunctionOnNextCall(foo);
foo(0);
assertEquals(u[0], o.x);
assertOptimized(foo);

// Triggering deopt by trying to store non-smi
foo(1);
assertEquals(u[1], o.x);
assertUnoptimized(foo);
