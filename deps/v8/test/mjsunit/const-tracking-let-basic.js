// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

class A {}

var origA = A;

function create() {
  return new A();
}

%PrepareFunctionForOptimization(create);
create();

%OptimizeFunctionOnNextCall(create);
var o1 = create();
assertOptimized(create);
assertTrue(o1 instanceof A);

// Change what A is.
var newACalled = false;
A = function() { newACalled = true; }

assertUnoptimized(create);
var o2 = create();
assertFalse(o2 instanceof origA);
assertTrue(o2 instanceof A);
assertTrue(newACalled);
