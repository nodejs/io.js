// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

// Empty array.
(function() {
  function f() {
    return [];
  }

  %PrepareFunctionForOptimization(f);
  f();

  %OptimizeMaglevOnNextCall(f);
  assertEquals(0, f().length);
  assertTrue(isMaglevved(f));
})();

// Calls builtin create shallow object.
(function() {
  function f() {
    return {a: 42, b: 24};
  }

  %PrepareFunctionForOptimization(f);
  f();
  f();

  %OptimizeMaglevOnNextCall(f);
  assertEquals(42, f().a);
  assertTrue(isMaglevved(f));
})();

// Calls runtime create literal object.
(function() {
  function f() {
    return { out: { in: 42 } }
  }

  %PrepareFunctionForOptimization(f);
  f();
  f();

  %OptimizeMaglevOnNextCall(f);
  assertEquals(42, f().out.in);
  assertTrue(isMaglevved(f));
})();
