// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let a = 0;

function read() {
  return a;
}

%PrepareFunctionForOptimization(read);
assertEquals(0, read());

%OptimizeFunctionOnNextCall(read);
assertEquals(0, read());
assertOptimized(read);

function write(newValue) {
  a = newValue;
}
%PrepareFunctionForOptimization(write);
write(0);

%OptimizeMaglevOnNextCall(write);
write(0);

// Invalidate the constness from the outside (of `write`).
a = 3;

assertUnoptimized(read);
assertEquals(3, read());

// Write was also deoptimized, because it depends on `a` constness.
assertUnoptimized(write);

%OptimizeMaglevOnNextCall(write);
write(1);
// Now it depends on Sminess of the context slot.
assertOptimized(write);

assertEquals(1, read());
