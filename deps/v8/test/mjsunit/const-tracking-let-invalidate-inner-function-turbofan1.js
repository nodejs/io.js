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
assertEquals(1, read());

%OptimizeFunctionOnNextCall(read);
assertEquals(1, read());
assertOptimized(read);

let write;
function outer() {
  // Force this function to have a function context.
  let b = 0;
  function inner(newA) {
    a = newA;
    return b;
  }
  write = inner;
}
outer();

%PrepareFunctionForOptimization(write);

// Write the same value. This won't invalidate the constness.
write(1);
%OptimizeFunctionOnNextCall(write);
write(1);
// TODO(v8:13567): This will deopt immediately, since we don't have the "if the
// value is the same, don't deopt" check yet.
// assertOptimized(write);

// This deopts `write`, then invalidates the constness which deopts `read`.
write(2);

assertUnoptimized(write);
assertUnoptimized(read);
assertEquals(2, read());
