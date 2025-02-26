// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function fact(n) {
  let s = 1;
  while (n > 1) {
    s *= n;
    n--;
  }
  return s;
}

%PrepareFunctionForOptimization(fact);
let n1 = fact(42);
%OptimizeFunctionOnNextCall(fact);
assertEquals(n1, fact(42));
assertOptimized(fact);
