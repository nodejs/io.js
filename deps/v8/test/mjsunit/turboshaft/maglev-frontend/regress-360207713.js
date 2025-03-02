// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function g(should_throw) {
  if (should_throw) {
    throw new Error();
  }
}
%NeverOptimizeFunction(g);

function foo(v, throw1, throw2) {
  try {
    v = v + 5;
    g(throw1);
    v = v >>> 0;
    g(throw2);
    v = v + 5;
  } catch(e) {
    return v;
  }
  return v;
}

%PrepareFunctionForOptimization(foo);
assertEquals(-5, foo(-10, true, false));
assertEquals(0xfffffffb, foo(-10, false, true));
assertEquals(0x100000000, foo(-10, false, false));

%OptimizeFunctionOnNextCall(foo);
assertEquals(-5, foo(-10, true, false));
assertEquals(0xfffffffb, foo(-10, false, true));
assertEquals(0x100000000, foo(-10, false, false));
