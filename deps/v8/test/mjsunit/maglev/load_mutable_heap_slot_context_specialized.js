// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --script-context-mutable-heap-number
// Flags: --maglev-function-context-specialization

let x = 42;
(function() {
  function foo() {
    return x + 1;
  }
  // It should optimize load as constant.
  %PrepareFunctionForOptimization(foo);
  assertEquals(43, foo());
  assertEquals(43, foo());
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(43, foo());
  assertOptimized(foo);

  // Deopt.
  x = 4;
  assertUnoptimized(foo);
  // Kill potential opt jobs
  %DeoptimizeFunction(foo);

  // It should optimize as Smi load.
  assertEquals(5, foo());
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(5, foo());
  assertOptimized(foo);

  // Deopt.
  x = 4.2;
  assertUnoptimized(foo);
  %DeoptimizeFunction(foo);

  // It should optimize as Double load.
  assertEquals(5.2, foo());
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(5.2, foo());
  assertOptimized(foo);

  // Deopt.
  x = null;
  assertUnoptimized(foo);
  %DeoptimizeFunction(foo);

  // It should optimize generically and not add any dependency.
  assertEquals(1, foo());
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(1, foo());
  assertOptimized(foo);

  // It shouldn't deopt.
  x = {};
  assertOptimized(foo);
})();


// Dynamic look up with eval.
let y = 0.5;
(function() {
  function foo() {
    eval("var y = 0");
    return function bar() {
      %PrepareFunctionForOptimization(bar);
      %OptimizeMaglevOnNextCall(bar);
      return y;
    };
  }

  // Ensure that the slot transition to MutableHeapNumber.
  y = 2.5;

  %PrepareFunctionForOptimization(foo);
  assertEquals(0, foo()());
  assertEquals(0, foo()());
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(0, foo()());
  assertOptimized(foo);

  // We should not deopt when transitioning to kOther.
  y = undefined;
  assertOptimized(foo);
})();
