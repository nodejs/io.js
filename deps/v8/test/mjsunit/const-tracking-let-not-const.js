// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let a = 0;
a = 1;  // Not a constant any more.

function read() {
  return a;
}

%PrepareFunctionForOptimization(read);
assertEquals(1, read());
%OptimizeFunctionOnNextCall(read);
assertEquals(1, read());
assertOptimized(read);

a = 2;

// `read` is not deopted because `a` was not embedded as a constant in it.
assertEquals(2, read());
assertOptimized(read);
