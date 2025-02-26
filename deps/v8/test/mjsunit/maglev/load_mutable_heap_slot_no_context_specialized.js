// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --script-context-mutable-heap-number
// Flags: --no-maglev-function-context-specialization

let x = 42;

(function() {
  function foo() {
    return x + 1;
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(43, foo());
  assertEquals(43, foo());
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(43, foo());
  assertOptimized(foo);

  // No deopt.
  x = 4;
  assertEquals(5, foo());
  assertOptimized(foo);

  // No deopt due to transition to MutableHeapNumber cell.
  x = 4.2;
  assertOptimized(foo);

  // It will eargely deopt due to + feedback.
  assertEquals(5.2, foo());
  assertUnoptimized(foo);

  // Optimize again.
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(5.2, foo());
  assertOptimized(foo);

  // It should be allocating a new HeapNumber in every load.
  assertEquals(5.2, foo());

  // It shouldn't deopt.
  x = {};
  assertOptimized(foo);
})();
