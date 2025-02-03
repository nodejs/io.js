// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function h(x) {
  if (x) { willThrow(); }
  else { return 17; }
}
%NeverOptimizeFunction(h);

function exc_f(a) {
  try {
    return h(a);
  } catch (e) {
    // Stringifying the exception for easier comparison.
    return 'abc' + e;
  }
}

%PrepareFunctionForOptimization(exc_f);
assertEquals(17, exc_f(0));
let err = exc_f(1); // Will cause an exception.
%OptimizeFunctionOnNextCall(exc_f);
assertEquals(17, exc_f(0));
assertEquals(err, exc_f(1));
assertOptimized(exc_f);
