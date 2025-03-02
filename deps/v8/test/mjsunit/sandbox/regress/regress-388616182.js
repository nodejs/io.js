// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing --allow-natives-syntax

// Radix value doesn't matter for this test but must be valid.
const radix = 3;

// Test toString conversion with a valid value.
%DoubleToStringWithRadix(12.34, radix);

// Test toString conversion with invalid/special values. These should not cause
// any memory corruption but may cause program termination, so we shuffle them.
let specialValues = [0.0, -0.0, NaN, Infinity, -Infinity];
shuffleArray(specialValues);

for (let value of specialValues) {
  %DoubleToStringWithRadix(value, radix);
}

// Finally, also check that invalid radix values don't cause memory corruption.
%DoubleToStringWithRadix(12.34, 0);
%DoubleToStringWithRadix(12.34, 1);
%DoubleToStringWithRadix(12.34, 2);
%DoubleToStringWithRadix(12.34, 50);
%DoubleToStringWithRadix(12.34, 100);

function shuffleArray(array) {
  for (let i = array.length - 1; i >= 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [array[i], array[j]] = [array[j], array[i]];
  }
}
