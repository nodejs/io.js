// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function h(x) {
  if (x) { willThrow(); }
  else { return 17; }
}
%NeverOptimizeFunction(h);

function f(a, b) {
  let r = a;
  try {
    r = h(a);
    return h(b) + r;
  } catch {
    return r * b;
  }
}

%PrepareFunctionForOptimization(f);
assertEquals(34, f(0, 0)); // Won't cause an exception
assertEquals(187, f(0, 11)); // Will cause an exception on the 2nd call to h
assertEquals(0, f(7, 0)); // Will cause an exception on the 1st call to h
%OptimizeFunctionOnNextCall(f);
assertEquals(34, f(0, 0));
assertEquals(187, f(0, 11));
assertEquals(0, f(7, 0));
assertOptimized(f);
