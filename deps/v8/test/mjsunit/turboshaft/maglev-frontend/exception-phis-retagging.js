// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function g(b) {
  if (b) { will_throw(); }
}
%NeverOptimizeFunction(g);

function f(a, b1, b2, b3) {
  let s = a;
  try {
    g(b1);
    s += 3;
    for (let i = 0; i < 9; i++) {
      g(b2);
      s += 10;
    }
    g(b3);
  } catch(e) {
    return s + 136;
  }
  return s + 585;
}

%PrepareFunctionForOptimization(f);
assertEquals(141, f(5, true, false, false));
assertEquals(144, f(5, false, true, false,));
assertEquals(234, f(5, false, false, true));
assertEquals(683, f(5, false, false, false));
%OptimizeFunctionOnNextCall(f);
assertEquals(141, f(5, true, false, false));
assertEquals(144, f(5, false, true, false,));
assertEquals(234, f(5, false, false, true));
assertEquals(683, f(5, false, false, false));
assertOptimized(f);
