// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbo-optimize-apply --turbofan
// Flags: --no-always-turbofan

// These tests do not work well if we flush the feedback vector, which causes
// deoptimization.
// Flags: --no-stress-flush-code --no-flush-bytecode

// Some of the tests rely on optimizing/deoptimizing at predictable moments, so
// this is not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0

// Test for optimization of CallWithSpread when the array iterator is replaced
// with a generator function after a function is compiled.
//
// Note: this test must be in a separate file because the test invalidates a
// protector, which then remains invalidated.
(function () {
  "use strict";
  var log_got_interpreted = true;
  %NeverOptimizeFunction(assertEquals);

  function log(a) {
    assertEquals(1, arguments.length);
    log_got_interpreted = %IsBeingInterpreted();
    return a;
  }
  function foo() {
    return log(...[1]);
  }

  %PrepareFunctionForOptimization(log);
  %PrepareFunctionForOptimization(foo);
  assertEquals(1, foo());
  assertTrue(log_got_interpreted);

  // Compile foo.
  %OptimizeFunctionOnNextCall(log);
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(1, foo());
  // The call with spread should have been inlined.
  assertFalse(log_got_interpreted);
  assertOptimized(foo);
  %PrepareFunctionForOptimization(foo);

  // This invalidates the DependOnArrayIteratorProtector and causes deopt.
  Object.defineProperty(Array.prototype, Symbol.iterator, {
    value: function* () {
      yield 42;
    },
  });

  // Now we expect the value yielded by the generator.
  assertUnoptimized(foo);
  assertEquals(42, foo());
  assertFalse(log_got_interpreted);

  // Recompile 'foo'.
  %PrepareFunctionForOptimization(foo);
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(42, foo());
  // The call with spread will not be inlined because we have redefined the
  // array iterator.
  assertFalse(log_got_interpreted);
  assertOptimized(foo);
})();
