// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(a, b) {
  try {
    return a + b;
  } catch(_) {
    return b;
  }
}

%PrepareFunctionForOptimization(f);
assertEquals(3n, f(1n, 2n));
%OptimizeFunctionOnNextCall(f);
assertEquals(3n, f(1n, 2n));
assertOptimized(f);

// Triggering exception by mixing int and bigint, which will lazy deopt the
// function, since the catch block had never been used so far.
assertEquals(2n, f(1, 2n));
