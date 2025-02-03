// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function branch_cmp(w32, f64) {
  let c1 = w32 + 2;
  let v;
  if (c1) {
    v = c1 + 12;
  } else {
    v = c1 + 17;
  }
  let c2 = f64 + 1.5;
  if (c2) {
    v += 10;
  } else {
    v += 3;
  }
  if (f64 > 1.5) {
    v += 2;
  } else {
    v += 7;
  }
  return v;
}

%PrepareFunctionForOptimization(branch_cmp);
assertEquals(41, branch_cmp(15, 3.25));
assertEquals(29, branch_cmp(-2, 3.25));
assertEquals(27, branch_cmp(-2, -1.5));
%OptimizeFunctionOnNextCall(branch_cmp);
assertEquals(41, branch_cmp(15, 3.25));
assertEquals(29, branch_cmp(-2, 3.25));
assertEquals(27, branch_cmp(-2, -1.5));
assertOptimized(branch_cmp);
