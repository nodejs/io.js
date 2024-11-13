// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function g(b) {
  if (b) {
    throw_before_this_function_is_not_defined();
  }
}
%NeverOptimizeFunction(g);

function foo(n, r, b) {
  let v = n >>> r; // {v} here has Uint32 representation.
  try {
    g(b);
    v = 45;
    g(true);
  } catch(e) {
    // {v} here is a Phi with a Uint32 and a Int32 input.
    return v + 3;
  }
}

%PrepareFunctionForOptimization(foo);
assertEquals(48, foo(23, 3, false));
assertEquals(5, foo(23, 3, true));
assertEquals(48, foo(-1478558121, 0, false));
assertEquals(2816409178, foo(-1478558121, 0, true));

%OptimizeFunctionOnNextCall(foo);
assertEquals(48, foo(23, 3, false));
assertEquals(5, foo(23, 3, true));
// Making sure that negative numbers are turned into positive ones by `>>> 0`.
assertEquals(48, foo(-1478558121, 0, false));
assertEquals(2816409178, foo(-1478558121, 0, true));
