// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --script-context-mutable-heap-number
// Flags: --maglev-function-context-specialization

let x = 42;
x = 4; // making x non-const.

(function() {
  function g() {}
  %NeverOptimizeFunction(g);

  function foo(a) {
    x = a;
    g(); // Opaque function call to ensure that {x} is reloaded in the line
         // below.
    return x + 2;
  }


  // Should optimize as Smi store.
  %PrepareFunctionForOptimization(foo);
  assertEquals(7, foo(5));
  assertEquals(7, foo(5));

  %OptimizeMaglevOnNextCall(foo);
  assertEquals(7, foo(5));
  assertOptimized(foo);

  // Should deopt/
  assertEquals(6.2, foo(4.2));
  assertUnoptimized(foo);

})();

let y = 42;
y = 4; // making x non-const.

(function() {
  function g() {}
  %NeverOptimizeFunction(g);

  function foo(a) {
    y = a;
    g(); // Opaque function call to ensure that {x} is reloaded in the line
         // below.
    return y + 2;
  }

  // Should optimize as MutableHeapStore store.
  %PrepareFunctionForOptimization(foo);
  assertEquals(7.2, foo(5.2));
  assertEquals(7.2, foo(5.2));

  %OptimizeMaglevOnNextCall(foo);
  assertEquals(7.2, foo(5.2));
  assertOptimized(foo);

  // Shouldn't deopt. Since we emit
  // CheckedNumberOrOddballToFloat64 before the StoreDoubleField call.
  assertEquals(44, foo(42));
  assertOptimized(foo); // Still optimized.

})();
