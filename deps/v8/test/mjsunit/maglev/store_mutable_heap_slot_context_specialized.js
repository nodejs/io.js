// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --script-context-mutable-heap-number
// Flags: --maglev-function-context-specialization

let x = 42;

(function() {
  function foo(a) {
    x = a;
    return x;
  }
  // It should optimize load as constant.
  %PrepareFunctionForOptimization(foo);
  assertEquals(42, foo(42));
  assertEquals(42, foo(42));
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(42, foo(42));
  assertOptimized(foo);

  // Deopt.
  x = 4;
  assertUnoptimized(foo);
  // Kill potential opt jobs
  %DeoptimizeFunction(foo);

  // It should optimize as Smi store.
  assertEquals(5, foo(5));
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(5, foo(5));
  assertOptimized(foo);

  // Deopt.
  x = 4.2;
  assertUnoptimized(foo);
  %DeoptimizeFunction(foo);

  // It should optimize as Double store.
  assertEquals(5.2, foo(5.2));
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(5.2, foo(5.2));
  assertOptimized(foo);

  // Deopt.
  x = null;
  assertUnoptimized(foo);
  %DeoptimizeFunction(foo);

  // It should optimize generically and not add any dependency.
  assertEquals(1, foo(1));
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(1, foo(1));
  assertOptimized(foo);

  // It shouldn't deopt.
  x = {};
  assertOptimized(foo);
})();

let y = undefined;
(function() {
  // Set slot property to kMutableHeapNumber
  y = 4.2;
  function foo() {
    return y;
  }
  // We should allocate a new heap number,
  // instead of leaking it.
  %PrepareFunctionForOptimization(foo);
  assertEquals(4.2, foo());
  assertEquals(4.2, foo());
  %OptimizeMaglevOnNextCall(foo);
  assertEquals(4.2, foo());
  assertOptimized(foo);

  let z = foo();

  // If we mutate y, we shouldn't mutate `z`
  y = 5.2;
  assertEquals(4.2, z);
  assertEquals(5.2, y);

  // The same thing should happen if we mutate
  // via on optimized code.
  function bar() {
    return y = 6.2;
  }
  %PrepareFunctionForOptimization(bar);
  bar();
  bar();
  %OptimizeMaglevOnNextCall(bar);
  bar();
  assertOptimized(bar);

  y = 5.2;
  z = foo();
  bar();
  assertEquals(5.2, z);
  assertEquals(6.2, y);

  y = 4.2;
  bar();
  z = y;
  bar();
  y = 5.2;
  assertEquals(6.2, z);
  assertEquals(5.2, y);

  // And foo was never deoptimized.
  assertOptimized(foo);
})();
