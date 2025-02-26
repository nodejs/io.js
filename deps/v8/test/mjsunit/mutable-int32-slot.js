// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev
// Flags: --maglev-function-context-specialization
// Flags: --script-context-mutable-heap-int32

// Reading slot and deopting via unoptimized code.
let x = 0;
(function() {
  function read() {
    return x;
  }

  // Should transition to Int32.
  x = 0x4fffffff;
  %PrepareFunctionForOptimization(read);
  assertEquals(0x4fffffff, read());
  %OptimizeFunctionOnNextCall(read);
  assertEquals(0x4fffffff, read());

  // Try a different Int32.
  x = 0x4ffefefe;
  assertEquals(0x4ffefefe, read());

  // Try a Smi.
  x = 123;
  assertEquals(123, read());

  // We haven't deopt.
  assertTrue(isOptimized(read));

  // Try a normal double.
  // We should deopt.
  x = 12.3;
  assertFalse(isOptimized(read));
  assertEquals(12.3, read());

  // Optimize again.
  x = 0x4fffffff;
  %OptimizeFunctionOnNextCall(read);
  assertEquals(0x4fffffff, read());

  // We shouldn't deopt anymore.
  x = 12.3;
  assertEquals(12.3, read());
  assertTrue(isOptimized(read));
})();


// Reading and writing slot and deopting via optimized code.
let y = 0;
(function() {
  function read() {
    return y;
  }

  function write(a) {
    y = a;
  }

  // Should transition to Int32.
  %PrepareFunctionForOptimization(read);
  %PrepareFunctionForOptimization(write);
  write(0x4fffffff);
  assertEquals(0x4fffffff, read());
  %OptimizeFunctionOnNextCall(read);
  %OptimizeFunctionOnNextCall(write);
  assertEquals(0x4fffffff, read());

  // Try a different Int32.
  write(0x4ffefefe);
  assertEquals(0x4ffefefe, read());

  // Try a Smi.
  write(123);
  assertEquals(123, read());

  // We haven't deopt.
  assertTrue(isOptimized(read));
  assertTrue(isOptimized(write));

  // Try a normal double.
  // We should deopt.
  write(12.3);
  assertFalse(isOptimized(read));
  assertEquals(12.3, read());
  assertFalse(isOptimized(write));

  // Optimize again.
  %OptimizeFunctionOnNextCall(read);
  %OptimizeFunctionOnNextCall(write);
  write(0x4fffffff);
  assertTrue(isOptimized(write));
  assertEquals(0x4fffffff, read());

  // We shouldn't deopt anymore.
  y = 12.3;
  assertEquals(12.3, read());
  assertTrue(isOptimized(read));
  assertTrue(isOptimized(write));
})();

// Checking -0.0.
let z = 0;
(function() {
  function read() {
    return z;
  }

  // Should transition to Int32.
  z = 0x4fffffff;
  %PrepareFunctionForOptimization(read);
  assertEquals(0x4fffffff, read());
  %OptimizeFunctionOnNextCall(read);
  assertEquals(0x4fffffff, read());

  // Should deopt in -0.0.
  z = -0.0;
  assertFalse(isOptimized(read));
  assertEquals(-0.0, read());

  // Optimize again.
  z = 0x4fffffff;
  %OptimizeFunctionOnNextCall(read);
  assertEquals(0x4fffffff, read());

  // We shouldn't deopt anymore.
  z = -0.0;
  assertTrue(isOptimized(read));
  assertEquals(-0.0, read());
  assertTrue(isOptimized(read));
})();
