// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

var arr = [1, 2, 3];

function test_for_in(a, b) {
  let s = a + b;
  for (x in arr) {}
  return s;
}

%PrepareFunctionForOptimization(test_for_in);
assertEquals(3, test_for_in(1, 2));
%OptimizeFunctionOnNextCall(test_for_in);
assertEquals(3, test_for_in(1, 2));
assertOptimized(test_for_in);
