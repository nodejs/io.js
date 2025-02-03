// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --concurrent-recompilation

function g(x) {
  assertEquals(42.42, x);
}
%NeverOptimizeFunction(g);

function test_osr(x, y) {
  let s = 0;
  let loop_is_turbofan = false;
  for (let i = 0; i < 20; i++) {
    %OptimizeOsr();
    s += i;
    loop_is_turbofan = %CurrentFrameIsTurbofan();
  }

  // This multiplication will trigger a deopt (because it has no feedback).
  s *= x;

  // The loop should have been in Turbofan in its last few iterations.
  assertTrue(loop_is_turbofan);

  // We shouldn't be in Turbofan anymore
  assertFalse(%CurrentFrameIsTurbofan());
  assertFalse(%ActiveTierIsTurbofan(test_osr));

  // Keeping the parameters alive, just to make sure that everything works.
  g(y);

  return s;
}

%PrepareFunctionForOptimization(test_osr);
assertEquals(570, test_osr(3, 42.42));
