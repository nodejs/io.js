// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

// An unconditional deopt will be inserted after {o} has been created, but
// before it is initialized with "arg + 1.5" and "arg + 41.5". These 2 fields
// are thus initialized with the Float64 hole. If {o} is escape-analyzed away,
// then the deopt state will have 2 identical HeapNumbers as input. These
// shouldn't be GVNed, because they are mutable.
function foo(arg) {
  var o = {
    x: arg + 1.5,
    y: arg + 41.5,
  };
  return o.x + o.y;
}

%PrepareFunctionForOptimization(foo);
assertEquals(NaN, foo());
assertEquals(NaN, foo());

%OptimizeFunctionOnNextCall(foo);
assertEquals(45, foo(1));
