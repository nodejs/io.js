// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

let x = 0;    // Initial value. It doesn't matter...
x = -12.65;   // Transition to kMutableHeapNumber.

function write(a) {
  x = a;
}

// Get context slot mutable number reference if leaked.
let y = x;

// Update mutable heap number.
%PrepareFunctionForOptimization(write);
write(2.2);
write(-7.8);
%OptimizeFunctionOnNextCall(write);
write(2.2);

assertEquals(x, 2.2);
assertEquals(y, -12.65);
