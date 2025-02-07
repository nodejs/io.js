// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// This function is designed so that the inner loop needs revisiting, and the
// outer loop also (and the revisit of the outer loop should trigger a revisit
// of the inner loop). The block triggering the revisit of the outer loop is not
// among the successors of the inner loop.

function f() {
  let arr = [1, 2, 3];
  let s1 = 0, s2 = 0;
  for (let i = 0; i < 10; i++) {
    if (i >= 3) {
      // The inner loop is in an if-else in the outer loop so that the block
      // containing `arr[2]++` is not among its successors.
      for (let j = 0; j < 10; j++) {
        s1 += arr[1];
        s2 += arr[2];
        arr[1]++; // Trigger revisit of inner loop
      }
    } else {
      arr[2]++; // Trigger revisit of outer loop
    }
  }
  return s1 + s2;
}

%PrepareFunctionForOptimization(f);
assertEquals(f(), 2975);
%OptimizeFunctionOnNextCall(f);
assertEquals(f(), 2975);
