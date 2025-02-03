// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function foo(arr, x) {
  let sum = 0;
  let v = 4.65; // {v} will be a HoleyFloat64 Phi
  for (let i = 0; i < 10; i++) {
    sum += v; // This is a Float64 use of {v} to ensures that it gets untagged.
    v = arr[i]; // HoleyFloat64 input to {v}.
  }
  // {v} is now a Float64 hole (because arr[9] is a hole).

  // This addition will have Smi feedback, and we'll make it deopt by overflow
  // Smi range. {v} will be passed in the FrameState. If it's marked as Float64
  // MachineRepresentation in the FrameState, then the deoptimizer will
  // materialize a NaN HeapNumber for it, which is wrong. It should instead have
  // HoleyFloat64 MachineRepresentation, and be materialized by the deoptimizer
  // as Undefined.
  let xtimes10 = x * 10;

  // We mainly care about returning {v} here, but we're also returning the other
  // things so that they aren't dead and optimized out.
  return [ sum, v, xtimes10 ];
}

let arr = [0.0, 1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, /*hole*/, 9.9,
           10.10, 11.11, 12.12, 13.13, 14.14];

%PrepareFunctionForOptimization(foo);
assertEquals(undefined, foo(arr, 42)[1]);

%OptimizeFunctionOnNextCall(foo);
assertEquals(undefined, foo(arr, 42)[1]);

// We'll now trigger a deopt.
assertEquals(undefined, foo(arr, 1000000000)[1]);
