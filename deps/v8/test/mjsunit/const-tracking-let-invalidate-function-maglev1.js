// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let a = 1;

function read() {
  return a;
}

// In this variant, `read` is optimized before `write`.
%PrepareFunctionForOptimization(read);
read();

%OptimizeFunctionOnNextCall(read);
assertEquals(1, read());
assertOptimized(read);

function write(newA) {
  a = newA;
}

%PrepareFunctionForOptimization(write);
write(1);
%OptimizeMaglevOnNextCall(write);

// Write the same value. This won't invalidate the constness.
write(1);

assertOptimized(read);

// This deopts `write`, then invalidates the constness which deopts `read`.
write(2);

assertUnoptimized(write);
assertUnoptimized(read);
assertEquals(2, read());
