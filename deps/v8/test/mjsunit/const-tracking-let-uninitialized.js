// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let myConstant;  // Uninitialized!

function foo() {
  return myConstant;
}

%PrepareFunctionForOptimization(foo);
assertEquals(undefined, foo());

%OptimizeFunctionOnNextCall(foo);
assertEquals(undefined, foo());
assertOptimized(foo);

// Change myConstant.
myConstant = 43;
assertUnoptimized(foo);
assertEquals(43, foo());
