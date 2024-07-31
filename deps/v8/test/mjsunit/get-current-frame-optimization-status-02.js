// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sparkplug --no-always-sparkplug --maglev --turbofan
// Flags: --no-always-turbofan --allow-natives-syntax --concurrent-recompilation

// Note: OSR from Maglev to Turbofan requires --concurrent-recompilation.

const topLevel = %GetFunctionForCurrentFrame();
let status = %GetOptimizationStatus(topLevel);
assertTrue(topFrameIsInterpreted(status));
assertFalse(topFrameIsBaseline(status));
assertFalse(topFrameIsMaglevved(status));
assertFalse(topFrameIsTurboFanned(status));

// Need a back edge for OSR to happen:
for (let i = 0; i < 1; ++i) {
  if (i == 0) {
    %BaselineOsr();
  }
}

status = %GetOptimizationStatus(topLevel);
assertFalse(topFrameIsInterpreted(status));
assertTrue(topFrameIsBaseline(status));
assertFalse(topFrameIsMaglevved(status));
assertFalse(topFrameIsTurboFanned(status));

%PrepareFunctionForOptimization(topLevel);

let gotMaglevved = false;
let gotTurbofanned = false;
for (let i = 0; ; ++i) {
  if (i == 1 || i == 2) {
    %OptimizeOsr();
  }

  status = %GetOptimizationStatus(topLevel);
  const isMaglevved = topFrameIsMaglevved(status);
  const isTurbofanned = topFrameIsTurboFanned(status);
  // The test will opt, deopt and opt again. We assert below that we reach the
  // top tiers at some point.
  gotMaglevved = gotMaglevved || isMaglevved;
  gotTurbofanned = gotTurbofanned || isTurbofanned;
  if (gotTurbofanned) {
    // This will deopt but it's fine, the test is over.
    break;
  }
}

assertTrue(gotMaglevved);
assertTrue(gotTurbofanned);
