// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function bar(arr, x) {
  // Loading a HoleyFloat64
  let v1 = arr[0];

  // An empty loop just so that the FrameState includes {v1} (otherwise, the
  // function has a single FrameState from before `v1 = arr[0]` is even
  // computed, and the interpreter recomputes it on deopt).
  for (let i = 0; i < 10; i++) {}

  // This addition will have Smi feedback, and we'll make it deopt by overflow
  // Smi range. {v1} will be passed in the FrameState. If it's marked as Float64
  // MachineRepresentation in the FrameState, then the deoptimizer will
  // materialize a NaN HeapNumber for it, which is wrong. It should instead have
  // HoleyFloat64 MachineRepresentation, and be materialized by the deoptimizer
  // as Undefined.
  let v2 = x * 10;

  // We mainly care about returning {v} here, but we're also returning the other
  // things so that they aren't dead and optimized out.
  return [ v1, v2 ];
}

let arr = [/*hole*/, 1.1, 2.2, 3.3];

%PrepareFunctionForOptimization(bar);
assertEquals(undefined, bar(arr, 42)[0]);

%OptimizeFunctionOnNextCall(bar);
assertEquals(undefined, bar(arr, 42)[0]);
// We'll now trigger a deopt.
assertEquals(undefined, bar(arr, 1000000000)[0]);
