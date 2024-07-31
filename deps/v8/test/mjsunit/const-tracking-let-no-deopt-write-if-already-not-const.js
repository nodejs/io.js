// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let a = 1;
a = 2;  // Not a constant any more.

function write(newA) {
  a = newA;
}
%PrepareFunctionForOptimization(write);
write(2);
%OptimizeFunctionOnNextCall(write);
write(2);

// `a` was no longer a constant when optimizing `write`, so we generate simple
// code without side data checking / invalidation.
assertOptimized(write);

write(3);
assertOptimized(write);

function read() {
  return a;
}
%PrepareFunctionForOptimization(read);
%OptimizeFunctionOnNextCall(read);

assertEquals(3, read());
assertOptimized(read);

write(4);
assertOptimized(write);
assertOptimized(read);

// Calling `write` also never deopts `read` since the value was not
// embedded as a constant.
