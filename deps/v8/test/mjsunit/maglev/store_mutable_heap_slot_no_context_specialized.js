// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --script-context-mutable-heap-number
// Flags: --no-maglev-function-context-specialization

let x = 42;

(function() {
  function foo(a) {
    x = a;
    return x;
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(42, foo(42));
  assertEquals(42, foo(42));
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(42, foo(42));
  assertOptimized(foo);

  // No deopt.
  x = 4;
  foo(5);
  assertEquals(5, foo(5));
  assertOptimized(foo);

  // No deopt due to transition to MutableHeapNumber cell.
  x = 4.2;
  assertOptimized(foo);
  assertEquals(5.2, foo(5.2));

  // It shouldn't deopt.
  x = {};
  assertOptimized(foo);
})();
