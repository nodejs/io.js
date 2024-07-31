// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

function read() {
  // This will assert that the value initialization was tracked correctly.
  a++;
}

// Tier up to Maglev.
const topLevel = %GetFunctionForCurrentFrame();
%PrepareFunctionForOptimization(topLevel);

// Need a back edge for OSR to happen:
for (let i = 0; i < 3; ++i) {
  if (i == 1) {
    %OptimizeOsr();
  }
}

// This is handled in Maglev code.
let a = 8;
read();
