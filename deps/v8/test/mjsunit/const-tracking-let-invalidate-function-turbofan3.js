// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

// In this variant, the variable doesn't have an initializer.
let a;

function read() {
  return a;
}

function write(newA) {
  a = newA;
}

%PrepareFunctionForOptimization(write);

// Write the same value. This won't invalidate the constness.
write(undefined);
%OptimizeFunctionOnNextCall(write);
write(undefined);

// TODO(v8:13567): The above will deopt `write` immediately, since we don't have
// the "if the value is the same, don't deopt" check yet.
// assertOptimized(write);

%PrepareFunctionForOptimization(read);
assertEquals(undefined, read());

%OptimizeFunctionOnNextCall(read);
assertEquals(undefined, read());
assertOptimized(read);

// This invalidates the constness which deopts `read`.
write(2);

assertUnoptimized(write);
assertUnoptimized(read);
assertEquals(2, read());
