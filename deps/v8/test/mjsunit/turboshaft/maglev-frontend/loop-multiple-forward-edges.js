// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function g() { }
%NeverOptimizeFunction(g);

function multi_pred_loop(x) {
  let i = 0;
  if (x == 1) {
    g();
  } else {
    g();
  }
  while (i < 5) {
    i++;
    // Putting a call in the loop so that it cannot be peeled (because it's
    // not "trivial" anymore).
    g();
  }
  return i;
}

%PrepareFunctionForOptimization(multi_pred_loop);
assertEquals(5, multi_pred_loop(1));
assertEquals(5, multi_pred_loop(2));
%OptimizeFunctionOnNextCall(multi_pred_loop);
assertEquals(5, multi_pred_loop(1));
assertEquals(5, multi_pred_loop(2));
assertOptimized(multi_pred_loop);
