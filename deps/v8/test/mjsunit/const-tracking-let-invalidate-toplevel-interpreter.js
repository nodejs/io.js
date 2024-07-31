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

%PrepareFunctionForOptimization(read);
read();

%OptimizeFunctionOnNextCall(read);
assertEquals(1, read());
assertOptimized(read);

// Change what `a` is.
a = 2;

assertUnoptimized(read);
assertEquals(2, read());
