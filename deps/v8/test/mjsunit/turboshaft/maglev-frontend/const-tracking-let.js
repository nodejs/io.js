// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan --const-tracking-let

let glob_a = 0;
let glob_b = 3.35;
{
  function compute_val(v) { return v + 1.15; }
  function read() { return glob_a + glob_b; }
  function write(v, w) {
    glob_a = v;
    let f64 = compute_val(w);
    glob_b = f64;
  }

  %PrepareFunctionForOptimization(compute_val);

  %PrepareFunctionForOptimization(read);
  assertEquals(3.35, read());
  %OptimizeFunctionOnNextCall(read);
  assertEquals(3.35, read());
  assertOptimized(read);

  %PrepareFunctionForOptimization(write);
  // Write the same value. This won't invalidate the constness.
  write(0, 2.2);
  glob_b = 3.35;
  assertEquals(3.35, read());

  %OptimizeFunctionOnNextCall(write);
  write(0, 2.2);
  assertEquals(3.35, read());
  assertOptimized(read);

  // Invalidating {glob_a} constness.
  write(1, 2.2);
  assertUnoptimized(write);
  assertEquals(4.35, read());

  %OptimizeFunctionOnNextCall(write);
  write(1, 2.2);
  assertEquals(4.35, read());
  assertOptimized(write);
}
