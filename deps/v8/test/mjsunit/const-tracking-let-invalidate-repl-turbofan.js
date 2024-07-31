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

// write() uses the StaGlobal bytecode for writing the top-level `let` variable.
%RuntimeEvaluateREPL('function write(newA) { a = newA; }');

%PrepareFunctionForOptimization(write);

// Write the same value. This won't invalidate the constness.
write(0);

%OptimizeFunctionOnNextCall(write);
write(0);

assertEquals(0, read());
assertOptimized(read);

write(1);

assertUnoptimized(read);
assertEquals(1, read());
