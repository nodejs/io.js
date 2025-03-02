// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbofan --no-always-turbofan
// Flags: --script-context-mutable-heap-number

let x = 42;
(function() {
  function foo() {
    return x + 1;
  }
  // It should optimize load as constant.
  %PrepareFunctionForOptimization(foo);
  assertEquals(43, foo());
  assertEquals(43, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(43, foo());
  assertOptimized(foo);

  // Deopt.
  x = 4;
  assertUnoptimized(foo);
  // Kill potential opt jobs
  %DeoptimizeFunction(foo);

  // It should optimize as Smi load.
  assertEquals(5, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(5, foo());
  assertOptimized(foo);

  // Deopt.
  x = 4.2;
  assertUnoptimized(foo);
  %DeoptimizeFunction(foo);

  // It should optimize as Double load.
  assertEquals(5.2, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(5.2, foo());
  assertOptimized(foo);

  // Deopt.
  x = null;
  assertUnoptimized(foo);
  %DeoptimizeFunction(foo);

  // It should optimize generically and not add any dependency.
  assertEquals(1, foo());
  %OptimizeFunctionOnNextCall(foo);
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
      %OptimizeFunctionOnNextCall(bar);
      return y;
    };
  }

  // Ensure that the slot transition to MutableHeapNumber.
  y = 2.5;

  %PrepareFunctionForOptimization(foo);
  assertEquals(0, foo()());
  assertEquals(0, foo()());
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(0, foo()());
  assertOptimized(foo);

  // We should not deopt when transitioning to kOther.
  y = undefined;
  assertOptimized(foo);
})();

let z;
(function() {
  // Compile OSR.
  for (let i = 0; i < 3; i++) {
    function store() { z = 42; };
    %PrepareFunctionForOptimization(store);
    store();
    store();
    %OptimizeFunctionOnNextCall(store);
    store();

    z = 52;

    function load() { return z; }
    load();
    %PrepareFunctionForOptimization(load);
    load();
    %OptimizeFunctionOnNextCall(load);
    load();
  }
})();
