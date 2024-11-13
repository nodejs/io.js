// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function branch_on(a) {
  let v = a * 1.35;
  if (v) {
    return 17;
  } else {
    return 42;
  }
}

%PrepareFunctionForOptimization(branch_on);
assertEquals(17, branch_on(1.5));
assertEquals(42, branch_on(0));
assertEquals(42, branch_on(NaN));
%OptimizeFunctionOnNextCall(branch_on);
assertEquals(17, branch_on(1.5));
assertEquals(42, branch_on(0));
assertEquals(42, branch_on(NaN));
assertOptimized(branch_on);
