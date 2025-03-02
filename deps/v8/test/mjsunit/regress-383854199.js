// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --invocation-count-for-osr=1

// Create a Int32 heap slot in script context.
let a;
function write(i) {
  a = 42 + i - i;
}
%PrepareFunctionForOptimization(write);
write(0x7fff0000);
%OptimizeFunctionOnNextCall(write);
write(0x7fff0000);

// Force an OSR read (non-function-context-specialization)
// of the value.
%NeverOptimizeFunction(assertEquals);
for (let i = 0; i < 5000; i++) {
  assertEquals(42, a);
}
