// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

// Testing exceptions (multiple throwing points, using the exception value).
function h(x) {
  if (x) { willThrow(); }
  else { return 17; }
}
%NeverOptimizeFunction(h);

function multi_exc_f(a, b) {
  let r = a;
  try {
    r = h(a);
    return h(b) + r;
  } catch (e) {
    // Stringifying the exception for easier comparison.
    return 'abc' + e + r;
  }
}

%PrepareFunctionForOptimization(multi_exc_f);
assertEquals(34, multi_exc_f(0, 0)); // Won't cause an exception.
let err1 = multi_exc_f(0, 11); // Will cause an exception on the 2nd call to h.
let err2 = multi_exc_f(7, 0); // Will cause an exception on the 1st call to h.
%OptimizeFunctionOnNextCall(multi_exc_f);
assertEquals(34, multi_exc_f(0, 0));
assertEquals(err1, multi_exc_f(0, 11));
assertEquals(err2, multi_exc_f(7, 0));
assertOptimized(multi_exc_f);
