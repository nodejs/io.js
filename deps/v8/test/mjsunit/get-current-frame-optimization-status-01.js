// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sparkplug --no-always-sparkplug --maglev --turbofan
// Flags: --no-always-turbofan --allow-natives-syntax

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

assertTrue(topFrameIsInterpreted(status));
assertFalse(topFrameIsBaseline(status));
assertFalse(topFrameIsMaglevved(status));
assertFalse(topFrameIsTurboFanned(status));

%PrepareFunctionForOptimization(topLevel);

let gotMaglevved = false;
for (let i = 0; i < 3; ++i) {
  if (i == 1) {
    %OptimizeOsr();
  }
  // We cannot use isMaglevved(topLevel) because the OSRed code is not
  // installed in the function.
  const status = %GetOptimizationStatus(topLevel);
  if (topFrameIsMaglevved(status)) {
    // This will deopt but it's fine, the test is over.
    gotMaglevved = true;
    break;
  }
}
assertTrue(gotMaglevved);
